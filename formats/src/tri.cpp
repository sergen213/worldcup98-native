// World Cup 98 .TRI parser; see wc98/formats/tri.hpp. No FIFA 99 counterpart.
#include "wc98/formats/tri.hpp"
#include "wc98/reader.hpp"
#include <algorithm>
#include <format>

namespace wc98::formats {
namespace {
// Consecutive NUL-terminated tokens from `pos`, up to cap, stopping at EOF. A final token without
// a terminator is kept verbatim (pool tail).
std::vector<std::string> readFields(std::span<const std::uint8_t> data, std::size_t pos, std::size_t cap) {
    std::vector<std::string> fields;
    while (fields.size() < cap && pos < data.size()) {
        const auto rest = data.subspan(pos);
        const auto it = std::ranges::find(rest, std::uint8_t(0));
        if (it == rest.end()) { // unterminated pool tail
            fields.emplace_back(reinterpret_cast<const char*>(rest.data()), rest.size());
            break;
        }
        fields.push_back(std::string(reinterpret_cast<const char*>(rest.data()), it - rest.begin()));
        pos += std::size_t(it - rest.begin()) + 1;
    }
    return fields;
}
} // namespace

Tri Tri::parse(std::span<const std::uint8_t> data) {
    if (data.size() < kPoolBase + 2)
        throw FormatError(std::format("tri: {} bytes, pool base at {:#x}", data.size(), kPoolBase));
    Reader r(data);
    Tri t;
    t.data_ = data;
    t.count = r.u32le();
    if (t.count == 0 || 4 + std::size_t(t.count) * 2 > data.size())
        throw FormatError(std::format("tri: count {} with table past file size {}", t.count, data.size()));
    t.offsets.reserve(t.count);
    for (std::uint32_t k = 0; k < t.count; ++k) {
        const auto off = r.u16le();
        if (kPoolBase + off >= data.size())
            throw FormatError(std::format("tri: record {} start {:#x} past end {:#x}", k, kPoolBase + off, data.size()));
        t.offsets.push_back(off);
    }
    return t;
}

std::vector<std::string> Tri::record(std::size_t k) const {
    if (k >= offsets.size())
        throw FormatError(std::format("tri: record {} out of range ({})", k, offsets.size()));
    return readFields(data_, kPoolBase + offsets[k], kMaxFieldsPerRecord);
}

std::vector<std::string> Tri::poolNames() const { return readFields(data_, kPoolBase, ~std::size_t(0)); }

} // namespace wc98::formats
