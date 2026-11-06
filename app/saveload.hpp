#pragma once
// App-level tournament save/load over USER/SLOT%.2d.SAV (docs/reverse-engineering/
// SAVESLOT_WC98.md): the FE "Tournament" entry offers New / Load; the progression driver
// persists its state to a slot after each completed round, and Load restores it.
#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <filesystem>
#include <vector>

#include "wc98/tournament/tournament.hpp"

namespace wc98::app {

struct TournamentState {
    std::array<std::uint16_t, 32> groups{};
    std::uint32_t seed = 0;
    // Group matches played, concatenated in group order (schedule order within each group).
    std::vector<wc98::tournament::MatchResult> groupResults;
    std::array<std::uint32_t, 16> koWinner{}, koLoser{};  // kNoTeam = node not decided
    std::uint32_t champion = wc98::tournament::kNoTeam;
    // MatchResult has no declared ==; the implicit aggregate one is invisible to vector== here.
    bool operator==(const TournamentState& o) const {
        return seed == o.seed && champion == o.champion && groups == o.groups && koWinner == o.koWinner && koLoser == o.koLoser &&
               std::ranges::equal(groupResults, o.groupResults, [](const wc98::tournament::MatchResult& a, const wc98::tournament::MatchResult& b) {
                   return a.teamA == b.teamA && a.teamB == b.teamB && a.goalsA == b.goalsA && a.goalsB == b.goalsB;
               });
    }
};

// Slot payload image layout (little endian): groups[32] u16, u32 seed, u32 nGroup, then nGroup *
// {u16 teamA, u16 teamB, u8 goalsA, u8 goalsB}, koWinner[16] u32, koLoser[16] u32, u32 champion.
std::vector<std::uint8_t> encodeState(const TournamentState& st);
TournamentState decodeState(std::span<const std::uint8_t> image);  // FormatError on size mismatch

// Summary record fields (rec+0x08..0x18, six u32; Wine-oracle semantics unproven — this port's
// documented mapping): [0]=mode (1=tournament), [1]=group matches played, [2]=knockout nodes
// decided, [3]=champion id (kNoTeam if none), [4]=schedule seed, [5]=0.
std::array<std::uint32_t, 6> summaryFields(const TournamentState& st);

std::filesystem::path defaultSlotDir();  // userDirs("wc98-native").data / "user"
void saveTournament(const std::filesystem::path& path, const TournamentState& st);
TournamentState loadTournament(const std::filesystem::path& path);

// Headless scenario gate (no tree needed): new tournament -> play round 1 (matchday-1 fixtures)
// -> save slot -> load -> state identical; then complete the tournament, persist final state,
// reload and verify. Prints lines per step; returns 0 on success, 1 on any mismatch.
int runTournamentSaveLoad(int slot, const std::filesystem::path& dir);

// FE Load entry: ensure a scenario save exists for `slot`, load it, print summary fields and
// state-identical verdict. Returns 0/1 like runTournamentSaveLoad.
int runTournamentLoadVerify(int slot);

}  // namespace wc98::app
