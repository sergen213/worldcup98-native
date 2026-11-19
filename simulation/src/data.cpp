// Loading MatchData team sheets from the user's FCDB.DBI.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "wc98/file.hpp"
#include "wc98/formats/fcdb.hpp"
#include "wc98/sim/data.hpp"

namespace wc98::sim {

TeamSheet makeTeamSheet(const formats::FcdbDatabase& db, std::size_t teamIndex) {
    if (teamIndex >= db.teams.size()) throw std::runtime_error("team index out of range");
    TeamSheet s;
    s.team = db.teams[teamIndex];
    for (std::uint16_t id : s.team.roster)
        for (const auto& p : db.players)
            if (p.id == id) {
                s.squad.push_back(p);
                break;
            }
    return s;
}

formats::FcdbDatabase loadFcdb(const std::filesystem::path& gameDir) {
    // WC98 tree layout (SIM_PORT_NOTES.md): the team/player DB is COMMON/FCDB.DBI.
    const auto p = findCaseInsensitive(gameDir, "COMMON/FCDB.DBI");
    if (p.empty()) throw std::runtime_error("FCDB.DBI not found under " + gameDir.string());
    return formats::FcdbDatabase::parse(readFile(p));
}

bool GameData::teamIndexForId(std::uint16_t id, std::size_t& out) const {
    const auto it = std::ranges::find(db.teams, id, &formats::FcdbTeam::id);
    if (it == db.teams.end()) return false;
    out = std::size_t(std::distance(db.teams.begin(), it));
    return true;
}

std::string GameData::fullName(std::size_t teamIndex) const {
    return teamIndex < teamNames.teams.size() ? teamNames.teams[teamIndex].fullName : std::string();
}

std::string GameData::abbreviation(std::size_t teamIndex) const {
    return teamIndex < teamNames.teams.size() ? teamNames.teams[teamIndex].abbreviation : std::string();
}

std::string GameData::playerName(std::size_t playerIndex) const {
    return playerIndex < playerNames.size() ? playerNames[playerIndex] : std::string();
}

GameData loadGameData(const std::filesystem::path& gameDir, std::string_view lang) {
    std::string up(lang);
    for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
    GameData g;
    g.language = up;
    g.dbPath = findCaseInsensitive(gameDir, "COMMON/FCDB.DBI");
    if (g.dbPath.empty()) throw std::runtime_error("FCDB.DBI not found under " + gameDir.string());
    const auto dbRaw = readFile(g.dbPath);
    g.dbBytes = dbRaw.size();
    g.db = formats::FcdbDatabase::parse(dbRaw);
    g.namesPath = findCaseInsensitive(gameDir, "COMMON/FCDB_" + up + ".DBI");
    if (g.namesPath.empty()) {
        g.namesPath = findCaseInsensitive(gameDir, "COMMON/FCDB_ENG.DBI");
        if (!g.namesPath.empty()) g.language = "ENG"; // language table absent: ENG fallback (exe FUN_0047ac14 lane)
    }
    if (!g.namesPath.empty()) g.teamNames = formats::FcdbNames::parse(readFile(g.namesPath));
    g.playerNamesPath = findCaseInsensitive(gameDir, "COMMON/FCDBP" + up + ".DBI");
    if (g.playerNamesPath.empty()) g.playerNamesPath = findCaseInsensitive(gameDir, "COMMON/FCDBPENG.DBI");
    if (!g.playerNamesPath.empty()) {
        try { g.playerNames = formats::FcdbNames::parse(readFile(g.playerNamesPath)).players; } catch (const std::exception&) {}
    }
    return g;
}

} // namespace wc98::sim
