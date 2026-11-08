// Team-history pair-matrix (.RES) parser. Layout + pair-index formula proven in
// docs/reverse-engineering/TOURNAMENT_MODULE_WC98.md §2.5 (loader FUN_0048cae8, lookup
// FUN_0048cc58); triple semantics validated against RSSSF history in analysis/pass12tourn/
// ZWCUP_TRIPLE.md: {goalsA, venueFlag, goalsB} head-to-head finals snapshot.
#pragma once
#include <wc98/reader.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {

struct ResTriple {
    std::uint8_t goalsA = 0;   // team-a side score (slot order as looked up)
    std::uint8_t venueFlag = 0; // venue-pattern enum (HOME_AWAY / HOME_HOME / ... §3 vocabulary)
    std::uint8_t goalsB = 0;
    bool operator==(const ResTriple&) const = default;
};

// `[u16 n][n × u16 ids][C(n,2) triples of u8]`; WC98 ZWCUP/ZINTER.RES: n=40, size 2422.
struct ResFile {
    std::uint32_t n = 0;                  // header count (40 on shipped files)
    std::vector<std::uint16_t> ids;       // id section (FCDB team ids + >=256 BYE placeholders)
    std::vector<ResTriple> pairs;         // C(n,2) triples in pair-index order

    // Pair slot index for a<b (positions into `ids`); formula verified bijection over C(n,2).
    static std::size_t pairIndex(std::size_t n, std::size_t a, std::size_t b);

    // Directional head-to-head: slotA/slotB positions into ids; swaps goalsA/goalsB on order flip.
    ResTriple headToHead(std::size_t slotA, std::size_t slotB) const;

    // Slot of team id (first occurrence), throws FormatError if absent.
    std::size_t slotOf(std::uint16_t id) const;

    static ResFile parse(std::span<const std::uint8_t> data);
};

} // namespace wc98::formats
