// FE flow implementation — see fe_flow.hpp for the design notes.
#include "fe_flow.hpp"

#include "wc98/presentation/hud.hpp"
#include "wc98/tournament/tournament.hpp"
#include "wc98/file.hpp"
#include "wc98/renderer/renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <span>
#include <array>
#include <random>

namespace wc98::app {
namespace fs = std::filesystem;
using presentation::FeDrawStats;
using presentation::ScreenArt;

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
            }
        } else {
            poolNames_.assign(poolIds_.size(), std::string());
        }
    } catch (const std::exception&) {
        poolIds_.clear();
        poolNames_.clear();
    }
    teams_.setCount(int(poolIds_.size()));
}

FeDrawStats FeFlow::drawCurrent(render::SpriteBatch& ui) const {
    if (teamScreen_) return drawTeams(ui);
    FeDrawStats st;
    // Scene-ownership visibility: MNMSETUP's netplay widgets (mneb edit boxes, modem lists,
    // dial strings) belong to scenes the port never shows -> hidden. Only the FE-flow art
    // objects are painted: back collage + titl logos + flag anim (evidence in the header).
    if (haveScreen_) st = presentation::composite(screen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &menuVisible_);
    // Menu item labels: drawn by scene-handler code in the original, not by LAY widgets. The
    // item strings are embedded in FIFAWC.EXE right after the "main menu" label string at
    // 0x564f89: 'friendly' (0x564fb1) and 'cup competition' (0x564fcd); no shipped LAY carries
    // kt/menu objects for them. Labels sit to the right of the selection markers.
    static constexpr std::string_view kLabels[kFeEntryCount] = {"Friendly", "Cup Competition"};
    // Plain face eras10 (fontID 0, exe font table 0x57d2c0) for menu items; the bold-italic
    // HUD face is a caption font and reads muddy over the collage. Fallback: primary face.
    const formats::FfnFont* f = text_ ? text_->face(0) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(0) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    const bool hasText = text_ && text_->ready() && f && ftex;
    for (int i = 0; i < kFeEntryCount; ++i) {
        const float y = 380.f + 28.f * i;
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
// pages of the 40-team pool swap the slot contents. Flags = FE/PACK/GLOBAL F<fcdbid>.FSH, single
// shape tag "f000" (measured: every FNNN.FSH carries one entry named f000).
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
        const std::string sfx = std::format("{:02d}", i); // stfNN / ttnn row slots in the LAY
        const bool cur = idx == teams_.sel;
        char flagFile[8];
        std::snprintf(flagFile, sizeof flagFile, "F%03u", poolIds_[std::size_t(idx)] % 1000u);
        if (const formats::LayoutObject* stf = gridScreen_.object("stf" + sfx)) {
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", flagFile, "f000", -1);
            if (sh.tex) {
                ui.draw(sh.tex, {float(stf->x), float(stf->y), float(sh.w), float(sh.h)}, {}, 0xFFFFFFFFu);
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
    if (!poolIds_.empty()) {
        const std::uint16_t id = poolIds_[std::size_t(teams_.sel)];
        char flagFile[8];
        std::snprintf(flagFile, sizeof flagFile, "F%03u", id % 1000u);
        if (const formats::LayoutObject* tmfg = gridScreen_.object("tmfg")) {
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", flagFile, "f000", -1);
            if (sh.tex) ui.draw(sh.tex, {float(tmfg->x), float(tmfg->y), float(sh.w), float(sh.h)}, {}, 0xFFFFFFFFu);
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
    ui.draw(cursorTex_, {x, y, w, h}, {}, 0xFFFFFFFFu);
    ++st.cursors;
}

void FeFlow::move(int delta) {
    if (teamScreen_) teams_.move(delta);
    else {
        sel_ = (sel_ + delta) % kFeEntryCount;
        if (sel_ < 0) sel_ += kFeEntryCount;
    }
}

bool FeFlow::enter() {
    if (teamScreen_) { // confirm the highlighted team: Enter launches with pickedTeam()
        pick_ = poolIds_.empty() ? -1 : int(poolIds_[std::size_t(teams_.sel)]);
        return true;
    }
    if (sel_ == int(FeEntry::Tournament) && !poolIds_.empty()) { // Tournament entry: pick your country first
        teamScreen_ = true;
        teams_.setCount(int(poolIds_.size()));
        return false;
    }
    return true;
}

void FeFlow::back() {
    teamScreen_ = false;
    pick_ = -1; // no selection: default behaviour restored
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
        char flagFile[8];
        std::snprintf(flagFile, sizeof flagFile, "F%03u", id % 1000u); // GLOBAL FNNN.FSH "f000" (measured)
        const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", flagFile, "f000", -1);
        if (sh.tex) {
            ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {}, 0xFFFFFFFFu);
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
                ui.draw(sh.tex, {float(wcps->x), float(wcps->y), float(sh.w), float(sh.h)}, {}, 0xFFFFFFFFu);
                ++st.shapes;
                break;
            }
        }
    }
    return st;
}

} // namespace wc98::app
