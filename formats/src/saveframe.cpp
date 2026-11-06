#include "wc98/formats/saveframe.hpp"

#include <array>
#include <string>

namespace wc98::formats {

// Standard reflected CRC-16 table (polynomial 0xA001); the identical 256-entry u16 table is
// present in FIFAWC.EXE at DAT_00585d50, consumed by FUN_004c8070 (analysis/pass7/slots_framing.txt).
static const std::array<std::uint16_t, 256>& crcTable() {
    static const std::array<std::uint16_t, 256> tab = [] {
        std::array<std::uint16_t, 256> t{};
        for (int i = 0; i < 256; ++i) {
            std::uint16_t c = static_cast<std::uint16_t>(i);
            for (int b = 0; b < 8; ++b) c = std::uint16_t((c >> 1) ^ ((c & 1u) ? 0xA001u : 0u));
            t[std::size_t(i)] = c;
        }
        return t;
    }();
    return tab;
}

std::uint16_t crc16SaveFormat(std::span<const std::uint8_t> data, std::uint16_t seed) {
    const auto& tab = crcTable();
    std::uint16_t crc = seed;
    for (const std::uint8_t b : data) crc = std::uint16_t((crc >> 8) ^ tab[(crc ^ b) & 0xFFu]);
    return crc;
}

SaveFrameStream SaveFrameStream::parse(std::span<const std::uint8_t> data) {
    Reader r(data);
    SaveFrameStream out;
    while (r.remaining() != 0) {
        const std::size_t recStart = r.pos();
        const std::uint32_t tag = r.u32le();
        const std::uint32_t version = r.u32le();
        const std::uint32_t len = r.u32le();
        SaveChunk c{tag, version, {}};
        const auto s = r.bytes(std::size_t(len));
        c.payload.assign(s.begin(), s.end());
        const std::uint16_t crc = r.u16le();
        const std::span<const std::uint8_t> rec(data.data() + recStart, 12 + std::size_t(len));
        if (crc16SaveFormat(rec) != crc)
            throw FormatError("saveframe: CRC mismatch in record at " + std::to_string(recStart));
        out.add(std::move(c));
    }
    return out;
}

std::vector<std::uint8_t> SaveFrameStream::serialize() const {
    std::vector<std::uint8_t> out;
    for (const auto& c : chunks_) {
        const std::size_t start = out.size();
        auto put32 = [&](std::uint32_t v) {
            out.push_back(std::uint8_t(v));
            out.push_back(std::uint8_t(v >> 8));
            out.push_back(std::uint8_t(v >> 16));
            out.push_back(std::uint8_t(v >> 24));
        };
        put32(c.tag);
        put32(c.version);
        put32(static_cast<std::uint32_t>(c.payload.size()));
        out.insert(out.end(), c.payload.begin(), c.payload.end());
        const std::uint16_t crc = crc16SaveFormat({out.data() + start, out.size() - start});
        out.push_back(std::uint8_t(crc));
        out.push_back(std::uint8_t(crc >> 8));
    }
    return out;
}

}  // namespace wc98::formats
