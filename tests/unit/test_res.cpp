// Derived from fifa99-native (same author), ported for World Cup 98
// .RES team-history matrix gate: parses ZWCUP.RES/ZINTER.RES from the extracted tree
// (FE/TEAMHIST.ENG) and asserts the §2.5 layout, pair-index bijection, placeholder ids,
// and the dossier-validated head-to-head triples (analysis/pass12tourn/ZWCUP_TRIPLE.md).
#include <wc98/formats/res.hpp>

#include "check.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

using namespace wc98::formats;

namespace {
std::vector<std::uint8_t> slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
} // namespace

int main() {
    const auto tree = treeDirOrSkip();
    const auto dir = tree / "FE" / "TEAMHIST.ENG";
    if (!std::filesystem::exists(dir)) { std::fprintf(stderr, "no TEAMHIST.ENG: skipping\n"); return 77; }

    for (const char* name : {"ZWCUP.RES", "ZINTER.RES"}) {
        const auto bytes = slurp(dir / name);
        CHECK(bytes.size() == 2422);
        const ResFile f = ResFile::parse(bytes);
        CHECK(f.n == 40);
        CHECK(f.pairs.size() == 780);

        // Pair-index bijection: strictly increasing over all a<b, matrix ends at EOF.
        std::size_t prev = 0;
        for (std::size_t a = 0; a + 1 < f.n; ++a)
            for (std::size_t b = a + 1; b < f.n; ++b) {
                const std::size_t idx = ResFile::pairIndex(f.n, a, b);
                CHECK(idx > prev || (a == 0 && b == 1)); // strictly increasing scan order
                prev = idx;
            }
        CHECK(prev == f.pairs.size() - 1);

        int placeholders = 0;
        for (const auto id : f.ids) if (id >= 256) ++placeholders;
        CHECK(placeholders == 7); // BYE/qualifier slots {599,760,704,796,635,815,752}

        std::set<int> venues;
        for (const auto& t : f.pairs) venues.insert(t.venueFlag);
        CHECK(std::ranges::all_of(venues, [](int v) { return v <= 0x40; })); // venue enum bound
    }

    // Dossier-validated triples (ZWCUP_TRIPLE.md): Brazil(59)-Scotland(35) = 2-1 venue'1';
    // Italy(47)-Chile(63) = 1-1. Directional access must swap on order flip.
    const ResFile zwcup = ResFile::parse(slurp(dir / "ZWCUP.RES"));
    const auto bs = zwcup.slotOf(59), sc = zwcup.slotOf(35);
    const ResTriple br = zwcup.headToHead(bs, sc);
    CHECK(br.goalsA == 2 && br.venueFlag == 1 && br.goalsB == 0);
    const ResTriple rv = zwcup.headToHead(sc, bs); // reverse direction swaps sides
    CHECK(rv.goalsA == 0 && rv.venueFlag == 1 && rv.goalsB == 2);

    const auto it = zwcup.slotOf(47), cl = zwcup.slotOf(63);
    const ResTriple ic = zwcup.headToHead(it, cl);
    CHECK(ic.goalsA == 1 && ic.venueFlag == 0 && ic.goalsB == 1);

    // Documented finals-score histogram (§2.5 byte0 in {0,1,2,3,5}); ZINTER carries larger
    // aggregate values (international-history variant), so the bound is ZWCUP-scoped.
    std::set<int> goalsA;
    for (const auto& t : zwcup.pairs) goalsA.insert(t.goalsA);
    CHECK(goalsA == std::set<int>({0, 1, 2, 3, 5}));

    // ZINTER shares the id section; pair bytes diverge from 0x52 on (international variant).
    const ResFile zinter = ResFile::parse(slurp(dir / "ZINTER.RES"));
    CHECK(zinter.ids == zwcup.ids);
    CHECK(zinter.pairs != zwcup.pairs);

    std::printf("test_res: ok\n");
    return 0;
}
