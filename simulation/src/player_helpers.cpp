// Player / team helper functions used by the player states (owner: SimPorter2), ported from
// FIFA99.EXE v1.1: vector math 0x4BB1A8..0x4BB8F8, player helpers 0x4BBE30, 0x4BCA4C, 0x4BD05C..
// 0x4BEBA8, the tackle move table 0x4D18E0..0x4D24EC, team nearest-player queries 0x451F4C..
// 0x4529F8 and the skill/aim helpers 0x4E01EC..0x4E0448. Exe addresses in comments.
#include <cstring>

#include "player_states.hpp"

namespace wc98::sim {
namespace {

TeamState& T(World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(p.team)]; }
const TeamState& T(const World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(p.team)]; }
const PlayerAttr& A(const World& w, const PlayerState& p) { return w.attrs[static_cast<std::size_t>(p.attr)]; }

constexpr std::int32_t wrapDiff(std::int32_t a) { // and 0x3ff; > 0x200 -> -= 0x400
    a &= 0x3ff;
    return a > 0x200 ? a - 0x400 : a;
}
constexpr std::int32_t absDiff(std::int32_t a) { // and 0x3ff; > 0x200 -> 0x400 - a
    a &= 0x3ff;
    return a > 0x200 ? 0x400 - a : a;
}
// `cdq; idiv b; ...; shrd; idiv b` two-step 16.16 division == (a << 16) / b truncated.
constexpr Fixed fdiv2(Fixed a, Fixed b) { return fdiv(a, b); }

// 0x4BB648 cross product (EAX a, EDX b, EBX out).
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {fx(a.y, b.z) - fx(a.z, b.y), -(fx(a.x, b.z) - fx(a.z, b.x)), fx(a.x, b.y) - fx(a.y, b.x)};
}
// 0x4BB6CC dot product.
Fixed dot(const Vec3& a, const Vec3& b) { return fx(a.x, b.x) + fx(a.y, b.y) + fx(a.z, b.z); }

// 0x4BB4E8 (EAX p1, EDX d1, EBX radius, ECX pt, stack out): unit normal from the line p1 + t*d1 to
// the point where a sphere of `radius` around pt touches it.
void closestNormal(const Vec3& p1, const Vec3& d1, Fixed radius, const Vec3& pt, Vec3& out) {
    const Vec3 v{pt.x - p1.x, pt.y - p1.y, pt.z - p1.z};
    const std::int32_t len = Math_vecLength3(v); // ebp (signed)
    const std::int32_t alen = len < 0 ? -len : len;
    Fixed k;
    if (Math_sqrtFx(0x7fff0000u) < alen) { // 0x4BB625: integer path for long vectors
        k = static_cast<Fixed>(Math_isqrt(static_cast<std::uint32_t>((len >> 16) * (len >> 16) -
                                                                      (radius >> 16) * (radius >> 16)))
                               << 16);
    } else {
        std::int32_t a = fx(len, len) - fx(radius, radius);
        if (a < 0) a = 0; // `xor eax,ecx`
        k = Math_sqrtFx(static_cast<std::uint32_t>(a));
    }
    const Vec3 q{p1.x + fx(d1.x, k), p1.y + fx(d1.y, k), p1.z + fx(d1.z, k)};
    out = {pt.x - q.x, pt.y - q.y, pt.z - q.z};
    Math_normalize3(out, out);
}

// 0x4BAE38 (EAX from, EDX to, EBX out): out[1..2] = to - from, out[3] = atan2 (out[0] untouched).
Angle angleTo(const Vec3& from, const Vec3& to) { return atan2i(to.x - from.x, to.z - from.z); }

// Tackle move row 0x58B340 + row*0x2C (w.pl.tackleMoves).
std::uint32_t tk32(const World& w, int row, int off) {
    std::uint32_t v;
    std::memcpy(&v, &w.pl.tackleMoves[static_cast<std::size_t>(row)][static_cast<std::size_t>(off)], 4);
    return v;
}
std::int32_t tk16(const World& w, int row, int off) {
    std::int16_t v;
    std::memcpy(&v, &w.pl.tackleMoves[static_cast<std::size_t>(row)][static_cast<std::size_t>(off)], 2);
    return v;
}
void tkSet16(World& w, int row, int off, std::int32_t v) {
    const auto s = static_cast<std::int16_t>(v);
    std::memcpy(&w.pl.tackleMoves[static_cast<std::size_t>(row)][static_cast<std::size_t>(off)], &s, 2);
}

// Exe data of 0x58B340 (12 x 0x2C) before Tackle_buildMoves rewrites +0x08..+0x19.
constexpr std::uint8_t kTackleRows[12][0x2c] = {
    {0x1e, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x00, 0x00, 0x76, 0x01, 0x00, 0x00, 0x77, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x55, 0x00, 0x02, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x74, 0x01, 0x00, 0x00, 0x75, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0f, 0x55, 0x00, 0x02, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x1e, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x76, 0x01, 0x00, 0x00, 0x77, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0f, 0x55, 0x00, 0x02, 0x5a, 0x05, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x1e, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x00, 0x00, 0x76, 0x01, 0x00, 0x00, 0x77, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x0f, 0x55, 0x00, 0x02, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x1e, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x35, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x19, 0x4b, 0x00, 0x05, 0x5f, 0x01, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00},
    {0x11, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x7c, 0x01, 0x00, 0x00, 0x7c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x19, 0x4b, 0x00, 0x05, 0x5f, 0x01, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00},
    {0x0f, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x19, 0x4b, 0x00, 0x08, 0x14, 0x1e, 0x0a, 0x0f, 0x05, 0x14, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00},
    {0x2f, 0x00, 0x00, 0x00, 0x31, 0x00, 0x00, 0x00, 0x7e, 0x01, 0x00, 0x00, 0x7f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x19, 0x4b, 0x00, 0x05, 0x5f, 0x01, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00},
    {0x37, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x5f, 0x05, 0x00, 0x06, 0x0a, 0x0a, 0x1e, 0x1e, 0x0a, 0x0a, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00},
    {0x0b, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x73, 0x01, 0x00, 0x00, 0x73, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x4b, 0x19, 0x00, 0x06, 0x0a, 0x0a, 0x1e, 0x1e, 0x0a, 0x0a, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00},
    {0x12, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x7d, 0x01, 0x00, 0x00, 0x7d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x64, 0x00, 0x00, 0x09, 0x00, 0x0a, 0x32, 0x14, 0x0a, 0x0a, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00},
    {0x37, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x7d, 0x01, 0x00, 0x00, 0x7d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0x5f, 0x05, 0x00, 0x06, 0x0a, 0x0a, 0x1e, 0x1e, 0x0a, 0x0a, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00},
};

// Common loop of the nearest-player queries: players of team t without +0x22C bits 1/0 and
// curSlot != excl (0x451F81 / 0x452050 / 0x45221B pattern).
template <class Skip>
int nearestTo(const World& w, const Vec3& pos, int t, int excl, Skip skip) {
    const TeamState& T = w.teams[static_cast<std::size_t>(t)];
    int best = -1;
    std::uint32_t bestD = 0x7fff8000u;
    for (int i = 0; i < T.playerCount; ++i) {
        const int q = T.players[i];
        if (q < 0) continue; // empty roster slot
        const PlayerState& o = P(w, q);
        if ((o.flags22c & 2) || excl == o.curSlot || (o.flags22c & 1) || skip(q, o)) continue;
        const auto d = static_cast<std::uint32_t>(octLength(pos.x - o.posI.x, pos.z - o.posI.z));
        if (d < bestD) {
            best = q;
            bestD = d;
        }
    }
    return best;
}

} // namespace

// ---------------------------------------------------------------- vector math 0x4BB1A8..0x4BB8F8

// 0x4BB1A8 (EAX r, EDX a, EBX out): {r, fx(r, sin a), fx(r, cos a), a}.
void Math_polarRecord(std::int32_t r, Angle a, std::int32_t out[4]) {
    const SinCos sc = sinCos(a);
    out[1] = fx(r, sc.s);
    out[2] = fx(r, sc.c);
    out[0] = r;
    out[3] = a;
}

// 0x4BB70C: Euclidean length (16.16). Components are halved until the largest is below
// sqrt(0x7FFF0000 / 3) so the squares cannot overflow; the result is shifted back.
Fixed Math_vecLength3(const Vec3& v) {
    std::int32_t x = v.x, y = v.y, z = v.z;
    const Fixed lim = Math_sqrtFx(static_cast<std::uint32_t>(fdiv2(0x7fff0000, 0x30000)));
    int sh = 0;
    for (;;) {
        std::int32_t m = iabs(y);
        if (iabs(x) > m) m = iabs(x);
        if (iabs(z) > m) m = iabs(z);
        if (m <= lim) break;
        ++sh;
        x >>= 1;
        y >>= 1;
        z >>= 1;
    }
    const Fixed r = Math_sqrtFx(static_cast<std::uint32_t>(fx(x, x) + fx(y, y) + fx(z, z)));
    return static_cast<Fixed>(static_cast<std::uint32_t>(r) << sh);
}

// 0x4BB820 (EAX in, EDX out): out = in doubled until a component reaches 1.0, then divided by
// its length. (An all-zero vector loops forever in the exe as well.)
void Math_normalize3(const Vec3& in, Vec3& out) {
    out = in;
    while (iabs(out.x) < 0x10000 && iabs(out.y) < 0x10000 && iabs(out.z) < 0x10000) {
        out.x += out.x;
        out.y += out.y;
        out.z += out.z;
    }
    const Fixed len = Math_vecLength3(out);
    out.x = fdiv2(out.x, len);
    out.y = fdiv2(out.y, len);
    out.z = fdiv2(out.z, len);
}

// 0x4BB1F4: closest approach of the lines p0 + t0*d0 and p1 + t1*d1 (unit directions).
int Math_closestApproach(const Vec3& p0, const Vec3& d0, const Vec3& p1, const Vec3& d1, Fixed radius, Fixed* t0,
                         Fixed* t1, Vec3* normal) {
    const Vec3 w{p0.x - p1.x, p0.y - p1.y, p0.z - p1.z};
    Vec3 n = cross(d0, d1);
    const Fixed len = Math_vecLength3(n);
    if (len == 0) { // parallel
        const Fixed t = dot(w, d1);
        const Vec3 r{w.x - fx(t, d1.x), w.y - fx(t, d1.y), w.z - fx(t, d1.z)};
        const Fixed l2 = Math_vecLength3(r);
        normal->x = fx(d0.x, static_cast<Fixed>(0xffff0000u));
        normal->y = fx(d0.y, static_cast<Fixed>(0xffff0000u));
        normal->z = fx(d0.z, static_cast<Fixed>(0xffff0000u));
        *t0 = static_cast<Fixed>(0x80010000u);
        *t1 = 0x7fff0000;
        return (l2 >> 16) <= (radius >> 16) ? 1 : 0;
    }
    Math_normalize3(n, n);
    std::int32_t dd = dot(w, n);
    if (dd < 0) dd = -dot(w, n);
    if (dd > radius) return 0;
    const Fixed tc = fx(fdiv2(dot(cross(w, d1), n), len), static_cast<Fixed>(0xffff0000u)); // [esp+0x44]
    Vec3 c = cross(n, d1);
    Math_normalize3(c, c);
    const Fixed e = dot(d0, c);
    Fixed h = fdiv2(Math_sqrtFx(static_cast<std::uint32_t>(fx(radius, radius) - fx(dd, dd))), e);
    if (h < 0) h = -h;
    *t0 = tc - h;
    *t1 = tc + h;
    const Vec3 pt{p0.x + fx(*t0, d0.x), p0.y + fx(*t0, d0.y), p0.z + fx(*t0, d0.z)};
    closestNormal(p1, d1, radius, pt, *normal);
    return 1;
}

// ------------------------------------------------------------------- movement object 0x4CBCEC..

// 0x4CBCEC (EAX M, EDX dx, EBX dz): head aim (M+0x84) toward a direction (zero vector: kept).
void Motion_faceVector(World& w, int pid, std::int32_t dx, std::int32_t dz) {
    if (dx == 0 && dz == 0) return;
    P(w, pid).headAim = wrapDiff(atan2i(dx, dz));
}
// 0x4CBD30 (EAX M, EDX point): head aim toward a point (same spot: kept).
void Motion_facePoint(World& w, int pid, const Vec3& pt) {
    const PlayerState& p = P(w, pid);
    Motion_faceVector(w, pid, pt.x - p.posI.x, pt.z - p.posI.z);
}

// ------------------------------------------------------------------------ player 0x4BBE30..0x4BEC0C

// 0x4BBE30: AI speed cap. Humans: Player_updateSpeedCap 0x4BBD84. Sprint-flagged attributes
// (+0x10, not +0x40 bit 6): sprintMax. Phase 0: 0x70000, other phases: 0x6C000. Phase 9:
// sprintMax, keepers 0x70000 (0x4BBE93..0x4BBF26 compute a state-dependent value in EDX that
// 0x4BBEB6 never stores).
void Player_refreshSpeedCap(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (p.ctrl != 0) {
        Player_updateSpeedCap(w, pid);
        return;
    }
    const PlayerAttr& at = A(w, p);
    if (at.f10 != 0 && !(at.at(0x40) & 0x40)) {
        p.speedCap = p.sprintMax;
        return;
    }
    const int ph = w.match.phase;
    if (ph != 9) {
        p.speedCap = ph == 0 ? 0x70000 : 0x6c000;
        return;
    }
    p.speedCap = p.slot != 0 ? p.sprintMax : 0x70000;
}

// 0x4BCA4C: offside-rule defensive line hold for defenders (role 2 / slot 1): move to the line
// of the marked (or nearest) attacker, clamped between the ball (landing point when high) and
// the penalty-area band, mirrored for side 0.
void Player_holdOffsideLine_4BCA4C(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (!(w.opt.rules & 2)) return;
    if (p.role != 2 && p.curSlot != 1) return;
    std::int32_t h = w.ball.apexHeight;
    if (h <= w.ball.posI.y) h = w.ball.posI.y;
    std::int32_t z = h > 0x55 ? w.ball.landing.z : w.ball.posI.z;
    const TeamState& t = T(w, p);
    if ((z >= 0 ? 1 : 0) != t.side) return;
    int m = p.markTarget - 1;
    if (m < 0) m = Team_nearestPlayer_4523E4(w, p.posI, t.opponent, 0, nullptr);
    if (m < 0) return;
    if (m == T(w, P(w, m)).f1e0 && w.ball.possessor < 0) return;
    Vec3 v = Motion_targetVec(w, pid);
    if (t.side != 1) {
        z = -z;
        v.z = -v.z;
    }
    const std::int32_t band = (w.pitch.penAreaDepth >> 1) + w.pitch.penAreaLineZ; // eax
    if (v.z < z) {
        v.z = z;
    } else { // 0x4BCB9A
        if (band > z + 0x20 && band < v.z) v.z = band;
        v.z += ((2 - t.ctrl1f4) << 5) + 0x30;
        if (band < v.z) v.z = band;
    }
    if (t.side == 0) v.z = -v.z;
    Player_moveToward_4BE804(w, pid, v);
}

// 0x4BD05C (EAX pos): octagonal distance from the restart spot is at least the phase's clearance
// (kick-off goalWidthVisual 0x65F358, throw-in 0xC0, else 0x1E0).
bool Player_clearOfRestart_4BD05C(const World& w, const Vec3& pos) {
    const std::int32_t d = octLength(w.ball.restartPos.x - pos.x, w.ball.restartPos.z - pos.z);
    const int ph = w.match.phase;
    const std::int32_t lim = ph == 0 ? w.pitch.goalWidthVisual : ph == 1 ? 0xc0 : 0x1e0;
    return d >= lim;
}

// 0x4BD0D0 (EDX other): 1 when other is the possessor, pid is not and other is nearer to the ball
// or pid's countdown runs; pid possessor -> 0.
bool Player_hasPriorityOver(const World& w, int pid, int other) {
    const int poss = w.ball.possessor;
    if (other == poss) return true;
    if (pid == poss) return false;
    if (P(w, other).distBall < P(w, pid).distBall) return true;
    return P(w, pid).countdown != 0;
}

// 0x4BD754 (EDX manual): pick the team-mate a human control switch goes to. 0 = pid owns the
// ball, 2 = the pick is pid itself, else 1 (transferred when manual).
int Player_switchControl(World& w, int pid, int manual) {
    if (pid == w.ball.possessor) return 0;
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    const TeamState& o = w.teams[static_cast<std::size_t>(t.opponent)];
    const int spt = w.match.setPieceTaker;
    int cand = -1;
    const int ph = w.match.phase;
    if (spt >= 0 && (ph == 1 || ph == 2 || ph == 5 || ph == 7 || ph == 3 || P(w, spt).state == 0x201)) {
        // 0x4BD79A: next outfield, uncontrolled team-mate after pid that is not the taker
        const auto n = static_cast<std::uint32_t>(t.playerCount);
        std::uint32_t i = static_cast<std::uint32_t>(p.index);
        for (std::uint32_t c = 0; c < n; ++c, ++i) {
            if (i >= n) i = 0;
            const int q = t.players[i];
            if (P(w, q).slot != 0 && P(w, q).ctrl == 0 && q != spt) {
                cand = q;
                break;
            }
        }
    } else { // 0x4BD857
        std::int32_t d = 1;
        if (o.f1cc >= 0) d = P(w, o.f1cc).distBall - P(w, t.f1cc).distBall;
        if (w.ball.possessor < 0 && d > -0x80) {
        } else if (!(t.flags & 2) && d < 0 && static_cast<std::uint32_t>(t.humans) <= 1) {
            cand = Team_nearestPlayer_4522D8(w, w.ball.landing, t.index, 0xff); // 0x4BD935
            goto done;
        }
        { // 0x4BD88F
            const Vec3 v = t.f1e0 >= 0 ? P(w, t.f1e0).posI : w.ball.landing;
            const int poss = w.ball.possessor;
            if (w.match.phase == 9 && (poss < 0 || P(w, poss).state == 0x188) &&
                static_cast<std::uint32_t>(t.humans) < 2)
                cand = Team_nearestPlayer_4522A0(w, v, t.index, p.curSlot, 1);
            else
                cand = Team_nearestPlayer_4522B0(w, v, t.index, 0xff); // 0x4BD970
        }
    }
done:
    if (cand < 0) return 1;
    if (cand == pid) return 2;
    if (manual == 0) return 1;
    if (pid == w.match.setPiecePartner) w.match.setPiecePartner = cand;
    Player_transferControl(w, pid, cand);
    t.oneTouch = -1;
    return 1;
}

// 0x4BD9AC: frames until a runner at `from` with `speed` can meet a ball moving by (velX, velZ)
// per frame (integer parts) — 0x2D0 frames at most, -1 when the ball leaves the half-sizes first.
// out = the meeting point. 0x673220 is the lazily built table 0x10000 / n (n = 1..0xB3).
std::int32_t Player_solveIntercept(World&, const Vec3& from, Fixed speed, const Vec3& ballPos, Fixed velX, Fixed velZ,
                                   std::int32_t halfW, std::int32_t halfL, Vec3& out) {
    const std::int32_t sp = speed >> 16, vx = velX >> 16, vz = velZ >> 16;
    out = ballPos;
    out.y = 0;
    std::int32_t lastD = 0x2710;
    const std::int32_t dx = ballPos.x - from.x, dz = ballPos.z - from.z;
    const std::int32_t dist = octLength(dx, dz);
    const std::int32_t ang = absDiff(atan2i(dx, dz));
    std::int32_t n = 1;
    if (ang > 0xe && ang < 0x100) { // 0x4BDB22
        if (sp == 0) {
            out.x += vx * 0xb3;
            out.z += vz * 0xb3;
            return -1;
        }
        const SinCos sc = sinCos(ang);
        const std::int32_t a = fx(dist, sc.s);
        const std::int32_t b = fx(dist, sc.c) * sp - octLength(vx, vz) * a;
        if (b == 0) {
            const std::int32_t k = a / sp;
            out.x += vx * k;
            out.z += vz * k;
            return k + n;
        }
        if (b < 0) { // 0x4BDBE6
            n = a / sp;
            if (n >= 2) {
                out.x += vx * (n - 1);
                out.z += vz * (n - 1);
            } else {
                n = 1;
            }
        }
    }
    for (;; ++n) { // 0x4BDAC3
        if (n * 4 >= 0x2d0) return -1;
        out.x += vx;
        out.z += vz;
        if (iabs(out.x) > halfW || iabs(out.z) > halfL) return -1;
        const Fixed r = 0x10000 / n;
        const std::int32_t d = octLength(vx + fx(dx, r), fx(dz, r) + vz);
        if (d <= sp && d >= lastD) return n;
        lastD = d;
    }
}

// 0x4BDCAC (EDX dist, EBX angle, ECX out): dribble push vector, dist <= 0x20 (doubled for sprint
// flagged attributes) -> Math_polarRecord.
void Player_dribbleVector(World& w, int pid, std::int32_t dist, Angle a, std::int32_t out[4]) {
    const PlayerAttr& at = A(w, P(w, pid));
    if (dist > 0x20) dist = 0x20;
    if (at.f10 != 0 && !(at.at(0x40) & 0x40)) dist += dist;
    Math_polarRecord(dist, a, out);
}

// 0x4BE034: claim range 0x5890A8[team.ctrl1f8].
std::int32_t Player_claimRange(const World& w, int pid) {
    static constexpr std::int32_t kRange[3] = {0x55, 0x42, 0x2f};
    return kRange[T(w, P(w, pid)).ctrl1f8];
}

// 0x4BE080 (EDX point): 1 when the point is on the left of the facing (wrapped difference < 0)
// or on the player's spot.
bool Player_pointLeftOf_4BE080(const World& w, int pid, const Vec3& pt) {
    const PlayerState& p = P(w, pid);
    const OctDist d = octDistance(p.posI, pt);
    if (d.dx == 0 && d.dz == 0) return true;
    return wrapDiff(atan2i(d.dx, d.dz) - p.facing) < 0;
}

// 0x4BE168 (EAX player, EDX dist, EBX contact angle, ECX anim yaw, stack ball, c, s0, from, out): where
// to stand so a contact at (dist, cang) of an anim with yaw `yaw` meets the ball. Direction: from `from`
// (no yaw correction), else toward the goal of the player's side (c = s0 = 0), else atan2_16(c, s0).
void Player_meetPointCore_4BE168(World& w, int pid, std::int32_t dist, std::int32_t cang, std::int32_t yaw,
                                 const Vec3& ball, std::int32_t c, std::int32_t s0, const Vec3* from, Vec3& out) {
    Angle a;
    if (from != nullptr) {
        a = (ball.x == from->x && ball.z == from->z) ? 0 : angleTo(ball, *from);
    } else if (c == 0 && s0 == 0) {
        const int side = T(w, P(w, pid)).side;
        const Vec3 goal{0, 0, w.pitch.goalLineZ[side]}; // 0x65F418 + side*0xC
        a = angleTo(ball, goal);
    } else {
        a = Math_atan2_16(c, s0) >> 6;
    }
    std::int32_t h = wrapDiff(cang - yaw);
    if (from != nullptr) h = 0;
    h += a + 0x200;
    if (w.opt.funModes & 0x80) h += 0x200;
    h = wrapDiff(h);
    const SinCos sc = sinCos(h); // 0x4BB11C
    out.x = ball.x - fx(dist, sc.s);
    out.z = ball.z - fx(dist, sc.c);
    out.y = 0;
}

// 0x4BE0FC (EDX anim, EBX ball, ECX c, stack s0, from, out): the anim's yaw and mark-0 bone contact,
// then 0x4BE168.
void Player_meetPoint_4BE0FC(World& w, int pid, int anim, const Vec3& ball, std::int32_t c, std::int32_t s0,
                             const Vec3* from, Vec3& out) {
    const std::int32_t yaw = Anim_yawAt(w, anim, 0);
    std::int32_t dist = 0, cang = 0;
    Anim_contactInfo(w, anim, 0, 0, -0x62, 1, &dist, &cang);
    Player_meetPointCore_4BE168(w, pid, dist, cang, yaw, ball, c, s0, from, out);
}

// 0x4BE26C (EAX anim, EDX from, EBX to, ECX ox, stack oz, out): contact point of `anim` started at
// from + (ox, oz) facing `to`; 0 when the anim has no contact mark.
bool Player_animContactPoint_4BE26C(const World& w, int anim, const Vec3& from, const Vec3& to, std::int32_t ox,
                                    std::int32_t oz, Vec3& out) {
    std::int32_t d0 = 0, a0 = 0, d1 = 0, a1 = 0;
    const int r0 = Anim_contactInfo(w, anim, 0, 0, -0x62, 1, &d0, &a0);
    const int r1 = Anim_contactInfo(w, anim, 0, 0, -0x61, 1, &d1, &a1);
    if (r0 == -1 || r1 == -1) return false;
    Vec3 v = from;
    v.x += ox;
    v.z += oz;
    const Angle a = angleTo(v, to) - a0 + a1;
    const SinCos sc = sinCos(a); // 0x4BB0D0
    out.x = v.x + fx(d1, sc.s);
    out.z = v.z + fx(d1, sc.c);
    out.y = 0;
    return true;
}

// 0x4BE6D0: forget pid as team receiver (+0x1E0), one-touch kicker (+0x1D4), pass-back target
// (+0x1B8) and match.player2a0.
void Player_clearTeamRefs_4BE6D0(World& w, int pid) {
    TeamState& t = T(w, P(w, pid));
    if (pid == t.f1e0) t.f1e0 = -1;
    if (pid == t.oneTouch) t.oneTouch = -1;
    if (pid == t.f1b8) t.f1b8 = -1;
    if (pid == w.match.player2a0) w.match.player2a0 = -1;
}

// 0x4BE890 (EDX target, EBX lookAt, ECX n, stack out, angle out): one step of n frames toward
// target at the current speed scaled by cos(facing error) (capped at the distance); *angle =
// heading from the step to lookAt. Returns 1 if the step covers >= 0.98 of the way.
int Player_stepToward_4BE890(const World& w, int pid, const Vec3& target, const Vec3& lookAt, std::int32_t n,
                             Vec3& out, Angle* angle) {
    const PlayerState& p = P(w, pid);
    if (p.posI.x == target.x && p.posI.z == target.z) {
        out = {target.x, 0, target.z};
        const std::int32_t dx = lookAt.x - out.x, dz = lookAt.z - out.z;
        if (dx != 0 || dz != 0) *angle = atan2i(dx, dz);
        return 1;
    }
    const OctDist d = Math_vecTo(p.posI, target);
    const std::int32_t e = absDiff(p.facingDesired - atan2i(d.dx, d.dz));
    const Fixed c = sinCos(e + 0x100).s; // inline 0x582EB4 lookup = cos(e)
    std::int32_t step = n + (fx(n << 16, fx(fdiv2(p.speed, 0xc0000), c)) >> 16);
    if (step > d.dist) step = d.dist;
    Fixed r = 0x10000;
    std::int32_t ox = d.dx, oz = d.dz;
    if (d.dist != 0) {
        r = fdiv2(step << 16, d.dist << 16);
        ox = fx(r, d.dx);
        oz = fx(r, d.dz);
    }
    out = {p.posI.x + ox, 0, p.posI.z + oz};
    const std::int32_t dx = lookAt.x - out.x, dz = lookAt.z - out.z;
    *angle = (dx == 0 && dz == 0) ? 0 : atan2i(dx, dz);
    return r >= 0xfae1 ? 1 : 0;
}

// 0x4BEA94: the ball is at the anim contact point (height within 0x18, keepers with team+0x204
// 0x30; distance <= 0x30, or <= 8 for a loose ball outside state 0x206).
bool Player_ballAtContact_4BEA94(World& w, int pid) {
    const PlayerState& p = P(w, pid);
    const Vec3 c = Anim_contactPos(w, pid);
    const std::int32_t dy = iabs(c.y - w.ball.posI.y);
    const std::int32_t lim = (p.slot == 0 && T(w, p).f204 != 0) ? 0x30 : 0x18;
    if (dy > lim) return false;
    const std::int32_t d = octDistance(c, w.ball.posI).dist;
    if (p.state == 0x206 || w.ball.possessor >= 0 || w.ball.dribbler >= 0) return d <= 0x30;
    return d <= 8;
}

// 0x4BEB5C (EDX frames, EBX out): position after `frames` at the current velocity (m98/m9c).
void Player_predictPos_4BEB5C(const World& w, int pid, std::int32_t frames, Vec3& out) {
    const PlayerState& p = P(w, pid);
    out = p.posI;
    const Fixed s = static_cast<Fixed>(static_cast<std::uint32_t>(frames) << 16);
    out.x += fx(s, p.m98) >> 16;
    out.z += fx(s, p.m9c) >> 16;
}

// 0x4BEBA8: the ball's y=85 descent point (0x669190) is more than 5 frames away and the player can
// cover the distance (12 units per frame + 0x40) before it.
bool Player_canReachCross85(World& w, int pid) {
    const std::int32_t f = w.ball.cross85Frame - w.ball.consumed;
    if (f <= 5) return false;
    const std::int32_t reach = fx(f, 0xc0000) + 0x40;
    return reach > Math_vecTo(w.ball.cross85Pos, P(w, pid).posI).dist;
}

// --------------------------------------------------------------- tackle moves 0x4D18E0..0x4D24EC
// Row layout (0x58B340 + row*0x2C): +0x00/+0x04 u32 (0x4D2304 / 0x4D2398), +0x08/+0x0C anim A/B
// (0x4D227C sel 1/0), +0x10 i16 frames, +0x12 i16 dist, +0x14/+0x16 i16 angle A/B (0x4D23F0),
// +0x18 i16 contact height (0x4D22C4), +0x1A kind (0x4D23D0), +0x1B approach class (0x4D2358),
// bytes +0x1C (0x4D2320), +0x1D (0x4D23B4), +0x1E (0x4D22A8), +0x1F (0x4D233C), +0x20..+0x25
// (0x4D2444..0x4D24D0), +0x28 bits 0/1/2 (0x4D22E4 / 0x4D2378 / 0x4D24EC).

std::uint8_t Tackle_u8(const World& w, int row, int off) {
    return w.pl.tackleMoves[static_cast<std::size_t>(row)][static_cast<std::size_t>(off)];
}
std::int32_t Tackle_anim_4D227C(const World& w, int row, int sel) {
    return static_cast<std::int32_t>(tk32(w, row, sel ? 0x08 : 0x0c));
}
void Tackle_motion_4D23F0(const World& w, int row, int sel, std::int32_t* frames, std::int32_t* dist,
                          std::int32_t* angle) {
    *frames = tk16(w, row, 0x10);
    *dist = tk16(w, row, 0x12);
    *angle = tk16(w, row, sel ? 0x14 : 0x16);
}
std::int32_t Tackle_height_4D22C4(const World& w, int row) { return tk16(w, row, 0x18); }
std::int32_t Tackle_flag_4D22E4(const World& w, int row, int bit) {
    return static_cast<std::int32_t>((tk32(w, row, 0x28) >> bit) & 1);
}

// 0x4D18E0: resolve the anims of every row and cache their contact data (Match_init). The exe
// rewrites its data section in place; World starts from the exe's initial bytes each match.
void Tackle_buildMoves(World& w) {
    std::memcpy(w.pl.tackleMoves, kTackleRows, sizeof kTackleRows);
    for (int r = 0; r < 12; ++r) {
        const int a = Anim_resolveId(w, static_cast<int>(tk32(w, r, 0x08)));
        const int b = Anim_resolveId(w, static_cast<int>(tk32(w, r, 0x0c)));
        std::int32_t d0 = 0, a0 = 0, d1 = 0, a1 = 0;
        const int frames = Anim_contactInfo(w, a, 0, 0, -0x62, 1, &d0, &a0);
        Anim_contactInfo(w, b, 0, 0, -0x62, 1, &d1, &a1);
        const int h = Anim_boneHeightAt(w, static_cast<int>(tk32(w, r, 0x08)), -0x62); // unresolved id
        // 0x4D1961..0x4D197D: boneHeightAt(B), markFrame0/1(A/B): results unused
        const auto ua = static_cast<std::uint32_t>(a), ub = static_cast<std::uint32_t>(b);
        std::memcpy(&w.pl.tackleMoves[static_cast<std::size_t>(r)][0x08], &ua, 4);
        std::memcpy(&w.pl.tackleMoves[static_cast<std::size_t>(r)][0x0c], &ub, 4);
        tkSet16(w, r, 0x10, frames);
        tkSet16(w, r, 0x12, d0);
        tkSet16(w, r, 0x14, a0);
        tkSet16(w, r, 0x16, a1);
        tkSet16(w, r, 0x18, h);
    }
}

// 0x4D20E0 (EDX kind, EBX class): row with +0x1A == kind and +0x1B == class, -1 none.
int Tackle_findRow_4D20E0(const World& w, int kind, int cls) {
    for (int r = 0; r < 12; ++r)
        if (Tackle_u8(w, r, 0x1a) == kind && Tackle_u8(w, r, 0x1b) == cls) return r;
    return -1;
}

// 0x4D2128 (EDX target): approach class: 1 no target / behind (> 0x180), 3 side, 2 front, 0 when
// both run the same way within 0x60.
int Tackle_approachClass_4D2128(World& w, int pid, int o) {
    if (o < 0) return 1;
    const PlayerState& p = P(w, pid);
    const PlayerState& q = P(w, o);
    const std::int32_t c = absDiff(Player_cachedAngle(w, pid, o) - q.animFacing);
    if (c > 0x80 && absDiff(p.facingDesired - q.facingDesired) < 0x1c &&
        static_cast<std::uint32_t>(Player_cachedDistance(w, pid, o)) < 0x60)
        return 0;
    if (c > 0x180) return 1;
    return c > 0x80 ? 3 : 2;
}

// 0x4D21C4 (EDX mode): tackle target — mode 2: nearest opponent; else the possessor when he is an
// opponent.
int Tackle_target_4D21C4(World& w, int pid, int mode) {
    if (mode == 2) {
        std::int32_t d;
        return Team_nearestPlayer_4523E4(w, P(w, pid).posI, T(w, P(w, pid)).opponent, 0xff, &d);
    }
    const int poss = w.ball.possessor;
    if (poss < 0 || P(w, pid).team == P(w, poss).team) return -1;
    return poss;
}

// 0x4D221C: kind of pid's running tackle (0 also when not tackling).
int Tackle_kindOf_4D221C(const World& w, int pid) {
    if (Player_isState100Kind0(w, pid)) return 0;
    if (Player_isState100Kind1(w, pid)) return 1;
    if (Player_isState100Kind2(w, pid)) return 2;
    return 0;
}

// 0x4D2250: pid's team holds the ball.
bool Tackle_ownTeamBall_4D2250(const World& w, int pid) {
    const int poss = w.ball.possessor;
    return poss >= 0 && P(w, poss).team == P(w, pid).team;
}

// 0x4D1C64 (EAX frames, EDX out): ball position after `frames`: the trajectory forecast when the
// ball is free and settled (velCopy == vel, posI == posI_d78), else posI_d78 + velCopy*frames.
void Tackle_ballAfter_4D1C64(World& w, std::int32_t frames, Vec3& out) {
    const BallState& b = w.ball;
    if (b.possessor < 0 && b.dribbler < 0 && b.velCopy == b.vel && b.posI == b.posI_d78 &&
        Ball_predictPos(w, frames, 1, &out) != 0)
        return;
    out = b.posI_d78;
    const Fixed s = static_cast<Fixed>(static_cast<std::uint32_t>(frames) << 16);
    out.x += fx(b.velCopy.x, s) >> 16;
    out.z += fx(b.velCopy.z, s) >> 16;
}

// 0x4D1BB8 (EDX target, EBX row, ECX out): distance from pid to where row's B anim must start to
// meet the ball.
static std::int32_t Tackle_moveDist_4D1BB8(World& w, int pid, int row) {
    const int anim = Tackle_anim_4D227C(w, row, 0);
    std::int32_t frames, dist, ang;
    Tackle_motion_4D23F0(w, row, 0, &frames, &dist, &ang);
    Vec3 b;
    Tackle_ballAfter_4D1C64(w, frames, b);
    Vec3 m;
    Player_meetPoint_4BE0FC(w, pid, anim, b, 0, 0, &P(w, pid).posI, m);
    return octLength(P(w, pid).posI.x - m.x, P(w, pid).posI.z - m.z);
}

// 0x4D1D30 (EDX target): a standing tackle is allowed.
static bool Tackle_canStand_4D1D30(World& w, int pid, int o) {
    const PlayerState& p = P(w, pid);
    if (p.distBall > 0xa0) return false;
    if (Tackle_approachClass_4D2128(w, pid, o) != 2) return true;
    if (p.role == 2 || p.curSlot == 1 || static_cast<std::uint32_t>(w.time.possessionFrames) <= 0x78)
        return static_cast<std::uint32_t>(T(w, p).w360[0]) <= 2; // 0x4D1D52
    return true;
}

// 0x4D1D78 (EDX target): a sliding tackle is allowed (position, angle and attribute rolls).
static bool Tackle_canSlide_4D1D78(World& w, int pid, int o) {
    const PlayerState& p = P(w, pid);
    const TeamState& t = T(w, p);
    if (p.distBall < 0x40) return false;
    if (o < 0) return true;
    const bool ahead = absDiff(p.angleBall - Player_cachedAngle(w, pid, o)) < 0x47; // esi
    const auto ebp = static_cast<std::uint32_t>(A(w, p).at(0x30) + t.ctrl1f4 * 4);
    const bool roll = w.rng.next() % 20 < ebp; // [esp+8]
    const bool ballOwnThird = ((w.ball.posI.z - w.pitch.thirdLineZ[t.side] >= 0) ? 1 : 0) == t.side; // [esp+4]
    const bool inBox = iabs(w.ball.posI.x) < w.pitch.penAreaHalfWidth && iabs(w.ball.posI.z) > w.pitch.penAreaLineZ;
    const bool inGoalWidth = inBox && iabs(w.ball.posI.x) < w.pitch.goalHalfWidth; // [esp+0xC]
    const std::int32_t q = p.posI.z + (t.side == 1 ? -0x40 : 0x40) - w.ball.posI.z;
    const bool behind = ((q >= 0) ? 1 : 0) == t.side; // edx
    if (T(w, P(w, o)).f1b4 >= 0) return true;
    if (inBox) {
        if (((p.posI.z >= 0) ? 1 : 0) != t.side) { // 0x4D1FEE
            if (!ahead && inGoalWidth && behind) return true;
            return roll;
        }
        if (ahead) return false;
        if (behind) return true;
        if (inGoalWidth) return false;
        return iabs(p.posI.x) < iabs(w.ball.posI.x);
    }
    // 0x4D2016
    if (ballOwnThird && !ahead && iabs(w.ball.posI.z) > (w.pitch.halfWidth >> 1)) return true;
    if (pid == t.f1bc && !behind && P(w, t.f1bc).distBall > P(w, t.f1bc).distBallPrev) return true;
    if (ahead) return false;
    const std::uint32_t r = w.rng.next() & 0x1f;
    if (roll) return true;
    if (ballOwnThird && r < 0x14) return true;
    if (((w.ball.posI.z + w.pitch.thirdLineZ[t.side] >= 0) ? 1 : 0) == t.side) return false;
    return r < 10;
}

// 0x4D1B34 (EDX target, EBX row a, ECX row b): the row whose start point is nearer (a wins ties;
// a's limit 0x120).
static int Tackle_chooseRow_4D1B34(World& w, int pid, int o, int a, int b) {
    (void)o; // 0x4D1B56: target defaults to the possessor but 0x4D1BB8 never reads it
    std::int32_t lim = 0x120;
    int r = -1;
    if (a != -1) {
        lim = Tackle_moveDist_4D1BB8(w, pid, a);
        r = a;
    }
    if (b == -1) return r;
    return Tackle_moveDist_4D1BB8(w, pid, b) < lim ? b : r;
}

// 0x4D1A68 (EDX target, EBX kind): tackle move row for kind (3 = stand or slide, whichever start
// point is nearer); -1 none (AI further than 0x120 from the ball: none).
int Tackle_pickMove_4D1A68(World& w, int pid, int o, int kind) {
    const PlayerState& p = P(w, pid);
    if (p.ctrl == 0 && p.distBall > 0x120) return -1;
    const int cls = Tackle_approachClass_4D2128(w, pid, o >= 0 ? o : w.ball.possessor);
    if (kind != 3) return Tackle_findRow_4D20E0(w, kind, cls);
    int a = -1, b = -1;
    if (Tackle_canStand_4D1D30(w, pid, o)) a = Tackle_findRow_4D20E0(w, 0, cls);
    if (Tackle_canSlide_4D1D78(w, pid, o)) b = Tackle_findRow_4D20E0(w, 1, cls);
    if (a == -1 && b == -1) return -1;
    return Tackle_chooseRow_4D1B34(w, pid, o, a, b);
}

// 0x4D19C8 (EDX kind): start a tackle (state 0x100 with the chosen row) when allowed.
int Player_tryTackle(World& w, int pid, int kind) {
    if (pid == w.ball.possessor) return 0;
    if (w.match.phase != 9 || Match_seqIsFoul(w)) return 0;
    const PlayerState& p = P(w, pid);
    if (!(p.flags22c & 0x200) || (A(w, p).at(0x40) & 1) || p.countdown != 0) return 0;
    if (Player_isState100Kind1(w, pid) || Player_isState100Kind0(w, pid) || Player_isState100Kind2(w, pid)) return 0;
    const int row = Tackle_pickMove_4D1A68(w, pid, Tackle_target_4D21C4(w, pid, kind), kind);
    if (row == -1) return 0;
    Player_setState(w, pid, 0x100, row, 0, false, false);
    return 1;
}

// 0x4E03E4 (EDX tackler): with flags278 bit 16, pid's current anim dodges the tackler's kind.
// WC98 FUN_005108b0 (HANDLER_DIFF_AUDIT pair 16): same shape, flag DAT_0064019e bit 0; the dodge
// anim ids shift -2: stand {0x6C,0x73,0x7E,0x7F} -> {0x6C,0x71,0x7C,0x7D}, slide
// {0x73,0x74,0x75} -> {0x71,0x72,0x73}. WC98 constants implemented.
bool Player_dodgesTackle_4E03E4(const World& w, int pid, int tackler) {
    const int kind = Tackle_kindOf_4D221C(w, tackler);
    const int a = Player_animId(w, pid);
    if (!(w.match.flags278 & 0x10000)) return false; // WC98 DAT_0064019e & 1 (FIFA-99 DAT_0065f27a)
    if (kind == 0) return a == 0x6c || a == 0x71 || a == 0x7c || a == 0x7d; // WC98 0x5108B0
    if (kind == 1) return a == 0x71 || a == 0x72 || a == 0x73;              // WC98 0x5108B0
    return false;
}

// ------------------------------------------------------------- team queries 0x451F4C..0x4529F8

// 0x451F4C: nearest uncontrolled outfield player (not the pass-back target +0x1B8).
int Team_nearestFree_451F4C(const World& w, const Vec3& pos, int team, int excl) {
    const int pb = w.teams[static_cast<std::size_t>(team)].f1b8;
    return nearestTo(w, pos, team, excl,
                     [&](int q, const PlayerState& o) { return o.ctrl != 0 || o.slot == 0 || q == pb; });
}
// 0x452018 (EBX excl, ECX ctrlSlot, stack needOutfield): controlled players only in ctrlSlot.
int Team_nearestPlayer_452018(const World& w, const Vec3& pos, int team, int excl, int ctrlSlot, int needOutfield) {
    const int pb = w.teams[static_cast<std::size_t>(team)].f1b8;
    return nearestTo(w, pos, team, excl, [&](int q, const PlayerState& o) {
        return (o.ctrl != 0 && ctrlSlot != o.curSlot) || (needOutfield != 0 && o.slot == 0) || q == pb;
    });
}
// 0x452100: uncontrolled outfield player with the best distance * facing weight toward the goal
// he attacks (i16 table 0x57F9B4 by |angle error| >> 4).
int Team_bestFacing_452100(const World& w, int team, int excl) {
    static constexpr std::int32_t kW[33] = {7500,  7521,  7583,  7688,  7838,  8034,  8280,  8580,  8937,
                                            9356,  9843,  10401, 11036, 11749, 12542, 13415, 14361, 15375,
                                            16444, 17555, 18689, 19829, 20954, 22044, 23079, 24040, 24909,
                                            25672, 26315, 26827, 27199, 27424, 27500};
    const TeamState& T = w.teams[static_cast<std::size_t>(team)];
    const std::int32_t gz = T.side == 1 ? w.pitch.halfLength : -w.pitch.halfLength;
    const Angle a = atan2i(w.ball.posI.x, w.ball.posI.z - gz);
    int best = -1;
    std::uint32_t bestS = 0x80000000u;
    for (int i = 0; i < T.playerCount; ++i) {
        const int q = T.players[i];
        if (q < 0) continue; // empty roster slot
        const PlayerState& o = P(w, q);
        if ((o.flags22c & 2) || excl == o.curSlot || (o.flags22c & 1) || o.ctrl != 0 || o.slot == 0) continue;
        const auto s = static_cast<std::uint32_t>(o.distBall * kW[absDiff(o.angleBall - a) >> 4]);
        if (s < bestS) {
            best = q;
            bestS = s;
        }
    }
    return best;
}
// 0x4521EC: nearest uncontrolled player (keepers included).
int Team_nearestUncontrolled_4521EC(const World& w, const Vec3& pos, int team, int excl) {
    return nearestTo(w, pos, team, excl, [](int, const PlayerState& o) { return o.ctrl != 0; });
}
// 0x4522A0 (EBX ctrlSlot, ECX needOutfield) -> 0x452018 with excl 0xFF.
int Team_nearestPlayer_4522A0(const World& w, const Vec3& pos, int team, int ctrlSlot, int needOutfield) {
    return Team_nearestPlayer_452018(w, pos, team, 0xff, ctrlSlot, needOutfield);
}
// 0x4522B0: 0x451F4C, else 0x4521EC.
int Team_nearestPlayer_4522B0(const World& w, const Vec3& pos, int team, int excl) {
    const int r = Team_nearestFree_451F4C(w, pos, team, excl);
    return r >= 0 ? r : Team_nearestUncontrolled_4521EC(w, pos, team, excl);
}
// 0x4522D8: 0x452100, else 0x451F4C.
int Team_nearestPlayer_4522D8(const World& w, const Vec3& pos, int team, int excl) {
    const int r = Team_bestFacing_452100(w, team, excl);
    return r >= 0 ? r : Team_nearestFree_451F4C(w, pos, team, excl);
}
// 0x4522FC (EDX skip slot): player nearest to the ball (distBall, unsigned).
int Team_nearestToBall_4522FC(const World& w, int team, int skipSlot) {
    const TeamState& T = w.teams[static_cast<std::size_t>(team)];
    int best = -1;
    std::uint32_t bestD = 0x7fff8000u;
    for (int i = 0; i < T.playerCount; ++i) {
        const int q = T.players[i];
        if (q < 0) continue; // empty roster slot
        const PlayerState& o = P(w, q);
        if ((o.flags22c & 2) || o.curSlot == skipSlot || (o.flags22c & 1)) continue;
        if (static_cast<std::uint32_t>(o.distBall) < bestD) {
            best = q;
            bestD = static_cast<std::uint32_t>(o.distBall);
        }
    }
    return best;
}

// 0x452884 (EAX from, EDX to): hand from's controller to the uncontrolled `to` (pad offsets
// copied, input edges cleared).
void Player_transferControl(World& w, int from, int to) {
    if (from < 0 || to < 0) return;
    PlayerState& f = P(w, from);
    PlayerState& t = P(w, to);
    if (f.ctrl == 0 || t.ctrl != 0) return;
    t.ctrl = f.ctrl;
    w.controllers[static_cast<std::size_t>(f.ctrl - 1)].player = to + 1;
    f.ctrl = 0;
    t.padOffX = f.padOffX;
    t.padOffZ = f.padOffZ;
    Input_clearEdges(w, t.ctrl - 1);
}

// 0x4528C4 (EDX except): give pid (AI, team with humans) the controller of the farthest
// controlled team-mate other than `except`.
void Player_assignControlToReceiver(World& w, int pid, int except) {
    if (pid < 0) return;
    const PlayerState& p = P(w, pid);
    const TeamState& t = T(w, p);
    if (p.ctrl != 0 || t.humans == 0) return;
    int best = -1;
    std::int32_t far = 0;
    for (int i = 0; i < t.playerCount; ++i) {
        const int q = t.players[i];
        if (P(w, q).ctrl == 0 || q == except) continue;
        const std::int32_t d = octLength(p.posI.x - P(w, q).posI.x, p.posI.z - P(w, q).posI.z);
        if (d > far) {
            far = d;
            best = q;
        }
    }
    if (best >= 0) Player_transferControl(w, best, pid);
}

// 0x45299C: the team's goalkeeper (slot 0), -1 none (0x4D03A4 is called for its result only).
int Team_findKeeper(const World& w, int team) {
    const TeamState& T = w.teams[static_cast<std::size_t>(team)];
    for (int i = 0; i < T.playerCount; ++i) {
        const int qT = T.players[i];
        if (qT < 0) continue; // empty roster slot
        if (P(w, qT).slot == 0) return qT;
    }
    return -1;
}

// ------------------------------------------------------------------------ skills / aim 0x4E01EC..

// 0x4E01EC (EAX anim, EDX angle, EBX player): skill-move ball push: velCopy = polar(base * (1 +
// A[0x2A]*0x4FB), angle + per-skill offset), ball.timer34 = 0x2D * (1 + A[0x32]*0x4B0).
void Player_skillMoveBall(World& w, int animId, Angle angle, int pid) {
    std::int32_t a;
    Fixed base = 0x80000;
    switch (animId) {
    case 0x7c: case 0x7a: a = angle + 0x80; break;
    case 0x7d: case 0x7b: a = angle - 0x80; break;
    case 0x77: case 0x6d: a = angle + 0x1c; base = 0xa0000; break;
    case 0x76: case 0x6e: a = angle - 0x1c; base = 0xa0000; break;
    case 0x6f: a = angle + 0x1c; break;
    case 0x70: a = angle - 0x1c; break;
    default: return;
    }
    a = wrapDiff(a);
    const PlayerAttr& at = A(w, P(w, pid));
    const Fixed r = fx(base, at.at(0x2a) * 0x4fb + 0x10000);
    const Fixed t = fx(0x2d, at.at(0x32) * 0x4b0 + 0x10000);
    const SinCos sc = sinCos(a); // 0x4BB168
    w.ball.velCopy = {fx(r, sc.s), 0, fx(r, sc.c)};
    w.ball.timer34 = t;
}

// 0x4E0448: human pad direction, else the desired facing.
Angle Player_inputAimAngle(const World& w, int pid) {
    const PlayerState& p = P(w, pid);
    if (p.ctrl != 0) {
        const ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
        if (c.dirX != 0 || c.dirZ != 0) return atan2i(c.dirX, c.dirZ);
    }
    return p.facingDesired;
}

// *(u16*)[player+0x68]: id of the current AISEQS record (AnimCtl::seq = record id + 1).
int Player_animId(const World& w, int pid) { return P(w, pid).anim.seq - 1; }

} // namespace wc98::sim
