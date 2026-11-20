// WC98-specific decode (pass-105), no fifa99-native lineage — derived from FIFAWC.EXE bytes
// and decompile evidence (analysis/pass105venue/NOTES.md).
#pragma once
// WC98 FE venue pool (pass-105): the exe-embedded venue table at DGROUP VA 0x57dec8,
// 12 entries x stride 0x5c. Each entry carries {pak, mdl, fsh} path pointers (+0x00..0x08),
// a post-load callback pointer (+0x0c, only `lpolybf` = 0x47b9a4, the case-dispatched
// scene-handler frame `FUN_0047b9a4` which reserves a record via FUN_0049f5a0 then loads
// pak entries through FUN_00439810 in a loop capped 0x30), a once-guard load handle
// (+0x24), a scene-arg slot written
// by FUN_004833f8 (+0x1c), per-entry material count (+0x58) and fly-in transform fields
// (pass-106): f32 position[3] +0x28..0x30, f32 euler rotation[3] +0x38..0x40, f32 scale
// +0x4c, i32 bbox offset pair +0x50/+0x54 — composed each frame as identity -> euler
// (FUN_0047d2f0, rad const _DAT_00566324) -> scale (FUN_0047cf1c) -> matrix stack store
// (FUN_0047d828), with mode enum +0x20 (0 static, 1/2 animated global-delta lane, >2
// back-face silhouette collector FUN_00483ae0) driving the flat/textured polygon
// rasterizer FUN_00482d7c (material colour unpack per gfx mode DAT_0057fdd4). Loader
// FUN_00483418 (EAX = venue index, once-guard
// on +0x24): zero the 800-byte name slot pool `DAT_00629548 + idx*800` + 0x4b0-byte
// material pool `DAT_0062bac8 + idx*0x4b0`, load the .PAK (RefPack'd SHPI), load the
// companion .FSH when the pak does not cover it, run the callback, then map the pak
// material records (stride 12: type 0 = packed RGB colour, type 1 = white + case-
// insensitive name lookup FUN_004df695). Venue index provenance: schedule entry record
// word +0xa high half (`mov eax,[eax+0xa]; sar eax,0x10` at 0x4587e8, feflow35 prologue).
// See analysis/pass105venue/NOTES.md and analysis/pass106venue/NOTES.md (render lane).
#include "wc98/formats/fsh.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace wc98::formats {

struct VenueEntry {
    std::string_view pakPath;  // exe-verbatim lowercase path, e.g. "fe\\pack\\pdpf.pak"
    std::string_view mdlPath;  // "fe\\pdpf.mdl"
    std::string_view fshPath;  // "fe\\pdpf.fsh"
    bool loadCallback;         // entry +0x0c non-NULL at image load
};

// The 12-entry table in exe slot order (0 pdpf .. 11 lpolybf, decoded from raw 0x17c4c8).
std::span<const VenueEntry> venueTable();

// Decompresses a venue .PAK (RefPack header 0x10FB) and parses the SHPI container it
// holds; throws formats::FormatError on malformed input.
FshFile parseVenuePak(std::span<const std::uint8_t> pak);

} // namespace wc98::formats
