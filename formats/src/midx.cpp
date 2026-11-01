// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/midx.hpp"
#include "wc98/reader.hpp"
#include <format>

namespace wc98::formats {

Midx Midx::parse(std::span<const std::uint8_t> data) {
    Reader r(data);
    if (r.u32be() != 0x4D494478) throw FormatError("not a MIDx sequence"); // "MIDx"
    Midx m;
    std::uint32_t tick = 0;
    for (;;) {
        tick += r.u8();
        std::size_t at = r.pos();
        MidxEvent e{tick, r.u8(), 0, 0};
        if (e.status == 0xF0) { // only the end-of-track form F0 2F 00 occurs
            std::uint8_t type = r.u8(), len = r.u8();
            if (type != 0x2F || len != 0) throw FormatError(std::format("MIDx: unexpected F0 {:02x} {:02x} at {}", type, len, at));
            m.endTick = tick;
            m.size = r.pos();
            return m;
        }
        if (e.status == 0xFF) {
            e.data1 = r.u8();
        } else {
            switch (e.status & 0xF0) {
            case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: e.data1 = r.u8(); e.data2 = r.u8(); break;
            case 0xC0: case 0xD0: e.data1 = r.u8(); break;
            default: throw FormatError(std::format("MIDx: status byte {:02x} at {} (running status is not used)", e.status, at));
            }
            if ((e.data1 | e.data2) & 0x80) throw FormatError(std::format("MIDx: data byte >= 0x80 at {}", at));
        }
        m.events.push_back(e);
    }
}

} // namespace wc98::formats
