// Derived from fifa99-native (same author), ported for World Cup 98
#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/ffn.hpp"
#include <filesystem>
#include <vector>

using namespace wc98::formats;
using Bytes = std::vector<std::uint8_t>;

static void le16(Bytes& b, unsigned v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }

static void synthetic() {
    // FNTF with one glyph 'A' (2x1 at 1,0) and a 4x1 4-bit atlas record.
    Bytes b = {'F', 'N', 'T', 'F', 0, 0, 0, 0};
    le16(b, 100); le16(b, 1); le16(b, 9); le16(b, 0); le16(b, 0); le16(b, 12); le16(b, 0x20); le16(b, 0); le16(b, 0); le16(b, 0);
    le16(b, 0x2B); le16(b, 0);                 // atlas offset = 0x20 + 11
    le16(b, 'A'); b.push_back(2); b.push_back(1); le16(b, 1); le16(b, 0); b.push_back(3); b.push_back(0xFF); b.push_back(2);
    b.push_back(0x7A); b.push_back(0); b.push_back(0); b.push_back(0); le16(b, 4); le16(b, 1); for (int i = 0; i < 8; ++i) b.push_back(0);
    b.push_back(0x0F); b.push_back(0x80);      // pixels 0,15,8,0
    b[4] = std::uint8_t(b.size());
    auto f = FfnFont::parse(b);
    CHECK(f.version == 100 && f.lineHeight == 12 && f.glyphs.size() == 1);
    const auto& g = f.glyphs[0];
    CHECK(g.code == 'A' && g.width == 2 && g.x == 1 && g.advance == 3 && g.xOffset == -1 && g.yOffset == 2);
    CHECK(f.atlas.width == 4 && f.atlas.rgba[3] == 0 && f.atlas.rgba[7] == 255 && f.atlas.rgba[11] == 8 * 17);
    b[0x20 + 4] = 3; // glyph x=3 + width 2 exceeds the 4-pixel atlas
    CHECK_THROWS(FfnFont::parse(b));
    CHECK_THROWS(FfnFont::parse(Bytes(b.begin(), b.begin() + 40)));
}

static void original() {
    auto root = treeDirOrSkip();
    // WC98 gate: all 10 loose .FFN fonts parse (inventory doc: FE/ERAS*.FFN, 10 files).
    int n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
        if (!e.is_regular_file() || e.path().extension() != ".FFN") continue;
        auto d = wc98::readFile(e.path());
        if (!FfnFont::isFfn(d)) continue; // the tree has no BIGF-wrapped fonts (unlike FIFA 99)
        auto ff = FfnFont::parse(d);
        CHECK(!ff.glyphs.empty());
        CHECK(ff.atlas.width > 0 && ff.atlas.height > 0 && ff.atlas.rgba.size() == std::size_t(ff.atlas.width) * ff.atlas.height * 4);
        ++n;
    }
    std::fprintf(stderr, "%d loose FNTF fonts parsed\n", n);
    CHECK(n == 10);
}

int main() {
    synthetic();
    original();
    return 0;
}
