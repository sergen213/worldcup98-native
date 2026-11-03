// Loading MatchData formation tables from the user's GEN4TBLS.T.
#include <exception>

#include "wc98/file.hpp"
#include "wc98/sim/data.hpp"

namespace wc98::sim {

bool loadFormationTables(MatchData& d, const std::filesystem::path& gameDir) {
    // WC98 tree layout (SIM_PORT_NOTES.md): the G98*.FMT members live in the COMMON/GEN4TBLS.VIV BIGF.
    const auto p = findCaseInsensitive(gameDir, "COMMON/GEN4TBLS.VIV");
    if (p.empty()) return false;
    try {
        d.formations = formats::Gen4Tables::parse(readFile(p));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

} // namespace wc98::sim
