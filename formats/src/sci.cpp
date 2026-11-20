// Derived from fifa99-native (same author), ported for World Cup 98
#include <wc98/formats/sci.hpp>
#include <algorithm>
#include <ranges>

namespace wc98::formats {
namespace {

constexpr std::size_t kHeaderSize = 0x30;   // header + six relocation pointers at +0x10..+0x24
constexpr std::size_t kRecordStride = 16;   // record stream stride (8×i16)
constexpr std::size_t kTailStride = 12;     // tail table stride {u32 VA, u16[5]}
constexpr std::size_t kTailPad = 0x28;      // gap between the anchor*16 and the first tail entry

std::uint16_t le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// DGROUP image range for tail VA table entries (scene-data block region, TASK_HANDLERS_WC98 §6.3).
bool isDgroupVa(std::uint32_t va) {
    return va >= 0x580000u && va < 0x676000u;
}

} // namespace

SciFile SciFile::parse(std::span<const std::uint8_t> data) {
    if (data.size() < kHeaderSize + kRecordStride)
        throw FormatError("SCI: file shorter than header");
    const std::uint8_t* b = data.data();

    SciFile f;
    f.count = le32(b + 0x00);
    f.tagA = le16(b + 0x04);
    f.sizeField = le16(b + 0x06);
    f.tagB = le16(b + 0x08);
    f.offset = le16(b + 0x0A);
    f.tagC = le16(b + 0x0C);
    f.magic = le16(b + 0x0E);
    for (int i = 0; i < 6; ++i) f.relocs[static_cast<std::size_t>(i)] = le32(b + 0x10 + i * 4);

    if (f.tagA != 0x35 || f.tagC != 0x16)
        throw FormatError(std::format("SCI: header tags {:04x}/{:04x} != 0035/0016", f.tagA, f.tagC));
    if (f.magic < 0x201 || f.magic > 0x204) // full-tree survey: {0201:65, 0202:5, 0203:15, 0204:2}
        throw FormatError(std::format("SCI: magic {:04x} not in {{0201..0204}}", f.magic));

    // Record stream [0x30, anchor*16), stride 16.
    const std::size_t recEnd = static_cast<std::size_t>(f.offset) * kRecordStride;
    if (recEnd < kHeaderSize + kRecordStride || recEnd > data.size())
        throw FormatError(std::format("SCI: record end {0:x} out of range for size {1}", recEnd, data.size()));
    if ((recEnd - kHeaderSize) % kRecordStride != 0)
        throw FormatError("SCI: record stream not stride-aligned");

    // Tail region [anchor*16 + 0x28, EOF): main stride-12 entries, then a packed stride-8 list.
    const std::size_t tailStart = recEnd + kTailPad;
    if (tailStart > data.size() || (data.size() - tailStart) % 4 != 0)
        throw FormatError(std::format("SCI: tail region {0:x}..{1} misaligned", tailStart, data.size()));

    for (std::size_t off = kHeaderSize; off < recEnd; off += kRecordStride) {
        SciRecord r{};
        for (int i = 0; i < 8; ++i)
            r.fields[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(le16(b + off + i * 2));
        if (r.fields[0] < 0 || r.fields[0] > 0x19)
            throw FormatError(std::format("SCI: record @0x{0:x} type {1} out of observed range", off, r.fields[0]));
        f.records.push_back(r);
    }

    std::size_t off = tailStart;
    while (off + kTailStride <= data.size()) {
        const std::uint32_t va = le32(b + off);
        if (!isDgroupVa(va) || le16(b + off + 4) != 0 || le16(b + off + 6) != 0) break; // trailer start
        SciTailEntry t{};
        t.va = va;
        for (int i = 0; i < 4; ++i) t.words[static_cast<std::size_t>(i)] = le16(b + off + 4 + i * 2);
        f.tail.push_back(t);
        off += kTailStride;
    }
    if (f.tail.empty()) throw FormatError("SCI: empty tail table");
    if ((data.size() - off) % 8 != 0)
        throw FormatError(std::format("SCI: trailer {0:x}..{1} not stride-8", off, data.size()));
    for (; off + 8 <= data.size(); off += 8) {
        SciTailPair p{};
        p.va = le32(b + off);
        p.val = le32(b + off + 4);
        if (!isDgroupVa(p.va))
            throw FormatError(std::format("SCI: trailer @0x{0:x} VA {1:x} outside DGROUP scene block", off, p.va));
        f.trailer.push_back(p);
    }
    return f;
}

// Tree-wide observed record types (pass-17sci survey over all 87 COMMON/*.SCI files, 15305
// records): the stream is a data table of typed rows, not bytecode — see INTERPRETER.md.
std::vector<SciOp> decodeOpcodeStream(const SciFile& file) {
    static constexpr std::uint16_t kObserved[] = {0x1, 0x2, 0x4, 0x6, 0x9, 0xa, 0xb,
                                                  0xc, 0xd, 0x10, 0x13, 0x15};
    std::vector<SciOp> ops;
    ops.reserve(file.records.size());
    for (std::size_t i = 0; i < file.records.size(); ++i) {
        const auto& r = file.records[i];
        const auto t = static_cast<std::uint16_t>(r.fields[0]);
        if (std::ranges::find(kObserved, t) == std::end(kObserved))
            throw FormatError(std::format("SCI: record @0x{0:x} type {1:x} outside observed set",
                                          0x30 + i * 16, t));
        if (r.fields[1] != 0 || r.fields[5] != 0)
            throw FormatError(std::format("SCI: record @0x{0:x} operand f1/f5 non-zero", 0x30 + i * 16));
        const auto dx = r.fields[2];
        if (dx < -0x384 || dx > 0x384)
            throw FormatError(std::format("SCI: record @0x{0:x} f2 pitch delta {1:x} out of ±0x384",
                                          0x30 + i * 16, dx));
        SciOp op{};
        op.type = t;
        for (int j = 0; j < 7; ++j)
            op.ops[static_cast<std::size_t>(j)] = r.fields[static_cast<std::size_t>(j + 1)];
        op.offset = 0x30 + i * 16;
        ops.push_back(op);
    }
    return ops;
}

// Static script registry (pass-110sci, TASK_HANDLERS_WC98.md §10). DGROUP raw base of the retail
// FIFAWC.EXE image: VA 0x55A000 sits at file offset 0x158600 (VA = raw + 0x401A00). Boot slots:
// @0x580D3C pool-end sentinel (VA of last name entry), @0x580D40 record count (22),
// @0x580D44 record table VA (0x580A80). Registration FUN_004a7090 pushes each record's `count`
// consecutive pool entries from `first`; the selection builder FUN_004a7330 buckets them by class.
SciScriptRegistry parseExeScriptRegistry(std::span<const std::uint8_t> exeBytes) {
    static constexpr std::uint32_t kDgroupVa = 0x55a000u;
    static constexpr std::uint32_t kDgroupRaw = 0x158600u;
    static constexpr std::uint32_t kPoolVa = 0x580520u;
    static constexpr std::uint32_t kPoolStride = 16u;
    static constexpr std::uint32_t kSlotSentinel = 0x580d3cu;
    static constexpr std::uint32_t kSlotCount = 0x580d40u;
    static constexpr std::uint32_t kSlotTable = 0x580d44u;
    static constexpr std::uint32_t kRecordStride = 0x20u;
    static constexpr std::uint32_t kRecordMax = 64u;

    const auto raw = [&](std::uint32_t va) { return static_cast<std::size_t>(va - kDgroupVa + kDgroupRaw); };
    const auto at = [&](std::uint32_t va, std::size_t n) {
        if (raw(va) + n > exeBytes.size())
            throw FormatError(std::format("SCI registry: VA {0:x} outside image", va));
        return exeBytes.data() + raw(va);
    };

    SciScriptRegistry reg;
    reg.poolEndVa = le32(at(kSlotSentinel, 4));
    const std::uint32_t recordCount = le32(at(kSlotCount, 4));
    reg.tableVa = le32(at(kSlotTable, 4));
    if (recordCount == 0 || recordCount > kRecordMax)
        throw FormatError(std::format("SCI registry: record count {0} out of range", recordCount));
    if (reg.poolEndVa < kPoolVa)
        throw FormatError("SCI registry: sentinel slot below pool base");

    const std::size_t poolCount = (reg.poolEndVa - kPoolVa) / kPoolStride + 1; // sentinel = last entry
    if (poolCount == 0 || poolCount > 512)
        throw FormatError(std::format("SCI registry: pool span {0} entries out of range", poolCount));
    for (std::size_t i = 0; i < poolCount; ++i) {
        const std::uint8_t* e = at(kPoolVa + static_cast<std::uint32_t>(i) * kPoolStride, kPoolStride);
        for (std::size_t j = 0; j < 12; ++j)
            if (e[j] < 0x20 || e[j] >= 0x7f)
                throw FormatError(std::format("SCI registry: pool entry {0} name not printable", i));
        const std::uint16_t cls = le16(e + 12 + 2);
        if (cls > 10)
            throw FormatError(std::format("SCI registry: pool entry {0} class {1:x} > 10", i, cls));
        SciPoolEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(e), 12);
        const auto nul = entry.name.find('\0');
        if (nul != std::string::npos) entry.name.erase(nul);
        entry.cls = cls;
        reg.pool.push_back(std::move(entry));
    }

    std::uint32_t sum = 0;
    for (std::uint32_t i = 0; i < recordCount; ++i) {
        const std::uint32_t recVa = reg.tableVa + i * kRecordStride;
        const std::uint8_t* r = at(recVa, kRecordStride);
        SciScriptRecord rec;
        rec.flags = le32(r + 0x00);
        rec.ctxId = le32(r + 0x04);
        rec.count = le32(r + 0x08);
        rec.moduleByte = r[0x0c];
        rec.first = le32(r + 0x1c);
        if (rec.flags == 0 || rec.count == 0)
            throw FormatError(std::format("SCI registry: record {0} flags/count empty", i));
        if (rec.first < kPoolVa || rec.first > reg.poolEndVa || (rec.first - kPoolVa) % kPoolStride != 0)
            throw FormatError(std::format("SCI registry: record {0} first {1:x} outside pool", i, rec.first));
        if (rec.first + rec.count * kPoolStride > kPoolVa + poolCount * kPoolStride)
            throw FormatError(std::format("SCI registry: record {0} segment past pool end", i));
        sum += rec.count;
        reg.records.push_back(rec);
    }
    if (sum != poolCount)
        throw FormatError(std::format("SCI registry: record counts sum {0} != pool entries {1}", sum, poolCount));
    return reg;
}

} // namespace wc98::formats
