// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/fcdb.hpp"
#include "wc98/reader.hpp"
#include <algorithm>
#include <cstring>
#include <format>

namespace wc98::formats {

namespace {

constexpr std::uint32_t kVersion = 501; // checked by the game (FUN_004a69ac)

std::uint32_t bits(std::uint32_t v, unsigned lo, unsigned n) { return (v >> lo) & ((1u << n) - 1); }

void checkHeader(std::span<const std::uint8_t> d, const char* magic) {
    if (d.size() < 20 || std::memcmp(d.data(), magic, 4) != 0) throw FormatError(std::format("not a {} DBI", magic));
    if (Reader(d, 4).u32le() != kVersion) throw FormatError(std::format("{} DBI: unexpected version", magic));
}

// A table: u32 count, u32 recordSize (0 = offset table follows), then offsets[count]
// relative to the table start, or count fixed-size records.
struct Table {
    std::size_t base = 0;
    std::uint32_t count = 0, recordSize = 0;
    std::vector<std::size_t> offsets; // absolute
};

Table readTable(std::span<const std::uint8_t> d, std::size_t base) {
    Reader r(d, base);
    Table t;
    t.base = base;
    t.count = r.u32le();
    t.recordSize = r.u32le();
    if (t.recordSize == 0) {
        if (t.count > r.remaining() / 4) throw FormatError(std::format("DBI table @{}: count {} implausible", base, t.count));
        for (std::uint32_t i = 0; i < t.count; ++i) {
            std::size_t o = base + r.u32le();
            if (o >= d.size()) throw FormatError(std::format("DBI table @{}: entry {} out of range", base, i));
            t.offsets.push_back(o);
        }
    } else {
        if (std::uint64_t(t.count) * t.recordSize > r.remaining())
            throw FormatError(std::format("DBI table @{}: {} x {} records exceed file", base, t.count, t.recordSize));
        for (std::uint32_t i = 0; i < t.count; ++i) t.offsets.push_back(base + 8 + std::size_t(i) * t.recordSize);
    }
    return t;
}

std::string cstr(std::span<const std::uint8_t> d, std::size_t at, std::size_t maxLen) {
    Reader r(d, at);
    return r.fixedString(std::min(maxLen, r.remaining()));
}

} // namespace

bool isDbi(std::span<const std::uint8_t> d) {
    if (d.size() < 20 || Reader(d, 4).u32le() != kVersion) return false;
    return std::memcmp(d.data(), "FCDB", 4) == 0 || std::memcmp(d.data(), "STR0", 4) == 0 || std::memcmp(d.data(), "STR1", 4) == 0;
}

FcdbDatabase FcdbDatabase::parse(std::span<const std::uint8_t> d, FcdbSchema schema) {
    checkHeader(d, "FCDB");
    Reader h(d, 8);
    std::uint32_t lo = h.u32le(), to = h.u32le(), po = h.u32le();
    FcdbDatabase db;

    Table lt = readTable(d, lo);
    if (lt.recordSize != 0) throw FormatError("FCDB league table must be variable-size");
    for (std::size_t o : lt.offsets) {
        Reader r(d, o);
        std::uint32_t v = r.u32le();
        FcdbLeague l;
        l.id = std::uint16_t(bits(v, 0, 12));
        l.field12 = std::uint16_t(bits(v, 12, 12));
        l.flag24 = bits(v, 24, 1);
        l.flag25 = bits(v, 25, 1);
        for (unsigned i = 0, n = bits(v, 26, 6); i < n; ++i) l.teamIds.push_back(r.u16le());
        db.leagues.push_back(std::move(l));
    }

    Table tt = readTable(d, to);
    // WC98 delta (matrix section 5 + 12.1): the 1998 team record is 68 B (17 u32
    // words) with the roster u16 list at byte 24; FIFA 99 uses 80 B / 20 words /
    // roster base byte 28. The record size selects (and must confirm) the schema.
    if (tt.recordSize != 68 && tt.recordSize != 80)
        throw FormatError(std::format("FCDB team record size {} is neither 68 (WC98) nor 80 (FIFA 99)", tt.recordSize));
    db.schema = tt.recordSize == 68 ? FcdbSchema::WorldCup98 : FcdbSchema::Fifa99;
    if (db.schema != schema)
        throw FormatError(std::format("FCDB uses the {} layout (team record {} B), not {}",
                                      db.schema == FcdbSchema::WorldCup98 ? "World Cup 98" : "FIFA 99", tt.recordSize,
                                      schema == FcdbSchema::WorldCup98 ? "World Cup 98" : "FIFA 99"));
    for (std::size_t o : tt.offsets) {
        Reader r(d, o);
        FcdbTeam t;
        for (std::size_t i = 0; i * 4 < tt.recordSize; ++i) t.raw[i] = r.u32le();
        if (db.schema == FcdbSchema::Fifa99) {
            t.id = std::uint16_t(bits(t.raw[0], 0, 11));
            t.budget = bits(t.raw[0], 11, 17);
            t.isClub = bits(t.raw[4], 30, 1);
            t.nationality = std::uint8_t(bits(t.raw[5], 20, 8));
            t.rivalTeamId = std::uint16_t(bits(t.raw[6], 11, 11));
            unsigned n = bits(t.raw[5], 15, 5);
            if (n > 26) throw FormatError(std::format("FCDB team {} roster count {} > 26", t.id, n));
            Reader rr(d, o + 28);
            for (unsigned i = 0; i < n; ++i) t.roster.push_back(rr.u16le());
        } else {
            // 1998 bit layout, verified against the fifa98edit editor (MIT) for the
            // 68-byte record: bytes 0..23 carry id/bank/stadium/kits/tactics/nation,
            // bytes 24..64 are 20 big-endian u16 player ids (0 = empty slot).
            const auto rec = d.subspan(o, 68);
            auto b = [&](unsigned i) { return std::uint32_t(rec[i]); };
            t.id = std::uint16_t(((b(1) & 0x07u) << 8) | b(0));
            t.bank = ((b(1) & 0xF8u) >> 3) | (b(2) << 5) | ((b(3) & 0x0Fu) << 13);
            t.stadium = std::uint8_t(b(3) >> 4);
            t.home.shirtStyle = std::uint8_t(b(5) >> 2);
            t.home.shirtColor1 = std::uint8_t(b(4) & 31);
            t.home.shirtColor2 = std::uint8_t(((b(5) & 3u) << 3) | (b(4) >> 5));
            t.home.shirtColor3 = std::uint8_t(b(6) & 31);
            t.home.shortsStyle = std::uint8_t(b(7) >> 6);
            t.home.shortsColor1 = std::uint8_t((b(6) >> 5) | ((b(7) & 3u) << 3));
            t.home.shortsColor2 = std::uint8_t(b(8) & 31);
            t.home.socksStyle = std::uint8_t(b(11) >> 6);
            t.home.socksColor1 = std::uint8_t((b(8) >> 5) | ((b(9) & 3u) << 3));
            t.home.socksColor2 = std::uint8_t(b(10) & 31);
            t.away.shirtStyle = std::uint8_t(b(13) >> 2);
            t.away.shirtColor1 = std::uint8_t(b(12) & 31);
            t.away.shirtColor2 = std::uint8_t(((b(13) & 3u) << 3) | (b(12) >> 5));
            t.away.shirtColor3 = std::uint8_t(b(14) & 31);
            t.away.shortsStyle = std::uint8_t(b(15) >> 6);
            t.away.shortsColor1 = std::uint8_t((b(14) >> 5) | ((b(15) & 3u) << 3));
            t.away.shortsColor2 = std::uint8_t(b(16) & 31);
            t.away.socksStyle = std::uint8_t((b(19) >> 4) & 3);
            t.away.socksColor1 = std::uint8_t((b(16) >> 5) | ((b(17) & 3u) << 3));
            t.away.socksColor2 = std::uint8_t(b(18) & 31);
            t.strategy = std::uint8_t(b(17) >> 5);
            t.tactics = std::uint8_t(b(21) >> 2);
            t.nationality = std::uint8_t(((b(23) & 0x0Fu) << 4) | ((b(22) & 0xF0u) >> 4));
            Reader rr(d, o + 24);
            for (unsigned i = 0; i < 20; ++i) {
                // WC98 (1998): roster slots are little-endian player ids (measured:
                // 984/984 slots resolve via LE; FIFA 99's big-endian reads 31/984).
                std::uint16_t pid = rr.u16le();
                if (pid) t.roster.push_back(pid); // 0 marks an unused slot
            }
            // 1998 squad size = filled roster slots; the byte-22 nibble stores floor(squad/2)
            // (measured: odd squads 11/13 -> nibbles 5/6; 40 of 56 teams are slot-capped at 20).
            t.rosterSize = std::uint8_t(t.roster.size());
        }
        db.teams.push_back(std::move(t));
    }

    Table pt = readTable(d, po);
    if (pt.recordSize != 20) throw FormatError(std::format("FCDB player record size {} != 20", pt.recordSize));
    for (std::size_t o : pt.offsets) {
        Reader r(d, o);
        FcdbPlayer p;
        for (auto& w : p.raw) w = r.u32le();
        auto& w = p.raw;
        if (db.schema == FcdbSchema::Fifa99) {
            p.id = std::uint16_t(bits(w[0], 0, 14));
            p.nationality = std::uint16_t(bits(w[0], 14, 11));
            p.overall = std::uint8_t(bits(w[0], 25, 7));
            p.value = std::uint16_t(bits(w[1], 0, 16));
            p.shirtNumber = std::uint8_t(bits(w[4], 4, 6));
            p.position2 = std::uint8_t(bits(w[4], 10, 5));
            p.position = std::uint8_t(bits(w[4], 15, 5));
            // Attribute slot -> (word, bit) as unpacked by FUN_005202b0 into the runtime player struct.
            static constexpr std::uint8_t src[17][2] = {{2, 0},  {2, 8},  {2, 12}, {2, 16}, {2, 20}, {2, 24},
                                                        {2, 28}, {3, 0},  {3, 4},  {3, 12}, {3, 20}, {3, 24},
                                                        {3, 28}, {4, 0},  {2, 4},  {3, 8},  {3, 16}};
            for (std::size_t i = 0; i < 17; ++i) p.attributes[i] = std::uint8_t(bits(w[src[i][0]], src[i][1], 4));
        } else {
            // 1998 player layout (fifa98edit, MIT): the 20-byte record is byte-packed.
            // Same 14-bit id / 10-bit nation split as FIFA 99 (matrix section 5), the
            // 17 skill nibbles sit in bytes 8..16, skills read n*4+39 (39..99 step 4),
            // bytes 4..5 are the transfer price and bytes 16..17 the jersey + role.
            const auto rec = d.subspan(o, 20);
            auto b = [&](unsigned i) { return std::uint32_t(rec[i]); };
            p.id = std::uint16_t(((b(1) & 0x3Fu) << 8) | b(0));
            p.nationality = std::uint16_t((b(2) << 2) | ((b(1) & 0xC0u) >> 6));
            p.startingSlot = std::uint8_t(b(3));
            p.price = b(4) | (b(5) << 8);
            p.value = std::uint16_t(p.price);
            p.hairStyle = std::uint8_t(b(6) >> 4);
            p.hairColor = std::uint8_t((b(6) >> 2) & 3);
            p.skinTone = std::uint8_t(b(6) & 3);
            p.faceStyle = std::uint8_t(b(7) >> 3);
            p.beardStyle = std::uint8_t(b(7) & 7);
            for (unsigned i = 0; i < 8; ++i) {
                p.attributes[2 * i] = std::uint8_t(b(8 + i) >> 4);
                p.attributes[2 * i + 1] = std::uint8_t(b(8 + i) & 0x0F);
            }
            p.attributes[16] = std::uint8_t(b(16) & 0x0F); // tackle
            p.shirtNumber = std::uint8_t(((b(17) & 0x0Fu) << 4) | (b(16) >> 4));
            p.role = std::uint8_t(b(17) >> 4);
            int sum = 0;
            for (auto a : p.attributes) sum += wc98SkillValue(a);
            p.overall = std::uint8_t(sum / int(p.attributes.size()));
        }
        db.players.push_back(p);
    }
    return db;
}

FcdbNames FcdbNames::parse(std::span<const std::uint8_t> d) {
    if (d.size() < 4) throw FormatError("DBI too short");
    std::string magic(d.begin(), d.begin() + 4);
    if (magic != "STR0" && magic != "STR1") throw FormatError("not a STR0/STR1 DBI");
    checkHeader(d, magic.c_str());
    FcdbNames n;
    n.magic = magic;
    Reader h(d, 8);
    std::uint32_t a = h.u32le(), b = h.u32le();
    auto strings = [&](std::size_t base) {
        Table t = readTable(d, base);
        if (t.recordSize != 0) throw FormatError("DBI string table must be variable-size");
        std::vector<std::string> v;
        for (std::size_t o : t.offsets) v.push_back(cstr(d, o, d.size()));
        return v;
    };
    if (magic == "STR1") {
        n.players = strings(a); // all three header offsets point at the same table
        return n;
    }
    n.leagues = strings(a);
    Table t = readTable(d, b);
    for (std::size_t o : t.offsets) // fixed fields: char[6] abbreviation, char[11] short name, cstring full name
        n.teams.push_back({cstr(d, o, 6), cstr(d, o + 6, 11), cstr(d, o + 17, d.size())});
    return n;
}

FclgStrings FclgStrings::parse(std::span<const std::uint8_t> d) {
    if (d.size() < 5 || std::memcmp(d.data(), "FCLG", 5) != 0) throw FormatError("not an FCLG string table");
    std::size_t end = d.size();
    if (end >= 8 && std::memcmp(d.data() + end - 8, "\0cRC", 4) == 0) end -= 8; // CD trailer
    while (end > 0 && d[end - 1] == 0) --end;                                    // fixed-size buffer padding
    FclgStrings s;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= end; ++i)
        if (i == end || d[i] == 0) {
            s.strings.emplace_back(reinterpret_cast<const char*>(d.data()) + start, i - start);
            start = i + 1;
        }
    return s;
}

std::string cp1252ToUtf8(std::string_view s) {
    static constexpr char16_t hi[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                        0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    std::string out;
    for (unsigned char c : s) {
        char32_t u = c >= 0x80 && c < 0xA0 ? hi[c - 0x80] : c;
        if (u < 0x80) out += char(u);
        else if (u < 0x800) { out += char(0xC0 | u >> 6); out += char(0x80 | (u & 63)); }
        else { out += char(0xE0 | u >> 12); out += char(0x80 | (u >> 6 & 63)); out += char(0x80 | (u & 63)); }
    }
    return out;
}

} // namespace wc98::formats
