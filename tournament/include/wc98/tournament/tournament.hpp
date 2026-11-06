#pragma once
// Tournament module app slice (docs/reverse-engineering/TOURNAMENT_MODULE_WC98.md §4-§5).
// Pure logic: ZWCUP.RES/ZINTER.RES pair matrix, group standings, tiebreak chain, knockout
// bracket graph, 1998 schedule plan. No rendering/platform deps; gate = tests/unit/test_tournament.cpp.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::tournament {

// Bracket team-slot sentinel: FUN_0048d084 initializes node fields +6/+10 to 0xFFFFFFFF (§2.6).
inline constexpr std::uint32_t kNoTeam = 0xFFFFFFFF;

// ---------------------------------------------------------------------------
// ZWCUP.RES / ZINTER.RES pair matrix (§2.5, both files exactly 2422 B)
// Layout: [u16 n=40][n * u16 ids][C(n,2) triples of u8].
// Triple semantics (goalsA, venue/neutral flag, goalsB) = hyp (§5.4); histogram facts §2.5.
struct ResPairRec {
    std::uint8_t goalsA = 0;
    std::uint8_t venueFlag = 0;
    std::uint8_t goalsB = 0;
};

struct ZwcupRes {
    std::uint16_t poolSize = 0;               // observed: 40
    std::vector<std::uint16_t> ids;           // FCDB ids + placeholders >=256 (§2.5)
    std::vector<ResPairRec> pairs;            // C(n,2) triples in pair-index order

    // Exact formula from FUN_0048cc58 — verified bijection over all C(n,2) pairs (§2.5):
    // for a<b: byteOff = 2 + 2n + 3*(((2n - a - 3)*a)/2 + b - 1). Returns -1 when !(a<b<n).
    static std::int64_t pairByteOffset(std::uint16_t n, std::uint16_t a, std::uint16_t b);

    // Directional head-to-head lookup (§2.5: "lookup swaps team args and byte0<->byte2 when
    // order flips"). False if either id is absent from the ids section.
    bool findPair(std::uint16_t idA, std::uint16_t idB, ResPairRec& out) const;
};

// Validates size against the loader formula (§2.5, FUN_0048cae8: filesize >= 2 + 2n + 3*n*(n-1)/2).
bool parseZwcupRes(std::span<const std::byte> data, ZwcupRes& out);

// ---------------------------------------------------------------------------
// Group standings (§2.4: binary block NOT located in the exe; app-side per §4 sketch).
struct MatchResult {
    std::uint16_t teamA = 0; // "home" side (venue pattern semantics unproven, §3 vocabulary)
    std::uint16_t teamB = 0;
    std::uint8_t goalsA = 0;
    std::uint8_t goalsB = 0;
};

// win=3/draw=1 is FIFA convention of the era and NOT proven in the binary (§5.1) — (hyp).
enum class PointsSystem : std::uint8_t { kWin3Draw1, kWin2Draw1 };
int winPoints(PointsSystem p);
int drawPoints(PointsSystem p);

// §4 sketch + home/away split fields for the §3 tiebreak vocabulary keys
// (MOST_GOALS_HOME/_AWAY, MOST_WINS_AWAY). seedRank = TEAM_RANK1..5 input (§3), hyp.
struct StandingsRow {
    std::uint16_t teamId = 0;
    int seedRank = 0;      // lower = better rank (TEAM_RANK vocabulary)
    std::int16_t p = 0, w = 0, d = 0, l = 0;
    std::int16_t gf = 0, ga = 0;
    std::int16_t gfHome = 0, gfAway = 0; // goals scored on home / away side (teamA = home)
    std::int16_t awin = 0;         // wins as away side (MOST_WINS_AWAY)
    std::int16_t gd = 0, pts = 0;
};

// Tiebreak keys, enum order = §3 keyword-table slot order ("slot order = enum index").
enum class TieBreaker : std::uint8_t {
    kSeedRank,           // TEAM_RANK1..5
    kMostGoalsHome,      // MOST_GOALS_HOME
    kMostGoalsAway,      // MOST_GOALS_AWAY
    kMostGoalsTotal,     // MOST_GOALS_TOTAL (== goals for)
    kMostGoalDiff,       // MOST_GOAL_DIFF
    kMostWins,           // MOST_WINS
    kMostWinsAway,       // MOST_WINS_AWAY
    kHeadToHeadGoals,    // MOST_GOALS_ENCOUNTERS: goals scored in head-to-head encounters
    kCoefficientEfficiency, // CO_EFFECIANT: pts played-ratio — semantics hyp (§5.2)
};

// Default chain = vocabulary order cited by the task spec (rank, gd, gf, head-to-head, ...).
// Evaluation-order comparator site is NOT proven in the binary (§5.2) — config surface per §4 sketch.
const std::vector<TieBreaker>& defaultTieBreakers();

// Accumulates every match whose teams are in `teams`, computes P/W/D/L/GF/GA/GD/Pts, and sorts
// descending by pts then the tiebreak chain.
// kHeadToHeadGoals is a mini-table restricted to the equal-points tie group (encounters).
std::vector<StandingsRow> computeStandings(
    const std::vector<StandingsRow>& seedRows,   // teamId/seedRank filled by caller
    const std::vector<MatchResult>& matches,
    PointsSystem points = PointsSystem::kWin3Draw1,
    const std::vector<TieBreaker>* chain = nullptr);

// ---------------------------------------------------------------------------
// Knockout bracket: heap node graph mirroring §2.6 (slots +6/+10 sentinel, self-link semantics
// = parent/slot wiring here). Round vocabulary §3: QUARTER_FINAL..FINAL + THIRDROUND_PLAYOFF.
enum class KnockoutRound : std::uint8_t { kRoundOf16, kQuarterFinal, kSemiFinal, kFinal, kThirdPlace };

struct BracketNode {
    std::uint32_t teamA = kNoTeam, teamB = kNoTeam; // §2.6 slots +6/+10
    KnockoutRound round = KnockoutRound::kQuarterFinal;
    int parent = -1;        // node that receives this match's winner
    int slotInParent = 0;   // 0 -> parent.teamA, 1 -> parent.teamB
    int losersNode = -1;    // node receiving this match's loser (SF -> third-place playoff)
};

class KnockoutBracket {
public:
    // Balanced power-of-2 tree (leafTeams in {4,8,16,...}): seeds fill leaf slots in order,
    // adjacent pairs play. `thirdPlace` wires the semi-final losers to a THIRDROUND_PLAYOFF node.
    static KnockoutBracket makeTree(std::vector<std::uint32_t> seeds, bool thirdPlace = true);
    // Real 1998 bracket: 8 groups -> R16 cross-pairings + QF/SF/F wiring (see schedule.cpp table).
    static KnockoutBracket wc98();

    std::size_t nodeCount() const { return nodes_.size(); }
    const BracketNode& node(std::size_t i) const { return nodes_[i]; }
    int finalNode() const { return final_; }
    int thirdPlaceNode() const { return third_; }
    std::uint32_t champion() const { return champion_; }

    // Plays the node: both slots must be filled and `team` one of them. Propagates the winner to
    // the parent slot (or records the champion at the final). False = not playable / bad team.
    bool setWinner(std::size_t node, std::uint32_t team);
    // Loser propagation: only valid where a losersNode is wired (semi-finals -> third-place).
    bool setLoser(std::size_t node, std::uint32_t team);

private:
    std::vector<BracketNode> nodes_;
    int final_ = -1;
    int third_ = -1;
    std::uint32_t champion_ = kNoTeam;
};

// ---------------------------------------------------------------------------
// 1998 schedule plan: groups A-H (4 each) -> 48 group matches + R16/QF/SF/F + third place.
struct SlotRef {
    enum Kind : std::uint8_t { kTeamSlot, kGroupPlace, kBracketWinner } kind = kTeamSlot;
    int a = -1; // kTeamSlot: flat index g*4+pos; kGroupPlace: group 0..7; kBracketWinner: node idx
    int b = 0;  // kGroupPlace: placing 1|2
    bool operator==(const SlotRef&) const = default; // schedule determinism gate compares plans
};

struct Fixture {
    SlotRef home, away;
    int group = -1;       // 0..7 (A-H), -1 for knockout
    int matchday = -1;    // 1..3 within the group stage, -1 for knockout
    int bracketNode = -1; // >=0 for knockout fixtures (KnockoutBracket::wc98 node indices)
    bool operator==(const Fixture&) const = default;
};

// Full 64-match plan: group fixtures reference concrete team ids from `groups`; knockout
// fixtures reference group placings / bracket-node winners. Deterministic for a given seed:
// ponytail: the original fixture ORDER is an RNG draw sequence (FUN_004c321c % N, §2.7/§5.3) and
// the exact permutation was never decompiled — mt19937(seed) stands in as the deterministic
// stand-in; swap in the real LCG once FUN_004c321c is decoded.
std::vector<Fixture> generateSchedule(const std::array<std::uint16_t, 32>& groups, std::uint32_t seed);

} // namespace wc98::tournament
