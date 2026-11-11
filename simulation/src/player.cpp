// Player objects: layout checks, match attribute records, movement parameters, fatigue, injuries,
// state machine entry and per-team player update (Players agent). Exe addresses in comments.
#include <cstddef>
#include <cstdio>

#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"

namespace wc98::sim {

static_assert(sizeof(PlayerAttr) == 0x44);
static_assert(sizeof(KickRequest) == 0x58);
static_assert(sizeof(AnimCtl) == 0x54);
static_assert(sizeof(PlayerState) == 0x26C);
static_assert(sizeof(ControllerState) == 0x1A4 + 4);
static_assert(offsetof(PlayerAttr, stamina18) == 0x18);
static_assert(offsetof(PlayerAttr, r) == 0x28);
static_assert(offsetof(PlayerState, anim) == 0x64);
static_assert(offsetof(PlayerState, pos) == 0xBC);
static_assert(offsetof(PlayerState, posI) == 0xD4);
static_assert(offsetof(PlayerState, target) == 0xEC);
static_assert(offsetof(PlayerState, distBall) == 0x11C);
static_assert(offsetof(PlayerState, facing) == 0x130);
static_assert(offsetof(PlayerState, speed) == 0x14C);
static_assert(offsetof(PlayerState, decelRecip) == 0x180);
static_assert(offsetof(PlayerState, kick) == 0x190);
static_assert(offsetof(PlayerState, kickPower) == 0x1EC);
static_assert(offsetof(PlayerState, runTarget) == 0x208);
static_assert(offsetof(PlayerState, flags22c) == 0x22C);
static_assert(offsetof(ControllerState, player) == 0x19C);
static_assert(offsetof(KickRequest, kind) == 0x50);

namespace {

// FUN_004CFEF0(EAX = table, EDX = top, EBX = scale): t[19] = top, t[0..4] = fx(top, scale),
// t[i] = (t[19] - t[4]) * (i - 4) / 15 + t[4] for i = 5..18 (idiv). FUN_004CFF94 rebuilds the
// three rating tables with it at every Match_init (0x4547C7), so the static exe contents of
// 0x590590 / 0x5908B0 / 0x5905E0 are never used in a match.
struct RatingTable {
    Fixed v[20];
};
constexpr RatingTable buildRatingTable(Fixed top, Fixed scale) {
    RatingTable t{};
    t.v[19] = top;
    t.v[4] = fx(top, scale);
    for (int i = 0; i < 4; ++i) t.v[i] = t.v[4];
    for (int i = 5; i < 19; ++i) t.v[i] = (t.v[19] - t.v[4]) * (i - 4) / 15 + t.v[4];
    return t;
}
constexpr RatingTable kRunSpeed = buildRatingTable(0x78000, 0xe666);    // 0x590590 (0x4CFFAA)
constexpr RatingTable kKeeperSpeed = buildRatingTable(0x80000, 0xc000); // 0x5908B0 (0x4CFFBE)
constexpr RatingTable kSprintBonus = buildRatingTable(0x26666, 0xcccc); // 0x5905E0 (0x4CFFD2)

// exe 0x590540, 20 x fx: acceleration / deceleration by A[0x2F].
constexpr Fixed kAccel[20] = {
    0x2ccc, 0x2ccc, 0x2ccc, 0x2ccc, 0x2ccc, 0x2ccc, 0x2ccc, 0x3333, 0x3333, 0x3999, 0x4000, 0x4666,
    0x4ccc, 0x5333, 0x5999, 0x6000, 0x6666, 0x6ccc, 0x7333, 0x7999,
};
// exe 0x590810, 20 x fx: fatigue rate by A[0x33] (flat 2.0).
constexpr Fixed kFatigueRate[20] = {
    0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000,
    0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000,
};

// The 17 WC98 (1998 schema) skill nibbles in kFcdbAttributeNames1998 record order.
enum Wc98Skill {
    kAggression, kAcceleration, kAttackBias, kAgility, kBallControl, kAwareness, kFitness, kCreativity,
    kPassing, kHeading, kReaction, kPassBias, kShotPower, kShotBias, kSpeed, kShotAccuracy, kTackle};

// FUN_005202B0 (front end, FCDB path: no edited-player override) expands one FCDB player record
// into the 0x3E-byte struct Team_buildMatchPlayerAttrs reads. Only the bytes the match reads are
// produced. WC98 adaptation (SIM_PORT_NOTES.md section 4): the 1998 record is byte-packed, so the
// nibbles/role come from the parsed FcdbPlayer fields (fifa98edit layout); each FIFA 99 byte slot is
// filled by matching the attribute NAME to its WC98 counterpart. The 1998 record has no speed
// class / alternative position field; both are derived as documented in SIM_PORT_NOTES.md.
struct DbPlayer {
    std::uint8_t b[0x3e + 2] = {};
};
DbPlayer expandFcdbPlayer(const formats::FcdbPlayer& p) {
    const auto& a = p.attributes; // raw skill nibbles 0..15 (game value = n*4+39, unused here: the
                                  // match consumes the same 0..15 nibble scale FIFA 99 read from w2/w3/w4)
    DbPlayer d;
    d.b[0x0a] = static_cast<std::uint8_t>(a[kSpeed] >= 12 ? 3 : a[kSpeed] >= 8 ? 2 : a[kSpeed] >= 4 ? 1 : 0); // anim rate class (FIFA 99 w4 bits 30..31)
    d.b[0x27] = p.overall;                              // squad.cpp best-rating comparator (WC98: 39..99, monotonic)
    d.b[0x11] = p.role;                                 // fifa98edit roles[] index, GK = 0 (FIFA 99 position code space)
    d.b[0x12] = p.role;                                 // the 1998 schema has no alternative position
    d.b[0x24] = a[kPassBias];                           // -> A[0x28] (no sim consumer observed)
    d.b[0x25] = a[kShotBias];                           // -> A[0x29] (no sim consumer observed)
    d.b[0x17] = a[kAttackBias];                         // -> A[0x2e] set-piece targeting (FIFA 99 "hidden2")
    d.b[0x23] = a[kAggression];                         // -> A[0x30]
    d.b[0x15] = a[kAcceleration];                       // -> A[0x2f]
    d.b[0x16] = a[kAgility];                            // -> A[0x2a]
    d.b[0x18] = a[kPassing];                            // -> A[0x31] pass evaluator (team_ai_ball)
    d.b[0x19] = a[kBallControl];                        // -> A[0x32]
    d.b[0x1a] = a[kCreativity];                         // -> A[0x2b]
    d.b[0x1b] = a[kFitness];                            // -> A[0x33], stamina18, A[0x3d]
    d.b[0x1c] = a[kHeading];                            // -> A[0x34]
    d.b[0x1d] = a[kAwareness];                          // -> A[0x2c] (FIFA 99 "hidden8", no sim consumer observed)
    d.b[0x1e] = a[kReaction];                           // -> A[0x35] reaction countdown frames
    d.b[0x1f] = a[kShotPower];                          // -> A[0x38]
    d.b[0x20] = a[kShotAccuracy];                       // -> A[0x37]
    d.b[0x21] = a[kSpeed];                              // -> A[0x3a], A[0x3c]
    d.b[0x22] = a[kTackle];                             // -> A[0x2d]
    return d;
}

PlayerAttr& attrOf(World& w, int pid) { return w.attrs[w.players[pid].attr]; }
const PlayerAttr& attrOf(const World& w, int pid) { return w.attrs[w.players[pid].attr]; }

} // namespace

std::uint8_t Player_attr(const World& w, int pid, int off) { return attrOf(w, pid).at(off); }

// Byte of the expanded DB player 0x65EF60[t*0x4D + squad] (FUN_005202B0 FCDB path).
std::uint8_t Player_dbByte(const World& w, int team, int squad, int off) {
    return expandFcdbPlayer(w.data->sheets[team].squad[static_cast<std::size_t>(squad)]).b[off];
}

// 0x478520: ratings +0x28..+0x35, +0x37..+0x3A, +0x3C and u16 +0x18 to 3..19; +0x3D likewise.
void PlayerAttr_clamp(PlayerAttr& a) {
    auto c = [](std::uint8_t& v) { v = v < 3 ? 3 : (v > 0x13 ? 0x13 : v); };
    for (int off = 0x28; off <= 0x35; ++off) c(a.at(off));
    for (int off = 0x37; off <= 0x3a; ++off) c(a.at(off));
    c(a.at(0x3c));
    a.stamina18 = a.stamina18 < 3 ? 3 : (a.stamina18 > 0x13 ? 0x13 : a.stamina18);
    c(a.at(0x3d));
}

// 0x478B08 (EAX = team, EDX = fresh): one 0x44 record per squad member (0x65F044[t*0x4D]) from
// the expanded DB player (0x65EF60[t*0x4D + i]). The season status struct 0x65EFB8 (+0xA, +0xB:
// suspension / injury carried from career mode, 0x4A7B10) is not part of MatchData: zero.
void Team_buildMatchPlayerAttrs(World& w, int team, bool fresh) {
    const TeamSheet& sheet = w.data->sheets[team];
    const int n = static_cast<int>(sheet.squad.size() < kSquadMax ? sheet.squad.size() : kSquadMax);
    for (int i = 0; i < n; ++i) {
        PlayerAttr& a = w.attrs[team * kSquadMax + i];
        const DbPlayer d = expandFcdbPlayer(sheet.squad[i]);
        a.squadIndex = i;
        a.at(0x28) = d.b[0x24];
        a.at(0x29) = d.b[0x25];
        a.at(0x2a) = d.b[0x16];
        a.at(0x2b) = d.b[0x1a];
        a.at(0x2c) = d.b[0x1d];
        a.at(0x2d) = d.b[0x22];
        a.at(0x2e) = d.b[0x17];
        a.at(0x30) = d.b[0x23];
        a.at(0x2f) = d.b[0x15];
        a.at(0x31) = d.b[0x18];
        a.at(0x32) = d.b[0x19];
        a.at(0x33) = d.b[0x1b];
        a.at(0x34) = d.b[0x1c];
        a.at(0x35) = d.b[0x1e];
        a.at(0x36) = d.b[0x0a];
        a.at(0x37) = d.b[0x20];
        a.at(0x39) = 0x0e;
        a.at(0x38) = d.b[0x1f];
        a.at(0x3a) = d.b[0x21];
        a.at(0x3c) = d.b[0x21];
        a.stamina18 = d.b[0x1b];
        a.at(0x3d) = d.b[0x1b];
        if (fresh) {
            a.at(0x3e) = 0;
            a.at(0x3f) = 0;
            const std::uint8_t injured = 0, suspended = 0; // 0x65EFB8 struct +0xB / +0xA (see above)
            a.at(0x40) = static_cast<std::uint8_t>((a.at(0x40) & 0xfe) | (injured & 1));
            a.at(0x40) = static_cast<std::uint8_t>((a.at(0x40) & 0xdf) | ((suspended & 1) << 5));
        }
        PlayerAttr_clamp(a);
    }
}

// 0x479028 (EAX = record, EDX = rate select). Returns 1 when a rating dropped.
bool PlayerAttr_fatigueTick(World& w, PlayerAttr& a, bool edx) {
    const std::int32_t thr = static_cast<std::int32_t>((static_cast<std::uint32_t>(w.match.durationSec) >> 3) * 0x3c);
    if (!w.opt.fatigueInjuries) {
        a.fatigueFrames = 0;
        return false;
    }
    std::int32_t carry = 0;
    if (thr < a.fatigueFrames) { // jl
        a.fatigueFrames -= thr;
        carry = 0x10000;
    }
    const std::int32_t base = ((kFatigueRate[a.at(0x33)] + carry) * 5) / 0x2d;
    Fixed k = edx ? 0x18000 : 0x13334;
    if (static_cast<std::uint32_t>(w.match.periodIndex) != 0) k += k >> 2; // jbe: extra time +25%
    const Fixed inc = fx(base, k);
    a.fatigueSpeed += inc;
    a.fatigueStamina += inc;
    bool dropped = false;
    if (a.fatigueSpeed > 0x10000 && a.at(0x3a) > 4) {
        --a.at(0x3a);
        a.fatigueSpeed -= 0x10000;
        dropped = true;
        ++a.at(0x3f);
    }
    if (a.fatigueStamina > 0x18000) {
        if (a.stamina18 > 4) {
            --a.stamina18;
            dropped = true;
            a.fatigueStamina -= 0x18000;
        }
        ++a.at(0x3e);
        if (a.at(0x3e) > a.at(0x3d)) a.at(0x3e) = a.at(0x3d);
    }
    return dropped;
}

void PlayerAttr_setInjured(PlayerAttr& a) { // 0x479268
    a.at(0x3a) = 3;
    a.at(0x3c) = 6;
    a.at(0x40) |= 1;
}

Fixed PlayerAttr_runSpeed(const PlayerAttr& a) { // 0x47927C
    return (a.at(0x40) & 1) ? 0x30000 : kRunSpeed.v[a.at(0x3a)];
}

namespace {
int squadCountOf(const World& w, int team) { // 0x65F044 + t*0x134
    const std::size_t n = w.data->sheets[team].squad.size();
    return static_cast<int>(n < kSquadMax ? n : kSquadMax);
}
std::uint8_t addU8(std::uint8_t v, int d) { return static_cast<std::uint8_t>(v + d); }

// 0x47881C (EAX = record, EDX = delta): every rating byte and u16 +0x18 += delta; keeps
// +0x3E <= +0x3D and +0x3F + 4 <= +0x3C, then clamps.
void attrShiftAll(PlayerAttr& a, std::int32_t d) {
    for (int off : {0x28, 0x2a, 0x29, 0x2b, 0x2c, 0x2e, 0x2d, 0x2f, 0x30, 0x32, 0x31, 0x33, 0x34, 0x37, 0x35, 0x38,
                    0x39, 0x3c, 0x3a, 0x3d})
        a.at(off) = addU8(a.at(off), d);
    a.stamina18 = static_cast<std::uint16_t>(a.stamina18 + d);
    if (a.at(0x3e) > a.at(0x3d)) a.at(0x3e) = a.at(0x3d);
    if (static_cast<int>(a.at(0x3f)) + 4 > static_cast<int>(a.at(0x3c))) a.at(0x3f) = addU8(a.at(0x3c), -4);
    PlayerAttr_clamp(a);
}

// 0x4788F4 (EAX = record, EDX = delta): skill bytes += delta (not +0x2E, +0x3A, +0x3C/D, +0x18).
void attrShiftSkills(PlayerAttr& a, std::int32_t d) {
    for (int off : {0x28, 0x29, 0x2b, 0x2a, 0x2c, 0x2d, 0x2f, 0x31, 0x30, 0x32, 0x33, 0x34, 0x37, 0x35, 0x38, 0x39})
        a.at(off) = addU8(a.at(off), d);
    PlayerAttr_clamp(a);
}

// 0x478980: v += (v - 9) >> 1 for every rating and u16 +0x18.
void attrSpread(PlayerAttr& a) {
    auto f = [](int v) { return v + ((v - 9) >> 1); };
    for (int off = 0x28; off <= 0x35; ++off) a.at(off) = static_cast<std::uint8_t>(f(a.at(off)));
    for (int off = 0x37; off <= 0x3a; ++off) a.at(off) = static_cast<std::uint8_t>(f(a.at(off)));
    a.stamina18 = static_cast<std::uint16_t>(f(a.stamina18));
    a.at(0x3c) = static_cast<std::uint8_t>(f(a.at(0x3c)));
    a.at(0x3d) = static_cast<std::uint8_t>(f(a.at(0x3d)));
    PlayerAttr_clamp(a);
}

// 0x478C60 / 0x478CBC (EAX = team, EDX = delta): records whose DB player byte +0x11 (position) != 0.
template <class F> void forOutfieldRecords(World& w, int team, F f) {
    const int n = squadCountOf(w, team);
    for (int i = 0; i < n; ++i)
        if (Player_dbByte(w, team, i, 0x11) != 0) f(w.attrs[team * kSquadMax + i]);
}
} // namespace

void PlayerAttr_raiseSpecial_478CBC(World& w, int team, std::int32_t delta) {
    forOutfieldRecords(w, team, [&](PlayerAttr& a) { attrShiftSkills(a, delta); });
}

// 0x478D18: per record (22) a random form offset -3..3 in +0x3B.
void PlayerAttr_resetTeam(World& w, int team) {
    for (int i = 0; i < kSquadMax; ++i) {
        PlayerAttr& a = w.attrs[team * kSquadMax + i];
        a.at(0x3b) = static_cast<std::uint8_t>(w.rng.next() & 3);
        if (w.rng.next() & 1) a.at(0x3b) = static_cast<std::uint8_t>(-a.at(0x3b));
    }
}

// 0x478F88: per squad record: speed restored, fatigue halved, stamina recomputed.
void PlayerAttr_newPeriod(World& w, int team) {
    const int n = squadCountOf(w, team);
    for (int i = 0; i < n; ++i) {
        PlayerAttr& a = w.attrs[team * kSquadMax + i];
        a.at(0x3a) = a.at(0x3c);
        a.at(0x3e) = static_cast<std::uint8_t>(a.at(0x3e) >> 1);
        a.fatigueFrames = 0;
        a.at(0x3f) = 0;
        a.stamina18 = static_cast<std::uint16_t>(a.at(0x3d) - a.at(0x3e));
        PlayerAttr_clamp(a);
    }
}

// 0x478E60 (EAX = team, EDX, EBX, ECX = spread): rebuild the records and re-apply form (EDX),
// handicap (team stats +0x22C), spread (ECX) and fatigue; EBX adds +2 to outfield players.
void PlayerAttr_applyHandicap(World& w, int team, int edx, int ebx, int ecx) {
    Team_buildMatchPlayerAttrs(w, team, false);
    if (edx != 0) // 0x478D68
        for (int i = 0; i < kSquadMax; ++i) {
            PlayerAttr& a = w.attrs[team * kSquadMax + i];
            attrShiftAll(a, static_cast<std::int8_t>(a.at(0x3b)));
        }
    if (const std::int32_t h = w.match.stats[team].handicap; h != 0) PlayerAttr_raiseSpecial_478CBC(w, team, h);
    if (ecx != 0) { // 0x478DA8
        const int n = squadCountOf(w, team);
        for (int i = 0; i < n; ++i) attrSpread(w.attrs[team * kSquadMax + i]);
    }
    if (ebx != 0) forOutfieldRecords(w, team, [](PlayerAttr& a) { attrShiftAll(a, 2); }); // 0x478C60(team, 2)
    const TeamState& t = w.teams[team]; // 0x478AC8
    for (int k = 0; k < t.playerCount; ++k) PlayerAttr_clamp(w.attrs[w.players[t.players[k]].attr]);
    const int n = squadCountOf(w, team); // 0x478DF0: carried fatigue
    for (int i = 0; i < n; ++i) {
        PlayerAttr& a = w.attrs[team * kSquadMax + i];
        if (a.stamina18 > a.at(0x3e)) a.stamina18 = static_cast<std::uint16_t>(a.stamina18 - a.at(0x3e));
        if (a.at(0x3a) > a.at(0x3f)) a.at(0x3a) = static_cast<std::uint8_t>(a.at(0x3a) - a.at(0x3f));
        PlayerAttr_clamp(a);
    }
}

// 0x478EDC: sum of 16 rating bytes over all 22 records / 0x160.
std::int32_t PlayerAttr_teamRating(const World& w, int team) {
    std::int32_t s = 0;
    for (int i = 0; i < kSquadMax; ++i) {
        const PlayerAttr& a = w.attrs[team * kSquadMax + i];
        for (int off : {0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x37, 0x38, 0x39})
            s += a.at(off);
    }
    return s / 0x160;
}

// FUN_00455DD0 (EAX = side, EDX = pos): inside the penalty box at the side's end.
static bool inOwnPenaltyBox(const World& w, int side, const Vec3& p) {
    if (iabs(p.x) > w.pitch.penAreaHalfWidth || iabs(p.z) > w.pitch.halfLength) return false;
    const std::int32_t line = w.pitch.halfLength - w.pitch.penAreaDepth;
    return side == 1 ? line <= p.z : p.z <= -line;
}

bool Player_isKeeperInOwnBox(const World& w, int pid) { // 0x4BE048
    const PlayerState& p = w.players[pid];
    return p.slot == 0 && inOwnPenaltyBox(w, w.teams[p.team].side, p.posI) && w.teams[p.team].f204 == 0;
}

// 0x4BBBF8.
void Player_initMoveParams(World& w, int pid) {
    PlayerState& p = w.players[pid];
    const PlayerAttr& a = attrOf(w, pid);
    const Fixed bonus = kSprintBonus.v[a.at(0x2f)];
    p.runMax = Player_isKeeperInOwnBox(w, pid) ? kKeeperSpeed.v[a.at(0x3a)] : PlayerAttr_runSpeed(a);
    p.speedCap = p.runMax;
    const Fixed acc = kAccel[a.at(0x2f)];
    p.accel = acc;
    p.accelRecip = static_cast<Fixed>(0x100000000LL / acc);
    p.decel = acc;
    p.decelRecip = p.accelRecip;
    p.sprintAccel = bonus >> 1;
    p.sprintAccelRecip = static_cast<Fixed>(0x100000000LL / (bonus >> 1));
    p.sprintMax = p.runMax + bonus;
    const std::int32_t agil = a.at(0x2a) >> 2;
    p.turnAgility = agil > 0xc ? 0xc : agil;
    p.mB0 = ((0x1a - a.at(0x32)) * 0x10000) >> 5;
    std::int32_t t = (0x10 - a.at(0x3d)) + a.at(0x3e);
    t = t < 0 ? 0 : (t > 0xe ? 0xe : t);
    // u16 at player+0x88 (anim ctl +0x24 low half): turn-limit angle in 1024 units.
    const std::int32_t lim = static_cast<std::int32_t>(((t + (t >> 1)) * 0x400 + 0xb4) / 0x168) - 0xe;
    p.anim.pitchLimit = static_cast<std::int16_t>(lim);
}

// 0x4BBB84: anim rate scale player+0xB0 (anim ctl +0x4C) by speed class A[0x36].
void Player_setSpeedScale(World& w, int pid) {
    PlayerState& p = w.players[pid];
    PlayerAttr& a = attrOf(w, pid);
    if (p.slot == 0) p.anim.rateScale = 0x10000;
    else switch (a.at(0x36)) {
        case 0: p.anim.rateScale = 0xe666; break;
        case 1: p.anim.rateScale = 0xf777; break;
        case 2: p.anim.rateScale = 0x10888; break;
        default: p.anim.rateScale = 0x11999; break;
        }
    a.f10 = 0;
    a.f14 = 0;
    Player_initMoveParams(w, pid);
}

void Player_injure(World& w, int pid) { // 0x4BD998
    PlayerAttr_setInjured(attrOf(w, pid));
    Player_initMoveParams(w, pid);
}

// 0x451BB4: every DAT_0065F208 frames while in play, non-keepers lose ratings.
void Team_applyFatigue(World& w, int team) {
    if (w.match.phase != 9) return;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(w.teams[team].playerCount); ++i) {
        const int pid = w.teams[team].players[i]; // team+0x184 list
        // EDX = team+0x1F8 (0x451C13; 0..2 from the controller count, 0x45159C).
        if (!Player_isKeeperInOwnBox(w, pid) && PlayerAttr_fatigueTick(w, attrOf(w, pid), w.teams[team].ctrl1f8 != 0))
            Player_initMoveParams(w, pid);
    }
}

// FUN_004CFF94: memset 0x65E050 (0xE38 bytes: card history 0x65E1AC.., both team stat blocks
// 0x65E208 + t*0x60C including the attribute records) then the rating-table rebuild (constant,
// see buildRatingTable). Rules zeroes its own fields of that range at the same call.
void Players_buildSpeedTables(World& w) { w.attrs = {}; }

// FUN_0049FA9C (EAX = team, EDX = pitch index k, EBX = squad index).
void Player_bindSquadMember(World& w, int team, int k, int squadIdx) {
    if (static_cast<std::uint32_t>(w.data->sheets[team].squad.size()) <= static_cast<std::uint32_t>(squadIdx)) return;
    const int pid = w.teams[team].players[k]; // team+0x184[k]
    std::uint8_t* lineup = w.pl.lineup[team];
    lineup[0x16 + k] = lineup[0x0b + k] = lineup[k] = static_cast<std::uint8_t>(squadIdx); // 0x66E57A/6F/64
    w.players[pid].dbPlayer = squadIdx;             // +0x24 = 0x65EF60[t*0x4D + squadIdx]
    w.players[pid].attr = team * kSquadMax + squadIdx; // +0x28 = 0x65E230 + t*0x60C + squadIdx*0x44
    Player_setSpeedScale(w, pid);
}

} // namespace wc98::sim

namespace wc98::sim {

namespace {

// exe 0x589050: per match phase, keeper-target check enabled for outfield players (0x4BD120).
constexpr std::uint8_t kPhaseKeeperCheck[0x16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};

int teamOf(int pid) { return pid / kPlayersPerTeam; }

void syncAnimReq(PlayerState& p) { p.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.moveReq) >> 16); } // [M+0]->+0x18

// Head-aim focus vector (DAT_0065F2B4).
const Vec3& focusPos(const World& w) {
    if (w.time.focus >= 0) return w.players[w.time.focus].posI;
    if (w.time.focus == -2) return w.ball.restartPos;
    return w.ball.posI;
}

// 0x479570: vector length from the angle (|z| / sin or |x| / cos of the folded angle).
std::int32_t vecLength(std::int32_t x, std::int32_t z) {
    std::int32_t a = atan2i(z, x);
    if (a <= 0) a = -a;
    if (a > 0x100) a = 0x200 - a;
    if (a > 0x80) return static_cast<std::int32_t>((static_cast<std::int64_t>(iabs(z)) << 15) / (sinCos(a).s >> 1));
    return static_cast<std::int32_t>((static_cast<std::int64_t>(iabs(x)) << 15) / (sinCos(a).c >> 1));
}

// FUN_00455F88 (EAX = ball in +z half, EDX = pos, EBX = target): goal kick — keep the target out
// of the penalty area the ball is in.
void keepOutOfBox(const World& w, int ballPos, const Vec3& pos, Vec3& t) {
    const std::int32_t lineZ = w.pitch.penAreaLineZ;
    if (iabs(pos.z) < lineZ - 0x90) return;
    if (pos.z > 0 && w.ball.posI.z < 0) return;
    if (pos.z < 0 && w.ball.posI.z > 0) return;
    const Vec3 o = offsetInDirection(pos, 0x20, atan2i(t.x - pos.x, t.z - pos.z));
    const std::int32_t edge = lineZ - 0x20, side = w.pitch.penAreaHalfWidth + 0x20;
    if (iabs(o.z) <= edge || iabs(o.x) >= side) return;
    if (iabs(pos.x) >= w.pitch.penAreaHalfWidth) {
        t.x = t.x > 0 ? side : -side;
    } else if (ballPos) {
        if (t.z > 0) t.z = edge;
    } else if (t.z < 0) {
        t.z = -edge;
    }
}

// 0x4BE724: flag 0x80000 (byte 0x22E bit 3) = "stay in formation"; cleared for an idle (state 4)
// AI player far from the ball unless its role rules keep it up.
void updateFormationFlag(World& w, int pid) {
    PlayerState& p = w.players[pid];
    p.flags22c |= 0x80000;
    if (p.state != 4 || p.distBall < 0x3c0 || p.ctrl != 0) return;
    if (iabs(p.posI.z - w.ball.posI.z) < 0x1e0) return;
    const TeamState& t = w.teams[p.team];
    if (p.f014 == 6 &&
        !(static_cast<std::int32_t>(w.ball.posI.z + w.pitch.thirdLineZ[t.side] > -1) == t.side && t.f1b4 == 0 &&
          !(t.flags & 0x200)))
        return;
    if (p.f014 == 4 && (t.flags & 0x100)) return;
    const int own = w.ball.possessor;
    if (own >= 0 && teamOf(own) == p.team && static_cast<std::uint32_t>(p.f014) <= 3) return;
    p.flags22c &= ~0x80000u;
}

} // namespace

const Vec3& Motion_targetVec(const World& w, int pid) {
    const PlayerState& p = w.players[pid];
    return p.targetRef ? p.altTarget : p.target;
}

// M+0xD4: no writer of a non-zero arrival callback exists in the exe (only 0x4BBD78 clears it).
bool Player_arrivalCallback(World&, int, std::int32_t) { return false; }

void Player_clearArrivalCb(World& w, int pid) { w.players[pid].arrivalCb = 0; } // 0x4BBD78

// 0x4BBD84: human player's speed cap from the analog magnitude (controller +0x28).
void Player_updateSpeedCap(World& w, int pid) {
    PlayerState& p = w.players[pid];
    if (!p.ctrl) return;
    const ControllerState& c = w.controllers[p.ctrl - 1];
    if (c.analogDevice == 0 || !(p.flags22c & 0x100000)) {
        p.speedCap = p.runMax;
        return;
    }
    p.flags22c &= ~0x100000u;
    const std::int32_t m = c.analogMag;
    Fixed cap = 0;
    if (m != 0) cap = m < 0x6666 ? 0x19999 : (m < 0xa666 ? 0x48000 : fx(m, p.runMax));
    if (p.state == 0x308 && p.distBall > 0x42 && cap < 0x6c000) cap = 0x6c000;
    p.speedCap = cap;
}

// 0x47914C (EAX = record, EDX = team+0x1F8): sprint timer +0x10 and sprint fatigue +0x14 (bit
// 0x40 of +0x40 = exhausted). Returns 1 while sprinting is allowed.
bool PlayerAttr_tickTimers(World& w, PlayerAttr& a, std::int32_t edx) {
    const std::int32_t dt = w.time.simFrames;
    std::int32_t used = a.f14;
    const std::int32_t sprint = a.f10;
    if (used == 0 && sprint == 0) return false;
    std::int32_t left = sprint - dt;
    if (!w.opt.fatigueInjuries) {
        if (sprint != 0) {
            a.f10 = left < 0 ? 0 : left;
            a.f14 = 0;
            a.at(0x40) &= 0xbf;
            return true;
        }
    } else if (sprint != 0) {
        const std::int32_t add = static_cast<std::int32_t>((static_cast<std::uint32_t>(edx * dt) >> 1)) + dt;
        a.fatigueFrames += dt;
        a.f10 = left < 0 ? 0 : left;
        std::int32_t lim = a.stamina18 * 8 + 0x128;
        if (static_cast<std::uint32_t>(lim) < 0x168) lim = 0x168;
        if (!(a.at(0x40) & 0x40) && used < lim) {
            a.f14 = used + add;
            return true;
        }
        std::int32_t u = used + add;
        if (lim < u) u = lim;
        a.f14 = u;
        a.at(0x40) |= 0x40;
        return false;
    }
    used -= dt << ((a.f14 == 0) + 1);
    if (used < 1) {
        a.at(0x40) &= 0xbf;
        a.f14 = 0;
        return false;
    }
    a.f14 = used;
    return false;
}

// 0x4BBF28: speed burst request (action 4).
void Player_requestSprint(World& w, int pid) {
    PlayerState& p = w.players[pid];
    PlayerAttr& a = attrOf(w, pid);
    if (a.at(0x40) & 1) return;
    if (p.ctrl == 0) {
        const int own = w.ball.possessor >= 0 ? w.ball.possessor : w.ball.prevPossessor;
        const bool teamBall = own >= 0 && teamOf(own) == p.team;
        const TeamState& t = w.teams[p.team];
        if (t.humans == 0 && !teamBall && t.ctrl1f4 == 0 && (w.rng.next() & 0x3f) != 0) return;
        std::uint32_t lim = static_cast<std::uint32_t>(a.stamina18) * 8 + 0x128;
        if (lim < 0x168) lim = 0x168;
        if (static_cast<std::int32_t>(lim - (static_cast<std::int32_t>(lim) >> 2)) < a.f14) return;
    }
    p.moveReq |= 1;
    syncAnimReq(p);
    p.speedCap = p.sprintMax;
    a.f10 = 0xf;
}

// 0x4BDED0.
void Player_dropBallAndReset(World& w, int pid) {
    PlayerState& p = w.players[pid];
    p.stateSub2 = -1;
    p.flags22c = (p.flags22c & 0xffff0000u) | (p.flags22c & 0x7e7fu);
    p.speedCap = p.runMax;
    Ball_releaseFromPossessor(w, pid);
    Player_setState(w, pid, 0, 0, 0);
}

// 0x4BE804 (EAX = player, EDX = target): skip a target change smaller than an eighth of the
// distance to the ball (flag 0x400000) while in play.
void Player_moveToward_4BE804(World& w, int pid, const Vec3& v) {
    PlayerState& p = w.players[pid];
    if (!(p.flags22c & 0x80000)) {
        const std::int32_t d = octLength(p.posI.x - v.x, p.posI.z - v.z);
        if (d <= p.distBall >> 3 && p.targetDist <= d && w.match.phase == 9) {
            p.flags22c |= 0x400000;
            return;
        }
    }
    Motion_setTarget(w, pid, v);
}

// 0x4BCDE0: dead-ball positioning — stay out of the box at goal kicks / penalties and keep the
// restart distance from the ball spot.
void Player_lineUp_4BCDE0(World& w, int pid) {
    PlayerState& p = w.players[pid];
    Vec3 t = Motion_targetVec(w, pid);
    const int phase = w.match.phase;
    if (phase == 3 || (phase == 4 && p.state != 0x185 && p.state != 0x300)) {
        if (p.slot != 0) {
            keepOutOfBox(w, w.ball.posI.z > 0 ? 1 : 0, p.posI, t);
            Player_moveToward_4BE804(w, pid, t);
        }
        return;
    }
    if (w.match.phaseTeam == p.team && (p.state != 4 || phase != 0)) return;
    if (!(w.match.flags278 & 2) || p.state == 0x18c) return;
    std::int32_t r = phase == 0 ? w.pitch.goalWidthVisual : (phase == 1 ? 0xc0 : 0x1e0);
    if (w.match.flags278 & 0x20) r += 0x20;
    if (r + 0x90 <= p.distBall) return;
    const Vec3 spot = w.ball.restartPos;
    const Vec3& tv = Motion_targetVec(w, pid);
    const std::int32_t odx = tv.x - spot.x, odz = tv.z - spot.z; // 0x4BAD70
    const std::int32_t olen = vecLength(odx, odz);
    const Angle a = Motion_dirToTarget(w, pid);
    const std::int32_t s = Math_asinRatio(r, p.distBall);
    const Angle ab = p.angleBall;
    if (r < olen) {
        const bool inside = a > ab ? a < s + ab : (a < ab && a > ab - s);
        if (!inside) return;
    }
    Angle ang = atan2i(odx, odz);
    const Angle away = ab - 0x200;
    if (iabs(t.x) > w.pitch.halfWidth || iabs(t.z) > w.pitch.halfLength) ang += 0x200;
    std::int32_t sd = (ang - away) & 0x3ff;
    if (sd > 0x200) sd -= 0x400;
    std::int32_t ad = (ang - away) & 0x3ff;
    if (ad > 0x200) ad = 0x400 - ad;
    if (ad > 0x1c) ang = away + (sd > 0 ? 0x1c : -0x1c);
    t = offsetInDirection(spot, r + 0x10, ang);
    Player_moveToward_4BE804(w, pid, t);
    if (p.ctrl) {
        p.padOffX = 0;
        p.padOffZ = 0;
    }
}

// 0x4BBB3C: leaving state 0x308 resets the shot-setup globals 0x6731F4..0x67321C.
static void leave308(World& w) {
    w.pl.g6731f4 = 0;
    const std::uint32_t r = w.rng.next();
    w.pl.g673218 = w.pl.g673214 = 0;
    w.time.timer673210 = 0;
    w.pl.g67320c = w.pl.g67321c = 0;
    w.pl.g673208 = static_cast<std::int32_t>(r & 3);
}

// 0x4BDCD8 (EAX = player, EDX = state, EBX = run handler now, ECX = force, stack arg0/arg1).
void Player_setState(World& w, int pid, int state, std::int32_t arg0, std::int32_t arg1, bool force, bool run) {
    PlayerState& p = w.players[pid];
    if ((p.flags22c & 0x40) && state != 0x186) return;
    if (p.slot == 0 && state == 4) state = 0x200;
    const int old = p.state;
    if (old == state && !force) return;
    if (old == 0x308) leave308(w);
    if (old == 0x30d) p.pendingCode = (p.pendingCode & 0xffff0000u) | 0xffffu;
    if (state == 0x309) {
        Ball_releaseFromPossessor(w, pid);
    } else if (state == 0x307 && w.teams[p.team].humans != 0 && w.teams[p.team].f1b8 < 0) {
        Player_assignControlToReceiver(w, pid, -1); // 0x4528C4
    }
    if ((p.flags22c & 1) && w.match.phase != 9 && w.match.phase != 10) {
        if (Player_animId(w, pid) == 0x26) Anim_start(w, pid, 0x27, 0, p.facing);
        p.flags22c &= ~1u;
    }
    p.stateSub = 0;
    p.stateSub2 = 0;
    p.state = state;
    p.stateArg0 = arg0;
    p.stateArg1 = arg1;
    p.prevState = old;
    p.flags22c &= ~0x100u;
    const StateFn fn = Player_stateHandler(state);
    p.stateCb = fn ? state : -1;
    p.flags22c &= ~0x200u;
    p.flags22c |= static_cast<std::uint32_t>(Player_stateFlags(state) & 1) << 9;
    p.stateTimer = 0;
    p.flags22c &= ~0x400u;
    p.f220 = 0;
    p.speedCap = p.runMax;
    p.f224 &= 0x1fffffff; // byte 0x227 &= 0x1F
    p.flags22c |= 0x30;
    if (run && fn) fn(w, pid);
}

// 0x4BBA2C (EAX = player, EDX = pos or null, EBX = positioning callback index, ECX = face ball).
void Player_reset(World& w, int pid, const Vec3* pos, int cbIndex, bool faceBall) {
    PlayerState& p = w.players[pid];
    Vec3 slotPos{};
    if (!pos) {
        p.posCb = Formation_evalSlotCallback(w, cbIndex, pid, slotPos, -1, 0, 1);
        pos = &slotPos;
    }
    Motion_warpTo(w, pid, pos); // WC98 warpTo has no facing/lookAt handling (D-60)
    (void)faceBall;            // exe FUN_004bba2c ECX carries it into the scene path, not warpTo
    p.stateTimer = 0;
    p.countdown = 0;
    p.pendingCode = 0x0000ffffu;
    p.kickPower = 0;
    p.f1F0 = 0;
    p.f228 = 0;
    p.f258 = 0;
    p.f264 = 0;
    p.f204 = static_cast<std::int32_t>(0x80000000u);
    p.f1FC = 0;
    p.flags22c = (p.flags22c & 0xff9b9f70u) | 0x80030u;
    p.timer200 = 0;
    Player_setState(w, pid, 0, 0, 0);
    Anim_init(w, pid, false);
    p.kick = {}; // 0x4D0750: memset request, +0 = player, flags +0x54 &= 0xE3
    p.kick.kicker = pid;
}

// 0x451210 (EAX = team, EDX = player count).
void Team_initPlayers(World& w, int team, int playerCount) {
    TeamState& t = w.teams[team];
    t.index = team;
    t.opponent = team == 0 ? 1 : 0;
    t.playerCount = playerCount;
    t.f350 = t.f354 = 0;
    t.f1f0 = -1; // null player ptr
    t.f374 = 0;
    Team_buildTacticsBlock(w, team); // memcpy 0x178 bytes *(0x65EF5C + t*0x134) -> team+0xC
    for (int i = 0; i < kPlayersPerTeam; ++i) {
        const int pid = team * kPlayersPerTeam + i;
        PlayerState& p = w.players[pid];
        t.players[i] = pid; // team+0x184 ptr list, set by Players_initArrays 0x4511D0 (same values)
        p.index = i;
        p.team = team;
        const std::int32_t* e = w.pitch.entryPos[w.teams[p.team].side];
        const Vec3 entry{e[0], e[1], e[2]};
        Motion_init(w, pid, entry);
        Anim_init(w, pid, true);
        Player_reset(w, pid, &entry, 8, false);
        Player_setState(w, pid, 4, 0, 0);
    }
    t.formStyle = Formation_styleForTeam(Team_dbByte6f(w, team));
    t.formIdx() = 0;
    Team_setMentality(w, team, t.rec(0)[0]);
    Team_buildMatchPlayerAttrs(w, team, true);
    Team_bindFormationSlots(w, team);
}

// 0x4BD104: outfield player per-frame update.
static void updateOutfield(World& w, int pid) {
    PlayerState& p = w.players[pid];
    w.pl.current = pid;
    updateFormationFlag(w, pid);
    p.flags22c &= ~0x400000u;
    int keeperCheck = (!(w.match.flags278 & 0x20) && kPhaseKeeperCheck[w.match.phase]) ? 1 : 0;
    if (Player_isState100Kind1(w, pid) || Player_isState100Kind2(w, pid)) keeperCheck = 0;
    p.f248 = Player_frameMove(w, pid, (p.flags22c >> 4) & 1, (p.flags22c >> 5) & 1, keeperCheck);
    p.moveReq = 0;
    p.anim.req = 0;
    if (const StateFn fn = p.stateCb >= 0 ? Player_stateHandler(p.stateCb) : nullptr) fn(w, pid);
    if (w.match.phase != 9) Player_lineUp_4BCDE0(w, pid);
    std::uint32_t f = (pid == w.ball.possessor && (w.ball.flags & 0x20)) ? 0x4010008u : 0;
    if (w.match.phase == 9) {
        if (((p.moveStatus & 0x1000000) && p.distBall < 0x780) || p.distBall < 0x5a0) f |= 0x1000000;
    } else if (p.moveReq & 1) {
        f |= 0x4000000;
    }
    if (attrOf(w, pid).at(0x40) & 1) f |= 0x80000;
    p.moveReq |= f;
    syncAnimReq(p);
    if (p.anim.flags() & 0x10) { // player+0x9B: ball at the foot bone
        const Vec3 b = Anim_ballBonePos(w, pid);
        std::int32_t y = (b.y >> 16) - 8;
        if (y < 0) y = 0;
        Ball_setDribblePosition(w, {b.x >> 16, y, b.z >> 16});
    }
    if (w.match.phase == 9 && Player_blockBall(w, pid, 0)) {
        w.teams[1].f1e0 = -1;
        w.teams[0].f1e0 = -1;
        w.match.player2a0 = -1;
        if (w.ball.possessor >= 0) Ball_releaseFromPossessor(w, w.ball.possessor);
    }
}

// 0x4BF3CC: goalkeeper per-frame update.
static void updateKeeper(World& w, int pid) {
    PlayerState& p = w.players[pid];
    w.pl.current = pid;
    if (p.flags22c & 0x20) p.speedCap = p.runMax;
    p.f248 = Player_frameMove(w, pid, (p.flags22c >> 4) & 1, (p.flags22c >> 5) & 1, w.match.periodType == 3);
    p.moveReq = 0;
    p.anim.req = 0;
    if (const StateFn fn = p.stateCb >= 0 ? Player_stateHandler(p.stateCb) : nullptr) fn(w, pid);
    TeamState& t = w.teams[p.team];
    if (p.state == 0x206) t.flags |= 1;
    else t.flags &= ~1u;
    std::uint32_t f = 0;
    if (pid == w.ball.possessor) {
        if (w.ball.flags & 0x20) f = 0x4010008;
    } else if (w.match.phase == 9) {
        if (p.state == 0x206) f = 0x10000000;
        else if (p.distBall < 0x780) f = 0x40000;
    } else if (p.state == 0x203) {
        f = 0x40000;
    }
    if (static_cast<std::uint32_t>(p.state) < 0x20c || static_cast<std::uint32_t>(p.state) > 0x22f) w.pl.keeper[p.team].k138 = 0;
    p.moveReq |= f;
    syncAnimReq(p);
    if (p.anim.flags() & 0x10) {
        const Vec3 b = Anim_ballBonePos(w, pid);
        std::int32_t y = (b.y >> 16) - 8;
        if (y < 0) y = 0;
        Ball_setDribblePosition(w, {b.x >> 16, y, b.z >> 16});
    }
}

// FUN_00453A58: every player of the team, keeper (base slot 0) or outfield.
void Team_updatePlayers(World& w, int team) {
    for (int i = 0; i < w.teams[team].playerCount; ++i) {
        const int pid = w.teams[team].players[i]; // team+0x184 list (permuted by Team_sendOff 0x49F804)
        if (w.players[pid].slot == 0) updateKeeper(w, pid);
        else updateOutfield(w, pid);
    }
}

// 0x4BC124 (EAX = player, EDX = M, EBX = face/head-aim mode, ECX = speed-cap mode, stack =
// keeper-target check). Returns the 0x542EA0 result flag. pid == kReferee is the exe's EAX = 0 call
// from Match_updateRules 0x46F8CD (no attribute timers, no player flag checks, never the possessor).
std::int32_t Player_frameMove(World& w, int pid, int ebx, int ecx, int keeperCheck) {
    const bool isPlayer = pid != kReferee;
    PlayerState& p = w.players[pid];
    std::int32_t res = -1, altMode = 0; // res: player ptr (0 = none) -> id (-1 none)
    bool sprinting = false;
    const std::uint32_t prevStatus = p.moveStatus;
    const bool human = isPlayer && p.ctrl != 0;
    Vec3 saved{}, out{};
    if (keeperCheck) {
        if (Player_interceptTarget_542EA0(w, pid, out, res)) {
            Player_moveToward_4BE804(w, pid, out);
        } else if (res >= 0) {
            altMode = (p.moveFlags & 1) ? 2 : 1;
            saved = Motion_targetVec(w, pid);
            Motion_setTargetAlt(w, pid, out);
        }
    }
    if (isPlayer) {
        PlayerAttr& a = attrOf(w, pid);
        if (PlayerAttr_tickTimers(w, a, w.teams[p.team].ctrl1f8)) {
            p.moveReq |= 0x100000;
            syncAnimReq(p);
            sprinting = true;
        } else if (pid == w.ball.possessor && p.speed > p.runMax && p.distBall > 0x60) {
            p.moveReq |= 0x100000;
            syncAnimReq(p);
            sprinting = true;
            a.f10 = 0xf;
        }
    }
    if (ecx) {
        if (sprinting) p.speedCap = p.sprintMax;
        else if (isPlayer && (p.flags22c & 0x40000)) p.speedCap = 0x48000;
        else if ((w.match.flags278 & 0x20) && (w.match.flags278 & 0x1000)) p.speedCap = 0xf0000;
        else if (human) Player_updateSpeedCap(w, pid);
    }
    auto setHead = [&](std::int32_t v) {
        p.anim.headYawTarget = static_cast<std::int16_t>(v); // word anim+0x2A
    };
    if (w.ball.posI.y > 0x72) {
        p.moveReq |= 0x2000000;
        syncAnimReq(p);
        const std::int32_t v = -((w.ball.posI.y - 0x72) - (p.distBall >> 8));
        p.anim.headPitchTarget = static_cast<std::int16_t>(v); // word anim+0x28
    }
    if (!ebx) {
        setHead(p.facing);
    } else {
        if (prevStatus & 2) {
            p.moveReq |= 1;
            syncAnimReq(p);
        }
        if (sprinting && p.distBall > 0x180) {
            p.moveReq |= 0x4000001;
            syncAnimReq(p);
        }
        const int own = w.ball.possessor;
        if (own >= 0 && own == pid) {
            const TeamState& opp = w.teams[w.teams[p.team].opponent];
            bool look = true;
            int tgt = w.match.offBallControl;
            const int winner = opp.possWinner;
            if (tgt < 0) {
                bool keeper = false;
                if (opp.f1e4 >= 0) {
                    if (iabs(p.posI.z) > w.pitch.penAreaLineZ &&
                        static_cast<std::int32_t>(p.posI.z + w.pitch.thirdLineZ[w.teams[p.team].side] >= 0) !=
                            w.teams[p.team].side)
                        keeper = true;
                    else if (winner < 0 || // exe reads the adjacent slot area for index -1 (mapped, never faults); garbage distance > threshold -> keeper lane taken
                             static_cast<std::uint32_t>(Player_cachedDistance(w, pid, winner)) > 0x120)
                        keeper = true;
                }
                if (keeper) {
                    tgt = opp.f1e4;
                    look = false;
                } else {
                    tgt = winner;
                }
            } else if (winner >= 0) { // garbage-slot read of the exe never matches d < 0x90/0x150 for -1; tgt stays unset
                const std::int32_t d = Player_cachedDistance(w, pid, winner);
                if (d < 0x90) {
                    tgt = winner;
                    if (d < 0x60) look = false;
                } else if (d < 0x150) {
                    std::int32_t da = (Player_cachedAngle(w, pid, winner) - p.facingDesired) & 0x3ff;
                    if (da > 0x200) da = 0x400 - da;
                    if (da < 0xab) tgt = winner;
                }
            }
            setHead(tgt >= 0 ? (Player_cachedDistance(w, pid, tgt) == 0 ? p.facing
                                                                        : Player_cachedAngle(w, pid, tgt))
                             : p.facing); // no target: exe's cached reads of slot -1 yield garbage angle; keep facing
            if (p.speed == 0) {
                p.headAim = p.angleBall;
            } else if (look) {
                p.headAim = p.angleBall;
                if (prevStatus & 0x4000000) {
                    if (w.rng.next() & 0xf) {
                        p.moveReq |= 0x4000000;
                        syncAnimReq(p);
                    }
                } else if (static_cast<std::uint32_t>(w.time.possessionFrames) > 0x78 && (w.rng.next() & 3) == 0) {
                    p.moveReq |= 0x4000000;
                    syncAnimReq(p);
                }
            }
        } else {
            const Vec3& f = w.match.phase == 9 ? focusPos(w) : w.ball.restartPos;
            const std::int32_t dx = f.x - p.posI.x, dz = f.z - p.posI.z;
            const Angle h = (dx == 0 && dz == 0) ? p.facing : atan2i(dx, dz);
            p.headAim = h;
            setHead(h);
        }
    }
    Motion_update(w, pid, w.time.simFrames);
    if (altMode == 1) Player_moveToward_4BE804(w, pid, saved);
    else if (altMode == 2) Motion_setTargetAlt(w, pid, saved);
    return res;
}

// 0x4BF708 (Ball_takePossession / release): keeper takes over the ball.
void Player_onBallLost(World& w, int pid) {
    PlayerState& p = w.players[pid];
    TeamState& t = w.teams[p.team];
    if (p.ctrl != 0 && pid != t.f1e0) Player_switchControl(w, pid, 0);
    if (t.f1e4 < 0) p.runMax = kKeeperSpeed.v[attrOf(w, pid).at(0x3a)];
    t.f1e4 = pid;
    if (w.ball.possessor >= 0 && pid != w.ball.possessor) w.pl.keeper[p.team].busy = 0;
}

// 0x4BF29C.
void Keeper_release_4BF29C(World& w, int pid) {
    if (w.players[pid].ctrl == 0) return;
    Player_transferControl(w, pid, Team_nearestPlayer_4523E4(w, w.match.setPieceAim, w.players[pid].team, 0, nullptr));
}

// 0x4BF788.
void Keeper_dropPossession_4BF788(World& w, int pid) {
    TeamState& t = w.teams[w.players[pid].team];
    if (t.humans != 0) Player_assignControlToReceiver(w, pid, -1);
    if (pid == t.f1e4) Player_initMoveParams(w, pid);
    t.f1e4 = -1;
    Ball_takePossession(w, pid, 0); // EDX = 0
}

// 0x4D2578 / 0x4D2540: state 0x100 with sub-step kind byte 0x58B35A[stateSub*0x2C] == 1 / 2.
bool Player_isState100Kind0(const World& w, int pid) {
    const PlayerState& p = w.players[pid];
    return p.state == 0x100 && w.pl.tackleMoves[p.stateSub][0x1a] == 0;
}
bool Player_isState100Kind1(const World& w, int pid) {
    const PlayerState& p = w.players[pid];
    return p.state == 0x100 && w.pl.tackleMoves[p.stateSub][0x1a] == 1;
}
bool Player_isState100Kind2(const World& w, int pid) {
    const PlayerState& p = w.players[pid];
    return p.state == 0x100 && w.pl.tackleMoves[p.stateSub][0x1a] == 2;
}

// 0x4511D0: memset the 22 player objects (0x3548 bytes) and rebuild the identity team lists
// team+0x184[i] = player t*11 + i.
void Players_initArrays_4511D0(World& w) {
    for (int i = 0; i < kPlayers; ++i) w.players[i] = PlayerState{};
    for (int t = 0; t < 2; ++t)
        for (int i = 0; i < kPlayersPerTeam; ++i) w.teams[t].players[i] = t * kPlayersPerTeam + i;
}

// 0x4BB9E0: fills the 16.16 reciprocal table 0x672F24[n] = 1/n (n < 0xB4). No code reads that
// table (no xref besides this writer), so it carries no simulation state.
void Players_init_4BB9E0(World&) {}

} // namespace wc98::sim
