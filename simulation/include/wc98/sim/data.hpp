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

// The one database the whole game reads: FCDB.DBI + the localized STR0/STR1 name
// tables (FE/FEFLOW73/74/79/80 rows and the IG lanes draw from these same records the
// match plays with). Provenance = the exact files loaded, never re-derived per screen.
struct GameData {
    formats::FcdbDatabase db;
    formats::FcdbNames teamNames;                 // COMMON/FCDB_<LANG>.DBI STR0
    std::vector<std::string> playerNames;         // COMMON/FCDBP<LANG>.DBI STR1 (parallel to db.players)
    std::string language;                         // upper-case LANG actually used
    std::filesystem::path dbPath, namesPath, playerNamesPath; // provenance
    std::size_t dbBytes = 0;

    bool teamIndexForId(std::uint16_t id, std::size_t& out) const; // record index of a ZWCUP pool id
    std::string fullName(std::size_t teamIndex) const;
    std::string abbreviation(std::size_t teamIndex) const;
    std::string playerName(std::size_t playerIndex) const; // '' when the name table is absent
};

// Loads the DB + name pair for one tree; lang upper-cases to FCDB_<LANG>.DBI with
// FCDB_ENG.DBI fallback. Throws if FCDB.DBI is missing (same contract as loadFcdb).
GameData loadGameData(const std::filesystem::path& gameDir, std::string_view lang = "ENG");

formats::FcdbDatabase loadFcdb(const std::filesystem::path& gameDir); // COMMON/FCDB.DBI
// Sheet for db.teams[teamIndex], squad in roster order.
TeamSheet makeTeamSheet(const formats::FcdbDatabase& db, std::size_t teamIndex);
// Reads <gameDir>/DATA/CMN/AI/GEN4TBLS.T (case-insensitive lookup) into d.formations; false on failure.
bool loadFormationTables(MatchData& d, const std::filesystem::path& gameDir);

} // namespace wc98::sim
