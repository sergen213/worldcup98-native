// wc98-tournament-demo: headless full-tournament progression (docs/reverse-engineering/
// TOURNAMENT_MODULE_WC98.md §4-§5). Loads the real FE/TEAMHIST.ENG/ZWCUP.RES pair matrix,
// builds the 64-match WC98 schedule (generateSchedule), feeds seed-pinned deterministic
// results through computeStandings -> group qualifiers -> KnockoutBracket propagation
// (R16 -> QF -> SF -> F + THIRDROUND_PLAYOFF) to a recorded champion.
// Env gate: WC98_TREE unset / ZWCUP.RES missing -> exit 77 (CTest Skipped), like test_tournament.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <span>
#include <string>

#include "wc98/file.hpp"
#include "wc98/tournament/tournament.hpp"

using namespace wc98::tournament;

namespace {

int gFail = 0;
void check(bool ok, int line) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: tournament_demo line %d\n", line);
        ++gFail;
    }
}
#define CHECK(cond) check((cond), __LINE__)

constexpr std::uint32_t kScheduleSeed = 0x98D19A8u; // fixture-order stand-in (§5.3 mt19937)
constexpr std::uint32_t kResultSeed   = 0xC0FFEE98u; // score stream: fixed seed, no runtime RNG

// Seed-pinned first group-stage fixture (schedule order + score stream are deterministic).
constexpr int kPinGroup = 7, kPinGa = 1, kPinGb = 3;
constexpr std::uint16_t kPinHome = 35, kPinAway = 36;

struct Score { int a, b; bool pkHome; }; // knockout tie -> penalty shootout (§3 PKSHOOTOUT)

Score play(std::mt19937& rng) {
    std::uniform_int_distribution<int> goals(0, 3);
    return {goals(rng), goals(rng), (rng() & 1u) != 0};
}

} // namespace

int main() {
    const char* treeEnv = std::getenv("WC98_TREE");
    if (!treeEnv) { std::printf("wc98-tournament-demo: WC98_TREE unset, skipping\n"); return 77; }
    const auto resPath = std::filesystem::path(treeEnv) / "FE" / "TEAMHIST.ENG" / "ZWCUP.RES";
    if (!std::filesystem::exists(resPath)) {
        std::printf("wc98-tournament-demo: %s missing, skipping\n", resPath.string().c_str());
        return 77;
    }

    // --- ZWCUP pair matrix (§2.5) ---------------------------------------------------------
    const auto bytes = wc98::readFile(resPath);
    CHECK(bytes.size() == 2422);
    ZwcupRes res;
    if (!parseZwcupRes(std::as_bytes(std::span(bytes)), res)) return 1;
    CHECK(res.poolSize == 40 && res.ids.size() == 40 && res.pairs.size() == 780);
    ResPairRec pair;
    CHECK(res.findPair(res.ids[0], res.ids[9], pair) && res.findPair(res.ids[9], res.ids[0], pair));

    // Pool = first 32 real FCDB ids (placeholders >= 256 dropped, §2.5).
    std::array<std::uint16_t, 32> groups{};
    for (std::size_t i = 0, w = 0; i < res.ids.size() && w < 32; ++i)
        if (res.ids[i] < 256) groups[w++] = res.ids[i];

    // --- Schedule: 48 group + 16 knockout, seed-pinned -------------------------------------
    const auto sched = generateSchedule(groups, kScheduleSeed);
    CHECK(sched.size() == 64 && sched == generateSchedule(groups, kScheduleSeed));

    std::vector<MatchResult> groupMatches[8];
    std::array<Fixture, 16> koFixtures{}; // index = bracketNode (makeTree(16): R16 0..7, QF 8..11, SF 12..13, TP 15, F 14)
    std::array<bool, 16> koSeen{};
    int nGroup = 0;
    for (const auto& f : sched) {
        if (f.group >= 0) {
            ++nGroup;
            groupMatches[f.group].push_back({groups[static_cast<std::size_t>(f.home.a)],
                                             groups[static_cast<std::size_t>(f.away.a)], 0, 0});
        } else {
            CHECK(f.bracketNode >= 0 && f.bracketNode < 16 && !koSeen[f.bracketNode]);
            koSeen[static_cast<std::size_t>(f.bracketNode)] = true;
            koFixtures[static_cast<std::size_t>(f.bracketNode)] = f;
        }
    }
    CHECK(nGroup == 48);
    for (int n = 0; n < 16; ++n) CHECK(koSeen[n]);

    // Score stream: deterministic goals per fixture in schedule order.
    std::mt19937 rng(kResultSeed);
    int cursor[8] = {0}; // push order == schedule order per group; consume draws in fixture order
    for (const auto& f : sched)
        if (f.group >= 0) {
            const Score s = play(rng);
            MatchResult& m = groupMatches[f.group][static_cast<std::size_t>(cursor[f.group]++)];
            m.goalsA = static_cast<std::uint8_t>(s.a);
            m.goalsB = static_cast<std::uint8_t>(s.b);
        }

    // --- Standings (§3): per-group tables + invariants -------------------------------------
    std::array<StandingsRow, 16> place{}; // [g*2 + (b-1)] = group placing b = 1|2
    for (int g = 0; g < 8; ++g) {
        std::vector<StandingsRow> seeds;
        for (int pos = 0; pos < 4; ++pos) {
            StandingsRow r;
            r.teamId = groups[static_cast<std::size_t>(g * 4 + pos)];
            r.seedRank = pos;
            seeds.push_back(r);
        }
        CHECK(groupMatches[g].size() == 6); // 4-team round robin = 6 matches
        const auto table = computeStandings(seeds, groupMatches[g]);
        CHECK(table.size() == 4);
        int gfSum = 0, gaSum = 0;
        for (const auto& r : table) {
            CHECK(r.p == 3 && r.w + r.d + r.l == 3);
            CHECK(r.gd == r.gf - r.ga);
            CHECK(r.pts == r.w * winPoints(PointsSystem::kWin3Draw1) +
                          r.d * drawPoints(PointsSystem::kWin3Draw1));
            gfSum += r.gf; gaSum += r.ga;
        }
        CHECK(gfSum == gaSum); // every scored goal conceded: table consistent with fixtures
        place[static_cast<std::size_t>(g * 2)] = table[0];
        place[static_cast<std::size_t>(g * 2 + 1)] = table[1];
    }

    // --- Qualification: top-2 advance; WC98 bracket uses group placings 1|2 only (§4) -------
    auto placeTeam = [&](const SlotRef& r) {
        return place[static_cast<std::size_t>(r.a * 2 + r.b - 1)].teamId;
    };
    std::array<std::uint32_t, 16> seeds{};
    for (int i = 0; i < 8; ++i) { // R16 fixtures feed bracket nodes 0..7
        const Fixture& f = koFixtures[i];
        CHECK(f.home.kind == SlotRef::kGroupPlace && f.away.kind == SlotRef::kGroupPlace);
        CHECK(f.home.b >= 1 && f.home.b <= 2 && f.away.b >= 1 && f.away.b <= 2); // no best-third slots
        seeds[static_cast<std::size_t>(2 * i)] = placeTeam(f.home);
        seeds[static_cast<std::size_t>(2 * i + 1)] = placeTeam(f.away);
    }
    for (int n = 8; n < 16; ++n) // QF/SF/F/TP fixtures reference bracket winners only
        CHECK(koFixtures[n].home.kind == SlotRef::kBracketWinner &&
              koFixtures[n].away.kind == SlotRef::kBracketWinner);
    for (std::size_t i = 0; i < 16; ++i)
        for (std::size_t j = i + 1; j < 16; ++j) CHECK(seeds[i] != seeds[j]); // 16 distinct qualifiers

    // --- Knockout propagation: R16 -> QF -> SF -> F + third-place ---------------------------
    KnockoutBracket bracket =
        KnockoutBracket::makeTree(std::vector<std::uint32_t>(seeds.begin(), seeds.end()), true);
    CHECK(bracket.nodeCount() == 16 && bracket.finalNode() == 14 && bracket.thirdPlaceNode() == 15);
    std::array<std::uint32_t, 16> winners{}, losers{};
    winners.fill(kNoTeam);
    losers.fill(kNoTeam);
    for (int n = 0; n < 16; ++n) { // topological: R16, QF, SF, third-place, final
        const BracketNode& node = bracket.node(static_cast<std::size_t>(n));
        CHECK(node.teamA != kNoTeam && node.teamB != kNoTeam); // no orphan slots
        // THIRDROUND_PLAYOFF SlotRefs (SF nodes) resolve to the SF losers (setLoser wiring).
        auto resolve = [&](const SlotRef& r) {
            return n == bracket.thirdPlaceNode() ? losers[static_cast<std::size_t>(r.a)]
                                                 : winners[static_cast<std::size_t>(r.a)];
        };
        const std::uint32_t h = koFixtures[n].home.kind == SlotRef::kGroupPlace ? placeTeam(koFixtures[n].home)
                                                                                : resolve(koFixtures[n].home);
        const std::uint32_t a = koFixtures[n].away.kind == SlotRef::kGroupPlace ? placeTeam(koFixtures[n].away)
                                                                                : resolve(koFixtures[n].away);
        CHECK(h == node.teamA && a == node.teamB); // fixture slots == bracket nodes (no drift)
        const Score s = play(rng);
        const bool homeWin = s.a > s.b || (s.a == s.b && s.pkHome);
        const std::uint32_t w = homeWin ? h : a, l = homeWin ? a : h;
        CHECK(bracket.setWinner(static_cast<std::size_t>(n), w));
        if (node.round == KnockoutRound::kSemiFinal)
            CHECK(bracket.setLoser(static_cast<std::size_t>(n), l)); // SF losers -> THIRDROUND_PLAYOFF
        winners[static_cast<std::size_t>(n)] = w;
        losers[static_cast<std::size_t>(n)] = l;
    }

    // Final + third-place played, champion recorded in the final SF half.
    CHECK(bracket.champion() != kNoTeam);
    CHECK(bracket.champion() == winners[12] || bracket.champion() == winners[13]);
    CHECK(winners[bracket.thirdPlaceNode()] != kNoTeam);

    // Seed-pinned fixture outcome: fails if schedule order / score stream reproducibility moves.
    {
        const Fixture& f = sched.front();
        CHECK(f.group >= 0);
        std::mt19937 r2(kResultSeed);
        const Score s = play(r2); // first group fixture in schedule order = first draw
        std::printf("pinned fixture: g%d %u %d-%d %u\n", f.group,
                    static_cast<unsigned>(groups[f.home.a]), s.a, s.b,
                    static_cast<unsigned>(groups[f.away.a]));
        CHECK(f.group == kPinGroup && groups[f.home.a] == kPinHome && groups[f.away.a] == kPinAway);
        CHECK(s.a == kPinGa && s.b == kPinGb);
    }

    std::printf("wc98-tournament-demo: champion = %u, third-place winner = %u\n",
                bracket.champion(), winners[static_cast<std::size_t>(bracket.thirdPlaceNode())]);
    for (int g = 0; g < 8; ++g)
        std::printf("group %c winner: %u\n", static_cast<char>('A' + g),
                    static_cast<unsigned>(place[g * 2].teamId));
    return gFail ? 1 : 0;
}
