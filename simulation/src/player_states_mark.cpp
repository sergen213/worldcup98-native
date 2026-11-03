// Defensive states 0x280 (close down / jockey the ball carrier) and 0x281 (man-marking) with
// the marking helpers 0x53C7E0..0x53D424 — owner: SimPorter2. Ported from FIFA99.EXE v1.1.
// Tables 0x59F488..0x59F5B4 are read byte-exact through Keeper_d32 (keeper_data.cpp).
#include "player_states.hpp"

namespace wc98::sim {
namespace {

TeamState& T(World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(p.team)]; }
const TeamState& T(const World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(p.team)]; }

constexpr std::int32_t absDiff(std::int32_t a) {
    a &= 0x3ff;
    return a > 0x200 ? 0x400 - a : a;
}
void offsetXZ(Vec3& out, const Vec3& base, Fixed d, Angle a) {
    const Vec3 r = offsetInDirection(base, d, a);
    out.x = r.x;
    out.z = r.z;
}
void reqBits(PlayerState& p, std::uint32_t m) {
    p.moveReq |= m;
    p.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.moveReq) >> 16);
}
std::int32_t tbl(std::uint32_t addr, int i) { return Keeper_d32(addr + static_cast<std::uint32_t>(i) * 4); }
std::uint32_t utbl(std::uint32_t addr, int i) { return static_cast<std::uint32_t>(tbl(addr, i)); }
// i32 0x590720[rating] (Mark_engageScore).
constexpr std::int32_t k590720[20] = {-11, -9, -8, -6, -4, -3, -1, 0, 3, 5, 7, 8, 10, 13, 15, 17, 20, 20, 20, 20};

// Formation target evaluated on the copy slot +0x10 (0x53CD9D / 0x53D178).
void copySlotTarget(World& w, int pid, Vec3& out) {
    PlayerState& p = P(w, pid);
    const std::int32_t saved = p.curSlot;
    p.curSlot = p.slotCopy;
    Formation_attackParticipation(w, p.posCb, pid, out, -1, 0);
    p.curSlot = saved;
}

// 0x53CC50: angle limit (v >> 4) + 0x55, at most 0xAB.
std::int32_t angleLimit(std::int32_t v) {
    v = (v >> 4) + 0x55;
    return v > 0xab ? 0xab : v;
}

// 0x53CC64 (EAX idx, EDX opponent): how far in front of the opponent the line holds (>= 0x40;
// 0x40 when he is inside the area at our end).
std::int32_t lineDepth(const World& w, int idx, int opp) {
    const PlayerState& o = P(w, opp);
    const int side = T(w, o).side;
    if (((o.posI.z >= 0) ? 1 : 0) != side && Pitch_inPenaltyArea(w, o.posI)) return 0x40;
    std::int32_t v = -static_cast<std::int32_t>((static_cast<std::uint32_t>(w.time.possessionFrames) >> 7) << 6);
    if (((o.posI.z >= 0) ? 1 : 0) == side || idx == 2) {
        v += tbl(0x59f4e8, idx);
    } else {
        const std::int32_t z = iabs(o.posI.z) >> (4 - idx);
        v += (tbl(0x59f4e8, idx) >> 1) - z;
    }
    return v < 0x40 ? 0x40 : v;
}

} // namespace

// 0x53C7E0: tackle roll (rng & 0x1F) < 0x59F500[team.ctrl1f4].
bool Mark_rollTackle(World& w, int pid) {
    const int c = T(w, P(w, pid)).ctrl1f4;
    return (w.rng.next() & 0x1f) < utbl(0x59f500, c);
}

// 0x53C808 (EDX point, EBX idx): how attractive engaging is (possession timers, the point's zone
// rows 0x59F518 / 0x59F488, rating table 0x590720, mentality).
std::int32_t Mark_engageScore(World& w, int pid, const Vec3& pt, int idx) {
    const PlayerState& p = P(w, pid);
    const TeamState& t = T(w, p);
    const std::uint32_t row = 0x59f518 + static_cast<std::uint32_t>(idx) * 16;
    const auto slow = static_cast<std::uint32_t>(w.time.possessorSlowFrames);
    std::int32_t s = 0;
    if (slow > utbl(row, 0)) return 0;
    if (slow > utbl(row, 1) || static_cast<std::uint32_t>(w.time.possessionFrames) > utbl(row, 2)) s -= 0x32;
    const std::int32_t line = (t.side == 1 ? tbl(row, 3) : -tbl(row, 3)) + p.f228;
    if (((pt.z - line >= 0) ? 1 : 0) == t.side) s -= 0x50;
    const std::uint32_t zr = 0x59f488 + static_cast<std::uint32_t>(idx) * 20;
    if (((pt.z + w.pitch.thirdLineZ[t.side] >= 0) ? 1 : 0) != t.side) {
        s += iabs(pt.z) > w.pitch.penAreaLineZ ? tbl(zr, 4) : tbl(zr, 3);
    } else if (((pt.z - w.pitch.thirdLineZ[t.side] >= 0) ? 1 : 0) != t.side) {
        s += tbl(zr, 2);
    } else {
        s += iabs(pt.z) > w.pitch.penAreaLineZ ? tbl(zr, 0) : tbl(zr, 1);
    }
    s -= k590720[w.attrs[static_cast<std::size_t>(p.attr)].at(0x30)];
    return s + ((t.mentality - 2) << 4);
}

// 0x53C990 (EDX idx): zone value by where the player stands (tables 0x59F548..0x59F5A8).
std::int32_t Mark_zoneValue(World& w, int pid, int idx) {
    const PlayerState& p = P(w, pid);
    const int side = T(w, p).side;
    if (((p.posI.z - w.pitch.thirdLineZ[side] >= 0) ? 1 : 0) == side) return tbl(0x59f598, idx);
    if (((p.posI.z >= 0) ? 1 : 0) == side) return tbl(0x59f584, idx);
    const std::int32_t z = side == 1 ? -p.posI.z : p.posI.z;
    if (z >= w.pitch.penAreaLineZ - 0x120 && iabs(p.posI.x) <= w.pitch.penAreaHalfWidth + 0x120)
        return tbl(0x59f548, idx);
    if (((p.posI.z + w.pitch.thirdLineZ[side] >= 0) ? 1 : 0) == side) return tbl(0x59f570, idx);
    return tbl(0x59f55c, idx);
}

// 0x53CA88 (EDX opponent, EBX dist): odds of winning the ball from the opponent now.
std::int32_t Mark_tackleOdds(World& w, int pid, int opp, std::int32_t dist) {
    const PlayerState& p = P(w, pid);
    const PlayerState& o = P(w, opp);
    if (Player_isState100Kind1(w, opp) || Player_isState100Kind0(w, opp)) return 0;
    if (static_cast<std::uint32_t>(w.time.possessionFrames) < utbl(0x59f50c, T(w, p).ctrl1f4)) return 0;
    if (p.distBall < 0x20) return 0xc8;
    const std::int32_t d = p.distBall - o.distBall;
    if (d <= 0) {
        std::int32_t r = (-d) * 8 - 0x38;
        if (Player_claimRange(w, pid) > p.distBall) r += 0x64;
        return r;
    }
    const std::int32_t k = absDiff(p.angleBall - o.angleBall) >> 6;
    if (k == 0) return 0x32;
    std::int32_t s = o.distBall >> 1;
    std::int32_t e;
    if (p.speed > 0x18000 && o.speed > 0x38000) {
        e = 0x32 - (absDiff(p.angleBall - (o.facingDesired - 0x200)) >> 3);
    } else {
        e = 0x32 - (octLength(p.m98 - o.m98, p.m9c - o.m9c) >> 14);
    }
    if (e < 0) e = 0;
    s += e;
    if (o.speed < 0x4ccc) s += 0x1e;
    std::int32_t f = dist - 0xa0;
    if (f < 0) f = 0;
    if (f > 0x120) f = 0x120;
    return fx(0x46 - (f >> 2) + s, tbl(0x59f4c4, k));
}

// 0x53CD1C (EDX opponent, EBX idx, ECX angle-limit input): defensive line z -> player+0x228.
void Mark_setDefendLine(World& w, int pid, int opp, int idx, Angle a) {
    PlayerState& p = P(w, pid);
    const TeamState& t = T(w, p);
    const PlayerState& o = P(w, opp);
    std::int32_t own = p.posI.z, z = o.posI.z;
    if (t.side == 1) {
        own = -own;
        z = -z;
    }
    z -= lineDepth(w, idx, opp);
    if ((p.role == 2 || p.curSlot == 1) && z > w.pitch.thirdLineZ[0]) z = w.pitch.thirdLineZ[0];
    const int os = T(w, o).side;
    if (((o.posI.z - w.pitch.thirdLineZ[os] >= 0) ? 1 : 0) == os && idx != 2) {
        Vec3 v;
        copySlotTarget(w, pid, v);
        std::int32_t c = t.side == 1 ? -v.z : v.z;
        if (c > 0) c = 0;
        if (c < z) z = c;
    }
    const Angle ga = atan2i(w.ball.posI.x, w.ball.posI.z - w.pitch.goalLineZ[t.side]);
    if (z < own && absDiff(p.angleBall - ga) < angleLimit(a)) z = own;
    const std::int32_t lo = w.pitch.goalAreaDepth - w.pitch.halfLength;
    if (z < lo) z = lo;
    else if (z > w.pitch.halfLength - w.pitch.goalAreaDepth) z = w.pitch.halfLength - w.pitch.goalAreaDepth;
    p.f228 = t.side == 1 ? -z : z;
}

// 0x53CE90 (EDX opponent, EBX out, ECX angle, stack s0 = dist, s1 = engage): jockey point; 1 =
// the point is more than 0x40 away (go there), 0 = holding position.
int Mark_jockeyTarget(World& w, int pid, int opp, Vec3& out, Angle a, std::int32_t s0, std::int32_t s1) {
    PlayerState& p = P(w, pid);
    const TeamState& t = T(w, p);
    const PlayerState& o = P(w, opp);
    const int side = t.side, os = T(w, o).side;
    std::int32_t bz = w.ball.posI.z, pz = p.posI.z; // [esp+0x24] / [esp+0x18]
    std::int32_t line = p.f228;                     // esi
    int wide = 0;                                   // [esp+0x4]
    if (((o.posI.z + w.pitch.thirdLineZ[os] >= 0) ? 1 : 0) != os) {
        const std::int32_t aa = iabs(a);
        wide = (aa > 0xab && aa < 0x155) ? 1 : 0;
    }
    if (side == 1) {
        line = -line;
        bz = -bz;
        pz = -pz;
    }
    const int idx = t.ctrl1f4;
    const std::int32_t err = absDiff(p.angleBall - a); // [esp+0x1c]
    const std::int32_t dep = lineDepth(w, idx, opp);
    std::int32_t reach; // [esp+0xc]
    if (wide) {
        reach = dep + 0x40 < s0 ? dep + 0x40 : s0;
        line = pz;
    } else {
        if (angleLimit(s0) > err) {
            if (line + dep > bz && p.distBall > 0x1c0) line = bz - dep;
            if (line < pz) line = pz;
        }
        reach = line + dep;
        if (bz > reach) {
            line = bz - dep;
            const std::uint32_t row = 0x59f5ac + static_cast<std::uint32_t>(idx) * 8;
            bool clamped = false;
            if ((p.role == 2 || p.curSlot == 1) && line > w.pitch.thirdLineZ[0]) {
                line = w.pitch.thirdLineZ[0];
                clamped = true;
            }
            if (!clamped && idx != 2 && ((o.posI.z - w.pitch.thirdLineZ[os] >= 0) ? 1 : 0) == os &&
                static_cast<std::uint32_t>(w.time.possessorSlowFrames) < utbl(row, 0) &&
                static_cast<std::uint32_t>(w.time.possessionFrames) < utbl(row, 1)) {
                copySlotTarget(w, pid, out);
                std::int32_t c = side == 1 ? -out.z : out.z;
                if (c > 0) c = 0;
                if (angleLimit(s0) > err && c < pz) c = pz;
                if (line > c) line = c;
            }
        }
    }
    // 0x53D004
    const std::int32_t lo = w.pitch.goalAreaDepth - w.pitch.halfLength;
    if (line < lo) line = lo;
    else if (line > w.pitch.halfLength - w.pitch.goalAreaDepth) line = w.pitch.halfLength - w.pitch.goalAreaDepth;
    p.f228 = side == 1 ? -line : line;
    std::int32_t front = tbl(0x59f4f4, idx) + line; // edx
    bool chase = line > bz;
    if (!chase) {
        if (s1 != 0) chase = true;
        else if (((o.posI.z >= 0) ? 1 : 0) != os && Pitch_inPenaltyArea(w, o.posI)) chase = true;
        else if (front > bz && s0 > 0xa0 && o.speed > 0x4ccc && err > 0x55) chase = true;
    }
    if (!chase) {
        if (wide) { // 0x53D3EA
            offsetXZ(out, w.ball.posI, reach, a - 0x200);
            return 0;
        }
        if (front > bz) line -= (front - bz) >> 1;
        if (side == 1) line = -line;
        const std::int32_t gl = w.pitch.goalLineZ[side];
        out.z = line;
        if (gl == w.ball.posI.z) {
            out.x = w.ball.posI.x;
            return 0;
        }
        out.x = ((gl - line) * w.ball.posI.x) / (gl - w.ball.posI.z);
        return 0;
    }
    // 0x53D049
    if (((p.posI.z >= 0) ? 1 : 0) == side && Pitch_inPenaltyArea(w, p.posI)) {
        offsetXZ(out, w.ball.posI, 0x60, a - 0x200);
    } else { // 0x53D314
        std::int32_t spd;
        if (p.speed > 0x4ccc) spd = p.runMax - (fx(absDiff(p.facingDesired - p.angleBall), 0x200) + 0x8000);
        else spd = p.runMax - 0x10000;
        if (spd < 0) spd = 0;
        if (Player_solveIntercept(w, p.posI, spd, w.ball.posI, o.m98, o.m9c, w.pitch.halfWidth, w.pitch.halfLength,
                                  out) < 0) {
            out.x = w.ball.posI.x + ((w.ball.vel.x << 5) >> 16);
            out.z = w.ball.posI.z + ((w.ball.vel.z << 5) >> 16);
        }
    }
    return static_cast<std::uint32_t>(octLength(out.x - p.posI.x, out.z - p.posI.z)) <= 0x40 ? 0 : 1;
}

// 0x280 0x53D424: close down the ball winner (team possWinner / f1d0 / f1cc): intercept a loose
// ball, else jockey and pick the moment to tackle (0x4D19C8).
void State280(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    if (w.match.phase != 9) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    const Vec3 ownGoal{0, 0, w.pitch.goalLineZ[t.side]}; // [esp+0x1C] (y never read)
    const Fixed runMax = p.runMax;                        // [esp+0x40]
    const std::int32_t dist = p.distBall;                 // [esp+0x54]
    const int poss = w.ball.possessor;                    // ebp
    const int idx = t.ctrl1f4;                            // [esp+0x50]
    const int pw = t.possWinner;                          // ecx
    if (pid != pw && pid != t.f1d0 && pid != t.f1cc) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (Motion_facingBall(w, pid)) reqBits(p, 0x20000);
    if (p.ctrl != 0) { // 0x53D59C
        reqBits(p, 0x20000);
        p.flags22c |= 0x100010;
        Vec3 v;
        Player_humanMoveTarget(w, pid, v);
        Player_moveToward_4BE804(w, pid, v);
        return;
    }
    if (p.countdown != 0) return;
    const OctDist gd = octDistance(ownGoal, w.ball.posI);
    const Angle ga = atan2i(gd.dx, gd.dz); // [esp+0xC]
    const std::int32_t err = absDiff(P(w, pw).angleBall - ga); // [esp+0x58]
    bool close = false; // [esp+0x44]
    if (pid != pw) {
        bool test = err < 0x39 && P(w, pw).distBall < 0x1e0;
        if (!test) // 0x53D5E3
            test = static_cast<std::uint32_t>(Player_cachedDistance(w, pid, pw)) < 0x100 && P(w, pw).distBall < 0x100;
        if (test) { // 0x53D546
            if (dist > 0xc0 && Mark_rollTackle(w, pid)) {
                Player_dropBallAndReset(w, pid);
                return;
            }
            close = true;
        }
    }
    if (poss < 0) { // 0x53D563
        p.flags22c |= 0x10;
        if (pid != pw) return;
        Vec3 v;
        if (Player_solveIntercept(w, p.posI, runMax - 0x10000, w.ball.posI, w.ball.vel.x, w.ball.vel.z,
                                  w.pitch.halfWidth, w.pitch.halfLength, v) >= 0) {
            Pitch_clampToField(w, v);
            Motion_setTargetAlt(w, pid, v);
        } else {
            Motion_setTargetAlt(w, pid, w.ball.posI);
        }
        return;
    }
    p.flags22c &= ~0x10u; // 0x53D67A
    p.headAim = p.angleBall;
    if (p.stateSub2 == 0) { // 0x53D820
        Mark_setDefendLine(w, pid, poss, idx, dist);
        p.stateTimer = 0;
        ++p.stateSub2;
        return;
    }
    if (p.stateSub2 != 1) return;
    const std::int32_t engage = Mark_engageScore(w, pid, w.ball.landing, idx); // [esp+0x38]
    const std::int32_t odds = Mark_tackleOdds(w, pid, poss, dist);             // [esp+0x3C]
    const TeamState& o = w.teams[static_cast<std::size_t>(t.opponent)];
    if (poss == o.f1c0) { // 0x53D849
        const int r = Team_nearestPlayer_4522D8(w, P(w, o.f1c0).posI, t.index, 0);
        if (r >= 0 && r != pid) Player_setState(w, r, 0x280, 0, 0, false, false);
    }
    const int go = (close || odds >= engage) ? 1 : 0; // [esp+0x4C]
    Vec3 v; // [esp+0x28]
    const int far = Mark_jockeyTarget(w, pid, poss, v, ga, dist, go); // ecx
    const std::int32_t vd = octLength(v.x - p.posI.x, v.z - p.posI.z); // [esp+0x34]
    if (w.ball.posI.y < 0x39 && dist < 0x120) {
        bool tryTackle = false;
        if (go) {
            tryTackle = true;
        } else if (((w.ball.landing.z >= 0) ? 1 : 0) == t.side && Pitch_inPenaltyArea(w, w.ball.landing) &&
                   Mark_rollTackle(w, pid) && (far != 0 || (P(w, poss).kick.flags & 4))) { // 0x53D896
            tryTackle = true;
        }
        if (tryTackle) { // 0x53D73F
            int kind;
            if (t.humans > 0 && idx != 2) {
                if (idx == 0) return;
                const int r = Team_nearestToBall_4522FC(w, p.team, p.curSlot);
                if (r >= 0 && P(w, r).ctrl != 0 && P(w, r).distBall <= 0x360) return;
                kind = 0;
            } else { // 0x53D8F6
                if (o.humans > 0 && idx == 0 && (w.rng.next() & 3) && P(w, poss).state == 0x300) return;
                kind = 3;
            }
            if (Player_tryTackle(w, pid, kind)) return;
        }
    }
    bool walk = false; // [esp+0x48]
    if (far != 0 || (vd >= 0x140 && err >= 0x55)) { // 0x53D939
        Player_requestSprint(w, pid);
    } else { // 0x53D7AD
        walk = true;
        p.speedCap = runMax;
    }
    if (p.moveStatus & 1) { // 0x53D7C0
        if (p.targetDist > 0x60) reqBits(p, 1);
        if (p.targetDist < 0x40) walk = true;
    } else if (p.distBall > p.distBallPrev) { // 0x53D945
        reqBits(p, 1);
    }
    Pitch_clampToField(w, v); // 0x53D7F6
    if (walk) Player_moveToward_4BE804(w, pid, v);
    else Motion_setTargetAlt(w, pid, v);
}

// 0x281 0x53D988: man-marking: stand goal-side of the marked opponent (distance by zone value);
// fall back toward the goal or give the mark up when he gets past.
void State281(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    if (p.markTarget == 0) return;
    const int m = p.markTarget - 1;
    const PlayerState& mp = P(w, m);
    const std::int32_t gl = w.pitch.goalLineZ[t.side];
    const std::int32_t zv = Mark_zoneValue(w, pid, t.mentality);
    std::int32_t gap;
    if (zv == 0) {
        const std::int32_t d = octLength(-p.posI.x, gl - p.posI.z);
        gap = d < 0x120 ? 0x10 : d < 0x240 ? 0x20 : 0x30;
    } else if (zv == 1) {
        gap = w.attrs[static_cast<std::size_t>(mp.attr)].at(0x2f) + 0x50;
    } else if (zv == 2) {
        std::int32_t cap = octLength(-p.posI.x, gl - p.posI.z) >> 1;
        if (cap > 0x1e0) cap = 0x1e0;
        gap = p.distBall >> 1;
        if (gap < 0x30) gap = 0x30;
        else if (gap > cap) gap = cap;
    } else {
        gap = p.distBall >> 1;
    }
    if (w.match.phase != 9) { // 0x53DA0E
        if (static_cast<std::uint32_t>(p.stateTimer) < 0xa) return;
        gap = 0x30;
    }
    Vec3 v;
    offsetXZ(v, mp.posI, gap, atan2i(-mp.posI.x, gl - mp.posI.z));
    if (((p.posI.z - mp.posI.z >= 0) ? 1 : 0) == t.side) {
        Player_moveToward_4BE804(w, pid, v);
        return;
    }
    if (p.role == 2 || p.curSlot == 1 || static_cast<std::uint32_t>(t.w360[2]) <= 2) { // 0x53DB13
        v.x = mp.posI.x >> 1;
        v.z = (gl + mp.posI.z) >> 1;
        Player_requestSprint(w, pid);
        Player_moveToward_4BE804(w, pid, v);
        return;
    }
    Team_unmark(w, m);
    Player_dropBallAndReset(w, pid);
}

} // namespace wc98::sim
