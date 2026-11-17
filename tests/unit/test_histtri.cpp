// World Cup 98 .HIS/.TRI pair: synthetic header/cell round trips plus a WC98-tree gate over all
// 8 language pairs in FE/TEAMHIST.ENG (DU,EN,FR,GE,IT,DA,SP,SW): HIS geometry invariants, the
// language-invariant team id list, every cell a "[multitext]" blob, TRI count=43 + record/pool
// reads without overrun.
#include "check.hpp"
#include <string_view>
#include "wc98/file.hpp"
#include "wc98/formats/his.hpp"
#include <algorithm>
#include "wc98/formats/tri.hpp"
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace wc98::formats;
using Bytes = std::vector<std::uint8_t>;

static void put16(Bytes& b, std::size_t at, std::uint16_t v) { std::memcpy(&b[at], &v, 2); }
static void put32(Bytes& b, std::size_t at, std::uint32_t v) { std::memcpy(&b[at], &v, 4); }

// 2x2 grid: header {2,2}, ids {26,31} @4, offsets {24,28,31,34} @8, table end 24, text @24..EOF.
static Bytes syntheticHis() {
    Bytes h(24, 0);
    put16(h, 0, 2);
    put16(h, 2, 2);
    put16(h, 4, 26);
    put16(h, 6, 31);
    put32(h, 8, 24);
    put32(h, 12, 28);
    put32(h, 16, 31);
    put32(h, 20, 34);
    const char text[] = "alphxyz12ab"; // cells: [24,28) [28,31) [31,34) [34,EOF)
    h.insert(h.end(), text, text + sizeof text - 1);
    return h;
}

static void synthetic() {
    Bytes good = syntheticHis(); // must outlive h (span-based parse)
    auto h = His::parse(good);
    CHECK(h.nTeams == 2 && h.nCols == 2);
    CHECK(h.teamIds == (std::vector<std::uint16_t>{26, 31}));
    CHECK(h.cellText(0, 0) == "alph");
    CHECK(h.cellText(0, 1) == "xyz");
    CHECK(h.cellText(1, 0) == "12a");
    CHECK(h.cellText(1, 1) == "b"); // last cell runs to EOF
    CHECK_THROWS(h.cellText(2, 0));                                   // team out of range
    CHECK_THROWS(h.cellText(0, 2));                                   // col out of range

    Bytes z = syntheticHis(); put16(z, 0, 0);
    CHECK_THROWS(His::parse(z));                                      // nTeams = 0
    Bytes z2 = syntheticHis(); put16(z2, 2, 0);
    CHECK_THROWS(His::parse(z2));                                     // nCols = 0
    const auto full = syntheticHis();
    Bytes short_(full.begin(), full.end() - 4);
    CHECK_THROWS(His::parse(short_));                                 // table past EOF

    Bytes d1 = syntheticHis(); put32(d1, 8, 23);
    CHECK_THROWS(His::parse(d1));                                     // offsets[0] < tableEnd
    Bytes d2 = syntheticHis(); put32(d2, 12, 24);
    CHECK_THROWS(His::parse(d2));                                     // not ascending
    Bytes d3 = syntheticHis(); put32(d3, 20, 35);
    CHECK_THROWS(His::parse(d3));                                     // last offset == EOF

    // Minimal TRI: pool base present, one record at B reading two empty tokens to EOF.
    Bytes t(Tri::kPoolBase + 2, 0);
    put32(t, 0, 1);
    auto tri = Tri::parse(t);
    CHECK(tri.count == 1 && tri.offsets.size() == 1);
    CHECK(tri.record(0) == (std::vector<std::string>{"", ""}));       // stops at EOF, no overrun
    CHECK_THROWS(tri.record(1));                                      // k out of range
    Bytes big = t; put32(big, 0, 0xFFFFFFFF);
    CHECK_THROWS(Tri::parse(big));                                    // table past file size
    Bytes off = t; put16(off, 4, 0xFFFF);
    CHECK_THROWS(Tri::parse(off));                                    // record start past EOF

    // Pool with a real token.
    Bytes named(Tri::kPoolBase, 0); // pool base of zeros; token appended at B
    const char s[] = "Uruguay\0";
    named.insert(named.end(), s, s + sizeof s - 1);
    put32(named, 0, 1);
    auto nt = Tri::parse(named);
    CHECK(nt.record(0).front() == "Uruguay");
    const auto toks = nt.poolNames();
    CHECK(std::ranges::find(toks, std::string_view("Uruguay")) != toks.end());
}

static void original() {
    const auto tree = treeDirOrSkip();
    static constexpr std::string_view kLangs[] = {"du", "en", "fr", "ge", "it", "da", "sp", "sw"};
    std::vector<std::uint16_t> ids0;
    for (const auto l : kLangs) {
        const std::string base(l);
        const auto hisP = wc98::findCaseInsensitive(tree, "FE/TEAMHIST.ENG/" + base + ".his");
        const auto triP = wc98::findCaseInsensitive(tree, "FE/TEAMHIST.ENG/" + base + ".tri");
        CHECK(!hisP.empty() && !triP.empty());
        const auto hb = wc98::readFile(hisP);
        const auto tb = wc98::readFile(triP);

        // HIS gate: true grid geometry (40 teams x 3 columns), language-invariant ids.
        auto h = His::parse(hb);
        CHECK(h.nTeams == 40 && h.nCols == 3);
        CHECK(h.teamIds.front() == 26 && h.teamIds[1] == 29 && h.teamIds[2] == 31 && h.teamIds.back() == 815);
        CHECK(std::ranges::is_sorted(h.teamIds)); // strictly ascending
        if (l == kLangs[0]) {
            ids0 = h.teamIds;
            CHECK(ids0.size() == 40);
        } else {
            CHECK(h.teamIds == ids0); // language-invariance of the team id list
        }
        // Every cell is a "[multitext]" blob and lies inside the file.
        for (std::size_t t = 0; t < h.nTeams; ++t)
            for (std::size_t c = 0; c < h.nCols; ++c)
                CHECK(h.cellText(t, c).starts_with("[multitext]"));

        if (l == "du") {
            CHECK(h.cellText(2, kHisColGen).find("The Football Association") != std::string_view::npos);
            CHECK(h.cellText(2, kHisColGreats).find("Quinn") != std::string_view::npos);
            std::size_t secs = 0;
            for (std::size_t p = 0; (p = h.cellText(2, kHisColTriv).find("[section]", p)) != std::string_view::npos; p += 9)
                ++secs;
            CHECK(secs == 8); // Ireland trivia: 8 sections (measured)
        }
        if (l == "en") {
            CHECK(h.cellText(2, kHisColGreats).find("Quinn") != std::string_view::npos);
            CHECK(h.cellText(2, kHisColGen).find("The Football Association") != std::string_view::npos);
        }

        // TRI gate: count + every record parses without overrun.
        auto t = Tri::parse(tb);
        CHECK(t.count == 43 && t.offsets.size() == 43);
        for (std::size_t k = 0; k < 43; ++k) CHECK(!t.record(k).empty());

        if (l == "du") {
            const auto rec = t.record(0);
            CHECK(rec.size() >= 3 && rec[1] == "Uruguay" && rec[2] == "Mexico"); // measured k=0 record
            const auto pool = t.poolNames();
            CHECK(std::ranges::find(pool, std::string_view("Uruguay")) != pool.end());
            CHECK(std::ranges::find(pool, std::string_view("Engeland")) != pool.end());
        }
        if (l == "en") {
            CHECK(t.record(0).front() == "Where was the first World Cup held?"); // EN k=0 parses clean
        }
    }
    std::printf("TEAMHIST: 8 languages, %zu team ids, 120 cells each, TRI count 43\n", ids0.size());
}

int main() {
    synthetic();
    original();
    return 0;
}
