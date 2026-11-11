// Kick velocity solvers and their math (FIFAWC.EXE): closed-form drag/gravity helpers
// FUN_0051e530..FUN_0051ecbb (pass-16kick dossier analysis/pass16kick/DECOMPILE + mathband_true.txt),
// the flight-time / spin-drift / lob tables, the kick-mode wrappers 0x4B95E8..0x4BA0A4 and the math
// helpers they share. Everything is integer 16.16 exactly like the exe (no x87). Docs: SIMULATION.md §8.
#include "wc98/sim/world.hpp"

#include "ball_tables.inc"

namespace wc98::sim {

using namespace balltab;

namespace {

// WC98 closed-form solver pool DAT_005913e8..0x59143c (kKickPool, ball_tables.inc); named slots per
// consumer site in mathband_true.txt (FUN_0051e690/e770/e920/e980/e9e0/ea30/ea70/eac0/eb50/ec40).
constexpr Fixed Pool(int idx) { return kKickPool[idx]; }
constexpr int kLamScale = 0, kFxOne = 1, kDragTime = 2, kVyApex = 3, kGravT = 4;
constexpr int kDyTfy = 6, kRootsYDiff = 7, kApexDy = 8, kOutScale = 9, kVtYDiff = 10;
constexpr int kVtTime = 12, kLamArg = 13, kRootsVyA = 14, kRootsVyB = 16, kApexPScale = 17;
constexpr int kRollFrames = 18, kDistScale = 20, kRollLogScale = 21;
// u16 lambert tables DAT_00590be8 / DAT_00590fe8 (kLambertA/kLambertB).
std::int32_t tblA(int i) { return static_cast<std::int32_t>(kLambertA[i]); }
std::int32_t tblB(int i) { return static_cast<std::int32_t>(kLambertB[i]); }
// Row d of the exp table DAT_00590a20 (rows of 31 dwords, stride 0x7c).
Fixed expRow(int idx) { return static_cast<Fixed>(kExpTable[idx]); }

// FUN_0051e560 reciprocal: EDX=1, EAX=0, idiv -> 0x100000000 / x (mathband_true.txt:28).
Fixed recip32(Fixed x) { return static_cast<Fixed>(0x100000000LL / x); }
Fixed sinA(Angle a) { return sinCos(a).s; }
Fixed cosA(Angle a) { return sinCos(a).c; }
// FUN_0051e690 (via the e6f0/e740 lerp tails): lambert-B table interpolation over DAT_00590fe8;
// row bases subtracted in-function are 0x2b333 for rows <=0xFF, else 0x1fae1; 0 outside the domain.
Fixed lambertB(std::int32_t x) {
    if (x > 0) return 0;
    const Fixed v = fx(Pool(kLamScale), -x);
    const std::int32_t i = v >> 16, fr = v & 0xffff;
    const std::int32_t i0 = i - 0x201, i1 = i - 0x200;
    if (i1 > 0x1ff || i0 < 0) return 0;
    const Fixed a = fx(tblB(i0) - (i0 > 0xff ? 0x1fae1 : 0x2b333), 0x10000 - fr);
    return a + fx(tblB(i1) - (i1 > 0xff ? 0x1fae1 : 0x2b333), fr);
}
// FUN_0051e770: both lambert branches (A rows over DAT_00590be8, base 0x10000; B as above),
// clamped indices; false when x > 0.
bool lambertAB(std::int32_t x, Fixed* o0, Fixed* o1) {
    if (x > 0) return false;
    const Fixed v = fx(Pool(kLamScale), -x);
    const std::int32_t i = v >> 16, fr = v & 0xffff, w0 = 0x10000 - fr;
    std::int32_t i0 = i - 0x201, i1 = i - 0x200;
    if (i1 > 0x1ff) {
        i1 = 0x1ff;
        if (i0 > 0x1ff) i0 = 0x1ff;
    } else if (i0 < 0) {
        i0 = 0;
        if (i1 < 0) i1 = 0;
    }
    *o0 = fx(tblA(i0) - 0x10000, w0) + fx(tblA(i1) - 0x10000, fr);
    *o1 = fx(tblB(i0) - (i0 > 0xff ? 0x1fae1 : 0x2b333), w0) + fx(tblB(i1) - (i1 > 0xff ? 0x1fae1 : 0x2b333), fr);
    return true;
}

} // namespace

// ---------------------------------------------------------------- math

std::int32_t Math_length(std::int32_t x, std::int32_t z) {
    std::int32_t t = atan2i(z, x); // EAX = z, EDX = x
    if (t <= 0) t = -t;
    std::int32_t a = t > 0x100 ? 0x200 - t : t;
    if (a > 0x80) {
        const std::int32_t v = static_cast<std::int32_t>(static_cast<std::uint32_t>(iabs(z)) << 15);
        return v / (sinA(a) >> 1);
    }
    const std::int32_t v = static_cast<std::int32_t>(static_cast<std::uint32_t>(iabs(x)) << 15);
    return v / (cosA(a) >> 1);
}

OctDist Math_vecTo(const Vec3& from, const Vec3& to) {
    const std::int32_t dx = to.x - from.x, dz = to.z - from.z;
    return {Math_length(dx, dz), dx, dz};
}

Fixed Math_sqrtFx(std::uint32_t v) {
    if (v == 0) return 0;
    const int c = 31 - __builtin_clz(v); // bsr
    return static_cast<Fixed>(static_cast<std::uint32_t>((static_cast<std::uint64_t>(v) * kSqrtSlope[c]) >> 16) + kSqrtBase[c]);
}

std::uint32_t Math_isqrt(std::uint32_t x) {
    const auto T = [](std::uint32_t i) { return static_cast<std::uint32_t>(kIsqrt[i]); };
    if (x >> 16) {
        std::uint32_t lo, hi;
        if (x >> 24) {
            const std::uint32_t t = x >> 24;
            lo = T(t - 1) << 8;
            hi = (T(t) << 8) + 0x100;
        } else {
            const std::uint32_t t = x >> 16;
            hi = (T(t) << 4) + 0x10;
            lo = T(t - 1) << 4;
        }
        while (hi - lo > 1) {
            const std::uint32_t mid = (lo + hi) >> 1;
            if (mid * mid > x) hi = mid;
            else lo = mid;
        }
        return lo;
    }
    const std::uint32_t ah = (x >> 8) & 0xff;
    std::uint32_t lo, hi;
    if (ah) {
        if (ah & 0xf0) {
            if (ah & 0xc0) { const std::uint32_t t = x >> 8; lo = T(t - 1); hi = T(t); }
            else { const std::uint32_t t = x >> 6; lo = T(t - 1) >> 1; hi = T(t) >> 1; }
        } else if (ah & 0x0c) { const std::uint32_t t = x >> 4; lo = T(t - 1) >> 2; hi = T(t) >> 2; }
        else { const std::uint32_t t = x >> 2; lo = T(t - 1) >> 3; hi = T(t) >> 3; }
        hi += 1;
        if (hi - lo <= 1) return lo;
        const std::uint32_t mid = (hi + lo) >> 1; // one bisection step only (0x4858C6)
        return mid * mid > x ? lo : mid;
    }
    return x ? T(x - 1) >> 4 : 0;
}

std::int32_t Math_atan2_16(std::int32_t x, std::int32_t z) {
    int f = 0;
    std::uint32_t ux = static_cast<std::uint32_t>(x), uz = static_cast<std::uint32_t>(z);
    if (z < 0) { f = 2; uz = 0u - uz; }
    if (x < 0) { ux = 0u - ux; f |= 4; }
    std::int32_t t;
    if (ux == uz) {
        t = 0x2000;
    } else {
        std::uint32_t mn = ux, mx = uz;
        if (uz < ux) { f |= 1; mn = uz; mx = ux; }
        const std::uint32_t q = static_cast<std::uint32_t>((static_cast<std::uint64_t>(mn) << 32) / mx);
        const std::uint32_t i = q >> 24, fr = (q >> 8) & 0xffff;
        const std::int32_t a = kAtan16[i];
        t = a + static_cast<std::int32_t>(static_cast<std::uint32_t>((kAtan16[i + 1] - a) * static_cast<std::int32_t>(fr)) >> 16);
    }
    switch (f) { // jump table 0x4619F0
    case 0: return t;
    case 1: return 0x4000 - t;
    case 2: return 0x8000 - t;
    case 3: return t + 0x4000;
    case 4: return -t;
    case 5: return t - 0x4000;
    case 6: return t - 0x8000;
    default: return -t - 0x4000;
    }
}

Fixed Math_sin16(std::int32_t a) {
    const std::int32_t f = (a & 0x3f) * 0x6487e;
    const SinCos sc = sinCos(a >> 6);
    return sc.s + (((sc.c >> 2) * (f >> 9)) >> 21);
}

Fixed Math_cos16(std::int32_t a) {
    const std::int32_t f = (a & 0x3f) * 0x6487e;
    const SinCos sc = sinCos(a >> 6);
    return sc.c + (((-sc.s) >> 2) * (f >> 9) >> 21);
}

void Math_sinCos16(std::int32_t a, Fixed* s, Fixed* c) {
    const SinCos sc = sinCos(a >> 6);
    const std::int32_t f = ((a & 0x3f) * 0x6487e) >> 9;
    *s = sc.s + (((sc.c >> 2) * f) >> 21);
    *c = sc.c - (((sc.s >> 2) * f) >> 21);
}

// ---------------------------------------------------------------- closed-form helpers

// FUN_0051e530 exp digit walk over DAT_00590a20 rows; negation path guarded by cmp 0xFFF59A51
// (51e57b) and reciprocal.
Fixed Flight_dragPow(std::int32_t x) {
    if (x == 0) return 0x10000;
    bool neg = false;
    if (x < 0) {
        if (x < -0xa65af) return 0; // FUN_0051e530 guard at 0x51e57b: cmp 0xFFF59A51
        neg = true;
        x = -x;
    }
    Fixed acc = 0;
    int row = 0;
    while (x) {
        const int d = x & 31;
        x >>= 5;
        if (d) acc = acc ? fx(acc, expRow(row + d)) : expRow(row + d);
        row += 31;
    }
    return neg ? recip32(acc) : acc;
}

// FUN_0051e5b0 binary search over the same rows, DAT_00590b94 down to DAT_00590a20 (rows 3..0).
Fixed Flight_dragLog(std::int32_t x) {
    bool neg = false;
    std::int32_t v = x;
    if (x < 0x10000) { v = recip32(x); neg = true; }
    std::int32_t acc = 0;
    int hi = 0x14;
    for (int row = 93; row >= 0; row -= 31) { // DAT_00590b94 down to DAT_00590a20
        int lo = 0;
        while (lo != hi) {
            int mid = (lo + hi) >> 1;
            if (mid == lo) mid = lo + 1;
            if (v < expRow(row + mid)) hi = mid - 1;
            else lo = mid;
        }
        acc <<= 5;
        if (hi) {
            acc |= lo;
            v = fdiv(v, expRow(row + lo));
        }
        hi = 31;
    }
    return neg ? -acc : acc;
}

// FUN_0051e920: frames a roll started at `speed` needs to cover `dist` (EAX = dist, EDX = speed):
// dragLog(fx(C[20], dist) >> 2 / speed + fxOne) * -C[21].
Fixed Flight_rollTime(Fixed dist, Fixed speed) {
    Fixed v = fdiv(fx(Pool(kDistScale), dist) >> 2, speed) + Pool(kFxOne);
    return -fx(Flight_dragLog(v), Pool(kRollLogScale));
}

// FUN_0051e980: ground speed a roll over `dist` in `frames` needs (guard frames < 0x8000).
Fixed Flight_groundSpeedForDistance(Fixed dist, Fixed frames) {
    if (frames < 0x8000) return 0xf0000;
    const Fixed p = Flight_dragPow(fx(Pool(kRollFrames), frames) >> 9);
    return fdiv(fx(dist, Pool(kDistScale)), Pool(kFxOne) - p) >> 2;
}

Fixed Flight_vyForApexTime(Fixed t) { // FUN_0051ea30
    const Fixed p = Flight_dragPow(fx(-Pool(kDragTime), t) >> 10);
    return fx(Pool(kVyApex), Pool(kFxOne) - p);
}

Fixed Flight_vyForTime(Fixed y0, Fixed y1, Fixed t) { // FUN_0051eac0 (guard t < 0x8000 -> 0xf0000)
    if (t < 0x8000) return 0xf0000;
    const Fixed p = Flight_dragPow(fx(Pool(kDragTime), t) >> 10);
    const Fixed den = Pool(kFxOne) - p;
    const Fixed num = (fx(Pool(kVtYDiff), y1 - y0) >> 12) - (fx(Pool(kVtTime), t) >> 8);
    return fdiv(num, den) + Pool(kVyApex);
}

Fixed Flight_timeFromVy(Fixed dy) { // FUN_0051e9e0: frames from vy to the apex
    const Fixed b = Pool(kFxOne) - (fx(dy, Pool(kDyTfy)) >> 15);
    return fx(Flight_dragLog(recip32(b)), Pool(kOutScale));
}

Fixed Flight_heightAt(Fixed y0, Fixed t) { // FUN_0051ea70: height at frame t (y0 = EAX)
    const Fixed p = Flight_dragPow(fx(-Pool(kDragTime), t) >> 10);
    return fx(Pool(kVyApex), t + fx(Pool(kGravT), Pool(kFxOne) - p)) + y0;
}

Fixed Flight_timeForApex(Fixed y0, Fixed apexY) { // FUN_0051ec40 (-1 when unreachable)
    const Fixed dy = apexY - y0;
    const Fixed p = Flight_dragPow(fx(Pool(kApexPScale), dy) >> 15);
    const Fixed r = lambertB(fx(Pool(kLamArg), p) >> 14);
    if (r == 0) return -1;
    return fx(dy, Pool(kApexDy)) + fx(Pool(kOutScale), r) - Pool(kGravT);
}

int Flight_rootsForHeight(Fixed y0, Fixed y1, Fixed vy, Fixed* outA, Fixed* outB) { // FUN_0051eb50
    const Fixed e = (fx(Pool(kRootsYDiff), y1 - y0) - fx(vy, Pool(kRootsVyB))) >> 6;
    const Fixed p = Flight_dragPow(fx(e, Pool(kDragTime)) >> 10);
    Fixed r0, r1;
    if (!lambertAB(fx(fx(vy, Pool(kRootsVyA)) + Pool(kLamArg), p) >> 14, &r0, &r1)) return 0;
    r0 = fx(r0, Pool(kOutScale));
    r1 = fx(r1, Pool(kOutScale));
    const Fixed base = Pool(kGravT) + e;
    *outA = base - r0;
    *outB = base - r1;
    return 1;
}

// ---------------------------------------------------------------- tables

void Ball_buildFlightTimeTable(World& w) {
    Fixed vx = 0x10000, vz = 0, s = 1000, x = 0, z = 0;
    w.ball.flightTime[0] = 0;
    for (int i = 1; i < 320; ++i) {
        x += vx;
        z += vz;
        vx -= fx(0xeb, vx);
        vz -= fx(0xeb, vz);
        if (s) {
            vx += fx(fx(s, vz), 0x2710);
            vz -= fx(fx(s, vx), 0x2710);
            s = fx(s, 0xf9db);
        }
        const Fixed v = fdiv(0x10000, Math_length(x >> 16, z >> 16) << 16);
        w.ball.flightTime[i] = static_cast<std::uint16_t>(v > 0xffff ? 0xffff : v);
    }
}

void Ball_buildSpinTable(World& w, Fixed drag, Fixed decay, Fixed gain) {
    Fixed vz = 0x40000, s = -1000, vx = 0, x = 0, z = 0;
    w.ball.spinDrift[0] = 0;
    for (int i = 1; i < 320; ++i) {
        x += vx;
        z += vz;
        vx -= fx(drag, vx);
        vz -= fx(drag, vz);
        if (s) {
            vx += fx(fx(s, vz), gain);
            vz -= fx(fx(s, vx), gain);
            s = fx(s, decay);
        }
        w.ball.spinDrift[i] = static_cast<std::uint16_t>(fdiv(Math_atan2_16(x, z), -1000) & 0xffff);
    }
}

void Ball_buildLobTable(World& w) {
    // 0x505F10 (a bare `ret`) is called first.
    Ball_buildFlightTimeTable(w);
    Ball_buildSpinTable(w, 0xeb, 0xf9db, 0x2710);
    Ball_buildNetBounds(w);
    std::int32_t* L = w.ball.lob;
    for (int r = 0; r < 12; ++r) L[r * 4] = kLobInit[r * 4]; // static vy column; the rest is rebuilt
    for (int r = 0; r < 12; ++r) {
        Ball_placeAt(w, Vec3{0, 0, 0}, false, 0); // 0x4B9560 = (0, 0, 0)
        Ball_kick(w, 0x10000, L[r * 4], 0, 0, 0xb4);
        L[r * 4 + 2] = w.ball.stamp[11] + 7; // first roll + 7 (dword 0x669078 >> 16)
        Vec3 p;
        int rel;
        std::uint32_t fl;
        Fixed delta = 0;
        if (Ball_forecast(w, L[r * 4 + 2], &p, &rel, &fl)) {
            L[r * 4 + 3] = p.x;
            Ball_forecast(w, L[r * 4 + 2] + 1, &p, &rel, &fl);
            delta = p.x - L[r * 4 + 3];
        }
        L[r * 4 + 1] = fx(9, fdiv(L[r * 4 + 3], delta)); // `mov eax,9; imul`
    }
}

// ---------------------------------------------------------------- solvers

namespace {
Angle drifted(const World& w, Fixed dx, Fixed dz, Fixed spin, int t) {
    std::int32_t a = Math_atan2_16(dx, dz) - fx(spin, w.ball.spinDrift[t]);
    if (a > 0x10000) a -= 0x10000;
    else if (a < 0) a += 0x10000;
    return a;
}
} // namespace

int Ball_solveGroundPass(const World& /*w*/, const Vec3& from, const Vec3& to, std::int32_t lift, Fixed limit, Vec3& out) {
    const Fixed dx = to.x - from.x, dz = to.z - from.z;
    const std::int32_t dist = Math_length(dx >> 16, dz >> 16);
    int flag = 0;
    Fixed sp = Flight_groundSpeedForDistance(dist << 16, lift << 16);
    if (sp > limit) { flag = 1; sp = 0x230000; }
    const std::int32_t a = Math_atan2_16(dx, dz);
    out.x = fx(sp, Math_sin16(a));
    out.y = 0;
    out.z = fx(sp, Math_cos16(a));
    return flag;
}

int Ball_solveTimedFlight(const World& w, const Vec3& from, const Vec3& to, Fixed spin, int t, Vec3& out) {
    const Fixed dx = to.x - from.x, dz = to.z - from.z;
    const Fixed dist = Math_length(dx >> 16, dz >> 16) << 16;
    if (t >= 0x140 || t < 0) { out = {}; return 1; }
    Fixed h = fx(w.ball.flightTime[t], dist);
    Fixed vy = Flight_vyForTime(from.y, to.y, t << 16);
    int flag = 0;
    if (h > 0x230000) { flag = 1; h = 0x230000; }
    if (vy > 0x190000) { flag |= 1; vy = 0x190000; }
    const Angle a = drifted(w, dx, dz, spin, t);
    out.x = fx(h, Math_sin16(a));
    out.y = vy;
    out.z = fx(h, Math_cos16(a));
    return flag;
}

int Ball_findFlightTime(const World& w, Fixed dist, Fixed speed) {
    const auto* T = w.ball.flightTime;
    const Fixed q = fdiv(speed, dist);
    if (q > T[1]) return 1;
    if (q < T[319]) return 0x13f;
    int lo = 1, hi = 0x13f, best = 0x13f;
    std::int32_t bestErr = 0xfffffff;
    int span;
    do {
        span = hi - lo;
        const int mid = lo + (span >> 1);
        const std::int32_t diff = T[mid] - q;
        if (diff > 0) lo = mid + 1;
        else if (diff < 0) hi = mid - 1;
        else span = 0;
        if (iabs(diff) < iabs(bestErr)) { bestErr = diff; best = mid; }
    } while (span > 0);
    return best;
}

int Ball_solveFlightLowArc(const World& w, const Vec3& from, const Vec3& to, Fixed spin, Fixed speed, Fixed maxVy, Vec3& out) {
    const Fixed dx = to.x - from.x, dz = to.z - from.z;
    const Fixed dist = Math_length(dx >> 16, dz >> 16) << 16;
    const int t = Ball_findFlightTime(w, dist, speed);
    if (t >= 0x140 || t < 0) { out = {}; return 1; }
    const Fixed h = fx(w.ball.flightTime[t], dist);
    int flag = 0;
    if (iabs(speed - h) > 0x8000) flag |= 2;
    Fixed vy = Flight_vyForTime(from.y, to.y, t << 16);
    if (vy > maxVy) { flag |= 1; vy = maxVy; }
    else if (vy < -maxVy) { flag |= 1; vy = -maxVy; }
    const Angle a = drifted(w, dx, dz, spin, t);
    out.x = fx(h, Math_sin16(a));
    out.y = vy;
    out.z = fx(h, Math_cos16(a));
    return flag;
}

// `lift` = apex height (absolute y, 16.16; raised to from.y + 4 when lower): the time to reach
// it gives vy0 (0x506394 -> 0x506190), the two landing-time roots at target height come from
// 0x5062A8 and useHigh picks which one is preferred.
int Ball_solveFlightToTarget(const World& w, const Vec3& from, const Vec3& to, Fixed spin, Fixed lift, int useHigh,
                             Fixed maxH, Fixed maxVy, Vec3& out) {
    const Fixed dx = to.x - from.x, dz = to.z - from.z;
    int flag = 0;
    if (lift < from.y) lift = from.y + 0x40000;
    Fixed t1 = Flight_timeForApex(from.y, lift);
    if (t1 < 0) useHigh = 0;
    Fixed vy = Flight_vyForApexTime(t1);
    Fixed rA = 0, rB = 0; // [esp+0xC], [esp+0x10]
    if (useHigh) {
        if (Flight_rootsForHeight(from.y, to.y, vy, &rA, &rB) && rB < 0 && rA > 0) rB = rA;
    } else {
        if (Flight_rootsForHeight(from.y, to.y, vy, &rB, &rA) && rB < 0 && rA > 0) rB = rA;
    }
    const int t = (rB + 0x8000) >> 16;
    if (t >= 0x140 || t < 0) { out = {}; return 1; }
    const Fixed dist = Math_length(dx >> 16, dz >> 16) << 16;
    Fixed h = fx(w.ball.flightTime[t], dist);
    const Fixed lim = fx(maxH, maxH) + fx(maxVy, maxVy);
    const Fixed mag = fx(vy, vy) + fx(h, h);
    if (mag > lim) {
        const Fixed s = Math_sqrtFx(static_cast<std::uint32_t>(fdiv(lim, mag)));
        h = fx(h, s);
        vy = fx(vy, s);
        flag |= 1;
    }
    if (vy > maxVy) { flag |= 1; vy = maxVy; }
    else if (vy < -maxVy) { flag |= 1; vy = -maxVy; }
    const Angle a = drifted(w, dx, dz, spin, t);
    out.x = fx(h, Math_sin16(a));
    out.y = vy;
    out.z = fx(h, Math_cos16(a));
    return flag;
}

void Ball_solveKick(World& w, Angle a, Fixed vh, Fixed vy, Fixed /*spin*/, std::int32_t out[4], std::int32_t* lift) {
    const Fixed t = Flight_timeFromVy(vy);
    *lift = Flight_heightAt(0, t) >> 16;
    Fixed rA = t, rB = 0;
    Flight_rootsForHeight(0, 0, vy, &rA, &rB);
    const int T = (rA + 0x8000) >> 16;
    const std::int32_t d = fdiv(vh, w.ball.flightTime[T]) >> 16;
    out[0] = d;
    out[1] = fx(d, sinCos(a).s);
    out[2] = fx(d, sinCos(a).c);
    out[3] = a;
}

// ---------------------------------------------------------------- kick wrappers

namespace {
Vec3 shl16(const Vec3& v) { return {v.x << 16, v.y << 16, v.z << 16}; }

// Common tail of every wrapper: clear "possessed", speed, kicker bookkeeping, precompute 60.
void finishKick(World& w, int kicker, int kind) {
    BallState& b = w.ball;
    b.flags &= ~0x20u;
    b.speed = octLength(b.vel.x, b.vel.z);
    if (kicker >= 0) {
        Ball_setKicker(w, kicker, kind);
        b.kickResult = 0;
    }
    Ball_kick(w, b.vel.x, b.vel.y, b.vel.z, b.spin, -1);
}

void placeHere(World& w) { Ball_placeAt(w, Vec3(w.ball.pos), true, 0); }
} // namespace

void Ball_setKicker(World& w, int pid, int kind) {
    BallState& b = w.ball;
    if (pid != b.lastKicker) b.lastKicker = -1;
    TeamState& t = w.teams[w.players[pid].team];
    b.kickerByTeam[t.index] = pid;
    b.lastToucher = pid;
    TeamState& o = w.teams[t.opponent];
    o.f1b8 = -1;
    o.oneTouch = -1;
    if (t.oneTouch != pid && t.oneTouch != t.f1e0) t.oneTouch = -1;
    b.lastKickKind = kind;
}

void Kick_volleyLimits(const World& w, int kicker, Fixed* maxVy, Fixed* maxH) {
    const BallState& b = w.ball;
    const Fixed inc = Math_sqrtFx(static_cast<std::uint32_t>(fx(b.speed, b.speed) + fx(b.vel.y, b.vel.y)));
    Fixed h = ((Player_attr(w, kicker, 0x34) >> 2) << 16) + 0x58000 + fx(inc, 0xa666);
    const int anim = Player_animId(w, kicker);
    if (anim == 0xf3 || anim == 0xf4) // `add word [ebx+2], 5`
        h = static_cast<Fixed>((static_cast<std::uint32_t>(h) & 0xffff) |
                               (static_cast<std::uint32_t>(static_cast<std::uint16_t>((static_cast<std::uint32_t>(h) >> 16) + 5)) << 16));
    *maxH = h;
    Fixed vy = fx(inc, 0x6666) + 0x30000;
    *maxVy = vy > 0xa0000 ? 0xa0000 : vy;
    if (*maxH > 0x178000) *maxH = 0x178000;
}

void Ball_kickLob(World& w, int kicker, const OctDist& d) {
    BallState& b = w.ball;
    b.spin = 0;
    placeHere(w);
    if (b.pos.y > 0) {
        b.pos.y = 0;
        placeHere(w);
    }
    const Vec3 tgt{b.pos.x + (d.dx << 16), 0, b.pos.z + (d.dz << 16)};
    if (d.dist <= 0x1e0) {
        const Fixed t = Flight_rollTime(d.dist << 16, 0x90000);
        Ball_solveGroundPass(w, b.pos, tgt, t >> 16, 0x320000, b.vel);
        b.landingFrame = (t + 0x8000) >> 16;
    } else {
        int i = 0;
        for (Fixed range = b.lob[1]; range < d.dist && i < 12;) {
            ++i;
            if (i < 12) range = b.lob[i * 4 + 1]; // row 12 (0x588FF8) is read by the exe but the loop exits anyway
        }
        if (i == 12) i = 11;
        b.vel.y = b.lob[i * 4];
        const Fixed h = fdiv(d.dist << 16, b.lob[i * 4 + 3]);
        const Angle a = atan2i(d.dx, d.dz);
        b.vel.x = fx(sinA(a), h);
        b.vel.z = fx(cosA(a), h);
        b.landingFrame = b.lob[i * 4 + 2];
        b.flags |= 0x80;
    }
    b.landing = {tgt.x >> 16, 0, tgt.z >> 16};
    finishKick(w, kicker, 2); // Ball_setKicker EDX = 2
}

void Ball_kickWithVelocity(World& w, int kicker, const Vec3& vel, Fixed spin, int kind, std::int32_t result) {
    BallState& b = w.ball;
    placeHere(w);
    b.vel = vel;
    b.spin = spin;
    b.flags &= ~0x20u;
    b.speed = octLength(b.vel.x, b.vel.z);
    if (kicker >= 0) {
        Ball_setKicker(w, kicker, kind);
        b.kickResult = result; // 0x4B9E8B: ECX
    }
    Ball_kick(w, b.vel.x, b.vel.y, b.vel.z, b.spin, -1);
}

// 0x4BA1EC: Ball_kickWithVelocity(EAX = null, EDX = &{0,0,0}, EBX = 0, ECX = 1, stack 0): the
// ball stops where it is (no kicker, so ECX/stack are unused).
void Ball_stopDead(World& w) { Ball_kickWithVelocity(w, -1, Vec3{0, 0, 0}, 0, 0, 1); }

void Ball_kickGroundPass(World& w, int kicker, const Vec3& target, std::int32_t lift, int kind) {
    BallState& b = w.ball;
    const Vec3 tgt{target.x << 16, 0, target.z << 16};
    placeHere(w);
    Ball_solveGroundPass(w, b.pos, tgt, lift, 0x1e0000, b.vel);
    b.flags &= ~0x20u;
    b.speed = octLength(b.vel.x, b.vel.z);
    if (kicker >= 0) {
        Ball_setKicker(w, kicker, kind);
        b.kickResult = 0;
    }
    b.landing = {target.x, 0, target.z};
    b.landingFrame = lift;
    Ball_kick(w, b.vel.x, b.vel.y, b.vel.z, b.spin, -1);
}

void Ball_kickTimed(World& w, int kicker, const Vec3& target, std::int32_t frames, Fixed spin, int kind) {
    const Vec3 tgt = shl16(target);
    placeHere(w);
    w.ball.spin = spin;
    Ball_solveTimedFlight(w, w.ball.pos, tgt, spin, frames, w.ball.vel);
    finishKick(w, kicker, kind);
}

void Ball_kickLofted(World& w, int kicker, const Vec3& target, Fixed spin, std::int32_t useHigh, std::int32_t lift, int kind) {
    const Vec3 tgt = shl16(target);
    placeHere(w);
    w.ball.spin = spin;
    Ball_solveFlightToTarget(w, w.ball.pos, tgt, spin, lift << 16, useHigh, 0x1e0000, 0x190000, w.ball.vel);
    finishKick(w, kicker, kind);
}

void Ball_kickDriven(World& w, int kicker, const Vec3& target, std::int32_t lift, Fixed spin, int kind) {
    Fixed maxVy = 0x190000, maxH = 0x1e0000;
    if (kicker >= 0 && kind == 4) Kick_volleyLimits(w, kicker, &maxVy, &maxH);
    const Vec3 tgt = shl16(target);
    placeHere(w);
    if (lift > maxH) lift = maxH;
    w.ball.spin = spin;
    Ball_solveFlightLowArc(w, w.ball.pos, tgt, spin, lift, maxVy, w.ball.vel);
    finishKick(w, kicker, kind);
}

void Ball_kickToTarget(World& w, int kicker, const OctDist& d, std::int32_t targetY, std::int32_t lift, Fixed spin, int kind) {
    BallState& b = w.ball;
    Fixed maxH = 0x1e0000, maxVy = 0x190000;
    Vec3 tgt{b.pos.x + (d.dx << 16), 0, b.pos.z + (d.dz << 16)};
    if (kicker >= 0) {
        if (kind == 4) Kick_volleyLimits(w, kicker, &maxVy, &maxH);
        const Fixed mk = kMaxKickSpeed[Player_attr(w, kicker, 0x38)] << 16;
        if (mk < maxH) maxH = mk;
    }
    placeHere(w);
    b.spin = spin;
    if (d.dist < 0x10) {
        b.vel = {};
    } else if (kind == 2 || kind == 0xe) {
        Ball_kickLob(w, kicker, d);
        return;
    } else {
        tgt.y = targetY << 16;
        if (lift > 0) Ball_solveFlightToTarget(w, b.pos, tgt, spin, lift << 16, 0, maxH, maxVy, b.vel);
        else Ball_solveFlightLowArc(w, b.pos, tgt, spin, maxH, maxVy, b.vel);
    }
    finishKick(w, kicker, kind);
}

void Ball_reflectOffPlayer(World& w, int pid, const Vec3& n, Fixed scale, std::int32_t result) {
    BallState& b = w.ball;
    b.flags &= ~8u;
    const Fixed mag = Math_sqrtFx(static_cast<std::uint32_t>(fx(b.vel.x, b.vel.x) + fx(b.vel.y, b.vel.y) + fx(b.vel.z, b.vel.z)));
    Fixed ux = 0, uy = 0, uz = 0;
    if (mag) {
        ux = -fdiv(b.vel.x, mag);
        uy = -fdiv(b.vel.y, mag);
        uz = -fdiv(b.vel.z, mag);
    }
    const Fixed k = (fx(ux, n.x) + fx(uy, n.y) + fx(uz, n.z)) * 2;
    const Fixed rx = fx(n.x, k) - ux, ry = fx(n.y, k) - uy, rz = fx(n.z, k) - uz;
    const Fixed s = fx(scale, mag);
    placeHere(w);
    b.vel = {fx(rx, s), fx(ry, s), fx(rz, s)};
    if (pid >= 0) {
        Ball_setKicker(w, pid, 0);
        b.kickResult = result; // ECX -> 0x669248
    }
    Ball_kick(w, b.vel.x, b.vel.y, b.vel.z, b.spin, -1);
}

void Ball_pushInDirection(World& w, int pid, Angle a, Fixed speed) {
    BallState& b = w.ball;
    const Fixed vx = fx(speed, sinA(a));
    Fixed vy;
    if (b.posI.y < 8) {
        vy = 0;
        b.pos.y = 0;
    } else {
        if (b.vel.y < -0x60000) b.vel.y = -0x60000;
        else if (b.vel.y > 0x60000) b.vel.y = 0x60000;
        vy = b.vel.y;
    }
    const Fixed vz = fx(speed, cosA(a));
    placeHere(w);
    b.vel = {vx, vy, vz};
    b.speed = octLength(vx, vz);
    if (pid >= 0) {
        Ball_setKicker(w, pid, 2);
        b.kickResult = 0;
    }
    Ball_kick(w, vx, vy, vz, b.spin, 5);
}

} // namespace wc98::sim
