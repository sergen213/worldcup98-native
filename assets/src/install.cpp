// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/assets/install.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace wc98::assets {

namespace {

std::string upper(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = char(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

// Upper-cased names of the direct children of a directory -> real paths.
struct Listing {
    std::vector<std::pair<std::string, fs::directory_entry>> items;
    const fs::directory_entry* find(std::string_view upperName) const {
        for (const auto& [n, e] : items)
            if (n == upperName) return &e;
        return nullptr;
    }
    bool hasDir(std::string_view n) const { auto* e = find(n); std::error_code ec; return e && e->is_directory(ec); }
    bool hasFile(std::string_view n) const { auto* e = find(n); std::error_code ec; return e && e->is_regular_file(ec); }
};

Listing list(const fs::path& dir) {
    Listing l;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        l.items.emplace_back(upper(it->path().filename().string()), *it);
    return l;
}

bool hasMagic(const fs::path& p, std::uint64_t off, std::string_view magic) {
    std::ifstream f(p, std::ios::binary);
    std::string buf(magic.size(), '\0');
    if (!f.seekg(static_cast<std::streamoff>(off)) || !f.read(buf.data(), static_cast<std::streamsize>(buf.size()))) return false;
    return buf == magic;
}

// The WC98 CD root (and the Windows install copy) carries FIFAWC.EXE plus these data dirs;
// any one of them next to the exe identifies a game tree (docs/research/ASSET_INVENTORY.md §2).
constexpr std::array kDataDirs{"AUDIO", "FE", "INGAME", "COMMON"};

} // namespace

std::string_view layoutName(Layout l) {
    switch (l) {
    case Layout::GameTree: return "game-tree";
    case Layout::IsoImage: return "iso-image";
    case Layout::ZippedIso: return "zipped-iso";
    case Layout::Unknown: break;
    }
    return "unknown";
}

Detection detect(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return {Layout::Unknown, p, "does not exist"};
    if (fs::is_regular_file(p, ec)) {
        // Detected for the message only: reading ISO9660 needs libarchive, which this port drops.
        if (hasMagic(p, 0x8001, "CD001"))
            return {Layout::IsoImage, p, "ISO9660 image; extract it with a standard tool and add the folder"};
        if (hasMagic(p, 0, "PK\x03\x04"))
            return {Layout::ZippedIso, p, "ZIP archive; unpack it and add the extracted World Cup 98 folder"};
        return {Layout::Unknown, p, "regular file that is neither an ISO9660 image nor a ZIP"};
    }
    if (!fs::is_directory(p, ec)) return {Layout::Unknown, p, "not a file or directory"};
    Listing l = list(p);
    if (!l.hasFile("FIFAWC.EXE"))
        return {Layout::Unknown, p, "no FIFAWC.EXE at this level (add the World Cup 98 folder itself)"};
    for (const auto* d : kDataDirs)
        if (l.hasDir(d)) return {Layout::GameTree, p, std::string("FIFAWC.EXE + ") + d + "/"};
    return {Layout::Unknown, p, "FIFAWC.EXE without AUDIO/, FE/, INGAME/ or COMMON/"};
}

std::vector<fs::path> commonLocations() {
    std::vector<fs::path> out;
    const char* home = std::getenv("HOME");
    const char* user = std::getenv("USER");
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (home && *home) out.push_back(fs::path(home) / "Games");
    if (user && *user) {
        for (const char* base : {"/run/media/", "/media/"}) {
            std::error_code ec;
            for (fs::directory_iterator it(fs::path(base) / user, ec), end; !ec && it != end; it.increment(ec))
                out.push_back(it->path());
        }
    }
    if (xdg && *xdg && fs::path(xdg).is_absolute()) out.push_back(fs::path(xdg) / "wc98-native" / "game");
    else if (home && *home) out.push_back(fs::path(home) / ".local/share/wc98-native/game");
    return out;
}

std::vector<Detection> searchCommonLocations() {
    std::vector<Detection> found;
    auto consider = [&](const fs::path& p) {
        Detection d = detect(p);
        if (d.layout != Layout::Unknown) found.push_back(std::move(d));
        return d.layout != Layout::Unknown;
    };
    auto children = [](const fs::path& dir) {
        std::vector<fs::path> c;
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::string ext = upper(it->path().extension().string());
            if (it->is_directory(ec) || ext == ".ISO" || ext == ".ZIP") c.push_back(it->path());
        }
        std::ranges::sort(c);
        return c;
    };
    for (const auto& loc : commonLocations()) {
        std::error_code ec;
        if (!fs::is_directory(loc, ec) || consider(loc)) continue;
        for (const auto& c : children(loc)) // depth 2: ~/Games/<game>/<install>
            if (!consider(c) && fs::is_directory(c, ec))
                for (const auto& g : children(c)) consider(g);
    }
    return found;
}

std::string normalizeGamePath(std::string_view p) {
    std::string s(p);
    std::ranges::replace(s, '\\', '/');
    while (s.starts_with("./")) s.erase(0, 2);
    while (s.starts_with('/')) s.erase(0, 1);
    if (s.empty()) return {};
    std::string out;
    std::size_t start = 0;
    while (true) {
        std::size_t end = s.find('/', start);
        std::string_view part = std::string_view(s).substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == ".." || part.find(':') != part.npos || part.find('\0') != part.npos) return {};
        if (!out.empty()) out += '/';
        out += upper(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

bool isExecutableName(std::string_view name) {
    auto dot = name.rfind('.');
    if (dot == std::string_view::npos) return false;
    static constexpr std::array exts{".EXE", ".DLL", ".COM", ".BAT", ".CMD", ".PIF", ".SCR", ".VXD",
                                     ".DRV", ".SYS", ".CPL", ".OCX", ".MSI", ".LNK", ".VBS", ".JS"};
    std::string e = upper(name.substr(dot));
    return std::ranges::find(exts, e) != exts.end();
}

std::string sha256Hex(const unsigned char* data, std::size_t size) {
    Sha256 h;
    h.update(data, size);
    return h.hex();
}

std::string sha256File(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    Sha256 h;
    std::vector<char> buf(1 << 20);
    while (f.read(buf.data(), static_cast<std::streamsize>(buf.size())) || f.gcount() > 0)
        h.update(buf.data(), static_cast<std::size_t>(f.gcount()));
    return h.hex();
}

} // namespace wc98::assets
