// Derived from fifa99-native (same author), ported for World Cup 98
#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/png.hpp"
#include "wc98/formats/refpack.hpp"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <filesystem>
#include <string>
#include <vector>

using namespace wc98::formats;
using Bytes = std::vector<std::uint8_t>;

static void le16(Bytes& b, unsigned v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }
static void le32(Bytes& b, std::uint32_t v) { le16(b, v & 0xFFFF); le16(b, v >> 16); }
static void rec(Bytes& b, std::uint8_t type, std::uint32_t next, unsigned w, unsigned h, unsigned m3 = 0) {
    b.push_back(type); b.push_back(std::uint8_t(next)); b.push_back(std::uint8_t(next >> 8)); b.push_back(std::uint8_t(next >> 16));
    le16(b, w); le16(b, h); le16(b, 0xFFFA); le16(b, 2); le16(b, 3); le16(b, m3);
}

static void synthetic() {
    // Directory: three entries built by hand (independent of game data).
    Bytes b = {'S', 'H', 'P', 'I'};
    le32(b, 0); le32(b, 3);
    for (char c : std::string("GIMX")) b.push_back(std::uint8_t(c));
    std::size_t dir = b.size();
    b.resize(dir + 24);
    auto entry = [&](int i, const char* tag) {
        std::memcpy(&b[dir + 8 * i], tag, 4);
        std::uint32_t o = std::uint32_t(b.size());
        std::memcpy(&b[dir + 8 * i + 4], &o, 4); // little-endian host
    };
    // 1) 8-bit 2x1 + 0x2A palette (B,G,R,A) + comment
    entry(0, "pal8");
    rec(b, 0x7B, 16 + 4, 2, 1); b.push_back(1); b.push_back(0); b.push_back(0); b.push_back(0);
    rec(b, 0x2A, 16 + 8, 2, 1); for (std::uint8_t v : {10, 20, 30, 40, 0xFF, 0x80, 0x00, 0xFF}) b.push_back(v);
    b.push_back(0x6F); b.push_back(0); b.push_back(0); b.push_back(0); le32(b, 3); for (char c : std::string("hi!")) b.push_back(std::uint8_t(c));
    // 2) RGB565 1x1 pure red, 0x24 palette is RGB order
    entry(1, "r565");
    rec(b, 0x78, 0, 1, 1); le16(b, 0xF800); le16(b, 0);
    // 3) RefPack-compressed 32-bit 1x1 (type 0x7D|0x80): literal run of 4 bytes B,G,R,A
    entry(2, "cmp1");
    rec(b, 0xFD, 0, 1, 1);
    for (std::uint8_t v : {0x10, 0xFB, 0, 0, 4, 0xE0, 0x01, 0x02, 0x03, 0x04, 0xFC}) b.push_back(v);
    std::uint32_t size = std::uint32_t(b.size());
    std::memcpy(&b[4], &size, 4);

    auto f = FshFile::parse(b);
    CHECK(f.dirId == "GIMX" && f.entries.size() == 3);
    const auto& p = *f.entries[0].bitmap;
    CHECK(p.width == 2 && p.height == 1 && p.paletteType == 0x2A && p.centerX == -6 && p.centerY == 2 && p.posX == 3);
    CHECK(p.rgba == (Bytes{0x00, 0x80, 0xFF, 0xFF, 30, 20, 10, 40}));
    CHECK(f.entries[0].comments.size() == 1 && f.entries[0].comments[0] == "hi!");
    CHECK(f.entries[1].bitmap->rgba == (Bytes{255, 0, 0, 255}));
    const auto& c = *f.entries[2].bitmap;
    CHECK(c.compressed && c.type == 0x7D && c.rgba == (Bytes{0x03, 0x02, 0x01, 0x04}));

    // 4-bit (high nibble = left pixel) with a 0x24 R,G,B palette, standalone record.
    Bytes r;
    rec(r, 0x7A, 16 + 1 + 3, 2, 1); r.push_back(0x10); r.resize(20);
    rec(r, 0x24, 0, 2, 1); for (std::uint8_t v : {1, 2, 3, 4, 5, 6}) r.push_back(v);
    auto nb = decodeFshBitmap(r, 0, r.size());
    CHECK(nb.rgba == (Bytes{4, 5, 6, 255, 1, 2, 3, 255}));

    // Malformed: truncated pixels, out-of-range palette index, bad entry offset.
    Bytes t = b; t[dir + 12] = 0xF0; // entry 1 offset -> outside
    CHECK_THROWS(FshFile::parse(t));
    Bytes q = b; q[f.entries[0].offset + 16] = 7; // index 7 >= 2 palette entries
    CHECK_THROWS(FshFile::parse(q));
    Bytes s(b.begin(), b.begin() + f.entries[1].offset + 17); // 565 pixel cut in half
    CHECK_THROWS(decodeFshBitmap(s, f.entries[1].offset, s.size()));
}

struct Count { int files = 0, images = 0, failed = 0, global = 0, truncatedArchives = 0; std::vector<std::string> failures; };

static void scan(std::span<const std::uint8_t> raw, Count& n, int depth) {
    std::vector<std::uint8_t> un;
    std::span<const std::uint8_t> d = raw;
    if (refpack::isCompressed(raw)) { un = refpack::decompress(raw); d = un; }
    // WC98 archives (matrix section 3): BIGF at offset 0, or at 0x30 behind a 48-byte prefix.
    std::size_t base = BigArchive::isBig(d) ? 0 : (d.size() > 0x34 && BigArchive::isBig(d.subspan(0x30)) ? 0x30 : 0);
    if (depth < 8 && (BigArchive::isBig(d) || base) && (!BigArchive::isBig(d) || base || true)) {
        if (BigArchive::isBig(d.subspan(base))) {
            try {
                auto a = BigArchive::parse(d.subspan(base));
                for (const auto& e : a.entries) scan(BigArchive::payload(d.subspan(base), e), n, depth + 1);
            } catch (const std::exception&) { ++n.truncatedArchives; } // INSTALL ships header-only PBK stubs
            return;
        }
    }
    if (FshFile::isFsh(d)) {
        ++n.files;
        try {
            for (const auto& e : FshFile::parse(d).entries) {
                n.images += e.bitmap.has_value();
                n.global += e.bitmap && e.bitmap->usedGlobalPalette;
            }
        } catch (const std::exception& e) {
            ++n.failed;
            if (n.failures.size() < 25) n.failures.push_back(e.what());
        }
    }
}

static void original() {
    auto root = treeDirOrSkip();
    namespace fs = std::filesystem;
    // WC98 delta (matrix section 4): bitmaps index past their attached palette record and
    // 15 of 1204 files reference the shared FE palette archive; load it before decoding.
    fs::path pal;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file() && e.path().filename() == "PAL.PAK") { pal = e.path(); break; }
    CHECK(!pal.empty());
    setGlobalPalette(wc98GlobalPaletteFromPalArchive(wc98::readFile(pal)));
    CHECK(globalPaletteSize() > 0);
    std::fprintf(stderr, "global palette from %s: %zu colours\n", pal.filename().c_str(), globalPaletteSize());

    // Every SHPI in the tree (loose .FSH, RefPack .PAK, BIG/VIV members, nested archives)
    // must decode. Gate: >= 1184 of the 1204-file FSH universe, ~6356 bitmaps.
    Count n;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) scan(wc98::readFile(e.path()), n, 0);
    std::fprintf(stderr, "SHPI files %d, images %d, failed %d, global-palette bitmaps %d, truncated archives %d\n", n.files, n.images, n.failed, n.global, n.truncatedArchives);
    for (const auto& f : n.failures) std::fprintf(stderr, "  FSH failure: %s\n", f.c_str());
    CHECK(n.files >= 1204);
    CHECK(n.failed <= 20 && n.images >= 6356);

    // WC98 palette type 0x2D / 0x29 (16-bit FE palettes, new in WC98): FE/PACK/FEFLOW02.
    auto flow = wc98::findCaseInsensitive(root, "FE/FEFLOW02/FEFLOW02.FSH");
    CHECK(!flow.empty());
    bool saw2D = false;
    for (const auto& e : FshFile::parse(wc98::readFile(flow)).entries)
        if (e.bitmap && (e.bitmap->paletteType == 0x2D || e.bitmap->paletteType == 0x29)) saw2D = true;
    CHECK(saw2D);

    // Global-palette fallback: FE/GLOBAL/CONTR.FSH indexes past its own palette.
    auto contr = wc98::findCaseInsensitive(root, "FE/GLOBAL/CONTR.FSH");
    if (!contr.empty()) {
        bool usedGlobal = false;
        for (const auto& e : FshFile::parse(wc98::readFile(contr)).entries) usedGlobal = usedGlobal || (e.bitmap && e.bitmap->usedGlobalPalette);
        CHECK(usedGlobal);
    }

    // PNG sample: a few decoded bitmaps (incl. one global-palette file) written as PNG.
    fs::path out = std::getenv("WC98_TEST_OUT") ? std::getenv("WC98_TEST_OUT") : fs::temp_directory_path() / "wc98port-out" / "png";
    fs::create_directories(out);
    int pngs = 0;
    for (const char* rel : {"FE/GLOBAL/CONTR.FSH", "FE/FEFLOW02/FEFLOW02.FSH", "INGAME/PLAYER/TEXTURES/PALJER.FSH"}) {
        auto p = wc98::findCaseInsensitive(root, rel);
        if (p.empty()) continue;
        for (const auto& e : FshFile::parse(wc98::readFile(p)).entries) {
            if (!e.bitmap || e.bitmap->rgba.empty()) continue;
            auto png = encodePng(e.bitmap->width, e.bitmap->height, e.bitmap->rgba);
            auto tag = e.tag.substr(0, e.tag.find_last_not_of(" \0") + 1);
            wc98::writeFile(out / std::format("{}_{}.png", p.stem().string(), tag), std::span(png.data(), png.size()));
            if (++pngs >= 6) break;
        }
    }
    std::fprintf(stderr, "PNG sample: %d bitmaps written to %s\n", pngs, out.c_str());
    CHECK(pngs >= 3);

    // M4 slice 1 loose-file census (2026-11-02): every *.FSH on disk parses standalone.
    // Header facts measured over all 411 files: magic "SHPI", dirId "GIMX" and
    // declaredSize == fileSize in 411/411; first-record pixel types {0x7B,0x78,0x6D,0x7D},
    // 306 of them RefPack-compressed (type | 0x80); attached palettes dominated by 0x2A.
    int loose = 0, imgs = 0, cmps = 0, pal2A = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (!e.is_regular_file() || ext != ".fsh") continue;
        ++loose;
        const auto d = wc98::readFile(e.path());
        const FshFile f = FshFile::parse(d); // a throw here is a decode regression: fail the gate
        CHECK(f.dirId == "GIMX");
        CHECK(f.declaredSize == d.size());
        for (const auto& en : f.entries)
            if (en.bitmap) { ++imgs; cmps += en.bitmap->compressed; pal2A += en.bitmap->paletteType == 0x2A; }
    }
    std::fprintf(stderr, "loose .FSH census: %d files, %d bitmaps (%d RefPack-compressed, %d on attached 0x2A palettes)\n",
                 loose, imgs, cmps, pal2A);
    CHECK(loose >= 307 && imgs >= 4100 && cmps >= 300 && pal2A >= 3000);

    // Kit-texture targets (docs/development/MILESTONE4.md loader map): a per-team jersey atlas,
    // a keeper kit and the PALJER swap set must decode with their documented SHPI tags.
    auto hasTag = [](const fs::path& rel, const char* tag) {
        const auto p = wc98::findCaseInsensitive(treeDirOrSkip(), rel);
        if (p.empty()) return false; // WC98_TREE gate already ran; missing file fails the CHECK
        for (const auto& e : FshFile::parse(wc98::readFile(p)).entries) {
            std::string t = e.tag.substr(0, e.tag.find_last_not_of(" \t\0") + 1);
            if (e.bitmap && t == tag) return true;
        }
        return false;
    };
    CHECK(hasTag("INGAME/PLAYER/TEXTURES/PK026A.FSH", "hlsv")); // per-team home kit shirt panel
    CHECK(hasTag("INGAME/PLAYER/TEXTURES/GK00_30.FSH", "hrsv")); // keeper kit (GK%.2d_%c loader pattern)
    CHECK(hasTag("INGAME/PLAYER/TEXTURES/PALJER.FSH", "1600"));  // jersey palette-swap entry
}

int main() {
    synthetic();
    original();
    return 0;
}
