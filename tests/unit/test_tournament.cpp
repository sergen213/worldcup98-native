// Tournament module gates (docs/reverse-engineering/TOURNAMENT_MODULE_WC98.md §2.5,§4-§5):
// pair-index formula bijection, ZWCUP.RES parse + directional lookup, standings math with the
// configurable tiebreak chain (incl. head-to-head mini-table), bracket propagation 16->1 + third
// place, schedule determinism (seed-pinned). Tree gate: real FE/TEAMHIST.ENG/ZWCUP.RES + ZINTER.RES.
#include "check.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <set>
#include <vector>

#include "wc98/file.hpp"
#include "wc98/tournament/tournament.hpp"

using namespace wc98::tournament;

// ---------------------------------------------------------------------------
// §2.5 pair formula: verified bijection over all C(40,2) pairs (spec [H-verify]).
static void testPairFormula() {
    constexpr std::uint16_t n = 40;
    std::set<std::int64_t> offs;
    std::int64_t prev = -1;
    for (std::uint16_t a = 0; a + 1 < n; ++a)
        for (std::uint16_t b = a + 1; b < n; ++b) {
            const auto off = ZwcupRes::pairByteOffset(n, a, b);
            CHECK(off > prev); // strictly increasing over the (a,b) scan => bijection, no holes
            prev = off;
            CHECK((off - 82) % 3 == 0);
            offs.insert(off);
        }
    CHECK(offs.size() == 780);
    CHECK(*offs.begin() == 82);                       // pair (0,1): triple idx 0 -> byteOff 2+80
    CHECK(*offs.rbegin() == 82 + 3 * 779);            // last pair = file end - 3 (size 2422)
    CHECK(ZwcupRes::pairByteOffset(n, 1, 0) == -1);   // a >= b rejected
    CHECK(ZwcupRes::pairByteOffset(n, 39, 40) == -1); // b >= n rejected
}

// Synthetic 2422-byte ZWCUP mirror: 33 FCDB ids {26..58} interleaved with the 7 placeholder
// values from §2.5 (>=256: 599,760,704,796,635,815,752); pair bytes = f(triple) pattern.
static std::vector<std::uint8_t> syntheticRes() {
    static const std::uint16_t kPlaceholders[7] = {599, 760, 704, 796, 635, 815, 752};
    std::vector<std::uint16_t> ids;
    int ph = 0;
    for (int i = 0; i < 40; ++i) { // placeholders at indices 9,16,23,30,34,37,39 (interleaved)
        if ((i == 9 || i == 16 || i == 23 || i == 30 || i == 34 || i == 37 || i == 39) && ph < 7)
            ids.push_back(kPlaceholders[ph++]);
        else
            ids.push_back(static_cast<std::uint16_t>(26 + (i - ph))); // FCDB run in {26..88}
    }
    CHECK(ph == 7);
    std::vector<std::uint8_t> buf(2 + 2 * 40 + 3 * 780, 0);
    buf[0] = 40; // n (LE u16 low byte; high stays 0)
    for (int i = 0; i < 40; ++i) std::memcpy(&buf[2 + 2 * i], &ids[i], 2);
    static const std::uint8_t kGoalsA[5] = {0, 1, 2, 3, 5}; // §2.5 byte0 histogram
    for (std::size_t t = 0; t < 780; ++t) {
        buf[82 + 3 * t]     = kGoalsA[t % 5];
        buf[82 + 3 * t + 1] = static_cast<std::uint8_t>(t % 4); // byte1 in {0..3}
        buf[82 + 3 * t + 2] = static_cast<std::uint8_t>((3 * t + 1) % 4); // byte2 in {0..3}
    }
    return buf;
}

static void testZwcupParse() {
    const auto buf = syntheticRes();
    CHECK(buf.size() == 2422);
    ZwcupRes res;
    CHECK(parseZwcupRes(std::span<const std::byte>(
              reinterpret_cast<const std::byte*>(buf.data()), buf.size()), res));
    CHECK(res.poolSize == 40 && res.ids.size() == 40 && res.pairs.size() == 780);

    // Placeholder multiset present; FCDB ids inside {26..88}.
    static const std::set<std::uint16_t> kWant{599, 760, 704, 796, 635, 815, 752};
    std::set<std::uint16_t> got;
    for (const auto id : res.ids) {
        if (id >= 256) got.insert(id);
        else CHECK(id >= 26 && id <= 88);
    }
    CHECK(got == kWant);

    // Directional access (§2.5): flipped team args swap byte0<->byte2.
    const std::uint16_t x = res.ids[3], y = res.ids[20];
    ResPairRec f, r;
    CHECK(res.findPair(x, y, f));
    CHECK(res.findPair(y, x, r));
    CHECK(f.goalsA == r.goalsB && f.goalsB == r.goalsA && f.venueFlag == r.venueFlag);
    CHECK(!res.findPair(x, x, f));            // same team: no pair
    CHECK(!res.findPair(0xFFFF, y, f));       // id absent from header

    const std::vector<std::uint8_t> shortBuf(buf.begin(), buf.end() - 1);
    ZwcupRes bad;
    CHECK(!parseZwcupRes(std::span<const std::byte>(
               reinterpret_cast<const std::byte*>(shortBuf.data()), shortBuf.size()), bad));
}

// ---------------------------------------------------------------------------
static StandingsRow row(std::uint16_t id, int rank = 0) {
    StandingsRow r;
    r.teamId = id;
    r.seedRank = rank;
    return r;
}

static void testStandingsMath() {
    const std::vector<StandingsRow> seeds = {row(26), row(29), row(31), row(33)};
    const std::vector<MatchResult> ms = {
        {26, 29, 2, 0}, {31, 33, 1, 1}, {26, 31, 1, 1},
        {29, 33, 0, 2}, {33, 26, 3, 0}, {29, 31, 0, 0},
    };
    const auto t = computeStandings(seeds, ms);
    CHECK(t.size() == 4);
    // pts order: D(7) A(4) C(3) B(1)
    CHECK(t[0].teamId == 33 && t[1].teamId == 26 && t[2].teamId == 31 && t[3].teamId == 29);
    const auto& D = t[0];
    CHECK(D.p == 3 && D.w == 2 && D.d == 1 && D.l == 0);
    CHECK(D.gf == 6 && D.ga == 1 && D.gd == 5 && D.pts == 7);
    CHECK(D.awin == 1); // BD (teamB side) win
    const auto& A = t[1];
    CHECK(A.p == 3 && A.w == 1 && A.d == 1 && A.l == 1);
    CHECK(A.gf == 3 && A.ga == 4 && A.gd == -1 && A.pts == 4);
    CHECK(A.gfHome == 3 && A.gfAway == 0); // AB, AC home (teamA side)

    // Configurable points model: win=2/draw=1 under kWin2Draw1 (win=3 hyp is the default).
    const auto t2 = computeStandings(seeds, ms, PointsSystem::kWin2Draw1);
    CHECK(t2[0].pts == 5 && t2[1].pts == 3); // D: 2 wins + draw = 5; A: win + draw = 3
}

static void testTieBreakers() {
    const std::vector<StandingsRow> seeds = {row(26), row(29), row(31), row(33)};

    // GD tiebreak (rank equal, default chain): A,B tied pts=7 with better goal festival first.
    {
        const std::vector<MatchResult> ms = {{26, 29, 0, 0}, {26, 31, 3, 0}, {29, 31, 1, 0},
                                             {26, 33, 1, 0}, {29, 33, 1, 0}};
        const auto t = computeStandings(seeds, ms);
        CHECK(t[0].pts == 7 && t[1].pts == 7);
        CHECK(t[0].teamId == 26 && t[1].teamId == 29); // gd 4 > 2
    }
    // GF tiebreak (all-draw league: pts and gd equal for all): chain = {MOST_GOALS_TOTAL}.
    {
        const std::vector<MatchResult> ms = {{26, 29, 1, 1}, {26, 31, 2, 2}, {26, 33, 0, 0},
                                             {29, 31, 3, 3}, {29, 33, 1, 1}, {31, 33, 2, 2}};
        const std::vector<TieBreaker> chain{TieBreaker::kMostGoalsTotal};
        const auto t = computeStandings(seeds, ms, PointsSystem::kWin3Draw1, &chain);
        // gf: C(7) B(5) A(3) D(3); equal gf -> deterministic teamId order.
        CHECK(t[0].teamId == 31 && t[1].teamId == 29 && t[2].teamId == 26 && t[3].teamId == 33);
    }
    // Head-to-head mini-table (MOST_GOALS_ENCOUNTERS): D wins all; A,B,C tie on pts=2 with
    // asymmetric encounter goals: C 3, A 2, B 1.
    {
        const std::vector<MatchResult> ms = {{26, 29, 0, 0}, {29, 31, 1, 1}, {31, 26, 2, 2},
                                             {26, 33, 0, 1}, {29, 33, 0, 1}, {31, 33, 0, 1}};
        const std::vector<TieBreaker> chain{TieBreaker::kHeadToHeadGoals};
        const auto t = computeStandings(seeds, ms, PointsSystem::kWin3Draw1, &chain);
        CHECK(t[0].teamId == 33 && t[1].teamId == 31 && t[2].teamId == 26 && t[3].teamId == 29);
    }
    // Seed rank (TEAM_RANK vocabulary) first in the default chain: equal everything, rank wins.
    {
        const std::vector<StandingsRow> rk = {row(26, 5), row(29, 1)};
        const auto t = computeStandings(rk, {});
        CHECK(t[0].teamId == 29 && t[1].teamId == 26);
    }
}

// ---------------------------------------------------------------------------
static void testBracket() {
    std::vector<std::uint32_t> seeds;
    for (std::uint32_t i = 0; i < 16; ++i) seeds.push_back(100 + i); // pairs (100,101),(102,103)...
    KnockoutBracket b = KnockoutBracket::makeTree(seeds, true);
    CHECK(b.nodeCount() == 16); // R16 8 + QF 4 + SF 2 + F 1 + third-place 1

    const int finals[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (const int n : finals) CHECK(b.node(static_cast<std::size_t>(n)).round == KnockoutRound::kRoundOf16);
    for (int n = 8; n <= 11; ++n) CHECK(b.node(static_cast<std::size_t>(n)).round == KnockoutRound::kQuarterFinal);
    CHECK(b.node(12).round == KnockoutRound::kSemiFinal && b.node(13).round == KnockoutRound::kSemiFinal);
    CHECK(b.node(14).round == KnockoutRound::kFinal && b.node(15).round == KnockoutRound::kThirdPlace);

    // 16 -> 1 propagation, always the higher id advances: champion must be 115.
    for (std::size_t n = 0; n < 8; ++n) {
        const BracketNode& nd = b.node(n);
        CHECK(b.setWinner(n, std::max(nd.teamA, nd.teamB)));
    }
    CHECK(b.node(8).teamA == 101 && b.node(8).teamB == 103); // winners of (100,101),(102,103)
    for (std::size_t n = 8; n < 14; ++n) {
        const BracketNode& nd = b.node(n);
        CHECK(b.setWinner(n, std::max(nd.teamA, nd.teamB)));
    }
    CHECK(b.setWinner(14, 115));
    CHECK(b.champion() == 115);

    // Third-place playoff: semi-final losers fill the THIRDROUND_PLAYOFF slots.
    CHECK(b.setLoser(12, std::min(b.node(12).teamA, b.node(12).teamB)));
    CHECK(b.setLoser(13, std::min(b.node(13).teamA, b.node(13).teamB)));
    CHECK(b.node(15).teamA != kNoTeam && b.node(15).teamB != kNoTeam);
    const BracketNode tp = b.node(15);
    CHECK(!b.setLoser(12, tp.teamA)); // target full: nowhere to propagate

    // Unplayable / invalid calls.
    KnockoutBracket e = KnockoutBracket::wc98();
    CHECK(e.nodeCount() == 16 && !e.setWinner(0, 5));       // sentinel slots -> not playable
    CHECK(!b.setWinner(3, 4242));                          // team not in the node
    CHECK(!KnockoutBracket::makeTree(std::vector<std::uint32_t>(14, 1), true).nodeCount()); // 14 not 2^k
}

// ---------------------------------------------------------------------------
static void testSchedule() {
    std::array<std::uint16_t, 32> groups{};
    for (int i = 0; i < 32; ++i) groups[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(i + 1);

    const auto a = generateSchedule(groups, 0x98);
    CHECK(a.size() == 64);
    CHECK(a == generateSchedule(groups, 0x98)); // seed-pinned determinism

    int groupFixtures = 0;
    std::set<std::pair<int, int>> seenPairs;
    for (const auto& f : a)
        if (f.group >= 0) {
            ++groupFixtures;
            CHECK(f.matchday >= 1 && f.matchday <= 3);
            CHECK(f.home.kind == SlotRef::kTeamSlot && f.away.kind == SlotRef::kTeamSlot);
            const int h = f.home.a, w = f.away.a;
            CHECK(h >= f.group * 4 && h < f.group * 4 + 4 && w >= f.group * 4 && w < f.group * 4 + 4);
            seenPairs.insert({std::min(h, w), std::max(h, w)});
        }
    CHECK(groupFixtures == 48 && seenPairs.size() == 48); // full round robin, no duplicates

    std::vector<Fixture> ko(16);
    std::set<int> nodes;
    for (const auto& f : a)
        if (f.bracketNode >= 0) { nodes.insert(f.bracketNode); ko[static_cast<std::size_t>(f.bracketNode)] = f; }
    CHECK(nodes.size() == 16 && *nodes.begin() == 0 && *nodes.rbegin() == 15);

    // R16 cross-pairings in bracket-slot order: slot 0 = M82 (C1-D2), slot 7 = M88 (H1-G2).
    CHECK(ko[0].home.kind == SlotRef::kGroupPlace && ko[0].home.a == 2 && ko[0].home.b == 1);
    CHECK(ko[0].away.a == 3 && ko[0].away.b == 2);
    CHECK(ko[7].home.a == 7 && ko[7].home.b == 1 && ko[7].away.a == 6 && ko[7].away.b == 2);
    // QF/SF/F wiring: node 8 = winners of nodes (0,1), SF 12 = (8,9), Final 14 = (12,13).
    CHECK(ko[8].home.kind == SlotRef::kBracketWinner && ko[8].home.a == 0 && ko[8].away.a == 1);
    CHECK(ko[12].home.a == 8 && ko[12].away.a == 9);
    CHECK(ko[14].home.a == 12 && ko[14].away.a == 13);
    CHECK(ko[15].home.a == 12 && ko[15].away.a == 13); // THIRDROUND_PLAYOFF = SF losers
}

int main() {
    testPairFormula();
    testZwcupParse();
    testStandingsMath();
    testTieBreakers();
    testBracket();
    testSchedule();

    // Tree gate: real ZWCUP.RES / ZINTER.RES (FE/TEAMHIST.ENG, spec §2.5).
    const auto tree = treeDirOrSkip();
    const auto dir = tree / "FE" / "TEAMHIST.ENG";
    const auto zc = wc98::readFile(dir / "ZWCUP.RES");
    const auto zi = wc98::readFile(dir / "ZINTER.RES");
    CHECK(zc.size() == 2422 && zi.size() == 2422);
    ZwcupRes res, inter;
    // readFile() yields vector<uint8_t>; std::as_bytes gives span<const byte>.
    CHECK(parseZwcupRes(std::as_bytes(std::span(zc)), res));
    CHECK(parseZwcupRes(std::as_bytes(std::span(zi)), inter));
    CHECK(res.poolSize == 40 && res.pairs.size() == 780);
    // ids section: 33 FCDB ids in {26..88} + the 7 placeholders from §2.5.
    static const std::set<std::uint16_t> kWant{599, 760, 704, 796, 635, 815, 752};
    std::set<std::uint16_t> ph;
    for (const auto id : res.ids) {
        if (id >= 256) ph.insert(id);
        else CHECK(id >= 26 && id <= 88);
    }
    CHECK(ph == kWant);
    // Bijection over all C(40,2): strictly increasing, in-range, aligned offsets.
    std::int64_t prev = -1;
    for (std::uint16_t x = 0; x + 1 < res.poolSize; ++x)
        for (std::uint16_t y = x + 1; y < res.poolSize; ++y) {
            const auto off = ZwcupRes::pairByteOffset(res.poolSize, x, y);
            CHECK(off > prev && off + 3 <= 2422 && (off - 82) % 3 == 0);
            prev = off;
        }
    // Directional symmetry on real data: a few pairs.
    ResPairRec f, r;
    for (int i = 0; i < 10; ++i) {
        const std::uint16_t x = res.ids[static_cast<std::size_t>(i)];
        const std::uint16_t y = res.ids[static_cast<std::size_t>(i + 7)];
        CHECK(res.findPair(x, y, f) && res.findPair(y, x, r));
        CHECK(f.goalsA == r.goalsB && f.goalsB == r.goalsA);
    }
    // ZINTER.RES: same ids, pair bytes differ from offset 0x52 (§2.5 [H-cmp]).
    CHECK(inter.ids == res.ids);
    CHECK(!std::equal(zi.begin() + 82, zi.end(), zc.begin() + 82));

    std::fprintf(stderr, "test_tournament: all checks passed\n");
    return 0;
}
