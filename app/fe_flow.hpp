#pragma once
// Front-end flow (M4/M9 slice): the windowed shell's screen state machine. Loads FE/PACK/
// LAYOUTS/LAYOUTS.BIG through the tolerant BIG reader, composites the main-menu screen
// (FEFLOW05) with presentation::composite, and drives menu selection + entry points.
// ponytail: minimal by design — a data-driven FE graph (LAY button objects -> command ids,
// FC*.BIN text) is an open item in LAY_COMPOSITOR_NOTES.md; promote this table when it lands.
#include "wc98/formats/layout.hpp"
#include "wc98/presentation/fe_screen.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include "team_select.hpp"
#include <cstdint>
#include <vector>

namespace wc98::render {
class Renderer;
class SpriteBatch;
} // namespace wc98::render

namespace wc98::app {

// FE menu entries the shell can launch. Index = selection state.
enum class FeEntry : int { Exhibition = 0, Tournament = 1, Count };
constexpr int kFeEntryCount = int(FeEntry::Count);

class FeFlow {
public:
    // tree = extracted game tree (FE/PACK/LAYOUTS/LAYOUTS.BIG + art under it).
    FeFlow(render::Renderer& r, std::filesystem::path tree);

    // Composites the current screen into `ui` and returns draw stats (shapes > 0 = real art drew).
    presentation::FeDrawStats drawCurrent(render::SpriteBatch& ui) const;
    [[nodiscard]] const std::string& lastError() const { return lastError_; } // menu load failure reason ("" = ok)
    // Active screen layout name for the gate evidence line ("FEFLOW05" menu / "FEFLOW11" team-select).
    [[nodiscard]] const char* screenName() const { return teamScreen_ ? "FEFLOW11" : "FEFLOW05"; }

    void move(int delta);              // arrow keys: cycle menu selection
    [[nodiscard]] int selected() const { return sel_; }
    // Team-select screen = the authentic FEFLOW11.LAY grid (LAY_COMPOSITOR_NOTES.md): Enter on
    // Tournament opens it; arrows/pad move the cursor through the ZWCUP pool (pages of 8 rows,
    // matching the LAY's 8 row slots), Enter confirms a pick, Esc returns.
    bool enter(); // Enter/confirm; true = shell launches selected entry (Tournament uses pickedTeam())
    void back();  // Esc: team-select -> main menu (no-op on the menu)
    [[nodiscard]] int pickedTeam() const { return pick_; } // confirmed FCDB id (-1 = no pick)

private:
    void loadTeams(); // ZWCUP.RES pool + FCDB STR0 names (non-throwing; empty = no team-select)
    presentation::FeDrawStats drawTeamsGrid(render::SpriteBatch& ui) const; // FEFLOW11 composite + dynamic rows
    presentation::FeDrawStats drawTeams(render::SpriteBatch& ui) const; // team-select frame (flat panel + FFN rows)
    std::filesystem::path tree_;
    presentation::ScreenArt art_; // texture handles cached per FSH member
    std::string lastError_; // set when LAYOUTS.BIG@FEFLOW05.LAY cannot be loaded
    formats::ScreenLayout screen_;     // main-menu layout (FEFLOW05)
    formats::ScreenLayout gridScreen_;     // team-select layout (FEFLOW11.LAY, world-cup pick grid)
    bool haveGrid_ = false;
    bool haveScreen_ = false;
    int sel_ = 0;
    std::unique_ptr<presentation::FeText> text_; // FFN face + FC*.BIN strings, loaded once (fe_screen.hpp)
    bool teamScreen_ = false;              // true = team-select list, false = FEFLOW05 main menu
    std::vector<std::uint16_t> poolIds_;   // ZWCUP.RES pool order (40 entries)
    std::vector<std::string> poolNames_;   // FCDB STR0 fullName per entry ("" when the DBI is absent)
    TeamListNav teams_;                    // paging cursor (team_select.hpp)
    int pick_ = -1;                        // confirmed team id (-1 = none)
};

} // namespace wc98::app
