#pragma once
// World Cup 98 team-history binary (.HIS), paired with the .TRI trivia/name pool under
// FE/TEAMHIST.ENG/ (DU,EN,FR,GE,IT,DA,SP,SW). Loader FUN_00489f58; format table in
// docs/asset-formats/TEAMHIST.md. No FIFA 99 counterpart (this parser is WC98-native).
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace wc98::formats {

// Grid geometry, re-decoded 2026-11-17 (pass-20ui): the pre-existing "52x49 grid" view was a
// header misread - the u16 fields at 0x1e/0x20 are ids[13..14] of the id run (...47,49,52...),
// not counts. True header, verified on all 8 shipped language files:
//   u16 nTeams @0 (=40), u16 nCols @2 (=3),
//   u16 ids[nTeams] @4 (FCDB id space, ascending: national teams 26..88 + 4 club ids
//   752,760,796,815; language-invariant),
//   u32 offsets[nTeams*nCols] @(4+2*nTeams), strictly ascending; cell text spans
//   [offsets[k], offsets[k+1] or EOF), cp1252 bytes.
// Per team, column 0 = greats (caps/goals list), 1 = gen (association blurb),
// 2 = triv (trivia, multi-section). Every cell is a "[multitext]" blob (multitext.hpp).
inline constexpr std::size_t kHisColGreats = 0;
inline constexpr std::size_t kHisColGen = 1;
inline constexpr std::size_t kHisColTriv = 2;

struct His {
    std::uint16_t nTeams = 0; // u16 @0; measured 40 on all 8 shipped files
    std::uint16_t nCols = 0;  // u16 @2; measured 3
    std::vector<std::uint16_t> teamIds;               // ascending; language-invariant
    std::vector<std::uint32_t> offsets;               // nTeams*nCols, strictly ascending

    // Loader gate (FUN_00489f58): file size > 4, header + offset table fit, offsets strictly
    // ascending starting at/after the table end and below EOF. Throws FormatError.
    // `data` must outlive this His: cell texts are read lazily.
    static His parse(std::span<const std::uint8_t> data);

    // Cell text at (team, col); throws FormatError past bounds.
    [[nodiscard]] std::string_view cellText(std::size_t team, std::size_t col) const;

    [[nodiscard]] std::span<const std::uint8_t> data() const { return data_; }

private:
    std::span<const std::uint8_t> data_;
};

} // namespace wc98::formats
