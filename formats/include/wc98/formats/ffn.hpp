// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA "FNTF" bitmap font (.FFN): header, 11-byte glyph table, embedded 4-bit FSH bitmap record.
// See docs/formats/FFN.md.
#include "wc98/formats/fsh.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {

struct FfnGlyph {
    std::uint16_t code = 0;          // character code (cp1252)
    std::uint8_t width = 0, height = 0;
    std::uint16_t x = 0, y = 0;      // top-left in the atlas
    std::uint8_t advance = 0;        // pen advance (hypothesis, see FFN.md)
    std::int8_t xOffset = 0, yOffset = 0;
};

struct FfnFont {
    std::uint32_t declaredSize = 0;
    std::uint16_t version = 0;    // 100 in all FIFA 99 fonts
    std::uint16_t flags = 0;      // unknown (0, 1, 9, 11 observed)
    std::uint16_t lineHeight = 0; // hypothesis: line spacing in pixels
    std::vector<FfnGlyph> glyphs;
    FshBitmap atlas;              // RGBA, white with alpha = 4-bit intensity

    [[nodiscard]] static bool isFfn(std::span<const std::uint8_t> data);
    static FfnFont parse(std::span<const std::uint8_t> data); // throws FormatError
};

} // namespace wc98::formats
