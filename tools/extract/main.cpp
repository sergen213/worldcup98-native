// Derived from fifa99-native (same author), ported for World Cup 98
// wc98-extract: unpack original World Cup 98 data into an output directory.
//   BIG/VIV members -> <file>.d/<member> (48-byte-prefixed WC98 archives -> <file>.p48.d),
//   RefPack payloads decompressed,
//   FSH -> <file>.png/<index>_<tag>.png + fsh.json, FFN -> <file>.png + <file>.json,
//   DBI / FC*.BIN -> <file>.json. Never writes inside the source directory.
#include "common/identify.hpp"
#include "common/json.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/fcdb.hpp"
#include "wc98/formats/ffn.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/png.hpp"
#include "wc98/formats/refpack.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace wc98;
using namespace wc98::formats;
using tools::jstr;

namespace {

struct Stats {
    int files = 0, members = 0, refpack = 0, fsh = 0, fshFailed = 0, images = 0, paletteOnly = 0, fonts = 0, dbi = 0, errors = 0;
    std::vector<std::string> failures;
} st;

void put(const fs::path& p, std::string_view s) { writeFile(p, std::span(reinterpret_cast<const std::uint8_t*>(s.data()), s.size())); }
void put(const fs::path& p, std::span<const std::uint8_t> d) { writeFile(p, d); }

// BIG member names use '\\' and may contain anything; keep them inside the output tree.
fs::path safeRelative(std::string name) {
    for (auto& c : name) if (c == '\\' || c == ':') c = '/';
    fs::path out;
    for (const auto& part : fs::path(name)) {
        auto s = part.string();
        if (s.empty() || s == "/" || s == "." || s == "..") continue;
        out /= s;
    }
    return out.empty() ? fs::path("_unnamed") : out;
}

std::string u8(std::string_view s) { return cp1252ToUtf8(s); }

void exportFsh(std::span<const std::uint8_t> d, const fs::path& base, const std::string& label) {
    ++st.fsh;
    FshFile f;
    try {
        f = FshFile::parse(d);
    } catch (const std::exception& e) {
        ++st.fshFailed;
        st.failures.push_back(label + ": " + e.what());
        return;
    }
    fs::path dir = base; dir += ".png";
    std::string j = std::format("{{\"dirId\":{},\"declaredSize\":{},\"entries\":[", jstr(f.dirId), f.declaredSize);
    for (std::size_t i = 0; i < f.entries.size(); ++i) {
        const auto& e = f.entries[i];
        std::string tag = safeRelative(e.tag).string();
        j += std::format("{}{{\"tag\":{},\"offset\":{},\"firstType\":{}", i ? "," : "", jstr(u8(e.tag)), e.offset, e.firstType);
        if (e.bitmap) {
            const auto& b = *e.bitmap;
            std::string png = std::format("{:03}_{}.png", i, tag);
            put(dir / png, encodePng(b.width, b.height, b.rgba));
            ++st.images;
            j += std::format(",\"png\":{},\"type\":{},\"format\":{},\"compressed\":{},\"width\":{},\"height\":{},\"center\":[{},{}],"
                             "\"pos\":[{},{}],\"mipLevels\":{},\"paletteType\":{}",
                             jstr(png), b.type, jstr(fshTypeName(b.type)), b.compressed, b.width, b.height, b.centerX, b.centerY,
                             b.posX, b.posY, b.mipLevels, b.paletteType);
        } else {
            ++st.paletteOnly;
        }
        if (!e.comments.empty()) {
            j += ",\"comments\":[";
            for (std::size_t k = 0; k < e.comments.size(); ++k) j += (k ? "," : "") + jstr(u8(e.comments[k]));
            j += "]";
        }
        j += "}";
    }
    put(dir / "fsh.json", j + "]}\n");
}

void exportFfn(std::span<const std::uint8_t> d, const fs::path& base) {
    auto f = FfnFont::parse(d);
    ++st.fonts;
    fs::path png = base; png += ".png";
    fs::path js = base; js += ".json";
    put(png, encodePng(f.atlas.width, f.atlas.height, f.atlas.rgba));
    std::string j = std::format("{{\"version\":{},\"flags\":{},\"lineHeight\":{},\"atlas\":{{\"png\":{},\"width\":{},\"height\":{}}},\"glyphs\":[",
                                f.version, f.flags, f.lineHeight, jstr(png.filename().string()), f.atlas.width, f.atlas.height);
    for (std::size_t i = 0; i < f.glyphs.size(); ++i) {
        const auto& g = f.glyphs[i];
        char c = char(g.code);
        j += std::format("{}{{\"code\":{},\"char\":{},\"x\":{},\"y\":{},\"w\":{},\"h\":{},\"advance\":{},\"xOffset\":{},\"yOffset\":{}}}",
                         i ? "," : "", g.code, jstr(g.code < 256 ? u8(std::string_view(&c, 1)) : ""), g.x, g.y, g.width, g.height,
                         g.advance, g.xOffset, g.yOffset);
    }
    put(js, j + "]}\n");
}

std::string strArray(const std::vector<std::string>& v) {
    std::string j = "[";
    for (std::size_t i = 0; i < v.size(); ++i) j += (i ? "," : "") + jstr(u8(v[i]));
    return j + "]";
}

void exportDbi(std::span<const std::uint8_t> d, const fs::path& base) {
    ++st.dbi;
    fs::path js = base; js += ".json";
    std::string j;
    if (std::string_view(reinterpret_cast<const char*>(d.data()), 4) == "FCDB") {
        auto db = FcdbDatabase::parse(d);
        j = "{\"leagues\":[";
        for (std::size_t i = 0; i < db.leagues.size(); ++i) {
            const auto& l = db.leagues[i];
            j += std::format("{}{{\"id\":{},\"field12\":{},\"flag24\":{},\"flag25\":{},\"teams\":[", i ? "," : "", l.id, l.field12, l.flag24, l.flag25);
            for (std::size_t k = 0; k < l.teamIds.size(); ++k) j += std::format("{}{}", k ? "," : "", l.teamIds[k]);
            j += "]}";
        }
        j += "],\"teams\":[";
        for (std::size_t i = 0; i < db.teams.size(); ++i) {
            const auto& t = db.teams[i];
            j += std::format("{}{{\"id\":{},\"budget\":{},\"isClub\":{},\"nationality\":{},\"rival\":{},\"roster\":[", i ? "," : "", t.id, t.budget, t.isClub, t.nationality, t.rivalTeamId);
            for (std::size_t k = 0; k < t.roster.size(); ++k) j += std::format("{}{}", k ? "," : "", t.roster[k]);
            j += "],\"raw\":[";
            for (std::size_t k = 0; k < t.raw.size(); ++k) j += std::format("{}{}", k ? "," : "", t.raw[k]);
            j += "]}";
        }
        j += "],\"players\":[";
        for (std::size_t i = 0; i < db.players.size(); ++i) {
            const auto& p = db.players[i];
            j += std::format("{}{{\"id\":{},\"nationality\":{},\"overall\":{},\"value\":{},\"position\":{},\"position2\":{},\"shirt\":{},\"attributes\":[",
                             i ? ",\n" : "\n", p.id, p.nationality, p.overall, p.value, p.position, p.position2, p.shirtNumber);
            for (std::size_t k = 0; k < p.attributes.size(); ++k) j += std::format("{}{}", k ? "," : "", p.attributes[k]);
            j += "],\"raw\":[";
            for (std::size_t k = 0; k < p.raw.size(); ++k) j += std::format("{}{}", k ? "," : "", p.raw[k]);
            j += "]}";
        }
        j += "]}\n";
    } else {
        auto n = FcdbNames::parse(d);
        j = "{\"magic\":" + jstr(n.magic) + ",\"leagues\":" + strArray(n.leagues) + ",\"players\":" + strArray(n.players) + ",\"teams\":[";
        for (std::size_t i = 0; i < n.teams.size(); ++i)
            j += std::format("{}{{\"abbr\":{},\"short\":{},\"full\":{}}}", i ? "," : "", jstr(u8(n.teams[i].abbreviation)),
                             jstr(u8(n.teams[i].shortName)), jstr(u8(n.teams[i].fullName)));
        j += "]}\n";
    }
    put(js, j);
}

void process(std::span<const std::uint8_t> raw, const fs::path& base, const std::string& label, bool member, int depth) {
    std::vector<std::uint8_t> unpacked;
    std::span<const std::uint8_t> d = raw;
    try {
        if (refpack::isCompressed(raw)) {
            unpacked = refpack::decompress(raw);
            d = unpacked;
            ++st.refpack;
            fs::path up = base; up += ".unpacked";
            put(up, d);
        }
        if (member) put(base, raw);
        std::string fmt = tools::identify(d);
        if (fmt == "BIG" && depth < 8) {
            std::size_t pfx = tools::bigBase(d); // WC98: BIGF at 0, or at 0x30 behind a 48-byte prefix
            const std::span<const std::uint8_t> body(d.data() + pfx, d.size() - pfx);
            auto big = BigArchive::parse(body);
            fs::path dir = base; dir += pfx ? ".p48.d" : ".d";
            for (const auto& e : big.entries) {
                ++st.members;
                process(BigArchive::payload(body, e), dir / safeRelative(e.name), label + "::" + e.name, true, depth + 1);
            }
        } else if (fmt == "FSH") {
            exportFsh(d, base, label);
        } else if (fmt == "FFN") {
            exportFfn(d, base);
        } else if (fmt == "DBI") {
            exportDbi(d, base);
        } else if (fmt == "FCLG") {
            fs::path js = base; js += ".json";
            put(js, "{\"strings\":" + strArray(FclgStrings::parse(d).strings) + "}\n");
        }
    } catch (const std::exception& e) {
        ++st.errors;
        st.failures.push_back(label + ": " + e.what());
    }
}

bool isWithin(fs::path p, fs::path root) {
    p = fs::weakly_canonical(p);
    root = fs::weakly_canonical(root);
    auto [a, b] = std::mismatch(root.begin(), root.end(), p.begin(), p.end());
    return a == root.end();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: wc98-extract <game-dir-or-file> <output-dir>\n");
        return 2;
    }
    fs::path src = argv[1], out = argv[2];
    fs::path srcRoot = fs::is_directory(src) ? src : src.parent_path();
    if (isWithin(out, srcRoot)) {
        std::fprintf(stderr, "refusing to write into the source directory (%s)\n", srcRoot.c_str());
        return 2;
    }
    // WC98 delta (matrix section 4): bitmaps index past their attached palette record and
    // 15 of 1204 FSH files use the shared FE palette archive; load it before any decode.
    if (fs::is_directory(srcRoot)) {
        fs::path pal = findCaseInsensitive(srcRoot, "FE/PACK/PAL.PAK");
        if (pal.empty())
            for (const auto& e : fs::recursive_directory_iterator(srcRoot))
                if (e.is_regular_file() && e.path().filename() == "PAL.PAK") { pal = e.path(); break; }
        if (!pal.empty()) {
            try {
                setGlobalPalette(wc98GlobalPaletteFromPalArchive(readFile(pal)));
                std::fprintf(stderr, "global palette: %s (%zu colors)\n", pal.c_str(), globalPaletteSize());
            } catch (const std::exception& e) {
                std::fprintf(stderr, "global palette %s failed: %s\n", pal.c_str(), e.what());
            }
        } else {
            std::fprintf(stderr, "warning: no PAL.PAK palette archive found; global-palette FSH files will fail\n");
        }
    }
    std::vector<fs::path> files;
    if (fs::is_directory(src)) {
        for (const auto& e : fs::recursive_directory_iterator(src))
            if (e.is_regular_file()) files.push_back(e.path());
    } else {
        files.push_back(src);
    }
    std::ranges::sort(files);
    for (const auto& f : files) {
        ++st.files;
        auto d = readFile(f);
        auto rel = fs::relative(f, srcRoot);
        process(d, out / rel, rel.generic_string(), false, 0);
    }
    std::string report = std::format("{} files, {} BIG members, {} RefPack payloads\nFSH: {} files, {} failed, {} images decoded, {} palette-only entries\n"
                                     "FFN: {} fonts; DBI: {}; errors: {}\n",
                                     st.files, st.members, st.refpack, st.fsh, st.fshFailed, st.images, st.paletteOnly, st.fonts, st.dbi, st.errors);
    for (const auto& f : st.failures) report += "FAIL " + f + "\n";
    put(out / "extract-report.txt", report);
    std::fputs(report.c_str(), stderr);
    return st.fshFailed || st.errors ? 1 : 0;
}
