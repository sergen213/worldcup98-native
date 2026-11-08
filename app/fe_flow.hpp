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
#include <optional>
#include <string>
#include <string_view>
#include <array>
#include <cstdint>
#include <vector>

namespace wc98::render {
class Renderer;
class SpriteBatch;
} // namespace wc98::render
namespace wc98::config { struct Settings; }   // settings.ini model (config/include/wc98/config/settings.hpp)
namespace wc98::audio  { class Mixer; }       // audio mixer for live volume updates (audio/include/wc98/audio/mixer.hpp)

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
    // Active screen layout name for the gate evidence line ("FEFLOW06" hub / "FEFLOW11" team-select /
    // "FEFLOW09"/"FEFLOW50" friendly & practise setup screens / "FEFLOW15" options / "FEFLOW89"
    // save-load entry / "FEFLOW19" credits).
    [[nodiscard]] const char* screenName() const {
        return useCapture_          ? captureName_.c_str()
               : leagueCfg_         ? cfgName_.c_str()
               : wcCfg_             ? cfgName_.c_str()
               : cheatsView_        ? "FEFLOW95"
               : teamScreen_        ? "FEFLOW11"
               : leagueView_        ? "FEFLOW73"
               : setupScreen_       ? (setupPractise_ ? "FEFLOW50" : "FEFLOW09")
               : optsView_          ? "FEFLOW15"
               : slView_            ? "FEFLOW89"
               : credView_          ? "FEFLOW19"
               : (haveHub_ && hubView_) ? "FEFLOW06"
                                        : "FEFLOW05";
    }

    // ---- World-cup family interactive screens (FEFLOW18/23/24/35; MENU_NAV_WC98.md rank-2) -----
    // Keyloop RE analysis/pass12/wcfamily_notes.txt: feflow18 'rand' = staged random-draw refresh of
    // the 32 tmNN zone slots (counter cap 99 -> auto-advance); feflow23/24 'strk'/'stsc' = bracket-board
    // confirm/stats buttons (+0x44/DAT_0060b4c8 stores, no keyloop row grid); feflow35 'nglt'/'ngrt' =
    // group-index mod-8 table repaint. openWcCfg opens one screen; drawWcFamily composites the scene-
    // owned whitelist and paints tm/gm/qtm/ss dynamic content as overlays (TourneyModel + pool data).
    bool openWcCfg(int screenId); // --fe-wc-cfg N gate; false = layout absent on this tree
    [[nodiscard]] bool onWcCfg() const { return wcCfg_ > 0; }
    presentation::FeDrawStats drawWcFamily(render::SpriteBatch& ui) const; // composite + row/bracket overlays

    // Arbitrary-screen capture lane (--fe-screen NAME; "make all menus 1:1"): loads a chosen
    // LAYOUTS.BIG member and composites it through the main-menu art pipeline. The scene-ownership
    // whitelist is NOT applied — every shown() object draws, labels via FeText (reference captures
    // for visual review). loadCaptureScreen false + `err` = load failure ("no such archive
    // member", parse errors); the lane maps it to exit code 2.
    bool loadCaptureScreen(std::string_view name, std::string& err);
    presentation::FeDrawStats drawCaptureScreen(render::SpriteBatch& ui) const;

    void move(int delta);              // arrows: cycle menu selection / setup-screen config rows
    [[nodiscard]] bool onSetup() const { return setupScreen_; } // friendly/practise config screen shown
    [[nodiscard]] bool onLeague() const { return leagueView_; } // FEFLOW73 league-select list shown
    void adjustValue(int dir);         // left/right on the setup screen: cycle one option per row
    // Launch entry for the shell (FeEntry index): the fallback menu cursor, or the hub button
    // mapping (fe07->Exhibition, fe11->Tournament, fe32->Practise; -1 = no-launch action).
    [[nodiscard]] int selected() const { return onHub() ? hubButtonEntry(hubButtonName(hubSel_)) : sel_; }
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
    // League Play select (FE_MENU_SCREENS_WC98.md §4/§8): Enter on the "League Play" entry opens
    // FEFLOW73.LAY — the authentic league list (pn00..pn08 rows, x=35 y=134..285). Rows are
    // code-driven kt/menu widgets; fixtures aren't simulated, so Enter on a row returns to the
    // menu (the Windows game chains into its league setup scenes with user-saved data).
    bool enterLeague();  // confirm on a list row: true = shell launches default match
    [[nodiscard]] int leagueSel() const { return leagueSel_; }
    void moveLeague(int dir);              // up/down through the pn00..pn08 rows
    presentation::FeDrawStats drawLeague(render::SpriteBatch& ui) const; // FEFLOW73 composite + row hilite
    // ---- League configuration family (FEFLOW72/74/75/76/79/80; MENU_PARITY.md §4 rank 7) -----
    // The FEFLOW73 scene cluster (analysis/pass12/edges_table.txt SCENEREG block: feflow72 @0x45a514,
    // feflow74 @0x45df84, feflow75 @0x45ee34, feflow79 @0x45ff14, feflow80 @0x461ed0 + registered
    // feflow76 aggression popup) opens these configs on Enter over the league list. Widget grammar:
    // analysis/pass12/list_painter_notes.txt (kt/menu rows 44 tricolour=1, sbar/sbNN sliders,
    // s001 KnobBar) + keyloop_notes.txt tags (gofd/gobk screen chain, ncml/ncmr pairs).
    bool openLeagueCfg(int screenId); // Enter on FEFLOW73 / --fe-league-cfg N gate; false = layout absent
    [[nodiscard]] bool onLeagueCfg() const { return leagueCfg_ > 0; }
    presentation::FeDrawStats drawLeagueCfg(render::SpriteBatch& ui) const; // composite + row/slider overlays

    static constexpr int kTourneyRounds = 4; // group stage, R16, QF, final boundaries -> 35/25/26/27
    struct TourneyModel;                     // deterministic schedule/standings/bracket (fe_flow.cpp)
    bool ensureTourney();                    // lazy: pool -> model + layouts FEFLOW35/25/26/27 (non-throwing)
    presentation::FeDrawStats drawStandings(render::SpriteBatch& ui, const formats::ScreenLayout& sc, int group) const; // FEFLOW35 overlay (group = table page)
    [[nodiscard]] std::vector<int> wcZones() const; // FEFLOW18 deterministic zones order (pool indices, re-shuffled per 'rand' press)
    presentation::FeDrawStats drawBracketBoard(render::SpriteBatch& ui, int round) const;                       // 25/26/27 overlays
    void loadTeams(); // ZWCUP.RES pool + FCDB STR0 names (non-throwing; empty = no team-select)
    presentation::FeDrawStats drawTeamsGrid(render::SpriteBatch& ui) const; // FEFLOW11 composite + dynamic rows
    presentation::FeDrawStats drawSetup(render::SpriteBatch& ui) const; // FEFLOW09/50 composite + flags, names, row hilite
    void drawCursor(render::SpriteBatch& ui, presentation::FeDrawStats& st) const; // MAUS.PAK arrow at the pointer
    void setPointer(int x, int y) { ptrX_ = x; ptrY_ = y; } // MAUS arrow position (virtual 640x480)
    // ---- Hub screen (Feflow06.lay) -------------------------------------------------------
    // The Windows hub layout (FE/PACK/LAYOUTS/LAYOUTS.BIG@Feflow06.lay, mixed-case member) is the
    // main-menu scene once present. 13 button widgets in LAY order: fe33,fe90,fe32,fe11,fe07,
    // fe04,fe02 (objecttype 32 Tbb) + quit,fe15,fe89,fe41,repl,cred (objecttype 6 nvicons). Each
    // carries released/hilited/pushed/disabled shape keys, FRAMES=a,b,c,d animation ranges and a
    // help=FC-index caption painted in the "help" pane (objecttype 14, x=160 y=444 w=300 h=40,
    // fontid=4, alignment=3). Button->scene edges: MENU_NAV_WC98.md §3/§7 + analysis/pass12/
    // edges_table.txt (fe07 -> feflow09 friendly setup, fe11 -> world-cup grid, fe32 -> feflow50);
    // the global-key hub table (FUN_00445664 EDX 2..0x1E) covers the same targets. Modes without a
    // port simulation (fe33 shootout, fe90 classic, fe04 modem, fe02 network, repl highlights,
    // fe89 team management) paint their DISABLEDSHAPE* and Enter is a documented no-op.
    [[nodiscard]] static std::string_view hubButtonName(int index);           // LAY order; "" = out of range
    [[nodiscard]] static int hubButtonIndex(std::string_view name);           // -1 when not a button
    // Hub button -> launch entry (FeEntry index): the edges feflow07/feflow11/feflow50 carry in the
    // scene registry; every other hub target is an in-place view change, not a shell launch.
    [[nodiscard]] static constexpr int hubButtonEntry(std::string_view name) { // FeEntry idx / -1 (unit-tested)
        if (formats::iequals(name, "fe07")) return 0; // Exhibition (friendly -> feflow09 setup)
        if (formats::iequals(name, "fe11")) return 2; // Tournament (world-cup grid / ZWCUP pool)
        if (formats::iequals(name, "fe32")) return 3; // Practise (feflow50 setup)
        return -1;                                    // quit/options/saveload/credits + disabled modes
    }
    [[nodiscard]] bool onHub() const { return haveHub_ && hubView_ && !optsView_ && !slView_ && !credView_; }
    [[nodiscard]] int hubCursor() const { return hubSel_; }
    [[nodiscard]] bool quitRequested() const { return quitRequested_; }
    void setPointerDown(bool down) { ptrDown_ = down; } // mouse button -> PUSHEDSHAPE* painting
    // Nav-axis mapping for the gate pre-actions (keeps fe_nav_* / fe_league gates valid on both
    // hub and FEFLOW05 fallback): 0 fe07, 1 FEFLOW73 list direct, 2 fe11, 3 fe32.
    void selectNav(int entry);
    // Mouse click on the active screen (main.cpp edge-detects SDL_BUTTON_LMASK): hit-tests the
    // button widgets under the pointer and performs Enter's action; returns the FeEntry index on
    // a match launch, -1 for no-launch actions.
    int pointerActivate();
    // ---- Cheats screen (feflow95.lay; MENU_PARITY.md §8) -------------------------------------
    // The hub global-key table posts payload 0x1E for this scene (MENU_NAV_WC98.md §3 row, evidence
    // 0x4456A3-0x4456A8), and those payloads are menu-context codes with no decoded keyboard mapping
    // ([H] note in that section), so the port opens it from a documented dev key: F9 in interactive FE,
    // Esc closes. Widgets: box1..box8 (objecttype 2 cheatbox `1box` frames) + btn0..btn7 objecttype 18
    // toggles (HILITEDSHAPEIDON=Xblak `blkx`, HILITEDSHAPEIDOFF=null = no glyph).
    bool openCheats(int onMask = 0); // --fe-cheats N / F9; false = layout absent on this tree.
                                     // `onMask` bit i starts row i ON (gate/dev pre-action [H]).
    [[nodiscard]] bool onCheats() const { return cheatsView_; }
    void moveCheat(int dir);   // up/down through the btn0..btn7 rows
    void toggleCheat(int row); // Enter/click on a row: flip its state (the engine's hilite-flag write)
    [[nodiscard]] bool cheatOn(int row) const { return row >= 0 && row < kCheats && cheatsOn_[std::size_t(row)]; }
    [[nodiscard]] int cheatCursor() const { return cheatRow_; }
    // Row captions the scene installs with FUN_004788b4 (widget fourcc 'txtN' + string pointer): these
    // are exe data strings, NOT FC-bank indices [C] FIFAWC.EXE VA 0x564014, 0x564020, 0x56402c,
    // 0x564038, 0x56404c, 0x564058, 0x564064, 0x564074 (DGROUP raw = VA - 0x401a00; byte-verified in
    // FIFAWC.EXE), installed by CALL->0x4788b4 sites 0x469b2d..0x469bb7 of feflow95's draw slot body
    // (analysis/pass12/edge2.txt:11631-11639); the pane title 'titl' string is "Cheats" @ VA 0x56400c.
    // The unavailable-row caption "-" (VA 0x564090, raw 0x162690) is installed by FUN_004788bc.
    static constexpr int kCheats = 8; // btn0..btn7 / txt0..txt7 rows
    static constexpr std::array<const char*, kCheats> kCheatLabels{
        "Take a Dive", "Hot Potato", "Big Heads", "Skeleton Players",
        "Crazy Ball", "Silly Moves", "Flaming Ball", "Alien Model"};
    static constexpr const char* kCheatTitle = "Cheats"; // 'titl' caption string (VA 0x56400c)
    // ---- Phase 2 options (FEFLOW15.LAY) / 3 save-load (Feflow89.LAY) / 4 credits (FEFLOW19) ----
    bool openOptions();  // Enter on fe15 / --fe-options; false when the layout is absent from the tree
    bool openSaveLoad(); // Enter on fe41 / --fe-saveload
    bool openCredits();  // Enter on cred / --fe-credits (FEFLOW19 auto-returns after its frames)
    [[nodiscard]] bool onOptions() const { return optsView_; }
    [[nodiscard]] bool onSaveLoad() const { return slView_; }
    [[nodiscard]] int saveSlot() const { return slSlot_; }
    [[nodiscard]] int optionTab() const { return optTab_; }
    void resetCursor();     // restore the menu-level cursor after a launch returns (hub fe07 / entry 0)
    void cycleTab(int dir); // options tab pages tbb1..4 ('ops2' event family; TAB key)
    [[nodiscard]] int optionRow() const { return optRow_; }
    // settings.ini [audio] master <-> audio::Mixer gain wiring (options volume row).
    void setAudioContext(config::Settings* settings, audio::Mixer* mixer) { settings_ = settings; mixer_ = mixer; }
    [[nodiscard]] bool audioDirty() const { return audioDirty_; }
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
    // League Play select (FEFLOW73.LAY): whitelist = background, banner, title, scrollbar and
    // the row/panel art; the scene-code captions belonging to other scenes stay hidden.
    formats::ScreenLayout league73_;
    bool haveLeague_ = false;
    bool leagueView_ = false;
    int leagueSel_ = 0;
    std::vector<std::string> leagueVisible_{"back", "bnnr", "titl", "sbar", "vit0", "lb00", "lb01",
                                            "ptop", "pn00", "pn01", "pn02", "pn03", "pn04",
                                            "pn05", "pn06", "pn07", "pn08", "pln0", "pln1",
                                            "pln2", "pln3", "pls0", "pls1"};
    // League configuration family (see the public API block above). Whitelists exclude the netplay
    // chat widgets (chat/cha2/cha3), the animseq placeholder rows (pNN + ptop/pbot) and the runtime
    // portrait/edit-box surfaces (plyr/tt20); kt/menu row objects (pnNN/prNN/ktNN) draw their shape
    // art in composite, captions + kit swatches paint as overlays from FCDB data.
    formats::ScreenLayout lg72_, lg74_, lg75_, lg76_, lg78_, lg79_, lg80_;
    bool haveLeagueCfg_ = false;   // at least one family layout loaded
    int leagueCfg_ = 0;            // active config screen id (72/74/75/76/79/80, 0 = none)
    std::string cfgName_;          // gate evidence label "FEFLOWxx"
    std::string cfgLang_;          // FCDBP<lang>.DBI code (lowercase)
    int cfgRow_ = 0;               // row cursor on the grid screens (roster index, pages via sbar)
    int stratVal_ = 2;             // FEFLOW75 s001 strategy: 5-step editor domain [+2att..+2def] [H]
    int aggrVal_ = 7;              // FEFLOW76 s001 aggression: FCDB skill nibble scale [0,15] [H]
    int biasVal_ = 4;              // FEFLOW79 s001 attack-bias: slider position domain [0,8] [H]
    int cfgTeamPool_ = 0;          // pool index of the team painted on the grids (lbNN arrows)
    std::vector<std::string> lg72Visible_, lg74Visible_, lg75Visible_, lg76Visible_, lg78Visible_, lg79Visible_, lg80Visible_;
    // World-cup family screens (analysis/pass12/wcfamily_notes.txt). Whitelists = scene-owned art only:
    // the tm/gm/qtm/tf/qf/ssN dynamic cells (objecttype 0/4, shapeID null) are engine content painted as
    // overlays; chat/masc runtime surfaces and the gpnm textid=514 caption slot stay hidden.
    formats::ScreenLayout wc18_, wc23_, wc24_, wc35_;
    bool haveWc_ = false;          // at least one family layout loaded
    int wcCfg_ = 0;                // active screen (18/23/24/35, 0 = none)
    int wcRow_ = 0;                // FEFLOW18 tm row cursor / 23-24 strk-stsc focus / 35 table row [H]
    int wcGroup_ = 0;              // FEFLOW35 group page (nglt/ngrt mod 8, decoded 1:1)
    int wcRandCount_ = 0;          // feflow18 'rand' press counter (engine cap 99 -> auto-advance)
    std::vector<std::string> wc18Visible_, wc23Visible_, wc24Visible_, wc35Visible_;
    // ---- Cheats screen (feflow95.lay; MENU_PARITY.md §8) ------------------------------------
    // Registry row [C] analysis/pass12/edges_table.txt:499 (feflow95 +0x10=0x469af8, +0x18=0x469a60
    // draw, +0x1c=0x469cb8; also analysis/pass11/menu5_scenemap_windowobj.txt:72). The row state lives in
    // the scene global DAT_0057d2a0, indexed per row: reads at 0x469a19 `MOV ESI,[EDX+0x57d2a0]`,
    // 0x469c54 `MOV EAX,[EBP+0x57d2a0]` (analysis/pass12/nav2_raw.txt:14379,14612) and in the event
    // handler at 0x469ce7 `MOV AL,[0x0057d2a0]` (nav1_raw.txt:24235). The address sits in DGROUP/.bss
    // (next to the exe font table DAT_0057d2c0), so it starts cleared; per-row runtime writes are not
    // statically enumerated, hence the port keeps its own bool array [H].
    formats::ScreenLayout cheats95_;
    bool haveCheats_ = false;          // layout present on this tree
    bool cheatsView_ = false;          // scene shown (F9 dev key / --fe-cheats gate)
    int cheatRow_ = 0;                 // row cursor over btn0..btn7 [H] port-side cursor
    std::array<bool, kCheats> cheatsOn_{}; // per-row toggle state (DAT_0057d2a0 analogue)
    std::vector<std::string> cheatsVisible_; // composite whitelist: scene-owned art only
    presentation::FeDrawStats drawCheats(render::SpriteBatch& ui) const; // composite + row overlays
    // One caption through FeText at widget geometry, centred inside the widget box (the alignment=3
    // semantics of the compositor's label path); colour = palette index from NORMALCOLOUR/HILITECOLOUR.
    // Captions bypass the FC-bank label() path: the scene installs exe strings on txt0..txt7/titl/'yes'.
    [[nodiscard]] int paintCaption(const formats::LayoutObject& o, std::string_view s, render::SpriteBatch& ui) const;
    int sel_ = 0;
    std::unique_ptr<presentation::FeText> text_; // FFN face + FC*.BIN strings, loaded once (fe_screen.hpp)
    bool teamScreen_ = false;              // true = team-select list, false = FEFLOW05 main menu
    std::vector<std::uint16_t> poolIds_;   // ZWCUP.RES pool order (40 entries)
    std::vector<std::string> poolNames_;   // FCDB STR0 fullName per entry ("" when the DBI is absent)
    // Flag art file per pool entry: "FNNN" = the team's record INDEX in FCDB.DBI (measured:
    // F030.FSH f000 = Argentina shield, F031.FSH = Australia; their DBI ids are 86/87).
    std::vector<std::string> poolFlagFile_;
    // Full FCDB record set for the league-config roster rows (FEFLOW73 pnNN, 74/79 prNN, 80 pnNN);
    // kept from loadTeams' parse. Names = FCDBP<lang>.DBI STR1 entries parallel to fcdb_->players.
    std::optional<formats::FcdbDatabase> fcdb_;
    std::vector<std::string> fcdbPlayerNames_;
    [[nodiscard]] const formats::FcdbTeam* cfgTeam() const; // active grid team (null = no FCDB data)
    void cycleLeagueScreen(int dir); // gofd/gobk chain 72->74->75->79->80 (keyloop_notes.txt)
    [[nodiscard]] int rosterSize() const; // squad rows on the active grid team
    // kt/menu row overlay: "#jersey name" caption + tricolour kit swatch from FCDB data.
    void paintRosterRow(const formats::LayoutObject& row, render::SpriteBatch& ui, int rosterIdx,
                        bool hilited, presentation::FeDrawStats& st) const;
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
    // ---- Hub (Feflow06.lay) state --------------------------------------------------------
    // Cursor domain = the 13 button objectnames in LAY order (object 6..19). Default cursor is
    // fe07 so the help pane paints its caption (help=1533) at startup -> texts>0 gate evidence.
    formats::ScreenLayout hubScreen_;
    bool haveHub_ = false;
    bool hubView_ = true;               // hub is the menu-level screen whenever present
    int hubSel_ = 4;                    // fe07 (LAY order index)
    std::vector<std::string> hubVisible_{"back", "flag", "titl", "masc", "help",
                                         "fe33", "fe90", "fe32", "fe11", "fe07", "fe04", "fe02",
                                         "quit", "fe15", "fe89", "fe41", "repl", "cred"};
    // chat*/ftxt widgets belong to netplay/chat scenes the port never shows (hidden).
    mutable std::uint32_t animTicks_ = 0; // FRAMES clock (one tick per presented frame)
    bool ptrDown_ = false;                // mouse button state for PUSHEDSHAPE* painting
    bool quitRequested_ = false;          // Enter/click on the hub "quit" button -> app exit
    // ---- Options (FEFLOW15.LAY) ----------------------------------------------------------
    // Four tab pages (tbb1..4 objecttype 32; lst1..4 objecttype 17 list rows). Row tuples are the
    // `rows=N+` continuation lines "label,mode,count, FC-indices..." (widget TEXTID = leading index);
    // captions via FeText::stringAt. The Music Volume row (FC label 1186) maps to settings audio.
    struct OptRow { int label = 0; int mode = 0; std::vector<int> values; };
    formats::ScreenLayout optsScreen_;
    bool haveOpts_ = false;
    std::string optsRaw_;               // member text (rows=N+ tuples live in the raw LAY)
    bool optsView_ = false;
    int optTab_ = 0, optRow_ = 0;       // active tab page + row cursor
    std::array<std::vector<OptRow>, 4> optRows_;
    std::array<std::vector<int>, 4> optVals_;   // current value index per row
    bool audioDirty_ = false;           // volume changed -> main.cpp saves settings.ini
    config::Settings* settings_ = nullptr;
    audio::Mixer* mixer_ = nullptr;
    // ---- Save/Load entry (Feflow89.LAY) ---------------------------------------------------
    // Six slot buttons b000..b005 (textID 4640 -> FC bank out of range, dynamic per engine); the
    // port paints "Slot n" + WCT1 file state rows on the button geometry. Selecting a row with an
    // existing wc98-slotN.wct file triggers the existing saveload verify path (app/saveload.cpp).
    formats::ScreenLayout slScreen_;
    bool haveSl_ = false;
    bool slView_ = false;
    int slSlot_ = 0;
    static constexpr int kSaveSlots = 6; // Feflow89.LAY buttons b000..b005
    // ---- Credits (FEFLOW19) ----------------------------------------------------------------
    formats::ScreenLayout credScreen_;
    bool haveCred_ = false;
    mutable bool credView_ = false;         // toggled by drawCurrent's auto-return (const method)
    mutable int credFrames_ = 0;             // auto-return after the presentation duration (drawCurrent is const)
    static constexpr int kCredDurationFrames = 540; // ~9 s at 60 fps before the roll returns to the hub
    // Composite helpers for each view (help pane caption included where the layout has one).
    presentation::FeDrawStats drawHub(render::SpriteBatch& ui) const;
    presentation::FeDrawStats drawOptions(render::SpriteBatch& ui) const;
    presentation::FeDrawStats drawSaveLoad(render::SpriteBatch& ui) const;
    presentation::FeDrawStats drawCredits(render::SpriteBatch& ui) const;
    void hubUiState(presentation::FeUiState& st) const; // hover/pressed/disabled + frame clock
    [[nodiscard]] const formats::LayoutObject* pointerHitHub() const; // hub button under the mouse pointer (null = none)
    void drawHelpPane(const formats::ScreenLayout& sc, render::SpriteBatch& ui,
                      presentation::FeDrawStats& st, std::string_view widgetName) const;
    int hubActivate(int index);          // Enter/click on button: returns FeEntry launch idx / -1
    static std::vector<OptRow> parseOptionRows(std::string_view layText, int tab);
};

} // namespace wc98::app
