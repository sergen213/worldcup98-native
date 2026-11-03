#pragma once
// Original data access for the presentation layer: case-insensitive paths under the game root and
// BIG/VIV/PAK members (RefPack-decompressed). WC98 layout: INGAME/, COMMON/, FE/ at the tree root.
#include "wc98/formats/fsh.hpp"
#include "wc98/renderer/renderer.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace wc98::presentation {

class GameData {
public:
    explicit GameData(std::filesystem::path root) : root_(std::move(root)) {}
    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] bool exists(std::string_view rel) const;
    [[nodiscard]] std::vector<std::uint8_t> file(std::string_view rel) const;                        // throws if missing
    [[nodiscard]] std::vector<std::uint8_t> member(std::string_view archive, std::string_view name) const; // throws if missing
    [[nodiscard]] std::vector<std::string> members(std::string_view archive) const;

private:
    std::filesystem::path root_;
};

// FSH bitmap -> RGBA image.
render::Image toImage(const formats::FshBitmap& b);
// Bitmap of the entry tagged `tag` (FSH tags are space padded: "hl" matches "hl  "), if any.
std::optional<render::Image> fshImage(const formats::FshFile& f, std::string_view tag);

} // namespace wc98::presentation
