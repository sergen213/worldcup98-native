// Derived from fifa99-native (same author), ported for World Cup 98
// Players: movement parameters (0x4BBBF8 with the runtime tables of 0x4CFF94), attribute build
// from a real FCDB player (0x478B08 + FE expansion 0x5202B0, WC98 nibble layout per
// SIM_PORT_NOTES.md section 4), motion predictor 0x4CBA60, fatigue 0x479028. Expected values are
// hand-computed from the disassembly (see player.cpp comments).
#include <iterator>
#include <memory>

#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"

using namespace wc98::sim;

int main() {
    auto data = std::make_unique<MatchData>();
    auto w = std::make_unique<World>();
    w->data = data.get();
    w->pitch = Pitch::make(2);
    w->opt.fatigueInjuries = true;

    // Synthetic ratings: speed A[0x3A] = 9, acceleration A[0x2F] = 10, agility 11, A[0x32] = 5.
    PlayerAttr& a = w->attrs[0];
    a.at(0x3a) = 9;
    a.at(0x2f) = 10;
    a.at(0x2a) = 11;
    a.at(0x32) = 5;
    a.at(0x3d) = 8;
    a.at(0x3e) = 0;
    a.at(0x33) = 8;
    a.stamina18 = 8;
    PlayerState& p = w->players[0];
    p.attr = 0;
    p.slot = 5;
    p.team = 0;
    Player_initMoveParams(*w, 0);
    CHECK(p.runMax == 0x6fffe);      // runtime 0x590590[9] = (7.5 - 6.75) * 5/15 + 6.75
    CHECK(p.speedCap == p.runMax);
    CHECK(p.sprintMax == 0x6fffe + 0x21caa); // + runtime 0x5905E0[10]
    CHECK(p.accel == 0x4000 && p.decel == 0x4000); // 0x590540[10]
    CHECK(p.accelRecip == 0x40000 && p.decelRecip == 0x40000);
    CHECK(p.sprintAccel == 0x10e55 && p.sprintAccelRecip == 0xf26d);
    CHECK(p.turnAgility == 2);
    CHECK(p.mB0 == 0xa800);
    CHECK(p.anim.pitchLimit == 20);
    a.at(0x40) |= 1; // injured: run speed 3.0
    Player_initMoveParams(*w, 0);
    CHECK(p.runMax == 0x30000);
    a.at(0x40) = 0;
    Player_initMoveParams(*w, 0);

    // Kinematic predictor: from rest, 30 frames at run speed: 28 frames accelerating then cruise.
    p.speed = 0;
    p.turnClass = 0;
    p.facingDesired = 0;
    CHECK(Motion_predictDistance(*w, 0, 0xc0000, 0, 30, 0, 0) == 111);
    CHECK(Motion_predictDistance(*w, 0, 0xc0000, 0, 10, 0, 0) == 12); // still accelerating
    p.speed = 0x40000;
    p.facingDesired = 0x100; // quarter turn: (0x100 >> 5) * (speed >> 18) = 8 frames lost
    CHECK(Motion_predictDistance(*w, 0, 0xc0000, 0, 30, 0, 0) == 135);
    p.speed = 0x40000;
    p.facingDesired = 0;
    CHECK(Motion_predictDistance(*w, 0, 0x40000, 0, 8, 0, 0) == 32); // constant speed 4.0

    // Fatigue: (2.0 * 5 / 45) * 1.5 = 0x5555 per tick; the 4th tick drops the speed rating.
    w->match.durationSec = 240;
    for (int i = 0; i < 3; ++i) CHECK(!PlayerAttr_fatigueTick(*w, a, true));
    CHECK(a.fatigueSpeed == 0xffff);
    CHECK(PlayerAttr_fatigueTick(*w, a, true));
    CHECK(a.at(0x3a) == 8 && a.at(0x3f) == 1 && a.fatigueSpeed == 0x5554);
    CHECK(PlayerAttr_fatigueTick(*w, a, true)); // stamina accumulator passes 1.5
    CHECK(a.stamina18 == 7 && a.at(0x3e) == 1);
    PlayerAttr b{};
    b.at(0x33) = 8;
    CHECK(!PlayerAttr_fatigueTick(*w, b, false) && b.fatigueSpeed == 0x4444); // k = 1.2

    // Attribute record from real FCDB players (record 0 and 100 of COMMON/FCDB.DBI).
    const auto dir = treeDirOrSkip();
    const auto db = wc98::formats::FcdbDatabase::parse(wc98::readFile(dir / "COMMON/FCDB.DBI"));
    data->sheets[0].squad = {db.players[0], db.players[100]};
    Team_buildMatchPlayerAttrs(*w, 0, true);
    // Hand-decoded from the raw 20-byte records (bytes 8..16 skill nibbles -> expandFcdbPlayer
    // name map -> 0x478B08 copy map -> clamp 3..19; A[0x36] = derived speed class, unclamped).
    const int exp0[] = {12, 10, 11, 9, 13, 9, 14, 11, 12, 11, 11, 10, 8, 9, 3, 10, 8, 14, 12, -1, 12, 10};
    const int exp1[] = {3, 8, 9, 6, 8, 7, 10, 10, 5, 4, 9, 5, 10, 7, 2, 11, 9, 14, 9, -1, 9, 5};
    for (int off = 0x28; off <= 0x3d; ++off) {
        if (off == 0x3b) continue;
        CHECK(w->attrs[0].at(off) == exp0[off - 0x28]);
        CHECK(w->attrs[1].at(off) == exp1[off - 0x28]);
    }
    CHECK(w->attrs[0].stamina18 == 10 && w->attrs[1].stamina18 == 5);
    CHECK(w->attrs[1].squadIndex == 1);
    return 0;
}
