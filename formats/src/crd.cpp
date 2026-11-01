// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/crd.hpp"
#include "wc98/reader.hpp"
#include <algorithm>
#include <format>

namespace wc98::formats {

Crd Crd::parse(std::span<const std::uint8_t> data) {
    Reader r(data);
    if (r.u32be() != 0x4352446C) throw FormatError("not a CRDl file"); // "CRDl"
    Crd c;
    c.version = r.u8();
    if (c.version != 2) throw FormatError(std::format("CRDl version {} unknown", c.version));
    r.skip(2);
    c.byte7 = r.u8();
    for (auto& p : c.params) {
        p.a = r.u16le();
        p.b = r.u16le();
        p.c = r.u32le();
        p.d = r.u32le();
    }
    auto slots = r.bytes(8);
    std::copy(slots.begin(), slots.end(), c.bankSlots.begin());
    for (int i = 0; i < 16; ++i) {
        auto raw = r.bytes(16);
        if (std::ranges::all_of(raw, [](auto b) { return b == 0; })) continue;
        if (raw[0] != 0x01 || raw[1] != 0xFF || raw[2] || raw[3]) throw FormatError(std::format("CRDl record {}: unexpected prefix", i));
        CrdRecord rec;
        Reader rr(raw, 4);
        for (auto& v : rec.values) v = rr.u16le();
        c.records.push_back(rec);
    }
    constexpr std::size_t base = 0x128, tables = 0x168;
    if (data.size() < tables || (data.size() - tables) % 512) throw FormatError("CRDl: table area is not a multiple of 512 bytes");
    for (auto& o : c.tableRefs) {
        o = r.u32le();
        if (o && (o < tables - base || base + o >= data.size())) throw FormatError(std::format("CRDl table reference {} out of range", o));
    }
    for (std::size_t t = tables; t < data.size(); t += 512) {
        auto& tab = c.tables.emplace_back();
        std::copy_n(data.begin() + std::ptrdiff_t(t), 512, tab.begin());
    }
    return c;
}

} // namespace wc98::formats
