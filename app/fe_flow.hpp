#pragma once
// Front-end flow (M4/M9 slice): the windowed shell's screen state machine. Loads FE/PACK/
// LAYOUTS/LAYOUTS.BIG through the tolerant BIG reader, composites the main-menu screen
// (FEFLOW05) with presentation::composite, and drives menu selection + entry points.
// ponytail: minimal by design — a data-driven FE graph (LAY button objects -> command ids,
// FC*.BIN text) is an open item in LAY_COMPOSITOR_NOTES.md; promote this table when it lands.
#include "wc98/formats/his.hpp"
#include "wc98/formats/tri.hpp"
#include "wc98/formats/res.hpp"
#include "wc98/formats/layout.hpp"
#include "wc98/presentation/fe_screen.hpp"
#include "wc98/presentation/ig_optns.hpp"
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

// Records-table row-window grammar (sbar branch, FUN_004572xx family): pool-anchored running
// index that skips the own slot only when the window crosses it [C records_dec10:149-192 +
// dec7:42-59]. DAT_0060a070 = own slot (pool scan), DAT_0060a064 = last pool index
// (poolSize-1; 0 when own is absent), DAT_0060a06e = running fetch index (offset+1 when
// offset==own). Returns the pool index painted on row r, or -1 for the clear branch (blank
// row when DAT_0060a064 < bound + r). Gate: selftest step records-window.
int recordsRowIndex(int offset, int own, int lastIdx, int r);
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
               : popKind_           ? popName_.c_str()
               : setup07View_       ? "FEFLOW07"
               : lineup16View_      ? "FEFLOW16"
               : stad08View_        ? "FEFLOW08"
               : cfg87View_            ? "FEFLOW87"
               : wc13View_             ? "FEFLOW13" // World Cup controller board (pass-24wc13)
               : cfg32View_ == 32      ? "FEFLOW32"
               : cfg32View_ == 33      ? "FEFLOW33"
               : leagueCfg_         ? cfgName_.c_str()
               : mgmtView_         ? "FEFLOW43"
               : lsView_           ? (lsView_ == 41 ? "FEFLOW41" : "FEFLOW42")
               : wcCfg_             ? cfgName_.c_str()
               : cheatsView_        ? "FEFLOW95"
               : histScreen_        ? (histScreen_ == 90 ? "FEFLOW90" : "FEFLOW91")
               : igMode_            ? (igMode_ == 1 ? "IGAOPTNS" : igMode_ == 2 ? "IGGOPTNS" : "IGMOPTNS")
               : teamScreen_        ? "FEFLOW11"
               : leagueView_        ? "FEFLOW73"
               : setupScreen_       ? (setupPractise_ ? "FEFLOW50" : "FEFLOW09")
               : optsView_          ? "FEFLOW15"
               : slView_            ? "FEFLOW89"
               : recScreen_         ? (recScreen_ == 28 ? "FEFLOW28" : recScreen_ == 29 ? "FEFLOW29"
                                        : recScreen_ == 30 ? "FEFLOW30" : recScreen_ == 31 ? "FEFLOW31"
                                        : recScreen_ == 34 ? "FEFLOW34" : "FEFLOW88")
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
    // 'rand' plate visibility (pass-20uitick tick handlers FUN_004524f8/0x452884, bit-8 hidden flag
    // FUN_0046bfcc/FUN_0046bfac, presenter gates FUN_0046c014/FUN_00464724): 18 shows it only when
    // bd8<99 && bd4==0, 19 only when bd4<99; the staged animation (DAT_0057ce30) is synchronous in
    // the port, so the busy term never spans a frame. 1 = visible, 0 = hidden, -1 = no rand widget.
    [[nodiscard]] int wcRandVisible() const {
        return wcCfg_ == 18 ? (wcRand18_ < 99 && wcRand19_ == 0 ? 1 : 0)
                            : wcCfg_ == 19 ? (wcRand19_ < 99 ? 1 : 0) : -1;
    }
    // Zone order view for gates: feflow18's draw (bd8 layer) is the family's shared pool;
    // feflow19 adds its staged re-draw layer (bd4) only after a pointer rand press on 19.
    std::vector<int> wcZonesView() const { return wcZones(); }
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
    // Language-row arrow = event 1017 [C] FE15 dispatch branch `0x3f9` (analysis/pass22fe15d):
    // writer FUN_0046b074 writes the lang byte; on mismatch the exe reloads "common\%s" via
    // FUN_0047ab1c + FUN_0047ac14 and repaints. Port: rebuild the FC-bank text layer and the
    // localized FCDB name tables so every caption repaints in the new language.
    void setLanguage(std::string code);
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
    // ---- FEFLOW07 single-player match setup + FEFLOW08 stadium select (pass 13 §B) --------------
    // Scene edges (MENU_NAV_WC98.md §3/§5): hub wedge 'fe07' opens feflow07 (matchup screen: ltnm/rtnm
    // names, shdl/shdr shields, shtg/pssg/rnng/dfce/ovrl FC-bank captions 429..433 over the lbNN/rbNN
    // skill bars = two-axis grammar; UP/DOWN rows, LEFT/RIGHT team cycle via ltlb/ltrb/rtlb/rtrb); its
    // 'fe09' forward edge chains to FEFLOW09 (friendly config) and the friendly confirm opens feflow08
    // ('fe08' tag @0x44C3D3): the stadium map — mpNN ball markers on the France map (hilitedshape=hbal,
    // help=4621..4630 stadium captions), nslt/nsrt arrows + stme runtime name, sint/wint/hint fake-
    // interface buttons (HALF LENGTH / WEATHER / DAY-NIGHT). Esc = back per 'fe07'/'fe06' exit tags.
    bool openSetup07();  // hub fe07 wedge entry / --fe-setup07 gate; false = layout absent on this tree
    [[nodiscard]] bool onSetup07() const { return setup07View_; }
    presentation::FeDrawStats drawSetup07(render::SpriteBatch& ui) const; // composite + shields/names/bars
    bool openStad08();   // friendly-confirm 'fe08' chain / --fe-stadium08 gate; false = layout absent
    [[nodiscard]] bool onStad08() const { return stad08View_; }
    presentation::FeDrawStats drawStad08(render::SpriteBatch& ui) const; // composite + tile/name overlays
    // ---- Lineups screen FEFLOW16 (pass-21igscn; analysis/pass21igscn/NOTES.md §2) -------------
    // feflow16 = team lineups board: LAY grid lpNN/lsNN (left 11, number+name rows) and
    // rpNN/rsNN (right 11), tmlt/tmrt center-aligned team-name slots (fontID 4). Scene edge
    // [C] analysis/pass12/edges_table.txt:22: feflow08 helper FUN_0043ce4c pushes NAME:feflow16
    // (0x562020) at site 0x43cf27 - the fe16 wedge on the stadium screen opens it. Enter on
    // the board kicks off the friendly match (kickoff 'e'+slot edge family, pass-12 §4).
    bool openLineup16();  // fe16 wedge / --fe-lineups16 gate; false = layout absent
    [[nodiscard]] bool onLineup16() const { return lineup16View_; }
    presentation::FeDrawStats drawLineup16(render::SpriteBatch& ui) const; // composite + roster rows
    // ---- Controller/2-player selection family FEFLOW32/33 (pass-20cfg; MENU_NAV_WC98 rows) ------
    // feflow32 = single-player controller select: one team-pool cursor (tick FUN_00432170 mirrors
    // [0x607fd4] into [0x608064] [C]; arrow tokens ltlb/ltlr decrement, ltrb increment [C init name
    // compares 0x6c746c62/0x6c746c72/0x6c747262). feflow33 = 2-player: two pool cursors, tick
    // FUN_00457f8c present-only, event FUN_0044a770 token set ltlb/ltrb/rtlb/rtrb [C asm].
    bool openCfg32(int screen);  // 32 = one pool cursor; 33 = two; false = layout absent
    [[nodiscard]] bool onCfg32() const { return cfg32View_ != 0; }
    presentation::FeDrawStats drawCfg32(render::SpriteBatch& ui) const; // composite + names/shields/bars
    [[nodiscard]] int cfg32Idx() const { return cfg32Idx_; }
    [[nodiscard]] int cfg33Left() const { return cfg33Left_; }
    [[nodiscard]] int cfg33Right() const { return cfg33Right_; }
    [[nodiscard]] int cfg33Side() const { return cfg33Side_; }
    void setCfg33Side(int s) { cfg33Side_ = s != 0; }
    // Pool-cursor grammar: count = pool size (0x608074); pure helper for the selftest gate.
    static int cfgCursor(int prev, int delta, int count) {
        return ((prev + delta) % count + count) % count;
    }
    // ---- Shootout controller board FEFLOW87 (pass-23fe87; MENU_PARITY.md §4 rank 2) -----------
    // "penalty shootout controller select screen" (LAY header): four slot rows of ovr/hpl+hpr
    // panes + arw arrows + anm controller icons (CONTR.FSH sequence 1..8, ticksperframe=0 ->
    // the tick handler owns the frame; FUN_00467ab0 zeroes the DAT_00608748.. device slots
    // every tick so frame 0 = keyboard paints [C fe87_dec1]). Edges [C LAY comments]:
    // fe07 "goto feflow33", fe06 "quit friendly (goto match select)", fe08 "goto feflow83",
    // quit wedge = feflow37 confirm.
    bool openCfg87(); // false = layout absent from this tree
    [[nodiscard]] bool onCfg87() const { return cfg87View_; }
    presentation::FeDrawStats drawCfg87(render::SpriteBatch& ui) const;
    [[nodiscard]] int cfg87Row() const { return cfg87Row_; }
    void setCfg87Row(int r) { cfg87Row_ = r & 3; }
    [[nodiscard]] int cfg87Dev(int i) const { return cfg87Dev_[std::size_t(i & 3)]; } // device slot (contr frame 1 + slot)
    void setCfg87Dev(int i, int v) { cfg87Dev_[std::size_t(i & 3)] = v & 7; }
    [[nodiscard]] std::string cfg87Caption(std::string_view name) const; // FC-remap caption of a cfg87 widget (gate evidence)

    // ---- World Cup controller board FEFLOW13 (pass-24wc13; MENU_PARITY §4 rank-4) ----
    // LAY header "world cup controller select screen"; member widget list byte-identical to
    // FEFLOW87, plate art = feflow13.fsh. Init handler FUN_0044e440 [C analysis/pass24wc13
    // fe87_dec3 disassembly 0x44e440..0x44e574]: event branch 0x3ea pushes NAME:feflow06
    // (Esc/back = straight to the hub, NOT a stack pop as on FEFLOW87); tag tree 'fe06'/'quit'
    // send FUN_00459410 tuples (0x3e9,0x82e,0x10f1 / 0x3ea,0x82e,0x10f3 = World Cup exit
    // variant) then push NAME:feflow37 (confirm popup); 'fe08' forward: FUN_00446624()==2
    // guard (2-controller play stays), FUN_0045a24c() < 0 -> FUN_00448428 (no edge), else
    // FUN_00448418(0) + FUN_0045a198 + push NAME:feflow56 (launch transition, null LAY).
    // Init tail writes 0x66 + 'fe15' = options jump. Open event 0x68 zeroes the device-slot
    // group DAT_00608748..0x608784 [C fe87_dec1/dec2, same slot cluster as FEFLOW87].
    bool openWc13(); // false = layout absent from this tree
    [[nodiscard]] bool onWc13() const { return wc13View_; }
    presentation::FeDrawStats drawWc13(render::SpriteBatch& ui) const;
    [[nodiscard]] int wc13Row() const { return wc13Row_; }
    void setWc13Row(int r) { wc13Row_ = r & 3; }
    [[nodiscard]] int wc13Dev(int i) const { return wc13Dev_[std::size_t(i & 3)]; } // device slot (contr frame 1 + slot)
    void setWc13Dev(int i, int v) { wc13Dev_[std::size_t(i & 3)] = v & 7; }
    [[nodiscard]] std::string wc13Caption(std::string_view name) const; // FC-remap caption (gate evidence)
    // ---- FEFLOW43 management grid + feflow41 LOAD / feflow42 SAVE lists (MENU_PARITY.md rows 29/30) --
    // FEFLOW43.LAY (analysis/pass13 sweep dump): the full team-management grid — back collage, bt00..bt07
    // formation/kick-takers/strategy/positioning/aggression/custom/line-up/bias edit buttons + canc/save/
    // rest/resa function buttons (feflow43.fsh iNNn/iNNh/iNNp chain), tmnm team name + tmlt/tmrt pool
    // arrows, vNNN roster rows (x=170 y=150+20i) with position slots v(N+4)NN and aNNN icon columns
    // (selection/bio/fatigue/match-cards/total-cards animseq placeholders at x=348/388/420/460/500),
    // oNNN row overlays, pyNN shirt-number slots, b000/b001 scroll buttons + sbar pan. Scene registry:
    // not in the exe SCENEREG table (superseded by the feflow73-80 family [H]); the port opens it as a
    // standalone management board (--fe-mgmt-grid gate) over FCDB squad data.
    bool openMgmtGrid(int poolIdx = 0); // false = layout absent on this tree
    [[nodiscard]] bool onMgmtGrid() const { return mgmtView_; }
    presentation::FeDrawStats drawMgmtGrid(render::SpriteBatch& ui) const; // composite + roster/position/icon overlays
    // feflow41 LOAD game list (registry row 0x562484, init 0x459420): blur + feflow42.fsh#pict panel +
    // lstb list box (page=10 lineHeight=19 font=5) over the port's SLOT%.2d.SAV directory scan
    // (app/saveload.cpp defaultSlotDir), sbar pan thumb, 'exit' nvicons crs button. The "0000" caption
    // slot carries soup textid 6 (" and icons") -> excluded from composite per the f0f8109 rule; rows
    // paint real save-file state. Enter on a row runs the existing loadTournament verify path.
    bool openLoadGame();
    [[nodiscard]] bool onLoadGame() const { return lsView_ == 41; }
    // feflow42 SAVE game list (registry row 0x562490, init 0x45979c, keyloop 0x459b98): same panel with
    // the tick/cros confirm pair (nvicons.fsh tic1/crs1 chain at x=417 y=370). The port paints `tick`
    // when a write is confirmed (Enter on a row = saveTournament into that slot), `cros` as the idle
    // state. soup textid 38 caption excluded; rows list existing slots + append new ones.
    bool openSaveGame();
    [[nodiscard]] bool onSaveGame() const { return lsView_ == 42; }
    presentation::FeDrawStats drawLoadSave(render::SpriteBatch& ui) const; // composite + lstb rows + sbar thumb
    [[nodiscard]] int loadSaveCursor() const { return lsRow_; }
    // Deterministic gate fixture: when the slot directory is empty, write two scenario states to
    // SLOT00/SLOT01 (same fixed seeds as the tournament presentation lane) so the LOAD list paints.
    void ensureFixtureSlots(); // deterministic --fe-load-game gate fixture (writes SLOT00/SLOT01 if empty)
    std::vector<int> slotFiles() const; // sorted SLOT%.2d.SAV indices in defaultSlotDir

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
    // ---- World-cup history boards feflow90/91 (hub wedge fe33; analysis/pass13/CLASSICS_FE33.md) --
    // Hub key handler SITE 0x449dc5 opens scene feflow90 on the 'fe33' wedge tag. Init FUN_004688e8
    // sets the list length DAT_0057c970 = 9 (nine World Cup editions); keyloop FUN_00468728 tags:
    // 'ysla'/'yslc' year-cursor -1, 'ysrb' +1 (bounded), Enter 'fe91' chains to feflow91
    // (FUN_004857a8 stores the selected index -> DAT_0062fab8 consumed by feflow91), 'quit'/'fe06 exit.
    struct WcFinal { int year; std::uint16_t leftId, rightId; const char* leftName, *rightName; int scoreL, scoreR; };
    static constexpr int kWcYears = 9; // DAT_0057c970 = 9 rows [C] FUN_004688e8
    // Finalist dataset per analysis/pass13/WCHISTORY_SLICE_SPEC: FIFA World Cup finals records, first
    // nine editions. FCDB.DBI team ids where a record exists (classic-era entries 861..876 + modern
    // pool teams); id 0 = no FCDB record -> the literal name string paints (Czechoslovakia). Scores
    // per FIFA finals records; names prefer the FCDB STR0 fullName for the id, fallback = table string.
    static constexpr std::array<WcFinal, kWcYears> kWcFinals{
        WcFinal{1930, 861, 862, "Uruguay", "Argentina", 4, 2},
        WcFinal{1934, 47, 0, "Italy", "Czechoslovakia", 2, 1},
        WcFinal{1938, 47, 864, "Italy", "Hungary", 4, 3},
        WcFinal{1950, 861, 59, "Uruguay", "Brazil", 2, 1},
        WcFinal{1954, 72, 864, "Germany", "Hungary", 3, 2},
        WcFinal{1958, 59, 39, "Brazil", "Sweden", 5, 2},
        WcFinal{1962, 59, 0, "Brazil", "Czechoslovakia", 3, 1},
        WcFinal{1966, 69, 72, "England", "Germany", 4, 2},
        WcFinal{1970, 59, 47, "Brazil", "Italy", 4, 1}};
    bool openHistory(int yearIdx = 0); // hub fe33 Enter / --fe-history N; false = layout absent
    [[nodiscard]] int historyScreen() const { return histScreen_; } // 90/91 active board (0 = none)
    [[nodiscard]] int historyYearIndex() const { return histYear_; }
    void moveHistory(int dir); // 'ysla'/'yslc' -1, 'ysrb' +1 bounded year cursor [C] FUN_00468728
    presentation::FeDrawStats drawHistory(render::SpriteBatch& ui) const;
    // ---- Records/trivia/history bookmarks family (FEFLOW28..31/34/88, pass-20records) ----
    // Bookmarked pages under one LAY per page; data = FE/TEAMHIST.<lang>/ {histext .his/.tri +
    // ZWCUP/ZINTER .RES} (formats::His/Tri/ResFile). Column grammar [C] sbar handler
    // 0x457345-0x4573f6: triple = {won,drawn,lost}, played = sum, opponent name = poolName
    // (FUN_00456c70). Dev gate --fe-records N opens page N (28=team select, 29/30=results,
    // 31=general, 34=trivia, 88=greats); Esc from 28 closes, Esc from pages returns to 28.
    bool openRecords(int screenId);
    [[nodiscard]] bool onRecords() const { return recScreen_ != 0; }
    [[nodiscard]] int recordsScreen() const { return recScreen_; }
    [[nodiscard]] int recordsTeam() const { return recTeam_; }
    void recordsTeamMove(int delta);   // left/right team cursor (pool ids order)
    void recordsTriviaMove(int delta); // trivia section cursor (page 34 trpb/trnb)
    void recordsRowMove(int delta);    // 9-row results window scroll (pages 29/30 sbar)
    [[nodiscard]] int recordsRow() const { return recRow_; }
    presentation::FeDrawStats drawRecords(render::SpriteBatch& ui) const;
    // ---- Shared popup lane (FEFLOW10 ok/canc + FEFLOW37 yes/no + IG boxes; analysis/pass13/POPUP_LANE_SPEC.md) --
    // The popups share one grammar: blur blackout (FE/GLOBAL/BLUR.FSH) + `popp` panel (feflow86.fsh#back,
    // 368x230 @136,140) + two nvicons buttons (crs1/crs5 = cancel/no, tic1/tic5 = ok/yes) + txtNN captions.
    // Handler evidence: feflow37 draw FUN_004592f0 label-setter x3 (FUN_004592d4) + popp page flip; the
    // standard widget payload events carry names `ok`/`cancel`, `no`/`yes` (+ Esc = cancel branch, §25-29).
    // IGCONSEL 'okay' box (FUN_00442000 stub: dismiss-only) paints INGAME.FSH btnn/btnd chrome on the
    // widget geometry (button.fsh stem alias); null.fsh region boxes are legit placeholders (§A rule).
    bool openPopup(std::string_view name, bool quitContext = false); // --fe-popup NAME / hub 'quit' wedge; false = layout absent
    [[nodiscard]] bool onPopup() const { return popKind_ != 0; }     // popup shown over its caller scene
    void movePopup(int dir);      // arrows cycle the two-button slot pair [C] payload names ok/cancel, yes/no
    void activatePopup();         // Enter: confirm + cancel both close to the caller (payload semantics)
    presentation::FeDrawStats drawPopup(render::SpriteBatch& ui) const; // composite + captions + help pane + chrome


    // ---- In-game option popups igaoptns/iggoptns/igmoptns (analysis/pass13/IGOPTNS_ART.md) --------
    // Captions lane: the trio ships list rows as inline `rows=N, tuples` data (FC-bank caption ids);
    // the ok/canc/defa buttons reference igicon.fsh#chck art that is PROVEN ABSENT on this media
    // (zero hits; lazy-loader "fepack\%s.all" dead) -> the port paints those three as captions of
    // their own help= FC indices (60/62/1176), no invented placeholder art. Esc returns to hub.
    bool openIgOptns(int mode); // 1=a(igaoptns) 2=g(iggoptns) 3=m(igmoptns); false = layout absent
    [[nodiscard]] int igOptnsMode() const { return igMode_; }
    void moveIgOption(int dir);  // up/down list-row cursor (bounded by the parsed row count)
    void adjustIgValue(int dir); // left/right cycle the focused row's FC-caption value
    presentation::FeDrawStats drawIgOptns(render::SpriteBatch& ui) const;
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
    // Row captions resolved through the FC widget-id remap lane (DAT_005774a4, FUN_0043c9a0 ->
    // FUN_004c31d0) — the list painter + help pane grammar [C fc_painter.txt:1231-1257, pass21igmenu].
    [[nodiscard]] std::string optionCaption(int tab, int row) const;      // row label caption
    [[nodiscard]] std::string optionValueCaption(int tab, int row, int idx) const; // value caption
    [[nodiscard]] int optionValueIdx(int tab, int row) const; // active value index of a row
    [[nodiscard]] std::string optionTabCaption(int tab) const;            // tbb{tab+1} help= caption
    // ttxt header pane caption: TABSW FUN_004516c0 rewrites the ttxt widget TEXTID per tab from the
    // DGROUP constant table (s16 0x57cd24+(tab-1)*4 = 78/81/1299/82 = tbb1..4 help ids) through
    // FUN_00478834, so the header pane is the tab title caption - never LAY design id 429.
    [[nodiscard]] std::string optionsHeaderText() const;
    // FE15_INIT widget-tag grammar [C analysis/pass22fe15/dec2.txt]: 'save' (0x73617665) @0x44f2b4
    // = settings writer cluster FUN_0046b0fc/b5c8/b3e0/b83c/b074 + options.sav write FUN_0046b320 +
    // click sfx FUN_004678b8, stay on screen (FUN_004461b4 redraw); 'deft' (0x64656674) @0x44f288 =
    // FUN_00459410 -> FUN_004593d4 cursor-state reset; 'fwrd' (0x66777264) = same writers + scene pop.
    void commitOptions();          // 'save': persist settings.ini domain + apply live gains
    void defOptions();             // 'deft': row-cursor reset
    void forwardOptions() { commitOptions(); optsView_ = false; } // 'fwrd': commit + pop to caller
    // settings.ini [audio] master <-> audio::Mixer gain wiring (options volume row).
    void setAudioContext(config::Settings* settings, audio::Mixer* mixer) { settings_ = settings; mixer_ = mixer; }
    [[nodiscard]] bool audioDirty() const { return audioDirty_; }
    std::string poolName(std::uint32_t id) const; // FCDB fullName for a pool id (""-name fallback)
    presentation::FeDrawStats drawTeams(render::SpriteBatch& ui) const; // team-select frame (flat panel + FFN rows)
    render::Renderer& r_;            // keeps FeText rebuildable at runtime (setLanguage)
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
    formats::ScreenLayout wc18_, wc19_, wc20_, wc21_, wc23_, wc24_, wc35_, wc81_, wc82_;
    bool haveWc_ = false;          // at least one family layout loaded
    int wcCfg_ = 0;                // active screen (18/19/20/21/23/24/35/81/82, 0 = none)
    int wcRow_ = 0;                // FEFLOW18 tm row cursor / 23-24 strk-stsc focus / 35 table row [H]
    int wcGroup_ = 0;              // FEFLOW35 group page (nglt/ngrt mod 8, decoded 1:1)
    int wcRand18_ = 0;             // bd8: feflow18 'rand' staged counter (cap 99; DAT_00608bd8)
    int wcRand19_ = 0;             // bd4: feflow19 'rand' staged counter (cap 99; DAT_00608bd4)
    std::vector<int> wcStack_;     // parent chain (pass-20uitick: scene-switch edges open children)
    std::vector<std::string> wc18Visible_, wc19Visible_, wc20Visible_, wc21Visible_, wc23Visible_, wc24Visible_, wc35Visible_, wc81Visible_, wc82Visible_;
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
    // World-cup history boards (feflow90/91): layouts + scene-ownership whitelists + state. The
    // year cursor is the port analogue of DAT_0057d1cc bounded by DAT_0057c970 = 9 [C].
    formats::ScreenLayout hist90_, hist91_;
    bool haveHistory_ = false;       // at least one board layout loaded
    int histScreen_ = 0;           // 90/91 active board (0 = none)
    int histYear_ = 0;               // year cursor over kWcFinals (init clamp [C] FUN_004688e8)
    // Records family (pass-20records): per-page layouts + cursors; data lazy on first open.
    formats::ScreenLayout rec28_, rec29_, rec30_, rec31_, rec34_, rec88_;
    bool haveRecords_ = false;
    int recScreen_ = 0;   // active page 28/29/30/31/34/88 (0 = not on records)
    int recTeam_ = 0;     // team cursor over pool ids order
    int recTrivia_ = 0;   // trivia section cursor (page 34)
    int recRow_ = 0;      // 9-row window top index (pages 29/30)
    std::vector<std::string> rec28Visible_, rec29Visible_, rec30Visible_, rec31Visible_,
        rec34Visible_, rec88Visible_;
    std::unique_ptr<formats::His> recHis_;
    std::unique_ptr<formats::Tri> recTri_;
    std::unique_ptr<formats::ResFile> recZwcup_, recZinter_;
    bool recDataLoaded_ = false;
    std::vector<std::uint8_t> recHisBytes_, recTriBytes_, recZwcupBytes_, recZinterBytes_;
    void loadRecordsData();               // FE/TEAMHIST.<LANG> bytes + parsers (once)
    void paintRecordsTable(const formats::ScreenLayout& sc, const formats::ResFile& res,
                           render::SpriteBatch& ui, presentation::FeDrawStats& st) const;
    void paintRecordsText(const formats::ScreenLayout& sc, const char* widget, int column,
                          int section, render::SpriteBatch& ui, presentation::FeDrawStats& st) const;
    std::vector<std::string> hist90Visible_, hist91Visible_;
    // ---- FEFLOW07 single-player match setup + FEFLOW08 stadium select (pass 13 §B) --------------
    // Layouts loaded eagerly in the ctor; scene-ownership whitelists exclude the netplay chat widgets
    // (chat/cha2/cha3), the footix/mascot runtime surfaces and the null.fsh hit-region polygons
    // (ltll/ltlr/rtlr/rtrr + stad/sare/0000 cover boxes) per LAY_COMPOSITOR_NOTES.md.
    formats::ScreenLayout setup07_, stad08_, lineup16_;
    bool haveSetup07_ = false, haveStad08_ = false, haveLineup16_ = false; // layouts present on this tree
    bool setup07View_ = false;  // FEFLOW07 shown (hub fe07 wedge / --fe-setup07 gate)
    int s07Row_ = 0;            // two-axis row cursor: 0 = left team side, 1 = right team side
    int s07Left_ = 0, s07Right_ = 1; // pool indices of the matchup sides (ltlb/ltrb/rtlb/rtrb cycle)
    std::vector<std::string> setup07Visible_, stad08Visible_;
    std::vector<std::string> lineup16Visible_; // lpNN/lsNN/rpNN/rsNN + tmlt/tmrt/ftix/shdr/shdl/back art
    bool stad08View_ = false;   // FEFLOW08 shown ('fe08' friendly-confirm chain / --fe-stadium08 gate)
    bool lineup16View_ = false; // FEFLOW16 lineups board (fe16 wedge on stad08; pass-21igscn §2)
    int s08Sel_ = 0;            // mpNN tile cursor (mp00..mp09, mod 10; nslt/nsrt arrows cycle)
    int s08Half_ = 0, s08Weather_ = 0, s08Night_ = 0; // sint/wint/hint fake-interface cycle state [H]
    formats::ScreenLayout cfg32_, cfg33_;               // controller-select family layouts
    bool haveCfg32_ = false, haveCfg33_ = false;        // present on this tree
    int cfg32View_ = 0;                                 // 0/32/33 = shown screen
    bool cfg87View_ = false;                            // FEFLOW87 shootout board (pass-23fe87)
    bool haveCfg87_ = false;                            // layout present on this tree
    int cfg87Row_ = 0;                                  // slot-row cursor (0..3)
    std::array<int, 4> cfg87Dev_{};                     // device slot per controller row (0 = keyboard, contr frame 1 + slot)
    formats::ScreenLayout cfg87_;                       // shootout board layout
    bool wc13View_ = false;                             // FEFLOW13 World Cup controller board (pass-24wc13)
    bool haveWc13_ = false;                             // layout present on this tree
    formats::ScreenLayout wc13_;                        // World Cup controller board layout
    std::vector<std::string> wc13Visible_;              // scene-ownership visible set (composite whitelist)
    int wc13Row_ = 0;                                   // slot-row cursor (0..3)
    std::array<int, 4> wc13Dev_{};                      // device slot per controller row (0 = keyboard, contr frame 1 + slot)
    int cfg32Idx_ = 0;                                  // feflow32 pool cursor ([0x607fd4]/[0x608064])
    int cfg33Left_ = 0, cfg33Right_ = 1;                // feflow33 per-side pool cursors
    int cfg33Side_ = 0;                                 // 0 = left side focused (UP/DOWN)
    std::vector<std::string> cfg32Visible_, cfg33Visible_, cfg87Visible_;
    // FEFLOW43 management grid (public API block): layout + scene-ownership whitelist. The vNNN/aNNN/
    // oNNN/pyNN animseq runtime surfaces, mmod mode slot and chat widgets stay hidden (composite paints
    // the iNNn button chain + back collage; dynamic content is painted as overlays from FCDB data).
    formats::ScreenLayout mgmt43_;
    bool haveMgmt43_ = false;
    bool mgmtView_ = false;      // FEFLOW43 shown (--fe-mgmt-grid gate)
    int mgmtRow_ = 0;            // roster row cursor (UP/DOWN; four-row window pages via sbar)
    int mgmtTeam_ = 0;           // pool index of the grid team (tmlt/tmrt arrows cycle, L/R axis [H])
    std::vector<std::string> mgmt43Visible_;
    // feflow41/42 save-list lane: lsView_ 41 = LOAD list, 42 = SAVE confirm; lsConfirm_ (42) drives the
    // tick/cros state icons. The "0000" soup caption slots are excluded from both whitelists.
    formats::ScreenLayout load41_, save42_;
    bool haveLoad41_ = false, haveSave42_ = false;
    int lsView_ = 0;             // active list scene (41/42, 0 = none)
    int lsRow_ = 0;              // lstb row cursor over the SLOT-file scan
    int lsConfirm_ = 0;          // feflow42: 0 = idle ('cros' icon), 1 = confirmed write ('tick')
    std::vector<std::string> load41Visible_, save42Visible_;
    [[nodiscard]] const formats::FcdbPlayer* gridPlayer(int rosterIdx) const; // FCDB record for a grid row
    void lstbPage(int dir); // feflow41/42: pan lsRow_ by one list page (sbar/b000 b001 semantics)
    void loadSelectedSlot(); // feflow41 Enter: loadTournament + summary print on the focused lstb row
    void saveSelectedSlot(); // feflow42 Enter: write the scenario state to the focused slot, tick confirm
    [[nodiscard]] int gridRosterSize() const; // populated squad slots of the active grid team
    [[nodiscard]] std::array<int, 5> teamBars(const formats::FcdbTeam* t) const; // FCDB skill rows 0..10
    [[nodiscard]] const formats::FcdbTeam* poolTeamFor(int poolIdx) const;        // FCDB record for a pool index
    // Shared popup lane (public API block above): FEFLOW10 ok_1/canc + FEFLOW37 yes/no loaded eagerly,
    // IGCONSEL/MESG/Feflow86 lazy-loaded members for the capture-style gates. The two-button cursor
    // domain is [canc|no, ok_1|yes]; popSel_ 0 = cancel slot (engine default focus [H]), 1 = confirm.
    formats::ScreenLayout pop10_, pop37_;
    bool havePop10_ = false, havePop37_ = false;
    formats::ScreenLayout popIg_;      // igconsel/mesg/Feflow86/CONTROL1 lazy member (openPopup)
    int popKind_ = 0;                  // 10/37 FE popup, -42 igconsel, -43 mesg, -86 Feflow86 panel demo, -44 CONTROL1 controller page (§H), 0 none
    std::string popName_;              // gate evidence label ("FEFLOW10"/"FEFLOW37"/"igconsel"/...)
    int popSel_ = 0;                   // focused button slot (two-axis cursor)
    bool popQuitCtx_ = false;          // hub 'quit' wedge context: confirm -> quitRequested_ (exit confirm)
    bool popWcExit_ = false;           // FEFLOW13 'fe06' context (pass-24wc13): confirm -> close WC family to hub
    std::vector<std::string> popVisible_; // composite whitelist for the active popup
    [[nodiscard]] const char* popupButtonName(int slot) const; // focused-slot widget name for the active popup
    [[nodiscard]] std::string wcTeamName(std::uint16_t id, const char* fallback) const; // FCDB fullName -> literal
    [[nodiscard]] std::string wcFlagFile(std::uint16_t id) const;                       // FNNN (record index), "" = no record
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
    std::vector<std::string> fcdbTeamFullNames_; // STR0 fullName parallel to fcdb_->teams (history boards)
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
    using OptRow = presentation::IgOptRow; // shared grammar (presentation/ig_optns.hpp)
    formats::ScreenLayout optsScreen_;
    bool haveOpts_ = false;
    std::string optsRaw_;               // member text (rows=N+ tuples live in the raw LAY)
    bool optsView_ = false;
    int optTab_ = 0, optRow_ = 0;       // active tab page + row cursor
    std::array<std::vector<OptRow>, 4> optRows_;
    std::array<std::vector<int>, 4> optVals_;   // current value index per row
    // igoptns popups: rows live inline in the LAY as `rows=N, tuples` (prop ROWS); the LayoutDocument
    // keeps them on one line, unlike feflow15's multi-line continuation rows.
    formats::ScreenLayout igA_, igG_, igM_;
    int haveIg_ = 0;                 // bit per mode present on this tree: 1<<a | 1<<g | 1<<m
    int igMode_ = 0;                 // active popup (1/2/3, 0 = none)
    int igRow_ = 0;                  // list row cursor
    std::vector<OptRow> igRows_;     // rows of the active popup (label/mode/values FC-bank ids)
    std::vector<int> igVals_;        // current value index per row
    std::vector<std::string> igVisible_; // composite whitelist: scene-owned art only
    static std::vector<OptRow> parseIgRows(std::string_view rowsProp);
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
