// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Discovery of user-supplied World Cup 98 game trees. Nothing found here is ever executed;
// detection only looks at names and file signatures. WC98 delta: the data lives directly in the
// tree root (FIFAWC.EXE + AUDIO/ FE/ INGAME/ COMMON/ ...), there is no DATA/ subdirectory, no
// official patch stream, and ISO/ZIP images are detected but not read (that needs libarchive,
// which this port does not depend on; extract the image with a standard tool).
// Layout facts: docs/research/ASSET_INVENTORY.md.
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wc98::assets {

enum class Layout {
    Unknown,
    GameTree, // extracted CD/install: FIFAWC.EXE + the game data directories at the root
    IsoImage, // ISO9660 image file (detected by CD001; extraction is a documented manual step)
    ZippedIso,  // ZIP archive (PK signature); same handling as IsoImage
};
std::string_view layoutName(Layout l);

struct Detection {
    Layout layout = Layout::Unknown;
    std::filesystem::path path;
    std::string detail; // human-readable reason / what was found
};

// Classifies one path. Never throws for unreadable or missing paths (returns Unknown with detail).
Detection detect(const std::filesystem::path& p);

// Directories searched by searchCommonLocations(): ~/Games (and its children), every mount under
// /run/media/$USER, $XDG_DATA_HOME/wc98-native/game (default ~/.local/share/wc98-native/game).
std::vector<std::filesystem::path> commonLocations();
// Detects every game tree in commonLocations() (plus *.iso/*.zip directly inside them), in search order.
std::vector<Detection> searchCommonLocations();

// Canonical game path: upper case, '/' separators, no leading "./" or '/'. Returns "" when the
// path is empty, absolute-with-drive, or contains "." / ".." / empty components (traversal).
std::string normalizeGamePath(std::string_view p);
// True for names that are Windows executables or scripts (.exe .dll .com .bat ...). The required
// manifest set never contains one of these.
bool isExecutableName(std::string_view name);

// Lower-case hex SHA-256 of a file (streamed) or a buffer.
std::string sha256File(const std::filesystem::path& p);
std::string sha256Hex(const unsigned char* data, std::size_t size);

} // namespace wc98::assets
