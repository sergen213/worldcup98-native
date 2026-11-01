// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA "CRDl" crowd/ambience description (CROWD.CRD, TRAFF.CRD), paired with the same-named .BNK.
// Layout is structural; field meanings are partly hypothetical. See docs/formats/CRD.md.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {

struct CrdParams {           // two 12-byte groups at 0x08 and 0x14
    std::uint16_t a = 0, b = 0;
    std::uint32_t c = 0, d = 0;
};

struct CrdRecord {           // 16 bytes; present records start with 01 FF 00 00
    std::array<std::uint16_t, 6> values{};
};

struct Crd {
    std::uint8_t version = 0;              // byte 4, 2 in FIFA 99
    std::uint8_t byte7 = 0;                // unknown (CROWD 0x46, TRAFF 0x04)
    std::array<CrdParams, 2> params;
    std::array<std::uint8_t, 8> bankSlots{}; // .BNK sound slots used, 0xFF = unused
    std::vector<CrdRecord> records;        // non-empty records of the 16 at 0x28
    std::array<std::uint32_t, 16> tableRefs{}; // u32s at 0x128, relative to 0x128, pointing into the
                                               // tables (not at table starts); 0 = none; purpose unknown
    std::vector<std::array<std::uint8_t, 512>> tables; // from 0x168 to EOF, non-decreasing byte curves

    static Crd parse(std::span<const std::uint8_t> data);
};

} // namespace wc98::formats
