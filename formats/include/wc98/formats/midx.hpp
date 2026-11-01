// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA "MIDx" event sequence (CHANT.EAM, crowd scripts in CRWD.BIG/COMO.BIG). A compact MIDI
// variant: 1-byte delta then an explicit-status MIDI channel event. See docs/formats/MIDX.md.
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {

struct MidxEvent {
    std::uint32_t tick = 0;   // absolute (sum of 8-bit deltas; time unit not yet known)
    std::uint8_t status = 0;  // 0x80..0xEF channel events, 0xFF = 2-byte marker (data1 = type)
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

struct Midx {
    std::vector<MidxEvent> events;
    std::uint32_t endTick = 0;   // tick of the F0 2F 00 end marker
    std::size_t size = 0;        // bytes up to and including the end marker

    static Midx parse(std::span<const std::uint8_t> data);
};

} // namespace wc98::formats
