// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/presentation/data.hpp"

#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/refpack.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace wc98::presentation {

namespace {
bool iequal(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) { return std::tolower(std::uint8_t(x)) == std::tolower(std::uint8_t(y)); });
}
std::string_view trimTag(std::string_view t) {
    while (!t.empty() && (t.back() == ' ' || t.back() == '\0')) t.remove_suffix(1);
    return t;
}
} // namespace

bool GameData::exists(std::string_view rel) const {
    std::error_code ec;
    return std::filesystem::exists(findCaseInsensitive(root_, rel), ec);
}

std::vector<std::uint8_t> GameData::file(std::string_view rel) const {
    auto p = findCaseInsensitive(root_, rel);
    if (!std::filesystem::exists(p)) throw std::runtime_error("missing game file " + std::string(rel));
    return readFile(p);
}

std::vector<std::uint8_t> GameData::member(std::string_view archive, std::string_view name) const {
    auto big = file(archive);
    auto data = formats::refpack::decompressIfNeeded(big); // MODELS2.PAK is a RefPack'd BIGF
    auto dir = formats::BigArchive::parse(data);
    for (const auto& e : dir.entries)
        if (iequal(e.name, name)) {
            auto p = formats::BigArchive::payload(data, e);
            return formats::refpack::decompressIfNeeded(p);
        }
    throw std::runtime_error("missing member " + std::string(name) + " in " + std::string(archive));
}

std::vector<std::string> GameData::members(std::string_view archive) const {
    auto data = formats::refpack::decompressIfNeeded(file(archive));
    std::vector<std::string> out;
    for (const auto& e : formats::BigArchive::parse(data).entries) out.push_back(e.name);
    return out;
}

render::Image toImage(const formats::FshBitmap& b) { return {b.width, b.height, b.rgba}; }

std::optional<render::Image> fshImage(const formats::FshFile& f, std::string_view tag) {
    for (const auto& e : f.entries)
        if (e.bitmap && iequal(trimTag(e.tag), trimTag(tag))) return toImage(*e.bitmap);
    return std::nullopt;
}

} // namespace wc98::presentation
