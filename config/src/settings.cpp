#include "wc98/config/settings.hpp"

#include "wc98/platform/paths.hpp"


#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace wc98::config {
namespace fs = std::filesystem;

namespace {


constexpr std::string_view kPresentation[] = {"original", "enhanced"};
constexpr std::string_view kAspect[] = {"auto", "4:3", "16:9", "16:10", "21:9"};
constexpr std::string_view kWindow[] = {"windowed", "borderless", "fullscreen"};
constexpr std::string_view kFilter[] = {"nearest", "linear", "anisotropic"};
constexpr std::string_view kBackend[] = {"auto", "vulkan"};


std::string trim(std::string_view s) {
    auto b = s.find_first_not_of(" \t\r");
    if (b == std::string_view::npos) return {};
    auto e = s.find_last_not_of(" \t\r");
    return std::string(s.substr(b, e - b + 1));
}
std::string lower(std::string_view s) {
    std::string r(s);
    std::ranges::transform(r, r.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return r;
}

template <class E, std::size_t N> std::optional<E> enumFrom(const std::string_view (&names)[N], std::string_view v) {
    auto l = lower(v);
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == l) return E(i);
    return std::nullopt;
}
template <std::size_t N> std::string choices(const std::string_view (&names)[N]) {
    std::string r;
    for (auto n : names) r += (r.empty() ? "" : "|") + std::string(n);
    return r;
}

std::optional<bool> parseBool(std::string_view v) {
    auto l = lower(v);
    if (l == "true" || l == "yes" || l == "on" || l == "1") return true;
    if (l == "false" || l == "no" || l == "off" || l == "0") return false;
    return std::nullopt;
}
std::optional<long> parseInt(std::string_view v) {
    long r = 0;
    auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), r);
    if (ec != std::errc{} || p != v.data() + v.size()) return std::nullopt;
    return r;
}


} // namespace


std::string_view name(Presentation v) { return kPresentation[std::size_t(v)]; }
std::string_view name(Aspect v) { return kAspect[std::size_t(v)]; }
std::string_view name(WindowMode v) { return kWindow[std::size_t(v)]; }
std::string_view name(TextureFilter v) { return kFilter[std::size_t(v)]; }
std::string_view name(Backend v) { return kBackend[std::size_t(v)]; }

// ---- parsing ----------------------------------------------------------------------------------

namespace {

struct Parser {
    LoadResult r;
    Settings& s = r.settings;
    std::size_t line = 0;
    bool sawData = false;

    void warn(std::string_view section, std::string_view key, std::string_view msg) {
        r.warnings.push_back(std::format("line {}: [{}] {}: {}", line, section, key, msg));
    }
    void keep(std::string_view section, std::string_view key, std::string_view value) {
        auto it = std::ranges::find(s.unknown, section, &Section::name);
        if (it == s.unknown.end()) it = s.unknown.insert(s.unknown.end(), Section{std::string(section), {}});
        it->entries.push_back({std::string(key), std::string(value)});
    }
    template <std::size_t N, class E>
    void setEnum(std::string_view sec, std::string_view k, std::string_view v, const std::string_view (&names)[N],
                 E& out) {
        if (auto e = enumFrom<E>(names, v))
            out = *e;
        else
            warn(sec, k,
                 std::format("invalid value '{}' (expected {}), using '{}'", v, choices(names),
                             names[std::size_t(out)]));
    }
    void setBool(std::string_view sec, std::string_view k, std::string_view v, bool& out) {
        if (auto b = parseBool(v))
            out = *b;
        else
            warn(sec, k, std::format("invalid value '{}' (expected true|false), using '{}'", v, out));
    }
    void setVolume(std::string_view sec, std::string_view k, std::string_view v, int& out) {
        auto n = parseInt(v);
        if (n && *n >= 0 && *n <= 100)
            out = int(*n);
        else
            warn(sec, k, std::format("invalid value '{}' (expected 0..100), using {}", v, out));
    }

    void entry(const std::string& sec, const std::string& k, const std::string& v) {
        if (sec == "meta") {
            if (k == "version") {
                if (auto n = parseInt(v); n && *n >= 0)
                    r.fileVersion = int(*n);
                else
                    warn(sec, k, std::format("invalid version '{}', treating as {}", v, kVersion));
            } else
                keep(sec, k, v);
        } else if (sec == "game") {
            if (k == "data") {
                if (!sawData) s.game.data.clear(), sawData = true;
                if (!v.empty()) s.game.data.emplace_back(v);
            } else if (k == "game_dir") { // unversioned (pre-v1) spelling, see migrate()
                s.game.data.insert(s.game.data.begin(), fs::path(v));
                sawData = true;
            } else
                keep(sec, k, v);
        } else if (sec == "graphics") {
            auto& g = s.graphics;
            if (k == "presentation")
                setEnum(sec, k, v, kPresentation, g.presentation);
            else if (k == "aspect")
                setEnum(sec, k, v, kAspect, g.aspect);
            else if (k == "window")
                setEnum(sec, k, v, kWindow, g.window);
            else if (k == "filter")
                setEnum(sec, k, v, kFilter, g.filter);
            else if (k == "backend")
                setEnum(sec, k, v, kBackend, g.backend);
            else if (k == "vsync")
                setBool(sec, k, v, g.vsync);
            else if (k == "validation")
                setBool(sec, k, v, g.validation);
            else if (k == "device")
                g.device = v;
            else if (k == "msaa") {
                auto n = parseInt(v);
                if (n && (*n == 1 || *n == 2 || *n == 4 || *n == 8))
                    g.msaa = std::uint32_t(*n);
                else
                    warn(sec, k, std::format("invalid value '{}' (expected 1|2|4|8), using {}", v, g.msaa));
            } else if (k == "resolution") {
                auto x = lower(v).find('x');
                std::optional<long> w, h;
                if (x != std::string::npos) w = parseInt(trim(v.substr(0, x))), h = parseInt(trim(v.substr(x + 1)));
                if (lower(v) == "desktop")
                    g.width = g.height = 0;
                else if (w && h && *w >= 320 && *h >= 240 && *w <= 16384 && *h <= 16384)
                    g.width = std::uint32_t(*w), g.height = std::uint32_t(*h);
                else
                    warn(sec, k, std::format("invalid value '{}' (expected WxH or desktop), using desktop", v)),
                        g.width = g.height = 0;
            } else
                keep(sec, k, v);
        } else if (sec == "audio") {
            auto& a = s.audio;
            if (k == "master")
                setVolume(sec, k, v, a.master);
            else if (k == "music")
                setVolume(sec, k, v, a.music);
            else if (k == "sfx")
                setVolume(sec, k, v, a.sfx);
            else if (k == "commentary")
                setVolume(sec, k, v, a.commentary);
            else if (k == "crowd")
                setVolume(sec, k, v, a.crowd);
            else if (k == "device")
                a.device = v;
            else
                keep(sec, k, v);
        } else if (sec == "gameplay") {
            s.gameplay.push_back({k, v});
        } else
            keep(sec, k, v);
    }

    // Brings older files to kVersion. v0 (no [meta] version): hand-written files may use
    // [game] game_dir (the engine's --game-dir spelling); it is folded into game.data while parsing.
    void migrate(bool hadMeta) {
        if (!hadMeta) r.fileVersion = 0;
        if (r.fileVersion > kVersion)
            r.warnings.push_back(std::format("settings version {} is newer than this build ({}); unknown keys are kept",
                                             r.fileVersion, kVersion));
        if (r.fileVersion < kVersion && r.existed) r.migrated = true;
    }
};

} // namespace

LoadResult parse(std::string_view text) {
    Parser p;
    p.r.existed = true;
    std::string section;
    bool hadMeta = false;
    std::istringstream in{std::string(text)};
    for (std::string raw; std::getline(in, raw);) {
        ++p.line;
        auto l = trim(raw);
        if (l.empty() || l[0] == '#' || l[0] == ';') continue;
        if (l.front() == '[') {
            if (l.back() != ']') {
                p.r.warnings.push_back(std::format("line {}: malformed section header '{}' ignored", p.line, l));
                continue;
            }
            section = lower(trim(std::string_view(l).substr(1, l.size() - 2)));
            continue;
        }
        auto eq = l.find('=');
        if (eq == std::string::npos || section.empty()) {
            p.r.warnings.push_back(
                std::format("line {}: '{}' is not a key = value line inside a section; ignored", p.line, l));
            continue;
        }
        auto key = lower(trim(std::string_view(l).substr(0, eq)));
        auto value = trim(std::string_view(l).substr(eq + 1));
        if (section == "meta" && key == "version") hadMeta = true;
        p.entry(section, key, value);
    }
    p.migrate(hadMeta);
    return std::move(p.r);
}

std::string serialize(const Settings& s) {
    std::string o;
    auto kv = [&](std::string_view k, auto&& v) { o += std::format("{} = {}\n", k, v); };
    auto extras = [&](std::string_view sec) {
        for (auto& u : s.unknown)
            if (u.name == sec)
                for (auto& e : u.entries) kv(e.key, e.value);
    };
    o += "# World Cup 98 native settings. Written by wc98-native; unknown keys are preserved.\n";
    o += "[meta]\n";
    kv("version", kVersion);
    extras("meta");

    o += "\n[game]\n";
    for (auto& d : s.game.data) kv("data", d.string());
    if (s.game.data.empty()) kv("data", "");
    extras("game");

    auto& g = s.graphics;
    o += "\n[graphics]\n";
    kv("presentation", name(g.presentation));
    kv("resolution", g.width ? std::format("{}x{}", g.width, g.height) : std::string("desktop"));
    kv("aspect", name(g.aspect));
    kv("window", name(g.window));
    kv("vsync", g.vsync);
    kv("msaa", g.msaa);
    kv("filter", name(g.filter));
    kv("device", g.device);
    kv("validation", g.validation);
    kv("backend", name(g.backend));
    extras("graphics");

    auto& a = s.audio;
    o += "\n[audio]\n";
    kv("master", a.master);
    kv("music", a.music);
    kv("sfx", a.sfx);
    kv("commentary", a.commentary);
    kv("crowd", a.crowd);
    kv("device", a.device);
    extras("audio");


    if (!s.gameplay.empty()) {
        o += "\n[gameplay]\n";
        for (auto& e : s.gameplay) kv(e.key, e.value);
    }
    for (auto& u : s.unknown) {
        static constexpr std::string_view known[] = {"meta", "game", "graphics", "audio", "gameplay"};
        if (std::ranges::find(known, u.name) != std::end(known)) continue;
        o += std::format("\n[{}]\n", u.name);
        for (auto& e : u.entries) kv(e.key, e.value);
    }
    return o;
}

fs::path defaultPath() {
    if (const char* p = std::getenv("WC98_CONFIG"); p && *p) return p;
    return platform::userDirs("wc98-native").config / "settings.ini";
}

LoadResult load(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::error_code ec;
        if (!fs::exists(file, ec)) return {};
        throw std::runtime_error(std::format("cannot read {}: {}", file.string(), std::strerror(errno)));
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

void save(const fs::path& file, const Settings& s) {
    auto dir = file.parent_path().empty() ? fs::path(".") : file.parent_path();
    fs::create_directories(dir);
    auto tmp = file;
    tmp += std::format(".tmp-{}", ::getpid());
    auto text = serialize(s);
    auto fail = [&](std::string_view what) {
        auto msg = std::format("{} {}: {}", what, tmp.string(), std::strerror(errno));
        ::unlink(tmp.c_str());
        throw std::runtime_error(msg);
    };
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) fail("cannot create");
    for (std::size_t done = 0; done < text.size();) {
        auto n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ::close(fd);
            fail("cannot write");
        }
        done += std::size_t(n);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        fail("cannot sync");
    }
    ::close(fd);
    if (::rename(tmp.c_str(), file.c_str()) != 0) fail("cannot rename");
    if (int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
}

} // namespace wc98::config
