// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/big.hpp"
#include "wc98/reader.hpp"
#include <cctype>
#include <format>

namespace wc98::formats {

bool BigArchive::isBig(std::span<const std::uint8_t> d) {
    return d.size() >= 16 && d[0] == 'B' && d[1] == 'I' && d[2] == 'G' && (d[3] == 'F' || d[3] == '4');
}


BigArchive BigArchive::parse(std::span<const std::uint8_t> data) {
    // WC98 (matrix section 3): the 1998 BIGF table is
    //   magic[4], u32be declaredSize, u32be member count, u32be header size,
    //   count x { u32be data offset, u32be size, NUL-terminated member name }   // records packed
    // (FIFA 99 used the same table with u32le fields, the reason every field here is read
    // big-endian; verified on ANIM.MO, ANIM.BIG, GEN4TBLS.VIV and FCDB.DBI). Some extractions prefix the archive with 48 bytes (COMMON/ANIM.MO,
    // the .DBI containers); the magic search below absorbs that and keeps entry offsets
    // absolute in `data`.
    std::size_t base = 0;
    if (!isBig(data)) {
        if (data.size() > 0x34 && isBig(data.subspan(0x30))) base = 0x30;
        else throw FormatError("not a BIGF archive");
    }
    const std::span<const std::uint8_t> arc = data.subspan(base);
    Reader r(arc, 4);
    BigArchive a;
    a.declaredSize = r.u32be();
    std::uint32_t count = r.u32be();
    a.headerSize = r.u32be();
    if (count > arc.size() / 12) throw FormatError(std::format("BIG entry count {} implausible", count));
    a.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        BigEntry e;
        e.offset = base + r.u32be();
        e.size = r.u32be();
        e.name = r.cString(); // names are stored NUL-terminated, packed against the next record
        if (std::uint64_t(e.offset) + e.size > data.size())
            throw FormatError(std::format("BIG entry '{}' [{}+{}] exceeds archive ({})", e.name, e.offset, e.size, data.size()));
        a.entries.push_back(std::move(e));
    }
    return a;
}

const BigEntry* BigArchive::find(std::string_view name) const {
    auto eq = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char x = char(std::tolower((unsigned char)a[i])), y = char(std::tolower((unsigned char)b[i]));
            if (x == '\\') x = '/';
            if (y == '\\') y = '/';
            if (x != y) return false;
        }
        return true;
    };
    for (const auto& e : entries)
        if (eq(e.name, name)) return &e;
    return nullptr;
}

std::span<const std::uint8_t> BigArchive::payload(std::span<const std::uint8_t> data, const BigEntry& e) {
    return data.subspan(e.offset, e.size);
}

} // namespace wc98::formats
