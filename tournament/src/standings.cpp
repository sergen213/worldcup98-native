// Group standings accumulation + tiebreak chain — spec §4 (app-side; binary block not located, §2.4).
// Tiebreak evaluation order NOT proven in the binary (§5.2): chain = config surface per task spec,
// vocabulary slot order cited from §3. Head-to-head (MOST_GOALS_ENCOUNTERS) resolved as a
// mini-table restricted to the equal-points tie group, like FIFA's "encounters" semantics [hyp].
#include "wc98/tournament/tournament.hpp"

#include <algorithm>
#include <set>

namespace wc98::tournament {

int winPoints(PointsSystem p) { return p == PointsSystem::kWin3Draw1 ? 3 : 2; }
int drawPoints(PointsSystem p) { (void)p; return 1; }

const std::vector<TieBreaker>& defaultTieBreakers() {
    // Vocabulary order cited in the task spec: rank, gd, gf, head-to-head, wins, away wins.
    static const std::vector<TieBreaker> kChain = {
        TieBreaker::kSeedRank, TieBreaker::kMostGoalDiff, TieBreaker::kMostGoalsTotal,
        TieBreaker::kHeadToHeadGoals, TieBreaker::kMostWins, TieBreaker::kMostWinsAway,
    };
    return kChain;
}

namespace {

// "MOST_*" keys: higher value wins. kHeadToHeadGoals handled by the tie-group mini-table below.
int keyOf(TieBreaker tb, const StandingsRow& r) {
    switch (tb) {
        case TieBreaker::kSeedRank:                return -r.seedRank; // lower rank value = better
        case TieBreaker::kMostGoalsHome:           return r.gfHome;
        case TieBreaker::kMostGoalsAway:           return r.gfAway;
        case TieBreaker::kMostGoalsTotal:          return r.gf;
        case TieBreaker::kMostGoalDiff:            return r.gd;
        case TieBreaker::kMostWins:                return r.w;
        case TieBreaker::kMostWinsAway:            return r.awin;
        case TieBreaker::kCoefficientEfficiency:   return (100 * r.pts) / (r.p > 0 ? r.p : 1); // hyp (§5.2)
        case TieBreaker::kHeadToHeadGoals:         break;
    }
    return 0;
}

// Goals scored by `r` in matches where BOTH teams belong to the tie group (encounters).
int miniTableGoals(const StandingsRow& r, const std::vector<MatchResult>& ms,
                   const std::set<std::uint16_t>& group) {
    int g = 0;
    for (const auto& m : ms) {
        if (!group.contains(m.teamA) || !group.contains(m.teamB)) continue;
        if (m.teamA == r.teamId) g += m.goalsA;
        else if (m.teamB == r.teamId) g += m.goalsB;
    }
    return g;
}

// Re-sorts one equal-points run [begin,end) via the chain, h2h keys from the tie mini-table.
void resolveTieGroup(std::vector<StandingsRow>::iterator begin,
                     std::vector<StandingsRow>::iterator end,
                     const std::vector<MatchResult>& ms,
                     const std::vector<TieBreaker>& chain) {
    std::set<std::uint16_t> group; // team ids in the run (range ctor would feed rows to uint16_t)
    for (auto it = begin; it != end; ++it) group.insert(it->teamId);
    std::sort(begin, end, [&](const StandingsRow& a, const StandingsRow& b) {
        for (const TieBreaker tb : chain) {
            int ka, kb;
            if (tb == TieBreaker::kHeadToHeadGoals) {
                ka = miniTableGoals(a, ms, group);
                kb = miniTableGoals(b, ms, group);
            } else {
                ka = keyOf(tb, a);
                kb = keyOf(tb, b);
            }
            if (ka != kb) return ka > kb; // "MOST_*": higher is better
        }
        return a.teamId < b.teamId; // deterministic final tiebreak (app-side, not from binary)
    });
}

} // namespace

std::vector<StandingsRow> computeStandings(
    const std::vector<StandingsRow>& seedRows,
    const std::vector<MatchResult>& matches,
    PointsSystem points,
    const std::vector<TieBreaker>* chain) {
    const std::vector<TieBreaker>& chainRef = chain ? *chain : defaultTieBreakers();
    std::vector<StandingsRow> rows = seedRows;
    for (const MatchResult& m : matches) {
        StandingsRow* a = nullptr;
        StandingsRow* b = nullptr;
        for (auto& r : rows) {
            if (r.teamId == m.teamA) a = &r;
            else if (r.teamId == m.teamB) b = &r;
        }
        if (!a || !b) continue; // friendly outside the pool: not accumulated
        const auto upd = [](StandingsRow& r, std::uint8_t gfT, std::uint8_t gaT, bool home) {
            r.p++;
            r.gf += gfT;
            r.ga += gaT;
            if (home) r.gfHome += gfT; else r.gfAway += gfT;
        };
        upd(*a, m.goalsA, m.goalsB, true);
        upd(*b, m.goalsB, m.goalsA, false);
        if (m.goalsA > m.goalsB) a->w++;
        else if (m.goalsA < m.goalsB) { b->w++; b->awin++; } // away win = teamB side
        else { a->d++; b->d++; }
        a->l = static_cast<std::int16_t>(a->p - a->w - a->d);
        b->l = static_cast<std::int16_t>(b->p - b->w - b->d);
    }
    for (auto& r : rows) {
        r.gd = static_cast<std::int16_t>(r.gf - r.ga);
        r.pts = static_cast<std::int16_t>(r.w * winPoints(points) + r.d * drawPoints(points));
    }
    std::sort(rows.begin(), rows.end(), [](const StandingsRow& a, const StandingsRow& b) {
        return a.pts > b.pts; // points always first (§4 wcstand1 column order)
    });
    for (std::size_t i = 0; i < rows.size();) { // equal-pts runs -> tie-break chain
        std::size_t j = i + 1;
        while (j < rows.size() && rows[j].pts == rows[i].pts) ++j;
        if (j - i >= 2) resolveTieGroup(rows.begin() + static_cast<std::ptrdiff_t>(i),
                                        rows.begin() + static_cast<std::ptrdiff_t>(j),
                                        matches, chainRef);
        i = j;
    }
    return rows;
}

} // namespace wc98::tournament
