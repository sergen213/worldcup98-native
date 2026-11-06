// Knockout bracket node graph — spec §2.6/§4: slots sentinel 0xFFFFFFFF (FUN_0048d084), winner ->
// parent slot. Per-round propagation chain in the original was not decompiled (§5.5); this is the
// app-side model the port uses.
#include "wc98/tournament/tournament.hpp"

namespace wc98::tournament {

KnockoutBracket KnockoutBracket::makeTree(std::vector<std::uint32_t> seeds, bool thirdPlace) {
    KnockoutBracket b;
    const std::size_t leaves = seeds.size();
    if (leaves < 4 || (leaves & (leaves - 1)) != 0) return b; // power of two >= 4

    std::vector<std::size_t> levelStart, levelCount;
    for (std::size_t m = leaves / 2, off = 0; m >= 1; m /= 2) {
        levelStart.push_back(off);
        levelCount.push_back(m);
        off += m;
    }
    b.nodes_.resize(levelStart.back() + levelCount.back());

    // Level 0: first-round matches, adjacent seed pairs (balanced tree).
    for (std::size_t k = 0; k < leaves / 2; ++k) {
        BracketNode& n = b.nodes_[levelStart[0] + k];
        n.teamA = seeds[2 * k];
        n.teamB = seeds[2 * k + 1];
    }
    for (std::size_t l = 0; l < levelStart.size(); ++l) {
        const unsigned participants = static_cast<unsigned>(leaves) >> l; // slots in this round
        for (std::size_t k = 0; k < levelCount[l]; ++k) {
            BracketNode& n = b.nodes_[levelStart[l] + k];
            n.round = participants >= 16 ? KnockoutRound::kRoundOf16
                    : participants == 8   ? KnockoutRound::kQuarterFinal
                    : participants == 4   ? KnockoutRound::kSemiFinal
                                          : KnockoutRound::kFinal;
            if (l + 1 < levelStart.size()) { // winner feeds the parent block, slot by parity
                const int p = static_cast<int>(levelStart[l + 1] + k / 2);
                n.parent = p;
                n.slotInParent = static_cast<int>(k % 2);
            }
        }
    }
    b.final_ = static_cast<int>(levelStart.back());

    if (thirdPlace && levelStart.size() >= 3) { // needs semi-final nodes to hand losers over
        const int tpIdx = static_cast<int>(b.nodes_.size());
        BracketNode tp;
        tp.round = KnockoutRound::kThirdPlace; // THIRDROUND_PLAYOFF token (§3 vocabulary)
        b.nodes_.push_back(tp);
        b.third_ = tpIdx;
        const std::size_t sf = levelStart.size() - 2; // semi-final level (participants == 4)
        for (std::size_t k = 0; k < levelCount[sf]; ++k)
            b.nodes_[levelStart[sf] + k].losersNode = tpIdx;
    }
    return b;
}

KnockoutBracket KnockoutBracket::wc98() {
    // Real-1998 wiring lives in the schedule table (schedule.cpp kR16Feed/kQfFeed); the bracket
    // graph itself is seeded from group results, so an empty 16-slot tree is the model here.
    return makeTree(std::vector<std::uint32_t>(16, kNoTeam), true);
}

bool KnockoutBracket::setWinner(std::size_t idx, std::uint32_t team) {
    if (idx >= nodes_.size()) return false;
    BracketNode& n = nodes_[idx];
    if (n.teamA == kNoTeam || n.teamB == kNoTeam) return false; // not playable
    if (team != n.teamA && team != n.teamB) return false;
    if (n.parent < 0) {
        if (n.round == KnockoutRound::kFinal) champion_ = team;
        return true;
    }
    BracketNode& p = nodes_[static_cast<std::size_t>(n.parent)];
    if (n.slotInParent == 0) p.teamA = team; else p.teamB = team; // child's slot, not parent's
    return true;
}

bool KnockoutBracket::setLoser(std::size_t idx, std::uint32_t team) {
    if (idx >= nodes_.size()) return false;
    BracketNode& n = nodes_[idx];
    if (n.losersNode < 0 || n.teamA == kNoTeam || n.teamB == kNoTeam) return false;
    if (team != n.teamA && team != n.teamB) return false;
    BracketNode& t = nodes_[static_cast<std::size_t>(n.losersNode)];
    if (t.teamA == kNoTeam) { t.teamA = team; return true; }
    if (t.teamB == kNoTeam) { t.teamB = team; return true; }
    return false; // target full: nowhere to propagate
}

} // namespace wc98::tournament
