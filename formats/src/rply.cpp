// WC98 replay container codec (rply.hpp + docs/asset-formats/RPLY.md).
#include "wc98/formats/rply.hpp"

namespace wc98::formats {
namespace {
std::uint32_t le32(const std::uint8_t* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
void putLe32(std::vector<std::uint8_t>& out, std::uint32_t v) { out.insert(out.end(), {std::uint8_t(v), std::uint8_t(v >> 8), std::uint8_t(v >> 16), std::uint8_t(v >> 24)}); }
void putBe16(std::vector<std::uint8_t>& out, std::uint16_t v) { out.insert(out.end(), {std::uint8_t(v >> 8), std::uint8_t(v)}); }
} // namespace

RplyFile RplyFile::parse(std::span<const std::uint8_t> b) {
    if (b.size() < kRplyHeaderBytes) throw FormatError("rply: short header");
    RplyFile f;
    f.recordCount = le32(b.data());
    for (std::size_t i = 0; i < kRplyTableDwords; ++i) f.table[i] = le32(b.data() + 4 + 4 * i);
    std::size_t off = kRplyHeaderBytes; // FUN_00495e60: record area @ header size (+0x23c)
    while (f.records.size() < f.recordCount && off < b.size()) { // walk the size-prefixed sequence
        if (b.size() - off < 2) throw FormatError("rply: truncated record size");
        const std::size_t sz = (std::size_t(b[off]) << 8) | b[off + 1]; // BE16 walk size (FUN_004963b0)
        if (sz < 2 || off + sz > b.size()) throw FormatError("rply: bad record size");
        f.records.emplace_back(b.begin() + std::ptrdiff_t(off), b.begin() + std::ptrdiff_t(off + sz));
        off += sz;
    }
    return f; // trailing pad past the declared count tolerated (browser walks by size only)
}

std::vector<std::uint8_t> RplyFile::serialize() const {
    std::vector<std::uint8_t> out;
    out.reserve(kRplyHeaderBytes + 8 * records.size());
    putLe32(out, recordCount);
    for (const std::uint32_t d : table) putLe32(out, d);
    for (const RplyRecord& r : records) out.insert(out.end(), r.begin(), r.end());
    return out;
}

RplyRecord makeRplyRecord(std::span<const std::uint8_t> payload) {
    RplyRecord r;
    putBe16(r, std::uint16_t(payload.size() + 2)); // size includes the prefix itself (FUN_004963b0 walk)
    r.insert(r.end(), payload.begin(), payload.end());
    return r;
}

std::size_t rplyRecordSize(const RplyRecord& r) {
    return r.size() >= 2 ? (std::size_t(r[0]) << 8) | r[1] : 0;
}

std::vector<RplyIndexEntry> parseRplyIndex(std::span<const std::uint8_t> b) {
    if (b.size() != kRplyIndexEntries * kRplyIndexEntryBytes) throw FormatError("rply: index size");
    std::vector<RplyIndexEntry> entries;
    for (std::size_t e = 0; e < kRplyIndexEntries; ++e) {
        const std::uint8_t* p = b.data() + e * kRplyIndexEntryBytes;
        RplyIndexEntry en;
        en.field0 = le32(p);
        en.number = p[4];                                        // sprintf slot source (*(entry+4), FUN_0043c4f0)
        const char* n = reinterpret_cast<const char*>(p + 5);    // browser string base (entry+5)
        std::size_t len = 0;
        while (len < kRplyIndexEntryBytes - 5 && n[len]) ++len;
        en.name.assign(n, len);
        entries.push_back(std::move(en));
    }
    return entries;
}

std::vector<std::uint8_t> serializeRplyIndex(std::span<const RplyIndexEntry> entries) {
    std::vector<std::uint8_t> out(kRplyIndexEntries * kRplyIndexEntryBytes, 0);
    const std::size_t n = entries.size() < kRplyIndexEntries ? entries.size() : kRplyIndexEntries;
    for (std::size_t e = 0; e < n; ++e) {
        std::uint8_t* p = out.data() + e * kRplyIndexEntryBytes;
        const std::uint32_t v = entries[e].field0;
        p[0] = std::uint8_t(v); p[1] = std::uint8_t(v >> 8); p[2] = std::uint8_t(v >> 16); p[3] = std::uint8_t(v >> 24);
        p[4] = entries[e].number;
        const std::size_t len = entries[e].name.size() < kRplyIndexEntryBytes - 5 ? entries[e].name.size() : kRplyIndexEntryBytes - 5;
        for (std::size_t i = 0; i < len; ++i) p[5 + i] = std::uint8_t(entries[e].name[i]);
    }
    return out;
}

} // namespace wc98::formats
