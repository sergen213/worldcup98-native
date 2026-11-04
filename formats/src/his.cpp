// World Cup 98 .HIS parser; see wc98/formats/his.hpp. No FIFA 99 counterpart.
#include "wc98/formats/his.hpp"
#include "wc98/reader.hpp"
#include <format>

namespace wc98::formats {

His His::parse(std::span<const std::uint8_t> data) {
    // Loader gate (FUN_00489f58): file size > 3, then the geometry predicate.
    if (data.size() <= 4)
        throw FormatError(std::format("his: {} bytes, need more than 4", data.size()));
    Reader r(data);
    His h;
    h.data_ = data;
    h.magic = r.u32le();
    if (h.magic != kHisMagic)
        throw FormatError(std::format("his: bad magic {:08x}, expected {:08x}", h.magic, kHisMagic));
    r.seek(0x1e); // u16 fields at +0x1e/+0x20; Reader throws if the file is shorter
    h.nRows = r.u16le();
    h.nIdx = r.u16le();
    const std::size_t need = 8 + std::size_t(h.nIdx) * 2 + std::size_t(h.nIdx) * h.nRows * 4;
    if (data.size() < need)
        throw FormatError(std::format("his: {} bytes, geometry needs {}", data.size(), need));
    r.seek(0x28);
    h.index.reserve(h.nIdx);
    for (std::uint16_t i = 0; i < h.nIdx; ++i) h.index.push_back(r.u16le());
    return h;
}

std::uint32_t His::cell(std::size_t row, std::size_t col) const {
    Reader r(data_, 8 + std::size_t(nIdx) * 2 + (row * nRows + col) * 4);
    return r.u32le(); // need() bounds-checks; the predicate covers every in-range cell
}

std::vector<std::uint16_t> participants(const His& h) {
    std::vector<std::uint16_t> ids;
    Reader r(h.data(), 4);
    while (r.remaining() >= 2) {
        const auto v = r.u16le();
        if (v >= kHisIdRunCap) break; // string-offset subtable begins here (measured 599/568)
        ids.push_back(v);
    }
    return ids;
}

} // namespace wc98::formats
