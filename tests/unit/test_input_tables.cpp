// Input-layer table provenance gate (pass-15/16, analysis/pass15input/BYTES5.md): every SpInput
// table used by the pad-decode/gesture path is pinned **byte-exact** to FIFAWC.EXE DGROUP. The
// context-slot lane decodes all 9x14 slot pointers @0x58d314 into action-table ids and pins the
// port's index lists to them — register D-71 **refuted**: dev5 ctx03 = ptr @0x58cd14 -> id 4,
// ctx10 = ptr @0x58cf94 -> id 9, exactly what the port carries (the registry's cited deltas
// 0x58cd94/0x58cf14 were big-endian reads of these little-endian slot bytes). The queue-gate lane
// also proves D-66/D-58: one 96-byte array @0x58c750, no +2 offset, no second table.
#include "check.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
#include "input_tables.inc" // kButtonMask / kCtxTable / kActionTable / kCtxKeep / kActionQueueGate

// FIFAWC.EXE DGROUP -> file offset (measured pass-15; REVERSE_ENGINEERING_NOTES.md §1).
constexpr std::uintptr_t kDataDelta = 0x401a00;
constexpr std::uintptr_t kMaskTableVA = 0x58c7b4;   // dev*0x60, 8 entries x 12 B (first dword)
constexpr std::uintptr_t kCtxSlotsVA = 0x58d314;    // dev*0xA8 + ctx*0xC = {8, table ptr, ctx}
constexpr std::uintptr_t kActionBaseVA = 0x58cb14;  // 16 tables x 0x80 bytes
constexpr std::uintptr_t kCtxKeepVA = 0x58d8fc;     // [old*14 + new] u8
constexpr std::uintptr_t kQueueGateVA = 0x58c750;   // D-66: single 96-byte array

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
    const auto u8At = [&](std::uintptr_t va) { return img[std::size_t(va - kDataDelta)]; };

    // 1. button masks: dev*0x60 + b*12, first dword only (BYTES5.md mask rows).
    for (int d = 0; d < 9; ++d)
        for (int b = 0; b < 8; ++b)
            CHECK(u32At(kMaskTableVA + std::uintptr_t(d) * 0x60 + std::uintptr_t(b) * 12) ==
                  kButtonMask[d][b]);

    // 2. context slots: decode the table pointer of every (dev, ctx) slot into an action-table id
    //    and pin the port's index list to it. This is D-71's byte lane; dev5/ctx10 = @0x58cf14 -> 8.
    for (int d = 0; d < 9; ++d) {
        for (int c = 0; c < 14; ++c) {
            const std::uintptr_t slot = kCtxSlotsVA + std::uintptr_t(d) * 0xA8 + std::uintptr_t(c) * 0xC;
            CHECK(u32At(slot) == 8u); // header word of the slot record
            const std::uint32_t ptr = u32At(slot + 4);
            CHECK(ptr >= kActionBaseVA && ptr < kActionBaseVA + 16 * 0x80);
            const std::uint32_t id = (ptr - kActionBaseVA) / 0x80;
            CHECK(kCtxTable[d][c] == std::uint8_t(id));
        }
    }
// dev5 explicit slot pointers from the WC98 bytes (BYTES5.md, LE decode): ctx03 @0x58cd14 -> id 4,
// ctx10 @0x58cf94 -> id 9. These are the two ids the D-71 registry row claimed as wrong; both pin.
    CHECK(u32At(kCtxSlotsVA + 5 * 0xA8 + 3 * 0xC + 4) == 0x58cd14u);
    CHECK(u32At(kCtxSlotsVA + 5 * 0xA8 + 10 * 0xC + 4) == 0x58cf94u);

    // 3. action tables: full 2048-byte lane (WC98 dump; 162 entries differ from FIFA-99's).
    for (int t = 0; t < 16; ++t)
        for (int e = 0; e < 16; ++e)
            for (int b = 0; b < 8; ++b)
                CHECK(u8At(kActionBaseVA + std::uintptr_t(t) * 0x80 + std::uintptr_t(e) * 8 + b) ==
                      kActionTable[t][e][b]);

    // 4. context-switch keep masks: full 196-byte lane.
    for (int o = 0; o < 14; ++o)
        for (int n2 = 0; n2 < 14; ++n2)
            CHECK(u8At(kCtxKeepVA + std::uintptr_t(o) * 14 + std::uintptr_t(n2)) == kCtxKeep[o][n2]);

    // 5. action-queue gate: D-66/D-58 — one 96-byte array at 0x58c750, no +2 offset.
    for (int i = 0; i < 96; ++i)
        CHECK(u8At(kQueueGateVA + std::uintptr_t(i)) == kActionQueueGate[i]);

    std::printf("input tables: masks/ctx slots/action tables/keep-masks/queue-gate byte-exact vs FIFAWC.EXE (D-71)\n");
    return 0;
}
