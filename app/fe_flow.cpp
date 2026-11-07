// FE flow implementation — see fe_flow.hpp for the design notes.
#include "fe_flow.hpp"

#include "wc98/presentation/hud.hpp"
#include "wc98/tournament/tournament.hpp"
#include "wc98/file.hpp"
#include "wc98/renderer/renderer.hpp"

#include "saveload.hpp"                    // USER/SLOTnn.SAV load path (Phase 3)
#include "wc98/config/settings.hpp"        // settings.ini [audio] volume rows (Phase 2)
#include "wc98/audio/mixer.hpp"            // live master-gain updates from the options screen
#include "wc98/formats/slot.hpp"           // slotFileName()

#include <algorithm>
#include <cstdio>
#include <charconv>   // row-tuple parsing (parseOptionRows)
#include <format>
#include <span>
#include <array>
#include <random>

namespace wc98::app {
namespace fs = std::filesystem;
using presentation::FeDrawStats;
using presentation::ScreenArt;
// Hub cursor domain: the 13 button widgets of Feflow06.lay in LAY order (objects 6..19). Wedge tag
// compares live in the feflow06 scene handler body (fe11 @0x449e29, fe07 @0x449e30, fe32 @0x449ce5,
// fe33 @0x449d4e, fe90 @0x449c4d/0x449d92, repl @0x449c01, cred @0x449c11); keyboard payload codes
// {2..9,0x1E} (hub FUN_00445664, MENU_NAV_WC98.md §3) route to the same wedge tags.
constexpr std::array<const char*, 13> kHubButtons{"fe33", "fe90", "fe32", "fe11", "fe07", "fe04", "fe02",
                                                  "quit", "fe15", "fe89", "fe41", "repl", "cred"};
// Modes the port has no simulation for paint their DISABLEDSHAPE* and Enter is a documented no-op.
constexpr std::array<const char*, 6> kHubDisabled{"fe33", "fe90", "fe04", "fe02", "fe89", "repl"};

FeFlow::FeFlow(render::Renderer& r, fs::path tree, std::string lang) : tree_(std::move(tree)), art_(r, tree_, tree_ / "FE/PACK/LAYOUTS") {
    // Main menu screen (LAY_COMPOSITOR_NOTES.md measured table: FEFLOW05 -> 26 objects -> 11 shapes).
    std::string text;
    try {
        fs::path layDir;
        text = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW05.LAY", layDir);
        const auto doc = formats::LayoutDocument::parse(std::string_view(text));
        screen_ = formats::ScreenLayout::build("FEFLOW05", doc, nullptr, nullptr, "PC");
        haveScreen_ = true;
    } catch (const std::exception& e) {
        // Menu art absent: the shell still navigates; drawCurrent reports shapes == 0.
        lastError_ = e.what();
    }
    // Text resources for the whole session (fe_screen.hpp FeText): FFN face, FC*.BIN strings, FEMSG.
    text_ = std::make_unique<presentation::FeText>(r, tree_, std::move(lang));
    // Team-select screen (FEFLOW11.LAY = the original ZWCUP pick grid: map, flags, group cells;
    // LAY_COMPOSITOR_NOTES.md). Absent -> drawTeams keeps the flat usability list.
    try {
        fs::path gdir;
        const std::string gtext = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW11.LAY", gdir);
        gridScreen_ = formats::ScreenLayout::build("FEFLOW11", formats::LayoutDocument::parse(std::string_view(gtext)), nullptr, nullptr, "PC");
        haveGrid_ = true;
    } catch (const std::exception&) { // no grid art on this tree: usability list remains
    }
    // Friendly/practise match-setup screens (FE_MENU_SCREENS_WC98.md §5): FEFLOW09.LAY and
    // FEFLOW50.LAY, same tolerant reader as the grid. Absent -> Enter on friendly/practise keeps
    // launching directly (pre-slice behaviour).
    try {
        fs::path d;
        const std::string t9 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW09.LAY", d);
        setup09_ = formats::ScreenLayout::build("FEFLOW09", formats::LayoutDocument::parse(std::string_view(t9)), nullptr, nullptr, "PC");
        haveSetup_ = true;
    } catch (const std::exception&) { // no friendly-setup art on this tree
    }
    try {
        fs::path d;
        const std::string t50 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW50.LAY", d);
        setup50_ = formats::ScreenLayout::build("FEFLOW50", formats::LayoutDocument::parse(std::string_view(t50)), nullptr, nullptr, "PC");
        haveSetup_ = true;
    } catch (const std::exception&) { // no practise-setup art on this tree
    }
    // League Play select screen (FE_MENU_SCREENS_WC98.md §4): FEFLOW73.LAY = the league list.
    try {
        fs::path d;
        const std::string t73 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW73.LAY", d);
        league73_ = formats::ScreenLayout::build("FEFLOW73", formats::LayoutDocument::parse(std::string_view(t73)), nullptr, nullptr, "PC");
        haveLeague_ = true;
    } catch (const std::exception&) { // no league-select art on this tree: League Play launches directly
    }
    // Hub screen (Phase 1): Feflow06.lay — the Windows main-menu hub (mixed-case member; MENU_NAV_
    // WC98.md §3 global-key table + analysis/pass12/edges_table.txt). When present it replaces the
    // FEFLOW05/MNMSETUP fallback as the menu-level screen.
    try {
        fs::path d;
        const std::string t6 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@Feflow06.lay", d);
        hubScreen_ = formats::ScreenLayout::build("FEFLOW06", formats::LayoutDocument::parse(std::string_view(t6)), nullptr, nullptr, "PC");
        haveHub_ = true;
    } catch (const std::exception&) { // no hub layout on this tree: FEFLOW05 menu fallback stays
    }
    // Options screen (feflow15.lay; key handler 0x44f22c, tags 'quit'/'deft'/'back'/'ops2'; the
    // tabs tbb1..4 + lists lst1..4 grammar is measured in MENU_PARITY.md §4 rank 1). Absent -> Enter
    // on fe15 keeps painting the hub (pre-slice behaviour). Raw text kept for the rows=N+ tuples.
    try {
        fs::path d;
        const std::string t15 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@feflow15.lay", d);
        optsRaw_ = t15;
        optsScreen_ = formats::ScreenLayout::build("FEFLOW15", formats::LayoutDocument::parse(std::string_view(t15)), nullptr, nullptr, "PC");
        haveOpts_ = true;
    } catch (const std::exception&) { // no options layout on this tree
    }
    // Save/Load entry (Feflow89.lay): the slot list scene behind the hub's fe41 button. Six slot
    // buttons b000..b005 (x=171, y=196+32*i, w=262 h=25) over USER/SLOTnn.SAV files (saveload.hpp).
    try {
        fs::path d;
        const std::string t89 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@Feflow89.lay", d);
        slScreen_ = formats::ScreenLayout::build("FEFLOW89", formats::LayoutDocument::parse(std::string_view(t89)), nullptr, nullptr, "PC");
        haveSl_ = true;
    } catch (const std::exception&) { // no save-load layout on this tree: fe41 is a documented no-op
    }
    // Credits scene (FEFLOW19.LAY): the core-team roll behind the hub's cred button. The tournament
    // presentation code already composites FEFLOW35/25/26/27; 19 is its credits sibling (MENU_NAV
    // §5: feflow19 handlers at 0x4526.., events 1001/1002 = button activate/hover family).
    try {
        fs::path d;
        const std::string t19 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW19.LAY", d);
        credScreen_ = formats::ScreenLayout::build("FEFLOW19", formats::LayoutDocument::parse(std::string_view(t19)), nullptr, nullptr, "PC");
        haveCred_ = true;
    } catch (const std::exception&) { // no credits layout on this tree: cred is a documented no-op
    }
    // Options row tables (feflow15.lay lst1..4 `rows=N+` tuples; parsed from the raw member text,
    // the LayoutDocument drops continuation lines). Value indices start at 0 (first FC caption).
    if (haveOpts_)
        for (int t = 0; t < 4; ++t) {
            optRows_[std::size_t(t)] = parseOptionRows(optsRaw_, t);
            optVals_[std::size_t(t)].assign(optRows_[std::size_t(t)].size(), 0);
        }
    // Mouse cursor sprite (MAUS.PAK, exe string offset 1451894): absent art => no sprite drawn.
    if (presentation::loadFeCursor(tree_, cursor_) && !cursorTex_)
        cursorTex_ = r.createTexture(cursor_.bmp, render::Wrap::Clamp, "fe/maus-curs");
    loadTeams();
}

// Team-select list data: ZWCUP.RES pool ids (40) + FCDB/FCDB_ENG.DBI STR0 fullName names.
// Non-throwing: a tree without these assets leaves the pool empty and Enter keeps launching
// the tournament driver directly (pre-team-select behaviour).
void FeFlow::loadTeams() {
    try {
        const auto res = wc98::readFile(tree_ / "FE/TEAMHIST.ENG/ZWCUP.RES");
        tournament::ZwcupRes z;
        if (!tournament::parseZwcupRes(std::as_bytes(std::span(res)), z)) return;
        poolIds_ = z.ids;
        const auto dbPath = wc98::findCaseInsensitive(tree_, "COMMON/FCDB.DBI");
        const auto nmPath = wc98::findCaseInsensitive(tree_, "COMMON/FCDB_ENG.DBI");
        if (!dbPath.empty() && !nmPath.empty()) {
            const auto db = formats::FcdbDatabase::parse(wc98::readFile(dbPath));
            const auto names = formats::FcdbNames::parse(wc98::readFile(nmPath));
            for (const std::uint16_t id : poolIds_) {
                const auto it = std::ranges::find(db.teams, id, &formats::FcdbTeam::id);
                const auto idx = std::size_t(std::distance(db.teams.begin(), it));
                poolNames_.push_back(it != db.teams.end() && idx < names.teams.size() ? names.teams[idx].fullName
                                                                                      : std::string());
                // Flag art file: "FNNN" = record index, not the id (F030 = Argentina, F031 = Australia).
                char ff[8];
                std::snprintf(ff, sizeof ff, "F%03u", unsigned(idx));
                poolFlagFile_.push_back(it != db.teams.end() ? ff : std::string{});
            }
        } else {
            poolNames_.assign(poolIds_.size(), std::string());
            poolFlagFile_.assign(poolIds_.size(), std::string());
        }
    } catch (const std::exception&) {
        poolIds_.clear();
        poolNames_.clear();
        poolFlagFile_.clear();
    }
    teams_.setCount(int(poolIds_.size()));
}

// Pool ids are FCDB team ids; the art file name carries the record INDEX (F030 = Argentina, id 86).
std::string FeFlow::flagFileForTeam(std::uint32_t id) const {
    const auto it = std::ranges::find(poolIds_, std::uint16_t(id));
    if (it == poolIds_.end()) return {};
    const std::size_t i = std::size_t(std::distance(poolIds_.begin(), it));
    return i < poolFlagFile_.size() ? poolFlagFile_[i] : std::string();
}

// --fe-screen capture lane ("make all menus 1:1"): an arbitrary member of FE/PACK/LAYOUTS/
// LAYOUTS.BIG through the same tolerant reader as the menu/grid screens (LAY_COMPOSITOR_NOTES.md).
// The composite draws every shown() object — no scene-ownership whitelist: these are reference
// captures for visual review, not in-game scenes.
bool FeFlow::loadCaptureScreen(std::string_view name, std::string& err) {
    // Search order: FE/PACK/LAYOUTS/LAYOUTS.BIG member, then IGLAY.BIG (in-game popups), then the
    // loose tree copies (FE/INGAME/*.LAY). All go through the same tolerant reader.
    const std::string n = std::string(name) + ".LAY";
    static constexpr std::array<const char*, 2> archives = {"LAYOUTS.BIG", "IGLAY.BIG"};
    try {
        fs::path dir;
        std::string text;
        bool found = false;
        for (const char* arc : archives) {
            try {
                text = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS" / arc).string() + "@" + n, dir);
                found = true;
                break;
            } catch (const std::exception&) { /* member absent in this archive: try the next */ }
        }
        if (!found) {
            const fs::path loose = wc98::findCaseInsensitive(tree_, "FE/INGAME/" + n);
            if (loose.empty()) throw std::runtime_error(n + ": no such layout member/file");
            text = presentation::readLayout(loose.string(), dir);
        }
        captureScreen_ = formats::ScreenLayout::build(std::string(name), formats::LayoutDocument::parse(std::string_view(text)), nullptr, nullptr, "PC");
        captureName_ = name;
        useCapture_ = true;
        return true;
    } catch (const std::exception& e) {
        err = e.what(); // formats::FormatError / readLayout's "no such archive member"
        return false;
    }
}

FeDrawStats FeFlow::drawCaptureScreen(render::SpriteBatch& ui) const {
    FeDrawStats st = presentation::composite(captureScreen_, ui, const_cast<ScreenArt&>(art_), text_.get());
    drawCursor(ui, st); // MAUS.PAK arrow overlay like every other FE composite
    return st;
}

FeDrawStats FeFlow::drawCurrent(render::SpriteBatch& ui) const {
    if (useCapture_) return drawCaptureScreen(ui); // --fe-screen lane replaces the menu composite
    if (teamScreen_) return drawTeams(ui);
    if (setupScreen_) return drawSetup(ui); // FEFLOW09/50 friendly/practise config screens
    if (leagueView_) return drawLeague(ui);
    if (optsView_ && haveOpts_) return drawOptions(ui);   // FEFLOW15 config screen (Phase 2)
    if (slView_ && haveSl_) return drawSaveLoad(ui);      // Feflow89 slot list (Phase 3)
    if (credView_ && haveCred_) {                         // FEFLOW19 credits roll behind the hub cred wedge (Phase 4)
        if (++credFrames_ > kCredDurationFrames) credView_ = false; // auto-return after the presentation duration
        else return drawCredits(ui);
    }
    if (onHub()) return drawHub(ui); // Feflow06 hub replaces the MNMSETUP fallback composite
    FeDrawStats st;
    // Scene-ownership visibility: MNMSETUP's netplay widgets (mneb edit boxes, modem lists,
    // dial strings) belong to scenes the port never shows -> hidden. Only the FE-flow art
    // objects are painted: back collage + titl logos + flag anim (evidence in the header).
    if (haveScreen_) st = presentation::composite(screen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &menuVisible_);
    // Menu item labels: drawn by scene-handler code in the original, not by LAY widgets. The four
    // item strings are embedded in FIFAWC.EXE right after the "main menu" label string at 0x564f89:
    // 'friendly' (0x564fb1), 'league play' (0x564fbd), 'cup competition' (0x564fcd), 'practise'
    // (0x564fe1); no shipped LAY carries kt/menu objects for them (FE_MENU_SCREENS_WC98.md §4).
    // Labels sit to the right of the selection markers; marker geometry is port-fixed ([H]: the
    // original positions come from the runtime mainmenu window-object, not recoverable statically).
    static constexpr std::string_view kLabels[kFeEntryCount] = {"Friendly", "League Play", "Cup Competition", "Practise"};
    // Plain face eras10 (fontID 0, exe font table 0x57d2c0) for menu items; the bold-italic
    // HUD face is a caption font and reads muddy over the collage. Fallback: primary face.
    const formats::FfnFont* f = text_ ? text_->face(0) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(0) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    const bool hasText = text_ && text_->ready() && f && ftex;
    for (int i = 0; i < kFeEntryCount; ++i) {
        const float y = 356.f + 30.f * i; // four rows, bottom row fully on screen (480 px)
        if (i == sel_) ui.draw(0, {36.f, y, 18.f, 18.f}, {}, render::packRgba(90, 200, 255, 255));
        else { // 1-px outline
            const std::uint32_t w = render::packRgba(255, 255, 255, 220);
            ui.draw(0, {36.f, y, 18.f, 1}, {}, w), ui.draw(0, {36.f, y + 17.f, 18.f, 1}, {}, w);
            ui.draw(0, {36.f, y, 1.f, 18.f}, {}, w), ui.draw(0, {53.f, y, 1.f, 18.f}, {}, w);
        }
        if (!hasText) continue;
        const std::uint32_t rgba = i == sel_ ? render::packRgba(255, 224, 96, 255) : render::packRgba(235, 235, 235, 255);
        for (const auto& q : presentation::layoutText(*f, kLabels[std::size_t(i)], 90.f, y + 2.f, 16.f, rgba))
            ui.draw(ftex, q.dst, q.uv, q.rgba);
        ++st.texts;
    }
    drawCursor(ui, st);
    return st;
}

// Team-select frame. With FEFLOW11.LAY present (the authentic ZWCUP pick grid), composite the
// layout and overlay the dynamic rows; else keep the flat usability list.
FeDrawStats FeFlow::drawTeams(render::SpriteBatch& ui) const {
    if (haveGrid_) return drawTeamsGrid(ui);
    FeDrawStats st;
    ui.draw(0, {24.f, 24.f, 592.f, 432.f}, {}, render::packRgba(10, 14, 28, 225));
    const formats::FfnFont* f = text_ ? text_->font() : nullptr;
    if (!f || !text_->ready()) return st; // no font: the panel alone draws
    auto line = [&](std::string_view s, float x, float y, float px, std::uint32_t rgba) {
        for (const auto& q : presentation::layoutText(*f, s, x, y, px, rgba)) ui.draw(text_->texture(), q.dst, q.uv, q.rgba);
    };
    line("SELECT YOUR TEAM", 48.f, 40.f, 18.f, render::packRgba(255, 255, 255, 255));
    const int pg = teams_.page(), rows = TeamListNav::kRows;
    for (int i = 0; i < rows; ++i) {
        const int idx = pg * rows + i;
        if (idx >= int(poolIds_.size())) break;
        std::string label = poolNames_[std::size_t(idx)];
        if (label.empty()) label = "team " + std::to_string(poolIds_[std::size_t(idx)]); // FCDB name table absent
        const bool cur = idx == teams_.sel;
        line(label, 84.f, 96.f + 32.f * i, 16.f,
             render::packRgba(cur ? 255 : 235, cur ? 224 : 235, cur ? 96 : 235, 255));
        ++st.texts;
    }
    // Cursor marker: the same flat quad as the menu selection.
    ui.draw(0, {52.f, 96.f + 32.f * (teams_.sel % rows) + 4.f, 18.f, 18.f}, {}, render::packRgba(90, 200, 255, 255));
    line("PAGE " + std::to_string(pg + 1) + "/" + std::to_string(teams_.pageCount()), 48.f, 420.f, 14.f,
         render::packRgba(200, 200, 200, 255));
    drawCursor(ui, st); // MAUS.PAK arrow on the mouse pointer as well
    return st;
}

// FEFLOW11 frame: static screen (map background, big flag tmfg, scoreboard bar sbar, group cells,
// help text) through the compositor, then per-row flag + name drawn at the LAY's own row-slot
// positions (stfNN/ttnn objects). The layout ships exactly 8 row slots == TeamListNav::kRows;
// pages of the 40-team pool swap the slot contents. Flags = FE/GLOBAL FNNN.FSH where NNN is the
// team's FCDB.DBI record index (measured: id 86 -> idx 30 -> F030 = Argentina shield), single shape
// tag "f000" (every FNNN.FSH carries exactly one entry, named f000).
FeDrawStats FeFlow::drawTeamsGrid(render::SpriteBatch& ui) const {
    FeDrawStats st = presentation::composite(gridScreen_, ui, const_cast<ScreenArt&>(art_), text_.get());
    const formats::FfnFont* f = text_ ? text_->font() : nullptr;
    const bool hasText = f && text_->ready();
    auto line = [&](std::string_view s, float x, float y, float px, std::uint32_t rgba) {
        if (!hasText) return;
        for (const auto& q : presentation::layoutText(*f, s, x, y, px, rgba)) ui.draw(text_->texture(), q.dst, q.uv, q.rgba);
    };
    const int rows = TeamListNav::kRows;
    const int pg = teams_.page();
    for (int i = 0; i < rows; ++i) {
        const int idx = pg * rows + i;
        if (idx >= int(poolIds_.size())) break;
        const std::string sfx = std::format("{:02d}", i); // ttNN name slots (zero-padded in the LAY)
        const bool cur = idx == teams_.sel;
        const std::string flagFile = std::size_t(idx) < poolFlagFile_.size() ? poolFlagFile_[std::size_t(idx)]
                                                                           : std::string(); // FNNN = FCDB record index
        if (const formats::LayoutObject* stf = gridScreen_.object("stf" + std::to_string(i))) { // stf0..stf7
            if (flagFile.empty()) ++st.missing;
            else if (const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", flagFile, "f000", -1);
                     sh.tex) {
                // Flag bitmaps are full-size shields; the LAY slot is a small icon cell -> scale into it.
                ui.draw(sh.tex, {float(stf->x), float(stf->y), float(stf->width > 0 ? stf->width : 27),
                                  float(stf->height > 0 ? stf->height : 21)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
                ++st.shapes;
            } else
                ++st.missing;
        }
        std::string label = poolNames_[std::size_t(idx)];
        if (label.empty()) label = "team " + std::to_string(poolIds_[std::size_t(idx)]); // FCDB name absent
        const formats::LayoutObject* tt = gridScreen_.object("tt" + sfx);
        const int tx = tt ? tt->x : 294; // LAY name-column slot (measured fallback)
        line(label, float(tx), float(172 + 32 * i), 16.f,
             render::packRgba(cur ? 255 : 235, cur ? 224 : 235, cur ? 96 : 235, 255)); // current row yellow (measured menu highlight)
        ++st.texts;
    }
    // Current-team big flag over the tmfg slot (the LAY's static default is f002): same shape rule.
    if (!poolIds_.empty() && teams_.sel >= 0 && std::size_t(teams_.sel) < poolFlagFile_.size()) {
        const std::string& flagFile = poolFlagFile_[std::size_t(teams_.sel)]; // record-index name (F030 = Argentina)
        if (const formats::LayoutObject* tmfg = gridScreen_.object("tmfg"); tmfg && !flagFile.empty()) {
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", flagFile, "f000", -1);
            if (sh.tex) ui.draw(sh.tex, {float(tmfg->x), float(tmfg->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
        }
    }
    // Scrollbar thumb in the sbar track: flat quad, page-proportional.
    if (const formats::LayoutObject* sbar = gridScreen_.object("sbar")) {
        const int pages = teams_.pageCount();
        const float track = float(sbar->height > 0 ? sbar->height : 245);
        const float thumb = track / float(pages > 0 ? pages : 1);
        const float y = float(sbar->y) + (track - thumb) * float(teams_.page()) / float(pages > 0 ? pages : 1);
        ui.draw(0, {float(sbar->x), y, float(sbar->width > 0 ? sbar->width : 8), thumb}, {}, render::packRgba(210, 230, 255, 255));
    }
    drawCursor(ui, st);
    return st;
}

// MAUS.PAK cursor arrow (entry 0 = the 28x26 arrow): drawn last over each FE composite. The
// original engine loads .\fe\pack\maus.pak in code (exe string block offset 1451894) and blits
// it after screen composition; the decoded RGBA carries transparent pixels (measured: 250 of
// 728 alpha=0), blended by the UI pipeline's SRC_ALPHA state. Hotspot = FSH header centerX/Y.
void FeFlow::drawCursor(render::SpriteBatch& ui, presentation::FeDrawStats& st) const {
    if (!cursorTex_) return;
    const float w = float(cursor_.bmp.width), h = float(cursor_.bmp.height);
    const float x = std::clamp(float(ptrX_ + cursor_.hotX), 0.f, render::SpriteBatch::kWidth - w);
    const float y = std::clamp(float(ptrY_ + cursor_.hotY), 0.f, render::SpriteBatch::kHeight - h);
    ui.draw(cursorTex_, {x, y, w, h}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
    ++st.cursors;
}

// Friendly/practise config screens (FE_MENU_SCREENS_WC98.md §5): the original uses up/down to move
// the row cursor and left/right to cycle that row's option values; Enter starts the match, Esc
// returns. The port mirrors the navigation — row selection is drawn as a hilite outline over the
// alp{n} pane (the LAY's HILITECOLOUR analogue), the option index is kept per row for the match
// model to consume when the config plumbing lands.
void FeFlow::move(int delta) {
    if (optsView_ && haveOpts_) {                         // options rows lst{tab} (up/down, clamped)
        const int n = std::max<int>(1, int(optRows_[std::size_t(optTab_)].size()));
        optRow_ = std::clamp(optRow_ + delta, 0, n - 1);
        return;
    }
    if (slView_ && haveSl_) {                             // slot rows b000..b005
        slSlot_ = std::clamp(slSlot_ + delta, 0, kSaveSlots - 1);
        return;
    }
    if (credView_) return;                                // roll: no cursor domain
    if (onHub()) {                                        // hub buttons in LAY order (object 6..19)
        hubSel_ = (hubSel_ + delta) % int(kHubButtons.size());
        if (hubSel_ < 0) hubSel_ += int(kHubButtons.size());
        return;
    }
    if (teamScreen_) teams_.move(delta);
    else if (leagueView_) { moveLeague(delta); return; } // arrows = pn00..pn08 rows
    else if (setupScreen_) {
        setupRow_ = (setupRow_ + delta) % 4;
        if (setupRow_ < 0) setupRow_ += 4; // four config rows (ovr0..3 family)
    } else {
        sel_ = (sel_ + delta) % kFeEntryCount;
        if (sel_ < 0) sel_ += kFeEntryCount;
    }
}
void FeFlow::moveLeague(int dir) {
    if (!haveLeague_) return;
    leagueSel_ = (leagueSel_ + dir) % 9; // pn00..pn08 rows
    if (leagueSel_ < 0) leagueSel_ += 9;
}

bool FeFlow::enterLeague() {
    return haveLeague_; // row confirm: fixtures aren't simulated -> shell launches the default match
}

void FeFlow::adjustValue(int dir) {
    // Left/right cycles one option per row on the setup screen and on the options list rows
    // (the original's two-axis scheme: up/down = row, left/right = value).
    if (setupScreen_) {
        setupValue_[std::size_t(setupRow_)] = (setupValue_[std::size_t(setupRow_)] + dir + 8) % 8;
        return;
    }
    if (!optsView_ || !haveOpts_) return;
    auto& rows = optRows_[std::size_t(optTab_)];
    auto& vals = optVals_[std::size_t(optTab_)];
    if (optRow_ < 0 || std::size_t(optRow_) >= rows.size()) return;
    const OptRow& r = rows[std::size_t(optRow_)];
    if (!r.values.empty() && !vals.empty()) { // FC-caption value cycle (mode 2 list row)
        int& v = vals[std::size_t(optRow_)];
        v = (v + dir + int(r.values.size())) % int(r.values.size());
    } else if (r.label == 1186 && settings_) { // "Music Volume" slider -> port [audio] music volume, live gain
        settings_->audio.music = std::clamp(settings_->audio.music + dir * 5, 0, 100);
        audioDirty_ = true;
        if (mixer_) mixer_->setMasterGain(float(settings_->audio.music) / 100.f); // [H] port mapping: music row drives the FE stream gain
    }
}

// Enter dispatch (per-scene key handlers, event 2001): feflow15 init 0x44f22c tags 'quit'/'deft'/
// 'back'; feflow41/feflow89 family init 0x459420 ('quit' @0x4595cc = back-out path); hub feflow06
// handler per-wedge tag compares (fe11 @0x449e29, fe07 @0x449e30, fe32 @0x449ce5, repl @0x449c01,
// cred @0x449c11). Enter on hub = the wedge tag's scene edge (hubActivate below).
bool FeFlow::enter() {
    if (optsView_ && haveOpts_) return false; // row Enter = value select is mouse-only in the port; L/R cycles values
    if (slView_ && haveSl_) {                 // slot confirm (Phase 3): WCT1 load via app/saveload.cpp
        const auto file = defaultSlotDir() / formats::slotFileName(slSlot_);
        try {
            const TournamentState st = loadTournament(file);
            const auto f = summaryFields(st);
            std::printf("fe: slot %d loaded (mode=%u champion=%u)\n", slSlot_, f[0], f[3]);
        } catch (const std::exception&) { // no save on this slot yet: the original offers New/Load choices
            std::printf("fe: slot %d empty\n", slSlot_);
        }
        slView_ = false;                      // back to the hub after the load attempt ('quit' tag path)
        return false;
    }
    if (credView_) { credView_ = false; return false; } // Enter/Esc exits the roll (event 2001)
    if (onHub()) return hubActivate(hubSel_) >= 0;      // wedge -> scene edge; true only for launch targets
    if (leagueView_) return true; // league-list row confirm: shell launches the default match
    if (teamScreen_) { // confirm the highlighted team: Enter launches with pickedTeam()
        pick_ = poolIds_.empty() ? -1 : int(poolIds_[std::size_t(teams_.sel)]);
        return true;
    }
    if (setupScreen_) return true; // config accepted: shell starts the match from here
    if ((sel_ == int(FeEntry::Exhibition) || sel_ == int(FeEntry::Practise)) && haveSetup_) {
        // Friendly / practise entries open their match-setup screen first (feflow09/feflow50).
        setupScreen_ = true;
        setupPractise_ = sel_ == int(FeEntry::Practise);
        return false;
    }
    if (sel_ == int(FeEntry::LeaguePlay) && haveLeague_) { // League Play opens FEFLOW73 first
        leagueView_ = true;
        return false;
    }
    if (sel_ == int(FeEntry::Tournament) && !poolIds_.empty()) { // Tournament entry: pick your country first
        teamScreen_ = true;
        teams_.setCount(int(poolIds_.size()));
        return false;
    }
    return true;
}

void FeFlow::back() {
    // Esc = the scenes' 'back'/'quit' widget tags (feflow15 tag 'back' @0x44f29a, feflow41 family
    // 'quit' @0x4595cc): every view closes to its parent; the hub has no parent scene.
    if (optsView_) { optsView_ = false; return; }
    if (slView_)   { slView_ = false; return; }
    if (credView_) { credView_ = false; return; }
    if (onHub()) return; // hub Esc: no parent scene (the Windows hub exits via the 'quit' wedge)
    if (leagueView_) { leagueView_ = false; return; } // Esc from the league list: straight to menu
    if (setupScreen_) { setupScreen_ = false; return; } // Esc from the config screen: straight to menu
    teamScreen_ = false;
    pick_ = -1; // no selection: default behaviour restored
}

// League Play select (FEFLOW73.LAY): composite art whitelist + a hilite pane on the highlighted
// pn row, matching the original's kt/menu selection feedback.
FeDrawStats FeFlow::drawLeague(render::SpriteBatch& ui) const {
    FeDrawStats st = presentation::composite(league73_, ui, const_cast<ScreenArt&>(art_), text_.get(), &leagueVisible_);
    // Row hilite: filled pane over the highlighted pn slot (pn00..pn08 geometry x=35 y=134+19*i).
    const float y = 134.f + 19.f * float(leagueSel_);
    ui.draw(0, {32.f, y - 3.f, 230.f, 16.f}, {}, render::packRgba(255, 224, 96, 48));
    return st;
}

// FEFLOW09/50 composite: art whitelist + the dynamic layer the original paints from live state —
// the two team shields (ltfg/rtfg, same FNNN record-index flag shapes as the pick grid) and the team names in
// the tltx/trtx slots. Row option captions (ctx{n}/grv{n}) are engine-driven strings the port has
// no static source for: they stay blank (open item), the row hilite is drawn over alp{n}.
FeDrawStats FeFlow::drawSetup(render::SpriteBatch& ui) const {
    const formats::ScreenLayout* sc = setupPractise_ && haveSetup_ ? &setup50_ : haveSetup_ ? &setup09_ : nullptr;
    if (!sc) return {};
    FeDrawStats st = presentation::composite(*sc, ui, const_cast<ScreenArt&>(art_), text_.get(), &setupVisible_);
    const auto drawFlag = [&](const char* objName, std::size_t poolIdx) {
        if (poolIds_.empty() || poolIdx >= poolIds_.size() || poolIdx >= poolFlagFile_.size()) return;
        const formats::LayoutObject* o = sc->object(objName);
        if (!o || poolFlagFile_[poolIdx].empty()) return;
        const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", poolFlagFile_[poolIdx], "f000", -1);
        if (sh.tex) { ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
        else ++st.missing;
    };
    const std::size_t selIdx = poolIds_.empty() ? 0 : std::size_t(teams_.sel) % poolIds_.size();
    drawFlag("ltfg", selIdx);
    drawFlag("rtfg", poolIds_.empty() ? 0 : (selIdx + 1) % poolIds_.size()); // CPU side: next pool entry
    const formats::FfnFont* f = text_ ? text_->face(0) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(0) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    const auto drawName = [&](const char* objName, std::size_t poolIdx, float dx) {
        if (!text_ || !text_->ready() || !f || !ftex || poolIds_.empty() || poolIdx >= poolIds_.size()) return;
        const formats::LayoutObject* o = sc->object(objName);
        if (!o) return;
        std::string label = poolNames_[poolIdx];
        if (label.empty()) label = "team " + std::to_string(poolIds_[poolIdx]);
        for (const auto& q : presentation::layoutText(*f, label, float(o->x) + dx, float(o->y), 14.f, render::packRgba(235, 235, 235, 255)))
            ui.draw(ftex, q.dst, q.uv, q.rgba);
        ++st.texts;
    };
    drawName("tltx", selIdx, -30.f);   // name slots sit over the shields (measured x 166/424)
    drawName("trtx", poolIds_.empty() ? 0 : (selIdx + 1) % poolIds_.size(), -30.f);
    if (const formats::LayoutObject* alp = sc->object("alp" + std::to_string(setupRow_))) { // row hilite
        const float x = float(alp->x), y = float(alp->y), w = float(alp->width > 0 ? alp->width : 217), h = float(alp->height > 0 ? alp->height : 55);
        const std::uint32_t c = render::packRgba(255, 224, 96, 255);
        ui.draw(0, {x, y, w, 2.f}, {}, c), ui.draw(0, {x, y + h - 2.f, w, 2.f}, {}, c);
        ui.draw(0, {x, y, 2.f, h}, {}, c), ui.draw(0, {x + w - 2.f, y, 2.f, h}, {}, c);
    }
    drawCursor(ui, st);
    return st;
}

// ---- Between-round tournament presentation -------------------------------------------------
// Registry RE (pass 8, analysis/pass8/tourney_registry.txt): FUN_004479d0 registers the feflowNN
// scene cluster. Confirmed between-round screens: feflow35 = group standings table (gpnm + rows),
// feflow25 = quarterfinal board (tmNN participants, qtmN SF slots), feflow26 = semifinal board
// (+ wcup/wcps trophy slot), feflow27 = full bracket + chmp champion slot + 3rd-place match.
// Overlays draw the app's deterministic tournament model at the LAY's own slot positions,
// mirroring the drawTeamsGrid row-overlay pattern.
struct FeFlow::TourneyModel {
    std::array<std::uint16_t, 32> groups{};                     // group g = slots g*4..g*4+3
    std::array<std::vector<tournament::StandingsRow>, 8> tables; // sorted standings per group
    tournament::KnockoutBracket bracket{};                    // played via makeTree + seeded stream (below)
    std::array<std::uint32_t, 16> seeds{}, winners{}, losers{}; // knockout node results
    int userGroup = 0;                                          // group shown on the feflow35 board
};
FeFlow::~FeFlow() = default; // unique_ptr<TourneyModel> completes here

namespace {
constexpr std::uint32_t kPresScheduleSeed = 0x98D19A8u; // same fixed seeds as tournament_demo
constexpr std::uint32_t kPresResultSeed = 0xC0FFEE98u;  // so overlays are gate-deterministic
tournament::StandingsRow presSeedRow(std::uint16_t id, int rank) {
    tournament::StandingsRow r;
    r.teamId = id;
    r.seedRank = rank;
    return r;
}
} // namespace

int FeFlow::tourneyPresentCount() { return ensureTourney() ? kTourneyRounds : 0; }

// Builds the model from the ZWCUP pool (fixed seeds: no live match code, no RNG drift). Lazy so
// main-menu frames never touch it; returns false when assets/pool are absent (non-throwing).
bool FeFlow::ensureTourney() {
    if (haveTourney_) return true;
    if (tourney_) return false; // attempted once, tree lacks the layouts
    tourney_ = std::make_unique<TourneyModel>();
    static constexpr const char* kSpecs[kTourneyRounds] = {"FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW35.LAY",
                                                           "FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW25.LAY",
                                                           "FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW26.LAY",
                                           "FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW27.LAY"};
    if (poolIds_.size() < 32) { tourney_.reset(); return false; } // needs the full 32-team pool
    try {
        for (int i = 0; i < kTourneyRounds; ++i) {
            fs::path dir;
            const std::string text = presentation::readLayout((tree_ / kSpecs[i]).string(), dir);
            presScreens_[std::size_t(i)] = formats::ScreenLayout::build("feflow",
                formats::LayoutDocument::parse(text), nullptr, nullptr);
        }
    } catch (const std::exception&) { tourney_.reset(); return false; }
    auto& m = *tourney_;
    for (int i = 0; i < 32; ++i) m.groups[std::size_t(i)] = poolIds_[std::size_t(i)];
    const auto sched = tournament::generateSchedule(m.groups, kPresScheduleSeed);
    std::array<std::vector<tournament::MatchResult>, 8> gm{}; // played group fixtures per group
    std::array<tournament::Fixture, 16> koFix{};              // index == bracket node
    for (const auto& f : sched) {
        if (f.group >= 0)
            gm[std::size_t(f.group)].push_back({m.groups[f.home.a], m.groups[f.away.a], 0, 0});
        else
            koFix[std::size_t(f.bracketNode)] = f;
    }
    std::mt19937 rng(kPresResultSeed);
    auto play = [&rng] {
        std::uniform_int_distribution<int> goals(0, 3);
        return std::tuple{goals(rng), goals(rng), (rng() & 1u) != 0}; // a, b, penalty-home-wins
    };
    int cursor[8] = {0};
    for (const auto& f : sched)
        if (f.group >= 0) {
            const auto [a, b, pkHome] = play();
            (void)pkHome; // draws allowed in the group stage: goals only feed the table
            tournament::MatchResult& r = gm[std::size_t(f.group)][std::size_t(cursor[f.group]++)];
            r.goalsA = std::uint8_t(a);
            r.goalsB = std::uint8_t(b);
        }
    std::array<tournament::StandingsRow, 16> place{}; // [g*2 + (b-1)] = group placing b
    for (int g = 0; g < 8; ++g) {
        std::vector<tournament::StandingsRow> seeds4;
        for (int pos = 0; pos < 4; ++pos)
            seeds4.push_back(presSeedRow(m.groups[std::size_t(g * 4 + pos)], pos));
        m.tables[std::size_t(g)] = tournament::computeStandings(seeds4, gm[std::size_t(g)]);
        place[std::size_t(g * 2)] = m.tables[std::size_t(g)][0];
        place[std::size_t(g * 2 + 1)] = m.tables[std::size_t(g)][1]; // top-2 qualify (§4)
    }
    auto placeTeam = [&](const tournament::SlotRef& r) {
        return place[std::size_t(r.a * 2 + r.b - 1)].teamId;
    };
    for (int i = 0; i < 8; ++i) { // R16 fixtures feed bracket seeds 0..15
        const tournament::Fixture& f = koFix[std::size_t(i)];
        m.seeds[std::size_t(2 * i)] = placeTeam(f.home);
        m.seeds[std::size_t(2 * i + 1)] = placeTeam(f.away);
    }
    m.bracket = tournament::KnockoutBracket::makeTree(
        std::vector<std::uint32_t>(m.seeds.begin(), m.seeds.end()), true);
    for (int n = 0; n < 16; ++n) { // topological R16, QF, SF, third-place, final: play the stream
        const tournament::BracketNode& node = m.bracket.node(std::size_t(n));
        if (node.teamA == tournament::kNoTeam && node.teamB == tournament::kNoTeam) continue; // TP pre-SF guard
        auto resolve = [&](const tournament::SlotRef& r) {
            return n == int(m.bracket.thirdPlaceNode()) ? m.losers[std::size_t(r.a)]
                                                        : m.winners[std::size_t(r.a)];
        };
        const std::uint32_t h = koFix[n].home.kind == tournament::SlotRef::kGroupPlace
                                    ? placeTeam(koFix[n].home) : resolve(koFix[n].home);
        const std::uint32_t a = koFix[n].away.kind == tournament::SlotRef::kGroupPlace
                                    ? placeTeam(koFix[n].away) : resolve(koFix[n].away);
        const auto [ga, gb, pkHome] = play();
        const bool homeWin = ga > gb || (ga == gb && pkHome);
        const std::uint32_t w = homeWin ? h : a, l = homeWin ? a : h;
        if (!m.bracket.setWinner(std::size_t(n), w)) continue;
        if (node.round == tournament::KnockoutRound::kSemiFinal)
            m.bracket.setLoser(std::size_t(n), l); // SF losers -> third-place playoff
        m.winners[std::size_t(n)] = w;
        m.losers[std::size_t(n)] = l;
    }
    if (pick_ >= 0)
        for (int g = 0; g < 8; ++g)
            for (int k = 0; k < 4; ++k)
                if (m.groups[std::size_t(g * 4 + k)] == std::uint16_t(pick_)) m.userGroup = g;
    haveTourney_ = true;
    return true;
}

const char* FeFlow::tourneyPresentName(int round) const {
    static constexpr const char* kNames[kTourneyRounds] = {"FEFLOW35", "FEFLOW25", "FEFLOW26", "FEFLOW27"};
    return (round >= 0 && round < kTourneyRounds && haveTourney_) ? kNames[std::size_t(round)] : "";
}

// Composite + dynamic overlay for the boundary after `round` (0 group, 1 R16, 2 QF, 3 final).
presentation::FeDrawStats FeFlow::drawTourneyPresent(render::SpriteBatch& ui, int round) {
    if (!ensureTourney() || round < 0 || round >= kTourneyRounds) return {};
    presentation::FeDrawStats st =
        presentation::composite(presScreens_[std::size_t(round)], ui, art_, text_.get());
    const presentation::FeDrawStats ov =
        round == 0 ? drawStandings(ui, presScreens_[0]) : drawBracketBoard(ui, round);
    st.shapes += ov.shapes; st.texts += ov.texts; st.cursors += ov.cursors; st.missing += ov.missing;
    drawCursor(ui, st);
    return st;
}

std::string FeFlow::poolName(std::uint32_t id) const {
    const auto it = std::ranges::find(poolIds_, std::uint16_t(id));
    if (it == poolIds_.end()) return "team " + std::to_string(id);
    const std::string& n = poolNames_[std::size_t(std::distance(poolIds_.begin(), it))];
    return n.empty() ? "team " + std::to_string(id) : n;
}

// FEFLOW35: standings table for the user team's group. Row slots ssnN (name), ssp/ssw/ssd/ssl
// (P W D L), ssf/ssa (GF GA), sst (points); suffix 0..3 = row, measured grid analysis/pass8.
presentation::FeDrawStats FeFlow::drawStandings(render::SpriteBatch& ui, const formats::ScreenLayout& sc) const {
    presentation::FeDrawStats st;
    if (!text_ || !text_->ready() || !text_->font()) return st;
    auto line = [&](std::string_view s, float x, float y, std::uint32_t rgba) {
        for (const auto& q : presentation::layoutText(*text_->font(), s, x, y, 12.f, rgba)) {
            ui.draw(text_->texture(), q.dst, q.uv, q.rgba);
            ++st.texts;
        }
    };
    const auto& table = tourney_->tables[std::size_t(tourney_->userGroup)];
    if (const formats::LayoutObject* gpnm = sc.object("gpnm"))
        line(std::string("GROUP ") + char('A' + tourney_->userGroup), float(gpnm->x), float(gpnm->y),
             render::packRgba(255, 235, 96, 255));
    for (int pos = 0; std::size_t(pos) < table.size(); ++pos) {
        const auto& row = table[std::size_t(pos)];
        const std::string pfx = std::to_string(pos);
        if (const formats::LayoutObject* o = sc.object("ssn" + pfx))
            line(poolName(row.teamId), float(o->x), float(o->y), render::packRgba(235, 235, 235, 255));
        const std::pair<const char*, int> nums[] = {{"ssp", row.p}, {"ssw", row.w}, {"ssd", row.d},
                                                    {"ssl", row.l}, {"ssf", row.gf}, {"ssa", row.ga},
                                                    {"sst", row.pts}};
        for (const auto& [key, v] : nums)
            if (const formats::LayoutObject* no = sc.object(key + pfx))
                line(std::to_string(v), float(no->x), float(no->y), render::packRgba(255, 255, 255, 255));
    }
    return st;
}

// Knockout boards (feflow25/26/27): team names + flags at the LAY's own slots. Slot inventory
// measured from analysis/pass8 extracts: feflow25 tmNN=QF entrants (nodes 8..11 pairs), qtm0..3=
// SF slots, tfNN/qfNN = flag strips; feflow26 tm00/01=node12 pair, tm02/03=node13, wcup/wcps =
// trophy slot (WCUPF.PAK frame sprite); feflow27 tmNN=seeds, qtmN=R16 winners, stmN=QF winners,
// ftm0/1=finalists, chmp=champion (gold), 3tm0/1=third-place playoff entrants.
presentation::FeDrawStats FeFlow::drawBracketBoard(render::SpriteBatch& ui, int round) const {
    presentation::FeDrawStats st;
    if (!text_ || !text_->ready() || !text_->font()) return st;
    const auto& sc = presScreens_[std::size_t(round)];
    const auto& m = *tourney_;
    auto line = [&](const formats::LayoutObject* o, std::string_view s, std::uint32_t rgba) {
        for (const auto& q : presentation::layoutText(*text_->font(), s, float(o->x), float(o->y), 14.f, rgba)) {
            ui.draw(text_->texture(), q.dst, q.uv, q.rgba);
            ++st.texts;
        }
    };
    auto team = [&](std::string_view slot, std::uint32_t id, bool gold = false) {
        if (id == tournament::kNoTeam) return;
        if (const formats::LayoutObject* o = sc.object(slot))
            line(o, poolName(id), render::packRgba(gold ? 255 : 235, gold ? 215 : 235, gold ? 64 : 235, 255));
    };
    auto flag = [&](std::string_view slot, std::uint32_t id) {
        if (id == tournament::kNoTeam) return;
        const formats::LayoutObject* o = sc.object(slot);
        if (!o) return;
        const std::string flagFile = flagFileForTeam(id); // FNNN = FCDB record index (measured)
        if (flagFile.empty()) { ++st.missing; return; }
        const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", flagFile, "f000", -1);
        if (sh.tex) {
            ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
            ++st.shapes;
        } else
            ++st.missing;
    };
    auto w = [&](int node) { return m.winners[std::size_t(node)]; }; // played result of a bracket node
    auto pairSlot = [&](const char* pfx, int i) { return pfx + std::to_string(i); };
    if (round == 1) { // FEFLOW25: quarterfinal board - R16 done, QF entrants known
        for (int i = 0; i < 8; ++i) { // tmNN = QF participants (node 8 + i/2 slots)
            const auto& nd = m.bracket.node(std::size_t(8 + i / 2));
            const std::uint32_t id = i % 2 == 0 ? nd.teamA : nd.teamB;
            team(pairSlot("tm", i), id);
            flag(pairSlot("tf", i), id);
        }
        for (int k = 0; k < 4; ++k) { // SF slots: QF winners, unplayed at this boundary -> skipped
            team(pairSlot("qtm", k), w(8 + k));
            flag(pairSlot("qf", k), w(8 + k));
        }
    } else if (round == 2) { // FEFLOW26: semifinal board
        for (int i = 0; i < 4; ++i) {
            const auto& nd = m.bracket.node(std::size_t(12 + i / 2));
            team(pairSlot("tm", i), i % 2 == 0 ? nd.teamA : nd.teamB);
            flag(pairSlot("tf", i), i % 2 == 0 ? nd.teamA : nd.teamB);
        }
        for (int k = 0; k < 2; ++k) {
            team(pairSlot("qtm", k), w(12 + k)); // final slot: SF winner, unplayed -> skipped
            flag(pairSlot("qf", k), w(12 + k));
        }
    } else { // FEFLOW27: full bracket after the final
        for (int i = 0; i < 16; ++i) {
            team(pairSlot("tm", i), m.seeds[std::size_t(i)]);
            flag(pairSlot("tf", i), m.seeds[std::size_t(i)]);
        }
        for (int k = 0; k < 8; ++k) { team(pairSlot("qtm", k), w(k)); flag(pairSlot("qf", k), w(k)); }
        for (int s = 0; s < 4; ++s) { team(pairSlot("stm", s), w(8 + s)); flag(pairSlot("sf", s), w(8 + s)); }
        const auto& fin = m.bracket.node(14);
        team("ftm0", fin.teamA);
        team("ftm1", fin.teamB);
        flag("ff00", fin.teamA);
        flag("ff01", fin.teamB);
        const auto& tp = m.bracket.node(15);
        team("3tm0", tp.teamA);
        team("3tm1", tp.teamB);
        if (const formats::LayoutObject* o = sc.object("chmp")) { // champion: gold label at its slot
            const std::uint32_t champ = w(14);
            if (champ != tournament::kNoTeam) line(o, poolName(champ), render::packRgba(255, 215, 64, 255));
        }
    }
    // Trophy slot: WCUPF.PAK carries the pre-rendered trophy frames (SHPI sprites); draw the first
    // present frame at the wcps slot. The runtime PCMD/3D sequence stays a known polish item.
    if (const formats::LayoutObject* wcps = sc.object("wcps")) {
        for (int idx = 0; idx < 8; ++idx) {
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", "wcupf", "", idx);
            if (sh.tex) {
                ui.draw(sh.tex, {float(wcps->x), float(wcps->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
                ++st.shapes;
                break;
            }
        }
    }
    return st;
}
// ================================================================================================
// Hub screen (Phase 1) + options/save-load/credits views (Phases 2-4). Evidence: Feflow06.lay
// widget table (button objectnames objects 6..19, help= FC captions, helppaneid=help), the global-
// key hub table FUN_00445664 EDX payload -> feflow scenes (MENU_NAV_WC98.md §3), and the scene->
// scene edges in analysis/pass12/edges_table.txt. feflow15.lay key handler 0x44f22c: event 2001,
// widget tags 'quit'/'deft'/'back'/'ops2'; Feflow89.lay slot buttons b000..b005 (USER/SLOTnn.SAV).

std::string_view FeFlow::hubButtonName(int index) {
    return index >= 0 && index < int(kHubButtons.size()) ? std::string_view{kHubButtons[std::size_t(index)]} : std::string_view{};
}
int FeFlow::hubButtonIndex(std::string_view name) {
    const auto it = std::ranges::find_if(kHubButtons, [name](const char* c) { return formats::iequals(c, name); });
    return it == kHubButtons.end() ? -1 : int(std::distance(kHubButtons.begin(), it));
}

void FeFlow::resetCursor() {
    if (haveHub_) hubSel_ = hubButtonIndex("fe07");
    sel_ = 0;
}

void FeFlow::selectNav(int entry) {
    // Gate nav-axis mapping shared by hub and FEFLOW05 fallback: 0 fe07, 1 league list direct,
    // 2 fe11 (team grid), 3 fe32 (practise). Keeps fe_nav_* / fe_league gates valid on both menus.
    if (!onHub()) {
        sel_ = std::clamp(entry, 0, kFeEntryCount - 1);
        return;
    }
    switch (entry) {
        case 0: hubSel_ = hubButtonIndex("fe07"); break;
        case 1: if (haveLeague_) { leagueView_ = true; } break; // FEFLOW73 list opens directly
        case 2: hubSel_ = hubButtonIndex("fe11"); break;
        case 3: hubSel_ = hubButtonIndex("fe32"); break;
        default: break;
    }
}

// Enter/click on the hub button `index`: scene-graph edges (edges_table.txt). Returns the FeEntry
// launch index when the shell must start a match, -1 for in-place view changes and no-ops.
int FeFlow::hubActivate(int index) {
    const std::string_view n = hubButtonName(index);
    if (n == "fe07") {
        if (haveSetup_) { setupScreen_ = true; setupPractise_ = false; return -1; } // feflow09 config first
        return int(FeEntry::Exhibition);
    }
    if (n == "fe11") {
        if (!poolIds_.empty()) { teamScreen_ = true; teams_.setCount(int(poolIds_.size())); return -1; }
        return int(FeEntry::Tournament); // no pool data: tournament driver launches directly
    }
    if (n == "fe32") {
        if (haveSetup_) { setupScreen_ = true; setupPractise_ = true; return -1; } // feflow50 config first
        return int(FeEntry::Practise);
    }
    if (n == "quit") { quitRequested_ = true; return -1; } // clean app exit (main.cpp loop check)
    if (n == "fe15") { openOptions(); return -1; }
    if (n == "fe41") { openSaveLoad(); return -1; }
    if (n == "cred") { openCredits(); return -1; }
    // fe33 shootout, fe90 classic, fe04 modem, fe02 network: no port simulation of those modes.
    // fe89 team management, repl highlights: same (no shipped scene driver in the port).
    return -1;
}

int FeFlow::pointerActivate() {
    using presentation::widgetHit;
    if (optsView_ && haveOpts_) { // tab buttons tbb1..4 (POINTS polygons in the LAY)
        for (int t = 0; t < 4; ++t) {
            const formats::LayoutObject* o = optsScreen_.object("tbb" + std::to_string(t + 1));
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { optTab_ = t; optRow_ = 0; return -1; }
        }
        return -1;
    }
    if (slView_ && haveSl_) { // slot buttons b000..b005: click selects + confirms the row
        for (int s = 0; s < kSaveSlots; ++s) {
            const formats::LayoutObject* o = slScreen_.object("b00" + std::to_string(s));
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { slSlot_ = s; enter(); return -1; }
        }
        return -1;
    }
    if (!onHub()) return -1; // team/setup/league/menu rows are code-drawn: no LAY hit boxes here
    for (int i = 0; i < int(kHubButtons.size()); ++i) {
        const formats::LayoutObject* o = hubScreen_.object(kHubButtons[std::size_t(i)]);
        if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) {
            hubSel_ = i;
            return hubActivate(i);
        }
    }
    return -1;
}

bool FeFlow::openOptions()  { if (!haveOpts_) return false; optsView_ = true; optRow_ = 0; return true; }
bool FeFlow::openSaveLoad() { if (!haveSl_) return false; slView_ = true; slSlot_ = 0; return true; }
bool FeFlow::openCredits()  { if (!haveCred_) return false; credView_ = true; credFrames_ = 0; return true; }
void FeFlow::cycleTab(int dir) {
    if (!optsView_) return;
    optTab_ = (optTab_ + dir) % 4; // tabs tbb1..4 ('ops2' event family in the engine key handler)
    if (optTab_ < 0) optTab_ += 4;
    optRow_ = 0;
}

void FeFlow::hubUiState(presentation::FeUiState& st) const {
    // Mouse hover wins over the keyboard cursor; press paints PUSHEDSHAPE* on the hovered button.
    st.hover = std::string(hubButtonName(hubSel_));
    if (const formats::LayoutObject* o = pointerHitHub()) st.hover = o->name;
    st.mx = float(ptrX_); st.my = float(ptrY_);
    if (ptrDown_) st.pressed = st.hover;
    st.animMs = animTicks_ * 16u; // FRAMES=a,b,c,d ranges advance at 150 ms/frame (LAY_COMPOSITOR_NOTES)
    for (const char* d : kHubDisabled) st.disabled.emplace_back(d);
}
const formats::LayoutObject* FeFlow::pointerHitHub() const {
    using presentation::widgetHit; // POINTS= polygons (tbb wedges) / x-y-width-height box fallback
    for (const char* n : kHubButtons)
        if (const formats::LayoutObject* o = hubScreen_.object(n); o && widgetHit(*o, float(ptrX_), float(ptrY_))) return o;
    return nullptr;
}

FeDrawStats FeFlow::drawHub(render::SpriteBatch& ui) const {
    presentation::FeUiState uist;
    hubUiState(uist);
    ++animTicks_; // one frame-clock tick per presented frame (button FRAMES animations)
    FeDrawStats st = presentation::composite(hubScreen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &hubVisible_, &uist);
    drawHelpPane(hubScreen_, ui, st, uist.hover); // help= FC caption in the helppaneid pane (fontid 4)
    drawCursor(ui, st);
    return st;
}

// Help pane: the hovered button's `help=NNN,...` leading number indexes FeText::stringAt (FC*.BIN;
// measured captions fe07 help=1533 ... quit 4240). The "help" pane (objecttype 14) supplies geometry
// + fontid (4); text is centered within it (alignment=3 semantics of the engine list widgets).
void FeFlow::drawHelpPane(const formats::ScreenLayout& sc, render::SpriteBatch& ui,
                          FeDrawStats& st, std::string_view widgetName) const {
    if (!text_ || !text_->ready() || widgetName.empty()) return;
    const formats::LayoutObject* o = sc.object(widgetName);
    if (!o) return;
    int idx = 0;
    const std::string* h = o->prop("HELP");
    if (!h || std::sscanf(h->c_str(), "%d", &idx) != 1 || idx <= 0) return;
    const std::string_view s = text_->stringAt(std::size_t(idx));
    if (s.empty()) return;
    const formats::LayoutObject* pane = sc.object(o->prop("HELPPANEID") ? *o->prop("HELPPANEID") : "help");
    if (!pane) pane = o; // fall back to the button's own box
    int fid = 4;
    const formats::FfnFont* f = text_->face(fid);
    render::TextureId ftex = text_->faceTexture(fid);
    if (!f || !ftex) { f = text_->font(); ftex = text_->texture(); }
    if (!f || !ftex) return;
    auto quads = presentation::layoutText(*f, s, 0.f, float(pane->y) + (pane->height - 16) / 2, 14.f, render::packRgba(255, 232, 96, 255));
    if (quads.empty()) return;
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& q : quads) minX = std::min(minX, q.dst.x), maxX = std::max(maxX, q.dst.x + q.dst.w);
    const float off = float(pane->x) + (float(pane->width) - (maxX - minX)) / 2.f - minX; // centered
    for (auto& q : quads) { q.dst.x += off; ui.draw(ftex, q.dst, q.uv, q.rgba); }
    ++st.texts;
}

// feflow15.lay list rows: the `rows=N+` key of lst{tab+1} is followed by N continuation lines
// "label,mode,count,v0..vN-1" (FC-bank indices; mode 2 = value list, mode 1 = slider/numeric row).
std::vector<FeFlow::OptRow> FeFlow::parseOptionRows(std::string_view layText, int tab) {
    std::vector<OptRow> rows;
    const std::string want = "objectname=lst" + std::to_string(tab + 1);
    std::size_t pos = layText.find(want);
    if (pos == std::string_view::npos) return rows;
    // Continuation lines run until the next [object / ;--- section marker.
    const std::size_t end = layText.find("[object", pos + want.size());
    std::string_view block = layText.substr(pos, (end == std::string_view::npos ? layText.size() : end) - pos);
    for (std::string_view line; !(block = block.substr(std::min<std::size_t>(block.find('\n') + 1, block.size()))).empty();) {}
    // Row tuples are the lines starting with ',': split on commas, strip '+' continuation markers.
    std::size_t p = 0;
    while (p < block.size()) {
        std::size_t nl = block.find('\n', p);
        if (nl == std::string_view::npos) nl = block.size();
        std::string_view ln = block.substr(p, nl - p);
        p = nl + 1;
        const std::size_t c0 = ln.find_first_not_of(" \t");
        if (c0 == std::string_view::npos || ln[c0] != ',') continue;
        OptRow r;
        std::vector<int> nums;
        std::size_t q = c0 + 1;
        while (q < ln.size()) {
            const std::size_t comma = ln.find(',', q);
            std::string_view tok = ln.substr(q, (comma == std::string_view::npos ? ln.size() : comma) - q);
            // trim + strip the trailing '+' continuation marker
            while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) tok.remove_prefix(1);
            while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t' || tok.back() == '+')) tok.remove_suffix(1);
            if (!tok.empty()) { int v = 0; if (std::from_chars(tok.data(), tok.data() + tok.size(), v).ptr == tok.data() + tok.size()) nums.push_back(v); }
            if (comma == std::string_view::npos) break;
            q = comma + 1;
        }
        if (nums.size() >= 3) { r.label = nums[0]; r.mode = nums[1]; r.values.assign(nums.begin() + 3, nums.end()); rows.push_back(r); }
        else if (nums.size() == 2) { r.label = nums[0]; r.mode = nums[1]; rows.push_back(r); } // "label,mode" slider row
    }
    return rows;
}

FeDrawStats FeFlow::drawOptions(render::SpriteBatch& ui) const {
    static constexpr std::array<const char*, 4> kTabs{"tbb1", "tbb2", "tbb3", "tbb4"};
    presentation::FeUiState uist;
    uist.animMs = animTicks_ * 16u;
    ++animTicks_;
    static const std::vector<std::string> kOptsVisible{"back", "bnnr", "flag", "titl", "tab1", "tab2", "tab3", "tab4",
                                                       "tbb1", "tbb2", "tbb3", "tbb4", "help", "quit", "save", "deft", "fwrd", "ttxt"};
    FeDrawStats st = presentation::composite(optsScreen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &kOptsVisible, &uist);
    drawHelpPane(optsScreen_, ui, st, kTabs[std::size_t(optTab_)]); // tab captions via help= keys
    const formats::LayoutObject* lst = optsScreen_.object("lst" + std::to_string(optTab_ + 1));
    if (!lst) { drawCursor(ui, st); return st; }
    const int rowH = lst->intProp("ROWHEIGHT", 21);
    const int fid = lst->intProp("TEXTFONT", 4);
    const formats::FfnFont* f = text_ ? text_->face(fid) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(fid) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    const auto& rows = optRows_[std::size_t(optTab_)];
    const auto& vals = optVals_[std::size_t(optTab_)];
    for (int i = 0; i < int(rows.size()); ++i) {
        const float y = float(lst->y) + float(i) * rowH;
        if (i == optRow_) ui.draw(0, {float(lst->x) - 4.f, y - 2.f, float(lst->width), float(rowH)}, {}, render::packRgba(255, 224, 96, 48));
        if (!f || !ftex) continue;
        const OptRow& r = rows[std::size_t(i)];
        const std::string_view cap = text_->stringAt(std::size_t(r.label));
        if (cap.empty()) continue; // FC-bank row captions are the scene-code truth here; gaps stay blank
        for (const auto& q : presentation::layoutText(*f, cap, float(lst->x) + 8.f, y, 13.f, render::packRgba(235, 235, 235, 255)))
            ui.draw(ftex, q.dst, q.uv, q.rgba);
        ++st.texts;
        if (!r.values.empty()) { // current value caption at the right edge (engine list-value column)
            const int vi = std::clamp(vals[std::size_t(i)], 0, int(r.values.size()) - 1);
            const std::string_view val = text_->stringAt(std::size_t(r.values[std::size_t(vi)]));
            if (!val.empty()) {
                for (const auto& q : presentation::layoutText(*f, val, float(lst->x + lst->width) - 150.f, y, 13.f, render::packRgba(255, 232, 96, 255)))
                    ui.draw(ftex, q.dst, q.uv, q.rgba);
                ++st.texts;
            }
        } else if (r.label == 1186 && settings_) { // "Music Volume" slider row -> the port's [audio] music volume
            const std::string v = std::format("{}%", settings_->audio.music);
            for (const auto& q : presentation::layoutText(*f, v, float(lst->x + lst->width) - 150.f, y, 13.f, render::packRgba(255, 232, 96, 255)))
                ui.draw(ftex, q.dst, q.uv, q.rgba);
            ++st.texts;
        }
    }
    drawCursor(ui, st);
    return st;
}

FeDrawStats FeFlow::drawSaveLoad(render::SpriteBatch& ui) const {
    static const std::vector<std::string> kSlVisible{"back", "bnnr", "flag", "titl", "vit0", "vit1",
                                                      "lb00", "lb01", "lb02", "lb03",
                                                      "b000", "b001", "b002", "b003", "b004", "b005"};
    FeDrawStats st = presentation::composite(slScreen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &kSlVisible);
    const formats::FfnFont* f = text_ ? text_->face(4) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(4) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    const auto dir = defaultSlotDir(); // USER/SLOTnn.SAV domain (saveload.hpp, SAVESLOT_WC98.md)
    for (int s = 0; s < kSaveSlots; ++s) {
        const formats::LayoutObject* o = slScreen_.object("b00" + std::to_string(s));
        if (!o) continue;
        if (s == slSlot_) ui.draw(0, {float(o->x) - 8.f, float(o->y) - 2.f, float(o->width) + 16.f, float(o->height)}, {}, render::packRgba(255, 224, 96, 48));
        if (!f || !ftex) continue;
        const auto file = dir / formats::slotFileName(s);
        std::string label = "Slot ";
        label += char('0' + s);
        label += exists(file) ? "  (saved game)" : "  (empty)"; // WCT1 slot state from the port's own saves
        for (const auto& q : presentation::layoutText(*f, label, float(o->x) + 8.f, float(o->y) + 5.f, 13.f, render::packRgba(235, 235, 235, 255)))
            ui.draw(ftex, q.dst, q.uv, q.rgba);
        ++st.texts;
    }
    drawCursor(ui, st);
    return st;
}

FeDrawStats FeFlow::drawCredits(render::SpriteBatch& ui) const {
    static const std::vector<std::string> kCredVisible{"back", "flag", "titl", "help"};
    FeDrawStats st = presentation::composite(credScreen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &kCredVisible);
    // FC-bank credit role captions (measured indices in COMMON/FCENG.BIN: 508 "Core Team Artists:",
    // 509 "Core Team Development Director:", 510/511 programmers). The runtime roll names the people
    // from engine data the port has no static source for; the role captions are shipped strings.
    const formats::FfnFont* f = text_ ? text_->face(4) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(4) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    static constexpr std::array<int, 4> kRoles{508, 509, 510, 511};
    if (f && ftex) {
        for (std::size_t i = 0; i < kRoles.size(); ++i) {
            const std::string_view s = text_->stringAt(std::size_t(kRoles[i]));
            if (s.empty()) continue;
            for (const auto& q : presentation::layoutText(*f, s, 180.f, 240.f + 22.f * float(i), 13.f, render::packRgba(235, 235, 235, 255)))
                ui.draw(ftex, q.dst, q.uv, q.rgba);
            ++st.texts;
        }
    }
    drawCursor(ui, st);
    return st;
}

} // namespace wc98::app
