// 1998 schedule plan — groups A-H (4 teams), top-2 -> R16 cross-pairings, QF/SF/F + third place.
// Spec §2.7/§5.3: the original fixture permutation (FUN_0048ace4 body, counters DAT_0057e930/e934)
// is NOT decompiled; structure = public 1998 format (TOURNAMENT_WC98.md §"Hyp"), not binary-proven.
#include "wc98/tournament/tournament.hpp"

#include <algorithm>
#include <numeric>
#include <random>

namespace wc98::tournament {

namespace {

// R16 cross-pairings (0-based groups; place = group finishing position 1|2). M81..M88 order.
constexpr std::array<std::pair<SlotRef, SlotRef>, 8> kR16Feed{{
    {{SlotRef::kGroupPlace, 0, 1}, {SlotRef::kGroupPlace, 1, 2}}, // Brazil v Chile (A1-B2)
    {{SlotRef::kGroupPlace, 2, 1}, {SlotRef::kGroupPlace, 3, 2}}, // France v Paraguay (C1-D2)
    {{SlotRef::kGroupPlace, 1, 1}, {SlotRef::kGroupPlace, 0, 2}}, // Italy v Norway (B1-A2)
    {{SlotRef::kGroupPlace, 3, 1}, {SlotRef::kGroupPlace, 2, 2}}, // Nigeria v Denmark (D1-C2)
    {{SlotRef::kGroupPlace, 5, 1}, {SlotRef::kGroupPlace, 4, 2}}, // Germany v Mexico (F1-E2)
    {{SlotRef::kGroupPlace, 4, 1}, {SlotRef::kGroupPlace, 5, 2}}, // Netherlands v Yugoslavia (E1-F2)
    {{SlotRef::kGroupPlace, 6, 1}, {SlotRef::kGroupPlace, 7, 2}}, // Romania v Croatia (G1-H2)
    {{SlotRef::kGroupPlace, 7, 1}, {SlotRef::kGroupPlace, 6, 2}}, // Argentina v England (H1-G2)
}};

// Bracket-slot order of the R16 fixtures so that KnockoutBracket::makeTree's adjacent QF wiring
// reproduces the real quarter-final paths: (M82,M83)(M85,M87)(M81,M84)(M86,M88) — France/Italy
// side, Germany/Croatia side, Brazil/Denmark side, Netherlands/Argentina side.
constexpr std::array<int, 8> kBracketOrder{1, 2, 4, 6, 0, 3, 5, 7};

SlotRef winner(int node) { return SlotRef{SlotRef::kBracketWinner, node, 0}; }

} // namespace

std::vector<Fixture> generateSchedule(const std::array<std::uint16_t, 32>& groups,
                                      std::uint32_t seed) {
    std::vector<Fixture> fixtures;
    (void)groups; // fixtures reference team SLOTS (g*4+pos); id resolution is the caller's job
    fixtures.reserve(64);

    // Group stage: 8 x round-robin (matchday pairing = app-side convention; venue patterns
    // HOME_AWAY... of §3 vocabulary unproven per fixture).
    static constexpr int kMd[3][2][2] = {{{0, 3}, {1, 2}}, {{0, 2}, {1, 3}}, {{0, 1}, {2, 3}}};
    for (int g = 0; g < 8; ++g)
        for (int md = 0; md < 3; ++md)
            for (int k = 0; k < 2; ++k) {
                Fixture f;
                f.group = g;
                f.matchday = md + 1;
                f.home = SlotRef{SlotRef::kTeamSlot, g * 4 + kMd[md][k][0], 0};
                f.away = SlotRef{SlotRef::kTeamSlot, g * 4 + kMd[md][k][1], 0};
                fixtures.push_back(f);
            }

    // Knockout: node indices match KnockoutBracket::makeTree(16) layout (R16=0..7, QF=8..11,
    // SF=12..13, Final=14, third-place=15). R16 slots permuted by kBracketOrder.
    for (int i = 0; i < 8; ++i) {
        Fixture f;
        f.bracketNode = i;
        f.home = kR16Feed[kBracketOrder[i]].first;
        f.away = kR16Feed[kBracketOrder[i]].second;
        fixtures.push_back(f);
    }
    for (int q = 0; q < 4; ++q) {
        Fixture f;
        f.bracketNode = 8 + q;
        f.home = winner(2 * q);
        f.away = winner(2 * q + 1);
        fixtures.push_back(f);
    }
    for (int s = 0; s < 2; ++s) {
        Fixture f;
        f.bracketNode = 12 + s;
        f.home = winner(8 + 2 * s);
        f.away = winner(8 + 2 * s + 1);
        fixtures.push_back(f);
    }
    { // THIRDROUND_PLAYOFF: semi-final losers (SlotRef carries the SF nodes; propagation via setLoser)
        Fixture f;
        f.bracketNode = 15;
        f.home = winner(12);
        f.away = winner(13);
        fixtures.push_back(f);
    }
    { // Final
        Fixture f;
        f.bracketNode = 14;
        f.home = winner(12);
        f.away = winner(13);
        fixtures.push_back(f);
    }
    std::mt19937 rng(seed);
    for (std::size_t i = 47; i > 0; --i) {
        std::uniform_int_distribution<std::size_t> d(0, i);
        std::swap(fixtures[i], fixtures[d(rng)]);
    }
    return fixtures;
}

} // namespace wc98::tournament
