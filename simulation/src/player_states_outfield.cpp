// Outfield player states 0x000-0x004, 0x080-0x086, 0x100 and their helpers (owner: SpStates),
// ported from FIFA99.EXE v1.1 0x532900..0x535BEC, plus the pair distance/angle cache 0x4D9F84..
// 0x4DA190. Exe addresses in comments; see player_states.hpp for conventions.
#include "player_states.hpp"

namespace wc98::sim {
namespace {

// `and 0x3ff; cmp 0x200; jle; sub 0x400`: angle difference in -0x1FF..0x200.
constexpr std::int32_t wrapDiff(std::int32_t a) {
    a &= 0x3ff;
    return a > 0x200 ? a - 0x400 : a;
}
// `and 0x3ff; cmp 0x200; jle; 0x400 - a`: absolute angle difference 0..0x200.
constexpr std::int32_t absDiff(std::int32_t a) {
    a &= 0x3ff;
    return a > 0x200 ? 0x400 - a : a;
}
// FUN_004BB0D0 writes only x/z of the output (y is left as it was).
void offsetXZ(Vec3& out, const Vec3& base, Fixed scale, Angle a) {
    const Vec3 r = offsetInDirection(base, scale, a);
    out.x = r.x;
    out.z = r.z;
}
constexpr std::uint32_t kB22D(std::uint32_t m) { return m << 8; }  // byte player+0x22D
constexpr std::uint32_t kB22E(std::uint32_t m) { return m << 16; } // byte player+0x22E

// Common prologue of states 0x000/0x001 (0x532C3E..0x532C73): a human-controlled player walks
// to his pad target unless the phase is 4, 6 or 10.
void humanPadMove(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const int ph = w.match.phase;
    if (p.ctrl != 0 && ph != 4 && ph != 6 && ph != 10) {
        Vec3 t;
        Player_humanMoveTarget(w, pid, t);
        Player_moveToward_4BE804(w, pid, t);
        p.flags22c |= kB22E(0x10);
    }
}

// Pair index FUN_004D9F84 (EAX/EDX players, EBX out index, ECX out swapped): ids a, b ordered
// low/high; index = 22*lo + hi - (lo+1)(lo+2)/2 (`shr` = unsigned halving).
int pairIndex(int a, int b, bool& swapped) {
    std::uint32_t lo = static_cast<std::uint32_t>(a), hi = static_cast<std::uint32_t>(b);
    swapped = lo > hi;
    if (swapped) std::swap(lo, hi);
    return static_cast<int>(lo * 22 + hi - (((lo + 1) * (lo + 2)) >> 1));
}

} // namespace

// 0x4D9FFC: octagonal integer distance between two players, cached for 10 frames (table 0x676604).
std::int32_t Player_cachedDistance(World& w, int a, int b) {
    if (a == b) return 0;
    bool swapped;
    std::int32_t* e = w.pl.pairCache[pairIndex(a, b, swapped) + 1];
    if (static_cast<std::uint32_t>(w.time.totalFrames) - static_cast<std::uint32_t>(e[2]) > 10) {
        const PlayerState& pa = P(w, a);
        const PlayerState& pb = P(w, b);
        e[1] = octLength(pb.posI.x - pa.posI.x, pb.posI.z - pa.posI.z);
        e[2] = w.time.totalFrames;
    }
    return e[1];
}

// 0x4DA094: angle from player `from` to player `to` (cached for 10 frames; the cache holds the
// low->high id direction, +0x200 when the ids are swapped). Also refreshes the distance.
Angle Player_cachedAngle(World& w, int from, int to) {
    bool swapped;
    std::int32_t* e = w.pl.pairCache[pairIndex(from, to, swapped) + 1];
    if (static_cast<std::uint32_t>(w.time.totalFrames) - static_cast<std::uint32_t>(e[3]) > 10) {
        const PlayerState& pa = P(w, from);
        const PlayerState& pb = P(w, to);
        const std::int32_t dx = pb.posI.x - pa.posI.x, dz = pb.posI.z - pa.posI.z;
        e[0] = atan2i(dx, dz);
        e[1] = octLength(dx, dz);
        if (swapped) {
            e[0] = (e[0] + 0x200) & 0x3ff;
            if (e[0] > 0x200) e[0] -= 0x400;
        }
        e[2] = e[3] = w.time.totalFrames;
    }
    std::int32_t r = e[0];
    if (swapped) {
        r = (r + 0x200) & 0x3ff;
        if (r > 0x200) r -= 0x400;
    }
    return r;
}

namespace {
// FUN_004BB9A0 (EAX = a, EDX = cur, EBX = step): cur + a when both have the same sign, a when
// they differ or cur == 0; a == 0 moves cur toward 0 by step without crossing it.
std::int32_t approach(std::int32_t a, std::int32_t cur, std::int32_t step) {
    if (cur > 0) {
        if (a > 0) return cur + a;
        if (a < 0) return a;
        const std::int32_t r = cur - step;
        return r >= 0 ? r : 0;
    }
    if (cur < 0) {
        if (a < 0) return cur + a;
        if (a > 0) return a;
        const std::int32_t r = cur + step;
        return r <= 0 ? r : 0;
    }
    return a;
}
} // namespace

// 0x4BC008: human move target = position + pad offset (player+0x38/+0x40, 0xC0 units). Analog
// pads: offset along the stick angle; digital: +-0xC0 per dpad axis, decaying to 0 by
// simFrames*32 per frame when released. Clamped to the pitch.
void Player_humanMoveTarget(World& w, int pid, Vec3& out) {
    PlayerState& p = P(w, pid);
    const ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
    if (c.analogDevice == 1) {
        if (c.analogMag == 0) {
            p.padOffX = 0;
            p.padOffZ = 0;
        } else {
            const SinCos sc = sinCos(static_cast<std::int16_t>(c.analogAngle & 0xffff));
            p.padOffX = fx(0xc0, sc.s);
            p.padOffZ = fx(0xc0, sc.c);
        }
    } else if (c.dirX == 0 && c.dirZ == 0) {
        const std::int32_t step = w.time.simFrames << 5;
        p.padOffZ = approach(0, p.padOffZ, step);
        p.padOffX = approach(0, p.padOffX, step);
    } else {
        p.padOffX = c.dirX > 0 ? 0xc0 : c.dirX < 0 ? -0xc0 : 0;
        p.padOffZ = c.dirZ > 0 ? 0xc0 : c.dirZ < 0 ? -0xc0 : 0;
    }
    out.x = p.posI.x + p.padOffX;
    out.z = p.posI.z + p.padOffZ;
    out.y = 0;
    Pitch_clampToField(w, out);
}

// 0x4BCBE0: support target = base-slot formation target; z = ball winner's z when within 0x330
// of him; pushed 0x1B0 toward the side the role moved to (role vs base role), z clamped to
// +-(halfLength - 0x90).
void Player_supportTarget(World& w, int pid, Vec3& out) {
    const PlayerState& p = P(w, pid);
    const TeamState& T = w.teams[static_cast<std::size_t>(p.team)];
    const int K = T.possWinner;
    Formation_attackParticipation(w, p.posCb, pid, out, -1, 1);
    if (K >= 0 && static_cast<std::uint32_t>(Player_cachedDistance(w, pid, K)) < 0x330) out.z = P(w, K).posI.z;
    out.z += ((p.role - p.baseRole >= 0) == (T.side == 1)) ? 0x1b0 : -0x1b0;
    const std::int32_t lim = w.pitch.halfLength - 0x90;
    if (-lim > out.z) out.z = -lim;
    else if (lim < out.z) out.z = lim;
}

// 0x4BCC80: offside-aware run target (offside rule on, phase != 1, opponent offside-line player
// team+0x1BC set): a target beyond the halfway line in the attacking half is held 0x30 short of
// the deeper of that defender and the ball (opponent keeper holding: the defender).
void Player_adjustRunTarget(World& w, int pid, const Vec3& in, Vec3& out) {
    out = in;
    if (!(w.opt.rules & 2) || w.match.phase == 1) return;
    const PlayerState& p = P(w, pid);
    const TeamState& T = w.teams[static_cast<std::size_t>(p.team)];
    const int D = w.teams[static_cast<std::size_t>(T.opponent)].f1bc;
    if (D < 0) return;
    if ((out.z >= 0 ? 1 : 0) == T.side) return;
    const PlayerState& d = P(w, D);
    const std::int32_t sgn = T.side == 1 ? 0x30 : -0x30;
    if ((d.posI.z >= 0 ? 1 : 0) == T.side) {
        out.z = sgn;
        return;
    }
    const std::int32_t gl = w.pitch.goalLineZ[w.teams[static_cast<std::size_t>(d.team)].side];
    const std::int32_t t = iabs(out.z - gl);
    const std::int32_t defD = iabs(d.posI.z - gl) - t;
    const std::int32_t ballD = iabs(w.ball.posI.z - gl) - t;
    const int poss = w.ball.possessor;
    const bool oppKeeper = poss >= 0 && P(w, poss).slot == 0 && P(w, poss).team != p.team;
    if (ballD >= defD || oppKeeper) {
        if (defD <= -0x30) return;
        out.z = d.posI.z + sgn;
    } else {
        if (ballD <= -0x30) return;
        out.z = w.ball.posI.z + sgn;
    }
}

// 0x532900 (EAX = runner, EDX = other player, EBX = desired target, ECX = out): steer a run
// around another player within 0x2D0 units. out = target, or a point 0x270 from the other
// player's position predicted 8 frames ahead (m98/m9c velocity), on the side away from the
// runner. Returns 1 when the runner should keep the steered target (sprint), 0 otherwise.
int Player_avoidPlayer(World& w, int pid, int other, const Vec3& target, Vec3& out) {
    const Vec3 tgt = target;
    out = tgt;
    if (pid == other || other < 0) return 0;
    const std::int32_t d = Player_cachedDistance(w, pid, other);
    if (d > 0x2d0) return 0;
    const Angle ang = Player_cachedAngle(w, pid, other);
    const PlayerState& p = P(w, pid);
    const PlayerState& o = P(w, other);
    Vec3 pred;
    pred.x = o.posI.x + ((o.m98 << 3) >> 16);
    pred.y = 0;
    pred.z = o.posI.z + ((o.m9c << 3) >> 16);
    if (octLength(tgt.x - pred.x, tgt.z - pred.z) < 0x290) { // 0x532B24
        offsetXZ(out, pred, 0x270, ang - 0x200);
        Pitch_clampToField(w, out);
        return 0;
    }
    const Angle toTarget = atan2i(tgt.x - p.posI.x, tgt.z - p.posI.z);   // edi
    const Angle predToTarget = atan2i(tgt.x - pred.x, tgt.z - pred.z);   // ecx
    const std::int32_t faceOff = wrapDiff(toTarget - o.facingDesired);
    bool steer = true;
    Angle a = 0;
    if (d < 0x290 && o.speed > 0x4ccc && iabs(faceOff) < 0x72) {
        const std::int32_t t = wrapDiff(ang - 0x200 - predToTarget);
        if (iabs(t) >= 0x72) a = predToTarget + (t > 0 ? 0x72 : -0x72); // 0x532B58
        else a = ang + 0x200;
    } else { // 0x532B88
        const std::int32_t side = absDiff(ang - 0x200 - predToTarget);
        const std::int32_t t = wrapDiff(predToTarget - toTarget);
        if (iabs(t) >= 0x72) steer = false;
        else if (side > 0x100) a = predToTarget + (t > 0 ? 0xe4 : -0xe4);
        else if (side >= 0x72) steer = false;
        else a = predToTarget + (t > 0 ? 0x72 : -0x72);
    }
    if (steer) offsetXZ(out, pred, 0x270, a); // 0x532A96
    Player_adjustRunTarget(w, pid, out, out); // 0x532A9B
    Pitch_clampToField(w, out);
    if (o.speed > 0x4ccc && absDiff(ang - o.facingDesired) < 0x72) return 1;
    return octLength(tgt.x - p.posI.x, tgt.z - p.posI.z) > 0x80 ? 1 : 0;
}

// 0x000 0x532C34: stand in position. Human: pad move. AI (countdown expired): walk to the
// positioning-callback target. Phase 9 -> state 4 once the countdown ends; flags278 & 0x20
// (outside phase 9) -> state 4 at once.
void State000(World& w, int pid) {
    PlayerState& p = P(w, pid);
    humanPadMove(w, pid);
    if (p.ctrl == 0 && p.countdown != 0) return;
    if (w.match.phase == 9) {
        if (p.countdown == 0) Player_setState(w, pid, 4, 0, 0, false, false);
        return;
    }
    if (w.match.flags278 & 0x20) {
        Player_setState(w, pid, 4, 0, 0, false, true);
        return;
    }
    if (p.ctrl != 0) return;
    Vec3 t;
    Formation_attackParticipation(w, p.posCb, pid, t, -1, 0);
    Pitch_clampToField(w, t);
    Player_moveToward_4BE804(w, pid, t);
}

// 0x001 0x532D18: short pause (human pad move allowed); back to state 0 after
// 2*(20 - A[0x35]) frames.
void State001(World& w, int pid) {
    PlayerState& p = P(w, pid);
    humanPadMove(w, pid);
    const std::uint32_t wait = static_cast<std::uint32_t>(2 * (0x14 - w.attrs[p.attr].at(0x35)));
    if (wait > static_cast<std::uint32_t>(p.stateTimer)) return;
    Player_setState(w, pid, 0, 0, 0, false, true);
}

// 0x532DA0: AI off-ball run step machine (stateSub2 0 pick target / 1, 2 follow it; restarts
// after 0x24 or 0x3C frames). Targets: formation/support position, attacking set-piece spots
// in phase 9 (wall248 mark spot or a random box spot), keeper-possession outlets, dodging the
// team's ball winner (Player_avoidPlayer).
void Player_offBallRun(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& T = w.teams[static_cast<std::size_t>(p.team)];
    const int K = T.possWinner;               // [esp+0x30]
    const std::uint32_t line = static_cast<std::uint32_t>(p.f014); // [esp+0x2C]
    const std::int32_t mode = p.stateSub2;
    if (static_cast<std::uint32_t>(mode) > 2) return;
    if (mode == 0) {
        std::int32_t res = 1;
        Vec3 tgt;
        if (p.flags22c & kB22D(0x20)) {
            Player_supportTarget(w, pid, tgt);
        } else { // 0x533165: evaluate on the copy slot +0x10
            const std::int32_t saved = p.curSlot;
            p.curSlot = p.slotCopy;
            Formation_attackParticipation(w, p.posCb, pid, tgt, -1, 0);
            p.curSlot = saved;
        }
        const Vec3 copy = tgt;
        if (w.match.phase == 9) {
            const std::int32_t kz = P(w, K).posI.z;
            bool partA = false;
            if (line == 3) partA = p.role == 4;
            else partA = true;
            if (partA) { // 0x532E25
                if (T.flags & 0x200) {
                    if (p.markSpot != 0) {
                        tgt = T.kickPos[p.markSpot - 1];
                    } else { // 0x53319A
                        const std::uint32_t nx = static_cast<std::uint32_t>(w.pitch.penAreaHalfWidth >> 4);
                        tgt.x = static_cast<std::int32_t>((w.rng.next() % nx) << 4);
                        const std::uint32_t nz = static_cast<std::uint32_t>((w.pitch.penAreaDepth - 0x90) >> 4);
                        tgt.z = w.pitch.halfLength - 0x90 - static_cast<std::int32_t>((w.rng.next() % nz) << 4);
                        if (p.posI.x < 0) tgt.x = -tgt.x;
                        if (T.side == 1) tgt.z = -tgt.z;
                    }
                    res = 2; // 0x532E4C
                    p.flags22c |= kB22D(0x40);
                    Player_requestSprint(w, pid);
                } else if (T.f1b4 != 0) { // 0x53323A
                    const std::uint32_t nz = static_cast<std::uint32_t>((w.pitch.penAreaDepth - 0x90) >> 4);
                    tgt.z = w.pitch.halfLength - 0x90 - static_cast<std::int32_t>((w.rng.next() % nz) << 4);
                    res = 2;
                    p.flags22c |= kB22D(0x40);
                } else if (!(p.role == 4 && !(p.flags22c & kB22D(0x20)))) { // 0x53321F
                    if ((w.ball.landing.z >= 0 ? 1 : 0) != T.side) { // 0x533291
                        const int ks = w.teams[static_cast<std::size_t>(P(w, K).team)].side;
                        const std::int32_t off = ks == 1 ? -0x90 : 0x90;
                        if ((tgt.z - (off + kz) >= 0 ? 1 : 0) == T.side) {
                            const std::int32_t kz2 = kz + (T.side == 1 ? -0x90 : 0x90);
                            tgt.z = kz2;
                            if ((kz2 + w.pitch.thirdLineZ[T.side] >= 0 ? 1 : 0) != T.side && p.markSpot != 0)
                                tgt.x = T.kickPos[p.markSpot - 1].x;
                            Pitch_clampToField(w, tgt);
                            p.flags22c |= kB22D(0x40);
                        }
                    }
                }
            }
            // 0x532E6B
            if (line < 4) {
                const int poss = w.ball.possessor;
                const bool ownKeeper = poss >= 0 && P(w, poss).slot == 0 &&
                                       w.teams[static_cast<std::size_t>(P(w, poss).team)].side == T.side;
                if (!ownKeeper) { // 0x53334F
                    const std::int32_t* s = &T.w310[static_cast<int>(line) * 3]; // team+0x310 + line*0xC
                    const Vec3 v10{s[0], s[1], s[2]};
                    P(w, K).flags22c |= 2;
                    std::int32_t dist = 0;
                    Team_nearestTargetToPoint(w, v10, p.team, p.curSlot, &dist);
                    P(w, K).flags22c &= ~2u;
                    if (dist > 0x240 && static_cast<std::uint32_t>(octLength(tgt.x - v10.x, tgt.z - v10.z)) > 0x240) {
                        tgt = v10;
                        p.flags22c |= kB22D(0x40);
                    }
                } else if (w.ball.flags & 0x20) {
                    std::int32_t bx = w.ball.posI.x;
                    if (T.side == 1) bx = -bx;
                    if ((bx > 0 && line == 2) || (bx < 0 && line == 1)) { // 0x532EDA
                        tgt = copy;
                        const std::int32_t z = w.pitch.penAreaLineZ - 0xf0;
                        tgt.z = T.side == 1 ? z : -z;
                    }
                }
            }
        }
        // 0x532F02
        bool check = p.role == 2 || p.curSlot == 1;
        if (check && !(p.flags22c & kB22D(0x20)) &&
            static_cast<std::uint32_t>(w.time.possessionFrames) < 0x3c) {
            if ((tgt.z - w.pitch.thirdLineZ[T.side] >= 0 ? 1 : 0) != T.side) { // 0x5333FD
                if ((w.ball.landing.z + w.pitch.thirdLineZ[T.side] >= 0 ? 1 : 0) == T.side) tgt = copy;
            }
        }
        Player_adjustRunTarget(w, pid, tgt, tgt); // 0x532F4E
        if (Player_avoidPlayer(w, pid, K, tgt, tgt) != 0) {
            Player_requestSprint(w, pid);
            Motion_setTargetAlt(w, pid, tgt);
            res = 2;
        } else { // 0x533432
            Player_moveToward_4BE804(w, pid, tgt);
            const PlayerState& k = P(w, K);
            bool sprintCheck = true;
            if ((k.posI.z >= 0 ? 1 : 0) == w.teams[static_cast<std::size_t>(k.team)].side) {
                const PlayerAttr& A = w.attrs[k.attr];
                if (A.f10 == 0 || (A.at(0x40) & 0x40)) // 0x5334B1
                    sprintCheck = (tgt.z + w.pitch.thirdLineZ[T.side] >= 0 ? 1 : 0) != T.side;
            }
            if (sprintCheck) { // 0x53346E
                const std::int32_t lim = 0x240 - T.ctrl1f4 * 0xc0;
                if (lim < p.targetDist) {
                    Player_requestSprint(w, pid);
                    res = 2;
                }
            }
        }
        p.stateSub2 = res; // 0x532F97
        p.stateTimer = p.index * 2;
        return;
    }
    // mode 1 (0x532FBD) / mode 2 (0x533082)
    if (mode == 1) {
        bool skip;
        if (!(T.flags & 0x200) && T.f1b4 == 0 && p.distBall >= 0x180) skip = false; // 0x5334EA -> 0x532FDF
        else skip = line < 4 || line == 6;
        if (skip) {
            p.stateTimer = 0;
            p.stateSub2 = 0;
            return;
        }
    }
    Vec3 loc;
    const std::int32_t z0 = p.posI.z;
    Player_adjustRunTarget(w, pid, p.posI, loc);
    if (z0 == loc.z) { // 0x533545 / 0x5335AC
        Player_adjustRunTarget(w, pid, Motion_targetVec(w, pid), loc);
        Player_moveToward_4BE804(w, pid, loc);
    } else {
        const Vec3 tv = Motion_targetVec(w, pid);
        if ((tv.z - loc.z >= 0 ? 1 : 0) == T.side) {
            Motion_setTargetAlt(w, pid, tv);
        } else { // 0x533513
            if (iabs(tv.x) > iabs(loc.z >> 1)) loc.x = tv.x;
            Player_moveToward_4BE804(w, pid, loc);
        }
        if (p.targetDist > 0x80) Player_requestSprint(w, pid);
    }
    if (mode == 1) { // 0x53304E
        if (static_cast<std::uint32_t>(p.stateTimer) < (line >= 4 ? 0x3cu : 0x24u)) return;
        p.stateTimer = 0;
        p.stateSub2 = 0;
        return;
    }
    if (absDiff(Motion_dirToTarget(w, pid) - p.facingDesired) < 0x55) Player_requestSprint(w, pid); // 0x5330F1
    if (p.distBall > 0x180 && static_cast<std::uint32_t>(p.stateTimer) < 0x24 && !(T.flags & 0x200) && T.f1b4 == 0)
        return;
    p.stateTimer = 0; // 0x53314E
    p.stateSub2 = 0;
}

} // namespace wc98::sim
