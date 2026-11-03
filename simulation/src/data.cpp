// Loading MatchData team sheets from the user's FCDB.DBI.
#include <stdexcept>

#include "wc98/file.hpp"
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

} // namespace wc98::sim
