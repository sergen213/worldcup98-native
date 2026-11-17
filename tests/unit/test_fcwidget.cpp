// FC widget-id remap table gate (pass-21igmenu; analysis/pass21igmenu/fc_painter.txt:1231-1257).
// The list painter resolves ROWS labels as s16 at (rows+0x40)+idx*2: >=0 ids go through the remap
// table DAT_005774a4 (FUN_0043c9a0) into the FC bank (FUN_004c31d0); <0 ids take the dynamic-string
// lane (FUN_0047a8b4). Asserted byte-exact against FIFAWC.EXE:
//   * full 4096-dword table dump == kFcWidgetRemap, incl. sentinel count and the accessor-body bytes
//     (remap read; bank deref via runtime-heap pointer DAT_00585cb0 = FUN_0049f5a0 in loader FUN_004745e8).
//   * key rows: 1='Main Menu'(1022), 2='Match Select'(1039), 3='Team Management'(1833),
//     5='Options'(1273), 60='OK'(1256), 1174='Home or Away'(891).
//   * the IGOPTN2 trio's ROWS ids (2000,2001,2002,2026,2027,2028,2031,2032,2033) are 0xffffffff
//     sentinels: the remap lane is inert for them, and the sentinel accessor reads heap[-4] with no
//     NULL guard in the painter copy loop -> retail's trio captions come from a separate lane [H];
//     the port's sentinel->"" row rendering is a [H] model, not retail-proven.
#include "check.hpp"
#include <cstdint>
#include <initializer_list>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include "fcwidget_tables.inc" // kFcWidgetRemap (DAT_005774a4 byte dump)

using namespace wc98::presentation;

constexpr std::uintptr_t kDataDelta = 0x401a00; // DGROUP VA -> file offset (test_cam_tables)
constexpr std::uintptr_t kTextDelta = 0x400c00; // .text VA -> file offset
constexpr std::uintptr_t kRemapVA = 0x5774a4;   // DAT_005774a4, raw 0x175aa4
constexpr std::uintptr_t kFcbankVA = 0x585cb0;  // DAT_00585cb0 FC bank pointer table

namespace {
std::vector<std::uint8_t> img;

std::uint32_t u32AtData(std::uintptr_t va) {
    const auto* b = &img[va - kDataDelta];
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
}
bool bytesAtText(std::uintptr_t va, std::initializer_list<int> pat) {
    const auto* b = &img[va - kTextDelta];
    for (unsigned i = 0; i < pat.size(); ++i)
        if (b[i] != unsigned(*std::next(pat.begin(), i))) return false;
    return true;
}
} // namespace

int main() {
    const auto tree = treeDirOrSkip();
    std::ifstream f(tree / "FIFAWC.EXE", std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", (tree / "FIFAWC.EXE").string().c_str()); return 1; }
    img.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (img.size() < 0x190000) { std::fprintf(stderr, "image too small\n"); return 1; }

    // 1. Full table byte-exact vs the port's DAT_005774a4 dump.
    for (int i = 0; i < 4096; ++i)
        CHECK(u32AtData(kRemapVA + 4 * std::uintptr_t(i)) == kFcWidgetRemap[std::size_t(i)]);

    // 2. Key widget rows (gated FCENG.BIN.json resolution).
    CHECK(kFcWidgetRemap[1] == 1022u);   // 'Main Menu'
    CHECK(kFcWidgetRemap[2] == 1039u);   // 'Match Select'
    CHECK(kFcWidgetRemap[3] == 1833u);   // 'Team Management'
    CHECK(kFcWidgetRemap[5] == 1273u);   // 'Options'
    CHECK(kFcWidgetRemap[6] == 998u);    // 'Load Game'
    CHECK(kFcWidgetRemap[60] == 1256u);  // 'OK'
    CHECK(kFcWidgetRemap[1174] == 891u); // 'Home or Away'

    // 3. IGOPTN2 trio ROWS ids: all sentinels -> the remap lane is inert for them; retail's row
    // captions come from a separate lane [H] (immediate-id dispatcher, pass-21 todo).
    for (int id : {2000, 2001, 2002, 2026, 2027, 2028, 2031, 2032, 2033})
        CHECK(kFcWidgetRemap[std::size_t(id)] == 0xffffffffu);
    std::size_t sent = 0;
    for (std::uint32_t v : kFcWidgetRemap) sent += v == 0xffffffffu;
    CHECK(sent == 3044);

    // 4. Bank array is runtime-allocated: FUN_004c3150 = cmp [0x585cb0],0; (guard); mov [0x648fa8],eax;
    // shl eax,2; mov eax,0x5692a0(count); call FUN_0049f5a0 (heap); mov [0x585cb0],eax
    // (fc_loader3.txt:275-289). Consequence: the 0xffffffff accessor reads heap[-4] (allocator header),
    // NOT a static DGROUP dword, and the painter copy loop has no NULL guard (fc_painter.txt:1234-1250)
    // -> sentinel-row string is runtime-heap-dependent, statically unknowable. The port's
    // sentinel->"" rendering is a [H] model, not retail-proven.
    CHECK(bytesAtText(0x4c3150, {0x53, 0x83, 0x3D, 0xB0, 0x5C, 0x58, 0x00, 0x00}));
    CHECK(bytesAtText(0x4c3172, {0xE8, 0x29, 0xC4, 0xFD, 0xFF, 0xA3, 0xB0, 0x5C, 0x58, 0x00}));

    // 5. Accessor bodies: FUN_0043c9a0 = mov eax,[eax*4+0x5774a4] (remap table read);
    // FUN_004c31d0 = push edx; mov edx,[0x585cb0]; mov eax,[edx+eax*4]; pop edx; ret (bank deref).
    CHECK(bytesAtText(0x43c9a0, {0x8B, 0x04, 0x85, 0xA4, 0x74, 0x57, 0x00}));
    CHECK(bytesAtText(0x4c31d0, {0x52, 0x8B, 0x15, 0xB0, 0x5C, 0x58, 0x00, 0x8B, 0x04, 0x82, 0x5A, 0xC3}));

    // 6. FC bank table base sanity: remap rows index the bank, so every mapped id must be a valid
    // pointer dword in the image (the bank table holds string VAs, not ids; mapped ids < 2031 = bank size).
    for (std::size_t i = 0; i < 4096; ++i)
        if (kFcWidgetRemap[i] != 0xffffffffu) CHECK(kFcWidgetRemap[i] < 2031u);

    std::fprintf(stderr, "fcwidget: table byte-exact, sentinels=%zu\n", sent);
    return 0;
}
