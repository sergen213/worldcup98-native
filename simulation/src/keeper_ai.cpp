// Goalkeeper decision planners 0x4BF830..0x4C2D00 and the trajectory plane scanners 0x5440F0..0x544404
// (owner: SimPorter2 / SimKeeperAI). Ported from the FIFA99.EXE v1.1 disassembly. The planners cache
// their last answer per ball kick in the team's keeper block 0x6734F0 + t*0x144 (sub-blocks at +0x24
// catch, +0x40 cross, +0x70 low shot, +0x88 shot, +0xA0 + i*0x2C states 0x209..0x20B).
#include "player_states.hpp"
#include "wc98/sim/sim.hpp"

namespace wc98::sim {
namespace {

TeamState& T(World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(p.team)]; }
KeeperBlock& KB(World& w, const PlayerState& p) { return w.pl.keeper[static_cast<std::size_t>(p.team)]; }
// Unnamed keeper-block dwords by block offset (+0x24..+0x9F).
std::int32_t* blk(KeeperBlock& b, int off) { return &b.k024[(off - 0x24) / 4]; }
Vec3 getV(const std::int32_t* p) { return {p[0], p[1], p[2]}; }
void setV(std::int32_t* p, const Vec3& v) {
    p[0] = v.x;
    p[1] = v.y;
    p[2] = v.z;
}
std::int32_t d16(std::uint32_t a) { return Keeper_d32(a) >> 16; } // `mov eax,[a]; sar eax,16`
std::uint32_t diveRow(int s) { return 0x580fa0 + (static_cast<std::uint32_t>(s) << 6); }
Vec3 goalOf(const World& w, int side) { return {0, 0, w.pitch.goalLineZ[side]}; } // 0x65F418 + side*0xC
std::int32_t now(const World& w) { return w.time.totalFrames; }
bool stampValid(const World& w, int k) { // stamp k set and still in the trajectory ring
    const int s = w.ball.stamp[k];
    return s != -1 && Ball_ringIndex(w, s - w.ball.consumed) >= 0;
}
bool ballDead(const World& w) {
    const Vec3& v = w.ball.vel;
    return v.x == 0 && v.y == 0 && v.z == 0 && w.ball.posI.y == 0;
}
Vec3 shr16v(const Vec3& v) { return {v.x >> 16, v.y >> 16, v.z >> 16}; }
std::int32_t wrapAbs(std::int32_t d) { // `and 0x3ff; > 0x200 -> 0x400 - d`
    d &= 0x3ff;
    return d > 0x200 ? 0x400 - d : d;
}
// Two-step 16.16 division `cdq; idiv b; shrd eax,edx,16; sar edx,16; idiv b` (num already scaled).
std::int32_t fxDiv2(std::int32_t num, std::int32_t den) {
    const std::int32_t q1 = num / den, r = num % den;
    const auto q2 = static_cast<std::int32_t>((static_cast<std::int64_t>(r) << 16) / den);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(q1) << 16) + q2;
}
// 0x4BAE20 (EAX from, EDX to, EBX out): octDistance plus heading.
struct DistAng {
    std::int32_t dist, dx, dz, ang;
};
DistAng vecAng(const Vec3& from, const Vec3& to) {
    const OctDist d = octDistance(from, to);
    return {d.dist, d.dx, d.dz, atan2i(d.dx, d.dz)};
}

// 0x4C2BA4 (EAX team): the team's ball winner, or the last possessor when he is a team-mate within
// 0x50 of the ball and nearer it (unless the winner is the kick receiver).
int winner_4C2BA4(const World& w, int team) {
    const TeamState& t = w.teams[static_cast<std::size_t>(team)];
    const int r = t.possWinner;
    if (r == t.f1e0) return r;
    const int q = w.ball.prevPossessor;
    if (q == -1 || r == q || r == -1) return r;
    if (P(w, r).team != P(w, q).team) return r;
    const std::int32_t d = P(w, q).distBall;
    if (d < 0x50 && d < P(w, r).distBall) return q;
    return r;
}

// 0x4C22C8 (EAX frames): no post/bar hit (stamp 8) before `frames`.
bool noPostBefore_4C22C8(const World& w, std::int32_t frames) {
    const int s = w.ball.stamp[8];
    if (s == -1 || Ball_ringIndex(w, s - w.ball.consumed) < 0) return true;
    Vec3 p;
    int rel = 0;
    std::uint32_t fl = 0;
    Ball_forecastInt(w, s, &p, &rel, &fl);
    return frames <= rel;
}

// 0x4C2290: (kickCount % (20 - attr[0x30])) * 0x30, 0 for a 20 rating.
std::int32_t reachSlack_4C2290(const World& w, int pid) {
    const std::int32_t r = 0x14 - static_cast<std::int32_t>(Player_attr(w, pid, 0x30));
    if (r == 0) return 0;
    return (w.ball.kickCount % r) * 0x30;
}

// 0x4C0BCC (EAX side, EDX point): point inside the side's goal frame (+0x30 over the bar, +0x50 wide).
bool onTarget_4C0BCC(const World& w, int side, const Vec3& p) {
    if (side == 1 && p.z <= 0) return false;
    if (side == 0 && p.z >= 0) return false;
    if (w.pitch.goalHeight + 0x30 < p.y) return false;
    return iabs(p.x) <= w.pitch.goalHalfWidth + 0x50;
}

// 0x4C093C (EDX point): point on the pitch width (0x60 margin) and not past this keeper's goal line.
bool beforeGoalLine_4C093C(const World& w, int pid, const Vec3& p) {
    if (iabs(p.x) > w.pitch.halfWidth - 0x60) return false;
    const int side = w.teams[static_cast<std::size_t>(P(w, pid).team)].side;
    if (side == 1) return w.pitch.goalLineZ[1] - 0x60 <= p.z;
    return w.pitch.goalLineZ[side] + 0x60 >= p.z;
}

// 0x4C0834: free ball (not last touched by a team-mate) going over or wide of this goal: it crosses
// a goal line (stamp 5) off target, high (>= 0x156 now and there) and lands off the pitch.
bool goingOut_4C0834(const World& w, int pid) {
    if (w.ball.possessor != -1) return false;
    const int q = w.ball.prevPossessor;
    if (q != -1 && P(w, pid).team == P(w, q).team) return false;
    if (stampValid(w, 8)) return false;
    if (!stampValid(w, 5)) return false;
    Vec3 p;
    int rel = 0;
    std::uint32_t fl = 0;
    Ball_forecastInt(w, w.ball.stamp[5], &p, &rel, &fl);
    if (!beforeGoalLine_4C093C(w, pid, p)) return true;
    if (onTarget_4C0BCC(w, w.teams[static_cast<std::size_t>(P(w, pid).team)].side, p)) return false;
    if (w.ball.posI.y < 0x156 || p.y < 0x156) return false;
    return !Pitch_onField(w, w.ball.landing);
}

// 0x4BAEAC (EAX p, EDX dx, EBX dz, ECX q, stack ux, uz, out; all 16.16): intersection of the line
// p + t*(dx, dz) with the line through q along (ux, uz); 0 when (near) parallel. out.y = 0.
bool lineHit_4BAEAC(const Vec3& p, Fixed dx, Fixed dz, const Vec3& q, Fixed ux, Fixed uz, Vec3& out) {
    const Fixed c1 = fx(dx, uz), c2 = fx(dz, ux);
    if (c1 == c2) return false;
    const std::int32_t num = fx(p.x - q.x, uz) - fx(p.z - q.z, ux);
    const std::int32_t den = c2 - c1;
    if (iabs(den) <= iabs(num) >> 14) return false;
    const std::int32_t t = fxDiv2(num, den);
    out.x = p.x + fx(t, dx);
    out.y = 0;
    out.z = p.z + fx(t, dz);
    return true;
}

// 0x4C09A4: the ball threatens this keeper's goal: in his half and its post/bar (8) or goal-line (5)
// stamp is on target, or it moves toward the goal and (goal-area stamp 10, or already in the goal
// area) its straight line meets the goal line on target.
bool threat_4C09A4(World& w, int pid) {
    const int side = T(w, P(w, pid)).side;
    const Vec3& b = w.ball.posI;
    if (side == 1 && b.z < 0) return false;
    if (side == 0 && b.z > 0) return false;
    for (int k : {8, 5}) {
        if (!stampValid(w, k)) continue;
        Vec3 p;
        int rel = 0;
        std::uint32_t fl = 0;
        Ball_forecastInt(w, w.ball.stamp[k], &p, &rel, &fl);
        if (onTarget_4C0BCC(w, side, p)) return true;
    }
    if (side == 1 ? w.ball.vel.z <= 0 : w.ball.vel.z >= 0) return false;
    if (!stampValid(w, 10) && !Pitch_inGoalArea(w, side, w.ball.posI)) return false;
    const Vec3 g = goalOf(w, side);
    const Vec3 g16{g.x << 16, g.y << 16, g.z << 16};
    Vec3 x;
    if (!lineHit_4BAEAC(w.ball.pos, w.ball.vel.x, w.ball.vel.z, g16, 0x10000, 0, x)) return false;
    return onTarget_4C0BCC(w, side, shr16v(x));
}

// 0x4C2D00: frames until the ball reaches the post/bar (8) or goal line (5), minus 8, in 0..7.
std::int32_t lateFrames_4C2D00(const World& w) {
    std::int32_t m = 10000;
    for (int k : {8, 5}) {
        Vec3 p;
        int rel = 0;
        std::uint32_t fl = 0;
        if (Ball_forecastInt(w, w.ball.stamp[k], &p, &rel, &fl) && rel < m) m = rel;
    }
    m -= 8;
    if (m < 0) return 0;
    return m > 7 ? 7 : m;
}

// 0x4C2BF4: random reaction delay (frames) for the dive pick: grows with distance and ball spin,
// shrinks with attr[0x35]; scaled by difficulty (team+0x1FC) and a human opponent.
std::uint8_t reactDelay_4C2BF4(World& w, int pid) {
    const PlayerState& p = P(w, pid);
    const bool human = static_cast<std::uint32_t>(w.teams[static_cast<std::size_t>(T(w, p).opponent)].humans) > 0;
    const auto c = static_cast<std::uint32_t>(T(w, p).ctrl1fc);
    Fixed scale;
    if (c == 0) scale = human ? 0x18000 : 0x10000;
    else if (c == 2) scale = human ? 0x9999 : 0xb333;
    else scale = 0xcccc;
    std::int32_t t = fx(w.ball.spin + 0xccc, static_cast<std::int32_t>(static_cast<std::uint32_t>(p.distBall) << 16)) >> 16;
    t = iabs(t) >> 4;
    const std::int32_t base = 0x14 - static_cast<std::int32_t>(Player_attr(w, pid, 0x35));
    std::int32_t v = fx(static_cast<std::int32_t>(static_cast<std::uint32_t>(base + t) << 16), scale) >> 16;
    if (v < 0) return 0;
    if (v > static_cast<std::int16_t>(lateFrames_4C2D00(w))) v = static_cast<std::int16_t>(lateFrames_4C2D00(w));
    if (v == 0) return 0;
    return static_cast<std::uint8_t>(w.rng.next() % static_cast<std::uint32_t>(v));
}

// Candidate anim records 0x58911C (cross: 2) / 0x58919C (catch: 4), 0x40 bytes each. Only the input
// fields are static; +0x18..+0x3F are recomputed by every 0x4BE33C call before use.
struct Cand {
    std::int32_t anim, reachOpp, reachNear; // +0, +4, +8
    std::uint32_t maxApex;                  // +0xC (apex height, unsigned compare)
    std::int32_t checkApex, alongVel;       // +0x10, +0x14
};
constexpr Cand kCross[2] = {{0xc1, 0x140, 0x3e80, 0x10, 1, 1}, {0xd8, 0xa0, 0x280, 0x20, 1, 0}};
constexpr Cand kCatch[4] = {{0xc1, 0x140, 0x3e80, 0x10, 1, 1}, {0xd8, 0, 0x1e0, 0x20, 1, 0},
                            {6, 0x20, 0x3e80, 0x20, 0, 1}, {5, 0x20, 0x3e80, 0x20, 0, 1}};
struct CandWork {
    std::int32_t contact, dist, cang, yaw, boneH, ok, done; // +0x18..+0x30
    Vec3 meet;                                              // +0x34
};

// 0x4BE33C (EAX keeper, EDX opponent q, EBX records, ECX count, stack frames*, ball*, outFrames*, outMeet*,
// outAnim*; ret 0x14): step 5 frames at a time (from 5 up to 0x3C0) along the predicted flight below
// 0x8E until some anim can meet the ball: q must not get there first (unless q is the keeper), the
// apex/bone heights must fit and the keeper must cover the distance to the meeting point in time.
bool solveIntercept_4BE33C(World& w, int pid, int q, const Cand* c, int n, std::int32_t* frames, Vec3* ball,
                           std::int32_t* outFrames, Vec3* outMeet, std::int32_t* outAnim) {
    PlayerState& k = P(w, pid);
    CandWork r[4];
    *frames = 5;
    for (int i = 0; i < n; ++i) {
        r[i].done = 0;
        r[i].boneH = Anim_boneHeightAt(w, c[i].anim, -0x62);
        r[i].yaw = Anim_yawAt(w, c[i].anim, 0);
        r[i].contact = Anim_contactInfo(w, c[i].anim, 0, 0, -0x62, 1, &r[i].dist, &r[i].cang);
        if (r[i].contact == -1) return false;
    }
    for (;;) {
        if (*frames > 0x3c0) return false;
        if (!Ball_predictPos(w, *frames, 0, ball)) return false;
        if (ball->y >= 0x8e) {
            *frames += 5;
            continue;
        }
        Vec3 vel;
        if (!Ball_predictVel(w, *frames, 0, &vel)) return false;
        for (int i = 0; i < n; ++i) {
            CandWork& e = r[i];
            if (e.done) continue;
            e.ok = 1;
            if (e.contact > *frames) {
                e.ok = 0;
                continue;
            }
            // ponytail: q == -1 (no ball winner) would fault in the exe (reads null + 0xD4); skipped here.
            if (pid != q && q != -1) {
                const PlayerState& o = P(w, q);
                const std::int32_t dx = ball->x - o.posI.x, dz = ball->z - o.posI.z;
                const std::uint32_t d2 = static_cast<std::uint32_t>(dx) * static_cast<std::uint32_t>(dx) +
                                         static_cast<std::uint32_t>(dz) * static_cast<std::uint32_t>(dz);
                const std::int32_t m = Motion_predictDistance(w, q, o.sprintMax, atan2i(dx, dz), *frames, 1, 1);
                const auto a = static_cast<std::uint32_t>(c[i].reachOpp + m);
                if (a * a > d2) {
                    e.ok = 0;
                    e.done = 1;
                    continue;
                }
                const auto b = static_cast<std::uint32_t>(m + c[i].reachNear);
                if (b * b < d2) {
                    e.ok = 0;
                    continue;
                }
            }
            if (c[i].checkApex) {
                Fixed vy = 0;
                if (!Ball_predictVy(w, *frames, 0, &vy)) {
                    e.done = 1;
                    e.ok = 0;
                    continue;
                }
                if (static_cast<std::uint32_t>(vy >> 16) >= c[i].maxApex) {
                    e.ok = 0;
                    continue;
                }
            }
            if (iabs(ball->y - e.boneH) > 0x18) {
                e.ok = 0;
                continue;
            }
            Player_meetPointCore_4BE168(w, pid, e.dist, e.cang, e.yaw, *ball, vel.x, vel.z,
                                        c[i].alongVel ? nullptr : &k.posI, e.meet);
            const std::int32_t dx = e.meet.x - k.posI.x, dz = e.meet.z - k.posI.z;
            const std::int32_t d2 = static_cast<std::int32_t>(static_cast<std::uint32_t>(dx) * static_cast<std::uint32_t>(dx) +
                                                              static_cast<std::uint32_t>(dz) * static_cast<std::uint32_t>(dz));
            const std::int32_t m =
                Motion_predictDistance(w, pid, k.sprintMax, atan2i(dx, dz), *frames - e.contact, 1, 1);
            if (static_cast<std::int32_t>(static_cast<std::uint32_t>(m) * static_cast<std::uint32_t>(m)) < d2) e.ok = 0;
        }
        bool allDone = true;
        for (int i = 0; i < n; ++i) {
            if (r[i].ok) {
                *outFrames = *frames - r[i].contact;
                *outMeet = r[i].meet;
                *outAnim = c[i].anim;
                return true;
            }
            if (!r[i].done) allDone = false;
        }
        if (allDone) return false;
        *frames += 5;
    }
}

// 0x4C0390 (ret 0x14): the keeper already has the ball, or it is free and 0x4BE33C finds an
// on-field ball point and meeting point.
bool solve_4C0390(World& w, int pid, int q, const Cand* c, int n, std::int32_t* frames, Vec3* ball,
                  std::int32_t* outFrames, Vec3* meet, std::int32_t* anim) {
    const int poss = w.ball.possessor;
    if (poss == pid) return true;
    if (poss != -1) return false;
    if (!solveIntercept_4BE33C(w, pid, q, c, n, frames, ball, outFrames, meet, anim)) return false;
    return Pitch_onField(w, *ball) && Pitch_onField(w, *meet);
}
// 0x4C0320 / 0x4C0358: against the opponents' / own ball winner (0x4C2BA4).
bool solveVsOpp_4C0320(World& w, int pid, const Cand* c, int n, std::int32_t* frames, Vec3* ball,
                       std::int32_t* outFrames, Vec3* meet, std::int32_t* anim) {
    const int q = winner_4C2BA4(w, T(w, P(w, pid)).opponent);
    return solve_4C0390(w, pid, q, c, n, frames, ball, outFrames, meet, anim);
}
bool solveVsOwn_4C0358(World& w, int pid, const Cand* c, int n, std::int32_t* frames, Vec3* ball,
                       std::int32_t* outFrames, Vec3* meet, std::int32_t* anim) {
    const int q = winner_4C2BA4(w, P(w, pid).team);
    return solve_4C0390(w, pid, q, c, n, frames, ball, outFrames, meet, anim);
}
// 0x4C02AC (EAX keeper, EDX records, EBX count, ECX frames*, stack ball*, outFrames*, meet*, anim*).
bool solveBoth_4C02AC(World& w, int pid, const Cand* c, int n, std::int32_t* frames, Vec3* ball,
                      std::int32_t* outFrames, Vec3* meet, std::int32_t* anim) {
    if (!solveVsOpp_4C0320(w, pid, c, n, frames, ball, outFrames, meet, anim)) return false;
    return solveVsOwn_4C0358(w, pid, c, n, frames, ball, outFrames, meet, anim);
}

// 0x4BFC7C (EDX ball point, EBX frames): the opponents' winner cannot reach the point in time
// (more than 0x320 short) or the ball is above 0x20 there.
bool oppTooFar_4BFC7C(World& w, int pid, const Vec3& pt, std::int32_t frames) {
    const int q = winner_4C2BA4(w, T(w, P(w, pid)).opponent);
    // ponytail: q == -1 would fault in the exe (null + 0xD4); treated as "too far".
    if (q == -1) return true;
    const OctDist d = octDistance(P(w, q).posI, pt);
    const std::int32_t m = Motion_predictDistance(w, q, P(w, q).sprintMax, atan2i(d.dx, d.dz), frames, 1, 1);
    if (m < d.dist - 0x320) return true;
    return pt.y > 0x20;
}

// 0x4BFCF0 (EDX from, EBX ball point, ECX anim): 0 when the anim's contact point and the ball point
// are both in this keeper's box, else 1 (also 1 without a contact mark).
bool outsideBox_4BFCF0(World& w, int pid, const Vec3& from, const Vec3& pt, int anim) {
    Vec3 out;
    if (!Player_animContactPoint_4BE26C(w, anim, from, pt, 0, 0, out)) return true;
    const int side = T(w, P(w, pid)).side;
    const bool a = Pitch_inPenaltyBox(w, side, pt);
    const bool b = Pitch_inPenaltyBox(w, side, out);
    return !(a && b);
}

// Trajectory plane scanner 0x544174 / 0x54426C (state 0x6A39BC..0x6A39F0, only used back to back by
// 0x4C1490): first ring step where the ball crosses the plane (point, normal).
struct PlaneScan {
    Vec3 prev, pt, n;
    std::int32_t prevIdx = 0, prevSign = 0, idx = 0, done = 1;
};
// 0x5440F0 (EAX a, EDX b, EBX n): sign of fx-dot(a - b, n).
int planeSide_5440F0(const Vec3& a, const Vec3& b, const Vec3& n) {
    const std::int32_t s = fx(a.x - b.x, n.x) + fx(a.y - b.y, n.y) + fx(a.z - b.z, n.z);
    return s < 0 ? -1 : (s > 0 ? 1 : 0);
}
// 0x476084 (EDX ring index): frames from the read index to it, + 1; -1 when not yet available.
std::int32_t ringFrames_476084(const World& w, std::int32_t k) {
    const BallState& b = w.ball;
    if (k < b.readIdx) k += kRingSize;
    k -= b.readIdx;
    if (static_cast<std::uint32_t>(k) > static_cast<std::uint32_t>(b.avail)) return -1;
    return k + 1;
}
// 0x544174 (EAX plane point, EDX normal).
bool planeStart_544174(const World& w, PlaneScan& s, const Vec3& pt, const Vec3& dir) {
    s.done = 1;
    if (w.ball.avail == 0) return false;
    Vec3 n = dir;
    while (iabs(n.x) > 0x10000 || iabs(n.y) > 0x10000 || iabs(n.z) > 0x10000) {
        n.x >>= 1;
        n.y >>= 1;
        n.z >>= 1;
    }
    s.n = n;
    s.pt = pt;
    s.idx = s.prevIdx = w.ball.readIdx;
    s.done = 0; // (+0x6A39E4 flags = 0: never read)
    s.prev = w.ball.pos;
    s.prevSign = planeSide_5440F0(s.prev, s.pt, s.n);
    return true;
}
// 0x54426C (EAX out pos, EDX out frames, EBX out flags, ECX out side).
bool planeNext_54426C(const World& w, PlaneScan& s, Vec3* pos, std::int32_t* frames, std::uint32_t* flags,
                      std::int32_t* side) {
    if (s.done) return false;
    for (;;) {
        const Vec3 cur = w.ball.ring[s.idx].pos;
        const int sg = planeSide_5440F0(cur, s.pt, s.n);
        if (sg != s.prevSign && s.prevSign != 0) {
            *flags = w.ball.ring[s.prevIdx].flags;
            *pos = s.prev;
            *frames = ringFrames_476084(w, s.prevIdx);
            *side = s.prevSign > 0 ? 1 : 0;
            s.prevSign = sg;
            s.prev = cur;
            return true;
        }
        s.prevSign = sg;
        s.prev = cur;
        s.prevIdx = s.idx;
        if (++s.idx >= kRingSize) s.idx -= kRingSize;
        if (s.idx == w.ball.writeIdx) return false;
    }
}

// Height scanner 0x5443B0 / 0x544404 (state 0x6A39A0..0x6A39B8, used back to back by 0x4C182C /
// 0x4C2688): ring steps where the ball passes the height y (16.16).
struct HeightScan {
    Vec3 prev;
    std::int32_t idx = 0, y = 0, done = 1;
    std::uint32_t flags = 0;
};
bool heightStart_5443B0(const World& w, HeightScan& s, std::int32_t y16) {
    s.done = 1;
    if (w.ball.avail == 0) return false;
    s.y = y16;
    s.flags = 0;
    s.done = 0;
    s.prev = w.ball.pos;
    s.idx = w.ball.readIdx;
    return true;
}
// 0x544404 (EAX out pos (16.16), EDX out frames, EBX out flags): the step nearer the height.
bool heightNext_544404(const World& w, HeightScan& s, Vec3* pos, std::int32_t* frames, std::uint32_t* flags) {
    if (s.done) return false;
    for (;;) {
        const BallEntry& e = w.ball.ring[s.idx];
        const Vec3 cur = e.pos;
        const std::int32_t d1 = cur.y - s.y, d0 = s.prev.y - s.y;
        if ((d1 > 0 && d0 < 0) || (d1 <= 0 && d0 > 0)) {
            if (iabs(d1) > iabs(d0)) {
                *flags = s.flags;
                *pos = s.prev;
                *frames = ringFrames_476084(w, s.idx) - 1;
            } else {
                *flags = e.flags;
                *pos = cur;
                *frames = ringFrames_476084(w, s.idx);
            }
            s.prev = cur;
            s.flags = e.flags;
            return true;
        }
        s.prev = cur;
        s.flags = e.flags;
        if (++s.idx >= kRingSize) s.idx -= kRingSize;
        if (s.idx == w.ball.writeIdx) return false;
    }
}

// 0x4C232C (EDX out point, EBX out normal; 16.16): the plane the keeper defends: through a point
// 0x10 toward the ball, normal toward the far post the shot is going for (normalised by 0x4BB820).
void savePlane_4C232C(World& w, int pid, Vec3& outPos, Vec3& outN) {
    PlayerState& p = P(w, pid);
    const std::int32_t marg = 0x20;
    const int side = T(w, p).side;
    const Vec3 p16{p.posI.x << 16, 0, p.posI.z << 16};
    const Vec3 g16{0, 0, w.pitch.goalLineZ[side] << 16}; // 0x65F418 + side*0xC with z << 16
    Angle a;
    if (w.ball.posI.x == p.posI.x && w.ball.posI.z == p.posI.z) a = side == 1 ? 0x200 : 0;
    else a = p.angleBall;
    const std::int32_t d = p.distBall < 0x10 ? p.distBall : 0x10;
    const Vec3 s = offsetInDirection(p.posI, d, a); // 0x4BB0D0 (x/z)
    outPos = {s.x << 16, 0, s.z << 16};
    const SinCos sc = sinCos(a);
    outN = {sc.s, 0, sc.c};
    Vec3 x;
    if (!lineHit_4BAEAC(p16, outN.z, -outN.x, g16, 0x10000, 0, x)) return;
    const std::int32_t xi = x.x >> 16;
    const std::int32_t lim = w.pitch.goalHalfWidth + marg;
    if (iabs(xi) > lim) return;
    const std::int32_t px = p.posI.x;
    bool between = false;
    if (px >= 0 && xi >= 0 && xi <= px) between = true;
    if (px <= 0 && xi <= 0 && xi >= px) between = true;
    std::int32_t e = x.x > 0 ? lim : -lim;
    if (between) e = -e;
    const std::int32_t zt = x.z >> 16;
    outN.x = static_cast<std::int32_t>(static_cast<std::uint32_t>(zt - p.posI.z) << 16);
    outN.z = -static_cast<std::int32_t>(static_cast<std::uint32_t>(e - px) << 16);
    outN.y = 0;
    Math_normalize3(outN, outN);
}

// 0x4C1490 (EDX out point, EBX out frames): where/when the shot crosses the keeper's save plane;
// cached per kick in block +0x88 {kick, frame, point, result}. Returns the cached result, else 1 only
// for a fresh crossing in state 0x203 (other states see it from the next frame on).
int shotCross_4C1490(World& w, int pid, Vec3* pos, std::int32_t* frames) {
    PlayerState& p = P(w, pid);
    KeeperBlock& kb = KB(w, p);
    std::int32_t* S = blk(kb, 0x88);
    const std::int32_t kc = w.ball.kickCount;
    std::int32_t fr = 0;
    const bool hit = kc == S[0] && kb.k12c == 0 && threat_4C09A4(w, pid);
    if (pid == w.ball.dribbler || hit) {
        *pos = getV(S + 2);
        *frames = S[1] - now(w);
        return S[5];
    }
    Vec3 a, n;
    savePlane_4C232C(w, pid, a, n);
    PlaneScan ps;
    if (planeStart_544174(w, ps, a, n)) {
        std::uint32_t fl = 0;
        std::int32_t sd = 0;
        if (planeNext_54426C(w, ps, pos, frames, &fl, &sd)) {
            *pos = shr16v(*pos);
            fr = *frames;
            if (fr <= 0 || noPostBefore_4C22C8(w, fr)) {
                S[0] = kc;
                S[1] = *frames + now(w);
                setV(S + 2, *pos);
                S[5] = 1;
                kb.k12c = 0;
                return p.state == 0x203 ? 1 : 0;
            }
        }
    }
    if (!stampValid(w, 2) && !ballDead(w)) return 0; // 0x4C1555
    if (fr > 0 && !noPostBefore_4C22C8(w, fr)) return 0;
    S[0] = kc;
    S[5] = 0;
    kb.k12c = 0;
    return 0;
}

// 0x4C0C2C (EDX ball point, EBX frames): pick the dive state 0x20C..0x22F (rows 0x58929C + i*0x40,
// whose i16 is 0x20C + i, describing dive row 0x580FA0 + s*0x40) that best meets the ball, widening
// the height tolerance by 8 per pass (k = 0x10..0xA0); -1 none. Schedules a split at the anim start.
int chooseDive_4C0C2C(World& w, int pid, const Vec3& ball, std::int32_t frames) {
    PlayerState& p = P(w, pid);
    KeeperBlock& kb = KB(w, p);
    const int side = T(w, p).side;
    const bool left = Player_pointLeftOf_4BE080(w, pid, ball);
    const Vec3 P0 = p.posI;
    std::uint8_t delay = reactDelay_4C2BF4(w, pid);
    kb.k13c = left ? 1 : 0;
    int best = -1;
    std::int32_t first = 1;
    Vec3 vel;
    if (!Ball_predictVel(w, frames, 1, &vel)) vel = w.ball.vel;
    std::int32_t bestScore = 0x7fffffff, bestE = 0x7fffffff;
    std::int32_t allow = 1;
    if (T(w, p).f204 != 0) allow = 0;
    if (kb.busy != 0) allow = 0;
    if (!Pitch_inPenaltyBox(w, side, ball)) {
        allow = 0;
        delay = 0;
    } else if (allow == 0) {
        delay = 0;
    }
    kb.k018 = (kb.k018 & ~0xffu) | delay; // byte +0x18
    const std::int32_t lim2 = static_cast<std::int32_t>(static_cast<std::uint32_t>((frames >> 1) + 0x12) << 16);
    std::int32_t k = 0;
    while (best == -1) {
        k += 0x10;
        if (k > 0xa0) return -1;
        for (int i = 0; i < 36; ++i) {
            const int s = 0x20c + i;
            const std::uint32_t D = diveRow(s);
            const std::int32_t tol = (k >> 1) + 0x10;
            const std::int32_t inBoxRow = Keeper_d32(D + 0x34) == 1 ? 1 : 0;
            const std::int32_t h = d16(D + 0xe);
            const int anim = left ? Keeper_d32(D) : Keeper_d32(D + 4);
            const std::uint8_t kind = Keeper_d8(D + 0x24);
            const std::int32_t timed = Keeper_d32(D + 0x38) == 1 ? 1 : 0;
            const std::int32_t cf = d16(D + 6), cd = d16(D + 8);
            const std::int32_t isCatch = (kind == 4 || kind == 3 || kind == 1) ? 1 : 0;
            if (inBoxRow == allow) continue;
            if (iabs(h - ball.y) > tol) continue;
            if (timed == 1 && cf > frames) continue;
            if (timed == 1) {
                Vec3 v2;
                if (Ball_predictVel(w, frames, 0, &v2) && octLength(v2.x, v2.z) > lim2) continue;
            }
            if (isCatch) {
                if (first == 0 && bestE == 0) continue;
                Vec3 v3;
                if (Ball_predictVel(w, frames, 0, &v3) &&
                    iabs(v3.y >> 16) + iabs(v3.x >> 16) + iabs(v3.z >> 16) < 5)
                    continue;
            }
            const std::int32_t K = kb.k010;
            if (K != -1) {
                const std::int32_t c7 = Keeper_d8(D + 0x27), c8 = Keeper_d8(D + 0x28);
                if (!(K % 3 == 2 && c7 % 3 == 2) && K != c7 && K != c8) continue;
            }
            Vec3 M;
            Player_meetPoint_4BE0FC(w, pid, anim, ball, vel.x, vel.z, nullptr, M);
            if (!isCatch) {
                Vec3 C;
                if (Player_animContactPoint_4BE26C(w, anim, M, ball, 0, 0, C) && !Pitch_inPenaltyBox(w, side, C) &&
                    !Pitch_inGoal(w, side, C))
                    continue;
            }
            std::int32_t e = 0, md = 0;
            Angle ang = 0;
            if (!(P0.x == M.x && P0.z == M.z)) {
                const OctDist vt = Math_vecTo(P0, M);
                ang = atan2i(vt.dx, vt.dz);
                std::int32_t avail = frames - cf - delay;
                if (avail < 0) avail = 0;
                md = Motion_predictDistance(w, pid, p.sprintMax, ang, avail, 1, 0);
                if (timed) md -= 0x20;
                if (timed && md < vt.dist && vt.dist > 0x30) continue;
                e = vt.dist - md;
                if (e <= 0) e = 0;
            }
            const OctDist d1 = Math_vecTo(M, ball), d2 = Math_vecTo(p.posI, ball);
            std::int32_t g = d1.dist - d2.dist;
            if (g <= 0) g = 0;
            std::int32_t turn = 0;
            if (e != 0 && !(d2.dx == 0 && d2.dz == 0)) {
                const Angle a2 = atan2i(d1.dx, d1.dz); // exe tests d2 for zero but takes d1's angle
                const Vec3 S = offsetInDirection(P0, md, ang); // 0x4BB0D0 (x/z)
                if (!(S.x == M.x && S.z == M.z)) turn = wrapAbs(a2 - vecAng(S, ball).ang);
            }
            const std::int32_t score = (g >> 1) + e + turn * 3;
            const std::int32_t total = (cd >> 1) + score + (cd >> 2);
            if (score == 0 && !isCatch && first != 0) {
                bestScore = total;
                bestE = score;
                first = 0;
                best = s;
                continue;
            }
            if (total >= bestScore) continue;
            if (isCatch && first == 0 && bestE <= 0) continue;
            bestScore = total;
            bestE = score;
            best = s;
            first = isCatch ? 1 : 0;
        }
    }
    Sim_scheduleSplit(w, frames - d16(diveRow(best) + 6));
    return best;
}

} // namespace

// 0x4BF830 (EDX t1*, EBX p1*, ECX t2*, stack p2*, anim*; ret 8): come for a cross/high ball: frames to
// the ball (t1) at p1, frames to start the anim (t2) at p2. Cached per kick in block +0x40 {kick,
// frame, p1, opponent winner, anim, p2, frame2, last evaluation}; block +0x130 = 1 "no cross plan".
int Keeper_crossPlan_4BF830(World& w, int pid, std::int32_t* t1, Vec3* p1, std::int32_t* t2, Vec3* p2,
                            std::int32_t* anim) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    if (w.ball.possessor != -1) {
        kb.k130 = 1;
        return 0;
    }
    if ((p.posI.z >= 0 ? 1 : 0) != t.side || t.f204 != 0) {
        kb.k130 = 1;
        return 0;
    }
    if (kb.busy == 1) {
        kb.k130 = 1;
        return 0;
    }
    std::int32_t* R = blk(kb, 0x40);
    const std::int32_t kc = w.ball.kickCount;
    const int q = winner_4C2BA4(w, t.opponent);
    const bool same = kc == R[0] && kb.k130 == 0;
    auto cached = [&] {
        *t1 = R[1] - now(w);
        *p1 = getV(R + 2);
        *t2 = R[10] - now(w);
        *p2 = getV(R + 7);
        *anim = R[6];
    };
    if ((static_cast<std::uint32_t>(R[1] - now(w)) <= 10 || q == R[5]) && same) {
        cached();
        return 1;
    }
    if (static_cast<std::uint32_t>(now(w) - R[11]) < 0x14) return 0;
    if (t.side == 1 ? w.ball.landing.z <= 0 : w.ball.landing.z >= 0) {
        kb.k130 = 1;
        return 0;
    }
    if (!solveBoth_4C02AC(w, pid, kCross, 2, t1, p1, t2, p2, anim)) {
        kb.k130 = 1;
        return 0;
    }
    const bool keep = p.state == 0x207 && kc == R[0];
    if (keep) cached();
    R[11] = now(w);
    if (!keep) {
        if (oppTooFar_4BFC7C(w, pid, *p1, *t1) || outsideBox_4BFCF0(w, pid, *p2, *p1, *anim) ||
            !noPostBefore_4C22C8(w, *t1)) {
            kb.k130 = 1;
            return 0;
        }
    }
    if (!Pitch_inPenaltyBox(w, t.side, *p1)) {
        kb.k130 = 1;
        return 0;
    }
    R[0] = kc;
    R[1] = *t1 + now(w);
    setV(R + 2, *p1);
    R[10] = *t2 + now(w);
    setV(R + 7, *p2);
    R[6] = *anim;
    R[5] = q;
    std::int32_t d = 0, a = 0;
    Anim_contactInfo(w, *anim, 0, 0, -0x62, 1, &d, &a); // result unused
    Sim_scheduleSplit(w, *t2);
    kb.k130 = 0;
    return 1;
}

// 0x4BFD60 (EDX pos*, EBX frames*): catchable loose ball: where and when to collect it. Cached per
// kick in block +0x24 {kick, frame, pos, opponent winner, last evaluation}.
bool Keeper_catchPlan_4BFD60(World& w, int pid, Vec3* pos, std::int32_t* frames) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    if (stampValid(w, 6) && pid != t.f1e0 && t.f204 == 0 && kb.busy == 0 && p.state != 0x205) return false;
    auto fail = [&] {
        Player_clearTeamRefs_4BE6D0(w, pid);
        return false;
    };
    if (w.ball.possessor != -1) return fail();
    const int q = winner_4C2BA4(w, t.opponent);
    if (Player_hasPriorityOver(w, pid, q)) return fail();
    Vec3 tmp;
    int rel = 0;
    std::uint32_t fl = 0;
    const bool roll = Ball_forecastInt(w, w.ball.stamp[11], &tmp, &rel, &fl);
    const bool low = w.ball.posI.y <= 0 && w.ball.vel.y == 0;
    if (!stampValid(w, 11) && !roll && !low) return false;
    if (goingOut_4C0834(w, pid)) return fail();
    std::int32_t* C = blk(kb, 0x24);
    const std::int32_t kc = w.ball.kickCount;
    const bool same = kc == C[0];
    if (t.f1e0 != -1 && pid != t.f1e0) return false;
    if (t.possWinner != -1 && P(w, t.possWinner).distBall < 0x60) return false;
    auto cached = [&] {
        *pos = getV(C + 2);
        *frames = C[1] - now(w);
        if (*frames < -0xf) {
            *pos = w.ball.posI;
            *frames = p.distBall > 8 ? 0x3c : 1;
        }
        return true;
    };
    if ((static_cast<std::uint32_t>(C[1] - now(w)) <= 10 || q == C[5]) && same) return cached();
    if (static_cast<std::uint32_t>(now(w) - C[6]) < 0x14) return false;
    if (t.side == 1 ? w.ball.landing.z < 0 : w.ball.landing.z > 0) return fail();
    Vec3 meet;
    std::int32_t outFrames = 0, anim = 0;
    const bool r = pid == t.f1e0 ? solveVsOpp_4C0320(w, pid, kCatch, 4, frames, pos, &outFrames, &meet, &anim)
                                 : solveBoth_4C02AC(w, pid, kCatch, 4, frames, pos, &outFrames, &meet, &anim);
    if (r && p.state == 0x205 && kc == C[0]) return cached();
    C[6] = now(w);
    if (!r) return fail();
    const int side = t.side;
    if (!Pitch_onField(w, *pos)) return fail();
    if (Pitch_inGoal(w, side, *pos)) return fail();
    const bool inBox = Pitch_inPenaltyBox(w, side, *pos);
    if (!noPostBefore_4C22C8(w, *frames)) return fail();
    auto store = [&] {
        C[0] = kc;
        setV(C + 2, *pos);
        C[1] = *frames + now(w);
        C[5] = q;
        return true;
    };
    if (!inBox) return store();
    if (t.f204 != 0 || kb.busy != 0) return store();
    if (outsideBox_4BFCF0(w, pid, meet, *pos, anim) && t.f204 == 0 && kb.busy == 0) return store();
    if (!oppTooFar_4BFC7C(w, pid, *pos, *frames)) return fail();
    return store();
}

// 0x4C03F4: rush out at an attacker (or just-passed ball) entering the box: the anim contact point
// must be in the box/goal, the attacker near enough, and he must be clear through (0x4DBA34) and
// heading at goal (or close to it).
bool Keeper_rushOut_4C03F4(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    if (t.f204 != 0 || kb.busy != 0) return false;
    int q = w.ball.possessor;
    if (q == -1) {
        q = w.ball.prevPossessor;
        if (q == -1) return false;
        if (static_cast<std::uint32_t>(now(w) - w.ball.kickTime) > 0xf) return false;
        if (static_cast<std::uint32_t>(now(w) - w.ball.possTime) < 3) return false;
    }
    PlayerState& o = P(w, q);
    Vec3 b;
    if (Ball_predictPos(w, 0, 0, &b) && octDistance(o.posI, b).dist > 0x60) return false;
    if (o.team == p.team) return false;
    if (Keeper_attackLost_4C2D80(w, pid)) return true;
    const int side = t.side;
    if ((o.posI.z >= 0 ? 1 : 0) != side) return false;
    if (!Pitch_inPenaltyBox(w, side, p.posI)) return false;
    const int anim = Player_pointLeftOf_4BE080(w, pid, w.ball.posI) ? Keeper_d32(0x589c30) : Keeper_d32(0x589c34);
    Vec3 c;
    if (!Player_animContactPoint_4BE26C(w, anim, p.posI, w.ball.posI, 0, 0, c)) return false;
    if (!Pitch_inPenaltyBox(w, side, c) && !Pitch_inGoal(w, side, c)) return false;
    const OctDist od = octDistance(p.posI, o.posI);
    if (o.state == 0x300) {
        if (p.state == 0x206 ? od.dist > 0x1e0 : od.dist > 0xf0) return false;
    }
    const OctDist og = octDistance(o.posI, goalOf(w, side));
    if ((static_cast<std::int32_t>(Player_attr(w, pid, 0x30)) >> 1) + 0x280 > og.dist) return true;
    if (w.ball.possessor == -1) return true;
    if (!Team_beatsDefender_4DBA34(w, q)) return false;
    const Angle h = (og.dx == 0 && og.dz == 0) ? (side == 1 ? 0 : 0x200) : atan2i(og.dx, og.dz);
    return wrapAbs(o.facingDesired - h) <= 0x100;
}

// 0x4C0688 (EDX out state): dive at a shot crossing the save plane in/at the box: picks the dive state
// (0x4C0C2C); balls too high (> 0x8E, or 0xAA with nothing else on) set block +0x12C and fail; returns
// 0 when a cross plan (0x4BF830) takes over.
int Keeper_divePlan_4C0688(World& w, int pid, std::int32_t* state) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    if (w.ball.possessor != -1) return 0;
    if (kb.k010 == -1 && goingOut_4C0834(w, pid)) return 0;
    const int side = t.side;
    if (!Pitch_inPenaltyBox(w, side, p.posI)) return 0;
    Vec3 B;
    std::int32_t fr = 0;
    if (!shotCross_4C1490(w, pid, &B, &fr)) return 0;
    const std::int32_t dz = side == 1 ? w.pitch.halfLength - B.z : w.pitch.halfLength + B.z;
    if (dz > 0x30) {
        const std::int32_t top = (t.f204 != 0 || kb.busy != 0) ? 0x8e : 0xaa;
        if (B.y > top) {
            kb.k12c = 1;
            return 0;
        }
    }
    if (!Pitch_inPenaltyBox(w, side, B) && !Pitch_inGoal(w, side, B)) return 0;
    *state = chooseDive_4C0C2C(w, pid, B, fr);
    if (*state == -1) return 0;
    if (t.f204 != 0 || kb.busy != 0) return 1;
    std::int32_t t1 = 0, t2 = 0, anim = 0;
    Vec3 p1, p2;
    return Keeper_crossPlan_4BF830(w, pid, &t1, &p1, &t2, &p2, &anim) ? 0 : 1;
}

// 0x4C16D0 (EDX state, EBX out stand point, ECX out frames, stack out ball point; ret 4): plan for an
// already chosen dive state: shot crossing (0x4C1490), no cross plan, then where to stand for the
// state's anim (side from the dive code +0x010 or the ball side +0x13C).
int Keeper_divePlan20C_4C16D0(World& w, int pid, int state, Vec3* a, std::int32_t* frames, Vec3* b) {
    PlayerState& p = P(w, pid);
    KeeperBlock& kb = KB(w, p);
    std::int32_t fr = 0;
    if (!shotCross_4C1490(w, pid, b, &fr)) return 0;
    std::int32_t t1 = 0, t2 = 0, an = 0;
    Vec3 p1, p2;
    if (Keeper_crossPlan_4BF830(w, pid, &t1, &p1, &t2, &p2, &an)) return 0;
    const std::uint32_t D = diveRow(state);
    int anim;
    if (kb.k010 != -1) anim = Keeper_sideCode_4C2658(kb.k010) != 0 ? Keeper_d32(D + 4) : Keeper_d32(D);
    else anim = kb.k13c != 0 ? Keeper_d32(D) : Keeper_d32(D + 4);
    const std::int32_t cf = d16(D + 6);
    Sim_scheduleSplit(w, fr - cf);
    Vec3 v;
    if (!Ball_predictVel(w, fr, 1, &v)) v = w.ball.vel;
    Player_meetPoint_4BE0FC(w, pid, anim, *b, v.x, v.z, nullptr, *a);
    *frames = fr - cf;
    return 1;
}

// 0x4C182C (EDX state 0x209..0x20B, EBX out stand point, ECX out frames, stack out ball point; ret 4):
// high-shot save plan by row 0x589BA0 + (state-0x209)*0x28 {anim, frames, dist, height, depth min/max,
// flags}: the ball passes the row's height (0x5443B0/0x544404) where the keeper can reach in time,
// in the box and depth band. Cached in block +0xA0 + (state-0x209)*0x2C; +0x128 invalidates all three.
int Keeper_planShot_4C182C(World& w, int pid, int state, Vec3* A, std::int32_t* frames, Vec3* B) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    const int side = t.side;
    if (kb.k128 != 0)
        for (auto& pl : kb.plan) pl[8] = 1;
    if (goingOut_4C0834(w, pid)) return 0;
    if (w.ball.possessor != -1) return 0;
    const std::int32_t kc = w.ball.kickCount;
    const int q = winner_4C2BA4(w, t.opponent);
    std::int32_t* PL = kb.plan[state - 0x209];
    const std::uint32_t R = 0x589ba0 + static_cast<std::uint32_t>(state - 0x209) * 0x28;
    const std::int32_t needQ = Keeper_d32(R + 0x18) == 1 ? 1 : 0;
    const bool same = kc == PL[3] && PL[8] == 0 && (q == PL[10] || needQ == 0);
    if (pid == w.ball.dribbler || same) {
        *A = getV(PL);
        *frames = PL[4] - now(w);
        *B = getV(PL + 5);
        return PL[9];
    }
    std::int32_t fr0 = 0;
    // 0x4C1A27: no plan; record the miss once the ball has stopped (or will)
    auto noPlan = [&]() -> int {
        if (!stampValid(w, 2) && !ballDead(w)) return 0;
        if (fr0 > 0 && !noPostBefore_4C22C8(w, fr0)) return 0;
        PL[9] = 0;
        PL[8] = 0;
        PL[3] = kc;
        kb.k128 = 0;
        return 0;
    };
    const std::int32_t apex = w.ball.apexHeight > w.ball.posI.y ? w.ball.apexHeight : w.ball.posI.y;
    if (apex == 0 || t.f204 != 0 || kb.busy == 1) return noPlan();
    const std::int32_t y16 = static_cast<std::int32_t>(static_cast<std::uint32_t>(d16(R + 8)) << 16);
    HeightScan hs;
    if (!heightStart_5443B0(w, hs, y16)) return noPlan();
    std::uint32_t fl = 0;
    if (!heightNext_544404(w, hs, B, frames, &fl)) {
        if (static_cast<std::int32_t>(static_cast<std::uint32_t>(w.ball.posI.y) << 16) < y16) return noPlan();
        return 0;
    }
    while (!(fl & 0x200))
        if (!heightNext_544404(w, hs, B, frames, &fl)) return 0;
    fr0 = *frames;
    *B = shr16v(*B);
    const int anim = Keeper_d32(R);
    const std::int32_t dist = d16(R + 4), cfr = d16(R + 2);
    const bool useGoal = Keeper_d32(R + 0x20) == 1;
    std::int32_t d0 = 0, dc = 0;
    Vec3 M, vel{0, 0, 0}; // [esp+0x60], {vx, vy, vz} = [esp+0xcc / 0xc4 / 0xd4]
    if (useGoal) {
        const OctDist g = Math_vecTo(goalOf(w, side), *B);
        const SinCos sc = sinCos(atan2i(g.dx, g.dz)); // 0x4BB11C (x/z)
        M = {B->x - fx(dist, sc.s), 0, B->z - fx(dist, sc.c)};
    } else {
        if (!Ball_predictVel(w, fr0, 0, &vel)) vel = w.ball.vel;
        Player_meetPoint_4BE0FC(w, pid, anim, *B, vel.x, vel.z, nullptr, M);
    }
    const std::int32_t reach = reachSlack_4C2290(w, pid) + 0x60;
    std::int32_t ok = 1;
    if (!(M.x == p.posI.x && M.z == p.posI.z)) {
        const DistAng r1 = vecAng(p.posI, M);
        const std::int32_t md = Motion_predictDistance(w, pid, p.speedCap, r1.ang, *frames - cfr, 1, 1);
        ok = md - reach > r1.dist ? 1 : 0;
    }
    Vec3 M2;
    if ((vel.y >> 16) < 0 && !useGoal) {
        const std::int32_t ax = iabs(vel.x >> 16), ay = iabs(vel.y >> 16), az = iabs(vel.z >> 16);
        if (ay > (az + ax) * 2) {
            d0 = 1;
            Player_meetPoint_4BE0FC(w, pid, anim, *B, 0, 0, nullptr, M2);
            if (M2.x == p.posI.x && M2.z == p.posI.z) {
                dc = 1;
            } else {
                const DistAng r2 = vecAng(p.posI, M2);
                const std::int32_t md = Motion_predictDistance(w, pid, p.sprintMax, r2.ang, *frames - cfr, 1, 1);
                dc = md - reach > r2.dist ? 1 : 0;
            }
        }
    }
    if (!ok && d0 == 0) return noPlan();
    if (!ok && d0 == 1 && dc == 0) return noPlan();
    const Vec3 Tp = ok ? M : M2;
    bool deep = false;
    if (Pitch_inGoalArea(w, side, *B)) {
        const Vec3& k30 = w.ball.posI_d30;
        const OctDist og = octDistance(k30, goalOf(w, side)), ob = octDistance(k30, *B);
        if (ob.dist + 0x60 < og.dist && iabs(k30.x) > w.pitch.penAreaHalfWidth && iabs(k30.z) < w.pitch.penAreaDepth)
            deep = true;
    }
    // ponytail: q == -1 would fault in the exe (null + 0xD4); skipped here.
    if (needQ == 1 && !deep && q != -1) {
        const OctDist oq = octDistance(P(w, q).posI, *B);
        if (oq.dist == 0) return noPlan();
        const std::int32_t mq = Motion_predictDistance(w, q, P(w, q).sprintMax, atan2i(oq.dx, oq.dz), *frames, 1, 1);
        if (mq > oq.dist - 0x190) return noPlan();
    }
    const std::int32_t lo = d16(R + 0xa), hi = d16(R + 0xc);
    const std::int32_t depth = side == 1 ? w.pitch.halfLength - Tp.z : w.pitch.halfLength + Tp.z;
    if (depth < lo || depth > hi) return noPlan();
    if (!Pitch_inPenaltyBox(w, side, *B)) return noPlan();
    if (Keeper_d32(R + 0x1c) == 0) {
        Vec3 c;
        if (Player_animContactPoint_4BE26C(w, anim, Tp, *B, 0, 0, c) && !Pitch_inPenaltyBox(w, side, c)) return noPlan();
    }
    if (fr0 > 0 && !noPostBefore_4C22C8(w, fr0)) return noPlan();
    PL[4] = *frames + now(w) - cfr;
    setV(PL + 5, *B);
    PL[3] = kc;
    setV(PL, Tp);
    PL[9] = 1;
    PL[8] = 0;
    PL[10] = q;
    *A = getV(PL);
    *frames = PL[4] - now(w);
    *B = getV(PL + 5);
    Sim_scheduleSplit(w, PL[4] - now(w));
    kb.k128 = 0;
    return 1;
}

// 0x4C2688 (EDX out point, EBX out frames): low shot for state 0x204: the threatening ball passes the
// row-0x589C18 height on target, nearer the goal than the keeper, inside his box. Cached per kick in
// block +0x70 {kick, frame, point, result}; +0x124 invalidates.
bool Keeper_lowShot_4C2688(World& w, int pid, Vec3* pos, std::int32_t* frames) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    std::int32_t* L = blk(kb, 0x70);
    const std::int32_t kc = w.ball.kickCount;
    if (kc == L[0] && kb.k124 == 0) {
        *pos = getV(L + 2);
        *frames = L[1] - now(w);
        return L[5] != 0;
    }
    if (w.ball.possessor != -1 || t.f204 != 0 || kb.busy != 0) return false;
    if (goingOut_4C0834(w, pid)) return false;
    std::int32_t fr = 0;
    if (threat_4C09A4(w, pid)) {
        HeightScan hs;
        std::uint32_t fl = 0;
        if (heightStart_5443B0(w, hs, static_cast<std::int32_t>(static_cast<std::uint32_t>(d16(0x589c18 + 8)) << 16)) &&
            heightNext_544404(w, hs, pos, frames, &fl) && ((fl & 0x200) || heightNext_544404(w, hs, pos, frames, &fl))) {
            fr = *frames;
            *pos = shr16v(*pos);
            const int side = t.side;
            if (onTarget_4C0BCC(w, side, *pos) &&
                octDistance(*pos, goalOf(w, side)).dist <= octDistance(p.posI, goalOf(w, side)).dist &&
                Pitch_inPenaltyBox(w, side, *pos) && (fr <= 0 || noPostBefore_4C22C8(w, fr))) {
                L[0] = kc;
                setV(L + 2, *pos);
                L[1] = *frames + now(w);
                L[5] = 1;
                Sim_scheduleSplit(w, *frames - d16(0x589c18 + 2));
                kb.k124 = 0;
                return true;
            }
        }
    }
    if (!stampValid(w, 2) && !ballDead(w)) return false; // 0x4C2773
    if (fr > 0 && !noPostBefore_4C22C8(w, *frames)) return false;
    L[5] = 0;
    L[0] = kc;
    kb.k124 = 0;
    return false;
}

// 0x4C2970 (EDX out frames, EBX out point): player outside the other side's box moving toward this
// box: where (and in how many frames) his velocity line (+0x150/+0x154) crosses its front line or a
// side line within the box.
bool Keeper_edgeIntersect_4C2970(World& w, int pid, std::int32_t* frames, Vec3& e) {
    const PlayerState& p = P(w, pid);
    const int side = w.teams[static_cast<std::size_t>(p.team)].side;
    const Vec3& P0 = p.posI;
    if (Pitch_inPenaltyBox(w, side == 1 ? 0 : 1, P0)) return false;
    const std::int32_t vx = p.m98, vz = p.m9c;
    if (vx == 0 && vz == 0) return false;
    const std::int32_t hl = w.pitch.halfLength, depth = w.pitch.penAreaDepth, phw = w.pitch.penAreaHalfWidth;
    const std::int32_t dz = side == 1 ? hl + P0.z : hl - P0.z;
    if (dz > depth) {
        std::int32_t d;
        if (side == 1) {
            if (vz >= 0) return false;
            e.z = depth - hl;
            d = P0.z - e.z;
        } else {
            if (vz <= 0) return false;
            e.z = hl - depth;
            d = e.z - P0.z;
        }
        const std::int32_t t = fxDiv2(static_cast<std::int32_t>(static_cast<std::uint32_t>(d) << 16), iabs(vz));
        e.x = (fx(t, vx) >> 16) + P0.x;
        if (iabs(e.x) > phw) return false;
        *frames = t >> 16;
        e.y = 0;
        return true;
    }
    if (P0.x > 0) {
        if (vx >= 0) return false;
        e.x = phw;
    } else {
        if (vx <= 0) return false;
        e.x = -phw;
    }
    const std::int32_t d = iabs(P0.x) - phw;
    const std::int32_t t = fxDiv2(static_cast<std::int32_t>(static_cast<std::uint32_t>(d) << 16), iabs(vx));
    e.z = (fx(t, vz) >> 16) + P0.z;
    if (iabs(e.z) > hl) return false;
    if (iabs(e.z) < hl - depth) return false;
    *frames = t >> 16;
    e.y = 0;
    return true;
}

} // namespace wc98::sim
