// Derived from fifa99-native (same author), ported for World Cup 98
// Team / formation module checks against hand-computed exe arithmetic (0x4CF230, 0x4CF678, 0x451860).
// WC98 adaptation (SIM_PORT_NOTES.md section 6): the slot-target expectations are computed in-test
// from the loaded G98PL bytes with an independent fx replica (the FIFA 99 hard-coded values come
// from G99PL, whose contents differ); G99DF is absent in WC98 so the mentality lerp degenerates.
#include "check.hpp"

#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"

using namespace wc98::sim;

namespace {

World makeWorld() {
    World w;
    w.pitch = Pitch::make(2); // 3648 x 5088
    Formation_initZoneScales(w);
    for (int t = 0; t < 2; ++t) {
        w.teams[t].index = t;
        w.teams[t].opponent = 1 - t;
        w.teams[t].side = t;
        for (int i = 0; i < 11; ++i) {
            w.teams[t].players[i] = t * 11 + i;
            w.players[t * 11 + i].team = t;
            w.players[t * 11 + i].index = i;
        }
    }
    return w;
}

Vec3 at(int x, int z) { return {x * 65536, 0, z * 65536}; }

void ballZone() {
    World w = makeWorld();
    CHECK(w.formation.colPerUnit == 197 && w.formation.rowPerUnit == 270); // (11<<16)/3648, (21<<16)/5088
    CHECK(w.formation.cellW == 331 && w.formation.cellL == 242);
    std::int32_t z[2];
    Formation_ballZone(w, at(0, 0), z, 0);
    CHECK(z[0] == 149 && z[1] == 149); // centre: col 6, row 11
    Formation_ballZone(w, at(1000, -2000), z, 0);
    CHECK(z[0] == 48 && z[1] == 250); // col 9, row 3; mirrored 298 - zone
    // Inside the 0x50 hysteresis margin of cell (9, 3): kept.
    Formation_ballZone(w, at(1200, -1800), z, 1);
    CHECK(z[0] == 48);
    // Same point without hysteresis: col 10, row 4.
    Formation_ballZone(w, at(1200, -1800), z, 0);
    CHECK(z[0] == 62);
    // Leaving the margin with hysteresis on recomputes.
    Formation_ballZone(w, at(1200, -1800), z, 0);
    Formation_ballZone(w, at(-1800, 2600), z, 1);
    CHECK(z[0] == 287); // col 1, row 22
    Formation_ballZone(w, at(5000, -9000), z, 0);
    CHECK(z[0] == 12 && z[1] == 286);
}

void slotTarget(const MatchData& data) {
    World w = makeWorld();
    w.data = &data;
    w.match.phase = 9;
    CHECK(w.formation.scaleX == 2530514 && w.formation.scaleZ == 1983328); // 0x4CE5EC for 3648 x 5088
    for (int t = 0; t < 2; ++t) {
        std::uint8_t* rec = w.teams[t].rec(0);
        const std::uint8_t desc[7] = {1, 0, 4, 5, 1, 0, 0};
        for (int i = 0; i < 7; ++i) rec[1 + i] = desc[i];
        Formation_selectTeamFormation(w, t, 0);
    }
    PlayerState& p = w.players[0];
    p.curSlot = 12;
    p.role = 4;
    Vec3 o;
    // G98PL slot 12 zone 149 {ax,az,dx,dz}; the record leaves slotOff[12] zero (only slots
    // 8..10 get wide-midfield offsets from d[5]=1, d[3]=5) and the slot-record nudges rec[1]/rec[2]
    // are zero, so this is a pure fx scale of the raw byte. The replica below is independent of
    // formation.cpp's fx/table indexing, catching layout and sign bugs against the WC98 table.
    const auto fxr = [](std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b + 0x8000) >> 16); };
    const auto shl16 = [](std::int32_t v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << 16) >> 7; };
    const std::int8_t* tab = data.formations.pl.data() + (12 * 299 + 149) * 4;
    // Not in possession: defend (dx, dz) scaled, rounded fx.
    Formation_slotPosition(w, 0, o, 149, 0x40);
    CHECK(o.x == fxr(tab[2], w.formation.scaleX) && o.z == fxr(tab[3], w.formation.scaleZ) && o.y == 0);
    // In possession: attack (ax, az) lerped toward defend by val/128 of the delta.
    w.teams[0].flags |= 2;
    Formation_slotPosition(w, 0, o, 149, 0x40);
    {
        const std::int32_t ax = fxr(tab[0], w.formation.scaleX), az = fxr(tab[1], w.formation.scaleZ);
        CHECK(o.x == ax + fxr(0x40, shl16(fxr(tab[2], w.formation.scaleX) - ax)));
        CHECK(o.z == az + fxr(0x40, shl16(fxr(tab[3], w.formation.scaleZ) - az)));
    }
    // Side 1 mirrors x and z.
    w.teams[0].flags = 0;
    w.teams[0].side = 1;
    Formation_slotPosition(w, 0, o, 149, 0x40);
    CHECK(o.x == -fxr(tab[2], w.formation.scaleX) && o.z == -fxr(tab[3], w.formation.scaleZ));
    // Mentality lerp (0x4CF5F8): WC98 GEN4TBLS.VIV has no G99DF member, so the df table is all
    // zero and the blend returns endpoint b exactly (FIFA 99 mixed 35/128 toward a).
    w.teams[0].mentality = 2;
    Vec3 r{1, 1, 1};
    Formation_lerpByMentality(w, r, 0, Vec3{1280, 0, 0}, Vec3{128, 0, -9});
    CHECK(r.x == 128 && r.z == -9);
}

int mentality(World& w, int diff, int remaining, int landingZ, int possessor) {
    w.match.score[0] = diff > 0 ? diff : 0;
    w.match.score[1] = diff < 0 ? -diff : 0;
    w.match.periodType = 1;
    w.match.durationSec = 1000;
    w.match.elapsedSec = 1000 - remaining;
    w.ball.landing.z = landingZ;
    w.ball.possessor = possessor;
    w.teams[0].mentality = 2;
    w.teams[0].flags = 0;
    w.teams[0].f1b4 = -1;
    Team_autoMentality(w, 0);
    return w.teams[0].mentality;
}

void autoMentality() {
    World w = makeWorld(); // team 0 attacks +z (side 0), thirdLineZ = -848 / +848
    CHECK(mentality(w, -1, 100, 0, -1) == 0);    // behind, < 0x4B0 s left
    CHECK(mentality(w, -3, 2000, 100, -1) == 1); // behind by 3, ball in the +z half
    CHECK(mentality(w, -3, 2000, -100, -1) == 4);
    CHECK(mentality(w, 2, 100, 0, -1) == 3);    // ahead by 2 late
    CHECK(mentality(w, 1, 100, 0, -1) == 4);    // ahead by 1 late
    CHECK(mentality(w, 0, 2000, 2000, -1) == 1);
    CHECK(mentality(w, 0, 2000, -2000, -1) == 2);
    CHECK(mentality(w, 0, 2000, -2000, 11) == 3); // opponent has the ball
    w.teams[0].humans = 1;                         // human team without AI management: untouched
    CHECK(mentality(w, -1, 100, 0, -1) == 2);
}

} // namespace

int main() {
    ballZone();
    autoMentality();
    const auto dir = treeDirOrSkip();
    MatchData data;
    CHECK(loadFormationTables(data, dir));
    slotTarget(data);
    return 0;
}
