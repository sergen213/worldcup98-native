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

// FE menu entries the shell can launch. Index = selection state. The Windows mainmenu scene
// (FE_MENU_SCREENS_WC98.md §4) draws four items, in registry string order: friendly -> feflow09
// setup, league play -> feflow73+ league scenes, cup competition -> world-cup family, practise ->
// feflow50 setup.
enum class FeEntry : int { Exhibition = 0, LeaguePlay = 1, Tournament = 2, Practise = 3, Count };
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
    // Active screen layout name for the gate evidence line ("FEFLOW05" menu / "FEFLOW11" team-select /
    // "FEFLOW09"/"FEFLOW50" friendly & practise setup screens).
    [[nodiscard]] const char* screenName() const {
        return useCapture_ ? captureName_.c_str() : teamScreen_ ? "FEFLOW11" : setupScreen_ ? (setupPractise_ ? "FEFLOW50" : "FEFLOW09") : "FEFLOW05";
    }

    // Arbitrary-screen capture lane (--fe-screen NAME; "make all menus 1:1"): loads a chosen
    // LAYOUTS.BIG member and composites it through the main-menu art pipeline. The scene-ownership
    // whitelist is NOT applied — every shown() object draws, labels via FeText (reference captures
    // for visual review). loadCaptureScreen false + `err` = load failure ("no such archive
    // member", parse errors); the lane maps it to exit code 2.
    bool loadCaptureScreen(std::string_view name, std::string& err);
    presentation::FeDrawStats drawCaptureScreen(render::SpriteBatch& ui) const;

    void move(int delta);              // arrows: cycle menu selection / setup-screen config rows
    [[nodiscard]] bool onSetup() const { return setupScreen_; } // friendly/practise config screen shown
    void adjustValue(int dir);         // left/right on the setup screen: cycle one option per row
    [[nodiscard]] int selected() const { return sel_; }
    // Team-select screen = the authentic FEFLOW11.LAY grid (LAY_COMPOSITOR_NOTES.md): Enter on
    // Tournament opens it; arrows/pad move the cursor through the ZWCUP pool (pages of 8 rows,
    // matching the LAY's 8 row slots), Enter confirms a pick, Esc returns.
    bool enter(); // Enter/confirm; true = shell launches selected entry (Tournament uses pickedTeam())
    void back();  // Esc: team-select/setup -> main menu (no-op on the menu)
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
    presentation::FeDrawStats drawSetup(render::SpriteBatch& ui) const; // FEFLOW09/50 composite + flags, names, row hilite
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
    // --fe-screen capture lane: injected LAY member + active flag (screenName() gate evidence).
    formats::ScreenLayout captureScreen_;
    bool useCapture_ = false;
    std::string captureName_;
    // Friendly/practise match-setup screens (FE_MENU_SCREENS_WC98.md §5): FEFLOW09.LAY and
    // FEFLOW50.LAY share the widget grammar — four config rows (ovr/lrp/rrp/alp/arp/anm/arw/ctx{n}),
    // team shields ltfg/rtfg + name slots tltx/trtx, right-column nav icons (quit, fe06, fe15,
    // fe07, fe08). Netplay chat widgets (chat/cha2/cha3) and the mascot placeholder stay hidden.
    formats::ScreenLayout setup09_, setup50_;
    bool haveSetup_ = false;   // at least one of the two layouts loaded
    bool setupScreen_ = false; // friendly/practise setup shown (FEFLOW09/50)
    bool setupPractise_ = false; // true = FEFLOW50, false = FEFLOW09
    int setupRow_ = 0;             // highlighted config row (arrows), 4 rows
    int setupValue_[4] = {0, 0, 0, 0}; // per-row option index (left/right cycle)
    // Scene-ownership visibility for the menu composite (composite() `visible`): FEFLOW05.LAY is
    // MNMSETUP.LAY — a netplay-setup layout whose 26 objects belong to scenes the port never
    // shows. Only its FE-flow art objects (back collage, titl logos, flag anim) are painted on
    // the menu screen; the rest count as hidden (LAY_COMPOSITOR_NOTES.md "Main-menu scene").
    std::vector<std::string> menuVisible_{"back", "titl", "flag"};
    // Setup-screen composite whitelist (composite() `visible`): the FE-flow art objects of
    // FEFLOW09/50 — background, flag anim, title, row panes/arrows, nav icons. The netplay chat
    // widgets and the mascot placeholder belong to scenes the port never shows (hidden).
    std::vector<std::string> setupVisible_{"back", "flag", "titl",  "ltfg", "rtfg",      "tltx", "trtx",
                                           "ovr0", "lrp0", "rrp0",  "alp0", "arp0",      "arw0", "ctx0",
                                           "ovr1", "lrp1", "rrp1",  "alp1", "arp1",      "arw1", "ctx1",
                                           "ovr2", "lrp2", "rrp2",  "alp2", "arp2",      "arw2", "ctx2",
                                           "ovr3", "lrp3", "rrp3",  "alp3", "arp3",      "arw3", "ctx3",
                                           "grv0", "grv1", "grv2",  "grv3", "quit",      "fe06", "fe15",
                                           "fe07", "fe08", "tmtl",  "tmtr", "npll",      "nplr", "help"};
    int sel_ = 0;
    std::unique_ptr<presentation::FeText> text_; // FFN face + FC*.BIN strings, loaded once (fe_screen.hpp)
    bool teamScreen_ = false;              // true = team-select list, false = FEFLOW05 main menu
    std::vector<std::uint16_t> poolIds_;   // ZWCUP.RES pool order (40 entries)
    std::vector<std::string> poolNames_;   // FCDB STR0 fullName per entry ("" when the DBI is absent)
    // Flag art file per pool entry: "FNNN" = the team's record INDEX in FCDB.DBI (measured:
    // F030.FSH f000 = Argentina shield, F031.FSH = Australia; their DBI ids are 86/87).
    std::vector<std::string> poolFlagFile_;
    // "FNNN" art name for a cup-pool team id ("" when the pool has no entry). NNN = FCDB record index.
    std::string flagFileForTeam(std::uint32_t id) const;
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
