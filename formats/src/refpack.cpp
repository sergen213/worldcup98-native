// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/refpack.hpp"
#include "wc98/reader.hpp"
#include <format>

namespace wc98::formats::refpack {

namespace {
struct Header { std::uint32_t size; std::size_t dataStart; };
Header header(std::span<const std::uint8_t> d) {
    if (d.size() < 5 || d[1] != 0xFB || (d[0] & 0x3E) != 0x10) throw FormatError("not RefPack");
    std::uint8_t flags = d[0];
    bool large = flags & 0x80;     // 4-byte size fields
    bool hasCompSize = flags & 0x01;
    std::size_t sz = large ? 4 : 3;
    Reader r(d, 2);
    if (hasCompSize) r.skip(sz);
    std::uint32_t usize = large ? r.u32be() : r.u24be();
    return {usize, r.pos()};
}
} // namespace

bool isCompressed(std::span<const std::uint8_t> d) {
    return d.size() >= 5 && d[1] == 0xFB && (d[0] & 0x3E) == 0x10;
}
std::uint32_t uncompressedSize(std::span<const std::uint8_t> d) { return header(d).size; }

std::vector<std::uint8_t> decompress(std::span<const std::uint8_t> d) {
    auto h = header(d);
    std::vector<std::uint8_t> out;
    out.reserve(h.size);
    std::size_t i = h.dataStart;
    auto in = [&](std::size_t n) {
        if (i + n > d.size()) throw FormatError("RefPack: truncated input");
    };
    auto lit = [&](std::size_t n) {
        in(n);
        out.insert(out.end(), d.begin() + i, d.begin() + i + n);
        i += n;
    };
    auto copy = [&](std::size_t dist, std::size_t n) {
        if (dist == 0 || dist > out.size()) throw FormatError(std::format("RefPack: bad distance {} at out {}", dist, out.size()));
        std::size_t src = out.size() - dist;
        for (std::size_t k = 0; k < n; ++k) out.push_back(out[src + k]); // overlap allowed
    };
    for (;;) {
        in(1);
        std::uint8_t b0 = d[i];
        if (b0 < 0x80) { // 2-byte: 0DDRRRPP DDDDDDDD
            in(2);
            std::uint8_t b1 = d[i + 1]; i += 2;
            lit(b0 & 3);
            copy(((b0 & 0x60) << 3) + b1 + 1, ((b0 >> 2) & 7) + 3);
        } else if (b0 < 0xC0) { // 3-byte
            in(3);
            std::uint8_t b1 = d[i + 1], b2 = d[i + 2]; i += 3;
            lit(b1 >> 6);
            copy(((b1 & 0x3F) << 8) + b2 + 1, (b0 & 0x3F) + 4);
        } else if (b0 < 0xE0) { // 4-byte
            in(4);
            std::uint8_t b1 = d[i + 1], b2 = d[i + 2], b3 = d[i + 3]; i += 4;
            lit(b0 & 3);
            copy(((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1, ((b0 & 0x0C) << 6) + b3 + 5);
        } else if (b0 < 0xFC) { // literal run
            ++i;
            lit(((b0 & 0x1F) + 1) * 4);
        } else { // end: 0..3 trailing literals
            ++i;
            lit(b0 & 3);
            break;
        }
        if (out.size() > h.size) throw FormatError("RefPack: output exceeds declared size");
    }
    if (out.size() != h.size)
        throw FormatError(std::format("RefPack: produced {} bytes, header says {}", out.size(), h.size));
    return out;
}

std::vector<std::uint8_t> decompressIfNeeded(std::span<const std::uint8_t> d) {
    if (isCompressed(d)) return decompress(d);
    return {d.begin(), d.end()};
}

} // namespace wc98::formats::refpack
