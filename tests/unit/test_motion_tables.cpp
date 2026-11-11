// Motion cap / turn-class provenance gate (pass-15 S2, analysis/pass15motion/BYTES.md): the speed
// caps and turn-class threshold rows used by Motion_rootMotionStep are pinned **byte-exact** to
// FIFAWC.EXE DGROUP, and the FIFA-99 cite addresses that named them in the fifa99-native lineage
// (0x58AD60 / 0x58AD6C) are asserted to be zeros — WC98 keeps these tables at DAT_0058aee4 /
// DAT_0058aef0. Rows: caps stride-4 triple, thresholds stride-8 rows (cls0 = 228/469 is the row
// the FIFA-99-derived port never used; register D-55).
#include "check.hpp"
#include "wc98/sim/fixed.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
#include "wc98/sim/motion_tables.inc" // kSpeedCap / kTurnClass (public header, D-55 rows)

using namespace wc98::sim;

// FIFAWC.EXE DGROUP -> file offset (measured pass-15; REVERSE_ENGINEERING_NOTES.md §1).
constexpr std::uintptr_t kDataDelta = 0x401a00;
constexpr std::uintptr_t kCapTableVA = 0x58aee4;   // DAT_0058aee4, stride 4
constexpr std::uintptr_t kTurnTableVA = 0x58aef0;  // DAT_0058aef0, stride 8 rows
constexpr std::uintptr_t kFifa99CapCite = 0x58ad60;
constexpr std::uintptr_t kFifa99TurnCite = 0x58ad6c;

int main() {
    const auto tree = treeDirOrSkip();
    const auto exePath = tree / "FIFAWC.EXE";
    std::ifstream f(exePath, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", exePath.string().c_str()); return 1; }
    std::vector<std::uint8_t> img((std::size_t(f.seekg(0, std::ios::end).tellg())));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(img.data()), std::streamsize(img.size()));
    const auto u32At = [&](std::uintptr_t va) {
        const std::uint8_t* b = &img[std::size_t(va - kDataDelta)];
        return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) |
               (std::uint32_t(b[3]) << 24);
    };

    // 1. speed caps by turn class byte-exact (0xc0000 / 0x50000 / 0x50000).
    for (int i = 0; i < 3; ++i)
        CHECK(u32At(kCapTableVA + 4 * std::uintptr_t(i)) == std::uint32_t(kSpeedCap[i]));

    // 2. turn-class thresholds byte-exact, stride-8 rows indexed by current class.
    for (int cls = 0; cls < 3; ++cls) {
        CHECK(u32At(kTurnTableVA + 8 * std::uintptr_t(cls)) == std::uint32_t(kTurnClass[cls][0]));
        CHECK(u32At(kTurnTableVA + 4 + 8 * std::uintptr_t(cls)) == std::uint32_t(kTurnClass[cls][1]));
    }
    // The cls-0 row is the one the FIFA-99-derived table dropped (D-55): pin it explicitly.
    CHECK(u32At(kTurnTableVA) == 228u);
    CHECK(u32At(kTurnTableVA + 4) == 469u);

    // 3. the FIFA-99 cite addresses are zeros in WC98 (provenance hygiene, cf. trig gate).
    for (int i = 0; i < 4; ++i) {
        CHECK(u32At(kFifa99CapCite + 4 * std::uintptr_t(i)) == 0u);
        CHECK(u32At(kFifa99TurnCite + 4 * std::uintptr_t(i)) == 0u);
    }

    // 4. stride continuity: the threshold rows immediately follow the caps triple, so the byte
    // lane proves the two tables are one adjacent block (caps +0..0xB, thresholds +0xC..).
    CHECK(u32At(kCapTableVA + 12) == 228u);

    // 5. D-09 (pass-17 S5): the FIFA-99 anim-rate switch {e666,f777,10888,11999} is refuted.
    // The f777/11999 dwords exist NOWHERE in FIFAWC.EXE; e666/10888 are FE-config/camera/scorer
    // constants. The ladder at DAT_00581bd8 (VA 0x581bd8..0x581c4f) is the FE config table: its
    // only code reference is disp32 @file 0xb0ab7 = VA 0x4b16b7 inside FUN_004b1690, which stores
    // DAT_00581bd8[..] into DAT_00645c74[mode*0x14 + 0xD4C..] indexed by (DAT_00640fc4 = FE mode).
    const std::uint32_t kFeConfig[30] = {
        0x9eb8, 0x9eb8, 0x9eb8, 0x9eb8, 0x9eb8, 0x9999, 0xa666, 0x8000, 0x4000, 0x8ccc,
        0xbae1, 0xbae1, 0xbae1, 0xbae1, 0xbae1, 0xb851, 0xcccc, 0xb333, 0xe666, 0xc000,
        0xfae1, 0xfae1, 0xfae1, 0xfae1, 0xfae1, 0xfae1, 0xfae1, 0xf5c2, 0xf333, 0xf851};
    for (int i = 0; i < 30; ++i) CHECK(u32At(0x581bd8 + 4 * std::uintptr_t(i)) == kFeConfig[i]);
    const auto countRaw = [&](std::uint32_t v) { // LE dword occurrences in the whole image
        const std::uint8_t pat[4] = {std::uint8_t(v), std::uint8_t(v >> 8), std::uint8_t(v >> 16),
                                     std::uint8_t(v >> 24)};
        std::size_t n = 0, first = ~std::size_t(0);
        for (std::size_t i = 0; i + 4 <= img.size(); ++i)
            if (img[i] == pat[0] && img[i + 1] == pat[1] && img[i + 2] == pat[2] && img[i + 3] == pat[3]) {
                if (n == 0) first = i;
                ++n;
            }
        CHECK(n == 1 && first == 0xb0ab7); // the single ladder reference: FUN_004b1690 store site
        return n;
    };
    CHECK(countRaw(0x581bd8u) == 1); // FE config consumer (VA 0x4b16b7, +0x27 of FUN_004b1690)
    const auto countAll = [&](std::uint32_t v) {
        const std::uint8_t pat[4] = {std::uint8_t(v), std::uint8_t(v >> 8), std::uint8_t(v >> 16),
                                     std::uint8_t(v >> 24)};
        std::size_t n = 0;
        for (std::size_t i = 0; i + 4 <= img.size(); ++i)
            if (img[i] == pat[0] && img[i + 1] == pat[1] && img[i + 2] == pat[2] && img[i + 3] == pat[3]) ++n;
        return n;
    };
    CHECK(countAll(0xf777u) == 0);  // FIFA-99 anim-rate switch constants: absent from WC98 (D-09)
    CHECK(countAll(0x11999u) == 0);
    std::printf("motion tables: caps @DAT_0058aee4, turn rows @DAT_0058aef0 byte-exact vs FIFAWC.EXE\n");
}
