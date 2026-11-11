// Derived from fifa99-native (same author), ported for World Cup 98
// Ball-table provenance gate (pass-15, docs/reverse-engineering/GAMEPLAY_VALIDATION.md §3.2):
// the runtime ball tables are pinned to FIFAWC.EXE DGROUP rows at their **measured** WC98 addresses
// (the FIFA-99-era cites in ball_tables.inc comments are stale), and the closed-form solver rows
// installed by pass-16kick are pinned too: kExpTable / kLambertA / kLambertB / kKickPool byte-exact
// at DAT_00590a20 / DAT_00590be8 / DAT_00590fe8 / DAT_005913e8. The 13 FIFA-99-only closed-form
// constant cites (literal rows below) do not occur in the image at all - the D-49 correction lane,
// now flipped to Fixed; the gate asserts they stay absent from flight.cpp's grammar.
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

    // --- pass-16kick closed-form solver rows (D-49): byte-exact WC98 lanes ---------------------
    constexpr std::uintptr_t kExpTableVA = 0x590a20, kLambertAVA = 0x590be8, kLambertBVA = 0x590fe8,
                             kKickPoolVA = 0x5913e8;
    for (int i = 0; i < 124; ++i) CHECK(u32At(kExpTableVA + 4 * i) == std::uint32_t(balltab::kExpTable[i]));
    auto u16At = [](const std::vector<std::uint8_t>& img, std::uintptr_t va, int idx) {
        const std::uint8_t* b = img.data() + (va - kDataDelta) + 2 * idx;
        return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8);
    };
    for (int i = 0; i < 512; ++i) {
        CHECK(u16At(gImg, kLambertAVA, i) == balltab::kLambertA[i]);
        CHECK(u16At(gImg, kLambertBVA, i) == balltab::kLambertB[i]);
    }
    for (int i = 0; i < 22; ++i) CHECK(u32At(kKickPoolVA + 4 * i) == std::uint32_t(balltab::kKickPool[i]));
    CHECK(u32At(0x5913e8) == 0xadf8545u); // lambert scale slot
    CHECK(u32At(0x5913ec) == 0x10000u);   // fx ONE slot
    CHECK(balltab::kNormScale[0] == 0x6a09e6u && balltab::kNormBias[0] == 0x98u); // FUN_004f2f10 seed row

    // --- FIFA-99-only closed-form cites: absent from WC98 (the D-49 gap, now closed) ------------
    static constexpr std::uint32_t kAbsentF99[] = {
        0xfffc5250u /* -0x3adb0 */,   0xffcc6895u /* -0x33976b */, 0x116e068u,
        0xc7cc5751u /* -0x3833a8af */,0xfd84db43u /* -0x27b24bd */, 0xfffec26eu /* -0x13d92 */,
        0xffee99acu /* -0x1166054 */, 0xffd0a400u /* -0x2f5c00 */, 0xe874ab47u /* -0x178b53b9 */,
        0xff8b2c0du /* -0x74d3f3 */,  0xffaa9833u /* -0x567cd */,   0xea60caacu /* -0x159f354 */,
        0xffdb7eaau /* -0x24816 */,
    };
    for (const std::uint32_t v : kAbsentF99) CHECK(!valuePresent(v));

    // --- .text constant pins (pass-16, D-14): net-rest bounce write mov [ball+0xb9c],imm32=0x2468
    // at VA 0x4bfaf8 (the avy<0x147 lane of the FUN_004c0150 family). Gravity (-0x2468, FUN_004b2a40)
    // and drag (0xcb, same companion lane) are register-computed immediates with no imm32 encoding in
    // .text; their rows are pinned behaviorally by self_test_gameplay physics + match_demo_frames.
    static constexpr std::uint8_t kBounceBytes[] = {0xC7, 0x86, 0x9C, 0x0B, 0x00, 0x00, 0x68, 0x24,
                                                    0x00, 0x00};
    for (std::size_t i = 0; i < sizeof(kBounceBytes); ++i) CHECK(gImg[atText(0x4bfaf8) + i] == kBounceBytes[i]);
    std::printf("ball tables: 9 runtime tables byte-exact, solver rows (exp/lambert/pool) pinned, %zu FIFA-99 cites absent\n",
                sizeof(kAbsentF99) / sizeof(*kAbsentF99));
    return 0;
}
