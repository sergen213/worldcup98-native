// Keeper / body block code (owner: SpStates/SpKeeper): ball blocked by a player's body or the
// keeper's skeleton (0x543390 and callees 0x543560, 0x5437D4, 0x543D3C, 0x543D78, 0x544084), the
// random body deflection 0x484C0C, the keeper save reaction 0x4BF5A4 and the small shared helpers
// 0x543280 / 0x5432B0 / 0x543350 / 0x543370.
#include <algorithm>

#include "wc98/sim/sim.hpp"
#include "player_states.hpp"

namespace wc98::sim {
namespace {

// Wrap used by these routines: `and 0x3ff; cmp 0x200; jle; sub 0x400` (range -0x1ff..0x200).
constexpr std::int32_t wrap200(std::int32_t a) {
    a &= 0x3ff;
    return a > 0x200 ? a - 0x400 : a;
}

bool ballGoalAhead(const World& w) { // stamp[6] (dword 0x66906E >> 16) still in the ring
    const std::int32_t s = w.ball.stamp[6];
    return s != -1 && Ball_ringIndex(w, s - w.ball.consumed) >= 0;
}

// 0x484C0C (EAX = player): random deflection off a body. Clears ball flag 8, kicks the ball back with
// speed ~0.4 * ball speed and a random spread; a non-keeper clears the restart/penalty taker.
void Ball_deflectOffBody(World& w, int pid) {
    const PlayerState& p = P(w, pid);
    BallState& b = w.ball;
    b.flags &= ~0x8u;
    std::int32_t ebx = 0x55; // 0x484C35
    if (b.posI.y <= 0x55) ebx = static_cast<std::int32_t>(w.rng.next() % 0x55u); // 0x484E3D
    const std::int32_t avy = iabs(b.vel.y) >> 9;
    if (avy + ebx >= 0x1ff) ebx = 0x1ff;                                       // 0x484E64
    else ebx += avy;
    std::int32_t esi = ebx; // pitch angle
    if (ebx != 0) {
        esi = static_cast<std::int32_t>(w.rng.next() % static_cast<std::uint32_t>(ebx));
        if (b.posI.y < 0x55 && (w.rng.next() & 1)) esi = -esi;
    }
    std::int32_t speed = fx(fx(b.speed, 0x6666), sinCos(esi).c); // 0x484CBB 0x6666 = 0.4
    speed += static_cast<std::int32_t>(w.rng.next() & 0x1ffff);
    std::int32_t yaw;
    if (b.vel.x != 0 || b.vel.z != 0) {
        yaw = atan2i(-b.vel.x, -b.vel.z);
        yaw += static_cast<std::int32_t>(w.rng.next() & 0x1ff) - 0x100;
    } else {
        yaw = static_cast<std::int32_t>(w.rng.next() & 0x3ff); // 0x484E7B
    }
    yaw = wrap200(yaw);
    Vec3 v;
    v.x = fx(speed, sinCos(yaw).s);
    v.z = fx(speed, sinCos(yaw).c);
    if (b.vel.y < 0 && p.posI.y + 0x6a <= b.posI.y) v.y = -(b.vel.y >> 1); // 0x484E8D
    else v.y = fx(speed, sinCos(esi).s);
    const bool goal = ballGoalAhead(w);
    Ball_kickWithVelocity(w, pid, v, 0, 0, 1); // 0x484E19 (EBX spin 0, stack kind 0, ECX result 1)
    if (goal) {
        // exe calls 0x4D9150 (save commentary, 0x4A2818/0x667294 state): not simulated
    }
    if (p.slot != 0) {
        w.match.restartTaker = -1; // DAT_0065F2AC
        w.match.penaltyTaker = -1; // DAT_0065F2B0
    }
}

// 0x543560 (EAX = player, EDX = out normal): ball touching the body (< ball radius + 15) -> normal
// from the player to the ball, 1. Otherwise, when the ball will pass the player within reach in
// the next 6 frames, schedules a time-slice split at the contact frame and clears blockUntil.
int Player_bodyBlockNormal(World& w, int pid, Vec3& out) {
    const PlayerState& p = P(w, pid);
    const BallState& b = w.ball;
    std::int32_t maxY;
    if (p.flags22c & 1) maxY = 0x1c;
    else maxY = Player_isState100Kind1(w, pid) ? 0x1c : 0x72;
    if (maxY < b.posI.y) return 0;
    if (p.distBall > 0xe4) return 0;
    const std::uint32_t reach = static_cast<std::uint32_t>(b.radius + 0xf);
    if (reach >= static_cast<std::uint32_t>(p.distBall)) { // 0x54372E contact
        if (b.posI.x == p.posI.x && b.posI.z == p.posI.z) {
            if (b.vel.x == 0 && b.vel.z == 0) out = {0, 0x10000, 0};
            else out = {-b.vel.x, 0, -b.vel.z};
        } else {
            out = {fxi(b.posI.x - p.posI.x), 0, fxi(b.posI.z - p.posI.z)};
        }
        Math_normalize3(out, out);
        return 1;
    }
    if (static_cast<std::uint32_t>(w.time.totalFrames) > static_cast<std::uint32_t>(w.match.blockUntil)) return 0;
    Vec3 pred;
    Ball_predictPos(w, 6, 0, &pred); // 0x5435D4
    const std::int32_t esi = pred.z - b.posI.z;
    const std::int32_t ebx = b.posI.x - pred.x;
    if (esi == 0 && ebx == 0) return 0;
    const Angle side = atan2i(esi, ebx);
    const std::int32_t a = wrap200(atan2i(pred.x - p.posI.x, pred.z - p.posI.z) - side);
    const std::int32_t c = wrap200(p.angleBall - side);
    if (!((a > 0 && c < 0) || (a < 0 && c > 0))) return 0; // ball must cross the player's line
    std::int32_t perp = (esi * p.dxBall + p.dzBall * ebx) / p.distBall; // 0x54366D idiv
    if (static_cast<std::uint32_t>(iabs(perp)) > reach) return 0;
    std::int32_t frames = 6;
    if (b.vel.x > b.vel.z) {
        const std::int32_t s = iabs(b.vel.x) >> 16;
        if (s > 0)
            frames = static_cast<std::int32_t>((static_cast<std::uint32_t>(iabs(p.dxBall)) + (reach >> 1)) /
                                               static_cast<std::uint32_t>(s));
    } else {
        const std::int32_t s = iabs(b.vel.z) >> 16;
        if (s > 0)
            frames = static_cast<std::int32_t>((static_cast<std::uint32_t>(iabs(p.dzBall)) + (reach >> 1)) /
                                               static_cast<std::uint32_t>(s));
    }
    if (frames > 6) frames = 6;
    Sim_scheduleSplit(w, frames); // 0x5436D2
    w.match.blockUntil = 0;
    return 0;
}

// 0x544084 (EAX = point, EDX/EBX x range, ECX/stack y range, stack z range): inside the box with
// 2.0 slack on every side.
bool inBox(const Vec3& c, Fixed x0, Fixed x1, Fixed y0, Fixed y1, Fixed z0, Fixed z1) {
    return x0 - 0x20000 <= c.x && x1 + 0x20000 >= c.x && y0 - 0x20000 <= c.y && y1 + 0x20000 >= c.y &&
           z0 - 0x20000 <= c.z && z1 + 0x20000 >= c.z;
}

// 0x543D78 (EAX = bones, EDX = i, EBX = j, ECX = radius, stack t, normal, point): ball path this frame
// (prevPos -> pos, raised by the ball radius) against the capsule bone i -> bone j of `radius`.
int Keeper_hitBone(const World& w, const Vec3 bones[16], int i, int j, std::int32_t radius, Fixed* t, Vec3* normal,
                   Vec3* point) {
    const Fixed r16 = fxi(w.ball.radius);
    Vec3 a = w.ball.prevPos, b = w.ball.pos;
    a.y += r16;
    b.y += r16;
    Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z};
    if (d.x == 0 && d.y == 0 && d.z == 0) return 0;
    const Vec3 p = bones[i], q = bones[j];
    Vec3 e{q.x - p.x, q.y - p.y, q.z - p.z};
    // Port guard (exe-faithful Math_normalize3 loops forever on a zero vector, see player_helpers):
    // headless lanes run clip frames whose skeleton bones coincide -> degenerate axis = no capsule.
    if ((e.x | e.y | e.z) == 0) return 0;
    Math_normalize3(d, d);
    Math_normalize3(e, e);
    const Fixed reach = fxi(radius) + r16; // 0x543E8D
    Fixed t0 = 0, t1 = 0;
    if (!Math_closestApproach(a, d, p, e, reach, &t0, &t1, normal)) return 0;
    const Vec3 c{a.x + fx(d.x, t0), a.y + fx(d.y, t0), a.z + fx(d.z, t0)};
    if (!inBox(c, std::min(p.x, q.x) - reach, std::max(p.x, q.x) + reach, std::min(p.y, q.y) - reach,
               std::max(p.y, q.y) + reach, std::min(p.z, q.z) - reach, std::max(p.z, q.z) + reach))
        return 0;
    if (!inBox(c, std::min(a.x, b.x), std::max(a.x, b.x), std::min(a.y, b.y), std::max(a.y, b.y), std::min(a.z, b.z),
               std::max(a.z, b.z)))
        return 0;
    *t = t0;
    *point = c;
    return 1;
}

// 0x543D3C (EAX = t, EDX = normal, EBX = point, ECX = best t, stack outNormal, outPoint): keep the
// earliest hit.
void Keeper_keepEarliest(Fixed t, const Vec3& normal, const Vec3& point, Fixed* best, Vec3& outNormal, Vec3& outPoint) {
    if (t >= *best) return;
    *best = t;
    outNormal = normal;
    outPoint = point;
}

// 0x5437D4 (EAX = player, EDX = out normal): ball within 0xE4 -> test the ball path against 13 bone
// capsules of the posed skeleton; earliest hit's normal, 1 if any hit.
int Keeper_skeletonBlockNormal(World& w, int pid, Vec3& out) {
    Fixed best = 0x7fff0000;
    if (octDistance(P(w, pid).posI, w.ball.posI).dist > 0xe4) return 0;
    Vec3 bones[16];
    Anim_evalSkeleton(w, pid, bones); // 0x460ACC(player+0x74, bones, player+0x78)
    // Capsules {bone i, bone j, radius} in call order 0x54385A..0x543CF2.
    static constexpr std::int32_t kCaps[13][3] = {{3, 2, 6},  {2, 1, 0xb}, {1, 0, 0xb},  {4, 5, 4},  {5, 6, 3},
                                                  {7, 8, 4},  {8, 9, 3},   {0, 10, 6},   {10, 11, 5}, {11, 12, 5},
                                                  {0, 13, 6}, {13, 14, 5}, {14, 15, 5}};
    int hit = 0;
    Vec3 point, normal, hitPoint;
    for (const auto& c : kCaps) {
        Fixed t = 0;
        if (Keeper_hitBone(w, bones, c[0], c[1], c[2], &t, &normal, &point)) {
            hit = 1;
            Keeper_keepEarliest(t, normal, point, &best, out, hitPoint);
        }
    }
    return hit;
}

} // namespace

// 0x5432B0: asin of a 16.16 value as a 1024-per-turn angle (-0x100..0x100), byte table 0x5A04CC
// (513 entries) with linear interpolation above 0xFA00.
std::int32_t Math_asin1024(Fixed x) {
    const bool neg = x < 0;
    if (neg) x = -x;
    std::int32_t i = x >> 7, r;
    if (x > 0xfa00) {
        if (x >= 0x10000) {
            r = 0x100;
        } else {
            const std::int32_t f = x & 0x7f;
            if (i == 0x1ff) {
                r = Keeper_d8(0x5a06cb);
                r += ((0x100 - r) * f) >> 7;
            } else {
                r = Keeper_d8(0x5a04cc + i);
                r += ((Keeper_d8(0x5a04cd + i) - r) * f) >> 7;
            }
        }
    } else {
        if (x & 0x40) ++i;
        r = Keeper_d8(0x5a04cc + i);
    }
    return neg ? -r : r;
}

// 0x543280 (EAX = a, EDX = b): asin(a / b), +-0x100 outside (-b, b).
std::int32_t Math_asinRatio(std::int32_t a, std::int32_t b) {
    if (a <= -b) return -0x100;
    if (a >= b) return 0x100;
    const std::int32_t q = static_cast<std::int32_t>(static_cast<std::uint32_t>(a) << 14) / b;
    return Math_asin1024(static_cast<std::int32_t>(static_cast<std::uint32_t>(q) << 2));
}

// 0x543350: acos as 0x100 - asin.
std::int32_t Math_acos1024(Fixed x) { return 0x100 - Math_asin1024(x); }

// 0x543370: no blocks until totalFrames + frames (only extends the stamp; unsigned compare).
void Keeper_holdOff(World& w, std::int32_t frames) {
    const std::uint32_t t = static_cast<std::uint32_t>(frames + w.time.totalFrames);
    if (t > static_cast<std::uint32_t>(w.match.keeperHoldUntil)) w.match.keeperHoldUntil = static_cast<std::int32_t>(t);
}

// 0x543390 (EAX = player, EDX = deflect): loose moving ball blocked by this player. deflect != 0:
// skeleton test + reflection (keeper), else body test + random deflection. 1 if blocked.
int Player_blockBall(World& w, int pid, int deflect) {
    PlayerState& p = P(w, pid);
    BallState& b = w.ball;
    if (static_cast<std::uint32_t>(w.match.keeperHoldUntil) > static_cast<std::uint32_t>(w.time.totalFrames)) return 0;
    if (b.possessor != -1) return 0;
    if (!deflect && (p.distBall >= p.distBallPrev || p.distBall > 0x156)) return 0;
    if (b.posI.y > 0xe4) return 0;
    if (b.vel.x == 0 && b.vel.y == 0 && b.vel.z == 0) return 0;
    const int other = w.match.player2a0; // DAT_0065F2A0
    if (other != -1 && w.teams[P(w, other).team].index == w.teams[p.team].index) return 0;
    if (pid == w.teams[p.team].f1e0) return 0;
    if (pid == b.lastToucher) return 0;
    if (p.state == 0x307 || p.state == 0x306 || p.state == 0x207) return 0;
    if (p.state == 0x205 && !Pitch_inPenaltyBox(w, w.teams[p.team].side, p.posI)) return 0;
    if (b.lastToucher != -1 &&
        (Player_isState100Kind1(w, b.lastToucher) || Player_isState100Kind0(w, b.lastToucher)))
        return 0;
    Vec3 n;
    const int hit = deflect ? Keeper_skeletonBlockNormal(w, pid, n) : Player_bodyBlockNormal(w, pid, n);
    if (!hit) return 0;
    w.teams[0].f1e0 = -1; // 0x65A4B0
    w.teams[1].f1e0 = -1; // 0x65A878
    w.match.player2a0 = -1;
    // Momentary possession (take + release in the same call): no possessor change is reported.
    Ball_takePossession(w, pid, 0);
    Ball_releaseFromPossessor(w, pid);
    if (deflect) Ball_reflectOffPlayer(w, pid, n, 0x8000, 1); // 0x5434D4
    else Ball_deflectOffBody(w, pid);
    return hit;
}

// 0x4BF5A4: keeper reaction to a loose ball in open play (phase 9): skeleton block against shots
// by the opponents; a shot headed for goal that no longer is counts as a save.
void Keeper_reactToBall(World& w, int pid) {
    const PlayerState& p = P(w, pid);
    const BallState& b = w.ball;
    if (w.match.phase != 9) return;
    if (b.possessor == pid && (b.flags & 0x20)) return;
    const int t = w.teams[p.team].index;
    const KeeperBlock& k = w.pl.keeper[t];
    if (b.kickCount == k.kickSeen && k.kickSeenFlag != 0) return;
    int toucher = b.possessor;
    if (toucher == -1) toucher = b.prevPossessor;
    if (toucher == -1) toucher = b.lastToucher;
    bool shot = false;
    if (toucher != -1 && w.teams[P(w, toucher).team].index != t) shot = ballGoalAhead(w); // 0x4BF658
    Player_blockBall(w, pid, 1); // result unused by the exe
    if (!shot) return;
    if (ballGoalAhead(w)) return;
    if (b.flags & 8) return;
    // exe calls 0x4D9150 (save commentary): not simulated
    if (w.match.periodType == 3) return;
    w.match.stats[t == 0 ? 1 : 0].goals++; // 0x65E1FC + opponent*0x60C (shots on target)
    w.match.stats[t].f200++;               // 0x65E200 + team*0x60C (saves)
    // exe calls [0x581BB0](0x12) (presentation trigger): not simulated
    w.emit(EventType::Save, t, pid);
}

} // namespace wc98::sim
