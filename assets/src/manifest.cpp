// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/assets/manifest.hpp"
#include "wc98/assets/vfs.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace fs = std::filesystem;

namespace wc98::assets {

namespace {

// Regenerate from a verified tree with the command documented in
// docs/development/MILESTONE8_LAUNCHER_PORT.md (find + SHA-256 over the game-data dirs,
// executables and installer redist trees excluded).
constexpr ManifestEntry kEntries[] = {
#include "wc98_manifest.inc"
};

// WC98 feature groups by top-level dir (docs/research/ASSET_INVENTORY.md §2 layout roles).
constexpr std::array kFeatures{
    Feature{"frontend", true, "menus and front-end screens unavailable"},
    Feature{"match", true, "matches cannot be played (players, stadiums, textures)"},
    Feature{"engine-data", true, "databases, animations, AI scripts and commentary tables unavailable"},
    Feature{"speech", false, "no match commentary and player voices"},
    Feature{"music", false, "no music"},
    Feature{"crowd", false, "no crowd chants and ambience"},
    Feature{"sound-effects", false, "no in-game sound effects"},
    Feature{"video", false, "no intro, flyover and qualification videos"},
    Feature{"other", false, "unclassified data file"},
};

// First matching prefix wins (most specific first).
constexpr std::pair<std::string_view, std::size_t> kPrefixes[] = {
    {"FE2/", 0}, {"FEPACK/", 0}, {"FE/", 0},
    {"INGAME/", 1},
    {"COMMON/", 2},
    {"AUDIO/PHRS/", 3}, {"AUDIO/PBP/", 3}, {"AUDIO/PLRS/", 3}, {"AUDIO/ANTH/", 3}, {"AUDIO/TEAM/", 3},
    {"AUDIO/MUSC/", 4},
    {"AUDIO/CHNT/", 5}, {"AUDIO/CRWD/", 5},
    {"AUDIO/SFXS/", 6},
    {"VIDEO/", 7},
    {"AUDIO/", 8}, // root stamps (VER.DAT) and any future audio file
};

std::size_t featureIndex(std::string_view path) {
    for (const auto& [p, i] : kPrefixes)
        if (path.starts_with(p)) return i;
    return kFeatures.size() - 1;
}

bool needsHash(const ManifestEntry& m, std::uint64_t size) {
    return m.size == size; // unique paths: a matching size is common enough to warrant the hash
}

} // namespace

std::span<const ManifestEntry> wc98Manifest() { return kEntries; }

const Feature& featureFor(std::string_view gamePath) { return kFeatures[featureIndex(gamePath)]; }

std::string_view statusName(FileStatus s) {
    switch (s) {
    case FileStatus::Ok: return "ok";
    case FileStatus::Missing: return "missing";
    case FileStatus::Mismatch: return "mismatch";
    case FileStatus::UnknownVariant: return "unknown-variant";
    }
    return "?";
}

Report classify(const std::map<std::string, Observed>& observed) {
    Report r;
    for (const auto& f : kFeatures) r.features.push_back({f});
    for (const auto& m : kEntries) {
        FileCheck c{std::string(m.path)};
        if (auto it = observed.find(c.path); it != observed.end()) {
            c.size = it->second.size;
            if (m.size == c.size && m.sha256 == it->second.sha256) c.status = FileStatus::Ok;
            else c.status = needsHash(m, c.size) ? FileStatus::Mismatch : FileStatus::UnknownVariant;
        }
        auto& st = r.features[featureIndex(m.path)];
        (c.status == FileStatus::Ok ? st.ok : st.bad)++;
        r.files.push_back(std::move(c));
    }
    r.playable = std::ranges::all_of(r.features, [](const FeatureState& f) { return !f.feature.required || f.bad == 0; });
    return r;
}

std::string Report::variant() const {
    if (!playable) return "unknown or incomplete (required data missing)";
    std::string lost;
    for (const auto& f : features) {
        if (f.bad == 0) continue;
        lost += (lost.empty() ? "" : ", ") + std::string(f.feature.name);
    }
    return lost.empty() ? "World Cup 98 (Win95 CD)" : "World Cup 98 (Win95 CD, incomplete: " + lost + ")";
}

Report validate(const fs::path& path) {
    Detection d = detect(path);
    if (d.layout != Layout::GameTree)
        throw std::runtime_error("not a World Cup 98 game tree: " + path.string() + " (" + d.detail + ")");
    Vfs v(0); // cache 0: streaming hashes must not fill the LRU with whole trees
    v.mountTree(path, Layer::Base);
    std::map<std::string, Observed> obs;
    for (const auto& m : kEntries) {
        auto res = v.resolve(m.path);
        if (!res) continue;
        Observed o{fs::file_size(res->file), {}};
        if (needsHash(m, o.size)) o.sha256 = sha256File(res->file);
        obs.emplace(std::string(m.path), std::move(o));
    }
    Report r = classify(obs);
    r.source = std::move(d);
    return r;
}

} // namespace wc98::assets
