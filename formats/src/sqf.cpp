// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/sqf.hpp"
#include "wc98/reader.hpp"
#include <algorithm>

namespace wc98::formats::sqf {
namespace {
constexpr std::size_t kHeaderSize = 0x44; // id block end / payload start

bool fourccIs(std::span<const std::uint8_t> d, std::size_t at, std::string_view tag) {
    return d.size() >= at + tag.size() &&
           std::equal(tag.begin(), tag.end(), d.begin() + static_cast<std::ptrdiff_t>(at));
}
} // namespace

bool isSqf(std::span<const std::uint8_t> data) {
    return fourccIs(data, 0, "EULZ") && fourccIs(data, 4, "*RTFD\0");
}

SqfArchive parseSqf(std::span<const std::uint8_t> data) {
    if (!isSqf(data)) throw FormatError("not an SQF container (need EULZ + *RTFD\\0)");
    Reader r(data, 0x08);
    SqfArchive a;
    a.archiveCount = r.u32le();
    a.typeCount = r.u32le();
    if (a.typeCount == 0) throw FormatError("SQF: type count is zero");
    r.seek(0x10);
    a.ids.assign(data.begin() + 0x10, data.begin() + kHeaderSize);
    if (data.size() <= kHeaderSize)
        throw FormatError(std::format("SQF: file size {} has no payload past header", data.size()));
    a.payload = data.subspan(kHeaderSize);
    return a;
}

} // namespace wc98::formats::sqf
