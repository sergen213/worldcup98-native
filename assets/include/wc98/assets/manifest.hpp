// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Embedded manifest of the World Cup 98 (Win95 CD) runtime data files and validation of a user
// tree against it. Executables, scripts and installer redist trees are never part of the set
// (docs/research/ASSET_INVENTORY.md §14 categorization). WC98 delta: single edition, no patch layer.
#include "wc98/assets/install.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wc98::assets {

struct ManifestEntry {
    std::string_view path; // canonical game path, e.g. "COMMON/FCDB.DBI"
    std::uint64_t size;
    std::string_view sha256; // lower-case hex
};
// Generated from the verified tree (see wc98_manifest.inc header). Sorted by path, one entry per file.
std::span<const ManifestEntry> wc98Manifest();

struct Feature {
    std::string_view name;
    bool required;           // false: the game runs without it, only the feature is lost
    std::string_view effect; // what is lost when its files are absent or wrong
};
const Feature& featureFor(std::string_view gamePath);

enum class FileStatus {
    Ok,             // matches the manifest size and hash (size alone for unique-size paths)
    Missing,
    Mismatch,       // known size, different content (corrupt / modified)
    UnknownVariant, // size matches no known file (other edition, ripped stub, ...)
};
std::string_view statusName(FileStatus s);

struct Observed {
    std::uint64_t size = 0;
    std::string sha256; // may be empty when size already rules out every known version
};

struct FileCheck {
    std::string path;
    FileStatus status = FileStatus::Missing;
    std::uint64_t size = 0;
};
struct FeatureState {
    Feature feature;
    int ok = 0, bad = 0;
};
struct Report {
    Detection source;
    std::vector<FileCheck> files;       // one per manifest path
    std::vector<FeatureState> features; // in declaration order
    bool playable = false;              // every required feature intact
    [[nodiscard]] std::string variant() const;
};

// Classifies observed files (keyed by canonical game path) against the manifest.
Report classify(const std::map<std::string, Observed>& observed);
// Detects `path` (a WC98 tree), hashes its data files and classifies them.
// Throws std::runtime_error when `path` is not a World Cup 98 game tree.
Report validate(const std::filesystem::path& path);

} // namespace wc98::assets
