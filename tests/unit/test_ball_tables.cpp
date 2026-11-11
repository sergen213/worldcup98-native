// Derived from fifa99-native (same author), ported for World Cup 98
// Ball-table provenance gate (pass-15, docs/reverse-engineering/GAMEPLAY_VALIDATION.md §3.2):
// the runtime ball tables are pinned to FIFAWC.EXE DGROUP rows at their **measured** WC98 addresses
// (the FIFA-99-era cites in ball_tables.inc comments are stale), and the flight-solver constant gap
// is encoded: kFlightSets rows all exist in WC98, while 13 of the 18 kFlightConst rows do not occur
// in the image at all - those closed-form solver constants are FIFA-99-derived, which is exactly
// what the register (GAMEPLAY_DISCREPANCIES.md) tracks as an open correction lane. A future pass
// that re-derives them from WC98 will flip the "absent" assertions to "present at new VA".
#include "check.hpp"
#include "ball_tables.inc"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

using namespace wc98::sim;

namespace {
constexpr std::uintptr_t kDataDelta = 0x401a00; // DGROUP VA -> file offset (measured pass-15)

std::vector<std::uint8_t> gImg;
std::size_t atText(std::uintptr_t va) { return std::size_t(va - 0x400000); } // .text VA -> file offset
std::size_t at(std::uintptr_t va) { return std::size_t(va - kDataDelta); }
std::uint32_t u32At(std::uintptr_t va) {
    const std::uint8_t* b = gImg.data() + at(va);
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) |
           (std::uint32_t(b[3]) << 24);
}
bool valuePresent(std::uint32_t v) {
    const std::uint8_t pat[4] = {std::uint8_t(v), std::uint8_t(v >> 8), std::uint8_t(v >> 16),
                                 std::uint8_t(v >> 24)};
    return std::search(gImg.begin(), gImg.end(), pat, pat + 4) != gImg.end();
}
} // namespace

int main() {
    const auto tree = treeDirOrSkip();
    const auto exePath = tree / "FIFAWC.EXE";
    std::ifstream f(exePath, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", exePath.string().c_str()); return 1; }
    gImg.resize(std::size_t(f.seekg(0, std::ios::end).tellg()));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(gImg.data()), std::streamsize(gImg.size()));

    // --- runtime tables: byte-exact rows at the measured WC98 addresses -------------------------
    constexpr std::uintptr_t kRestitutionVA = 0x581bd8, kBounceRetainVA = 0x581c00,
                             kRollFrictionVA = 0x581c28, kLobInitVA = 0x58ad8c,
                             kMaxKickSpeedVA = 0x59490c, kSqrtSlopeVA = 0x591490,
                             kSqrtBaseVA = 0x591514, kAtan16VA = 0x58ec6c, kIsqrtVA = 0x593d88;
    for (int i = 0; i < 10; ++i) {
        CHECK(u32At(kRestitutionVA + 4 * i) == std::uint32_t(balltab::kRestitution[i]));
        CHECK(u32At(kBounceRetainVA + 4 * i) == std::uint32_t(balltab::kBounceRetain[i]));
        CHECK(u32At(kRollFrictionVA + 4 * i) == std::uint32_t(balltab::kRollFriction[i]));
    }
    for (int i = 0; i < 48; ++i) CHECK(u32At(kLobInitVA + 4 * i) == std::uint32_t(balltab::kLobInit[i]));
    for (int i = 0; i < 20; ++i)
        CHECK(u32At(kMaxKickSpeedVA + 4 * i) == std::uint32_t(std::uint32_t(balltab::kMaxKickSpeed[i])));
    for (int i = 0; i < 33; ++i) {
        CHECK(u32At(kSqrtSlopeVA + 4 * i) == balltab::kSqrtSlope[i]);
        CHECK(u32At(kSqrtBaseVA + 4 * i) == balltab::kSqrtBase[i]);
    }
    for (int i = 0; i < 257; ++i) CHECK(u32At(kAtan16VA + 4 * i) == std::uint32_t(balltab::kAtan16[i]));
    for (int i = 0; i < 256; ++i) CHECK(gImg[at(kIsqrtVA) + i] == balltab::kIsqrt[i]);

    // --- flight-set rows: all present in WC98 (interleaved layout, not a contiguous array) ------
    for (const std::int32_t v : balltab::kFlightSets) CHECK(valuePresent(std::uint32_t(v)));

    // --- documented gap: closed-form flight constants absent from the WC98 image ---------------
    static constexpr int kAbsentRows[] = {2, 3, 4, 5, 6, 7, 9, 12, 13, 14, 15, 16, 17};
    for (const int i : kAbsentRows) CHECK(!valuePresent(std::uint32_t(balltab::kFlightConst[i])));

    // --- .text constant pins (pass-16, D-14): net-rest bounce write mov [ball+0xb9c],imm32=0x2468
    // at VA 0x4bfaf8 (the avy<0x147 lane of the FUN_004c0150 family). Gravity (-0x2468, FUN_004b2a40)
    // and drag (0xcb, same companion lane) are register-computed immediates with no imm32 encoding in
    // .text; their rows are pinned behaviorally by self_test_gameplay physics + match_demo_frames.
    static constexpr std::uint8_t kBounceBytes[] = {0xC7, 0x86, 0x9C, 0x0B, 0x00, 0x00, 0x68, 0x24,
                                                    0x00, 0x00};
    for (std::size_t i = 0; i < sizeof(kBounceBytes); ++i) CHECK(gImg[atText(0x4bfaf8) + i] == kBounceBytes[i]);
    std::printf("ball tables: 9 runtime tables byte-exact, flight-set rows present, %zu solver rows absent\n",
                sizeof(kAbsentRows) / sizeof(*kAbsentRows));
    return 0;
}
