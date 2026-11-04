#pragma once
// World Cup 98 team-history trivia/name pool (.TRI), paired with .HIS under
// FE/TEAMHIST.<lang>/. Format table: docs/asset-formats/TEAMHIST.md (measured facts from
// docs/reverse-engineering/TOURNAMENT_WC98.md + analysis/pass7/teamhist.txt). No FIFA 99 counterpart.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wc98::formats {

struct Tri {
    std::uint32_t count = 0;             // u32 @0 record count; measured 43 on all 8 shipped files
    std::vector<std::uint16_t> offsets;  // count LE u16 record starts, relative to kPoolBase

    // String pool base B: records start at B + offsets[k]. Measured constant across languages.
    static constexpr std::size_t kPoolBase = 0x2c0;
    // Records share string tails (offsets overlap), so the per-record field count is data-driven.
    // Cap = 6, the bracket loader's fixed slot count per team record (0x300-stride records).
    static constexpr std::size_t kMaxFieldsPerRecord = 6;

    // Validates count + table bounds (every B+offsets[k] inside the file); throws FormatError.
    // `data` must outlive this Tri: strings are read lazily.
    static Tri parse(std::span<const std::uint8_t> data);

    // Consecutive NUL-terminated pool fields from B + offsets[k], up to kMaxFieldsPerRecord,
    // stopping at EOF. Bytes are cp1252 as stored (see fcdb.hpp cp1252ToUtf8 for decoding).
    // Throws FormatError on k >= count. No overrun is possible: reads are bounds-checked.
    [[nodiscard]] std::vector<std::string> record(std::size_t k) const;

    // Every NUL-separated token in [kPoolBase, fileSize): the participant country names
    // (e.g. "Engeland", "Uruguay") plus all trivia strings, verbatim and in pool order.
    [[nodiscard]] std::vector<std::string> poolNames() const;

private:
    std::span<const std::uint8_t> data_;
};

} // namespace wc98::formats
