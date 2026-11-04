#pragma once
// World Cup 98 team-history binary (.HIS), paired with the .TRI trivia/name pool under
// FE/TEAMHIST.<lang>/ (du,en,fr,ge,it,da,sp,sw). Loader FUN_00489f58; geometry evidence in
// docs/reverse-engineering/TOURNAMENT_WC98.md + analysis/pass7/teamhist.txt, format table in
// docs/asset-formats/TEAMHIST.md. No FIFA 99 counterpart (this parser is WC98-native).
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {

// Measured constants, identical on all 8 shipped language files.
inline constexpr std::uint32_t kHisMagic = 0x30028; // u32 @0
struct His {
    std::uint32_t magic = 0;   // u32 @0; measured 0x30028 on all 8 shipped files
    std::uint16_t nRows = 0;   // u16 @0x1e; measured 49 (loader geometry predicate input)
    std::uint16_t nIdx = 0;    // u16 @0x20; measured 52 (loader geometry predicate input)
    // Raw nIdx x LE u16 table at +0x28. Mixed content: the tail of the ascending id run, then a
    // string-offset subtable, then alternating offset/zero u32 pairs. No semantics claimed.
    std::vector<std::uint16_t> index;

    // Validates magic and the loader predicate `8 + nIdx*2 + nIdx*nRows*4 <= fileSize`
    // (FUN_00489f58); throws FormatError. `data` must outlive this His: cells are read lazily.
    static His parse(std::span<const std::uint8_t> data);

    // Raw row-major u32 grid per the loader geometry: base 8 + nIdx*2, nIdx rows x nRows cells.
    // Cell semantics are UNKNOWN (docs/asset-formats/TEAMHIST.md "open items"); the record area
    // embeds localized strings inline, so this fixed-stride view is not language-invariant.
    // Bounds-checked: throws FormatError past EOF.
    [[nodiscard]] std::uint32_t cell(std::size_t row, std::size_t col) const;

    [[nodiscard]] std::span<const std::uint8_t> data() const { return data_; }

private:
    std::span<const std::uint8_t> data_;
};

// Participant team ids (FCDB id space): the language-invariant ascending u16 run starting at file
// offset 4, right after the magic. Measured on all 8 shipped files: 33 ids {26,29,31,...,88},
// "sequential-with-gaps"; the run continues through +0x28 into the index table region and ends
// where a string-offset subtable begins (first u16 >= kHisIdRunCap). This is the authoritative
// participant list consumed by the tournament-mode app slice.
std::vector<std::uint16_t> participants(const His& h);

inline constexpr std::uint16_t kHisIdRunCap = 0x100; // measured: first non-id element is 599/568

} // namespace wc98::formats
