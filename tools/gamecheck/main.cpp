// gamecheck - runtime-view gate over the game database (goal phases 3/8/11).
//
// Loads the SAME records the game plays with (wc98::sim::GameData = FCDB.DBI +
// FCDB_<LANG>.DBI STR0 + FCDBP<LANG>.DBI STR1), resolves every team roster slot to a
// player record, and reports kit-atlas availability against the on-disk texture set.
// --json emits a structured document the ctest gates diff against the extraction
// layer's own export (identity: UI lane == extraction lane).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "wc98/file.hpp"
#include "wc98/formats/fcdb.hpp"
#include "wc98/sim/data.hpp"

namespace fs = std::filesystem;

namespace {

std::string jsonEsc(std::string s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: wc98-gamecheck <tree> [--json <out.json>]\n";
        return 2;
    }
    const fs::path tree{argv[1]};
    fs::path jsonOut;
    if (argc >= 4 && std::strcmp(argv[2], "--json") == 0) jsonOut = argv[3];

    wc98::sim::GameData g;
    try {
        g = wc98::sim::loadGameData(tree, "ENG");
    } catch (const std::exception& e) {
        std::cerr << "gamecheck: " << e.what() << '\n';
        return 2;
    }

    const fs::path texDir = tree / "INGAME/PLAYER/TEXTURES";
    int unresolved = 0, kitHome = 0, kitAway = 0, refs = 0;

    std::ostringstream teamsJson;
    for (std::size_t i = 0; i < g.db.teams.size(); ++i) {
        const auto& t = g.db.teams[i];
        const std::string home = "PK" + std::format("{:03}", t.id) + "H";
        const std::string away = "PK" + std::format("{:03}", t.id) + "A";
        const bool h = !wc98::findCaseInsensitive(texDir, home + ".FSH").empty();
        const bool a = !wc98::findCaseInsensitive(texDir, away + ".FSH").empty();
        kitHome += h; kitAway += a;
        int squad = 0;
        std::ostringstream rosterJson;
        for (std::size_t s = 0; s < t.roster.size(); ++s) {
            const std::uint16_t id = t.roster[s];
            if (id == 0) continue; // empty slot (league-config padding)
            ++refs;
            const auto it = std::ranges::find(g.db.players, id, &wc98::formats::FcdbPlayer::id);
            if (it == g.db.players.end()) { ++unresolved; continue; }
            const std::size_t idx = std::size_t(std::distance(g.db.players.begin(), it));
            ++squad;
            if (rosterJson.str().size() < 4000) { // cap inline dump: first 3 slots only
                if (squad <= 3)
                    rosterJson << (squad > 1 ? "," : "") << "{\"id\":" << id << ",\"shirt\":" << int(it->shirtNumber)
                               << ",\"name\":\"" << jsonEsc(wc98::formats::cp1252ToUtf8(g.playerName(idx))) << "\"}";
            }
        }
        teamsJson << (i ? "," : "") << "{\"index\":" << i << ",\"id\":" << t.id
                  << ",\"fullName\":\"" << jsonEsc(wc98::formats::cp1252ToUtf8(g.fullName(i)))
                  << "\",\"abbreviation\":\"" << jsonEsc(wc98::formats::cp1252ToUtf8(g.abbreviation(i)))
                  << "\",\"kitHome\":" << (h ? "true" : "false") << ",\"kitAway\":" << (a ? "true" : "false")
                  << ",\"squadSize\":" << squad << ",\"roster\":[" << rosterJson.str() << "]}";
    }

    std::ostringstream doc;
    doc << "{\n\"provenance\": {\"db\": \"" << jsonEsc(g.dbPath.string()) << "\", \"dbBytes\": " << g.dbBytes
        << ", \"names\": \"" << jsonEsc(g.namesPath.string()) << "\", \"playerNames\": \""
        << jsonEsc(g.playerNamesPath.string()) << "\", \"language\": \"" << g.language << "\"},\n"
        << "\"teams\": " << g.db.teams.size() << ",\n\"players\": " << g.db.players.size() << ",\n"
        << "\"rosterRefs\": " << refs << ",\n\"unresolved\": " << unresolved << ",\n"
        << "\"kitHomeFound\": " << kitHome << ",\n\"kitAwayFound\": " << kitAway << ",\n"
        << "\"teamRows\": [" << teamsJson.str() << "]\n}\n";

    if (!jsonOut.empty()) {
        std::ofstream f(jsonOut, std::ios::binary);
        if (!f) { std::cerr << "gamecheck: cannot write " << jsonOut.string() << '\n'; return 2; }
        f << doc.str();
        std::cout << "gamecheck: wrote " << jsonOut.string() << '\n';
    }
    std::cout << "teams=" << g.db.teams.size() << " players=" << g.db.players.size()
              << " rosterRefs=" << refs << " unresolved=" << unresolved
              << " kitHome=" << kitHome << " kitAway=" << kitAway << " lang=" << g.language << '\n';
    return unresolved == 0 ? 0 : 1;
}
