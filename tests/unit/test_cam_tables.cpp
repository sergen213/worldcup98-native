// Camera parameter provenance gate (pass-16cam, register D-54; dossiers analysis/pass16cam/*).
// WC98-proven rows asserted byte-exact against FIFAWC.EXE:
//   * FOV/task-height halfword table DAT_0057FB82 == `kTaskHeights` (port dump), accessor read shape
//     dword[base + [cam+8]*2] >> 16 == word[idx+1], sentinel words, and the "cam0" ASCII end marker.
//   * code immediates of caminit FUN_004a0810 (FOV default cam+0x78 = 0xe28 + record defaults) and of
//     the FOV accessor FUN_004a0790 (mode-broadcast grid), incl. its table-read instruction bytes.
//   * h12/h14/h28 preset rows (DAT_00585FEA, DAT_00586046 stride 10; DAT_005862D2) and the case-5
//     preset-record pitch field DAT_00586268 = 63000 (WC98's own per-preset pitch lane).
// Provenance hygiene: FIFA-99 cite `FOV i16 0x57FAD8` is REFUTED (ascending u16 pair table there), and
// the kModes[] FIFA-99 zone-table row signatures occur ZERO times in WC98 -> those FIFA-99 rows are
// deleted from camera.cpp, replaced by byte-derived WC98 preset records (pass-17cam, section 12).
#include "check.hpp"
#include "wc98/presentation/camera.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "cam_wc98_tables.inc" // kTaskHeights (DAT_0057FB82 byte dump)

using namespace wc98::presentation;

// Flat mapping measured from the PE section table (analysis/pass16cam/camcode_consts.txt).
constexpr std::uintptr_t kDataDelta = 0x401a00; // DGROUP VA -> file offset
constexpr std::uintptr_t kTextDelta = 0x400c00; // .text ("AUTO") VA -> file offset (NOT 0x400000)

constexpr std::uintptr_t kFovTableVA = 0x57fb82;    // DAT_0057FB82, raw 0x17e182
constexpr std::uintptr_t kFifa99FovCite = 0x57fad8; // FIFA-99 cite: not a FOV table in WC98
constexpr std::uintptr_t kCaminitVA = 0x4a0810;     // camera record init (FUN_004a0810)
constexpr std::uintptr_t kAccessorVA = 0x4a0790;    // FOV accessor (FUN_004a0790)

namespace {
std::vector<std::uint8_t> img;

const std::uint8_t* at(std::uintptr_t va, std::uintptr_t delta) { return &img[va - delta]; }
std::uint16_t u16AtData(std::uintptr_t va) { const auto* b = at(va, kDataDelta); return std::uint16_t(b[0] | (b[1] << 8)); }
std::uint32_t u32AtData(std::uintptr_t va) {
    const auto* b = at(va, kDataDelta);
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
}
int countBytes(std::uintptr_t va, std::size_t n, const std::vector<std::uint8_t>& pat) {
    const auto* b = at(va, kTextDelta);
    int hits = 0;
    for (std::size_t i = 0; i + pat.size() <= n; ++i)
        if (std::equal(pat.begin(), pat.end(), b + i)) { ++hits; i += pat.size() - 1; }
    return hits;
}
int countWhole(const std::vector<std::uint8_t>& pat) {
    int hits = 0;
    for (std::size_t i = 0; i + pat.size() <= img.size(); ++i)
        if (std::equal(pat.begin(), pat.end(), img.begin() + std::ptrdiff_t(i))) { ++hits; i += pat.size() - 1; }
    return hits;
}
// LE byte pattern of a u32 sequence (int32 values sign-extended).
std::vector<std::uint8_t> seq(const std::vector<int>& vals) {
    std::vector<std::uint8_t> p;
    for (const int v : vals)
        for (int sh = 0; sh < 4; ++sh) p.push_back(std::uint8_t((std::uint32_t(v) >> (8 * sh)) & 0xff));
    return p;
}
std::vector<std::uint8_t> seq16(const std::vector<int>& vals) {
    std::vector<std::uint8_t> p;
    for (const int v : vals)
        for (int sh = 0; sh < 2; ++sh) p.push_back(std::uint8_t((std::uint32_t(v) >> (8 * sh)) & 0xff));
    return p;
}
} // namespace

int main() {
    const auto tree = treeDirOrSkip();
    std::ifstream f(tree / "FIFAWC.EXE", std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", (tree / "FIFAWC.EXE").string().c_str()); return 1; }
    img.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (img.size() < 0x190000) { std::fprintf(stderr, "image too small\n"); return 1; } // VAs are virtual

    // 1. FOV/task-height table byte-exact vs the port's DAT_0057FB82 dump (32 halfwords).
    for (int i = 0; i < 32; ++i) CHECK(u16AtData(kFovTableVA + 2 * std::uintptr_t(i)) == kTaskHeights[std::size_t(i)]);

    // 2. accessor read shape: dword at base+idx*2, top halfword sign-extended = word[idx+1]
    // (instruction bytes proven in section 4 below).
    for (int idx = 0; idx < 30; ++idx) {
        const int v = std::int32_t(u32AtData(kFovTableVA + 2 * std::uintptr_t(idx))) >> 16;
        CHECK(v == std::int16_t(kTaskHeights[std::size_t(idx) + 1]));
    }

    // 3. table boundary: word[31] = 0x6163 is the start of the "cam0" ASCII marker string, i.e. the
    // table proper ends at word[30]; no entry extends past it (WC98 file bytes; dossier fov_table.txt).
    CHECK(kTaskHeights[31] == 0x6163);
    const auto* m = at(kFovTableVA, kDataDelta) + 62; // marker begins after word[31]
    CHECK(m[0] == 'c' && m[1] == 'a' && m[2] == 'm' && m[3] == '0');

    // 4. accessor FUN_004a0790: indexes the table by [cam+8] (camera mode) with stride 2, then sar 0x10
    // — instruction bytes `mov edx,[eax+8]; mov edx,[0x57fb82+edx*2]`. Its mode-broadcast grid immediates
    // (case-2 lane DAT_00644858) are the constants tabATaskHeight returns.
    const auto* a = at(kAccessorVA, kTextDelta);
    CHECK(a[1] == 0x8b && a[2] == 0x50 && a[3] == 0x08);                       // push edx; mov edx,[eax+8]
    CHECK(a[4] == 0x8b && a[5] == 0x14 && a[6] == 0x55);                       // mov edx,[disp32+edx*2]
    CHECK(a[7] == 0x82 && a[8] == 0xfb && a[9] == 0x57 && a[10] == 0x00);      // disp32 = DAT_0057FB82
    for (const int v : {0x1280, 0x1980, 0x1100, 0x1540, 0x440}) CHECK(countBytes(kAccessorVA, 0x80, seq({v})) >= 1);

    // 5. caminit FUN_004a0810 code immediates: the FOV default EBX[0x1e] (byte +0x78) = 0xe28 is a
    // single site at 0x4a08d6 (EBX[0x1e], dword-indexed), plus the defaults shown by the decompile
    // ([0x1c]=0x460, [0x1d]=0x500, [0x23]=0x3c0, [0x1f]=0x4000, [0x20]=0x3480, [0x21]=0x1900).
    CHECK(countBytes(kCaminitVA, 0x400, seq({0xe28})) == 1); // site at raw 0x9fcd6 (EBX[0x1e] = +0x78)
    for (const int v : {0x460, 0x500, 0x3c0, 0x4000, 0x3480, 0x1900}) CHECK(countBytes(kCaminitVA, 0x400, seq({v})) == 1);

    // 6. tabATaskHeight: sentinel lanes resolve to the byte-proven constants above (grid rows).
    CHECK(tabATaskHeight(8, 0, false, false, 0xe28, 0x114) == 0xe28);  // -5 -> rec+0x78 (FOV default)
    CHECK(tabATaskHeight(11, 0, false, false, 0xe28, 0x114) == 0x114); // -4 -> rec+0x114
    CHECK(tabATaskHeight(13, 1, false, false, 0, 0) == 0x1280);
    CHECK(tabATaskHeight(13, 2, true, false, 0, 0) == 0x1280);
    CHECK(tabATaskHeight(13, 2, false, false, 0, 0) == 0x440);
    CHECK(tabATaskHeight(13, 3, false, true, 0, 0) == 0x1980);  // rec+0x10 == 3
    CHECK(tabATaskHeight(13, 3, false, false, 0, 0) == 0x1100);
    CHECK(tabATaskHeight(13, 0, false, false, 0, 0) == 0x1540);

    // 7. preset rows byte-exact: h12 DAT_00585FEA stride 10, h14 DAT_00586046 stride 10, h28 DAT_005862D2.
    for (int row = 0; row < 2; ++row) {
        const std::uintptr_t va = 0x585fea + 10 * std::uintptr_t(row);
        const auto r = cameraPresetH12(row);
        CHECK(r.angle == u16AtData(va) && r.speed == u16AtData(va + 2) && r.height == u16AtData(va + 4) && r.yawOff == u16AtData(va + 6));
    }
    for (int slot = 0; slot < 4; ++slot) {
        const std::uintptr_t va = 0x586046 + 10 * std::uintptr_t(slot);
        const auto r = cameraPresetH14(slot);
        CHECK(r.angle == u16AtData(va) && r.speed == u16AtData(va + 2) && r.height == u16AtData(va + 4) && r.yawOff == u16AtData(va + 6));
    }
    {
        const auto r = cameraPresetH28();
        CHECK(r.angle == u16AtData(0x5862d2) && r.speed == u16AtData(0x5862d4) && r.height == u16AtData(0x5862d6) &&
              r.yawOff == u16AtData(0x5862d8));
    }

    // 8. WC98's own per-preset pitch lane: case-5 preset record (DAT_00586248 stride 0x2c) +0x20 = 63000,
    // and the selector FUN_004ca880 writes that same value live. Distinct from FIFA-99's zone tables.
    CHECK(u32AtData(0x586268) == 63000u);
    CHECK(countBytes(0x4ca880, 0x300, seq({63000})) == 1); // selector write site at 0x4caa2f

    // 9. FIFA-99 cite refuted: WC98 bytes at 0x57FAD8 are an ascending u16 pair table, not FOV words.
    CHECK(u16AtData(kFifa99FovCite) == 0x0005 && u16AtData(kFifa99FovCite + 2) == 0x0004);
    CHECK(u16AtData(kFifa99FovCite + 4) == 0x0007 && u16AtData(kFifa99FovCite + 6) == 0x0006);
    CHECK(countBytes(kFifa99FovCite, 32, seq16({0x0cc0, 0x0cc0, 0x0cc0, 0x0a00})) == 0); // WC98 FOV run absent there

    // 10. FIFA-99 zone-table row signatures (the deleted `kModes` rows, pass-17cam): zero occurrences
    // in the WC98 image (analysis/pass16cam/absent_scan.txt) -> WC98 uses preset records instead.
    CHECK(countWhole(seq({60900, 61050, 61200, 61350, 61500})) == 0); // Tele pitchMax
    CHECK(countWhole(seq({60500, 60700, 60900, 61100, 61300})) == 0); // Tele pitchMin
    CHECK(countWhole(seq({61750, 61750, 61750, 61750, 61750})) == 0); // Sideline pitchMax
    CHECK(countWhole(seq({61500, 61500, 61500, 61500, 61500})) == 0); // Action/Stadium pitchMax
    CHECK(countWhole(seq({63000, 63000, 63000, 62000, 62000})) == 0); // Goal pitchMax
    CHECK(countWhole(seq({12250, 12000, 11750, 11500, 11250})) == 0); // Stadium yawMax
    CHECK(countWhole(seq({-2000, -750, 750, 2000})) == 0);            // Stadium yawKeys
    CHECK(countWhole(seq({62500, 61900})) == 0);                      // Tele pitchA/pitchB pair
    CHECK(countWhole(seq({0, 800, -3000, 0, 800, -1750})) == 0);      // CamHeightTele0 rows
    CHECK(countWhole(seq({0, 450, -3000, 0, 450, -1250})) == 0);      // CamHeightSide0 rows
    CHECK(countWhole(seq16({3200, 3264, 3264, 2700, 3264, 2176, 2752, 3900})) == 0); // FIFA-99 FOV rows
    CHECK(countWhole(seq({61900})) == 0);                             // magic single (u32) absent

    // 11. WC98's live per-mode FOV words [1..8] occur exactly once: the DAT_0057FB82 table itself.
    CHECK(countWhole(seq16({0x0cc0, 0x0cc0, 0x0cc0, 0x0a00, 0x0cc0, 0x0880, 0x0ac0, 0x0f00})) == 1);

    // 12. WC98 preset-record grammar (pass-17cam, register D-54 CLOSED): the port's kPresets table ==
    // image bytes field-for-field for all 11 selector FUN_004ca880 targets (stride 0x2c band DAT_00586xxx).
    {
        constexpr std::uintptr_t va[11] = {0x58613c, 0x586168, 0x586194, 0x5861c0, 0x5861ec, 0x586218,
                                           0x58608c, 0x5860b8, 0x5860e4, 0x586110, 0x586248};
        for (int i = 0; i < 11; ++i) {
            const auto& p = kPresets[std::size_t(i)];
            CHECK(u16AtData(va[std::size_t(i)] + 0x8) == p.flags);                     // task-lane flags
            CHECK(u16AtData(va[std::size_t(i)] + 0xa) == std::uint16_t(p.radius));     // radius/x lane
            CHECK(u16AtData(va[std::size_t(i)] + 0xc) == std::uint16_t(p.height));     // eye height
            CHECK(u16AtData(va[std::size_t(i)] + 0xe) == std::uint16_t(p.zOff));       // signed z lane
            CHECK(u16AtData(va[std::size_t(i)] + 0x18) == std::uint16_t(p.cx));        // center pair
            CHECK(u16AtData(va[std::size_t(i)] + 0x1a) == std::uint16_t(p.cz));
            CHECK(u16AtData(va[std::size_t(i)] + 0x1c) == p.yawRaw);                   // direct yaw word
            CHECK(u16AtData(va[std::size_t(i)] + 0x20) == p.pitch);                    // pitch target lane
            CHECK(u16AtData(va[std::size_t(i)] + 0x26) == p.speed);                    // smoothing word
        }
        // Case-5 (Shoulder) live-patch inputs: multiplier DAT_00586244 + selector immediates. The
        // pitch write 63000 is pinned in section 8; the height patch equals the record static.
        CHECK(u16AtData(0x586244) == kCase5Mul);                          // 0x3e0 = 992/65536 scale
        CHECK(u16AtData(0x586248 + 0xc) == std::uint16_t(kCase5Height));  // record static == patch 0x180
        // Mode-word dispatch (DAT_00580df0): selector entry loads the word, dec + range-checks
        // {1..4} (case 5 falls to default), and FUN_004ca7b0 gates case-3 on cmp eax,3.
        CHECK(countBytes(0x4ca88f, 16, std::vector<std::uint8_t>{0xa1, 0xf0, 0x0d, 0x58, 0x00, 0x48, 0x83, 0xf8, 0x04}) == 1);
        CHECK(countBytes(0x4ca7b5, 16, std::vector<std::uint8_t>{0xa1, 0xf0, 0x0d, 0x58, 0x00, 0x83, 0xf8, 0x03}) == 1);
    }

    std::printf("cam tables: FOV DAT_0057FB82 + caminit/preset rows byte-exact; FIFA-99 cites refuted\n");
    return 0;
}
