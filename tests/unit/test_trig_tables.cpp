// Derived from fifa99-native (same author), ported for World Cup 98
// Trig-table provenance gate (pass-15, docs/reverse-engineering/GAMEPLAY_VALIDATION.md §3.1):
// the fixed-point sine quarter-wave and atan octant tables used by every rotation in the sim are
// pinned **byte-exact** to FIFAWC.EXE DGROUP rows, and the FIFA-99 cite addresses that used to name
// them are asserted absent (they are a zeros region / a float table in WC98). Quadrant-boundary
// exactness of sinCos/atan2i is gated too - those values feed kick/dribble vector math
// (`ball.cpp` `fx(0x100000, sinCos(a).s)`) and keeper steering.
#include "check.hpp"
#include "wc98/sim/fixed.hpp"

#include <cstdint>
#include <fstream>
#include <vector>

using namespace wc98::sim;

// FIFAWC.EXE DGROUP -> file offset (measured pass-15; REVERSE_ENGINEERING_NOTES.md §1).
constexpr std::uintptr_t kDataDelta = 0x401a00;
constexpr std::uintptr_t kSineTableVA = 0x585898; // DAT_00585898, 257 x i32 (16.16)
constexpr std::uintptr_t kAtanTableVA = 0x594cac; // 257 x u8 octant ratios
// FIFA-99 cites retained from the fifa99-native lineage: in WC98 these name non-tables.
constexpr std::uintptr_t kFifa99SineCite = 0x582eb4;
constexpr std::uintptr_t kFifa99AtanCite = 0x59f104;

int main() {
    const auto tree = treeDirOrSkip();
    const auto exePath = tree / "FIFAWC.EXE";
    std::ifstream f(exePath, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", exePath.string().c_str()); return 1; }
    std::vector<std::uint8_t> img((std::size_t(f.seekg(0, std::ios::end).tellg())));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(img.data()), std::streamsize(img.size()));
    const auto byteAt = [&](std::uintptr_t va) { return img[std::size_t(va - kDataDelta)]; };
    const auto u32At = [&](std::uintptr_t va) {
        return std::uint32_t(byteAt(va)) | (std::uint32_t(byteAt(va + 1)) << 8) |
               (std::uint32_t(byteAt(va + 2)) << 16) | (std::uint32_t(byteAt(va + 3)) << 24);
    };

    // 1. sine quarter-wave rows byte-exact, including the adjacent word T[256] the fold indexes.
    for (int i = 0; i <= 256; ++i) {
        const auto want = std::uint32_t(std::uint32_t(detail::kSinQuarter[i]));
        CHECK(u32At(kSineTableVA + 4 * std::uintptr_t(i)) == want);
    }
    CHECK(detail::kSinQuarter[256] == 0x10000); // sin(quarter turn) * 65536, exact
    for (int i = 1; i <= 256; ++i) CHECK(detail::kSinQuarter[i] > detail::kSinQuarter[i - 1]);

    // 2. atan octant rows byte-exact.
    for (int i = 0; i <= 256; ++i) {
        CHECK(byteAt(kAtanTableVA + std::uintptr_t(i)) == detail::kAtanOctant[i]);
    }
    CHECK(detail::kAtanOctant[0] == 0);
    CHECK(detail::kAtanOctant[256] == 128);

    // 3. the FIFA-99 cite addresses are not trig tables in WC98 (provenance hygiene).
    for (int i = 0; i < 4; ++i) CHECK(u32At(kFifa99SineCite + 4 * std::uintptr_t(i)) == 0u);
    CHECK(byteAt(kFifa99AtanCite + 3) == 0x3f); // float table (exponent byte), not u8 ratios

    // 4. quadrant-boundary exactness of the rotation used by vector math.
    const SinCos q0 = sinCos(0);
    CHECK(q0.s == 0 && q0.c == 0x10000);
    const SinCos q1 = sinCos(0x100);
    CHECK(q1.s == 0x10000 && q1.c == 0);
    const SinCos q2 = sinCos(0x200);
    CHECK(q2.s == 0 && q2.c == -0x10000);
    const SinCos q3 = sinCos(0x300);
    CHECK(q3.s == -0x10000 && q3.c == 0);

    // 5. atan2i octant/axis behaviours (angle units: 0x400 per turn, 0 faces +z).
    CHECK(atan2i(0, 0x100) == 0);
    CHECK(atan2i(0x100, 0x100) == 0x80);   // equal magnitudes = 45 deg
    CHECK(atan2i(-0x100, 0x100) == -0x80);

    // 6. Octant composition dispatch (pass-17s7atan2): the decoded exe accessor FUN_004f3db4
    // builds flags {8 = z<0, 0x10 = x<0, 4 = swap} and jumps through the thunk table at
    // VA 0x4f3dfd (entries 0x4f3e24/e1d/e27/e31/e39/e3e/e46/e4e) with v = octant-table byte.
    // Mirror arithmetic over sample magnitudes x sign combinations and pin every arm.
    const std::int32_t kMags[] = {1, 7, 64, 255, 256, 0x4f3d};
    for (const std::int32_t a : kMags)
        for (const std::int32_t b : kMags)
            for (const int sz : {1, -1})
                for (const int sx : {1, -1}) {
                    const std::int32_t z = sz * a, x = sx * b;
                    unsigned flags = 0;
                    std::uint32_t uz = static_cast<std::uint32_t>(z), ux = static_cast<std::uint32_t>(x);
                    if (z < 0) { flags |= 8; uz = 0u - uz; }
                    if (x < 0) { flags |= 0x10; ux = 0u - ux; }
                    // The exe's equal-magnitude branch (|x| == |z|, incl. 0/0) dispatches through
                    // the same thunk table with v = 0x80 and no swap bit; mixed signs compose via
                    // arms {v, 0x200-v, -v, -v-0x100} (entries EBX = 0, 8, 0x10, 0x18).
                    int v;
                    if (ux == uz) {
                        v = 0x80; // direct constant, no table read and no swap bit set
                    } else {
                        std::uint32_t num = ux, den = uz;
                        if (ux > uz) { num = uz; den = ux; flags |= 4; }
                        const std::uint32_t q = static_cast<std::uint32_t>(
                            (static_cast<std::uint64_t>(num) << 32) / den); // div pair, round nearest
                        v = byteAt(kAtanTableVA + std::uintptr_t((q >> 24) + ((q >> 23) & 1)));
                    }
                    int exp;
                    switch (flags >> 2) {
                    case 0: exp = v; break;
                    case 1: exp = 0x100 - v; break;
                    case 2: exp = 0x200 - v; break;
                    case 3: exp = v + 0x100; break;
                    case 4: exp = -v; break;
                    case 5: exp = v - 0x100; break;
                    case 6: exp = v - 0x200; break;
                    default: exp = -v - 0x100; break;
                    }
                    CHECK((atan2i(x, z) & 0x3ff) == (exp & 0x3ff)); // consumers mask to circle 0x400
                }
    CHECK((atan2i(0, 0) & 0x3ff) == 0x80); // (0,0): equal magnitudes, flags 0 -> direct 0x80

    CHECK(atan2i(0x100, 0) == 0x100); // +x axis = 90 deg (section 5 tail)

    std::printf("trig tables: sine @%s, atan @%s byte-exact vs FIFAWC.EXE\n",
                "DAT_00585898", "DAT_00594CAC");
    return 0;
}
