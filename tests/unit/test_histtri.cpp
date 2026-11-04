// World Cup 98 .HIS/.TRI pair: synthetic header/pool round trips plus a WC98-tree gate over all
// 8 language pairs in FE/TEAMHIST.ENG (du,en,fr,ge,it,da,sp,sw): HIS geometry invariants, the
// language-invariant participant id list, TRI count=43 + record/pool reads without overrun.
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

// Smallest file passing the predicate with nIdx=52/nRows=49: 8 + 104 + 52*49*4 = 10304 bytes.
static Bytes syntheticHis() {
    Bytes h(8 + 52 * 2 + 52 * 49 * 4, 0);
    put32(h, 0, kHisMagic);
    put16(h, 0x1e, 49);
    put16(h, 0x20, 52);
    put16(h, 0x04, 26); put16(h, 0x06, 29); put16(h, 0x08, 31); // id run
    put16(h, 0x0a, 0x100);                                       // first non-id (cap terminator)
    return h;
}

static void synthetic() {
    Bytes good = syntheticHis(); // must outlive h (span-based parse)
    auto h = His::parse(good);
    CHECK(participants(h) == (std::vector<std::uint16_t>{26, 29, 31}));
    CHECK(h.magic == kHisMagic && h.nRows == 49 && h.nIdx == 52 && h.index.size() == 52);
    CHECK(h.cell(0, 0) == 0 && h.cell(51, 48) == 0); // predicate covers every in-range cell
    Bytes bad = syntheticHis(); put32(bad, 0, kHisMagic + 1);
    CHECK_THROWS(His::parse(bad));                                   // magic gate
    const auto full = syntheticHis();
    Bytes short_(full.begin(), full.end() - 4);
    CHECK_THROWS(His::parse(short_));                                // geometry predicate
    CHECK_THROWS(h.cell(0, std::size_t(h.nIdx) * h.nRows));          // bounds-checked

    // Minimal TRI: pool base present, one record at B reading two empty tokens to EOF.
    Bytes t(Tri::kPoolBase + 2, 0);
    put32(t, 0, 1);
    auto tri = Tri::parse(t);
    CHECK(tri.count == 1 && tri.offsets.size() == 1);
    CHECK(tri.record(0) == (std::vector<std::string>{"", ""}));      // stops at EOF, no overrun
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

        // HIS gate: header invariants + loader predicate.
        auto h = His::parse(hb);
        CHECK(h.magic == 0x30028 && h.nRows == 49 && h.nIdx == 52);
        CHECK(std::size_t(hb.size()) >= 8 + std::size_t(h.nIdx) * 2 + std::size_t(h.nIdx) * h.nRows * 4);
        for (std::uint16_t r = 0; r < h.nIdx; ++r)
            for (std::uint16_t c = 0; c < h.nRows; ++c) (void)h.cell(r, c); // raw grid, no overrun
        const auto ids = participants(h);
        if (l == kLangs[0]) {
            ids0 = ids;
            CHECK(ids.size() == 33 && ids.front() == 26 && ids[1] == 29 && ids[2] == 31 && ids.back() == 88);
        } else {
            CHECK(ids == ids0); // language-invariance of the participant id list
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
    std::printf("TEAMHIST: 8 languages, %zu participant ids, TRI count 43\n", ids0.size());
}

int main() {
    synthetic();
    original();
    return 0;
}
