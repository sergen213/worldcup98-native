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

        std::set<int> draws;
        for (const auto& t : f.pairs) draws.insert(t.drawn);
        CHECK(std::ranges::all_of(draws, [](int v) { return v <= 0x40; })); // draw-count bound
    }

    // Head-to-head W/D/L (records_dec10 FUN_0045721c slot grammar + bank headers Games/Won/
    // Draws/Lost): Brazil(59)-Scotland(35) = W2 D1 L0; Italy(47)-Chile(63) = W1 D0 L1.
    // Directional access must swap won<->lost on order flip.
    const ResFile zwcup = ResFile::parse(slurp(dir / "ZWCUP.RES"));
    const auto bs = zwcup.slotOf(59), sc = zwcup.slotOf(35);
    const ResTriple br = zwcup.headToHead(bs, sc);
    CHECK(br.won == 2 && br.drawn == 1 && br.lost == 0);
    const ResTriple rv = zwcup.headToHead(sc, bs); // reverse direction swaps won/lost
    CHECK(rv.won == 0 && rv.drawn == 1 && rv.lost == 2);

    const auto it = zwcup.slotOf(47), cl = zwcup.slotOf(63);
    const ResTriple ic = zwcup.headToHead(it, cl);
    CHECK(ic.won == 1 && ic.drawn == 0 && ic.lost == 1);

    // Won-count histogram (§2.5 byte0 in {0,1,2,3,5}); ZINTER carries larger
    // aggregate values (international-history variant), so the bound is ZWCUP-scoped.
    std::set<int> won;
    for (const auto& t : zwcup.pairs) won.insert(t.won);
    CHECK(won == std::set<int>({0, 1, 2, 3, 5}));

    // ZINTER shares the id section; pair bytes diverge from 0x52 on (international variant).
    const ResFile zinter = ResFile::parse(slurp(dir / "ZINTER.RES"));
    CHECK(zinter.ids == zwcup.ids);
    CHECK(zinter.pairs != zwcup.pairs);

    std::printf("test_res: ok\n");
    return 0;
}
