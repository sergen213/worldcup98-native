// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/ffn.hpp"
#include "wc98/reader.hpp"
#include <format>

namespace wc98::formats {

bool FfnFont::isFfn(std::span<const std::uint8_t> d) {
    return d.size() >= 32 && d[0] == 'F' && d[1] == 'N' && d[2] == 'T' && d[3] == 'F';
}

FfnFont FfnFont::parse(std::span<const std::uint8_t> data) {
    if (!isFfn(data)) throw FormatError("not an FNTF font");
    Reader r(data, 4);
    FfnFont f;
    f.declaredSize = r.u32le();
    f.version = r.u16le();
    std::uint16_t count = r.u16le();
    f.flags = r.u16le();
    r.skip(4); // always zero
    f.lineHeight = r.u16le();
    std::uint32_t glyphOffset = r.u16le();
    r.skip(6); // always zero
    std::uint32_t atlasOffset = r.u32le();
    if (glyphOffset != 0x20 || std::uint64_t(glyphOffset) + 11ull * count > atlasOffset || atlasOffset + 16 > data.size())
        throw FormatError(std::format("FNTF tables out of range (glyphs @{} x{}, atlas @{})", glyphOffset, count, atlasOffset));
    r.seek(glyphOffset);
    for (std::uint16_t i = 0; i < count; ++i) {
        FfnGlyph g;
        g.code = r.u16le();
        g.width = r.u8();
        g.height = r.u8();
        g.x = r.u16le();
        g.y = r.u16le();
        g.advance = r.u8();
        g.xOffset = std::int8_t(r.u8());
        g.yOffset = std::int8_t(r.u8());
        f.glyphs.push_back(g);
    }
    // The declared size can be a few bytes short of the 4-bit atlas (ERAS*BI, HJNUM, MJNUM); the
    // remaining bytes are present in the file, so bound by the data instead. The atlas is a 4-bit
    // INTENSITY map (index = coverage), not a palette lookup: decode it through the luminance ramp.
    f.atlas = decodeFshBitmap(data, atlasOffset, data.size(), /*intensityRamp=*/true);
    for (const auto& g : f.glyphs)
        if (g.x + g.width > f.atlas.width || g.y + g.height > f.atlas.height)
            throw FormatError(std::format("FNTF glyph {} outside {}x{} atlas", g.code, f.atlas.width, f.atlas.height));
    return f;
}

} // namespace wc98::formats
