// Ball physics (FIFA99.EXE v1.1): kick -> trajectory precompute (ring of 180 frames) -> per-frame
// playback, ground bounce, goal frame (posts, crossbar, nets), stadium walls, possession.
// Ported instruction by instruction from 0x474DC0..0x4776xx and 0x481B48..0x484C08; every
// constant cites its exe address. Docs: docs/reverse/SIMULATION.md §4.
#include "wc98/sim/world.hpp"

#include "ball_tables.inc"

namespace wc98::sim {

using namespace balltab;

namespace {

constexpr Fixed sh(std::int32_t v) { return static_cast<Fixed>(static_cast<std::uint32_t>(v) << 16); }

Vec3 shr16(const Vec3& v) { return {v.x >> 16, v.y >> 16, v.z >> 16}; }

int sgn3(Fixed v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

void absPos(BallState& b) {
    b.absX = iabs(b.tpos.x);
    b.absZ = iabs(b.tpos.z);
    b.absPrevX = iabs(b.tprev.x);
    b.absPrevZ = iabs(b.tprev.z);
}

// Pitch-derived goal frame constants (all from 0x65F340..0x65F37C and radius 0x6691D4).
struct Goal {
    std::int32_t hl, hw, r, post2, bar2, gh, ghw;
    explicit Goal(const World& w)
        : hl(w.pitch.halfLength), hw(w.pitch.halfWidth), r(w.ball.radius), post2(w.pitch.postThickness >> 1),
          bar2(w.pitch.barThickness >> 1), gh(w.pitch.goalHeight), ghw(w.pitch.goalHalfWidth) {}
    Fixed innerX() const { return sh(post2 + ghw - r); }      // 226
    Fixed outerX() const { return sh(post2 + ghw + r); }      // 236
    Fixed barTop() const { return sh(bar2 + gh); }            // 155
    Fixed underBar() const { return sh(bar2 + gh) - sh(2 * r); } // 145
};

// ---- plane / cylinder solvers (0x481C44, 0x481CB8, 0x481D34) ----

bool planeSolve(Fixed d, Fixed o, Fixed* t) { // 0x481C44
    if (iabs(d) < 0x10000) {
        if (static_cast<std::int32_t>(static_cast<std::uint32_t>(iabs(d)) << 15) <= iabs(o)) return false;
        if (d == 0) return false;
    }
    *t = fdiv(o, d);
    return true;
}

bool lineSolve(Fixed a, Fixed b, Fixed c, Fixed d, Fixed s1, Fixed s2, Fixed* t) { // 0x481CB8
    const Fixed den = fx(a, d) - fx(b, c);
    if (den == 0) return false;
    *t = fdiv(fx(s1, d) - fx(s2, c), den);
    return true;
}

bool cylinderSolve(Fixed o1, Fixed o2, Fixed d1, Fixed d2, std::int32_t r2, Fixed* t) { // 0x481D34
    if (d1 == 0 && d2 == 0) return false;
    const Fixed a = fx(d1, d1), b1 = fx(d1, o1), c1 = fx(d1, o2);
    const Fixed e = fx(d2, o1), f = fx(d2, o2);
    const Fixed dd = fx(d2, d2) + a;
    const Fixed cr = c1 - e;
    const Fixed disc = fx(dd, r2) - fx(cr, cr >> 16);
    if (disc < 0) return false;
    const Fixed s = sh(static_cast<std::int32_t>(Math_isqrt(static_cast<std::uint32_t>(disc))));
    *t = fdiv(f + b1 - s, dd);
    return true;
}

bool better(bool ok, Fixed t, Fixed* best) {
    if (!ok || t < 0 || t >= *best) return false;
    *best = t;
    return true;
}

bool hitPost(const World& w, const Vec3& prev, const Vec3& pos, Fixed* best) { // 0x4821EC
    const Goal g(w);
    Fixed cx = sh(g.post2 + g.ghw);
    if (pos.x <= 0) cx = -cx;
    Fixed cz = sh(g.hl - g.post2);
    if (pos.z <= 0) cz = -cz;
    Fixed t = 0;
    const std::int32_t rr = (g.post2 + g.r) * (g.post2 + g.r);
    return better(cylinderSolve(cx - prev.x, cz - prev.z, pos.x - prev.x, pos.z - prev.z, rr, &t), t, best);
}

bool hitBar(const World& w, const Vec3& prev, const Vec3& pos, Fixed* best) { // 0x482290
    const Goal g(w);
    const Fixed oy = sh(g.bar2 + g.gh - g.r) - prev.y;
    Fixed cz = sh(g.hl - (w.pitch.postThickness >> 1));
    const Fixed oz = pos.z > 0 ? cz - prev.z : -cz - prev.z;
    const std::int32_t rr = (g.bar2 + g.r) * (g.bar2 + g.r);
    Fixed t = 0;
    return better(cylinderSolve(oy, oz, pos.y - prev.y, pos.z - prev.z, rr, &t), t, best);
}

// Net surfaces: back 0x48235C, inner side 0x4824BC, roof 0x48263C, outer roof 0x482840, outer side
// 0x482938, goal line 0x482744. `check` = ECX (0 from the net-contact routine: no bounds/store).
bool netBack(const World& w, const Vec3& prev, const Vec3& pos, Fixed* best, bool check) {
    const Goal g(w);
    const Fixed* n = w.ball.net;
    const int s = pos.z > 0 ? 1 : -1;
    Fixed t;
    if (!lineSolve(pos.y - prev.y, pos.z - prev.z, g.underBar(), s * (n[2] - n[1]), -prev.y, n[1] * s - prev.z, &t)) return false;
    if (t < 0 || t >= *best) return false;
    const Fixed px = prev.x + fx(t, pos.x - prev.x), pz = prev.z + fx(t, pos.z - prev.z);
    if (!check) return true;
    if (iabs(px) >= g.innerX()) return false;
    const Fixed az = iabs(pz);
    if (az < n[2] || az > n[1]) {
        if (n[2] != n[1]) return false;
    }
    *best = t;
    return true;
}

Fixed slant(Fixed h, Fixed a, Fixed b) {
    if (h > 0x10000) return a - b;
    if (h < 0) return 0;
    return fx(h, a) - fx(h, b);
}

bool netSide(const World& w, const Vec3& prev, const Vec3& pos, Fixed* best, bool check) {
    const Goal g(w);
    const Fixed* n = w.ball.net;
    Fixed px = g.innerX();
    if (pos.x <= 0) px = -px;
    Fixed t;
    if (!planeSolve(pos.x - prev.x, px - prev.x, &t) || t < 0 || t >= *best) return false;
    const Fixed py = prev.y + fx(t, pos.y - prev.y), pz = prev.z + fx(t, pos.z - prev.z);
    const Fixed lim = slant(fdiv(py, g.underBar()), n[1], n[2]);
    if (!check) return true;
    if (iabs(pz) <= sh(g.hl)) return false;
    if (iabs(pz) >= n[1] - lim) return false;
    *best = t;
    return true;
}

bool netRoof(const World& w, const Vec3& prev, const Vec3& pos, Fixed* best, bool check) {
    const Goal g(w);
    Fixed t;
    if (!planeSolve(pos.y - prev.y, g.underBar() - prev.y, &t) || t < 0 || t >= *best) return false;
    const Fixed px = prev.x + fx(t, pos.x - prev.x), pz = prev.z + fx(t, pos.z - prev.z);
    if (!check) return true;
    if (iabs(px) >= g.innerX()) return false;
    if (iabs(pz) >= w.ball.net[2]) return false;
    if (iabs(pz) <= sh(g.hl)) return false;
    *best = t;
    return true;
}

bool netOuterRoof(const World& w, const Vec3& prev, const Vec3& pos, Fixed* best, bool check) {
    const Goal g(w);
    Fixed t;
    if (!planeSolve(pos.y - prev.y, g.barTop() - prev.y, &t) || t < 0 || t >= *best) return false;
    const Fixed px = prev.x + fx(t, pos.x - prev.x), pz = prev.z + fx(t, pos.z - prev.z);
    if (!check) return true;
    if (iabs(px) >= g.outerX()) return false;
    if (iabs(pz) >= w.ball.net[0]) return false;
    if (iabs(pz) <= sh(g.hl)) return false;
    *best = t;
    return true;
}

bool netOuterSide(const World& w, const Vec3& prev, const Vec3& pos, Fixed* best, bool check) {
    const Goal g(w);
    const Fixed* n = w.ball.net;
    Fixed px = g.outerX();
    if (pos.x <= 0) px = -px;
    Fixed t;
    if (!planeSolve(pos.x - prev.x, px - prev.x, &t) || t < 0 || t >= *best) return false;
    const Fixed py = prev.y + fx(t, pos.y - prev.y), pz = prev.z + fx(t, pos.z - prev.z);
    const Fixed lim = slant(fdiv(py, g.barTop()), n[3], n[0]);
    if (!check) return true;
    if (iabs(pz) <= sh(g.hl)) return false;
    if (iabs(pz) >= n[3] - lim + 0x80000) return false;
    *best = t;
    return true;
}

bool goalLineCrossing(const World& w, const Vec3& prev, const Vec3& pos, Fixed* best) { // 0x482744
    const Goal g(w);
    Fixed lz = sh(g.hl + g.r);
    if (pos.z <= 0) lz = -lz;
    Fixed t;
    if (!planeSolve(pos.z - prev.z, lz - prev.z, &t) || t < 0 || t >= *best) return false;
    const Fixed px = prev.x + fx(t, pos.x - prev.x), py = prev.y + fx(t, pos.y - prev.y);
    if (iabs(px) > g.innerX()) return false;
    if (g.underBar() < py) return false;
    *best = t;
    return true;
}

// 0x481E34: reflect the integrator velocity about n, scaled by `scale` * |v|.
void reflect(BallState& b, const Vec3& n, Fixed scale) {
    const Vec3& v = b.tvel;
    const Fixed mag = Math_sqrtFx(static_cast<std::uint32_t>(fx(v.x, v.x) + fx(v.y, v.y) + fx(v.z, v.z)));
    Fixed ux = 0, uy = 0, uz = 0;
    if (mag) {
        ux = -fdiv(v.x, mag);
        uy = -fdiv(v.y, mag);
        uz = -fdiv(v.z, mag);
    }
    const Fixed k = (fx(ux, n.x) + fx(uy, n.y) + fx(uz, n.z)) * 2;
    const Fixed rx = fx(n.x, k) - ux, ry = fx(n.y, k) - uy, rz = fx(n.z, k) - uz;
    const Fixed s = fx(scale, mag);
    b.tvel = {fx(rx, s), fx(ry, s), fx(rz, s)};
}

// 0x41DF38: stand walls at |z| = 3220, |x| = 2400 (slanted normal), installed by the stadium
// setup. Returns the step fraction or -1. (0x57C684 = 100.0f: the y tests are float compares of
// the 16.16 integers against 6553600.0, exact as integer compares.)
Fixed stadiumWall(const Vec3& prev, const Vec3& pos, Vec3* n) {
    const Fixed zw = 0xc940000, xw = 0x9600000;
    const Fixed dx = pos.x - prev.x, dz = pos.z - prev.z;
    if (dx == 0 || dz == 0) return -1;
    const int sx = dx / iabs(dx), sz = dz / iabs(dz);
    if (prev.y > 6553600 && pos.y > 6553600) return -1;
    if (pos.z < -zw && sz < 0) {
        *n = {0, 0x578d, 0xf0a3};
        return fdiv(iabs(pos.z) - zw, iabs(pos.z) - iabs(prev.z));
    }
    if (zw < pos.z && sz > 0) {
        *n = {0, 0x578d, -0xf0a3};
        return fdiv(iabs(pos.z) - zw, iabs(pos.z) - iabs(prev.z));
    }
    if (pos.x < -xw && sx < 0) {
        *n = {0xf0a3, 0x578d, 0};
        return fdiv(iabs(pos.x) - xw, iabs(pos.x) - iabs(prev.x));
    }
    if (xw < pos.x && sx > 0) {
        *n = {-0xf0a3, 0x578d, 0};
        return fdiv(iabs(pos.x) - xw, iabs(pos.x) - iabs(prev.x));
    }
    return -1;
}

// WC98 post (x/z plane) FUN_004bf660 / crossbar (y/z plane) FUN_004bf760.
// FUN_004bf660 (WC98 post bounce; pass-16 line-by-line vs decompile dossier): atan2 mirror,
// sqrt(vx^2+vz^2)*0x8000 damping, tvel.y*0x8000.
void postBounce(BallState& b, Fixed nx, Fixed nz) {
    const std::int32_t a = Math_atan2_16(nx, nz) * 2 - Math_atan2_16(b.tprev.x - b.tpos.x, b.tprev.z - b.tpos.z);
    Fixed s, c;
    Math_sinCos16(a, &s, &c);
    const Fixed h = fx(Math_sqrtFx(static_cast<std::uint32_t>(fx(b.tvel.x, b.tvel.x) + fx(b.tvel.z, b.tvel.z))), 0x8000);
    b.tvel.y = fx(b.tvel.y, 0x8000);
    b.tvel.x = fx(h, s);
    b.tvel.z = fx(h, c);
}

void barBounce(BallState& b, Fixed ny, Fixed nz) { // FUN_004bf760 (WC98; atan2 mirror y/z plane, sqrt*0x8000, tvel.x*0xc000)
    const std::int32_t a = Math_atan2_16(ny, nz) * 2 - Math_atan2_16(b.tprev.y - b.tpos.y, b.tprev.z - b.tpos.z);
    Fixed s, c;
    Math_sinCos16(a, &s, &c);
    const Fixed h = fx(Math_sqrtFx(static_cast<std::uint32_t>(fx(b.tvel.y, b.tvel.y) + fx(b.tvel.z, b.tvel.z))), 0x8000);
    b.tvel.x = fx(b.tvel.x, 0xc000);
    b.tvel.y = fx(h, s);
    b.tvel.z = fx(h, c);
}

void Ball_markGoal(World& w, BallEntry& e) { // 0x483228
    BallState& b = w.ball;
    if (b.stamp[6] != -1) return;
    if (b.stamp[7] != -1 && !(b.stamp[7] > b.step)) return;
    if (b.stamp[5] != -1 && !(b.stamp[5] + 10 > b.step)) return;
    e.flags |= 0x30;
    b.stamp[6] = static_cast<std::int16_t>(b.step);
    const int side = b.tpos.z > 0 ? 1 : 0;
    if (b.lastToucher >= 0 && w.teams[w.players[b.lastToucher].team].side != side) {
        b.flags |= 4;
        // FUN_004D71E0 sets the commentary flag 0x66727C (presentation, skipped).
    }
}

// 0x483C34: damp the velocity component pointing out of the net the ball rests in.
// FUN_004c12e0 (WC98 netDamp): switch on netCase {1 roof, 2/3 side-net, 4 back}; gates vs absX/net[2]
// bands; damp = -fx(v,0x4000) when velocity sign == tpos sign. Pass-16 verified row-for-row.
void netDamp(World& w) {
    BallState& b = w.ball;
    const Goal g(w);
    auto damp = [](Fixed& v) { v = -fx(v, 0x4000); };
    switch (b.netCase) {
    case 1:
        if (g.innerX() <= b.absX && sgn3(b.tvel.x) == sgn3(b.tpos.x)) damp(b.tvel.x);
        if (b.net[2] <= b.absZ && sgn3(b.tvel.z) == sgn3(b.tpos.z)) damp(b.tvel.z);
        break;
    case 2:
    case 3:
        if (g.underBar() <= b.tprev.y && b.tvel.y > 0) damp(b.tvel.y);
        if (b.net[2] <= b.absZ && sgn3(b.tvel.z) == sgn3(b.tpos.z)) damp(b.tvel.z);
        break;
    case 4:
        if (g.innerX() <= b.absX && sgn3(b.tvel.x) == sgn3(b.tpos.x)) damp(b.tvel.x);
        break;
    default: break;
    }
}

void setNetPos(BallState& b, const Vec3& p) {
    b.netPos = p;
    b.netPos.y = 0;
    b.landing = shr16(b.netPos);
}

// z of the net contact for the side nets (cases 6 and 9).
void sideNetZ(World& w, Fixed pz) {
    BallState& b = w.ball;
    const Fixed e = iabs(fx(b.tvel.z, 0x78000) + pz);
    if (e > b.net[1]) b.netPos.z = b.netPos.z > 0 ? b.net[1] - 0x20000 : -b.net[1] + 0x20000;
    else if (e < sh(w.pitch.halfLength)) b.netPos.z = b.netPos.z > 0 ? sh(w.pitch.halfLength) + 0x20000 : -sh(w.pitch.halfLength) - 0x20000;
    else b.netPos.z += fx(b.tvel.z, 0x78000);
}

void Ball_resolveCollision(World& w, BallEntry& e, int kind, const Vec3& p) { // FUN_004c09a0 (WC98 switch 1..10; post/bar helpers FUN_004bf660/0x4bf760; wall clamp ±0x50000 under flag 0x40; netCase stores +0xb60, contactMode/timer rows 1/0x3c — pass-16 verified)
    BallState& b = w.ball;
    const Goal g(w);
    switch (kind) {
    case 1: { // post
        const Fixed cx = sh(g.post2 + g.ghw), cz = sh(g.hl - g.post2);
        const Fixed nx = b.tpos.x > 0 ? p.x - cx : p.x + cx;
        const Fixed nz = b.tpos.z > 0 ? p.z - cz : p.z + cz;
        if (b.stamp[8] == -1) b.stamp[8] = static_cast<std::int16_t>(b.step);
        e.flags |= 0x1000;
        postBounce(b, nx, nz);
        b.tpos = p;
        w.emit(EventType::PostHit, b.lastToucher >= 0 ? w.players[b.lastToucher].team : -1, b.lastToucher);
        break;
    }
    case 2: { // crossbar
        const Fixed ny = p.y - sh(g.bar2 + g.gh - g.r);
        const Fixed cz = sh(g.hl - g.post2);
        const Fixed nz = b.tpos.z > 0 ? p.z - cz : p.z + cz;
        if (b.stamp[8] == -1) b.stamp[8] = static_cast<std::int16_t>(b.step);
        e.flags |= 0x2000;
        barBounce(b, ny, nz);
        b.tpos = p;
        w.emit(EventType::BarHit, b.lastToucher >= 0 ? w.players[b.lastToucher].team : -1, b.lastToucher);
        break;
    }
    case 3: // fun-mode side walls (DAT_0065EF55 & 1)
    case 4: {
        e.flags |= 0x8000;
        Fixed& v = kind == 3 ? b.tvel.x : b.tvel.z;
        v = -fx(v, 0xc000);
        if (b.flags & 0x40) {
            if (v < -0x50000) v = -0x50000;
            else if (v > 0x50000) v = 0x50000;
        }
        b.tpos = p;
        break;
    }
    case 5: // inner roof
        b.netPrev = b.tprev;
        b.tvel.y = 0;
        Ball_markGoal(w, e);
        e.flags |= 0x01010000;
        b.netCase = 1;
        b.contactMode = 2;
        setNetPos(b, p);
        netDamp(w);
        break;
    case 6: { // inner side net
        b.netPrev = b.tprev;
        if (b.absZ < b.net[2] && b.net[2] > b.absPrevZ) Ball_markGoal(w, e);
        const bool same = (b.tpos.z > 0) == (b.tpos.x > 0);
        e.flags |= same ? 0x40000u : 0x20000u;
        e.flags |= 0x01000000;
        b.netCase = same ? 3 : 2;
        b.contactMode = 2;
        b.netPos = p;
        sideNetZ(w, p.z);
        b.netPos.y = 0;
        b.landing = shr16(b.netPos);
        netDamp(w);
        break;
    }
    case 7: { // back net
        b.netPrev = b.tprev;
        Ball_markGoal(w, e);
        e.flags |= 0x01080000;
        b.netCase = 4;
        b.contactMode = 2;
        b.netPos = p;
        const Fixed lim = g.innerX();
        if (iabs(fx(b.tvel.x, 0x50000) + p.x) > lim) b.netPos.x = b.tvel.x > 0 ? lim - 0x100000 : -lim + 0x100000;
        else b.netPos.x += fx(b.tvel.x, 0x50000);
        b.netPos.y = 0;
        b.landing = shr16(b.netPos);
        netDamp(w);
        break;
    }
    case 8: // outer roof
        b.netPrev = b.tprev;
        e.flags |= 0x01100000;
        b.netCase = 5;
        b.contactMode = 2;
        b.netPos = p;
        b.netPos.y -= 0x60000;
        b.landing = shr16(b.netPos);
        break;
    case 9: { // outer side net
        b.netPrev = b.tprev;
        const bool same = (b.tpos.z > 0) == (b.tpos.x > 0);
        e.flags |= same ? 0x400000u : 0x200000u;
        e.flags |= 0x01000000;
        b.netCase = same ? 7 : 6;
        b.contactMode = 2;
        b.netPos = p;
        sideNetZ(w, p.z);
        b.netPos.y = 0;
        b.landing = shr16(b.netPos);
        break;
    }
    case 10: // outer back
        b.netPrev = b.tprev;
        e.flags |= 0x01800000;
        b.netCase = 8;
        b.contactMode = 2;
        b.netPos.x = p.x;
        b.netPos.z = p.z > 0 ? b.net[3] : -b.net[3];
        b.netPos.y = 0;
        b.landing = shr16(b.netPos);
        break;
    default: break;
    }
}

void Ball_collideGoalMouth(World& w, BallEntry& e, Fixed* best, int* kind) { // FUN_004c159c (WC98; geometry predicates netBack=FUN_004bf9e0, netSide=FUN_004bfb40, netRoof=FUN_004bfcc0, crossing=FUN_004bfdd0 + FUN_004c08d0 — pass-16 verified)
    BallState& b = w.ball;
    const Goal g(w);
    if (b.net[2] <= b.absZ) {
        bool test = b.stamp[6] != -1 || w.match.phase == 0xd || sh(g.hl + g.r) > b.absPrevZ;
        if (test && netBack(w, b.tprev, b.tpos, best, true)) *kind = 7;
    }
    if (sh(g.hl) > b.absZ) return;
    if (g.innerX() <= b.absX && netSide(w, b.tprev, b.tpos, best, true)) *kind = 6;
    if (g.underBar() <= b.tpos.y && netRoof(w, b.tprev, b.tpos, best, true)) *kind = 5;
    const Fixed lz = sh(g.hl + g.r);
    if (lz < b.absZ && lz >= b.absPrevZ) {
        Fixed t = *best;
        if (goalLineCrossing(w, b.tprev, b.tpos, &t)) Ball_markGoal(w, e);
    }
}

void stampOnce(BallState& b, int i) {
    if (b.stamp[i] == -1) b.stamp[i] = static_cast<std::int16_t>(b.step);
}

// Height crossing bookkeeping for y = 142 / 85.
void heightMarker(BallState& b, BallEntry& e, Fixed y, std::uint32_t flag, std::int16_t& count, std::int16_t* at,
                  Vec3& pos, std::int32_t& frame) {
    const bool cross = b.tprev.y < y ? b.tpos.y >= y : (b.tprev.y > y && b.tpos.y <= y);
    if (!cross) return;
    e.flags |= 0x8000000 | flag;
    const bool falling = b.tprev.y > b.tpos.y;
    if (b.crossCount < 8 && (b.step > 0x14 || falling)) b.crossAt[b.crossCount++] = static_cast<std::int16_t>(b.step);
    if (count < 8) {
        at[count] = static_cast<std::int16_t>(b.step);
        ++count;
        if (count <= 2 && falling) {
            pos = shr16(b.tpos);
            frame = b.step;
        }
    }
}

void Ball_collideStep(World& w, BallEntry& e) { // FUN_004c0150 net-rest tail (WC98; retest switch, roof lane 0x4ccc/0x30000..0x50000/0x3333, reflect helper FUN_004bf490, rng-trig exit — pass-16 verified)
    BallState& b = w.ball;
    const Goal g(w);
    int kind = 0;
    Fixed best = 0x10001;
    heightMarker(b, e, 0x8e0000, 0x400, b.cross142Count, b.cross142At, b.cross142Pos, b.cross142Frame);
    heightMarker(b, e, 0x550000, 0x800, b.cross85Count, b.cross85At, b.cross85Pos, b.cross85Frame);
    if (b.tpos.y < 0) { // ground bounce
        e.flags |= 0x40;
        b.tpos.y = 0;
        if (b.bounceCount < 12) {
            if (b.bounceCount == 0) {
                b.stamp[4] = static_cast<std::int16_t>(b.step);
                if (!(b.flags & 0x80)) {
                    b.landing = {b.tpos.x >> 16, 0, b.tpos.z >> 16};
                    b.landingFrame = b.step;
                    const OctDist d = octDistance(b.posI, b.landing);
                    b.f04[0] = d.dist;
                    b.f04[1] = d.dx;
                    b.f04[2] = d.dz;
                }
            }
            b.bounceAt[b.bounceCount++] = static_cast<std::int16_t>(b.step);
        }
        b.tvel.y = -fx(b.tvel.y, b.restitution);
        b.tvel.x = fx(b.tvel.x, b.bounceRetain);
        b.tvel.z = fx(b.tvel.z, b.bounceRetain);
        if (b.tvel.y < 0xc000) b.tvel.y = 0;
    }
    if (w.opt.funModes & 1) { // side walls
        const Fixed lx = sh(g.hw - g.r - 1);
        if (lx < b.absX && b.absX - b.absPrevX != 0) {
            best = fdiv(lx - b.absPrevX, b.absX - b.absPrevX);
            kind = 3;
        }
        if (b.stamp[6] == -1) {
            const Fixed lz = sh(g.hl - g.r - 1);
            if (lz < b.absZ) {
                const bool inMouth = sh(g.bar2 + g.gh - g.r) >= b.tpos.y && sh(g.post2 + g.ghw) >= b.absX;
                if (!inMouth && b.absZ - b.absPrevZ != 0) {
                    const Fixed t = fdiv(lz - b.absPrevZ, b.absZ - b.absPrevZ);
                    if (t < best && t >= 0) {
                        best = t;
                        kind = 4;
                    }
                }
            }
        }
    }
    if (b.contactMode == 2) {
        netDamp(w);
    } else {
        const Fixed z0 = sh(g.hl - (w.pitch.postThickness + g.r) - 2);
        const Fixed z1 = sh(g.hl + g.r + 2);
        const bool nearLine = (z0 <= b.absZ || z0 <= b.absPrevZ) && !(z1 < b.absZ && z1 < b.absPrevZ);
        if (nearLine) {
            const Fixed xo = sh(w.pitch.postThickness + g.r + g.ghw + 2);
            if (!(xo < b.absX && xo < b.absPrevX)) {
                const Fixed xi = sh(g.ghw - g.r - 2);
                if (!(xi > b.absX && xi > b.absPrevX)) {
                    const Fixed yp = sh(g.bar2 + g.gh - g.r);
                    if ((yp >= b.tpos.y || yp >= b.tprev.y) && hitPost(w, b.tprev, b.tpos, &best)) kind = 1;
                }
                const Fixed yb = sh(g.gh - g.r - 2);
                if (!(yb > b.tpos.y && yb > b.tprev.y)) {
                    const Fixed yt = sh(w.pitch.barThickness + g.r + g.gh + 2);
                    if ((yt >= b.tpos.y || yt >= b.tprev.y) && hitBar(w, b.tprev, b.tpos, &best)) kind = 2;
                }
            }
        }
        if (sh(g.post2 + g.ghw - g.r) + 0x200000 >= b.absPrevX && b.net[1] >= b.absPrevZ && g.underBar() >= b.tprev.y)
            Ball_collideGoalMouth(w, e, &best, &kind);
        const Fixed hl = sh(g.hl);
        if (!(hl > b.absZ && hl > b.absPrevZ) && !(b.net[3] < b.absZ && b.net[3] < b.absPrevZ) &&
            !(g.outerX() < b.absX && g.outerX() < b.absPrevX) && !(g.barTop() < b.tpos.y && g.barTop() < b.tprev.y)) {
            if (hl <= b.absZ && g.outerX() >= b.absX && netOuterSide(w, b.tprev, b.tpos, &best, true)) kind = 9;
            if (netOuterRoof(w, b.tprev, b.tpos, &best, true)) kind = 8;
            if (b.net[3] >= b.absZ && netBack(w, b.tprev, b.tpos, &best, true)) kind = 10;
        }
    }
    if (kind != 0) {
        const Vec3 p{b.tprev.x + fx(best, b.tpos.x - b.tprev.x), b.tprev.y + fx(best, b.tpos.y - b.tprev.y),
                     b.tprev.z + fx(best, b.tpos.z - b.tprev.z)};
        Ball_resolveCollision(w, e, kind, p);
        b.absX = iabs(b.tpos.x);
        b.absZ = iabs(b.tpos.z);
    }
    if (sh(g.hl + g.r) < b.absZ || sh(g.hw + g.r) < b.absX) { // out of the pitch
        stampOnce(b, 5);
        e.flags |= 0x10;
    }
    if ((e.flags & 0x10) && b.stadiumWalls) {
        Vec3 n;
        const Fixed t = stadiumWall(b.tprev, b.tpos, &n);
        if (t >= 0 && t <= 0x10000) {
            const Fixed u = 0x10000 - t;
            reflect(b, n, 0x9999);
            b.tpos = {fx(b.tpos.x, t), fx(b.tpos.y, t), fx(b.tpos.z, t)};
            b.tpos.x += fx(b.tprev.x, u);
            b.tpos.y += fx(b.tprev.y, u);
            b.tpos.z += fx(b.tprev.z, u);
            stampOnce(b, 7);
        }
    }
    if (sh(w.pitch.penAreaLineZ) < b.absZ && sh(w.pitch.penAreaHalfWidth) > b.absX) {
        stampOnce(b, 9);
        e.flags |= 0x80;
    }
    if (sh(w.pitch.goalAreaLineZ) < b.absZ && sh(w.pitch.goalAreaHalfWidth) > b.absX) {
        stampOnce(b, 10);
        e.flags |= 0x100;
    }
    if (b.contactMode != 0 && (e.flags & 0xff0000) == 0) {
        e.flags |= b.lastEntryFlags & 0xff0000;
        if (b.contactMode == 2) e.flags |= 0x01000000;
    }
    b.lastEntryFlags = e.flags;
}

// 0x482AB0: ball held by a net (contact mode 2): release test, damping, spring toward netPos.
void netContact(World& w) {
    BallState& b = w.ball;
    const Goal g(w);
    Fixed best = 0x10000;
    Vec3 q{0, 0x100000, 0};
    const Fixed zIn = b.net[2] - 0x100000;
    bool tested = true, hit = false;
    switch (b.netCase) {
    case 1:
        q.z = b.netPrev.z < 0 ? -zIn : zIn;
        hit = netRoof(w, q, b.tpos, &best, false);
        break;
    case 2:
    case 3:
        q.z = b.netPrev.z < 0 ? -zIn : zIn;
        hit = netSide(w, q, b.tpos, &best, false);
        break;
    case 4: {
        if (b.net[1] + 0x100000 <= b.absZ) { tested = false; break; }
        Fixed y = g.underBar() - 0x100000;
        if (y > b.tpos.y) y = b.tpos.y;
        q.y = y;
        q.z = b.netPrev.z < 0 ? -zIn : zIn;
        hit = netBack(w, q, b.tpos, &best, false);
        break;
    }
    case 5:
        q.z = b.netPrev.z < 0 ? -zIn : zIn;
        q.y = g.barTop() + 0x100000;
        hit = netOuterRoof(w, q, b.tpos, &best, false);
        break;
    case 6:
    case 7:
        q.x = g.outerX() + 0x100000;
        if (b.netPrev.x < 0) q.x = -q.x;
        q.z = b.netPrev.z < 0 ? -zIn : zIn;
        hit = netOuterSide(w, q, b.tpos, &best, false);
        break;
    case 8:
        q.z = b.net[3] + 0x100000;
        if (b.netPrev.z < 0) q.z = -q.z;
        hit = netBack(w, q, b.tpos, &best, false);
        break;
    default: tested = false; break;
    }
    if (tested && !hit) {
        b.contactMode = 1;
        b.contactTimer = 0x3c;
    }
    Vec3& v = b.tvel;
    if (b.netCase == 5) { // resting on the outer roof
        if (fx(v.x, v.x) + fx(v.z, v.z) > 0x400000 && v.y < 0) {
            v.y = fx(-v.y, 0x4ccc);
            if (v.y < 0x30000) v.y = 0x30000;
            else if (v.y > 0x50000) v.y = 0x50000;
            v.x = fx(v.x, 0x3333);
            v.z = fx(v.z, 0x3333);
            return;
        }
        const Fixed dyTop = g.barTop() - b.tpos.y;
        const Fixed dy2 = fx(dyTop, dyTop);
        Fixed edi, ebp;
        if (b.tpos.z > 0) {
            ebp = b.net[0] - b.tpos.z;
            edi = sh(g.hl) - b.tpos.z;
        } else {
            edi = -sh(g.hl) - b.tpos.z;
            ebp = -b.net[0] - b.tpos.z;
        }
        const Fixed l1 = Math_sqrtFx(static_cast<std::uint32_t>(fx(ebp, ebp) + dy2));
        const Fixed l2 = Math_sqrtFx(static_cast<std::uint32_t>(fx(edi, edi) + dy2));
        const Fixed depth = b.net[0] - sh(g.hl);
        if (l1 + l2 - depth > fx(depth, 0x1999)) {
            std::int32_t a = (atan2i(ebp, dyTop) + atan2i(edi, dyTop)) & 0x3ff;
            if (a > 0x200) a -= 0x400;
            a >>= 1;
            const SinCos sc = sinCos(a);
            const Vec3 n{0, sc.c, sc.s};
            const Fixed avy = iabs(v.y);
            if (avy < 0x147) {
                v = {0, 0x2468, 0}; // WC98 mov [ball+0xb9c],0x2468 @VA 0x4bfaf8 (was FIFA-99 0x2f5c)
            } else if (avy < 0x20000) {
                reflect(b, n, 0xfae1);
            } else if (v.y < 0) {
                reflect(b, n, 0x4ccc);
            }
        }
        if (iabs(b.tpos.x) > g.outerX()) v.x = -v.x;
        return;
    }
    v.x = fx(v.x, 0xc000);
    v.y = fx(v.y, 0xc000);
    v.z = fx(v.z, 0xc000);
    Fixed dx = b.netPos.x - b.tpos.x, dy = b.netPos.y - b.tpos.y, dz = b.netPos.z - b.tpos.z;
    std::int32_t len = static_cast<std::int32_t>(Math_isqrt(static_cast<std::uint32_t>(
        (dy >> 16) * (dy >> 16) + (dx >> 16) * (dx >> 16) + (dz >> 16) * (dz >> 16))));
    if (len < 5) len = 5;
    const Fixed k = fdiv(0xcccc, sh(len));
    dx = fx(dx, k);
    dy = fx(dy, k);
    dz = fx(dz, k);
    const std::int32_t before = octLength(v.x, v.z);
    v.y += dy;
    v.x += dx;
    v.z += dz;
    const std::int32_t after = octLength(v.x, v.z);
    if (before < after && after > 0x100000) {
        const Angle a = atan2i(v.x, v.z);
        v.x = fx(0x100000, sinCos(a).s);
        v.z = fx(0x100000, sinCos(a).c);
    }
}

void advance(BallState& b, int& idx, BallEntry*& e) {
    ++idx;
    if (idx == kRingSize) idx = 0;
    e = &b.ring[idx];
    e->flags = 0;
    b.tprev = b.tpos;
    b.tpos.x += b.tvel.x;
    b.tpos.y += b.tvel.y;
    b.tpos.z += b.tvel.z;
    absPos(b);
}

// FIFA-99 `Ball_steerToTarget` family cite (Cam-side grammar; the cited FUN_004766E4 has ZERO
// call/jmp/dword references in FIFAWC.EXE — absent from WC98, register D-50): steer the ball to
// dribbleTarget over n frames (no drag/gravity).
void Ball_moveToward(World& w, int n) {
    BallState& b = w.ball;
    b.trajFlags &= ~1u;
    if (static_cast<std::uint32_t>(n) > static_cast<std::uint32_t>(b.freeSlots)) n = b.freeSlots;
    const Fixed inv = static_cast<Fixed>(0x100000000LL / sh(n));
    b.tvel.x = fx(sh(b.dribbleTarget.x) - b.tpos.x, inv);
    b.tvel.y = fx(sh(b.dribbleTarget.y) - b.tpos.y, inv);
    b.tvel.z = fx(sh(b.dribbleTarget.z) - b.tpos.z, inv);
    auto clampv = [](Fixed& v, Fixed m) { if (v < -m) v = -m; else if (v > m) v = m; };
    clampv(b.tvel.x, 0xf0000);
    clampv(b.tvel.y, 0xa0000);
    clampv(b.tvel.z, 0xf0000);
    const Fixed mag = Math_sqrtFx(static_cast<std::uint32_t>(fx(b.tvel.x, b.tvel.x) + fx(b.tvel.y, b.tvel.y) + fx(b.tvel.z, b.tvel.z)));
    if (mag > 0x1e0000) {
        const Fixed s = fdiv(0x1e0000, mag);
        b.tvel = {fx(b.tvel.x, s), fx(b.tvel.y, s), fx(b.tvel.z, s)};
    }
    int idx = b.writeIdx;
    for (int k = 0; k < n; ++k) {
        BallEntry* e;
        advance(b, idx, e);
        Ball_collideStep(w, *e);
        e->pos = b.tpos;
        ++b.step;
    }
    b.writeIdx = idx;
    b.freeSlots -= n;
    b.avail += n;
}

void zoneUpdate(World& w, int n) {
    BallState& b = w.ball;
    const int k = b.stamp[4] == -1 ? -1 : Ball_ringIndex(w, b.stamp[4]);
    const Vec3 p = k < 0 ? b.pos : b.ring[k].pos;
    Formation_ballZone(w, p, b.zone, 1);
    if (b.zone[0] == b.prevZone) b.zoneFrames += n;
    else {
        b.zoneFrames = 0;
        b.prevZone = b.zone[0];
    }
}

void rollUpdate(BallState& b, int n, Fixed threshold, Fixed slope, Fixed base, bool wrapHeading) {
    if (b.held != 0 || (b.flags & 0x20)) return;
    const Fixed rate = b.speed >= threshold ? fx(b.speed, slope) + base : fx(b.speed, 0x61c8);
    b.rollAngle += rate * n;
    b.rollAngle = static_cast<Fixed>((static_cast<std::uint32_t>(b.rollAngle) & 0x3ffff));
    b.rollPhase = b.rollAngle >> 16;
    if (b.vel.x || b.vel.z) {
        const Angle a = atan2i(b.vel.x, b.vel.z);
        b.heading = wrapHeading ? (a & 0x3ff) : static_cast<std::int16_t>(a);
    }
}

// 0x47561C: frozen ball (trajFlags & 2): zone, landing and roll only.
void Ball_updateFrozen(World& w, int n) {
    BallState& b = w.ball;
    b.framesThisStep = -1;
    zoneUpdate(w, n);
    b.consumed = n;
    if (n > b.landingFrame) b.landing = b.posI;
    rollUpdate(b, n, 0x20000, 0xeab, 0xc391, true);
}

// 0x454448 (EDX = took possession): team +0x204 possession flags.
void possessionFlags(World& w, int pid, int took) {
    TeamState& t = w.teams[w.players[pid].team];
    TeamState& o = w.teams[t.opponent];
    const BallState& b = w.ball;
    const bool keeper = w.players[pid].slot == 0;
    if (took) {
        o.f204 = 0;
        if (b.prevPossessor < 0) t.f204 = keeper ? 0 : 1; // keeper: writes prevPossessor (0)
        else if (!keeper) t.f204 = 1;
        return;
    }
    if (b.lastKickKind == 4) {
        t.f204 = 0;
        o.f204 = 0;
        return;
    }
    if (keeper && static_cast<std::uint32_t>(w.players[pid].stateTimer) > 0x3c) t.f204 = 1;
}

// 0x4BC990: clear player +0x244 marks; offside check when the opponent's +0x1BC is set.
void possessionMarks(World& w, int pid) {
    TeamState& t = w.teams[w.players[pid].team];
    TeamState& o = w.teams[t.opponent];
    if (o.flags & 0x80)
        for (int i = 0; i < o.playerCount; ++i) w.players[o.players[i]].f244 = 0;
    if (w.players[pid].f244 == 0) return;
    if (o.f1bc >= 0 && Rules_isOffsidePosition(w, pid, o.f1bc) != 0) { // EDX = opponent +0x1BC
        Rules_registerFoul(w, 3, pid, -1, &w.players[pid].posI, 0);
        Match_dispatchEvent(w, 8, o.index);
    }
    for (int i = 0; i < t.playerCount; ++i) w.players[t.players[i]].f244 = 0;
}

} // namespace

// Gate-exported entry points into the file-local collision/net-contact helpers (0x4832EC,
// 0x482AB0): tests/unit/test_sim_scenarios.cpp scenarioSpinNetSurface drives the net-surface
// case table directly; behavior of the helpers themselves is untouched.
void Test_ballResolveCollision(World& w, BallEntry& e, int kind, const Vec3& p) {
    Ball_resolveCollision(w, e, kind, p); // FUN_004c09a0 (WC98)
}
void Test_netContact(World& w) { netContact(w); } // 0x482AB0

// ---------------------------------------------------------------- public

void Ball_setSurface(World& w, int col) {
    BallState& b = w.ball;
    const int i = w.opt.surfaceRow * 5 + col;
    b.restitution = kRestitution[i];
    b.bounceRetain = kBounceRetain[i];
    b.rollFriction = kRollFriction[i];
    Flight_selectSet(w, w.opt.surfaceRow == 0 ? 1 : col);
    Ball_buildLobTable(w);
}

void Ball_init(World& w) {
    BallState& b = w.ball;
    b.kickCount = 0;
    b.held = 0;
    b.possessor = b.prevPossessor = b.prevPossessor2 = -1;
    b.flags &= ~0x20u;
    b.framesThisStep = -1;
    b.radius = 5;
    Ball_setSurface(w, w.opt.surfaceColumn);
}

void Ball_buildNetBounds(World& w) {
    BallState& b = w.ball;
    const Pitch& p = w.pitch;
    const Fixed r16 = sh(b.radius);
    Fixed ex = r16, eb = r16;
    if (p.f34c != p.f348) {
        std::int32_t a = Math_atan2_16((p.barThickness >> 1) + p.goalHeight, p.f34c - p.f348);
        a = a / 2;
        Fixed s, c;
        Math_sinCos16(a, &s, &c);
        const Fixed tn = fdiv(s, c);
        eb = fx(r16, static_cast<Fixed>(0x100000000LL / tn));
        ex = fx(r16, tn);
    }
    const Fixed top = sh(p.halfLength + p.f34c), bot = sh(p.halfLength + p.f348);
    b.net[3] = top + ex; // 0x66BA38
    b.net[1] = top - eb; // 0x66BA30
    b.net[2] = bot - ex; // 0x66BA34
    b.net[0] = bot + ex; // 0x66BA2C
}

void Ball_placeAt(World& w, const Vec3& in, bool fixed16, int keepDribble) {
    BallState& b = w.ball;
    const Vec3 p = in;
    b.spin = 0;
    b.framesThisStep = -1;
    b.f04[0] = b.f04[1] = b.f04[2] = 0;
    b.vel = {};
    b.speed = 0;
    b.f14[0] = b.f14[1] = b.f14[2] = 0;
    b.lastToucher = -1;
    b.consumed = 0;
    b.apexHeight = b.landingFrame = b.cross142Frame = b.cross85Frame = -1;
    b.flags = (b.flags & ~0xffu) | (b.flags & 0x61);
    b.bounces = 0;
    b.timer34 = 0;
    b.outKind = 0;
    b.timer38 = 0;
    b.netKind = 0;
    b.flags |= 0x10;
    if (!fixed16) {
        b.cross85Pos = b.cross142Pos = b.landing = b.posI_d30 = b.posI = p;
        b.pos = {sh(p.x), sh(p.y), sh(p.z)};
    } else {
        b.posI = shr16(p);
        b.cross85Pos = b.cross142Pos = b.landing = b.posI_d30 = b.posI;
        b.pos = p;
    }
    b.framePos16 = b.pos;
    Formation_ballZone(w, b.pos, b.zone, 0);
    b.prevZone = b.zone[0];
    if (keepDribble == 0) {
        b.dribbler = -1;
        b.flags &= ~0x40u;
    }
    Ball_kick(w, 0, 0, 0, 0, 0);
}

void Ball_resetForPeriod(World& w) {
    BallState& b = w.ball;
    b.possessor = b.prevPossessor = b.prevPossessor2 = -1;
    const int gm = w.opt.gameMode;
    if (gm != 1 && gm != 3 && w.match.periodType == 3) {
        Ball_placeAt(w, Vec3{0, 0, 0}, false, 0); // centre spot 0x65F38C
    } else if (w.match.periodType == 3) {
        Match_lineUpRestart(w, 6);
        Ball_placeAt(w, Vec3(b.restartPos), false, 0);
    } else {
        Ball_placeAt(w, Vec3(b.posI), false, 0);
    }
    // 0x65F2B4 = &posI (camera focus, presentation).
    b.prevPosI = b.posI;
    b.posAtTouch = b.posI;
    b.kickerByTeam[0] = b.kickerByTeam[1] = -1;
    b.rollAngle = 0;
    b.flags &= ~4u;
    b.spin = 0;
}

void Ball_kick(World& w, Fixed vx, Fixed vy, Fixed vz, Fixed spin, int frames) {
    BallState& b = w.ball;
    ++b.kickCount;
    b.kickPos = b.pos;
    b.tpos = b.kickPos;
    b.vel = b.kickVel = b.tvel = {vx, vy, vz};
    b.spin = b.kickSpin = b.tspin = spin;
    b.step = 0;
    b.netCase = 0;
    b.contactMode = 0;
    b.apexCount = b.cross142Count = b.cross85Count = b.bounceCount = 0; // crossCount is not reset
    b.readIdx = 0;
    b.freeSlots = kRingSize;
    b.avail = 0;
    b.writeIdx = -1;
    for (auto& s : b.stamp) s = -1;
    b.kickTime = w.time.totalFrames;
    b.trajFlags &= ~2u;
    if ((vx | vy | vz) == 0) {
        b.trajFlags |= 1;
        return;
    }
    b.trajFlags &= ~1u;
    // The exe stamps the kicked ring entry with a touch flag (0x600010 bits); reading it back in
    // FUN_00476D40 sets ball.flags bit 0 ("in play") + posAtTouch. Our ring writer never emits
    // those bits, so mark the kick here: a kicked ball is live, and Rules_checkBallOut /
    // throw-in geometry consume these fields.
    if (w.match.phase == 9 || w.match.phase == 5 || w.match.periodType == 3) {
        b.flags |= 1u;
        const Vec3 kp = shr16(b.kickPos);
        b.posAtTouch = {kp.x, kp.y, kp.z + (kp.z > 0 ? 1 : -1)};
    }
    int n = 0xb4;
    if (frames == -1) n = 0x3c;
    else if (frames <= 0xb4) n = frames;
    Ball_integrateTrajectory(w, n);
}

void Ball_integrateTrajectory(World& w, int n) {
    BallState& b = w.ball;
    if (static_cast<std::uint32_t>(n) > static_cast<std::uint32_t>(b.freeSlots)) n = b.freeSlots;
    int idx = b.writeIdx;
    int k = 0;
    for (; k < n; ++k) {
        BallEntry* e;
        advance(b, idx, e);
        Ball_collideStep(w, *e);
        const Vec3 old = b.tvel;
        if (b.contactMode == 2) {
            netContact(w);
        } else if (b.contactMode == 1) {
            if (--b.contactTimer <= 0) {
                b.contactMode = 0;
                b.netCase = 0;
            }
        }
        Vec3& v = b.tvel;
        if (b.tpos.y == 0 && v.y == 0) { // on the ground
            if (v.x == 0 && v.z == 0) {
                b.stamp[2] = static_cast<std::int16_t>(b.step);
                if (b.contactMode == 0) b.trajFlags |= 1;
            } else {
                e->flags |= 0x04000000;
                stampOnce(b, 11);
                v.x = fx(v.x, b.rollFriction);
                v.z = fx(v.z, b.rollFriction);
                if (iabs(v.x) < 0x1999 && iabs(v.z) < 0x1999 &&
                    static_cast<std::int32_t>(static_cast<std::uint32_t>(v.z * v.z) + static_cast<std::uint32_t>(v.x * v.x)) < 0x28f3d71)
                    v.x = v.z = 0;
            }
        } else { // airborne
            v.x -= fx(0xcb, v.x); // drag rows measured in FUN_004b2a40 companion lane (WC98; was FIFA-99 0xeb)
            v.y -= fx(0xcb, v.y);
            v.z -= fx(0xcb, v.z);
            if (b.tspin) {
                if (w.opt.funModes & 4) {
                    v.x += fx(fx(b.tspin, v.z), 0x4e200);
                    v.z -= fx(fx(b.tspin, v.x), 0x4e200);
                    if ((w.rng.next() & 0xf) == 0) b.tspin = -b.tspin;
                } else {
                    v.x += fx(fx(b.tspin, v.z), 0x2710);
                    v.z -= fx(fx(b.tspin, v.x), 0x2710);
                }
                b.tspin = fx(b.tspin, 0xf9db);
            }
            v.y -= 0x2468; // WC98 gravity row (FUN_004b2a40 companion -0x2468 + collidestep bounce pair; was FIFA-99 0x2f5c)
        }
        // first velocity reversal stamps (x then z)
        if ((old.x > 0 && v.x <= 0) || (old.x < 0 && v.x >= 0)) b.stamp[0] = static_cast<std::int16_t>(b.step);
        if ((old.z > 0 && v.z <= 0) || (old.z < 0 && v.z >= 0)) b.stamp[1] = static_cast<std::int16_t>(b.step);
        if (v.y <= 0) {
            e->flags |= 0x200;
            if (old.y > 0) {
                e->flags |= 0x02000000;
                if (b.apexCount < 12) {
                    if (b.apexCount == 0) b.apexHeight = b.tpos.y >> 16;
                    b.apexAt[b.apexCount++] = static_cast<std::int16_t>(b.step);
                }
            }
        }
        e->pos = b.tpos;
        ++b.step;
    }
    b.writeIdx = idx;
    b.freeSlots -= k;
    b.avail += k;
}

void Ball_consumeTrajectory(World& w, int n) {
    BallState& b = w.ball;
    b.framesThisStep = n >= 6 ? 6 : n;
    for (int i = 0; i < n; ++i) {
        b.prevPos = b.pos;
        b.prevPosI = shr16(b.prevPos);
        b.pos = b.ring[b.readIdx].pos;
        b.posI = shr16(b.pos);
        if (i < 6) b.posHist[i] = b.posI;
        b.vel = {b.pos.x - b.prevPos.x, b.pos.y - b.prevPos.y, b.pos.z - b.prevPos.z};
        b.speed = octLength(b.vel.x, b.vel.z);
        if ((b.ring[b.readIdx].flags & 0xffb000) == 0) {
            if (w.teams[0].f1e4 >= 0) Keeper_reactToBall(w, w.teams[0].f1e4);
            if (w.teams[1].f1e4 >= 0) Keeper_reactToBall(w, w.teams[1].f1e4);
        }
        const std::uint32_t f = b.lastFlags = b.ring[b.readIdx].flags;
        if (f & 0x600010) {
            const int ph = w.match.phase;
            if (ph == 9 || ph == 5 || w.match.periodType == 3) {
                w.teams[0].f1e0 = w.teams[1].f1e0 = -1;
                b.flags |= 1;
                b.posAtTouch.x = b.posI.x;
                b.flags &= ~0x20u;
                b.posAtTouch.z = b.posI.z + (b.posI.z > 0 ? 1 : -1);
                b.posAtTouch.y = b.posI.y;
                if (b.stamp[6] != -1) b.flags |= 2;
                // else (no goal flag yet): FUN_004D4624 commentary (presentation, skipped).
            }
        } else if (b.flags & 1) {
            b.flags &= ~1u;
        }
        const std::uint32_t net = (f >> 16) & 0xff;
        if (net) {
            int k = 0;
            while (!(net & (1u << k))) ++k;
            b.netKind = k + 1;
            b.dribbler = -1;
            Ball_releaseFromPossessor(w, b.possessor);
            b.outKind = (f & 0x01000000) ? 2 : 1;
        } else {
            b.outKind = 0;
            b.netKind = 0;
        }
        const std::uint32_t f1 = (f >> 8) & 0xff;
        if (f1 & 0x30) { // post or bar
            const bool goalAhead = b.stamp[6] != -1 && Ball_ringIndex(w, b.stamp[6] - b.consumed) >= 0;
            if (!goalAhead && w.match.periodType != 3 && b.possessor < 0) {
                const int def = b.posI.z > 0 ? (w.teams[0].side != 1 ? 1 : 0) : (w.teams[0].side == 0 ? 0 : 1);
                const int lt = b.lastToucher;
                if (lt >= 0 && w.teams[w.players[lt].team].index != def && !(f1 & 0x80))
                    ++w.match.stats[def == 0 ? 1 : 0].shotsWide; // 0x65E204 + t*0x60C
            }
            // Post/bar commentary + thud sound (0x4D930C / 0x4D9564 / 0x4A2818 / [0x581BB0]): skipped.
        }
        if (f1 & 0x80) { // fun-mode wall
            b.dribbler = -1;
            b.flags |= 8;
            // sound 0x4A2818 skipped; the remaining branch only computes an unused local.
        }
        if (f & 0x40) { // bounce
            if (b.possessor >= 0 && w.players[b.possessor].distBall < 0x40) {
                b.vel.y >>= 2;
                Ball_kick(w, b.vel.x, b.vel.y, b.vel.z, b.spin, -1);
            } else {
                // bounce sound 0x4A2ED4 skipped.
                if (b.apexCount > 0) {
                    --b.apexCount;
                    for (int j = 0; j < b.apexCount; ++j) b.apexAt[j] = b.apexAt[j + 1];
                    const int k = Ball_ringIndex(w, b.apexAt[0]);
                    if (k >= 0) b.apexHeight = b.ring[k].pos.y >> 16;
                }
                if (b.bounceCount > 0) {
                    --b.bounceCount;
                    for (int j = 0; j < b.bounceCount; ++j) b.bounceAt[j] = b.bounceAt[j + 1];
                    const int k = Ball_ringIndex(w, b.bounceAt[0]);
                    if (k >= 0 && !(b.flags & 0x80)) b.landing = {b.ring[k].pos.x >> 16, 0, b.ring[k].pos.z >> 16};
                }
            }
            ++b.bounces;
        }
        if (f & 0x04000000) b.apexHeight = 0;
        if (++b.readIdx == kRingSize) b.readIdx = 0;
        if (b.avail > 0) {
            ++b.freeSlots;
            --b.avail;
        } else {
            b.writeIdx = -1;
            b.readIdx = 0;
            b.freeSlots = kRingSize;
            b.avail = 0;
        }
    }
    zoneUpdate(w, n);
    b.consumed += n;
    if (b.consumed > b.landingFrame) b.landing = b.posI;
    rollUpdate(b, n, 0x8000, 0x46e, 0x30e4, false);
}

void Ball_update(World& w) {
    BallState& b = w.ball;
    const int n = w.time.simFrames;
    b.framePos16 = b.pos;
    if ((b.flags & 0x40) && b.outKind == 0) {
        b.posI = b.prevPosI;
        Ball_kick(w, 0, 0, 0, 0, 0);
        Ball_moveToward(w, n);
        Ball_consumeTrajectory(w, n);
        b.flags &= ~0x40u;
    } else if (b.trajFlags & 2) {
        Ball_updateFrozen(w, n);
    } else {
        Ball_integrateTrajectory(w, n * 8);
        Ball_consumeTrajectory(w, n);
    }
    if (b.timer34 != 0) {
        b.timer34 = static_cast<std::uint32_t>(b.timer34) > static_cast<std::uint32_t>(n) ? b.timer34 - n : 0;
    } else {
        b.velCopy = b.vel;
    }
    if (b.timer38 == 0) {
        b.posI_d78 = b.posI;
        return;
    }
    b.timer38 = static_cast<std::uint32_t>(b.timer38) > static_cast<std::uint32_t>(n) ? b.timer38 - n : 0;
}

void Ball_takePossession(World& w, int pid, int edx) {
    BallState& b = w.ball;
    b.flags = (b.flags & ~0x20u) | (static_cast<std::uint32_t>(edx & 1) << 5);
    b.flags &= ~8u;
    if (pid == b.possessor) return; // + commentary 0x505084 (skipped)
    b.possTime = w.time.totalFrames;
    possessionMarks(w, pid);
    w.match.f26c = 0;
    const int old = b.possessor;
    if (old < 0 && pid == b.prevPossessor) {
        b.prevPossessor = b.prevPossessor2;
    } else if (old >= 0) {
        b.prevPossessor2 = b.prevPossessor;
        b.prevPossessor = old;
        if (w.players[old].slot == 0 && old != w.teams[w.players[old].team].f1b8) Player_onBallLost(w, old);
        if (old == b.dribbler) b.dribbler = -1;
    }
    b.possessor = pid;
    w.match.player2a0 = -1;
    w.match.restartTaker = -1;
    w.match.penaltyTaker = -1;
    if (b.prevPossessor < 0) w.time.possessionFrames = 0;
    w.time.possessorSlowFrames = 0;
    TeamState& t = w.teams[w.players[pid].team];
    t.possWinner = pid;
    const bool clearOwn = t.oneTouch < 0 || pid == t.f1e0 || w.players[pid].slot == 0; // 0x47524F / 0x475314
    if (clearOwn) t.f1e0 = -1;
    TeamState& o = w.teams[t.opponent];
    o.f1e0 = -1;
    o.f1b8 = -1;
    if (pid == t.f1b8) t.f1b8 = -1;
    possessionFlags(w, pid, 1);
    // commentary 0x505188 skipped.
    w.emit(EventType::Possession, t.index, pid, pid, old);
}

void Ball_releaseFromPossessor(World& w, int pid) {
    BallState& b = w.ball;
    if (pid < 0 || pid != b.possessor) return;
    w.match.f26c = 0;
    b.prevPossessor2 = b.prevPossessor;
    b.prevPossessor = pid;
    b.possessor = -1;
    w.time.possessionFrames = 0;
    b.flags &= ~0x20u;
    TeamState& t = w.teams[w.players[pid].team];
    if (w.players[pid].slot == 0 && pid != t.f1b8) Player_onBallLost(w, pid);
    if (pid == b.dribbler) b.dribbler = -1;
    if (pid != t.f1b8) t.f1b8 = -1;
    possessionFlags(w, pid, 0);
}

void Ball_setDribblePosition(World& w, const Vec3& p) {
    BallState& b = w.ball;
    if (b.possessor < 0 || b.possessor != w.pl.current || b.possessor != b.dribbler) return;
    b.prevPosI = b.posI;
    b.posI = p;
    b.dribbleTarget = p;
    b.flags |= 0x40;
}

int Ball_ringIndex(const World& w, int k) {
    const BallState& b = w.ball;
    if (k > b.step) return -1;
    k += b.readIdx;
    if (k >= kRingSize) k -= kRingSize;
    return (k >= kRingSize || k < 0) ? -1 : k;
}

bool Ball_forecast(const World& w, int frame, Vec3* pos16, int* rel, std::uint32_t* flags) {
    if (frame == -1) return false;
    *rel = frame - w.ball.consumed;
    if (*rel < 0) return false;
    const int i = frame % kRingSize;
    if (i < 0) return false;
    *pos16 = w.ball.ring[i].pos;
    *flags = w.ball.ring[i].flags;
    return true;
}

bool Ball_forecastInt(const World& w, int frame, Vec3* posI, int* rel, std::uint32_t* flags) {
    Vec3 p;
    if (!Ball_forecast(w, frame, &p, rel, flags)) return false;
    *posI = shr16(p);
    return true;
}

bool Ball_markerInForecast(const World& w, int k) {
    const int m = w.ball.stamp[6 + 2 * k];
    return m != -1 && Ball_ringIndex(w, m - w.ball.consumed) >= 0;
}

// ---- prediction readers 0x476BD0..0x47701C ----

namespace {
// Extend the precompute when k frames ahead are not available yet (EDX != 0).
bool ensureAhead(World& w, int k, int extend) {
    BallState& b = w.ball;
    if (static_cast<std::uint32_t>(k) <= static_cast<std::uint32_t>(b.avail)) return true;
    if (k > kRingSize || !extend) return false;
    Ball_integrateTrajectory(w, k - b.avail);
    return true;
}
const BallEntry& ahead(const World& w, int k) { return w.ball.ring[(w.ball.readIdx + k) % kRingSize]; }

// Limit k to the stopped-ball window (0x476E6C prologue).
int limitK(const World& w, int k) {
    const BallState& b = w.ball;
    const bool stopped = b.stamp[2] != -1 && Ball_ringIndex(w, b.stamp[2] - b.consumed) >= 0;
    const bool rest = b.vel.x == 0 && b.vel.y == 0 && b.vel.z == 0 && b.posI.y == 0;
    if (!stopped && !rest) return k;
    const std::uint32_t lim = static_cast<std::uint32_t>(b.step - (w.time.totalFrames - b.kickTime) - 1);
    return lim >= static_cast<std::uint32_t>(k) ? k : static_cast<int>(lim);
}
} // namespace

int Ball_predictVelRaw(World& w, int k, int extend, Vec3* out) { // 0x476BD0
    BallState& b = w.ball;
    if (k == 0) { *out = b.vel; return 2; }
    if (k < 0) return 0;
    if (!ensureAhead(w, k, extend)) return 0;
    const Vec3 p = ahead(w, k - 1).pos;
    const Vec3 q = k > 1 ? ahead(w, k - 2).pos : b.pos;
    *out = {p.x - q.x, p.y - q.y, p.z - q.z};
    return 2;
}

int Ball_predictPosRaw(World& w, int k, int extend, Vec3* out) { // 0x476CE0
    BallState& b = w.ball;
    if (k <= 0) {
        *out = shr16(b.pos);
        return k == 0 ? 2 : 0;
    }
    if (!ensureAhead(w, k, extend)) {
        *out = shr16(b.pos);
        return 0;
    }
    *out = shr16(ahead(w, k - 1).pos);
    return 2;
}

int Ball_predictVyRaw(World& w, int k, int extend, Fixed* out) { // 0x476DAC
    BallState& b = w.ball;
    if (k == 0) return 2;
    if (k < 0) return 0;
    if (!ensureAhead(w, k, extend)) return 0;
    const Fixed y1 = ahead(w, k - 1).pos.y;
    const Fixed dy = y1 - (k > 1 ? ahead(w, k - 2).pos.y : b.pos.y);
    *out = Flight_heightAt(y1, Flight_timeFromVy(dy));
    return 2;
}

int Ball_predictPos(World& w, int k, int extend, Vec3* out) { return Ball_predictPosRaw(w, limitK(w, k), extend, out); } // 0x476E6C
int Ball_predictVel(World& w, int k, int extend, Vec3* out) { return Ball_predictVelRaw(w, limitK(w, k), extend, out); } // 0x476EFC
int Ball_predictVy(World& w, int k, int extend, Fixed* out) { return Ball_predictVyRaw(w, limitK(w, k), extend, out); } // 0x476F8C

int Ball_firstStampAfter(const std::int16_t* arr, int count, int after) { // 0x47701C
    for (int i = 0; i < count; ++i)
        if (arr[i] > after) return arr[i];
    return -1;
}

} // namespace wc98::sim
