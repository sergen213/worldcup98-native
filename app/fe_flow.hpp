#pragma once
// Front-end flow (M4/M9 slice): the windowed shell's screen state machine. Loads FE/PACK/
// LAYOUTS/LAYOUTS.BIG through the tolerant BIG reader, composites the main-menu screen
// (FEFLOW05) with presentation::composite, and drives menu selection + entry points.
// ponytail: minimal by design — a data-driven FE graph (LAY button objects -> command ids,
// FC*.BIN text) is an open item in LAY_COMPOSITOR_NOTES.md; promote this table when it lands.
#include "wc98/formats/layout.hpp"
#include "wc98/presentation/fe_screen.hpp"
#include "team_select.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <array>
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
    // lang = FC*.BIN UI-string code (eng/fre/ger/ita/spa/dut/dan/swe/jap; registry Language key).
    FeFlow(render::Renderer& r, std::filesystem::path tree, std::string lang = "eng");
    ~FeFlow(); // out-of-line: unique_ptr<TourneyModel> (defined in fe_flow.cpp)

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
    // Gate evidence: UI string bank in use + how many exe font-table faces the tree carries.
    [[nodiscard]] std::string_view language() const { return text_ ? text_->language() : std::string_view{}; }
    [[nodiscard]] std::size_t fontsLoaded() const { return text_ ? text_->facesLoaded() : 0; }
    // Between-round tournament presentation (LAY_COMPOSITOR_NOTES.md "Tournament presentation
    // scenes" section): after each round the original FE shows group standings / knockout bracket
    // screens (FEFLOW35, FEFLOW25, FEFLOW26 + wcup trophy slot, FEFLOW27 full bracket + chmp).
    int tourneyPresentCount(); // lazy ensureTourney: pool + layouts -> kTourneyRounds, else 0
    [[nodiscard]] const char* tourneyPresentName(int round) const; // "FEFLOW35" ... gate evidence label
    presentation::FeDrawStats drawTourneyPresent(render::SpriteBatch& ui, int round); // builds the model lazily

    static constexpr int kTourneyRounds = 4; // group stage, R16, QF, final boundaries -> 35/25/26/27
    struct TourneyModel;                     // deterministic schedule/standings/bracket (fe_flow.cpp)
    bool ensureTourney();                    // lazy: pool -> model + layouts FEFLOW35/25/26/27 (non-throwing)
    presentation::FeDrawStats drawStandings(render::SpriteBatch& ui, const formats::ScreenLayout& sc) const;   // FEFLOW35 overlay
    presentation::FeDrawStats drawBracketBoard(render::SpriteBatch& ui, int round) const;                       // 25/26/27 overlays
    void loadTeams(); // ZWCUP.RES pool + FCDB STR0 names (non-throwing; empty = no team-select)
    presentation::FeDrawStats drawTeamsGrid(render::SpriteBatch& ui) const; // FEFLOW11 composite + dynamic rows
    void drawCursor(render::SpriteBatch& ui, presentation::FeDrawStats& st) const; // MAUS.PAK arrow at the pointer
    void setPointer(int x, int y) { ptrX_ = x; ptrY_ = y; } // MAUS arrow position (virtual 640x480)
    std::string poolName(std::uint32_t id) const; // FCDB fullName for a pool id (""-name fallback)
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
    presentation::FeCursor cursor_;      // MAUS.PAK art (absent = no sprite)
    render::TextureId cursorTex_ = 0;    // 0 = not uploaded (no art on this tree)
    int ptrX_ = 0, ptrY_ = 0;            // pointer position in virtual 640x480 pixels
    std::unique_ptr<TourneyModel> tourney_;  // built on first presentation draw (fixed seeds, no RNG drift)
    bool haveTourney_ = false;               // model + all four presentation layouts present
    std::array<formats::ScreenLayout, kTourneyRounds> presScreens_; // FEFLOW35/25/26/27 composites
};

} // namespace wc98::app
