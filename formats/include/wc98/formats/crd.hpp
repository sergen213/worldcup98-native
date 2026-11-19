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
    // Reaction record (pass-55 RE, cmn\scrowd.c): byte 0 = existence/enable gate
    // (SNDcrowdpulse FUN_004e4cac refuses records with byte 0 == 0), then six u16 at 4..15:
    // values[0] = pulse delta (exe adds delta << 16 to the 16.16 envelope level),
    // values[1] = pulse target ceiling (level clamped after the add), values[2..3] = pulse
    // ramp step as u32 (16.16 per frame, measured 0x38000 = 3.5), values[4..5] = idle ramp
    // frames as u32 (exe formula frames = rate * 0x21 >> 16; CROWD: 132/198/16500/264).
    std::array<std::uint16_t, 6> values{};
    std::uint32_t step() const { return std::uint32_t(values[2]) | (std::uint32_t(values[3]) << 16); }
    std::uint32_t rate() const { return std::uint32_t(values[4]) | (std::uint32_t(values[5]) << 16); }
};

struct Crd {
    std::uint8_t version = 0;              // byte 4, 2 in FIFA 99
    std::uint8_t byte7 = 0;                // envelope blend weight (pass-55): SNDcrowdload
                                           // computes be = (byte7 << 9) / 100, bc = 0x200 - be
                                           // (CROWD 0x46=70 -> 358/154; TRAFF 0x04 -> 20/492)
    std::array<CrdParams, 2> params;
    std::array<std::uint8_t, 8> bankSlots{}; // .BNK sound slots used, 0xFF = unused
    std::vector<CrdRecord> records;        // non-empty records of the 16 at 0x28
    std::array<std::uint32_t, 16> tableRefs{}; // u32s at 0x128, relative to each slot address
                                               // 0x128+i*4; SNDcrowdload fixups (FUN_004e4a80) to
                                               // absolute curve pointers (pass-54 measured: slots
                                               // 0..3 -> tables 0/2/4/6, slots 8..11 -> 1/3/5/7);
                                               // 0 = unbound slot
    std::vector<std::array<std::uint8_t, 512>> tables; // from 0x168 to EOF, non-decreasing byte curves

    static Crd parse(std::span<const std::uint8_t> data);
};

} // namespace wc98::formats
