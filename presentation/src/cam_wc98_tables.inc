// WC98 byte-exact camera tables (pass-16cam, register D-54). Rows are raw FIFAWC.EXE halfwords;
// do not edit. Provenance: FOV/task-height table **DAT_0057FB82** (raw 0x17e182), accessor
// FUN_004a0790 reads `mov edx,[0x57fb82+edx*2]` at 0x4a0794 with edx = [cam+8] (camera mode), then
// sar >>16 sign-extends the top halfword — so the value for index i is this table's word i+1.
// Dossiers: analysis/pass16cam/fov_table.txt (32-word dump + "cam0" ASCII end marker),
// camcode_consts.txt (section deltas + code-window immediates), band_0x4ca.txt (handler set).
// The FIFA-99 cite `FOV i16 0x57FAD8` is refuted for WC98: that VA holds an ascending u16 pair
// table {0005,0004,0007,0006,...}, not FOV words (asserted by test_cam_tables).
#pragma once
#include <cstdint>

namespace wc98::presentation {

// DAT_0057FB82 halfwords [0..31]; word[31] = 0x6163 is the first byte of the "cam0" ASCII marker
// string that follows the table, i.e. the table ends at word[30]. Sentinel words: -5 (0xfffb) ->
// rec+0x78 (FOV default 0xe28, caminit FUN_004a0810), -4 (0xfffc) -> rec+0x114, -1 (0xffff) -> the
// mode-broadcast constants in tabATaskHeight.
inline constexpr std::uint16_t kTaskHeights[32] = { // VA DAT_0057FB82, raw 0x17e182
    0x0000, 0x0cc0, 0x0cc0, 0x0cc0, 0x0a00, 0x0cc0, 0x0880, 0x0ac0,
    0x0f00, 0xfffb, 0x0cc0, 0x0cc0, 0xfffc, 0x1100, 0xffff, 0x1280,
    0x1280, 0x0880, 0xfffb, 0x1540, 0x0e40, 0x1a80, 0x1280, 0x1540,
    0x0f00, 0x1540, 0x08fc, 0x0cc0, 0x0cc0, 0x0cc0, 0x0cc0, 0x6163};

} // namespace wc98::presentation
