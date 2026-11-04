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
    bool truncated = false; // tolerant parse only: payload clamped to the member end (stripped mirror banks, e.g. PHRS/TYPE.PBK — SPEECH_INDEXING_NOTES.md §2)
};

struct BigArchive {
    std::uint32_t declaredSize = 0; // header field; not always equal to file size
    std::uint32_t headerSize = 0;   // offset of first data byte as declared
    std::vector<BigEntry> entries;

    // Parses the directory; throws FormatError. `data` must outlive returned spans.
    // tolerant=true accepts stripped-mirror archives: TOC offsets past the member end yield a clamped
    // payload view with `truncated` set, and an implausible entry count stops at the readable table
    // prefix instead of throwing (default false = strict; existing callers unchanged).
    static BigArchive parse(std::span<const std::uint8_t> data, bool tolerant = false);
    [[nodiscard]] static bool isBig(std::span<const std::uint8_t> data);
    // Case-insensitive name lookup.
    [[nodiscard]] const BigEntry* find(std::string_view name) const;
    [[nodiscard]] static std::span<const std::uint8_t> payload(std::span<const std::uint8_t> data, const BigEntry& e);
};

} // namespace wc98::formats
