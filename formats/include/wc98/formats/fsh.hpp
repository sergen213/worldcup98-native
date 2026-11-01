// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA "SHPI" shape/texture container (.FSH; also inside .BIG, RefPack-wrapped .PAK/.NEW).
// See docs/formats/FSH.md for the layout and the confidence of each field.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wc98::formats {

// WC98 deltas (research-inbox/F99_REUSABILITY_MATRIX.md section 4):
//   * palette records 0x29 / 0x2D: the 16-bit FE palettes (15 of the 1204 WC98
//     .FSH/.PAK files reference one of them and fail on FIFA 99's reader),
//   * WC98 bitmaps index past their attached palette record's declared count and carry
//     no "!pal" tag (0 of 410 .FSH), so record bodies are decoded to capacity (cap 256)
//     and the shared FE palette below is the fallback of last resort,
//   * declaredSize == fileSize in 100 % of WC98 files (no "\0cRC" trailer); the FIFA 99
//     trailer trimming is a safe no-op here.

// Supplies the shared WC98 FE palette (RGBA8, 2..256 entries) as the fallback for
// palette indices the file's own palette record cannot cover; an empty span disables it.
void setGlobalPalette(std::span<const std::array<std::uint8_t, 4>> palette);
// Number of colours currently loaded via setGlobalPalette (0 = feature off).
[[nodiscard]] std::size_t globalPaletteSize();

// Global-palette candidate from an archive of PAL#### members
// (FE/PACK/GLOBAL/PAK10042.BIG, matrix section 4): members are decoded as palette
// records and concatenated in archive order, capped at 256 RGBA entries. Returns an
// empty vector when the archive carries no usable palette member.
std::vector<std::array<std::uint8_t, 4>> wc98GlobalPaletteFromPalArchive(std::span<const std::uint8_t> bigBytes);

// One FSH bitmap record decoded to RGBA8 (row-major, top-down, 4 bytes per pixel).
struct FshBitmap {
    std::uint8_t type = 0;   // record type with the 0x80 "RefPack-compressed" bit stripped
    bool compressed = false; // pixel data was RefPack-compressed (type | 0x80)
    std::uint16_t width = 0, height = 0;
    std::int16_t centerX = 0, centerY = 0, posX = 0, posY = 0; // header misc[0..3], signed
    std::uint8_t mipLevels = 0;   // extra mip levels present after the base level (only base is decoded)
    std::uint8_t paletteType = 0; // palette record used (0 = none, 0xFE = global FE palette fallback)
    bool usedGlobalPalette = false; // WC98: indices resolved through the shared FE palette (see below)
    std::vector<std::uint8_t> rgba;
    // Paletted bitmaps (4/8-bit) only: one palette index per pixel and the attached palette as
    // RGBA. FIFA 99 recolours player textures by rewriting palette rows (docs/MATCH_RENDERING.md).
    std::vector<std::uint8_t> indices;
    std::vector<std::array<std::uint8_t, 4>> palette;
};

struct FshEntry {
    std::string tag;     // 4-char directory tag
    std::uint32_t offset = 0;
    std::uint8_t firstType = 0; // type of the first record in the chain
    std::optional<FshBitmap> bitmap; // empty for palette-only entries (e.g. "!tr1")
    std::vector<std::string> comments; // 0x6F text attachments
};

struct FshFile {
    std::uint32_t declaredSize = 0; // header size field; loose CD files carry an extra 8-byte "\0cRC" trailer
    std::string dirId;              // "GIMX" or "MATT" in FIFA 99 data
    std::vector<FshEntry> entries;

    [[nodiscard]] static bool isFsh(std::span<const std::uint8_t> data);
    // Parses the directory and decodes every bitmap; throws FormatError.
    static FshFile parse(std::span<const std::uint8_t> data);
};

// Decodes the record chain starting at `offset` (a 16-byte bitmap header followed by attached
// palette/comment records). `end` bounds the chain. Used by FshFile and by FNTF fonts, whose
// glyph atlas is a bare bitmap record without a SHPI wrapper. Throws FormatError.
FshBitmap decodeFshBitmap(std::span<const std::uint8_t> data, std::size_t offset, std::size_t end);

// WC98 (matrix section 4): palette records attached to a record chain at `offset` (0x01/0x1F/0x21/
// 0x22/0x24/0x27/0x29/0x2A/0x2D/0x2E), as RGBA. FE/PACK/PAL.PAK is a bare SHPI archive whose
// single entry carries a type-0x01 256-colour palette; the shared FE palette is read this way.
std::vector<std::array<std::uint8_t, 4>> fshPaletteRecordsAt(std::span<const std::uint8_t> data,
                                                             std::size_t offset, std::size_t end);

// Pixel-format name for a record type ("8bit-pal", "rgb565", ...), or "" when unknown.
[[nodiscard]] const char* fshTypeName(std::uint8_t type);

} // namespace wc98::formats
