// FE flow implementation — see fe_flow.hpp for the design notes.
#include "fe_flow.hpp"

#include "wc98/presentation/hud.hpp"
#include "wc98/tournament/tournament.hpp"
#include "wc98/file.hpp"
#include "wc98/renderer/renderer.hpp"

#include <algorithm>
#include <span>
#include <string>

namespace wc98::app {
namespace fs = std::filesystem;
using presentation::FeDrawStats;
using presentation::ScreenArt;

FeFlow::FeFlow(render::Renderer& r, fs::path tree) : tree_(std::move(tree)), art_(r, tree_, tree_ / "FE/PACK/LAYOUTS") {
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
    text_ = std::make_unique<presentation::FeText>(r, tree_);
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
    if (haveScreen_) st = presentation::composite(screen_, ui, const_cast<ScreenArt&>(art_), text_.get());
    // Selection marker: two flat panels per entry, filled when selected. The LAY objects themselves
    // draw their FC*.BIN labels through FeText above.
    for (int i = 0; i < kFeEntryCount; ++i) {
        const float y = 380.f + 28.f * i;
        if (i == sel_) ui.draw(0, {36.f, y, 18.f, 18.f}, {}, render::packRgba(90, 200, 255, 255));
        else { // 1-px outline
            const std::uint32_t w = render::packRgba(255, 255, 255, 220);
            ui.draw(0, {36.f, y, 18.f, 1}, {}, w), ui.draw(0, {36.f, y + 17.f, 18.f, 1}, {}, w);
            ui.draw(0, {36.f, y, 1.f, 18.f}, {}, w), ui.draw(0, {53.f, y, 1.f, 18.f}, {}, w);
        }
    }
    return st;
}

// Team-select frame: flat panel + FFN text rows (hud layoutText through the FeText face).
// ponytail: no FEFLOW map-grid art here — usability list, fidelity slice later (see hpp).
FeDrawStats FeFlow::drawTeams(render::SpriteBatch& ui) const {
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
    return st;
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

} // namespace wc98::app
