#pragma once
// 16.16 fixed-point arithmetic, 1024-step angles and distance metrics exactly as FIFA99.EXE v1.1
// computes them (see docs/reverse/SIMULATION.md §1). All simulation state is raw int32 like the
// original; `Fixed` is an alias so ported expressions stay one-to-one with the disassembly.
#include <cstdint>
#include <cstdlib>

#include "wc98/sim/trig_tables.inc"

namespace wc98::sim {

using Fixed = std::int32_t; // 16.16
using Angle = std::int32_t; // 1024 per turn; facings are stored in -0x200..0x1ff

inline constexpr Fixed kOne = 0x10000;
constexpr Fixed fxi(int i) { return static_cast<Fixed>(static_cast<std::uint32_t>(i) << 16); }

// `imul; shr eax,16; shl edx,16; adc eax,edx` — the product rounded half-up (bit 15 of the low
// word is added). This is the pattern used by almost every multiply in the simulation.
constexpr Fixed fx(Fixed a, Fixed b) {
    return static_cast<Fixed>((static_cast<std::int64_t>(a) * b + 0x8000) >> 16);
}
// `imul; shrd eax,edx,16` without the adc: truncating (floor) product.
constexpr Fixed fxt(Fixed a, Fixed b) { return static_cast<Fixed>((static_cast<std::int64_t>(a) * b) >> 16); }
// `xor eax,eax; shrd eax,edx,16; sar edx,16; idiv b` — (a << 16) / b truncated toward zero.
constexpr Fixed fdiv(Fixed a, Fixed b) { return static_cast<Fixed>((static_cast<std::int64_t>(a) << 16) / b); }

struct Vec3 {
    Fixed x = 0, y = 0, z = 0;
    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

constexpr std::int32_t iabs(std::int32_t v) { return v < 0 ? -v : v; }

// Math_vecLength2 0x4BAD9C: octagonal length |a| + |b| - min(|a|,|b|)/2 (sar = floor).
constexpr std::int32_t octLength(std::int32_t a, std::int32_t b) {
    a = iabs(a);
    b = iabs(b);
    return a > b ? a + b - (b >> 1) : a + b - (a >> 1);
}

struct OctDist {
    std::int32_t dist, dx, dz;
};
// Math_octDistance 0x4BADD4: from->to on the x/z plane (EAX = from, EDX = to; result at EBX:
// [0] dist, [1] dx, [2] dz).
constexpr OctDist octDistance(const Vec3& from, const Vec3& to) {
    const std::int32_t dx = to.x - from.x, dz = to.z - from.z;
    return {octLength(dx, dz), dx, dz};
}

// WC98 sine/cosine accessor pair FUN_00497740/4977b0 (camera.hpp l.38-42, pass-12cam) reads the
// quarter-wave table DAT_00585898 (257 entries, 16.16; rows byte-exact vs FIFAWC.EXE - gated by
// test_trig_tables). The FIFA-99 cite FUN_0046DDB0 / table 0x582EB4 names a zeros region in WC98.
// x component = sin(a), z component = cos(a): angle 0 faces +z. Quadrant = (a >> 8) & 3.
struct SinCos {
    Fixed s, c;
};
constexpr SinCos sinCos(Angle a) {
    const auto& T = detail::kSinQuarter;
    const int i = a & 0xff;
    switch ((a >> 8) & 3) {
    case 0: return {T[i], T[256 - i]};
    case 1: return {T[256 - i], -T[i]};
    case 2: return {-T[i], -T[256 - i]};
    default: return {-T[256 - i], T[i]};
    }
}

// Math_atan2_1024 0x4BB8F8 (EAX = x, EDX = z): angle of (x, z) in -0x200..0x200, 0 = +z.
// Octant ratio min/max as 0.32 (`div`), rounded to 8 bits (`shr 24; adc 0`) into byte table
// DAT_00594CAC (257 entries, 0..0x80; byte-exact vs FIFAWC.EXE - gated by test_trig_tables); equal
// magnitudes give 0x80 directly. The FIFA-99 cite 0x59F104 is a float table in WC98, not this one.
constexpr Angle atan2i(std::int32_t x, std::int32_t z) {
    unsigned flags = 0;
    std::uint32_t uz = static_cast<std::uint32_t>(z), ux = static_cast<std::uint32_t>(x);
    if (z < 0) { flags |= 8; uz = 0u - uz; }
    if (x < 0) { flags |= 0x10; ux = 0u - ux; }
    std::int32_t t;
    if (ux == uz) {
        t = 0x80;
    } else {
        std::uint32_t num = ux, den = uz;
        if (ux > uz) { num = uz; den = ux; flags |= 4; }
        const std::uint32_t q = static_cast<std::uint32_t>((static_cast<std::uint64_t>(num) << 32) / den);
        t = detail::kAtanOctant[(q >> 24) + ((q >> 23) & 1)];
    }
    switch (flags >> 2) { // jump table 0x4BB941
    case 0: return t;
    case 1: return 0x100 - t;
    case 2: return 0x200 - t;
    case 3: return t + 0x100;
    case 4: return -t;
    case 5: return t - 0x100;
    case 6: return t - 0x200;
    default: return -t - 0x100;
    }
}

// Normalise to the stored facing range -0x200..0x1ff.
constexpr Angle wrapAngle(Angle a) { return ((a + 0x200) & 0x3ff) - 0x200; }

// FUN_004BB0D0 / FUN_004BB11C: base +/- fx(scale, (sin, cos)(angle)) on x/z (y copied by caller).
constexpr Vec3 offsetInDirection(const Vec3& base, Fixed scale, Angle a) {
    const SinCos sc = sinCos(a);
    return {base.x + fx(scale, sc.s), base.y, base.z + fx(scale, sc.c)};
}

} // namespace wc98::sim
