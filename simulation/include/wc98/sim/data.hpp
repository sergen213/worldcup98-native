#pragma once
// Read-only match inputs loaded from the user's game files by the caller (never hashed, never
// mutated by the simulation). Subsystem owners append the loaded tables they need.
#include <cstdint>
#include <filesystem>
#include <vector>

#include "wc98/formats/fcdb.hpp"
#include "wc98/formats/gen4tbls.hpp"
#include "wc98/sim/anim.hpp"

namespace wc98::sim {

// One side's team sheet as the front end hands it to the match: the FCDB team record and its
// squad in roster order (the first 11 start). Team_buildMatchPlayerAttrs 0x478B08 turns these
// into the 0x44-byte match attribute records.
struct TeamSheet {
    formats::FcdbTeam team;
    std::vector<formats::FcdbPlayer> squad;
};

struct MatchData {
    TeamSheet sheets[2];
    formats::Gen4Tables formations; // GEN4TBLS.T (Formation_loadTables 0x4CE374)
    // --- SpAnim: AISEQS.TBI sequences + MOAI.VIV clips (loadMatchAnimData, anim.hpp) ---
    AnimData anim;
};

formats::FcdbDatabase loadFcdb(const std::filesystem::path& gameDir); // DATA/CMN/FE/FCDB.DBI
// Sheet for db.teams[teamIndex], squad in roster order.
TeamSheet makeTeamSheet(const formats::FcdbDatabase& db, std::size_t teamIndex);
// Reads <gameDir>/DATA/CMN/AI/GEN4TBLS.T (case-insensitive lookup) into d.formations; false on failure.
bool loadFormationTables(MatchData& d, const std::filesystem::path& gameDir);

} // namespace wc98::sim
