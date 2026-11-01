// Derived from fifa99-native (same author), ported for World Cup 98
// GEN4TBLS formation tables: a synthetic G98* BIGF archive (self-built bytes), the
// G99* compatibility path, and the WC98 tree gate (COMMON/GEN4TBLS.VIV, matrix section 11).
#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/gen4tbls.hpp"
#include "wc98/formats/refpack.hpp"
#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace wc98::formats;
using Bytes = std::vector<std::uint8_t>;

static void be32(Bytes& b, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s)); }

// BIGF directory: header + (offset,size) entries + NUL-terminated names + payloads.
static Bytes buildBig(const std::vector<std::pair<std::string, Bytes>>& members) {
    Bytes b = {'B', 'I', 'G', 'F', 0, 0, 0, 0};
    be32(b, std::uint32_t(members.size()));
    b.push_back(0); b.push_back(0); b.push_back(0); b.push_back(0);
    std::uint32_t hdr = 16;
    for (const auto& [n, d] : members) hdr += 8 + std::uint32_t(n.size() + 1);
    std::uint32_t off = hdr;
    for (const auto& [n, d] : members) {
        be32(b, off);
        be32(b, std::uint32_t(d.size()));
        for (char c : n) b.push_back(std::uint8_t(c));
        b.push_back(0);
        off += std::uint32_t(d.size());
    }
    for (const auto& [n, d] : members) b.insert(b.end(), d.begin(), d.end());
    return b;
}

static Bytes table(std::size_t size, std::uint8_t seed) {
    Bytes d(size);
    for (std::size_t i = 0; i < size; ++i) d[i] = std::uint8_t(seed + i % 17);
    return d;
}

// PK is a 17x4 grid of little-endian int16 deltas (real size 136 B); seed each cell.
static Bytes table16(std::size_t cells, std::uint16_t seed) {
    Bytes d;
    for (std::size_t i = 0; i < cells; ++i) {
        const std::uint16_t v = std::uint16_t(seed + i);
        d.push_back(std::uint8_t(v & 0xFF));
        d.push_back(std::uint8_t(v >> 8));
    }
    return d;
}

// AT/DF are int32 tables in FIFA 99 (at[mentality*5+roleClass], df[mentality]).
static Bytes table32(std::size_t cells, std::int32_t seed) {
    Bytes d;
    for (std::size_t i = 0; i < cells; ++i) {
        const std::uint32_t v = std::uint32_t(seed + std::int32_t(i));
        for (int s = 0; s < 4; ++s) d.push_back(std::uint8_t(v >> (8 * s)));
    }
    return d;
}

static void synthetic() {
    Bytes hd = {'4', '0', '0', '\r', '\n'};
    auto arch = buildBig({{"G98HD", hd},
                          {"G98KO.FMT", table(17 * 4, 1)},
                          {"G98PL.FMT", table(17 * 299 * 4, 2)},
                          {"G98PK.FMT", table16(17 * 4, 3)},
                          {"G98FK.FMT", table(17 * 299 * 4, 4)}});
    auto t = Gen4Tables::parse(arch);
    CHECK(t.prefix == "G98" && t.header == "400\r\n" && !t.haveAtDf); // WC98: no AT/DF members
    CHECK(t.ko[0] == 1 && t.ko[1] == 2 && t.pk[0] == 3 && t.pk[1] == 4);
    CHECK(t.pl[0] == 2 && t.fk[1] == 5);

    // .FMM fallback, then a missing member.
    auto renamed = arch;
    auto at = std::ranges::search(renamed, std::string_view("G98KO.FMT")).begin();
    CHECK(at != renamed.end());
    at[8] = 'M';
    CHECK(Gen4Tables::parse(renamed).ko == t.ko);
    at[4] = 'X';
    CHECK_THROWS(Gen4Tables::parse(renamed));

    // FIFA 99 archive: G99 prefix + AT/DF members.
    auto f99 = buildBig({{"G99HD", hd},
                         {"G99KO.FMT", table(17 * 4, 1)},
                         {"G99PL.FMT", table(17 * 299 * 4, 2)},
                         {"G99PK.FMT", table(17 * 4 * 2, 3)},
                         {"G99FK.FMT", table(17 * 299 * 4, 4)},
                         {"G99AT.FMT", table32(25, 6)},
                         {"G99DF.FMT", table32(5, 7)}});
    auto n = Gen4Tables::parse(f99);
    CHECK(n.prefix == "G99" && n.haveAtDf && n.at[0] == 6 && n.at[1] == 7 && n.df[0] == 7);

    // No header member at all.
    CHECK_THROWS(Gen4Tables::parse(buildBig({{"SOMETHING", hd}})));
}

static void original() {
    auto root = treeDirOrSkip();
    // WC98 (matrix section 12.1): GEN4TBLS ships as COMMON/GEN4TBLS.VIV, a plain BIGF archive
    // with the G98HD header member and the G98{KO,PL,PK,FK}.FMT delta members.
    const auto path = wc98::findCaseInsensitive(root, "COMMON/GEN4TBLS.VIV");
    if (path.empty()) { std::printf("no GEN4TBLS.VIV under WC98_TREE\\n"); return; }
    auto arc = wc98::formats::refpack::decompressIfNeeded(wc98::readFile(path));
    const std::span<const std::uint8_t> sp{arc};
    const auto big = BigArchive::parse(sp);
    CHECK(big.entries.size() == 5);
    int fmts = 0, hd = 0;
    for (const auto& e : big.entries) {
        auto mem = wc98::formats::refpack::decompressIfNeeded(BigArchive::payload(sp, e));
        if (!Gen4FmtTable::isFmtMember(e.name)) { ++hd; CHECK(e.name == "G98HD"); CHECK(mem.size() == 5); continue; }
        const auto t = Gen4FmtTable::parse(mem, e.name);
        CHECK(t.attributes == 17 && t.columns * 17 * (t.sixteenBit ? 2 : 1) == mem.size());
        CHECK(t.row(0).size() == t.columns);
        const auto [mn, mx] = std::ranges::minmax(t.cells);
        std::printf("  %s: %zu attrs x %zu %s deltas, min=%d max=%d\n", e.name.c_str(), t.attributes, t.columns,
                    t.sixteenBit ? "i16be" : "i8", mn, mx);
        if (e.name == "G98KO.FMT") CHECK(t.columns == 4 && !t.sixteenBit);
        if (e.name == "G98PK.FMT") CHECK(t.columns == 8 && !t.sixteenBit);
        if (e.name == "G98PL.FMT") CHECK(t.columns == 598 && t.sixteenBit);
        if (e.name == "G98FK.FMT") CHECK(t.columns == 598 && t.sixteenBit);
        ++fmts;
    }
    CHECK(fmts == 4 && hd == 1);
    std::printf("GEN4TBLS.VIV: %zu members (%d G98*.FMT delta tables)\n", big.entries.size(), fmts);
}

int main() {
    synthetic();
    original();
    return 0;
}
