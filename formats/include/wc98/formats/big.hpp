// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA "BIGF" archive (used for .BIG, .VIV and some .T files). See docs/formats/BIG.md.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wc98::formats {

struct BigEntry {
    std::string name;     // as stored (may contain '\\' separators)
    std::uint32_t offset; // absolute offset in the archive
    std::uint32_t size;
};

struct BigArchive {
    std::uint32_t declaredSize = 0; // header field; not always equal to file size
    std::uint32_t headerSize = 0;   // offset of first data byte as declared
    std::vector<BigEntry> entries;

    // Parses the directory; throws FormatError. `data` must outlive returned spans.
    static BigArchive parse(std::span<const std::uint8_t> data);
    [[nodiscard]] static bool isBig(std::span<const std::uint8_t> data);
    // Case-insensitive name lookup.
    [[nodiscard]] const BigEntry* find(std::string_view name) const;
    [[nodiscard]] static std::span<const std::uint8_t> payload(std::span<const std::uint8_t> data, const BigEntry& e);
};

} // namespace wc98::formats
