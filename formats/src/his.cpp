// World Cup 98 .HIS parser; see wc98/formats/his.hpp. No FIFA 99 counterpart.
#include "wc98/formats/his.hpp"
#include "wc98/reader.hpp"
#include <format>

namespace wc98::formats {

His His::parse(std::span<const std::uint8_t> data) {
    // Loader gate (FUN_00489f58): file size > 4, then the geometry predicate.
    if (data.size() <= 4)
        throw FormatError(std::format("his: {} bytes, need more than 4", data.size()));
    Reader r(data);
    His h;
    h.data_ = data;
    h.nTeams = r.u16le();
    h.nCols = r.u16le();
    if (h.nTeams == 0 || h.nCols == 0)
        throw FormatError(std::format("his: degenerate header nTeams={} nCols={}", h.nTeams, h.nCols));
    const std::size_t cells = std::size_t(h.nTeams) * h.nCols;
    const std::size_t tableEnd = 4 + std::size_t(h.nTeams) * 2 + cells * 4;
    if (data.size() < tableEnd)
        throw FormatError(std::format("his: {} bytes, geometry needs {}", data.size(), tableEnd));
    h.teamIds.reserve(h.nTeams);
    for (std::uint16_t i = 0; i < h.nTeams; ++i) h.teamIds.push_back(r.u16le());
    h.offsets.reserve(cells);
    std::uint32_t prev = 0;
    for (std::size_t k = 0; k < cells; ++k) {
        const auto off = r.u32le();
        if (k == 0 ? off < tableEnd : off <= prev)
            throw FormatError(std::format("his: offset {} not ascending/in-range at {}", off, k));
        h.offsets.push_back(off);
        prev = off;
    }
    if (prev >= data.size())
        throw FormatError(std::format("his: last offset {} past EOF {}", prev, data.size()));
    return h;
}

std::string_view His::cellText(std::size_t team, std::size_t col) const {
    if (team >= nTeams || col >= nCols)
        throw FormatError(std::format("his: cell ({}, {}) out of {}x{}", team, col, nTeams, nCols));
    const std::size_t k = team * nCols + col;
    const std::size_t end = k + 1 < offsets.size() ? offsets[k + 1] : data_.size();
    return std::string_view(reinterpret_cast<const char*>(data_.data()) + offsets[k], end - offsets[k]);
}

} // namespace wc98::formats
