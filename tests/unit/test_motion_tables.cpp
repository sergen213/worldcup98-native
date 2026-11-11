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

    std::printf("motion tables: caps @DAT_0058aee4, turn rows @DAT_0058aef0 byte-exact vs FIFAWC.EXE\n");
    return 0;
}
