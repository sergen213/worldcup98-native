// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA "BNKl" sound bank (.BNK: chants, SFX, crowd samples). See docs/formats/BNK.md.
#include "wc98/formats/schl.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {

struct BnkSound {
    std::uint32_t slot = 0;          // index in the bank's offset table (the game's sound id)
    std::size_t headerOffset = 0;    // absolute offset of the "PT" header
    PtHeader header;
};

struct Bnk {
    std::uint8_t version = 0;        // 2 or 4 in FIFA 99
    std::uint16_t slots = 0;         // table size; empty slots (offset 0) have no sound
    std::uint32_t dataStart = 0;     // u32 at 0x08: end of headers / first sample byte
    std::vector<BnkSound> sounds;    // non-empty slots in table order

    static Bnk parse(std::span<const std::uint8_t> data);
    // Decodes one sound (EA-XA v1 frames without per-block history, starting from silence).
    static Pcm decode(std::span<const std::uint8_t> data, const BnkSound& s);
};

} // namespace wc98::formats
