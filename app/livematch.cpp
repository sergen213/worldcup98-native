// wc98-native live match (M10): a real-time playable exhibition match. The simulation is ticked
// from the wall clock in fixed 1/50 s steps (the engine frame clock cadence: camera/commentary use
// DAT_0065F1E0 = 2 sim frames per 1/50 s tick); each render frame builds a MatchSnapshot ->
// presentation::MatchScene with the tele camera following play, plus the HUD strip overlay.
// Commentary (app::Commentary) is pumped every 2 render frames with its trigger seam fed from the
// sim's World::events stream; Esc and window close quit the match.
//
// Input mapping — the ported wc98::input layer assembles the pad word the exe's device readers
// produce, and controller 0 receives it through SimRunner::setController (the same command path
// the M6 demo drives). Bindings are config/src/settings.cpp defaults (settings.ini override):
//   keyboard: move = arrows | pass S | shoot D | sprint W | lob A | evade Q | through-ball E |
//             skill1 Ctrl | skill2 Alt | IGM Space | tactics Z/X/C/V (press) | pause Esc*
//   gamepad:  move = d-pad/left stick | pass A | shoot B | sprint Y | lob X | evade LB |
//             through-ball RB | skill1 LT | skill2 RT | IGM Back | pause Start
//   (*Esc also quits the live match at the app layer, before feeding the sim's pause bit.)
// Controlled-player switching is inside the ported controller code (Player_transferControl sites
// in simulation/src/input.cpp): the sim reassigns ctrl on possession/defensive-context changes.
#include "replay.hpp" // native replay record -> user/rplyNNNN.dat (docs/asset-formats/RPLY.md)
#include "livematch.hpp"
#include "commentary.hpp"
#include "game_audio.hpp"
#include "wc98/audio/scene_levels.hpp" // speech/commentary lane re-level from the percent table

#include "wc98/config/settings.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/fcdb.hpp"
#include "wc98/formats/png.hpp" // gate PNG evidence (encodePng)
#include "wc98/input/input.hpp"
#include "wc98/platform/window.hpp"
#include "wc98/presentation/camera.hpp"
#include "wc98/presentation/hud.hpp"
#include "wc98/presentation/match_scene.hpp"
#include "wc98/presentation/fe_screen.hpp" // IGMENU/IGQUESHN pause composite (ScreenArt, FeText, composite)
#include "wc98/renderer/vulkan.hpp"
#include "wc98/sim/ball.hpp"
#include "wc98/sim/data.hpp"
#include "wc98/sim/player.hpp"
#include "wc98/sim/rules.hpp"

#include <iomanip> // setw on the replay slot filename
#include "wc98/sim/sim.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <atomic>
#include <optional>
#include <cstdint>
#include <cctype>
#include <format>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <fstream>

namespace fs = std::filesystem;
using namespace wc98;
using namespace wc98::presentation;
namespace wc98 {
namespace app {

namespace {

// Fixed-point (16.16) -> float, as stored in sim Vec3 fields (fixed.hpp).
float fx(std::int32_t v) { return float(v) / 65536.0f; }

// DAT_0065F1C8 seconds-in-period (plain int32 seconds; HUD clock format site): gate evidence view.
double elapsedSecFloat(const sim::World& w) { return double(w.match.totalSec); } // DAT_0065F1C8 cumulative (period resets make elapsedSec unusable across the half boundary)

} // namespace

// Gate evidence capture (match_demo savePng pattern): offscreen copy of a presented frame.
bool savePng(render::Renderer& r, const render::Frame& frame, const fs::path& path) {
    const render::Image img = r.renderOffscreen(frame, 640, 480);
    if (img.width != 640 || img.height != 480) return false;
    const std::vector<std::uint8_t> png = formats::encodePng(img.width, img.height, img.rgba);
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    return bool(out);
}

bool MatchBoot::init(const fs::path& root, std::string_view lang) {
    data = std::make_unique<presentation::GameData>(root);
    // Preflight the assets the slice needs so a missing CD copy skips (ctest SKIP_RETURN_CODE 2).
    const char* need[] = {"COMMON/FCDB.DBI", "COMMON/AISEQS.TBI", "COMMON/ANIM.MO", "COMMON/GEN4TBLS.VIV",
                          "INGAME/MISC/MISC8.FSH", "INGAME/MISC/MISC16.FSH", "INGAME/MISC/TURFS0P0.PAK",
                          "INGAME/PLAYER/MODELS/HIG_30.PC", "INGAME/PLAYER/MODELS/SHI_10.PC",
                          "INGAME/PLAYER/MODELS/MED_70.PC", "INGAME/PLAYER/MODELS/LOW_90.PC",
                          "INGAME/PLAYER/MODELS/SHADOW.PC", "INGAME/PLAYER/MODELS/MODELS2.PAK"};
    for (const char* f : need)
        if (!data->exists(f)) { std::cerr << "skip: missing " << f << '\n'; return false; }

    // Shared FE palette every paletted kit/surface bitmap indexes into (render-demo pattern).
    fs::path pal = wc98::findCaseInsensitive(root, "FE/PACK/PAL.PAK");
    if (pal.empty()) pal = wc98::findCaseInsensitive(root, "PAL.PAK");
    if (!pal.empty()) formats::setGlobalPalette(formats::wc98GlobalPaletteFromPalArchive(wc98::readFile(pal)));

    // Teams: the single game database. FCDB.DBI carries 56 teams (team table @0x204, count 56,
    // record size 68); the localized FCDB_<LANG>.DBI/FCDBP<LANG>.DBI supply the display identities.
    // Index 0 = id 26 and index 26 = id 67 both ship PK###H + PK###A atlases: home kit of the
    // first, away of the latter.
    try {
        game = sim::loadGameData(root, lang);
    } catch (const std::exception& e) { std::cerr << "skip: " << e.what() << '\n'; return false; }
    const auto& db = game.db;
    if (db.teams.size() <= std::size_t(teamIndex[1])) { std::cerr << "skip: FCDB has " << db.teams.size() << " teams\n"; return false; }

    md.sheets[0] = sim::makeTeamSheet(db, std::size_t(teamIndex[0]));
    md.sheets[1] = sim::makeTeamSheet(db, std::size_t(teamIndex[1]));
    if (!sim::loadFormationTables(md, root)) { std::cerr << "skip: GEN4TBLS.VIV formation tables unreadable\n"; return false; }
    if (md.sheets[0].squad.size() < 11 || md.sheets[1].squad.size() < 11) { std::cerr << "skip: squad under 11 players\n"; return false; }

    // HUD strip names: FCDB STR0 rows pair 1:1 with the FCDB.DBI team order (same GameData the
    // match plays with; IG typ1/typ2 captions take fullName).
    for (int t = 0; t < 2; ++t) {
        const std::size_t idx = std::size_t(teamIndex[std::size_t(t)]);
        abbrv[std::size_t(t)] = game.abbreviation(idx);
        names[std::size_t(t)] = game.fullName(idx);
    }

    if (!sim::loadMatchAnimData(md, root)) { std::cerr << "skip: ANIM.MO clips unreadable\n"; return false; }
    return true;
}

MatchSnapshot snapshotFromWorld(const sim::World& w, const sim::MatchData& md) {
    MatchSnapshot s;
    s.pitchSize = std::uint8_t(w.opt.pitchSize);
    s.pitchUnits = {float(w.pitch.width), float(w.pitch.length)}; // pitch.hpp width/length game units

    for (int i = 0; i < sim::kPlayers; ++i) {
        const auto& p = w.players[std::size_t(i)];
        auto& v = s.players[std::size_t(i)];
        v.pos = {fx(p.pos.x), fx(p.pos.y), fx(p.pos.z)}; // +0x0BC M+0x04 16.16
        v.facing = float(p.facing & 0x3FF);              // +0x130, stored -0x200..0x1ff
        v.team = std::uint8_t(p.team & 1);               // +0x020
        v.role = p.slot == 0 ? Role::Goalkeeper : Role::Outfield;
        const auto& squad = md.sheets[std::size_t(p.team)].squad;
        if (p.dbPlayer >= 0 && std::size_t(p.dbPlayer) < squad.size()) { // +0x024 -> roster index
            v.shirtNumber = squad[std::size_t(p.dbPlayer)].shirtNumber;
            v.skinTone = std::int8_t(squad[std::size_t(p.dbPlayer)].skinTone & 3);
        }
        // Newest instance that is not fading out (flags bit 0x40, anim.hpp +0x20): the chain head
        // p.anim.inst (+0x10) is index+1; prev links the fading-out partner.
        int inst = p.anim.inst - 1;
        while (inst >= 0 && (w.anim.inst[std::size_t(inst)].flags & 0x40u)) inst = w.anim.inst[std::size_t(inst)].prev - 1;
        if (inst >= 0) {
            const auto& a = w.anim.inst[std::size_t(inst)];
            v.clip = a.clip; // +0x94: AnimData::clips index directly
            v.keyPos = float(a.keyA) + fx(std::uint32_t(a.frac)); // keyA +0xA4, frac 0..0xFFFF
            v.mirror = (a.flags & 1u) != 0; // flags bit 0 mirror (anim.hpp: "1 mirror")
            v.yaw16 = a.angle;              // +0x50 placement yaw
        } else {
            v.clip = -1; // stand pose
        }
    }
    const auto& ref = w.players[std::size_t(sim::kReferee)];
    s.referee.pos = {fx(ref.pos.x), fx(ref.pos.y), fx(ref.pos.z)};
    s.referee.facing = float(ref.facing & 0x3FF);
    s.referee.role = Role::Referee;
    int inst = ref.anim.inst - 1;
    while (inst >= 0 && (w.anim.inst[std::size_t(inst)].flags & 0x40u)) inst = w.anim.inst[std::size_t(inst)].prev - 1;
    if (inst >= 0) {
        const auto& a = w.anim.inst[std::size_t(inst)];
        s.referee.clip = a.clip;
        s.referee.keyPos = float(a.keyA) + fx(std::uint32_t(a.frac));
        s.referee.mirror = (a.flags & 1u) != 0;
    }

    const auto& b = w.ball;
    s.ball.pos = {fx(b.pos.x), fx(b.pos.y), fx(b.pos.z)}; // B+0xDE4 current position
    s.ball.rollPhase = int(b.rollPhase) & 3;              // B+0xDAC (rollAngle >> 16, visual & 3)
    s.ball.heading = float(b.heading & 0x3FF);            // B+0xDB0 atan2(vel), game angle units
    const int holder = (b.flags & 0x20u) ? b.possessor : b.lastToucher; // 0x20 possessed, fallback touch
    s.possessor = (holder >= 0 && holder < sim::kPlayers) ? holder : -1;
    // GAMEPLAY_PARITY §5.6 poses note (TASK_HANDLERS_WC98 §7 delta-3): during the kickoff placement
    // window (h6 dolly) the rendered pose follows the dolly bearing rec+0x1d0, not a snap; ball.pos
    // above tracks the dollied position frame by frame.
    if (w.match.kickoffPlace.dollyActive)
        s.ball.heading = float(w.match.kickoffPlace.bearing & 0x3FF);

    for (int t = 0; t < 2; ++t) {
        // TeamState::side (+0x004 "attack direction"): hypothesis side 0 -> +z, 1 -> -z.
        s.attackDir[std::size_t(t)] = std::int8_t(w.teams[std::size_t(t)].side == 0 ? 1 : -1);
        s.hud.score[std::size_t(t)] = w.match.score[t]; // DAT_0065E050
    }
    s.hud.clockSeconds = float(w.match.elapsedSec); // DAT_0065F1C8 seconds in this period
    s.hud.period = w.match.periodIndex;             // DAT_0065F224
    s.camera.mode = CameraMode::Tele;
    return s;
}
// ---- IG presentation boards (analysis/pass13/IG_FAMILIES_SPEC.md §C) ----------------------------
// IGLAY.BIG members igstats/ighalf/igflsum/igscsum rendered from IgMatchData. Raw baselines:
// /tmp/sweep-ig/report.txt (igstats texts=63, igflsum 42, igscsum 53, ighalf 0 = runtime rows).
// Composite paints the scene-ownership visible whitelist; every caption/value is painted by this
// file's overlay from FC-bank ids verified against COMMON/FCENG.BIN (FCLG index): title 28 'Match
// Statistics' / 858 'HALF-TIME' / 776 'Full-Time' / 1553 'SCORE SUMMARY'; stat rows 1382 Possession,
// 516 Corners Won, 392 Caught Offside, 758 Fouls Committed, 354 CARDS (page 0) + 781 GOALS,
// 1708 Shots off Target (page 1); summary column headers 1862 'Time', 1351 'Player', 1603 'Score';
// page buttons 1771 'Statistics' / 329 'Booking Summary'. Static textid soup slots (igstats
// ty12..ty16 = engine-context captions, summary typ3..typ5/typ8/typ9/up/down) are EXCLUDED from the
// whitelist per the f0f8109 composite rule; null.fsh box* placeholders stay legit invisible.
namespace {
std::string lowerName(std::string_view s) { std::string o(s); for (char& c : o) c = char(std::tolower(static_cast<unsigned char>(c))); return o; }
const formats::LayoutObject* layObj(const formats::ScreenLayout& sc, std::string_view n) {
    const std::string want = lowerName(n);
    for (const auto& o : sc.objects)
        if (lowerName(o.name) == want) return &o;
    return nullptr;
}
// objecttype-43 box widgets declare no FILLCOLOUR/shape: the engine draws them as frame-only
// outlines (IGMENU idiom, pass-26). Boxes with a fill (RECT plates) are painted by composite().
void strokeBoxes(render::SpriteBatch& sb, const formats::ScreenLayout& sc, presentation::FeDrawStats& st) {
    const std::uint32_t edge = render::packRgba(150u, 150u, 150u, 200u);
    for (const auto& o : sc.objects) {
        if (o.type != 43 || !o.shown()) continue;
        if (const std::string* f = o.prop("FILLCOLOUR"); f && !f->empty() && *f != "0") continue;
        const float bx = float(o.x), by = float(o.y);
        const float bw = float(o.intProp("BOXWIDTH", o.width));
        const float bh = float(o.intProp("BOXHEIGHT", o.height));
        if (bw < 2.f || bh < 2.f) continue;
        sb.draw(0, {bx, by, bw, 1.f}, {}, edge);
        sb.draw(0, {bx, by + bh - 1.f, bw, 1.f}, {}, edge);
        sb.draw(0, {bx, by, 1.f, bh}, {}, edge);
        sb.draw(0, {bx + bw - 1.f, by, 1.f, bh}, {}, edge);
        st.shapes += 4;
    }
}
// Centered caption painter (FeFlow::paintCaption pattern with explicit box width).
int paintCentered(presentation::FeText* txt, render::SpriteBatch& ui, std::string_view s, float cx, float cy,
                  float maxW, std::uint32_t rgba = render::packRgba(255u, 236u, 190u, 255u), int fontId = 4) {
    if (!txt || !txt->ready() || s.empty()) return 0;
    const formats::FfnFont* f = txt->face(fontId);
    render::TextureId ftex = txt->faceTexture(fontId);
    if (!f || !ftex) { f = txt->font(); ftex = txt->texture(); }
    if (!f || !ftex) return 0;
    const float sizePx = f->lineHeight ? float(f->lineHeight) : 16.f;
    std::string str(s);
    while (str.size() > 1 && presentation::textWidthPx(*f, str, sizePx) > maxW) str.pop_back();
    int n = 0;
    for (const auto& q : presentation::layoutTextCentered(*f, str, cx, cy, sizePx, rgba)) { ui.draw(ftex, q.dst, q.uv, q.rgba); ++n; }
    return n;
}
std::string numStr(int v) { return std::to_string(v); }
// Left-aligned caption painter mirroring drawLabel's placement (px = x + TEXTXOFFSET, py = widget
// origin). Used where composite() excludes the slot and a plate paints over it in LAY order
// (igflsum/igscsum bo25 bars, typ3..typ5 headers; pass-43).
int paintLeft(presentation::FeText* txt, render::SpriteBatch& ui, std::string_view s, float px, float py,
              int fontId, std::uint32_t rgba) {
    if (!txt || !txt->ready() || s.empty()) return 0;
    const formats::FfnFont* f = txt->face(fontId);
    render::TextureId ftex = txt->faceTexture(fontId);
    if (!f || !ftex) { f = txt->font(); ftex = txt->texture(); }
    if (!f || !ftex) return 0;
    const float sizePx = f->lineHeight ? float(f->lineHeight) : 16.f;
    int n = 0;
    for (const auto& q : presentation::layoutText(*f, std::string(s), px, py, sizePx, rgba)) { ui.draw(ftex, q.dst, q.uv, q.rgba); ++n; }
    return n;
}
// Palette ink for LAY label widgets: TEXTCOLOUR/NORMALCOLOUR index into the global palette
// (drawLabel rule); index 1 is the board-caption ink on all summary layouts.
std::uint32_t paletteInk(const formats::LayoutObject& o) {
    const int cidx = o.intProp("TEXTCOLOUR", o.intProp("NORMALCOLOUR", -1));
    const auto& pal = formats::globalPalette();
    if (cidx >= 0 && std::size_t(cidx) < pal.size())
        return render::packRgba(pal[std::size_t(cidx)][0], pal[std::size_t(cidx)][1], pal[std::size_t(cidx)][2], 0xFF);
    return render::packRgba(0xFFu, 0xFFu, 0xFFu, 0xFFu);
}
} // namespace

IgBoards::IgBoards(render::Renderer& r, const fs::path& tree) : tree_(tree), art_(r, tree_, tree / "FE/INGAME") {
    text_ = std::make_unique<presentation::FeText>(r, tree_); // FC*.BIN captions + FFN faces (non-throwing)
}

bool IgBoards::open(std::string_view name) {
    const std::string n = lowerName(name);
    static constexpr std::array<const char*, 2> archives = {"LAYOUTS.BIG", "IGLAY.BIG"}; // popup-lane search order
    try {
        fs::path d;
        std::string text;
        bool found = false;
        for (const char* arc : archives) {
            try { text = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS" / arc).string() + "@" + n + ".LAY", d); found = true; break; }
            catch (const std::exception&) {} // member absent in this archive: try the next
        }
        if (!found) {
            const fs::path loose = wc98::findCaseInsensitive(tree_, "FE/INGAME/" + n + ".LAY");
            if (loose.empty()) return false;
            text = presentation::readLayout(loose.string(), d);
        }
        sc_ = formats::ScreenLayout::build(n, formats::LayoutDocument::parse(std::string_view(text)), nullptr, nullptr, "PC");
    } catch (const std::exception&) { return false; }
    kind_ = n == "igstats" ? 1 : n == "ighalf" ? 2 : n == "igflsum" ? 3 : n == "igscsum" ? 4
          : n == "ighilite" ? 5 : n == "ighilsav" ? 6 : n == "ighilit1" ? 7
          : n == "igteam72" ? 8 : n == "igteam73" ? 9 : n == "igteam74" ? 10 : n == "igteam75" ? 11
          : n == "igteam79" ? 12 : n == "igteam80" ? 13 : n == "igteam89" ? 14 : n == "manmarks" ? 15
          : n == "igcamera" ? 16 : n == "igcamsel" ? 17 : n == "igpract1" ? 18 : n == "igpract2" ? 19
          : n == "igprac1" ? 20 : n == "igoptn2" ? 21 : n == "igaoptns" ? 22 : n == "iggoptns" ? 23 : n == "igmoptns" ? 24 : n == "control1" ? 25 : 0; // §G camera popups (16/17) + practise pages (18/19/20) + IGOPTN2 board (21, pass-21igscn) + legacy popup trio (22/23/24). IGHILIT1 = dummy hotkey-transition page, LAY comment. §D highlights + §E IGTEAM management mirrors + MANMARKS ratings board + CONTROL1 controller-select board (pass-26)
    page_ = 0;
    hlSel_ = 0;
    mgSel_ = 0;
    igRow_ = 0;
    igRows_.clear();
    igVals_.clear();
    igTab_ = 0;
    igRowsTab_ = {}; // per-tab panes rebuilt on open (std::array has no clear())
    igValsTab_ = {};
    if (kind_ >= 21) { // options layouts: the list widgets' ROWS tuples = FC-bank caption ids (analysis/pass21igmenu)
        if (kind_ == 21) { // board: lst1..lst4 = the four tab panes (Match/GC/controller/audio)
            static constexpr std::array<const char*, 4> kLst{"lst1", "lst2", "lst3", "lst4"};
            for (int t = 0; t < 4; ++t) {
                if (const formats::LayoutObject* list = sc_.object(kLst[std::size_t(t)]); list && list->prop("ROWS"))
                    igRowsTab_[std::size_t(t)] = presentation::parseIgRows(*list->prop("ROWS"));
            }
            igRows_ = igRowsTab_[0];
            for (int t = 0; t < 4; ++t) igValsTab_[std::size_t(t)].assign(igRowsTab_[std::size_t(t)].size(), 0);
            if (text_) { // Language row initial = current FC bank language (retail board reflects settings)
                const std::string_view lang = text_->language();
                static constexpr std::pair<std::string_view, int> kLangWid[] = {
                    {"dut", 93}, {"eng", 94}, {"fre", 95}, {"ger", 96}, {"ita", 97}, {"dan", 4398}, {"spa", 99}, {"swe", 100}};
                int wid = 94;
                for (const auto& [c, w] : kLangWid) if (c == lang) { wid = w; break; }
                for (int t = 0; t < 4; ++t) {
                    auto& rows = igRowsTab_[std::size_t(t)];
                    auto& vals = igValsTab_[std::size_t(t)];
                    for (std::size_t i = 0; i < rows.size() && i < vals.size(); ++i)
                        if (rows[i].label == 92) // lst1 row 2: Language (widget id 92 -> FC 968)
                            for (std::size_t v = 0; v < rows[i].values.size(); ++v)
                                if (rows[i].values[v] == wid) { vals[i] = int(v); break; }
                }
            }
        } else if (const formats::LayoutObject* list = sc_.object("list"); list && list->prop("ROWS"))
            igRows_ = presentation::parseIgRows(*list->prop("ROWS"));
        igVals_ = kind_ == 21 ? igValsTab_[0] : std::vector<int>(igRows_.size(), 0); // board: active pane keeps tab-0 init (Language row)
    }
    return kind_ != 0;
}

void IgBoards::movePage(int dir) {
    if (!kind_) return;
    if (kind_ >= 8 && kind_ <= 15) return; // IGTEAM pages: the gobk/gofd page cycle is caller-side (openBoard chain)
    if (kind_ >= 5) { // btlt/btrt browser arrows: 10-row slot pages over the 100-entry table
        hlSel_ += dir * 10;
        if (hlSel_ < 0) hlSel_ = 0;
        page_ = hlSel_ / 10;
        return;
    }
    page_ = (page_ + dir + 2) % 2; // 'igbC' btn1..btn4 page switch [C] FUN_0043a3f4: two pages on the L/R axis
}

void IgBoards::moveHl(int dir, int nRows) {
    if (nRows <= 0) return;
    hlSel_ = (((hlSel_ + dir) % nRows) + nRows) % nRows; // slol/slor slot arrows wrap the populated rows
    page_ = hlSel_ / 10;
}
void IgBoards::moveIgRow(int dir) {
    if (kind_ < 21) return;
    const int n = std::max(1, int(igRows_.size()));
    igRow_ = (igRow_ + dir + n) % n; // list widget row cursor (ROWHEIGHT stride rows) [H]
}

void IgBoards::cycleIgValue(int dir) {
    if (kind_ < 21 || igRow_ >= int(igRows_.size())) return;
    const std::vector<int>& vals = igRows_[std::size_t(igRow_)].values;
    if (vals.empty() || igVals_.size() <= std::size_t(igRow_)) return;
    const int n = int(vals.size());
    int& v = igVals_[std::size_t(igRow_)];
    v = (((v + dir) % n) + n) % n; // value caption cycle over the tuple's FC ids
    if (kind_ == 21) igValsTab_[std::size_t(std::clamp(igTab_, 0, 3))] = igVals_; // board tabs keep state
}

void IgBoards::setIgTab(int t) {
    if (kind_ != 21) return;
    igTab_ = std::clamp(t, 0, 3);
    igRows_ = igRowsTab_[std::size_t(igTab_)];
    igVals_ = igValsTab_[std::size_t(igTab_)];
    if (igVals_.size() != igRows_.size()) igVals_.assign(igRows_.size(), 0);
    igRow_ = igRows_.empty() ? 0 : std::clamp(igRow_, 0, int(igRows_.size()) - 1);
}

presentation::FeDrawStats IgBoards::draw(render::SpriteBatch& ui, const IgMatchData& d) {
    using namespace presentation;
    FeDrawStats st;
    if (!kind_) return st;
    // Scene-ownership whitelist: everything shown() except the static textid soup slots.
    std::vector<std::string> excl;
    if (kind_ == 1) excl = {"titl", "ty12", "ty13", "ty14", "ty15", "ty16", "typ8", "typ9"}; // ty12.. static engine captions
    else if (kind_ >= 3) excl = {"titl", "typ3", "typ4", "typ5", "typ6", "typ7", "typ8", "typ9"}; // header soup + bar captions (overlay paints them after the bo25 plates; arrows stay composite: btnup->ingame alias)
    std::vector<std::string> visible;
    for (const auto& o : sc_.objects) {
        if (!o.shown()) continue;
        const std::string n = lowerName(o.name);
        if (std::ranges::find(excl, n) != excl.end()) continue;
        visible.push_back(n); // composite() applies the whitelist by name (duplicates included)
    }
    st = composite(sc_, ui, art_, text_.get(), &visible);
    strokeBoxes(ui, sc_, st); // frame-only outlines for null-art box widgets [H] IGMENU idiom
    const auto cap = [&](std::size_t id) { return text_ && text_->ready() ? std::string(text_->stringAt(id)) : std::string(); };

    // Title (box1 x=116 w=408): FC-bank caption centered across the header pane.
    static constexpr int kTitle[5] = {0, 28, 858, 776, 1553}; // Match Statistics / HALF-TIME / Full-Time / SCORE SUMMARY
    if (const formats::LayoutObject* t = layObj(sc_, "titl")) st.texts += paintCentered(text_.get(), ui, cap(kind_ < 5 ? std::size_t(kTitle[kind_]) : 0u), 320.f, float(t->y) + 8.f, 400.f); // CONTROL1 (25) has a titl textid=0 slot: no caption, no table lookup

    // Header block: team names (typ1/typ2), shields, then score numbers LAST (nmb1/nmb2 over the
    // box4/box5 RECTs): the number slots sit inside the shield art's x-range on these layouts, so
    // the digits must paint after the flag quads to stay legible.
    for (int t = 0; t < 2; ++t) {
        if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "typ1" : "typ2")) st.texts += paintCentered(text_.get(), ui, d.names[std::size_t(t)], float(o->x) + 50.f, float(o->y) + 8.f, 130.f);
        if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "flg1" : "flg2"); o && !d.flags[std::size_t(t)].empty()) {
            const ScreenArt::Shape sh = art_.shape("..\\global\\", d.flags[std::size_t(t)], "f000", -1); // FNNN record-index shields (FEFLOW11 rule)
            if (sh.tex) { ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
            else ++st.missing;
        }
    }
    for (int t = 0; t < 2; ++t) {
        const formats::LayoutObject* box = layObj(sc_, t == 0 ? "box4" : "box5");
        if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "nmb1" : "nmb2")) {
            const float cx = box ? float(box->x) + 13.f : float(o->x) + 6.f; // box4/5 width 26 (ighalf type-0 slot: same geometry)
            st.texts += paintCentered(text_.get(), ui, numStr(d.score[std::size_t(t)]), cx, float(o->y) + 7.f, 24.f);
        }
    }

    if (kind_ <= 2) { // stat grid: 5 rows x 2 pages on the typ3..ty11 label slots
        static constexpr std::array<int, 5> kPage0Ids{1382, 516, 392, 758, 354}; // Possession / Corners Won / Caught Offside / Fouls Committed / CARDS
        static constexpr std::array<int, 5> kPage1Ids{781, 1708, 0, 0, 0};       // GOALS / Shots off Target (rows 3..5 stay blank)
        const bool p0 = page_ == 0;
        const auto& ids = p0 ? kPage0Ids : kPage1Ids;
        const int nRows = p0 ? 5 : 2;
        static constexpr std::array<const char*, 5> kLabelSlots{"typ3", "typ4", "typ5", "ty10", "ty11"};
        const float homeCx = kind_ == 1 ? 232.f : 218.f, awayCx = kind_ == 1 ? 460.f : 463.f; // bo15 x=212 / ty17 slot x (dumped)
        for (int row = 0; row < nRows; ++row) {
            const formats::LayoutObject* lab = layObj(sc_, kLabelSlots[std::size_t(row)]);
            if (!lab) continue;
            const float cy = float(lab->y) + 8.f;
            st.texts += paintCentered(text_.get(), ui, cap(std::size_t(ids[row])), (homeCx + awayCx) * 0.5f, cy, 190.f);
            for (int t = 0; t < 2; ++t) {
                const auto& s = d.stats[std::size_t(t)];
                std::string val;
                if (!p0) val = ids[row] == 781 ? numStr(s.goals) : numStr(s.shotsWide);
                else switch (row) {
                    case 0: val = numStr(d.possessionPct[std::size_t(t)]) + '%'; break;
                    case 1: val = numStr(s.corners); break;
                    case 2: val = numStr(s.offsides); break;
                    case 3: val = numStr(s.fouls); break;
                    default: val = numStr(s.cards); break;
                }
                st.texts += paintCentered(text_.get(), ui, val, t == 0 ? homeCx : awayCx, cy, 44.f);
            }
        }
        // Page state = pressed chip chrome only (pass-43): box6/box7 are the bottom-bar button
        // plates under the typ6/typ7 captions ('Match Statistics' / 'Bookings Summary'), which
        // the overlay paints after the plates — the earlier centered 'Statistics' / 'Booking
        // Summary' kPageCap soup ids 1771/329 caption text is gone (duplicated over the bar).
        for (int p = 0; p < 2; ++p) {
            if (p == page_) { // pressed chrome on the active page button ('igbC' grammar: btnd over btnn)
                if (const formats::LayoutObject* b = layObj(sc_, p == 0 ? "btn1" : "btn2")) {
                    const ScreenArt::Shape sh = art_.shape("..\\global\\", "INGAME.FSH", "btnd", -1); // button.fsh stem alias
                    if (sh.tex) { ui.draw(sh.tex, {float(b->x), float(b->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
                }
            }
        }
    } else { // summary list: Time / Player / Team columns over lst1..lst3 (objecttype 9).
        // Headers = typ3/typ4/typ5 widgets (composite-excluded): LAY alignment=1, textid remap
        // (igscsum 347/348/349 -> bank 1862 'Time' / 1351 'Player' / 1828 'Team'). Painted
        // left-aligned at widget x (pass-43: centered-on-column was the wrong grammar).
        static constexpr std::array<const char*, 3> kCols{"lst1", "lst2", "lst3"};
        static constexpr std::array<const char*, 3> kHeads{"typ3", "typ4", "typ5"};
        for (int c = 0; c < 3; ++c) {
            const formats::LayoutObject* lst = layObj(sc_, kCols[std::size_t(c)]);
            if (!lst) continue;
            if (const formats::LayoutObject* h = layObj(sc_, kHeads[std::size_t(c)]))
                if (const int tid = h->intProp("TEXTID", -1); tid > 0 && text_ && text_->ready())
                    st.texts += paintLeft(text_.get(), ui, text_->widgetStringAt(std::size_t(tid)),
                                          float(h->x) + float(h->intProp("TEXTXOFFSET", 0)), float(h->y), 0, paletteInk(*h));
            std::vector<std::string> rows;
            for (const auto& g : d.goals) {
                if (c == 0) rows.push_back(std::format("{}'", g.minute));
                else if (c == 1) rows.push_back(std::format("#{}{}", g.shirtNo, g.penalty ? " pen" : g.ownGoal ? " og" : ""));
                else rows.push_back(d.names[std::size_t(g.team)]); // 'Team' column (header remap 1828)
            }
            if (!rows.empty()) paintListRows(ui, *lst, rows, -1, *text_, 4, st);
        }
    }
    // Bottom bar (pass-43): the white bo25 RECT plates paint AFTER the typ6..typ9 caption
    // widgets in LAY order, hiding their labels; overlay all four remap-resolved captions on
    // top of the plates at their exact LAY positions (alignment=1 left at widget x:
    // 'Match Statistics' / 'Bookings Summary' / 'Score Summary' / 'Back'). Applies to every
    // kind of this family; widgets absent from a layout are skipped.
    for (const char* nm : {"typ6", "typ7", "typ8", "typ9"}) {
        if (const formats::LayoutObject* o = layObj(sc_, nm))
            if (const int tid = o->intProp("TEXTID", -1); tid > 0 && text_ && text_->ready())
                st.texts += paintLeft(text_.get(), ui, text_->widgetStringAt(std::size_t(tid)),
                                      float(o->x) + float(o->intProp("TEXTXOFFSET", 0)), float(o->y), 0, paletteInk(*o));
    }
    return st;
}

// ---- Highlights family (IG_FAMILIES_SPEC.md §D) -----------------------------------------------
// ighilite = playback transport browser: box3 video pane + btlt/btrt page arrows + play/paus/
// fast/rewd/slol/slor/save controls over the rply index rows; typ4..7 caption slots and titl carry
// FC-bank soup ids (249 'Attack', 77 '1', 303 'Bald') so composite skips them per f0f8109 rule and
// this overlay paints the runtime captions ('INSTANT REPLAY' 899, 'Highlights' 885, 'Replay' 1484).
// ighilsav = save list: lst1 rows from the rply index (paintListRows), flg1/flg2 FNNN flag quads +
// typ1/typ2 names + nmb pair digits (digits paint AFTER the flag quads — z-order lesson 5344a02),
// up/down scroll arrows via the btnup->ingame stem alias, save/back button pair. ighilit1 is the
// shipped dummy hotkey-transition page (single null box1): composite-only, shapes/texts 0 legit.
presentation::FeDrawStats IgBoards::draw(render::SpriteBatch& ui, const IgHlData& d) {
    using namespace presentation;
    FeDrawStats st;
    if (kind_ < 5) return st;
    // IGHILSAV 'titl' textid 303 = FC remap 'Save Replay' [H] — a valid static caption (raw-bank
    // interpretation as 'Bald' soup was the pass-21igscn misread); composite paints the label.
    std::vector<std::string> excl = kind_ == 6 ? std::vector<std::string>{"typ6", "typ8"}
                                               : std::vector<std::string>{"typ4", "typ5", "typ6", "typ7"};
    std::vector<std::string> visible;
    for (const auto& o : sc_.objects) {
        if (!o.shown()) continue;
        const std::string n = lowerName(o.name);
        if (std::ranges::find(excl, n) != excl.end()) continue;
        visible.push_back(n); // composite() applies the whitelist by name (duplicates included)
    }
    st = composite(sc_, ui, art_, text_.get(), &visible);
    strokeBoxes(ui, sc_, st); // frame-only outlines for null-art box widgets [H] IGMENU idiom
    const auto cap = [&](std::size_t id) { return text_ && text_->ready() ? std::string(text_->stringAt(id)) : std::string(); };

    // Rows with an index entry + on-disk file (hlRows() already filters populated).
    std::vector<const IgHlRow*> rows;
    for (const auto& r : d.rows) if (r.populated && !r.name.empty()) rows.push_back(&r);
    const int n = int(rows.size());
    hlSel_ = n ? (((hlSel_ % n) + n) % n) : 0;
    page_ = hlSel_ / 10;
    const IgHlRow* sel = n ? rows[std::size_t(hlSel_)] : nullptr;
    static constexpr std::uint32_t kBlack = render::packRgba(8u, 8u, 32u, 255u); // text over the white RECT fields

    if (kind_ == 5) {
        // Header pane title: FC-bank 'INSTANT REPLAY' (899) centered on box3 (x=116 w=408).
        if (const formats::LayoutObject* b3 = layObj(sc_, "box3")) st.texts += paintCentered(text_.get(), ui, cap(899), 320.f, float(b3->y) + 10.f, 400.f);
        // Current slot filename over the white box6/box7 entry field (x=132 w=200 y=397).
        if (const formats::LayoutObject* f = layObj(sc_, "box6"); f && sel) st.texts += paintCentered(text_.get(), ui, sel->name, float(f->x) + 100.f, float(f->y) + 9.f, 195.f, kBlack);
        // Slot number readout over the white box8 strip between the btlt/btrt arrows (x=159 w=134 y=447).
        if (const formats::LayoutObject* f = layObj(sc_, "box8")) st.texts += paintCentered(text_.get(), ui, sel ? std::format("SLOT {:04d}", sel->number) : std::string("--------"), float(f->x) + 67.f, float(f->y) + 9.f, 130.f, kBlack);
 // Transport state caption at the typ5 centre slot (play/pause share x=407: a toggle pair).
        if (const formats::LayoutObject* t = layObj(sc_, "typ5")) st.texts += paintCentered(text_.get(), ui, cap(1484), float(t->x), float(t->y) + 6.f, 120.f);
        // Right-column captions (soup ids 303/77): 'Highlights' over the save column, 'INSTANT REPLAY' under it.
        if (const formats::LayoutObject* t = layObj(sc_, "typ7")) st.texts += paintCentered(text_.get(), ui, cap(885), float(t->x) - 4.f, float(t->y) + 6.f, 100.f);
        if (const formats::LayoutObject* t = layObj(sc_, "typ6")) st.texts += paintCentered(text_.get(), ui, cap(899), float(t->x) - 4.f, float(t->y) + 6.f, 100.f);
    } else if (kind_ == 6) {
        // Header pair: names + FNNN shields first, score digits LAST (nmb1/nmb2 over box4/box5).
        if (d.hdr) {
            for (int t = 0; t < 2; ++t) {
                if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "typ1" : "typ2")) st.texts += paintCentered(text_.get(), ui, d.hdr->names[std::size_t(t)], float(o->x) + 50.f, float(o->y) + 8.f, 130.f);
                if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "flg1" : "flg2"); o && !d.hdr->flags[std::size_t(t)].empty()) {
                    const ScreenArt::Shape sh = art_.shape("..\\global\\", d.hdr->flags[std::size_t(t)], "f000", -1); // FNNN record-index shields (FEFLOW11 rule)
                    if (sh.tex) { ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
                    else ++st.missing;
                }
            }
            for (int t = 0; t < 2; ++t)
                if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "nmb1" : "nmb2")) {
                    const formats::LayoutObject* box = layObj(sc_, t == 0 ? "box4" : "box5"); // white 26x26 slots at x=212/399 (dumped)
                    const float cx = box ? float(box->x) + 13.f : float(o->x) + 6.f;
                    st.texts += paintCentered(text_.get(), ui, numStr(d.hdr->score[std::size_t(t)]), cx, float(o->y) + 7.f, 24.f);
                }
        }
        // lst1 save-list rows from the rply index (captions = slot filenames), cursor on hlSel_.
        if (const formats::LayoutObject* lst = layObj(sc_, "lst1")) {
            std::vector<std::string> caps;
            for (int i = 0; i < n && caps.size() < 10; ++i) caps.push_back(rows[std::size_t(i)]->name); // box h=200 @ Lineheight 20
            paintListRows(ui, *lst, caps, hlSel_, *text_, 4, st);
        }
        // Footer value fields over the white RECTs (box6 x=175 / box8 x=349, w=140 y=383): slot + filename.
        if (const formats::LayoutObject* f = layObj(sc_, "box6")) st.texts += paintCentered(text_.get(), ui, sel ? std::format("SLOT {:04d}", sel->number) : std::string(), float(f->x) + 70.f, float(f->y) + 9.f, 135.f, kBlack);
        if (const formats::LayoutObject* f = layObj(sc_, "box8"); f && sel) st.texts += paintCentered(text_.get(), ui, sel->name, float(f->x) + 70.f, float(f->y) + 9.f, 135.f, kBlack);
    }
    return st; // kind 7 (ighilit1): dummy transition page, composite only
}
// ---- Camera popups + practise pages (IG_FAMILIES_SPEC.md §G/§H) --------------------------------
// igcamera = camera-option popup over the live match: shared header pane (typ1/typ2 names, flg FNNN
// shields, RECT box4/box5 + nmb score digits painted AFTER the flag quads), FC#353 'CAMERA' title,
// the menu list widget's 5 key-assignment rows (FC captions Raise/Lower/Rotate Left/Rotate Right/
// Moves-Highlight-through-cameras @1468/1011/1545/1546/1104 + binding strings) with a row cursor,
// and the forw/menu nav pair (INGAME.FSH btnn/btnd art + FC 295 'Back' / 751 'Forward' captions over
// box6/box7). Soup slots typ4/typ5 (textid 77 '1', 1550) are excluded per the f0f8109 rule; null.fsh
// box* placeholders stay legit invisible. igcamsel = camera-select grid: FC#379 title + the menu
// widget's numItems=7 rows painted from the verified FC camera-name ids (Action/Ball/Classic Cam,
// End Zone/Free/Stadium/Shoulder Cam @175/306/405/633/769/1757/1711); Enter applies the row through
// presentation::MatchCamera. igpract1 = practise page 1: FC#1249 'Number of Players' caption over
// box9 + stepper value digits (bo10/bo11, bt4n/bt5n art) + keeper rows FC#1920 'User Keeper'/
// #374 'CPU Keeper' with On/Off values (FC 1267/1263) and toggle buttons; igpract2 = page 2 option
// grid; igprac1 = on/off options page. Soup ids (typ3 699, typ8/typ9 702/703, ty12/ty13 761/432)
// excluded. All captions verified against COMMON/FCENG.BIN (FCLG index).
presentation::FeDrawStats IgBoards::draw(render::SpriteBatch& ui, const IgMatchData& d, const IgCamData& cam) {
    using namespace presentation;
    FeDrawStats st;
    if (kind_ != 16 && kind_ != 17) return st;
    std::vector<std::string> excl = {"titl"};
    if (kind_ == 16) for (const char* s : {"typ4", "typ5"}) excl.push_back(s); // nav soup ids 77/1550 [C] f0f8109
    std::vector<std::string> visible;
    for (const auto& o : sc_.objects) {
        if (!o.shown()) continue;
        const std::string n = lowerName(o.name);
        if (std::ranges::find(excl, n) != excl.end()) continue;
        visible.push_back(n);
    }
    st = composite(sc_, ui, art_, text_.get(), &visible);
    strokeBoxes(ui, sc_, st); // frame-only outlines for null-art box widgets [H] IGMENU idiom
    const auto cap = [&](std::size_t id) { return text_ && text_->ready() ? std::string(text_->stringAt(id)) : std::string(); };
    // Shared header pane: names + shields first, score digits LAST (z-order lesson 5344a02).
    for (int t = 0; t < 2; ++t) {
        if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "typ1" : "typ2")) st.texts += paintCentered(text_.get(), ui, d.names[std::size_t(t)], float(o->x) + 50.f, float(o->y) + 8.f, 130.f);
        if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "flg1" : "flg2"); o && !d.flags[std::size_t(t)].empty()) {
            const ScreenArt::Shape sh = art_.shape("..\\global\\", d.flags[std::size_t(t)], "f000", -1); // FNNN record-index shields (FEFLOW11 rule)
            if (sh.tex) { ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
            else ++st.missing;
        }
    }
    for (int t = 0; t < 2; ++t)
        if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "nmb1" : "nmb2"))
            st.texts += paintCentered(text_.get(), ui, numStr(d.score[std::size_t(t)]), float(o->x) + 6.f, float(o->y) + 7.f, 24.f);
    const auto navPair = [&]() { // forw/menu pair: INGAME.FSH btnn/btnd art + FC Back/Forward captions over box6/box7
        if (const formats::LayoutObject* b = layObj(sc_, "back")) {
            const ScreenArt::Shape sh = art_.shape("..\\global\\", "INGAME.FSH", page_ == 1 ? "btnd" : "btnn", -1); // 'igbC' pressed chrome on the focused nav slot [C] FUN_0043a3f4
            if (sh.tex) { ui.draw(sh.tex, {float(b->x), float(b->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
        }
        if (const formats::LayoutObject* b = layObj(sc_, "forw")) {
            const ScreenArt::Shape sh = art_.shape("..\\global\\", "INGAME.FSH", page_ == 0 ? "btnd" : "btnn", -1);
            if (sh.tex) { ui.draw(sh.tex, {float(b->x), float(b->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
        }
        if (const formats::LayoutObject* r6 = layObj(sc_, "box6")) st.texts += paintCentered(text_.get(), ui, cap(295), float(r6->x) + std::max(40, r6->width) / 2.f, float(r6->y) + 9.f, 90.f);
        if (const formats::LayoutObject* r7 = layObj(sc_, "box7")) st.texts += paintCentered(text_.get(), ui, cap(751), float(r7->x) + std::max(40, r7->width) / 2.f, float(r7->y) + 9.f, 90.f);
    };
    if (kind_ == 16) { // igcamera: FC#353 title + the menu list's 5 key-assignment option rows
        st.texts += paintCentered(text_.get(), ui, cap(353), 320.f, 39.f, 400.f);
        static constexpr std::array<int, 5> kCamRowIds{1468, 1011, 1545, 1546, 1104}; // Raise / Lower / Rotate L / Rotate R / Camera Cycle [C] FCENG verified
        const formats::LayoutObject* list = layObj(sc_, "menu");
        for (int i = 0; i < 5 && std::size_t(i) < cam.binds.size(); ++i) {
            const float cy = float(list ? list->y : 138) + 14.f + 34.f * float(i);
            st.texts += paintCentered(text_.get(), ui, cap(std::size_t(kCamRowIds[std::size_t(i)])), 250.f, cy, 190.f,
                                      camSel() == i ? render::packRgba(255u, 236u, 90u, 255u) : render::packRgba(255u, 236u, 190u, 255u));
            st.texts += paintCentered(text_.get(), ui, cam.binds[std::size_t(i)], 470.f, cy, 120.f);
        }
        if (camSel() >= 0) { ui.draw(0, {168.f, float(list ? list->y : 138) + 6.f + 34.f * float(camSel()), 4.f, 16.f}, {}, render::packRgba(255u, 236u, 90u, 255u)); ++st.shapes; } // row cursor marker (flat quad)
        navPair();
    } else { // igcamsel: FC#379 title + the menu widget's numItems=7 camera grid rows
        st.texts += paintCentered(text_.get(), ui, cap(379), 320.f, 43.f, 400.f);
        static constexpr std::array<int, 7> kCamSelIds{175, 306, 405, 633, 769, 1757, 1711}; // Action/Ball/Classic/End Zone/Free/Stadium/Shoulder Cam [C] FCENG verified
        const formats::LayoutObject* list = layObj(sc_, "menu");
        for (int i = 0; i < 7; ++i) {
            const float cy = float(list ? list->y : 180) + 8.f + 16.f * float(i);
            st.texts += paintCentered(text_.get(), ui, cap(std::size_t(kCamSelIds[std::size_t(i)])), float(list ? list->x : 116) + 204.f, cy, 180.f,
                                      cam.curRow == i ? render::packRgba(255u, 236u, 90u, 255u) : render::packRgba(255u, 236u, 190u, 255u));
        }
        ui.draw(0, {float(list ? list->x : 116) + 4.f, float(list ? list->y : 180) + 2.f + 16.f * float(cam.curRow), 4.f, 13.f}, {}, render::packRgba(255u, 236u, 90u, 255u)); ++st.shapes; // grid cursor quad
    }
    return st;
}

presentation::FeDrawStats IgBoards::draw(render::SpriteBatch& ui, const IgMatchData& d, const IgPracData& prac) {
    using namespace presentation;
    FeDrawStats st;
    if (kind_ < 18) return st;
    // Soup slots excluded per f0f8109: typ3/typ4/typ5 + typ8/typ9 carry FC ids from other contexts
    // (699 'Final Match Qualifiers', 702/703 finals captions, -281.. dev ids). ty12/ty13 = FC remap
    // 761/432 = 'Attack'/'Defence' [H] — the practise-board tactic labels, painted by composite.
    std::vector<std::string> excl{"titl", "typ3", "typ4", "typ5"};
    if (kind_ != 19) { excl.push_back("typ8"); excl.push_back("typ9"); }
    std::vector<std::string> visible;
    for (const auto& o : sc_.objects) {
        if (!o.shown()) continue;
        const std::string n = lowerName(o.name);
        if (std::ranges::find(excl, n) != excl.end()) continue;
        visible.push_back(n);
    }
    st = composite(sc_, ui, art_, text_.get(), &visible);
    strokeBoxes(ui, sc_, st); // frame-only outlines for null-art box widgets [H] IGMENU idiom
    const auto cap = [&](std::size_t id) { return text_ && text_->ready() ? std::string(text_->stringAt(id)) : std::string(); };
    for (int t = 0; t < 2; ++t) { // shared header pane, digits after flags (z-order lesson 5344a02)
        if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "typ1" : "typ2")) st.texts += paintCentered(text_.get(), ui, d.names[std::size_t(t)], float(o->x) + 50.f, float(o->y) + 8.f, 130.f);
        if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "flg1" : "flg2"); o && !d.flags[std::size_t(t)].empty()) {
            const ScreenArt::Shape sh = art_.shape("..\\global\\", d.flags[std::size_t(t)], "f000", -1);
            if (sh.tex) { ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
        }
    }
    for (int t = 0; t < 2; ++t)
        if (const formats::LayoutObject* o = layObj(sc_, t == 0 ? "nmb1" : "nmb2"))
            st.texts += paintCentered(text_.get(), ui, numStr(d.score[std::size_t(t)]), float(o->x) + 6.f, float(o->y) + 7.f, 24.f);
    // Page nav pair: INGAME.FSH button art + FC 295 'Back' / 751 'Forward' captions over box6/box7.
    for (const char* w : {"back", "forw"})
        if (const formats::LayoutObject* b = layObj(sc_, w)) {
            const ScreenArt::Shape sh = art_.shape("..\\global\\", "INGAME.FSH", page_ == 0 && w[1] == 'a' ? "btnd" : page_ == 1 && w[1] == 'r' ? "btnd" : "btnn", -1);
            if (sh.tex) { ui.draw(sh.tex, {float(b->x), float(b->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
        }
    for (const char* r : {"box6", "box7"})
        if (const formats::LayoutObject* b = layObj(sc_, r)) st.texts += paintCentered(text_.get(), ui, cap(r[3] == '6' ? 295 : 751), float(b->x) + std::max(40, b->width) / 2.f, float(b->y) + 9.f, 90.f);
    // FC#1249 'Number of Players' caption over box9 + the stepper value digits (bo10/bo11 slots).
    if (const formats::LayoutObject* b = layObj(sc_, "box9")) st.texts += paintCentered(text_.get(), ui, cap(1249), float(b->x) + std::max(40, b->width) / 2.f, kind_ == 18 ? 142.f : 167.f, 150.f);
    st.texts += paintCentered(text_.get(), ui, numStr(prac.numPlayers), kind_ == 19 ? 320.f : 216.f, kind_ == 18 ? 192.f : 167.f, 40.f);
    if (kind_ != 19) { // bo11 pair digit + keeper rows: FC#1920 'User Keeper' / #374 'CPU Keeper' + On/Off values (FC 1267/1263)
        st.texts += paintCentered(text_.get(), ui, numStr(prac.numPlayers), 421.f, kind_ == 18 ? 192.f : 194.f, 40.f);
        const formats::LayoutObject *r1 = layObj(sc_, "typ8"), *r2 = layObj(sc_, "typ9");
        const float y1 = float(r1 ? r1->y : 257) + 8.f, y2 = float(r2 ? r2->y : 280) + 8.f;
        st.texts += paintCentered(text_.get(), ui, cap(1920), 250.f, y1, 130.f, mgSel() == 1 ? render::packRgba(255u, 236u, 90u, 255u) : render::packRgba(255u, 236u, 190u, 255u));
        st.texts += paintCentered(text_.get(), ui, cap(374), 250.f, y2, 130.f, mgSel() == 2 ? render::packRgba(255u, 236u, 90u, 255u) : render::packRgba(255u, 236u, 190u, 255u));
        st.texts += paintCentered(text_.get(), ui, prac.userKeeper ? cap(1267) : cap(1263), 440.f, y1, 60.f);
        st.texts += paintCentered(text_.get(), ui, prac.cpuKeeper ? cap(1267) : cap(1263), 440.f, y2, 60.f);
    }
    // Stepper + toggle buttons (igpract1/igprac1: bt4n/bt5n steppers, btnn/btnd toggles; igprac1 names them on/off).
    for (int i = 1; i <= 6; ++i) {
        const std::string w = kind_ == 20 ? (i == 5 ? "on" : i == 6 ? "off" : std::format("btn{}", i)) : std::format("btn{}", i);
        if ((kind_ == 20 && i < 5) || (kind_ != 18 && kind_ != 20)) continue; // igprac1: on/off pair only; igpract2: no steppers
        const formats::LayoutObject* b = layObj(sc_, w);
        if (!b) continue;
        const bool step = i <= 4 && kind_ == 18;
        const ScreenArt::Shape sh = art_.shape("..\\global\\", "INGAME.FSH", step ? std::format("bt{}n", i + 3).c_str() : (i >= 5 ? "btnn" : "btnn"), -1);
        if (sh.tex) { ui.draw(sh.tex, {float(b->x), float(b->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
    }
    return st;
}
// ---- IGOPTN2 in-game options (pass-21igscn, analysis/pass21igmenu) ----------------------------
// kind 21 = the IGOPTN2 board scene itself (four tab panes, scene registry row IGOPTN2); kinds
// 22/23/24 = the legacy LAYOUTS.BIG popup trio igaoptns/iggoptns/igmoptns (the FE-shell chain).
// Board composite: bpic back + subt/ttxt captions + tab2..4 strips + active header pane + help
// pane + side icons; lst1..4 + tbb1..4 stay out of the whitelist (the painter draws rows/art).
presentation::FeDrawStats IgBoards::drawIgOptns(render::SpriteBatch& ui) {
    using namespace presentation;
    FeDrawStats st;
    if (kind_ < 21) return st;
    if (kind_ == 25) { // CONTROL1 controller-select board (pass-26): every widget is a real strip/
                       // caption slot (rs1..rs4/ls1..ls4, grv0..3 quads, tmtl/tmtr caption 4594);
                       // no list widget, no soup slots -> full composite, cursor art painted by the
                       // strips' own HILITED state (no row pane to synthesise).
        st = composite(sc_, ui, art_, text_.get());
        strokeBoxes(ui, sc_, st);
        return st;
    }
    if (kind_ == 21) {
        static constexpr std::array<const char*, 4> kHeader{"matH", "gamH", "conH", "audH"};
        std::vector<std::string> visible{"bpic", "subt", "tab2", "tab3", "tab4", "help", "back", "fwrd", "ttxt"};
        visible.push_back(kHeader[std::size_t(std::clamp(igTab_, 0, 3))]); // active tab header pane
        st = composite(sc_, ui, art_, text_.get(), &visible);
    strokeBoxes(ui, sc_, st); // frame-only outlines for null-art box widgets [H] IGMENU idiom
        paintIgOptns2(ui, sc_, art_, text_.get(), igRows_, igVals_, igRow_, igTab_, st);
        return st;
    }
    static const std::vector<std::string> kVisible{"back", "titl", "go_g", "go_a", "go_c", "scrl", "1", "2", "3", "04"};
    st = composite(sc_, ui, art_, text_.get(), &kVisible);
    strokeBoxes(ui, sc_, st); // frame-only outlines for null-art box widgets [H] IGMENU idiom
    paintIgOptns(ui, sc_, art_, text_.get(), igRows_, igVals_, igRow_, kind_ == 22, st);
    if (const formats::LayoutObject* trk = sc_.object("scrl"))
        paintScrollSlot(ui, art_, *trk, igRow_, 0, std::max(1, int(igRows_.size()) - 1), st); // IGSROLBR groove/caps/thumb (pass-28scrl)
    return st;
}
// ---- IGTEAM management mirror pages (IG_FAMILIES_SPEC.md §E) ----------------------------------
// IGteam72..80/89 = in-game mirrors of the league-config family (same LAY grammar as FEFLOW72..80,
// app/fe_flow.cpp drawLeagueCfg): composite art whitelist + FCDB/sim content the engine paints from
// live state. 72 formation grid (pNNN slots, tNNN kit tiles, b010..b012 form templates), 73 sub pair
// (pln/pls rows, ab00/ab01 swap controls, yc/rc card icons), 74 kick takers + kit picker (prNN/ktNN),
// 75 attack-bias slider (s001), 79 positioning, 80 man-marking squad list, 89 hub rows b000..b005.
// Soup textid slots ('tt*'/'ad*', "0000") stay excluded per the f0f8109 rule; lbNN labels, vit team
// bars and card icons draw real art (INGAME stems). Team names/stamina paint AFTER the bar quads
// (z-order lesson 5344a02).
presentation::FeDrawStats IgBoards::draw(render::SpriteBatch& ui, const IgMgmtData& d) {
    using namespace presentation;
    FeDrawStats st;
    if (kind_ < 8) return st;
    std::vector<std::string> visible;
    for (const auto& o : sc_.objects) {
        if (!o.shown()) continue;
        const std::string n = lowerName(o.name);
        if (n == "0000") continue;                              // shared edit-box soup slot
        if (n.starts_with("tt") || n.starts_with("ad")) continue; // FC-bank default-soup labels (engine-driven)
        visible.push_back(n);                                    // composite() applies the whitelist by name
    }
    st = composite(sc_, ui, art_, text_.get(), &visible);
    strokeBoxes(ui, sc_, st); // frame-only outlines for null-art box widgets [H] IGMENU idiom
    const auto cap = [&](std::size_t id) { return text_ && text_->ready() ? std::string(text_->stringAt(id)) : std::string(); };
    const IgTeamMgmt& mg = d.teams[0]; // pages mirror the controlled side (home), hub lists both bars
    const auto txt = [&](std::size_t id, std::string_view fb) { const std::string s = cap(id); return s.empty() ? std::string(fb) : s; };

    // Row grammar: caption + tricolour kit swatch + hilite pane (paintRowLabel, list_painter §1).
    const auto rowCap = [&](std::string_view name, std::string c, bool hilited, const formats::FcdbKit1998* kit) {
        if (c.empty()) return;
        if (const formats::LayoutObject* o = layObj(sc_, name)) paintRowLabel(ui, *o, c, *text_, 0, hilited, kit, st);
    };
    const auto rowText = [](const std::vector<IgMgmtRow>& rows, int i) {
        return i >= 0 && std::size_t(i) < rows.size() ? std::format("#{} {}", rows[std::size_t(i)].shirt, rows[std::size_t(i)].name) : std::string();
    };

    // Team bars (vit0/vit1 teambar.fsh#team quads drawn by composite): names + squad fitness LAST.
    for (int t = 0; t < 2; ++t) {
        const formats::LayoutObject* v = layObj(sc_, t == 0 ? "vit0" : "vit1");
        if (!v) continue;
        const IgTeamMgmt& tm = d.teams[std::size_t(t)];
        st.texts += paintCentered(text_.get(), ui, tm.teamName, float(v->x) + 84.f, float(v->y) + 7.f, 160.f);
        st.texts += paintCentered(text_.get(), ui, std::format("{}%", tm.fitPct), float(v->x) + 152.f, float(v->y) + 7.f, 44.f);
    }
    // Subtitle band (subt slot, code-driven): FC-bank page caption centered at the panel top.
    static constexpr std::array<int, 7> kSubtId{748, 1788, 946, 250, 1379, 1025, 0}; // kind-8: Formation (IGM) / Substitutions / Kick Takers / Attack Bias / Positioning / Man Marking
    if ((kind_ >= 8 && kind_ <= 13) || kind_ == 15) st.texts += paintCentered(text_.get(), ui, txt(std::size_t(kSubtId[kind_ == 15 ? 5 : kind_ - 8]), "MANAGEMENT"), 400.f, 26.f, 300.f);

    if (kind_ == 8) { // formation grid: b010..b012 template buttons + pNNN slots with shirt numbers
        static constexpr std::array<const char*, 3> kBtn{"b010", "b011", "b012"}, kShape{"bt10", "bt20", "bt30"};
        for (int i = 0; i < 3; ++i) { // FEFLOW72 grammar: shape art + FC bank caption 233 centered
            const formats::LayoutObject* o = layObj(sc_, kBtn[std::size_t(i)]);
            if (!o) continue;
            const ScreenArt::Shape sh = art_.shape("..\\feflow72\\", "feflow72", kShape[std::size_t(i)], -1);
            if (sh.tex) { ui.draw(sh.tex, {float(o->x), float(o->y), float(o->width > 0 ? o->width : sh.w), float(o->height > 0 ? o->height : sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
            else ++st.missing;
        }
        // pNNN formation slots (roster order, GK first): shirt number centered + live-position dot.
        std::vector<const formats::LayoutObject*> slots;
        int bx0 = 1 << 30, by0 = 1 << 30, bx1 = 0, by1 = 0;
        for (const auto& o : sc_.objects) {
            const std::string n = lowerName(o.name);
            if (n.size() != 4 || n[0] != 'p' || !(n[1] >= '0' && n[1] <= '9') || !(n[2] >= '0' && n[2] <= '9') || !(n[3] >= '0' && n[3] <= '9')) continue;
            slots.push_back(&o);
            bx0 = std::min(bx0, int(o.x)); by0 = std::min(by0, int(o.y));
            bx1 = std::max(bx1, int(o.x + (o.width > 0 ? o.width : 32))); by1 = std::max(by1, int(o.y + (o.height > 0 ? o.height : 32)));
        }
        std::ranges::sort(slots, [](const formats::LayoutObject* a, const formats::LayoutObject* b) { return a->y != b->y ? a->y < b->y : a->x < b->x; });
        for (std::size_t s = 0; s < slots.size() && s < mg.lineup.size(); ++s) {
            const formats::LayoutObject* o = slots[s];
            st.texts += paintCentered(text_.get(), ui, std::format("#{}", mg.lineup[s].shirt), float(o->x) + (o->width > 0 ? float(o->width) : 32.f) * 0.5f, float(o->y) + (o->height > 0 ? float(o->height) : 32.f) * 0.5f, 30.f);
        }
        if (!slots.empty() && bx1 > bx0 && by1 > by0) { // PlayerState::pos -> grid dots (home side)
            for (std::size_t s = 0; s < mg.gx.size() && s < mg.lineup.size(); ++s) {
                if (mg.gx[s] == 999 || mg.gz[s] == 999) continue;
                const float px = float(bx0) + float(mg.gx[s] + 128) / 256.f * float(bx1 - bx0);
                const float py = float(by0) + float(mg.gz[s] + 128) / 256.f * float(by1 - by0);
                ui.draw(0, {px - 3.f, py - 3.f, 6.f, 6.f}, {}, render::packRgba(255u, 224u, 96u, 220u));
            }
        }
    } else if (kind_ == 9) { // substitutions: pnNN starters + pln/pls swap pair rows + card counts
        for (int i = 0; i < 9; ++i) rowCap(std::format("pn{:02d}", i), rowText(mg.lineup, i + 1), false, &mg.kit);
        std::vector<std::string> pln, pls; // pair rows in index order (plnN = starter off, plsN = sub on)
        for (const auto& o : sc_.objects) { const std::string n = lowerName(o.name); if (n.size() == 4 && n.starts_with("pl")) pln.push_back(n); }
        std::ranges::sort(pln);
        for (const auto& n : pln) {
            const int i = n[3] - '0';
            if (n[2] == 'n') rowCap(n, rowText(mg.lineup, 9 + i), mgSel_ == i, nullptr); // starter slot A/B
            else rowCap(n, rowText(mg.subs, i), mgSel_ == i, &mg.kit);                    // bench slot A/B
        }
        (void)pls;
        for (int i = 0; i < 2; ++i) { // ab00/ab01 swap controls: circles.fsh art stem, flat ring fallback
            if (const formats::LayoutObject* o = layObj(sc_, i == 0 ? "ab00" : "ab01"))
                ui.draw(0, {float(o->x), float(o->y), float(o->width > 0 ? o->width : 24), float(o->height > 0 ? o->height : 24)}, {}, render::packRgba(90u, 120u, 220u, mgSel_ == i ? 230u : 150u));
        }
        if (const formats::LayoutObject* o = layObj(sc_, "yc")) st.texts += paintCentered(text_.get(), ui, std::to_string(mg.yellows), float(o->x) + 8.f, float(o->y) + 6.f, 20.f); // yellow-red card icon counts (icons.fsh chain)
        if (const formats::LayoutObject* o = layObj(sc_, "rc")) st.texts += paintCentered(text_.get(), ui, std::to_string(mg.reds), float(o->x) + 8.f, float(o->y) + 6.f, 20.f);
    } else if (kind_ == 10) { // kick takers: prNN penalty order + ktNN kit picker rows + vit3 bar name
        for (int i = 0; i < 5; ++i) rowCap(std::format("pr{:02d}", i), rowText(mg.lineup, i + 1), mgSel_ == i, &mg.kit);
        // kt rows: home/away tricolour swatches (FEFLOW74 semantics; custom kits = .WCF state, open item)
        const formats::FcdbKit1998 away{};
        for (int i = 0; i < 5; ++i) { const formats::LayoutObject* o = layObj(sc_, std::format("kt{:02d}", i)); if (o) paintRowLabel(ui, *o, {}, *text_, 0, false, i == 0 ? &mg.kit : i == 1 ? &away : nullptr, st); }
        if (const formats::LayoutObject* o = layObj(sc_, "vit3")) st.texts += paintCentered(text_.get(), ui, mg.lineup.empty() ? std::string() : std::format("#{} {} {}", mg.lineup[0].shirt, mg.lineup[0].name, mg.lineup[0].staminaPct), float(o->x) + 84.f, float(o->y) + 7.f, 160.f);
    } else if (kind_ == 11 || kind_ == 12) { // attack bias / positioning sliders: s001 thumb + side captions
        const formats::LayoutObject* sl = layObj(sc_, "s001");
        if (sl) paintSliderThumb(ui, *sl, kind_ == 11 ? mg.attackBias : mg.attackBias * 8 / 100, 0, kind_ == 11 ? 100 : 8);
        const float cy = sl ? float(sl->y) - 16.f : 90.f;
        st.texts += paintCentered(text_.get(), ui, txt(573, "Defence"), sl ? float(sl->x) - 120.f : 180.f, cy, 110.f);   // FC bank 573 'Defence'
        st.texts += paintCentered(text_.get(), ui, txt(249, "Attack"), sl ? float(sl->x) + (sl->width > 0 ? float(sl->width) : 160.f) + 40.f : 560.f, cy, 90.f); // 249 'Attack'
        for (int i = 0; i < 5; ++i) rowCap(std::format("pr{:02d}", i), rowText(mg.lineup, i + 1), mgSel_ == i, &mg.kit);
    } else if (kind_ == 13) { // man marking: pnNN squad list rows + pair box caption
        for (int i = 0; i < 12; ++i) rowCap(std::format("pn{:02d}", i), rowText(mg.lineup, i), mgSel_ == i, &mg.kit);
        if (const formats::LayoutObject* o = layObj(sc_, "cmtx")) st.texts += paintCentered(text_.get(), ui, txt(1025, "Man Marking"), float(o->x) + float(o->width > 0 ? o->width : 120) * 0.5f, float(o->y) + 8.f, float(o->width > 0 ? o->width : 120));
    } else if (kind_ == 14) { // hub: b000..b005 page rows (FEFLOW89 slot-row grammar), cursor = mgSel_
        static constexpr std::array<const char*, 6> kHub{"b000", "b001", "b002", "b003", "b004", "b005"};
        static constexpr std::array<int, 6> kHubId{748, 1788, 1379, 250, 1025, 946}; // Formation / Substitutions / Positioning / Attack Bias / Man Marking / Kick Takers
        for (int i = 0; i < 6; ++i) {
            const formats::LayoutObject* o = layObj(sc_, kHub[std::size_t(i)]);
            if (!o) continue;
            const float w = o->width > 0 ? float(o->width) : 262.f, h = o->height > 0 ? float(o->height) : 25.f;
            if (mgSel_ == i) ui.draw(0, {float(o->x) - 3.f, float(o->y) - 2.f, w + 6.f, h + 4.f}, {}, render::packRgba(255u, 224u, 96u, 70u));
            st.texts += paintCentered(text_.get(), ui, txt(std::size_t(kHubId[std::size_t(i)]), "PAGE"), float(o->x) + w * 0.5f, float(o->y) + h * 0.5f, w);
        }
    } else if (kind_ == 15) { // MANMARKS ratings board (§E): pNNN squad grid rows + mm00..mm02 pair rows + sbar pan.
        const int nL = int(mg.lineup.size()), nS = int(mg.subs.size());
        const int nRows = std::min(nL + nS, 16);            // p000..p016 grid domain (LAY widget count)
        const int page = mgSel_ / 5;                        // ptop/pbot pan: five-row window per page
        for (int i = 0; i < 5 && page * 5 + i < nRows; ++i) {
            const int idx = page * 5 + i;                   // runtime row grid: widgets are x=0 y=0, engine positions at draw time [H]
            const IgMgmtRow& r = idx < nL ? mg.lineup[std::size_t(idx)] : mg.subs[std::size_t(idx - nL)];
            formats::LayoutObject row;                              // runtime row grid: widgets are x=0 y=0, engine positions at draw time [H]
            row.name = "p000"; row.x = 47; row.y = 132 + 16 * i; row.width = 300; row.height = 16;
            paintRowLabel(ui, row, std::format("#{} {}", r.shirt, r.name), *text_, 0, idx == mgSel_, &mg.kit, st); // hilite pane = cursor
        }
        for (int i = 0; i < 3; ++i) { // mmNN: home defender <-> away forward pairing (lineup tail pair = forwards [H])
            if (9 + i >= nL || i >= int(d.teams[1].lineup.size())) break;
            const IgMgmtRow& def = mg.lineup[std::size_t(9 + i)];
            const IgMgmtRow& fwd = d.teams[1].lineup[std::size_t(i)];
            if (const formats::LayoutObject* o = layObj(sc_, "mm0" + std::to_string(i)))
                st.texts += paintCentered(text_.get(), ui, std::format("{} <-> {}", def.name, fwd.name), float(o->x) + 150.f, float(o->y) + 9.f, 280.f);
        }
        if (const formats::LayoutObject* s = layObj(sc_, "sbar")) { // pan thumb over the row window
            paintSliderThumb(ui, *s, page, 0, std::max(1, (nRows + 4) / 5) - 1);
            ++st.shapes;
        }
    }
    return st;
}


// IGMENU row domain [C] pass-26c: table bytes read BYTE-EXACT from the data segment (delta
// VA=raw+0x3FEA00; raw 0x17add8..0x17af20) — the earlier "BSS-only/runtime-writer" negative was
// a wrong-delta (0x400000) read: the slots ARE initialized file data. Layout: u16 count at the
// base VA stored by init 0x43f040, then count rows of {u16 FC textid, u16 dispatch code} (stride
// 4; tick 0x43fc40 copy loop into the 'menu' window slots +0x3c/+0x40/+0x44). Init selects by
// game-mode class DAT_00640fb4 (byte-exact disasm 0x43f1f0-0x43f2b1): mode 0 or 9 (the
// exhibition/friendly class — its 1p branch 0x43f27c is the ONLY path into the tied-at-full-time
// draw table) -> 1p 0x57c7d8 10-row / 2p (DAT_00641274 bit 0x10) 0x57c802 8-row; all other mode
// classes (cup qualifiers, range check [2,8] at the Team-Management dispatch 0x43f669) -> 1p
// 0x57c832 11-row / 2p 0x57c860 9-row; mode 0xa PK phases -> 0x57c886 (4-row codes 16..19),
// 0x57c898 (9-row), 0x57c8be (8-row); mode 0xc -> 0x57c8e0 (8-row), 0x57c902 (6-row).
// The port's live fixture is the single-player EXHIBITION bootstrap (MatchBoot), i.e. retail
// mode class 0/9 -> armed below is the friendly 1p table 0x57c7d8 dump (raw 0x17add8, count
// 0x0a, byte-exact): rows 0-8 as the cup table, then 264/10 + 4337/12 WITHOUT the cup-only
// 267 "Quit World Cup":
//   {5083 Resume Match, 249 Instant Replay, 4423 Camera Select, 1407 Team Management,
//    258 Controller Select, 5 Options, 261 Match Statistics, 268 Restart Match, 264 Quit Match,
//    4337 Quit to Windows}, codes {0,1,2,3,4,5,6,9,10,12}; captions verified through the FC
//   remap DAT_005774a4 against FCENG.BIN. Codes match the 0x43f548 dispatch targets. The cup
//   tables (mode class [2,8]) are 0x57c832 11-row (adds 267/11 between 264/10 and 4337/12) /
//   2p 0x57c860 9-row; the friendly 2p table 0x57c802 (raw 0x17ae02, count 0x08) =
//   {277 Continue, 249/1, 261/6, 262 Score Summary/7, 281/8, 4344 Restart/9, 264/10, 4337/12}
//   (port is always 1p: keyboard controller, DAT_00641274 bit 0x10 never set). Tied draw table
//   0x57c824 = {3975 Extra-Time/22, 1536 Penalty Shootout/21, 3974 End Match/20} (port: the
//   full-time whistle opens the IGSCSUM/IGFLSUM chain instead of pausing on that table).
struct IgMenuRow { std::uint16_t tid; std::uint16_t code; };
static constexpr std::array<IgMenuRow, 10> kIgMenuRows{
    {{5083, 0}, {249, 1}, {4423, 2}, {1407, 3}, {258, 4}, {5, 5}, {261, 6}, {268, 9}, {264, 10}, {4337, 12}}};

int runLiveLoop(const fs::path& tree, const std::string& prefix, int smokeFrames,
                platform::Window& window, render::Renderer& renderer, bool recordReplay, bool igPauseSmoke, int boardScript,
                int quality, audio::Mixer* externalMixer, MenuMusicDirector* menuMusic) {
    MatchBoot boot;
    if (!boot.init(tree)) return 2;

    presentation::MatchScene scene(renderer, *boot.data, [&] {
        MatchSetup setup{};
        setup.teamIndex = boot.teamIndex;
        setup.pitchSize = std::uint8_t(boot.opt.pitchSize);
        setup.surfaceColumn = 0; // Options::surfaceColumn default; TURFS0P0.PAK (measured members)
        return setup;
    }());

    platform::initGamepads(); // open pads (SDL gamepad events feed input::Input; non-fatal if none)
    const config::LoadResult cfgFile = config::load(config::defaultPath());
    for (const auto& warn : cfgFile.warnings) std::cout << "settings: " << warn << '\n';

    // HUD layout calibration: settings.ini [gameplay] hud_* passthrough keys (HUD_WC98.md).
    HudGeometry geo;
    const auto knob = [&](std::string_view k, float& dst) {
        for (const auto& e : cfgFile.settings.gameplay)
            if (e.key == k) { try { dst = std::stof(std::string(e.value)); } catch (...) {} }
    };
    knob("hud_sb_x", geo.sbX); knob("hud_sb_y", geo.sbY); knob("hud_sb_h", geo.sbH);
    knob("hud_gap", geo.gap); knob("hud_clock_adv", geo.clockAdvance); knob("hud_base_w", geo.baseWidth);
    knob("hud_min_chip", geo.minChipMinutes); // minute countdown chip value (HUD_WC98.md pass-39)
    knob("hud_radar_x", geo.radarX); knob("hud_radar_y", geo.radarY); knob("hud_radar_w", geo.radarW); knob("hud_radar_h", geo.radarH);
    knob("hud_dot_half", geo.dotHalf); knob("hud_shirt_head_px", geo.shirtHeadPx); knob("hud_shirt_size", geo.shirtSize);
    scene.setHudGeometry(geo);

    input::Input in(cfgFile.settings);
    in.assign(1, input::Side::Home); // keyboard device id 1 is always present (test_input.cpp l.43)

    // Native replay recording (--record-replay): every controller-0 tick streams into the recorder;
    // sealed with the final world state hash on loop exit.
    ReplayRecorder rec;
    if (boardScript == 2 || boardScript == 3) boot.opt.halfLengthMinutes = 1; // IGHALF/IGSCSUM gates: fast-forward to half/full time (the highlights gates keep a running match)
    std::uint32_t recTick = 0;
    sim::SimRunner runner(boot.md, boot.opt, 0x2E7A);
    auto& w = runner.world();
    // Bind controller 0 (keyboard device 1) to the kickoff-taker side (Match_run Input_assignController
    // 0x4554C0): unbound controllers never strike center restarts (the demo slice lesson).
    sim::Input_assignController(w, 0, 1, 0);

    MatchCamera camera; // default Tele: follows play behind the ball
    camera.setQualityTier(quality); // h23 DAT_00644778 scene-level tier word [P]
    int controlledPid = -1;

    // Commentary: mixer is best-effort — headless CI has no OpenAL device, and the adapter then
    // runs decode-only (queue advances on silent completion). The FE shell passes its own context
    // (externalMixer): retail keeps one SND device across the scene seam, so a match launched from
    // the FE must join the FE mix instead of opening a second device.
    std::unique_ptr<audio::Mixer> mixer;
    if (!externalMixer) {
        try { mixer = std::make_unique<audio::Mixer>(); } catch (const std::exception& e) { std::cout << "commentary: no audio device (" << e.what() << ")\n"; }
    }
    audio::Mixer* audioMixer = externalMixer ? externalMixer : mixer.get();
    app::Commentary commentary(audioMixer);
    const bool haveSpeech = commentary.loadTree(tree);
    if (!haveSpeech) std::cout << "commentary: AUDIO/PHRS/PHRS.PBK unavailable, queue idle\n";
    std::size_t evPos = 0; // World::events is append-only (world.hpp emit)
    int speechPctApplied = app::Commentary::kSceneSpeechPct; // percent table idx3 (SNDspeechautovol)
    // Runtime SFX (docs/reverse-engineering/AUDIO_RUNTIME.md): crowd ambience loop for the whole
    // match + whistle/kick one-shots off the same event seam, independent of speech availability.
    app::MatchAudio matchAudio(tree);
    matchAudio.setMixer(audioMixer);
    // Match-lane boot seam (pass-51): percent notifier + stadium lane pair (crowd bed +
    // traffic loop) + home-team anthem sample + team chant/announcer lane catalogs, keyed by
    // the FCDB team ids the match actually plays with (same GameData as the sim sheets).
    const std::uint16_t laneHomeId = boot.md.sheets[0].team.id;
    const std::uint16_t laneAwayId = boot.md.sheets[1].team.id;
    matchAudio.boot(laneHomeId, laneAwayId,
                    boot.md.sheets[0].team.roster, boot.md.sheets[1].team.roster);
    std::size_t sfxPos = 0; // SFX sweep cursor (append-only events stream)
    std::uint32_t audioTicks = 0; // DAT_00636a14 mirror: +2 per 20 ms engine step (10 ms/tick)

    auto key = [&](SDL_Scancode sc, bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.scancode = sc;
        e.key.down = down;
        SDL_PushEvent(&e);
    };
    // Live UI pointer lane (pass-27igmouse): MAUS.PAK cursor art + pointer state for the pause
    // menu and the IGQUESHN confirm box. Retail window-object semantics [H]: hover on a menu row
    // moves the cursor, a press activates the row / the confirm box 'okay'/'canc' buttons.
    presentation::FeCursor mouseArt;
    render::TextureId mouseTex = 0;
    if (presentation::loadFeCursor(tree, mouseArt))
        mouseTex = renderer.createTexture(mouseArt.bmp, render::Wrap::Clamp, "live/maus-curs");
    float ptrX = -1.f, ptrY = -1.f;
    bool lmb = false, prevLmb = false;
    bool igMouseShot = false;
    auto motion = [&](float x, float y) { // script coords are virtual 640x480 -> window point space
        float wx = 0.f, wy = 0.f;
        const auto [pw, ptH] = window.pointSize();
        presentation::uiWindowFromPointer(int(x), int(y), pw, ptH, wx, wy);
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.x = wx;
        e.motion.y = wy;
        SDL_PushEvent(&e);
    };
    auto click = [&](float x, float y, bool down) {
        float wx = 0.f, wy = 0.f;
        const auto [pw, ptH] = window.pointSize();
        presentation::uiWindowFromPointer(int(x), int(y), pw, ptH, wx, wy);
        SDL_Event e{};
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = down;
        e.button.x = wx;
        e.button.y = wy;
        SDL_PushEvent(&e);
    };

    constexpr double kStepMs = 20.0; // 1/50 s wall-clock sim step (engine frame cadence)
    std::uint64_t last = SDL_GetTicks();
    double acc = 0.0;
    int presented = 0, kickIn = 0, kickDur = 0, forceIn = 0, prevPhase = -1;
    bool quit = false, sawPassBit = false;
    const double startClock = elapsedSecFloat(w); // DAT_0065F1C8 via fixed->seconds helper
    // In-game pause menu (MENU_PARITY.md §4 rank 8): FE/INGAME/IGMENU.LAY composited over the live
    // frame on Esc. The original draws its 11 runtime item captions from FC-bank indices the scene
    // handler sets at init (0x4401A4, tags 'gmnc'/'okay'); that caption source is undecoded, so the
    // port's menu domain is {Resume, Quit} (list rows 0..1) and Enter on the quit row opens the
    // IGQUESHN.LAY "are you sure" confirm box ('quit'-tag path). Absent layouts keep Esc = direct exit.
    std::optional<formats::ScreenLayout> igMenu, igQues;
    bool haveIgm = false, haveIq = false;
    try {
        std::filesystem::path igDir;
        const std::string t = presentation::readLayout((tree / "FE/INGAME/IGMENU.LAY").string(), igDir);
        if (!t.empty()) { igMenu = formats::ScreenLayout::build("IGMENU", formats::LayoutDocument::parse(std::string_view{t}), nullptr, nullptr, "PC"); haveIgm = true; }
    } catch (const std::exception&) {}
    try {
        std::filesystem::path igDir;
        const std::string t = presentation::readLayout((tree / "FE/INGAME/IGQUESHN.LAY").string(), igDir);
        if (!t.empty()) { igQues = formats::ScreenLayout::build("IGQUESHN", formats::LayoutDocument::parse(std::string_view{t}), nullptr, nullptr, "PC"); haveIq = true; }
    } catch (const std::exception&) {}
    presentation::ScreenArt igArt(renderer, tree, tree / "FE/INGAME");
    auto igText = std::make_unique<presentation::FeText>(renderer, tree); // FC*.BIN captions for the LAY textid slots
    enum class IgPause { Off, Menu, QuitAsk, RestartAsk, Board }; // + IG presentation boards (IG_FAMILIES_SPEC.md §C)
    IgPause pause = IgPause::Off;
    int igSel = 0;                       // menu cursor into kIgMenuRows (pass-26 decoded row table)
    bool igReported = false, iqReported = false, iqShot = false;
    std::int64_t prevSim = int(w.time.simFrames); // camera delta source (DAT_0065F1E0 frames/tick)
    // IG presentation boards: composite over the live/paused frame from last-match data. igstats opens
    // from the IGMENU Stats row, ighalf at half-time whistle, full time = igscsum then igflsum [C]; Esc
    // dismisses/resumes (payload grammar ESC=0x65), L/R moves pages ('igbC' btn1..btn4 FUN_0043a3f4).
    std::unique_ptr<IgBoards> boards;
    int boardSrc = 0;                    // open source: 1 stats menu, 2 half-time, 3 full-time chain, 4 highlights browser (menu), 5 save list (browser 'save' button), 6 IGTEAM management pages (§E), 11 controller-select board (pass-26)
    bool boardReported[26] = {}, halfShown = false, ftShown = false;
    int igTabShot = -1, igLastTab = -1, igLastSig = -1; // IGOPTN2 per-tab/per-cycle evidence (pass-21igscn)
    bool igCycShot = false;
    int boardShotAt = -1;                // capture frame of the current board (gate auto-script pacing)
    // pass-28 audit lane: background-only shot one frame before the first pause key is drained.
    // The sim freezes at that key frame, so _pre.png holds the exact live background every later
    // board capture composes over -> region diffs are board-only. Fixed-open lanes only (2/3 boards
    // trigger on half/full time, background unpinned).
    const int preShotAt = igPauseSmoke ? 99
                            : (boardScript >= 1 && boardScript <= 10 && boardScript != 2 && boardScript != 3) ? 199 : -1;
    static constexpr std::array<const char*, 26> kBoardName{"", "IGSTATS", "IGHALF", "IGFLSUM", "IGSCSUM", "IGHILITE", "IGHILSAV", "IGHILIT1", "IGTEAM72", "IGTEAM73", "IGTEAM74", "IGTEAM75", "IGTEAM79", "IGTEAM80", "IGTEAM89", "MANMARKS", "IGCAMERA", "IGCAMSEL", "IGPRACT1", "IGPRACT2", "IGPRAC1", "IGOPTN2", "IGOPTNS-A", "IGOPTNS-G", "IGOPTNS-M", "CONTROL1"}; // §G camera popups + practise pages + IGOPTN2 board + legacy trio popups (pass-21igscn) + CONTROL1 controller-select board (pass-26)
    int initPeriod = w.match.periodIndex; // DAT_0065F224: 0 -> >=1 transition marks the half whistle (rebuilt on Restart Match)
    std::array<std::int64_t, 2> possAcc{};      // per-team possessionFrames deltas (DAT_0065f264 + owning team DAT_0065f250)
    std::int32_t lastPoss = w.time.possessionFrames;
    const bool wantHalfBoard = boardScript == 2 || (boardScript == 0 && !smokeFrames);   // interactive play / fe_ighalf gate
    const bool wantFullBoards = boardScript >= 3 || (boardScript == 0 && !smokeFrames);  // interactive play / fe_igscsum + fe_igflsum gates
    auto buildIgData = [&]() {
        IgMatchData d;
        d.names = boot.names;            // FCDB STR0 fullName on the typ1/typ2 slots
        char ff[8];
        std::snprintf(ff, sizeof ff, "F%03u", unsigned(boot.teamIndex[0])); d.flags[0] = ff; // FNNN record-index shields (FEFLOW11 rule)
        std::snprintf(ff, sizeof ff, "F%03u", unsigned(boot.teamIndex[1])); d.flags[1] = ff;
        d.score = {w.match.score[0], w.match.score[1]}; // MatchState::score DAT_0065E050
        for (int t = 0; t < 2; ++t) {
            const auto& s = w.match.stats[std::size_t(t)]; // TeamStats block DAT_0065E1FC.. (rules.hpp fields)
            d.stats[t] = {s.corners, s.offsides, s.fouls, s.cards, s.goals, s.shotsWide};
        }
        const std::int64_t sum = possAcc[0] + possAcc[1];  // Possession row: accumulator over possessionFrames ticks
        if (sum > 0) {
            int p = int((possAcc[0] * 100 + sum / 2) / sum);
            if (p < 1) p = 1;
            if (p > 99) p = 99;
            d.possessionPct = {p, 100 - p};
        }
        std::array<int, 2> sc{0, 0};     // running score column from goalLog order (DAT_0065E058 count)
        const int n = std::min(w.match.goalCount, int(std::size(w.match.goalLog)));
        for (int i = 0; i < n; ++i) {    // DAT_0065E06C: byte0 flags (bit0 team, bit1 og, bit2 pen), byte1 shirt, u16 time seconds
            const std::uint32_t e = w.match.goalLog[std::size_t(i)];
            if (!e) continue;
            IgGoalRow g;
            const int fl = int(e & 0xFFu);
            g.team = fl & 1;
            g.ownGoal = (fl & 2) != 0;
            g.penalty = (fl & 4) != 0;
            g.shirtNo = int((e >> 8) & 0xFFu);
            const int timeSec = int((e >> 16) & 0xFFFFu);
            g.minute = timeSec / 60;
            g.second = timeSec % 60;
            if (g.ownGoal) sc[std::size_t(1 - g.team)] += 1;
            else sc[std::size_t(g.team)] += 1;
            g.scoreAfter = sc;
            d.goals.push_back(g);
        }
        return d;
    };
    auto openBoard = [&](std::string_view nm, int src) {
        if (!boards) boards = std::make_unique<IgBoards>(renderer, tree); // lazy: layouts + FC bank load once
        boardSrc = src;
        boardShotAt = -1;
        pause = IgPause::Board;
        if (!boards->open(nm)) { // layout absent on this tree: fall back to the caller semantics
            std::cerr << "live: igboard open failed: " << nm << '\n';
            if (src == 1 || src == 6 || src == 8 || src == 9 || src == 10) pause = IgPause::Menu; // IGSTATS / management pages / camera & practise popups / IGOPTN2 trio: layout absent -> back to the menu
            else if (src == 3) quit = true;
            else pause = IgPause::Off;
        }
    };
    auto dismissBoard = [&]() {
        switch (boardSrc) {
            case 1: pause = IgPause::Menu; break; // IGSTATS from the menu: return to IGMENU
            case 2: pause = IgPause::Off; break;  // half-time board: Esc resumes the match [C] ESC=0x65 resume callback
            case 4: pause = IgPause::Menu; break; // highlights browser -> IGMENU (Esc grammar)
            case 5: openBoard("ighilite", 4); break; // save list -> browser (save button opened it from there)
            case 6: pause = IgPause::Menu; break; // IGTEAM page/hub -> IGMENU (Esc grammar, §E)
            case 8: if (boards && boards->kind() == 17) openBoard("igcamera", 8); else pause = IgPause::Menu; break; // igcamsel -> igcamera ('menu' nav), igcamera -> IGMENU (§G)
            case 9: pause = IgPause::Menu; break; // practise pages -> the menu chain (§G)
            case 10: pause = IgPause::Menu; break; // IGOPTN2 popup -> IGMENU (Esc grammar, pass-21)
            case 11: pause = IgPause::Menu; break; // CONTROL1 controller-select board -> IGMENU (pass-26)
            default:                              // full-time chain: IGSCSUM -> IGFLSUM page, then exit
                if (boards && boards->kind() == 4) openBoard("igflsum", 3);
                else quit = true;
                break;
        }
    };
    // ---- Highlights lane (§D): rows from the rply index, deterministic gate fixtures --------------
    // Interactive play reads/writes tree/user (the exe's user\rply%04d.dat path); the gates run on a
    // scratch directory seeded with three scripted mini-replays (fixed seed + fixed pad stream: pure
    // sim -> byte-stable state hashes, so the replayFile re-drive reproduces them exactly).
    const fs::path hlUser = smokeFrames ? fs::path(prefix + "-user") : tree / "user";
    auto hlRows = [&] {
        std::vector<IgHlRow> rows;
        try {
            for (const auto& e : formats::parseRplyIndex(wc98::readFile(hlUser / "rplyindx.dat")))
                if (!e.name.empty() && fs::exists(hlUser / e.name)) rows.push_back({int(e.number), e.name, true});
        } catch (const std::exception&) {} // no index yet: browser shows the empty-slot readout
        return rows;
    };
    // Disable mask [C] tick 0x43fc40 fill loop: code 1 (Instant Replay) rows are disabled when the
    // replay list is empty (FUN_00494fc0 <= 0 -> mask bit at +0x44); code 9 (Restart Match) is
    // masked only outside the normal match phase (FUN_0043b140 vs FUN_004336f4) — the port runs a
    // normal phase, so only the replay rule applies here. Cursor moves skip masked rows.
    const auto igDisabled = [&](int row) { return kIgMenuRows[std::size_t(row)].code == 1 && hlRows().empty(); };
    const auto igStep = [&](int from, int dir) {
        int r = from;
        for (int i = 0; i < int(kIgMenuRows.size()); ++i) {
            r = (r + dir + int(kIgMenuRows.size())) % int(kIgMenuRows.size());
            if (!igDisabled(r)) return r;
        }
        return from;
    };
    auto seedHlFixtures = [&] { // gate mode only: clear + rebuild slots 1..3 deterministically
        std::error_code ec;
        fs::remove_all(hlUser, ec);
        fs::create_directories(hlUser, ec);
        for (int slot = 0; slot < 3; ++slot) {
            sim::SimRunner fr(boot.md, boot.opt, 0x2E7A); // the live loop's seed: re-drive parity
            sim::Input_assignController(fr.world(), 0, 1, 0);
            ReplayRecorder r;
            const int ticks = 90 * (slot + 1);
            for (int t = 0; t < ticks; ++t) {
                sim::ControllerState c{}; // scripted deterministic pad stream
                if ((t % 45) < 6) c.padBits |= input::actionBit(config::Action::Pass);
                fr.setController(0, c); // feed the recorded word (the live loop's path): replay re-drive reproduces these runs exactly
                r.append(unsigned(t), c);
                fr.tick();
            }
            recordToUserDir(hlUser, r.build(0x2E7A, sim::stateHash(fr.world()))); // existing save path
        }
    };
    if (smokeFrames && boardScript >= 4) seedHlFixtures();
    auto playSelected = [&] { // browser 'play': app/replay.cpp re-drive of the selected slot file
        const std::vector<IgHlRow> rows = hlRows();
        const int s = boards ? boards->hlSel() : 0;
        if (s < 0 || std::size_t(s) >= rows.size()) return;
        const std::string nm = rows[std::size_t(s)].name;
        try {
            const ReplayResult rr = replayFile(tree, hlUser / fs::path(nm));
            std::cout << "rply-play: " << nm << " ticks=" << rr.ticksPlayed << (rr.matches ? " match=1\n" : " match=0\n");
        } catch (const std::exception& e) { std::cout << "rply-play: " << nm << " FAILED (" << e.what() << ")\n"; }
    };
    auto saveHighlights = [&] { // 'save' button: rply%04d.dat + index update via recordToUserDir
        std::error_code ec;
        fs::create_directories(hlUser, ec);
        const int slot = recordToUserDir(hlUser, rec.build(0x2E7A, sim::stateHash(w)));
        std::cout << "hilsav: saved rply" << std::setw(4) << std::setfill('0') << slot << ".dat slot=" << slot << '\n';
    };
    // ---- IGTEAM management pages (§E): FCDB squad rows + STR1 names, stamina/cards from the live
    // PlayerAttr records. Built on page entry while the sim is frozen; sub swaps mutate IgMgmtData in
    // place (the board persists across frames like the highlights lane).
    IgMgmtData mgmt;
    // §G camera popups + practise pages (kinds 16-20): camd.curRow is the IGCAMSEL grid cursor,
    // boards->mgSel_ the IGCAMERA option / practise keeper row; prac = deterministic gate fixture.
    IgCamData camd;
    IgPracData prac{2, true, true}; // 2 players (FC#1249 stepper), keepers On (FC#1920/#374 rows)
    auto buildIgMgmt = [&] {
        IgMgmtData d;
        // Names come from the boot GameData: same FCDB.DBI + FCDBP<LANG>.DBI pair the match
        // lineup and attributes were built from (STR1 rows are parallel to the player pool).
        for (int t = 0; t < 2; ++t) {
            IgTeamMgmt& tm = d.teams[std::size_t(t)];
            const auto& sheet = boot.md.sheets[std::size_t(t)];
            tm.teamName = boot.names[std::size_t(t)];
            char ff[8]; std::snprintf(ff, sizeof ff, "F%03u", unsigned(boot.teamIndex[std::size_t(t)])); tm.flagFile = ff; // FNNN record-index shields (FEFLOW11 rule)
            tm.kit = sheet.team.home;
            for (int i = 0; std::size_t(i) < sheet.squad.size(); ++i) {
                const auto& p = sheet.squad[std::size_t(i)];
                IgMgmtRow r;
                r.shirt = p.shirtNumber;
                const auto it = std::ranges::find(boot.game.db.players, p.id, &formats::FcdbPlayer::id);
                if (it != boot.game.db.players.end()) r.name = boot.game.playerName(std::size_t(std::distance(boot.game.db.players.begin(), it)));
                for (int pid = 0; pid < sim::kPlayers; ++pid) { // live stamina + formation dot + card flags via PlayerState.attr -> World::attrs
                    const auto& ps = w.players[std::size_t(pid)];
                    if (int(ps.team) != t || int(ps.dbPlayer) != i) continue;
                    const auto& at = w.attrs[std::size_t(ps.attr)];
                    r.staminaPct = std::clamp(int(at.stamina18), 0, 100); // u16 copy of DB +0x1B, lowered by fatigue (floor 4)
                    if (at.at(0x40) & 2) ++tm.yellows;                    // +0x40 status: bit 2 booked
                    if ((at.at(0x40) & (8 | 0x20)) != 0) ++tm.reds;       // 8|0x20 sent off
                    tm.gx[std::size_t(i)] = std::clamp(int(fx(ps.pos.x) * 256.f / float(w.pitch.halfWidth) - 128.f), -128, 127);
                    tm.gz[std::size_t(i)] = std::clamp(int(fx(ps.pos.z) * 256.f / float(w.pitch.halfLength) - 128.f), -128, 127);
                }
                if (i < 11) tm.lineup.push_back(r); else tm.subs.push_back(r); // first 11 start (roster order)
            }
            int sum = 0;
            for (const auto& r : tm.lineup) sum += r.staminaPct;
            if (!tm.lineup.empty()) tm.fitPct = sum / int(tm.lineup.size());
        }
        return d;
    };

    // Enter dispatch on the ROW CODE, not the index [C] 0x43f548 jump table — shared by the key
    // lane and the mouse press lane (pass-27igmouse).
    const auto menuActivate = [&] {
        switch (kIgMenuRows[std::size_t(igSel)].code) {
            case 0: pause = IgPause::Off; break;                            // Resume Match: resume the match loop
            case 1: if (!hlRows().empty()) openBoard("ighilite", 4); break; // Instant Replay -> browser (§D); masked row never reaches here
            case 2: openBoard("igcamera", 8); break;                        // Camera Select (§G, row code 2 on the armed 1p table)
            case 3: mgmt = buildIgMgmt(); openBoard("igteam72", 6); break;  // Team Management -> IGTEAM mirror pages (§E)
            case 4: openBoard("control1", 11); break;                       // Controller Select board (pass-26)
            case 5: std::printf("live: IGOPTN2 board from the Options row\n"); openBoard("igoptn2", 10); break; // Options (pass-21igscn)
            case 6: openBoard("igstats", 1); break;                         // Match Statistics board
            case 9: pause = haveIq ? IgPause::RestartAsk : IgPause::Off; break; // Restart Match (tid 268): confirm box, Enter restarts
            case 10: if (haveIq) pause = IgPause::QuitAsk; else quit = true; break; // Quit Match: IGQUESHN box [C]
            case 11: quit = true; break; // Quit World Cup (cup tables only): abandon the cup run -> exits to FE hub
            case 12: quit = true; break;                                   // Quit to Windows: direct exit (0x43f72d chain)
            default: pause = IgPause::Off; break;                          // codes outside the armed table cannot dispatch
        }
    };
    int restarts = 0; // gate counter (pass-118timing restart lane): confirmYes executions
    const auto confirmYes = [&] { // confirm 'yes': rebuild the world on the same seed, reopen kickoff [C] restart chain installs 0x440706
        runner = sim::SimRunner(boot.md, boot.opt, 0x2E7A); // move-assign: `w` still binds runner.w_
        sim::Input_assignController(w, 0, 1, 0);
        evPos = 0; rec = ReplayRecorder{}; prevSim = int(w.time.simFrames);
        ++restarts;
        sfxPos = 0; // the rebuilt world restarts its event stream: the SFX cursor must follow
                    // (the exe restart chain 0x440706 reruns kickoff with the scene handlers
                    // installed; audio lane state like the chant pool survives, not replays)
        lastPoss = w.time.possessionFrames; possAcc = {}; initPeriod = w.match.periodIndex;
        halfShown = false; ftShown = false;
        std::printf("igmenu: match restarted\n");
        pause = IgPause::Off;
    };

    { // Snap the camera to its tele target once (demo passes a huge frame count per capture; live
      // smoothing climbs eye.y only +/-frames units per tick, so starting at 0 crawls for seconds).
        MatchSnapshot init = snapshotFromWorld(w, boot.md);
        init.hud.teamNames = boot.abbrv;
        camera.tick(init, 600); // enough to converge eye.y/FOV glide; huge values overflow q*frames
    }
    while (!quit) {
        // Gate input synthesis: a real keydown/keyup pair through the SDL queue -> Input layer ->
        // pad word -> controller 0 (proves the whole input->sim command path in --live-match-smoke).
        if (smokeFrames && presented == 20) key(SDL_SCANCODE_S, true);
        if (smokeFrames && presented == 34) key(SDL_SCANCODE_S, false);
        if (smokeFrames && presented == 2) motion(600.f, 50.f); // park pointer top-right, above the menu row band (scale-independent hover safety, pass-32)
        if (smokeFrames && !igPauseSmoke && presented > 2)
            motion(600.f, 50.f); // keyboard-only smoke lanes: the retail hover rule pins the cursor to the hovered row, so re-park every frame (pass-32: OS motion events land inside row 0)
        if (igPauseSmoke && presented == 100) key(SDL_SCANCODE_ESCAPE, true); // gate script: Esc opens IGMENU (after the forced kickoff kick ~frame 90)
        if (igPauseSmoke && presented == 105) motion(250.f, 273.5f); // pass-27: hover the Match Statistics row (row 6 of the armed 10-row friendly 1p table 0x57c7d8; winY = 131+(231-180)/2 = 156.5, centre 156.5+18*6+9 = 273.5) -> cursor pane follows
        if (igPauseSmoke && presented == 113) igMouseShot = true;    // _igmenu_mouse.png: cursor sprite + pane on row 6
        if (igPauseSmoke && presented == 114) click(250.f, 273.5f, true);  // press activates row 6 -> IGSTATS board (code lane shared with Enter)
        if (igPauseSmoke && presented == 116) click(250.f, 273.5f, false);
        if (igPauseSmoke && presented == 117) motion(600.f, 50.f);  // park pointer above the menu band (hover-safe)
        if (igPauseSmoke && presented == 118) key(SDL_SCANCODE_ESCAPE, true); // IGSTATS dismiss -> IGMENU (src 1 returns to the menu [C])
        if (igPauseSmoke && presented == 120) { key(SDL_SCANCODE_L, true); key(SDL_SCANCODE_L, false); } // Controller Select board: CONTROL1 evidence line + _control1.png (pass-26)
        if (igPauseSmoke && presented == 130) key(SDL_SCANCODE_ESCAPE, true); // dismiss CONTROL1 -> IGMENU (src 11)
        if (igPauseSmoke && presented == 131) { key(SDL_SCANCODE_UP, true); key(SDL_SCANCODE_UP, false); } // row 6 (Stats) -> row 5 Options
        // cursor walk Options -> Quit Match over the decoded 10-row 1p table (Instant Replay masked:
        // no replay rows in this lane); the Up-back walk afterwards lands on row 0 (Resume Match),
        // skipping the masked row 1 in every mask case.
        if (igPauseSmoke && presented == 134) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); } // 5 -> 6 Stats
        if (igPauseSmoke && presented == 136) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); } // 6 -> 7 Restart Match
        if (igPauseSmoke && presented == 138) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); } // 7 -> 8 Quit Match
        if (igPauseSmoke && presented == 145) key(SDL_SCANCODE_RETURN, true); // Enter on the Quit Match row -> IGQUESHN confirm (_igquesn.png)
        if (igPauseSmoke && presented == 146) key(SDL_SCANCODE_RETURN, false);
        if (igPauseSmoke && presented == 155) key(SDL_SCANCODE_ESCAPE, true); // Esc: confirm cancels back to the menu
        if (igPauseSmoke && presented == 160) { key(SDL_SCANCODE_UP, true); key(SDL_SCANCODE_UP, false); }
        if (igPauseSmoke && presented == 162) { key(SDL_SCANCODE_UP, true); key(SDL_SCANCODE_UP, false); }
        if (igPauseSmoke && presented == 164) { key(SDL_SCANCODE_UP, true); key(SDL_SCANCODE_UP, false); }
        if (igPauseSmoke && presented == 166) { key(SDL_SCANCODE_UP, true); key(SDL_SCANCODE_UP, false); }
        if (igPauseSmoke && presented == 168) { key(SDL_SCANCODE_UP, true); key(SDL_SCANCODE_UP, false); }
        if (igPauseSmoke && presented == 170) { key(SDL_SCANCODE_UP, true); key(SDL_SCANCODE_UP, false); }
        if (igPauseSmoke && presented == 172) { key(SDL_SCANCODE_UP, true); key(SDL_SCANCODE_UP, false); } // 2 -> masked Replay -> 0 Resume Match
        if (igPauseSmoke && presented == 178) key(SDL_SCANCODE_RETURN, true); // Enter on Resume Match: resume
        if (igPauseSmoke && presented == 179) key(SDL_SCANCODE_RETURN, false);
        if (igPauseSmoke && presented == 180) key(SDL_SCANCODE_ESCAPE, true); // reopen the menu, then close it: match resumes for the remaining frames
        if (igPauseSmoke && presented == 190) key(SDL_SCANCODE_ESCAPE, true); // Esc closes the menu: match resumes
        // pass-118timing restart lane (gate): reopen the menu, walk the cursor to the Restart
        // Match row (7), Enter activates -> RestartAsk, Enter 'yes' -> confirmYes rebuilds the
        // world; the remaining frames must re-fire the kickoff whistle from the reset cursor.
        if (igPauseSmoke && presented == 200) key(SDL_SCANCODE_ESCAPE, true); // reopen IGMENU (cursor 0)
        if (igPauseSmoke && presented == 204) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); } // 0 -> 2 (masked Replay row skipped by igStep)
        if (igPauseSmoke && presented == 206) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); } // 2 -> 3 Team
        if (igPauseSmoke && presented == 208) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); } // 3 -> 4 Controller
        if (igPauseSmoke && presented == 210) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); } // 4 -> 5 Options
        if (igPauseSmoke && presented == 212) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); } // 5 -> 6 Stats
        if (igPauseSmoke && presented == 214) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); } // 6 Stats -> 7 Restart Match
        if (igPauseSmoke && presented == 220) key(SDL_SCANCODE_RETURN, true); // Enter on Restart row -> RestartAsk confirm
        if (igPauseSmoke && presented == 221) key(SDL_SCANCODE_RETURN, false);
        if (igPauseSmoke && presented == 225) key(SDL_SCANCODE_RETURN, true); // Enter 'yes' -> confirmYes: world rebuilt
        if (igPauseSmoke && presented == 226) key(SDL_SCANCODE_RETURN, false);
        // IG board gate scripts. Mode 1 = stats tour through IGMENU (Esc opens, Down -> Stats row, Enter
        // opens the IGSTATS composite, Esc closes to menu, Esc resumes). Modes 2/3 auto-dismiss/auto-advance
        // N frames after each board capture (boardShotAt pacing below).
        if (boardScript == 1) {
            if (presented == 200) key(SDL_SCANCODE_ESCAPE, true);   // IGMENU opens after kickoff progress
            if (presented == 215) key(SDL_SCANCODE_DOWN, true);     // cursor walk to the Match Statistics row (row 6: Replay row masked, 5 steps from row 0)
            if (presented == 216) key(SDL_SCANCODE_DOWN, false);
            if (presented == 217) key(SDL_SCANCODE_DOWN, true);
            if (presented == 218) key(SDL_SCANCODE_DOWN, false);
            if (presented == 219) key(SDL_SCANCODE_DOWN, true);
            if (presented == 220) key(SDL_SCANCODE_DOWN, false);
            if (presented == 221) key(SDL_SCANCODE_DOWN, true);
            if (presented == 222) key(SDL_SCANCODE_DOWN, false);
            if (presented == 223) key(SDL_SCANCODE_DOWN, true);
            if (presented == 224) key(SDL_SCANCODE_DOWN, false);
            if (presented == 230) key(SDL_SCANCODE_RETURN, true); // Enter: IGSTATS board
            if (presented == 231) key(SDL_SCANCODE_RETURN, false);
            if (presented == 340) key(SDL_SCANCODE_ESCAPE, true);   // Esc: board -> menu
            if (presented == 350) key(SDL_SCANCODE_ESCAPE, true);   // Esc: resume
        }
        if (boardScript == 3 && presented % 120 == 40) key(SDL_SCANCODE_D, true); // gate lane: mash Shoot so the match can score (IGSCSUM/IGFLSUM goal rows need real goals)
        if (boardScript == 3 && presented % 120 == 52) key(SDL_SCANCODE_D, false);
        if (boardScript >= 2 && pause == IgPause::Board && boardShotAt >= 0) {
            const int bk = boards ? boards->kind() : 0;
            const bool fire = (boardScript == 2 && presented == boardShotAt + 30) ||
                              (boardScript == 3 && ((bk == 4 && presented == boardShotAt + 50) || (bk == 3 && presented == boardShotAt + 60)));
            if (fire) key(bk == 4 && boardScript == 3 ? SDL_SCANCODE_RETURN : SDL_SCANCODE_ESCAPE, true); // Enter: IGSCSUM -> IGFLSUM; Esc: dismiss / quit
        }
        // Highlights lane gate scripts. Mode 4 (fe_highlite_browser): IGMENU Highlights row opens the
        // IGHILITE browser over seeded fixture slots; btrt/btlt page arrows, slor slot cycle, Enter =
        // replay-driver re-drive of the selected slot. Mode 5 (fe_hilsav_list): 'S' on the browser opens
        // IGHILSAV; Enter writes a new rply%04d.dat + index entry via recordToUserDir.
        if (boardScript == 4) {
            if (presented == 200) key(SDL_SCANCODE_ESCAPE, true);   // IGMENU opens after kickoff progress
            if (presented == 215) key(SDL_SCANCODE_DOWN, true);     // cursor: Resume Match -> Instant Replay row (code 1; fixtures seeded -> row enabled)
            if (presented == 216) key(SDL_SCANCODE_DOWN, false);
            if (presented == 224) { key(SDL_SCANCODE_RETURN, true); } // Enter: IGHILITE browser with fixture slots
            if (presented == 225) key(SDL_SCANCODE_RETURN, false);
            if (presented == 300) { key(SDL_SCANCODE_RIGHT, true); }  // btrt slot page arrow
            if (presented == 301) key(SDL_SCANCODE_RIGHT, false);
            if (presented == 310) { key(SDL_SCANCODE_LEFT, true); }   // btlt back to the first page
            if (presented == 311) key(SDL_SCANCODE_LEFT, false);
            if (presented == 320) { key(SDL_SCANCODE_DOWN, true); }   // slor: cycle slot 1 -> 2
            if (presented == 321) key(SDL_SCANCODE_DOWN, false);
            if (presented == 340) { key(SDL_SCANCODE_RETURN, true); } // 'play': app/replay.cpp re-drive of the selected fixture
            if (presented == 341) key(SDL_SCANCODE_RETURN, false);
            if (presented == 420) key(SDL_SCANCODE_ESCAPE, true);     // browser -> menu
            if (presented == 435) key(SDL_SCANCODE_ESCAPE, true);     // resume
        }
        if (boardScript == 5) {
            if (presented == 200) key(SDL_SCANCODE_ESCAPE, true);   // IGMENU opens after kickoff progress
            if (presented == 215) key(SDL_SCANCODE_DOWN, true);
            if (presented == 216) key(SDL_SCANCODE_DOWN, false);    // Resume Match -> Instant Replay row (fixtures seeded -> enabled)
            if (presented == 224) { key(SDL_SCANCODE_RETURN, true); } // Enter: browser opens
            if (presented == 225) key(SDL_SCANCODE_RETURN, false);
            if (presented == 300) { key(SDL_SCANCODE_S, true); }     // 'save' button -> IGHILSAV list
            if (presented == 301) key(SDL_SCANCODE_S, false);
            if (presented == 360) { key(SDL_SCANCODE_RETURN, true); } // Enter: write new slot + index update
            if (presented == 361) key(SDL_SCANCODE_RETURN, false);
            if (presented == 420) key(SDL_SCANCODE_ESCAPE, true);     // save list -> browser
            if (presented == 435) key(SDL_SCANCODE_ESCAPE, true);     // browser -> menu
            if (presented == 450) key(SDL_SCANCODE_ESCAPE, true);     // resume
        }
        // IGTEAM management gate script. Mode 6 (fe_mgmt_pages): IGMENU Management row opens igteam72
        // over the deterministic seed-0x2E7A fixture; gobk/gofd L/R cycles pages {73,74,75,79,80}; Enter
        // on the 73 ab-pair swaps FCDB starter<->bench (igmgmt: evidence line); 'B' (tmf3) opens hub
        // IGTEAM89; Esc back to IGMENU, Esc resumes. PNGs per page via boardReported captures.
        if (boardScript == 6) {
            if (presented == 200) key(SDL_SCANCODE_ESCAPE, true);   // IGMENU opens after kickoff progress
            if (presented == 230) { key(SDL_SCANCODE_M, true); key(SDL_SCANCODE_M, false); } // 'M' from the menu: IGTEAM72 formation mirror (FCDB/sim data; retail mgmt row is on the 2p table)
            for (int i = 0; i < 5; ++i) { // gobk/gofd cycle: 72 -> 73 -> 74 -> 75 -> 79 -> 80
                if (presented == 246 + i * 15) { key(SDL_SCANCODE_RIGHT, true); key(SDL_SCANCODE_RIGHT, false); }
            }
            if (presented == 251) { key(SDL_SCANCODE_DOWN, true); }   // page 73: ab-pair cursor A -> B
            if (presented == 252) key(SDL_SCANCODE_DOWN, false);
            if (presented == 254) { key(SDL_SCANCODE_RETURN, true); } // Enter on ab01: starter <-> bench swap (FCDB data)
            if (presented == 255) key(SDL_SCANCODE_RETURN, false);
            if (presented == 321) { key(SDL_SCANCODE_B, true); }      // tmf3 icon -> hub IGTEAM89 rows
            if (presented == 322) key(SDL_SCANCODE_B, false);
            if (presented == 336) key(SDL_SCANCODE_ESCAPE, true);     // hub -> IGMENU (§E Esc grammar)
            if (presented == 350) key(SDL_SCANCODE_ESCAPE, true);     // resume
        }
        // MANMARKS ratings board gate script. Mode 7 (fe_manmarks_grid): IGMENU Management row opens
        // igteam72 over the seed-0x2E7A fixture, 'M' opens the man-marking board (pNNN squad grid rows
        // from FCDB lineup/subs + mmNN pair rows + sbar pan thumb); UP/DOWN pans the five-row window.
        if (boardScript == 7) {
            if (presented == 200) key(SDL_SCANCODE_ESCAPE, true);   // IGMENU opens after kickoff progress
            if (presented == 230) { key(SDL_SCANCODE_M, true); key(SDL_SCANCODE_M, false); } // 'M' from the menu: IGTEAM72 (frozen FCDB/sim data; retail mgmt row is on the 2p table)
            if (presented == 246) { key(SDL_SCANCODE_M, true); }      // 'M' on igteam72 -> MANMARKS ratings board (§E)
            if (presented == 247) key(SDL_SCANCODE_M, false);
            for (int i = 0; i < 3; ++i)                              // row cursor: pan window slides
                if (presented == 260 + i * 6) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); }
            if (presented == 300) key(SDL_SCANCODE_ESCAPE, true);     // MANMARKS -> IGMENU (§E Esc grammar)
            if (presented == 315) key(SDL_SCANCODE_ESCAPE, true);     // resume
        }
        // Camera popup gate script (§G, gate fe_camera_pages): 'C' opens IGCAMERA from the in-game
        // options chain (igoptns family); DOWN x2 moves the option row cursor; RIGHT ('igbC' forw nav)
        // opens the IGCAMSEL selection grid; DOWN x2 selects "Classic Cam" (FC#405, third row); Enter
        // applies presentation::MatchCamera::setMode + returns to the popup; Esc -> IGMENU, Esc resumes.
        if (boardScript == 8) {
            if (presented == 200) key(SDL_SCANCODE_ESCAPE, true);   // IGMENU opens after kickoff progress
            if (presented == 230) { key(SDL_SCANCODE_C, true); key(SDL_SCANCODE_C, false); } // camera options -> IGCAMERA
            for (int i = 0; i < 2; ++i)                              // option row cursor: slot 0 -> 2
                if (presented == 246 + i * 4) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); }
            if (presented == 262) { key(SDL_SCANCODE_RIGHT, true); key(SDL_SCANCODE_RIGHT, false); } // forw nav -> IGCAMSEL grid
            for (int i = 0; i < 2; ++i)                              // selection row: Action Cam -> Classic Cam
                if (presented == 278 + i * 6) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); }
            if (presented == 300) { key(SDL_SCANCODE_RETURN, true); key(SDL_SCANCODE_RETURN, false); } // apply camera mode
            if (presented == 318) key(SDL_SCANCODE_ESCAPE, true);    // IGCAMERA -> IGMENU ('menu' nav)
            if (presented == 334) key(SDL_SCANCODE_ESCAPE, true);    // resume
        }
        // Practise pages gate script (§G, gate fe_practise_pages): 'P' opens PRACT1 from the practice
        // pause path; btn-nav RIGHT tabs IGPRACT1 -> IGPRACT2 (evidence per page), LEFT back; DOWN +
        // Enter toggles the User Keeper row (FC#1920/#374, On/Off FC 1267/1263); Esc x2 resumes.
        if (boardScript == 9) {
            if (presented == 200) key(SDL_SCANCODE_ESCAPE, true);   // IGMENU opens after kickoff progress
            if (presented == 230) { key(SDL_SCANCODE_P, true); key(SDL_SCANCODE_P, false); } // PRACT1 page grid
            if (presented == 250) { key(SDL_SCANCODE_RIGHT, true); key(SDL_SCANCODE_RIGHT, false); } // btn nav -> IGPRACT2
            if (presented == 270) { key(SDL_SCANCODE_LEFT, true); key(SDL_SCANCODE_LEFT, false); }   // tab cycle back to PRACT1 (IGPRAC1 wrap evidence)
            if (presented == 286) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); }   // keeper row cursor: User Keeper
            if (presented == 300) { key(SDL_SCANCODE_RETURN, true); key(SDL_SCANCODE_RETURN, false); } // toggle -> OFF
            if (presented == 320) key(SDL_SCANCODE_ESCAPE, true);    // board -> IGMENU chain
            if (presented == 334) key(SDL_SCANCODE_ESCAPE, true);    // resume
        }
        // IGOPTN2 board gate script (pass-21igscn, gate fe_igoptn2_board): Esc opens IGMENU; DOWN x4
        // -> Options row; Enter -> IGOPTN2 scene board (four tab panes, FC caption rows from the
        // lst1..lst4 ROWS tuples); RIGHT cycles the row value caption; DOWN moves the list cursor;
        // 2/3/4 switch the tab panes (game play/controller/audio); Esc -> IGMENU, Esc resumes.
        if (boardScript == 10) {
            if (presented == 200) key(SDL_SCANCODE_ESCAPE, true);   // IGMENU opens after kickoff progress
            if (presented == 230) { key(SDL_SCANCODE_O, true); key(SDL_SCANCODE_O, false); } // 'O' mirrors the Options row (code 5 on the armed 1p table) -> igoptn2 board
            if (presented == 246) { key(SDL_SCANCODE_RIGHT, true); key(SDL_SCANCODE_RIGHT, false); }   // value caption cycle
            if (presented == 260) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); }     // list row cursor
            if (presented == 274) { key(SDL_SCANCODE_2, true); key(SDL_SCANCODE_2, false); }           // tab -> game play pane
            if (presented == 290) { key(SDL_SCANCODE_3, true); key(SDL_SCANCODE_3, false); }           // tab -> controller pane
            if (presented == 306) { key(SDL_SCANCODE_4, true); key(SDL_SCANCODE_4, false); }           // tab -> audio pane
            if (presented == 322) key(SDL_SCANCODE_ESCAPE, true);    // board -> IGMENU
            if (presented == 338) key(SDL_SCANCODE_ESCAPE, true);    // resume
        }

        for (SDL_Event e; SDL_PollEvent(&e);) { // keyboard + gamepad transitions into the ported layer
            if (e.type == SDL_EVENT_QUIT) quit = true;
            else if (e.type == SDL_EVENT_MOUSE_MOTION) { // pointer -> virtual 640x480 through the UI
                // content rect, same un-projection as the FE hub; the whole-window scale put the
                // pause-menu rows and confirm-box buttons bar-width inboard of the art on 16:9.
                const auto [pw, ptH] = window.pointSize();
                if (pw && ptH) {
                    int ux = 0, uy = 0;
                    presentation::uiPointerFromWindow(int(e.motion.x), int(e.motion.y), pw, ptH, ux, uy);
                    ptrX = float(ux);
                    ptrY = float(uy);
                }
            }
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (e.button.button == SDL_BUTTON_LEFT) lmb = e.button.down;
            }
            else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE) {
                // Esc opens/leaves IGMENU (global-key hub parity); no IG art on this tree -> direct exit.
                if (pause == IgPause::Board) dismissBoard(); // ESC=0x65 grammar: resume / dismiss to caller [C]
                else if (!haveIgm) quit = true;
                else pause = pause == IgPause::Off ? IgPause::Menu : (pause == IgPause::QuitAsk || pause == IgPause::RestartAsk) ? IgPause::Menu : IgPause::Off; // confirm boxes: Esc cancels back to the menu [C]
            } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && pause != IgPause::Off) {
                // Management lane helpers (§E): gobk/gofd cycle {72,73,74,75,79,80} on the L/R axis; 'B'
                // (tmf3 icon) opens/cycles to the hub IGTEAM89; Enter on a hub row enters the page, on the
                // 73 ab-pair swaps starter <-> bench in IgMgmtData. UP/DOWN = row cursor / slider value.
                const auto mgmtCycle = [&](int dir) {
                    static constexpr std::array<const char*, 6> kPages{"igteam72", "igteam73", "igteam74", "igteam75", "igteam79", "igteam80"};
                    int idx = boards->kind() - 8; // hub (14) stays out of the page cycle
                    for (int s = 0; s < 6; ++s) {
                        idx = (idx + dir + 6) % 6;
                        openBoard(kPages[std::size_t(idx)], 6); // absent layout falls back: restore Board and try the next page
                        if (boards->kind() == idx + 8) return;
                        pause = IgPause::Board;
                    }
                };
                const auto mgSwap = [&](int i) { // ab00/ab01: starter slot A/B <-> bench slot A/B (FCDB data)
                    auto& tm = mgmt.teams[0];
                    if (std::size_t(9 + i) < tm.lineup.size() && std::size_t(i) < tm.subs.size()) {
                        std::swap(tm.lineup[std::size_t(9 + i)], tm.subs[std::size_t(i)]);
                        std::cout << "igmgmt: swap pair " << (i == 0 ? "ab00" : "ab01") << " on #" << tm.lineup[std::size_t(9 + i)].shirt << ' ' << tm.lineup[std::size_t(9 + i)].name
                                  << " off #" << tm.subs[std::size_t(i)].shirt << ' ' << tm.subs[std::size_t(i)].name << '\n';
                    }
                };
                const auto mgRows = [&](int kind) { return kind == 14 ? 6 : kind == 9 ? 2 : kind == 13 ? 12 : kind == 15 ? int(mgmt.teams[0].lineup.size() + mgmt.teams[0].subs.size()) : (kind == 10 || kind == 12) ? 5 : 0; };
                switch (e.key.scancode) { // list cursor INC EDX @0x43FCD0: menu domain = armed 11-row cup table kIgMenuRows
                    case SDL_SCANCODE_UP:
                        if (pause == IgPause::Board && boards && boards->kind() >= 21) boards->moveIgRow(-1); // IGOPTN2 list row cursor (pass-21)
                        else if (pause == IgPause::Board && boards && boards->kind() >= 16) { const int bk = boards->kind(); if (bk == 17) camd.curRow = (camd.curRow + 6) % 7; else boards->moveMgmtRow(-1, bk == 16 ? 5 : 3); } // §G grid/keeper row cursor
                        else if (pause == IgPause::Board && boards && boards->kind() >= 8) { // §E mgmt rows / slider values
                            const int bk = boards->kind();
                            if ((bk == 11 || bk == 12) && mgRows(bk) == 0) mgmt.teams[0].attackBias = std::max(0, mgmt.teams[0].attackBias - 10); // s001 slider (75/79 keyloop_notes: up/down = value)
                            else boards->moveMgmtRow(-1, mgRows(bk));
                        }
                        else if (pause == IgPause::Board && boards && boards->kind() >= 5) boards->moveHl(-1, int(hlRows().size())); // slol: slot arrow
                        else { igSel = igStep(igSel, -1); } break; // 1p menu domain: wrap-up, skipping masked rows
                    case SDL_SCANCODE_DOWN:
                        if (pause == IgPause::Board && boards && boards->kind() >= 21) boards->moveIgRow(+1); // IGOPTN2 list row cursor (pass-21)
                        else if (pause == IgPause::Board && boards && boards->kind() >= 16) { const int bk = boards->kind(); if (bk == 17) camd.curRow = (camd.curRow + 1) % 7; else boards->moveMgmtRow(+1, bk == 16 ? 5 : 3); } // §G grid/keeper row cursor
                        else if (pause == IgPause::Board && boards && boards->kind() >= 8) {
                            const int bk = boards->kind();
                            if ((bk == 11 || bk == 12) && mgRows(bk) == 0) mgmt.teams[0].attackBias = std::min(100, mgmt.teams[0].attackBias + 10);
                            else boards->moveMgmtRow(+1, mgRows(bk)); // slor on the highlights lane; row cursor / pair select on §E pages
                        }
                        else if (pause == IgPause::Board && boards) { if (boards->kind() >= 5) boards->moveHl(+1, int(hlRows().size())); else boards->movePage(-1); } // L/R axis pages the 'igbC' btn1..btn4 pair [C] FUN_0043a3f4
                        else { igSel = igStep(igSel, +1); } break; // 1p menu domain cursor (kIgMenuRows, skipping masked rows)
                    case SDL_SCANCODE_LEFT: if (pause == IgPause::Board && boards) { const int bk = boards->kind(); if (bk >= 21) boards->cycleIgValue(-1); else if (bk >= 8 && bk <= 13) mgmtCycle(-1); else if (bk == 16) dismissBoard(); else if (bk >= 18) { static constexpr std::array<const char*, 3> kPra{"igpract1", "igpract2", "igprac1"}; openBoard(kPra[std::size_t((bk - 18 + 2) % 3)], 9); } else if (bk < 8) boards->movePage(-1); } break; // value cycle (pass-21) / btlt / gobk / 'menu' nav (§G) / practise tab cycle back
                    case SDL_SCANCODE_RIGHT: if (pause == IgPause::Board && boards) { const int bk = boards->kind(); if (bk >= 21) boards->cycleIgValue(+1); else if (bk >= 8 && bk <= 13) mgmtCycle(+1); else if (bk == 16) openBoard("igcamsel", 8); // 'igbC' forw nav: opens the camera selection grid (§G)
                        else if (bk >= 18) { static constexpr std::array<const char*, 3> kPra{"igpract1", "igpract2", "igprac1"}; openBoard(kPra[std::size_t((boards->kind() - 18 + 1) % 3)], 9); } else if (bk < 8) boards->movePage(1); } break; // value cycle (pass-21) / btrt / gofd / practise tab cycle PRACT1 -> PRACT2
                    case SDL_SCANCODE_S: if (pause == IgPause::Board && boards && boards->kind() == 5) openBoard("ighilsav", 5); break; // 'save' button -> IGHILSAV slot list
                    case SDL_SCANCODE_B: if (pause == IgPause::Board && boards && boards->kind() >= 8) openBoard("igteam89", 6); else if (pause == IgPause::Menu) { mgmt = buildIgMgmt(); openBoard("igteam89", 6); } break; // tmf3 icon -> management hub (§E)
                    case SDL_SCANCODE_M: if (pause == IgPause::Menu) { mgmt = buildIgMgmt(); openBoard("igteam72", 6); } else if (pause == IgPause::Board && boards && boards->kind() >= 8 && boards->kind() <= 14) openBoard("manmarks", 6); break; // igmgoptns hotkey: man-marking board; from the menu: mirrors the Team Management row (code 3 on the armed 1p table; hotkey keeps the gate lane key-independent)
                    case SDL_SCANCODE_C: if (pause == IgPause::Menu) { std::printf("live: camera options from the in-game chain\n"); openBoard("igcamera", 8); } break; // mirrors the Camera Select row (code 2) as hotkey (§G, gate fe_camera_pages)
                    case SDL_SCANCODE_L: if (pause == IgPause::Menu) { std::printf("igmenu: controller select board\n"); openBoard("control1", 11); } break; // mirrors the Controller Select row (code 4 on the armed 1p table) as hotkey (pass-26, gate capture)
                    case SDL_SCANCODE_O: if (pause == IgPause::Menu) { std::printf("live: IGOPTN2 board from the in-game chain\n"); openBoard("igoptn2", 10); } break; // mirrors the Options row (code 5) as hotkey: IGOPTN2 scene board, four tab panes (pass-21igscn)
                    case SDL_SCANCODE_1: if (pause == IgPause::Board && boards) { if (boards->kind() == 21) boards->setIgTab(0); else if (boards->kind() >= 22) openBoard("igaoptns", 10); } break; // tbb1 / legacy tab 1 (pass-21igscn)
                    case SDL_SCANCODE_2: if (pause == IgPause::Board && boards) { if (boards->kind() == 21) boards->setIgTab(1); else if (boards->kind() >= 22) openBoard("iggoptns", 10); } break;  // tbb2 / legacy tab 2 (pass-21igscn)
                    case SDL_SCANCODE_3: if (pause == IgPause::Board && boards) { if (boards->kind() == 21) boards->setIgTab(2); else if (boards->kind() >= 22) openBoard("igmoptns", 10); } break;  // tbb3 / legacy tab 3 (pass-21igscn)
                    case SDL_SCANCODE_4: if (pause == IgPause::Board && boards && boards->kind() == 21) boards->setIgTab(3); break; // tbb4 = audio tab (pass-21igscn)
                    case SDL_SCANCODE_P: if (pause == IgPause::Menu || (pause == IgPause::Board && boards && boards->kind() >= 18)) { std::printf("live: practise page from the practice pause path\n"); openBoard("igpract1", 9); } break; // PRACT1 pages (§G, gate fe_practise_pages)
                    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
                        if (pause == IgPause::Board) {
                            const int bk = boards ? boards->kind() : 0;
                            if (bk == 4) openBoard("igflsum", 3);      // Enter advances the full-time chain IGSCSUM -> IGFLSUM [C]
                            else if (bk == 5) playSelected();          // browser 'play': app/replay.cpp re-drive of the selected slot
                            else if (bk == 6) saveHighlights();        // 'save' button: rply%04d.dat + index update via recordToUserDir
                            else if ((bk >= 8 && bk <= 13 && bk != 9) || bk == 15) { /* pages apply in place: no dismiss */ }
                            else if (bk == 9) mgSwap(boards->mgSel() % 2); // ab00/ab01 sub pair swap (FCDB data, persists across frames)
                            else if (bk == 14) { // hub row: b000..b005 -> page map Formation/Subs/Positions/Bias/ManMark/KickTakers
                                static constexpr std::array<const char*, 6> kHubMap{"igteam72", "igteam73", "igteam79", "igteam75", "igteam80", "igteam74"};
                                openBoard(kHubMap[std::size_t(boards->mgSel() % 6)], 6);
                            }
                            else if (bk == 16) openBoard("igcamsel", 8); // Enter on the option row: opens IGCAMSEL selection grid (§G)
                            else if (bk == 17) { static constexpr std::array<int, 7> kRowMode{2, 6, 1, 3, 0, 4, 5}; camera.setMode(CameraMode(kRowMode[std::size_t(camd.curRow)])); std::printf("igcam: camera %s\n", presentation::cameraName(CameraMode(kRowMode[std::size_t(camd.curRow)]))); openBoard("igcamera", 8); } // apply selection, back to the popup (§G)
                            else if (bk >= 21 && bk != 25) { /* list value applied in place: no dismiss (pass-21) */ }
                            else if (bk == 25) dismissBoard(); // CONTROL1: Enter dismisses back to the menu chain (pass-26)
                            else if (bk >= 18) { const bool user = boards->mgSel() == 1; if (user) prac.userKeeper = !prac.userKeeper; else if (boards->mgSel() == 2) prac.cpuKeeper = !prac.cpuKeeper; std::printf("igprac: %s keeper %s\n", user ? "user" : "cpu", (user ? prac.userKeeper : prac.cpuKeeper) ? "ON" : "OFF"); } // FC#1920/#374 on-off toggle (§G)
                            else dismissBoard();
                        }
                        else if (pause == IgPause::Menu) menuActivate();          // Enter on a menu row (pass-27: shared with the mouse lane)
                        else if (pause == IgPause::RestartAsk) confirmYes();      // confirm 'yes' (shared with the confirm-box mouse lane)
                        else if (pause == IgPause::QuitAsk) quit = true;                       // IGQUESHN 'ok': leave the match (FE return deferred)
                        else pause = IgPause::Off;                                           // resume / canc
                        break;
                    default: break;
                }
            }
            if (pause == IgPause::Off) in.handle(e); // gameplay inputs never feed the sim while paused
        }
        // Pointer routing (pass-27): hover on a menu row moves the cursor (masked rows refuse,
        // same rule as igStep); a press activates the row; on the IGQUESHN/confirm box a press
        // over 'okay' activates (quit/restart), over 'canc' returns to the menu [H].
        {
            const int count = int(kIgMenuRows.size());
            const float winX = 116.f + (408.f - 286.f) / 2.f;
            const float winY = 131.f + (231.f - float(count) * 18.f) / 2.f;
            const auto rowAtPtr = [&] {
                if (ptrX < winX || ptrX >= winX + 286.f || ptrY < winY || ptrY >= winY + float(count) * 18.f) return -1;
                return int((ptrY - winY) / 18.f);
            };
            if (pause == IgPause::Menu) {
                const int r = rowAtPtr();
                if (r >= 0 && !igDisabled(r)) {
                    igSel = r;
                    if (lmb && !prevLmb) {
                        std::cout << "igmenu: mouse activate row=" << r << " code=" << int(kIgMenuRows[std::size_t(r)].code) << '\n';
                        menuActivate();
                    }
                }
            } else if (pause == IgPause::QuitAsk || pause == IgPause::RestartAsk) {
                const auto hit = [&](const char* nm) { const formats::LayoutObject* o = layObj(*igQues, nm); return o && presentation::widgetHit(*o, ptrX, ptrY); };
                if (lmb && !prevLmb) {
                    if (hit("okay")) { if (pause == IgPause::QuitAsk) quit = true; else confirmYes(); }
                    else if (hit("canc")) pause = IgPause::Menu;
                }
            }
        }
        prevLmb = lmb;
        if (quit) break;

        // The pause menu freezes the simulation while open (the original halts the match loop).
        const std::uint64_t now = SDL_GetTicks();
        if (pause == IgPause::Off) {
            acc += smokeFrames ? kStepMs : double(std::min<std::uint64_t>(now - last, 100)); // spiral guard
            last = now;
            // exe grammar (register D-70, FUN_0049daf0): the device is read exactly once per game
            // frame by the controller update; every sim tick drained from this frame consumes the
            // same word. Sample after the event pump so the word reflects this frame's levels.
            sim::ControllerState framePad{};
            in.fill(1, framePad);
            while (acc >= kStepMs) {
                acc -= kStepMs;
                sim::ControllerState c = framePad; // shared pad word of this game frame
                if (!sawPassBit && (c.padBits & input::actionBit(config::Action::Pass))) {
                    sawPassBit = true;
                    std::cout << "live: input->sim pass bit 0x20 reached controller 0 pad word\n";
                }
                if (boardScript == 3) { // gate lane: strike whenever the controlled player owns the ball;
                    const int p = w.ball.possessor;    // a real goal fills goalLog -> IGSCSUM/IGFLSUM rows
                    if (p >= 0 && w.players[std::size_t(p)].ctrl == 1 && (w.time.simFrames % 90u) < 8u)
                        c.padBits |= input::actionBit(config::Action::Shoot);
                }
                runner.setController(0, c);
                if (recordReplay || boardScript >= 4) rec.append(recTick++, c); // recorded input word for this tick (gate lanes keep the recorder hot for 'save')
                if (!runner.tick()) { // match ended (DAT_0057c604 == 2)
                    if (wantFullBoards && !ftShown) { ftShown = true; openBoard("igscsum", 3); } // full time opens IGSCSUM, then the IGFLSUM page [C]
                    else { quit = true; break; }
                }
                // Center-spot restarts (phase 0) wait for the taker's kick: pulse Pass (key S, bit 0x20),
                // and deliver the exe's "restart taken" event 0xb to Match_onEvent case 11 (0x470F98).
                if (w.match.phase == 0 && prevPhase != 0) { kickIn = smokeFrames ? 40 : 90; forceIn = smokeFrames ? 90 : 150; }
                prevPhase = w.match.phase;
                if (kickIn > 0 && --kickIn == 0) { key(SDL_SCANCODE_S, true); kickDur = 8; }
                if (kickDur > 0 && --kickDur == 0) key(SDL_SCANCODE_S, false);
                if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) {
                    std::cout << "live: restart forced via Match_dispatchEvent(0xb)\n";
                    sim::Match_dispatchEvent(w, 0xb, w.match.phaseTeam);
                }
                // Possession row source: World.time.possessionFrames (DAT_0065f264) deltas credited to the
                // owning team (DAT_0065f250); no sim-side accumulator exists.
                if (w.time.possessionFrames != lastPoss) {
                    const std::int32_t delta = w.time.possessionFrames - lastPoss;
                    if (delta > 0 && delta < (1 << 20) && w.time.possessingTeam >= 0 && w.time.possessingTeam < 2)
                        possAcc[std::size_t(w.time.possessingTeam)] += delta;
                    lastPoss = w.time.possessionFrames;
                }
                // Forced half/full-time whistle for the IG board gates + interactive play: the natural
                // MatchClock end path needs clean-ball conditions a scripted smoke match cannot meet.
                // Arm the phase-9 period-end sequence with the exe's own grammar (rules.cpp 0x471F30):
                // status |= 8, Match_requestPeriodEnd(whistle=1, next, team), event 9 = whistle sound.
                // Friendly periods end with nextPhase 0xe -> half; the last one takes 0x10 -> match end
                // (status |= 0x10 in requestPeriodEnd: seq9 emits MatchEnd, SimRunner sees DAT_0057C604=2).
                if ((boardScript == 2 || boardScript == 3) && w.match.phase == 9 && !(w.match.status & (8u | 0x20u)) &&
                    static_cast<std::int32_t>(w.match.elapsedSec) + 5 >= w.match.durationSec + w.match.injurySec) {
                    const bool full = boardScript == 3 && w.match.periodIndex > initPeriod; // second period ends the match [C] IGSCSUM at full time
                    w.match.status |= 8u;
                    sim::Match_requestPeriodEnd(w, 1, full ? 0x10 : 0xe, 2);
                    sim::Match_dispatchEvent(w, 9, 0); // whistle: the original plays it exactly when IGHALF/IGSCSUM opens [C]
                }
                if (wantHalfBoard && !halfShown && w.match.periodIndex > initPeriod) { halfShown = true; openBoard("ighalf", 2); } // original opens IGHALF at the half-time whistle [C]
                // exe audio lane: the pumps run once per engine frame (1/50 s tick, tick host
                // FUN_004906f8), and cadence gates compare the DAT_00636a14 mirror — the
                // multimedia timer tick counter (InitTimer(100), VA 0x43cb8f), 10 ms/tick, so
                // each 20 ms engine step adds 2 ticks. Render fps cannot alter trigger timing;
                // smoke/capture lanes stay deterministic (the counter rides the step
                // accumulator, not wall-clock). Pause: the exe timer thread keeps ticking
                // while IGMENU halts the pump, so the stamp stays stale and the first pump
                // after resume fires one cadence arrival — the halted counter reproduces the
                // same observable arrival.
                if (haveSpeech) {
                    // Speech lane re-level on percent-table change (options commentary row -> idx3).
                    const int pct = audio::sceneLevels().level(audio::kSceneSpeech);
                    if (pct != speechPctApplied) { speechPctApplied = pct; commentary.setScenePercent(pct); }
                    const std::uint32_t before = commentary.sentencesStarted();
                    for (; evPos < w.events.size(); ++evPos) commentary.triggerLive(w.events[evPos]);
                    if (commentary.sentencesStarted() > before)
                        std::cout << "live: commentary " << sim::eventName(w.events[evPos - 1].type)
                                  << " -> phrase " << commentary.lastPhrase() << '\n';
                    commentary.pump(1); // FUN_0048f920 per-frame pump, frame = 1/50 s engine tick
                } else {
                    evPos = w.events.size();
                }
                for (; sfxPos < w.events.size(); ++sfxPos) matchAudio.trigger(w.events[sfxPos]);
                matchAudio.relevelCrowd(); // idx2 seam: options reaction row -> loop voice float
                matchAudio.pumpChants(audioTicks); // pass-53 chant pump (FUN_0048da50): timer-stamp cadence, 100 ticks = 1.000 s
                matchAudio.tickCrowd();  // pass-55 crowd envelope pump (FUN_004e510c idle walk + FUN_005444
                                         // blend consumer): bed/hum voice gains follow the reaction machine
                if (menuMusic) menuMusic->update(); // FE music seam: fade-out at launch (FUN_0048ec64), rotation pump
                audioTicks += 2;                  // 10 ms/tick: one 20 ms engine step = 2 ticks
            }
        } else {
            last = now; acc = 0.0; // keep the clock honest so resuming does not burst-tick
        }

        MatchSnapshot snap = snapshotFromWorld(w, boot.md);
        snap.hud.teamNames = boot.abbrv; // FCDB STR0 abbreviations on the strip
        // Camera smoothing takes the frames SINCE THE LAST RENDER (demo passes it once per capture);
        // feeding the absolute counter collapsed the tele cam to ground height every frame.
        const int simDelta = std::max(1, int(w.time.simFrames) - int(prevSim));
        prevSim = int(w.time.simFrames);
        camera.tick(snap, simDelta);

        for (int pid = 0; pid < sim::kPlayers; ++pid)
            if (w.players[std::size_t(pid)].ctrl == 1) controlledPid = pid;
        snap.hud.controlled[0] = controlledPid; // user 1's player drives the shirt label + ground marker

        // In-game pause menu composite over the live frame (IGMENU scene tick=0x43FC40; list widget
        // 'menu' x=176 y=138 w=286 h=211 -> 19 px rows, FEFLOW73 pattern). The LAY objects carry
        // fname=null.fsh: the scene init installs button art from FE/INGAME/INGAME.FSH (tags
        // bt0n/bt0d..btnN/btnd; RELEASED->*n, HILITED/PRESSED->*d). QuitAsk = the IGQUESHN box
        // ("are you sure" ok/canc pair; 'quit'-tag path of IGMENU init 0x4401A4).
        bool igShotPending = false;
        render::SpriteBatch ov; // outlives scene.build(): pushOverlay stores a pointer consumed there
        bool boardShot = false;          // first draw of the current IG board -> prefix__ig*.png evidence capture
        if (pause == IgPause::Board && boards && boards->isOpen()) {
            const int bk = boards->kind();
            IgMatchData d = buildIgData(); // persistent header pane data for the save-list pair (typ1/nmb1/flg1)
            presentation::FeDrawStats bd = bk >= 21 ? boards->drawIgOptns(ov) : bk >= 18 ? boards->draw(ov, d, prac) : bk >= 16 ? boards->draw(ov, d, camd) : bk >= 8 ? boards->draw(ov, mgmt) : bk >= 5 ? boards->draw(ov, IgHlData{hlRows(), &d}) : boards->draw(ov, d); // §G popups get the camera/practise fixtures; IGOPTN2 trio paints its own caption lane (pass-21)
            if (!boardReported[std::size_t(bk)]) { // gate evidence line + PNG (legible stat rows)
                boardReported[std::size_t(bk)] = true;
                std::cout << kBoardName[std::size_t(bk)] << ": shapes=" << bd.shapes << " texts=" << bd.texts << '\n';
                boardShotAt = presented; // gate auto-script pacing (loop head)
                boardShot = true;
            }
            if (bk == 21) { // per-tab + per-cycle evidence: pane rows/value on tab switch, row/value on cycle
                const int t = boards->igTab();
                const int sig = t * 100000 + boards->igSel() * 1000 + boards->igValue();
                if (t != igLastTab) {
                    igLastTab = t; igTabShot = t; igLastSig = sig;
                    std::cout << "IGOPTN2: tab=" << t << " rows=" << boards->igRowCount() << " val=" << boards->igValue() << '\n';
                } else if (sig != igLastSig) {
                    igLastSig = sig; igCycShot = true;
                    std::cout << "IGOPTN2: cycle tab=" << t << " row=" << boards->igSel() << " val=" << boards->igValue() << '\n';
                }
            }
            scene.pushOverlay(ov); // consumed by the build() below
        } else if (pause != IgPause::Off) {
            // Retail pointer semantics [H]: LMB over a confirm-box button paints its PUSHEDSHAPE
            // variant (widgetHit + FeUiState::pressed, same lane as the FE hub buttons).
            presentation::FeUiState uiSt;
            uiSt.mx = ptrX; uiSt.my = ptrY;
            if (lmb && (pause == IgPause::QuitAsk || pause == IgPause::RestartAsk)) {
                for (const char* nm : {"okay", "canc"}) {
                    const formats::LayoutObject* o = layObj(*igQues, nm);
                    if (o && presentation::widgetHit(*o, ptrX, ptrY)) { uiSt.pressed = nm; break; }
                }
            }
            // Retail `name` byte-lane override [C] (pass-39): IGMENU init tail 0x43f1c0/0x43ffca —
            // the controller byte DAT_0057c920 is zero for the live-pause boards, so the engine
            // installs exe string VA 0x562144 (" ", blank) on widget `name` through FUN_004788b4,
            // covering the static textid-231 caption ("Are you sure you wish to exit with points
            // remaining?", tournament context) that the composite would otherwise paint at
            // (319,390). Port = composite whitelist without `name` -> blank name caption [C].
            static const std::vector<std::string> kIgMenuVisible{"box1", "box2", "box3", "typ1", "flg1", "box4", "nmb1",
                                                                 "titl", "box5", "nmb2", "typ2", "flg2", "menu"};
            FeDrawStats ig = pause == IgPause::Menu ? composite(*igMenu, ov, igArt, igText.get(), &kIgMenuVisible, &uiSt)
                                                     : composite(*igQues, ov, igArt, igText.get(), nullptr, &uiSt);
            strokeBoxes(ov, pause == IgPause::Menu ? *igMenu : *igQues, ig); // frame-only box outlines [H]
            // Shared header overlay [C] FUN_0043f3e8: IGMENU init and the confirm-box painter
            // FUN_0043fb90 both install it (0x43fb96 calls FUN_0043f3e8) — typ1/typ2 names,
            // flg1/flg2 shields, box4/box5 white plate, nmb1/nmb2 score digits on every
            // pause-scene box (menu and IGQUESHN alike).
            const IgMatchData hd = buildIgData();
            const auto paintPauseHeader = [&](const formats::ScreenLayout& lay) {
                for (int t = 0; t < 2; ++t) {
                    if (const formats::LayoutObject* o = layObj(lay, t == 0 ? "typ1" : "typ2"))
                        ig.texts += paintCentered(igText.get(), ov, hd.names[std::size_t(t)], float(o->x) + 50.f, float(o->y) + 8.f, 130.f);
                    if (const formats::LayoutObject* o = layObj(lay, t == 0 ? "flg1" : "flg2"); o && !hd.flags[std::size_t(t)].empty()) {
                        const ScreenArt::Shape sh = igArt.shape("..\\global\\", hd.flags[std::size_t(t)], "f000", -1);
                        if (sh.tex) { ov.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++ig.shapes; }
                    }
                }
                // Original LAY order puts box4/box5 AFTER flg1/flg2: the white RECT plate must
                // repaint over the shield art it was drawn under in the composite pass.
                for (int t = 0; t < 2; ++t) {
                    const formats::LayoutObject* box = layObj(lay, t == 0 ? "box4" : "box5");
                    if (box) { ov.draw(0, {float(box->x), float(box->y), float(box->width), float(box->height)}, {}, 0xFFFFFFFFu); ++ig.shapes; }
                }
                for (int t = 0; t < 2; ++t) {
                    const formats::LayoutObject* box = layObj(lay, t == 0 ? "box4" : "box5");
                    if (const formats::LayoutObject* o = layObj(lay, t == 0 ? "nmb1" : "nmb2")) {
                        const float cx = box ? float(box->x) + 13.f : float(o->x) + 6.f; // score plate width 26 (box4/5 RECT at 212/399)
                        ig.texts += paintCentered(igText.get(), ov, numStr(hd.score[std::size_t(t)]), cx, float(o->y) + 7.f, 24.f);
                    }
                }
            };
            if (pause == IgPause::Menu) {
                // Row grammar [C] tick 0x43fc40 + init 0x43f040: 18-px rows, runtime-centred into
                // box2 (x=116 w=408): win.x = (408-286)/2+116 = 177, win.y = (231-count*18)/2+131
                // ([win+0xc/+0e] centering; LAY x=176 y=138 is the stale template). Captions are the
                // FC remap table textids (widgetStringAt); the cursor row gets the paintRowLabel
                // hilite-pane idiom (list_painter §1). The old INGAME.FSH bt0n..bt5n strip is the
                // 20x20 button-icon art, not menu rows: replaced by the decoded {tid,code} table.
                const int count = int(kIgMenuRows.size());
                const float winX = 116.f + (408.f - 286.f) / 2.f;
                const float winY = 131.f + (231.f - float(count) * 18.f) / 2.f;
                // objecttype 43 boxes (box1 hdr 116,32,408x95; box2 menu container 116,131,408x231;
                // box3 bottom 116,366,408x84) declare no FILLCOLOUR/shape: the engine draws them as
                // frame-only outlines -> 1px stroke quads, same tint as the list pane border.
                const auto stroke = [&](float bx, float by, float bw, float bh) {
                    const std::uint32_t edge = render::packRgba(150u, 150u, 150u, 200u);
                    ov.draw(0, {bx, by, bw, 1.f}, {}, edge);
                    ov.draw(0, {bx, by + bh - 1.f, bw, 1.f}, {}, edge);
                    ov.draw(0, {bx, by, 1.f, bh}, {}, edge);
                    ov.draw(0, {bx + bw - 1.f, by, 1.f, bh}, {}, edge);
                    ig.shapes += 4;
                };
                stroke(116.f, 32.f, 408.f, 95.f); stroke(116.f, 131.f, 408.f, 231.f); stroke(116.f, 366.f, 408.f, 84.f);
                // 'titl' runtime caption [C] init 0x43f170/0x43f314/0x43f341: FUN_0x4c25d0(id) reads the
                // language string table DAT_00585cb0 and installs on widget 'titl' via FUN_00477cb4
                // (EDX='titl'). Scene event state DAT_00601934 -> {1 Half-Time(868), 2 Full-Time(776),
                // 3/4 Extra-Time(669), else End Match(632)}; state 0 (live play) + live table ->
                // "Pause Menu"(1333), PK-alt table 0x57c886 -> "Scenario Select"(1601), practice table
                // 0x57c902 -> "Post Game Menu"(1384) (not reachable in the port's 1p live sim). Bank ids
                // verified against FCENG.BIN (analysis/pass11/FCENG.BIN.json). Port derivation from sim
                // status/endWhistle/periodIndex mirrors the whistle sequence [C]-approx.
                const auto igTitleId = [](const sim::MatchState& m) {
                    if ((m.status & 0x10u) != 0u || m.endWhistle == 2) return 776;      // Full-Time
                    if (m.endWhistle == 1) return m.periodIndex >= 2 ? 669 : 868;       // Extra-Time / Half-Time break
                    return 1333;                                                        // Pause Menu (live play)
                };
                if (const formats::LayoutObject* o = layObj(*igMenu, "titl")) {
                    const std::string cap = igText && igText->ready()
                                                ? std::string(igText->stringAt(std::size_t(igTitleId(w.match))))
                                                : std::string();
                    ig.texts += paintCentered(igText.get(), ov, cap, 320.f, float(o->y) + 8.f, 350.f,
                                              render::packRgba(255u, 255u, 255u, 255u), 1); // fontid 1 (LAY titl)
                }
                paintPauseHeader(*igMenu);
                for (int row = 0; row < count; ++row) {
                    const float ry = winY + 18.f * float(row);
                    if (row == igSel) { ov.draw(0, {winX, ry, 286.f, 18.f}, {}, render::packRgba(255, 224, 96, 70)); ++ig.shapes; } // HILITED pane quad (paintRowLabel idiom)
                    const std::string_view cap = igText ? igText->widgetStringAt(kIgMenuRows[std::size_t(row)].tid) : std::string_view{};
                    ig.texts += paintCentered(igText.get(), ov, cap, winX + 143.f, ry + 9.f, 286.f,
                                              row == igSel ? render::packRgba(16u, 16u, 40u, 255u) : render::packRgba(255u, 236u, 190u, 255u), 0);
                }
                if (!igReported) { std::cout << "IGMENU: shapes=" << ig.shapes << " texts=" << ig.texts << '\n'; igReported = true; }
            } else {
                // Confirm box [C] dispatch chains 0x43f754 (restart, code 9) / 0x43f7dd (quit-match,
                // code 10) -> FUN_0043fb50 slots + painter FUN_0043fb90 fallback (0x43fba1 branch when
                // DAT_0057c934 == 0): 'titl' id = word[0x57c924] = the activated row's {tid,code} high
                // word (row caption), 'line' id = word[0x57c926] = the chain's literal question id,
                // 'lin2' = 0 (blank: retail draws nothing for id 0). Ids resolve through the FC remap
                // table (widgetStringAt, pass-21igscn rule): literal 0x112 -> 274 "Are you sure you
                // want to Restart Match?", literal 0x10e -> 270 "Quit Match"; PK/practice variants
                // (0x28f/0x2b7/0x28e) are not reachable in the port's 1p live sim.
                paintPauseHeader(*igQues);
                // 'okay'/'canc' + 'btt1'/'btt2': retail paints the white bo25 RECT plate (140x19
                // at 175/349) and the Yes/No captions as separate layers; composite draws the plate
                // over the caption first glyph, so repaint both captions after the composite with
                // drawLabel's exact placement (align 3 boxw center, y+6, palette ink) for a
                // pixel-exact overlay that uncovers the clipped first glyph.
                for (const char* nm : {"btt1", "btt2"}) {
                    if (const formats::LayoutObject* o = layObj(*igQues, nm)) {
                        const std::size_t id = nm[3] == '1' ? 12u : 13u;
                        const std::string_view s = igText ? igText->widgetStringAt(id) : std::string_view{};
                        if (s.empty() || !igText) continue;
                        const formats::FfnFont* f = igText->face(0);
                        if (!f) f = igText->font();
                        const render::TextureId tex = igText->faceTexture(0) ? igText->faceTexture(0) : igText->texture();
                        if (!f || !tex) continue;
                        const float sizePx = f->lineHeight ? float(f->lineHeight) : 16.f;
                        const float w = presentation::textWidthPx(*f, std::string(s), sizePx);
                        const int xoff = o->intProp("TEXTXOFFSET", 0);
                        const int yoff = o->intProp("TEXTYOFFSET", 0);
                        const int boxw = o->width > 0 ? o->width : o->intProp("PIXELWIDTH", 0);
                        const int align = o->intProp("ALIGNMENT", 1);
                        float px = float(o->x) + float(xoff);
                        if (align == 2) px -= w;
                        else if (align >= 3) px += (float(boxw) - w) * 0.5f;
                        const int cidx = o->intProp("TEXTCOLOUR", o->intProp("NORMALCOLOUR", -1));
                        const auto& pal = formats::globalPalette();
                        const std::uint32_t ink =
                            cidx >= 0 && std::size_t(cidx) < pal.size()
                                ? render::packRgba(pal[size_t(cidx)][0], pal[size_t(cidx)][1], pal[size_t(cidx)][2], 0xFF)
                                : render::packRgba(0xFFu, 0xFFu, 0xFFu, 0xFFu); // drawLabel white fallback
                        for (const auto& q : presentation::layoutText(*f, s, px, float(o->y) + float(yoff), sizePx, ink)) {
                            ov.draw(tex, q.dst, q.uv, q.rgba);
                            ++ig.texts;
                        }
                    }
                }
                const std::size_t row = std::min(std::size_t(std::max(igSel, 0)), kIgMenuRows.size() - 1u);
                const std::size_t lineId = kIgMenuRows[row].code == 9u ? 274u : 270u;
                if (const formats::LayoutObject* o = layObj(*igQues, "titl"))
                    ig.texts += paintCentered(igText.get(), ov,
                                              igText ? igText->widgetStringAt(kIgMenuRows[row].tid) : std::string_view{},
                                              320.f, float(o->y) + 8.f, 350.f,
                                              render::packRgba(255u, 255u, 255u, 255u), 1); // fontid 1 (LAY titl)
                if (const formats::LayoutObject* o = layObj(*igQues, "line"))
                    ig.texts += paintCentered(igText.get(), ov,
                                              igText ? igText->widgetStringAt(lineId) : std::string_view{},
                                              320.f, float(o->y) + 8.f, 286.f,
                                              render::packRgba(255u, 236u, 190u, 255u), 0); // fontid 0 (LAY line)
                if (!iqReported) { std::cout << "IGQUESHN: shapes=" << ig.shapes << " texts=" << ig.texts << '\n'; iqReported = true; }
            }
            if (mouseTex && ptrX >= 0.f) { // live pointer sprite (pass-27), clamped to the virtual 640x480
                const float cw = float(mouseArt.bmp.width), ch = float(mouseArt.bmp.height);
                const float cx = std::clamp(ptrX + float(mouseArt.hotX), 0.f, render::SpriteBatch::kWidth - cw);
                const float cy = std::clamp(ptrY + float(mouseArt.hotY), 0.f, render::SpriteBatch::kHeight - ch);
                ov.draw(mouseTex, {cx, cy, cw, ch}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
                ++ig.cursors;
            }
            scene.pushOverlay(ov); // consumed by the build() below (SpriteBatch::append after the HUD strip)
            if (igPauseSmoke && presented == 110) igShotPending = true; // visual evidence of the composite
        }

        const render::Frame& frame = scene.build(snap, camera.camera(), /*enhanced=*/true);
        if (igShotPending) savePng(renderer, frame, prefix + "_igmenu.png");
        if (igMouseShot) { savePng(renderer, frame, prefix + "_igmenu_mouse.png"); igMouseShot = false; }
        if (boardShot && boards) { std::string nm(lowerName(kBoardName[std::size_t(boards->kind())])); for (char& c : nm) c = char(std::tolower(static_cast<unsigned char>(c))); savePng(renderer, frame, prefix + "_" + nm + ".png"); }
        if (igTabShot >= 0) { savePng(renderer, frame, prefix + "_igoptn2_t" + std::to_string(igTabShot) + ".png"); igTabShot = -1; }
        if (igCycShot) { savePng(renderer, frame, prefix + "_igoptn2_cyc.png"); igCycShot = false; }
        if (igPauseSmoke && pause == IgPause::QuitAsk && !iqShot) { savePng(renderer, frame, prefix + "_igquesn.png"); iqShot = true; }

        if (smokeFrames && presented == 0) { // gate evidence: the scene drew geometry and the HUD strip
            std::cout << "live: shapes=" << frame.draws.size()
                      << " uiVerts=" << (frame.ui ? frame.ui->vertices().size() : 0u)
                      << " clock=" << snap.hud.clockSeconds << '\n';
            if (frame.draws.empty()) { std::cerr << "live: scene drew no draws\n"; return 1; }
            if (!frame.ui || frame.ui->vertices().empty()) { std::cerr << "live: HUD strip not drawn\n"; return 1; }
        }
        if (smokeFrames && presented == 40) savePng(renderer, frame, prefix + "_mid.png"); // visual evidence
        if (smokeFrames && preShotAt >= 0 && presented == preShotAt) savePng(renderer, frame, prefix + "_pre.png"); // pass-28 audit baseline (board-only region diffs)

        if (window.minimized() || !renderer.present(frame)) { // swapchain hiccup / minimized: pace and retry
            SDL_Delay(16);
            continue;
        }
        ++presented;
        if (smokeFrames && presented >= smokeFrames) break;
    }
    if (recordReplay && rec.ticks()) { // seal + write user/rplyNNNN.dat + rplyindx.dat (exe path pattern)
        std::error_code recEc;
        fs::create_directories(tree / "user", recEc);
        const std::uint64_t finalHash = sim::stateHash(w);
        const int slot = recordToUserDir(tree / "user", rec.build(0x2E7A, finalHash));
        std::cout << "replay: recorded " << rec.ticks() << " ticks -> user/rply" << std::setw(4) << std::setfill('0')
                  << slot << ".dat hash=0x" << std::hex << finalHash << std::dec << '\n';
    }
    std::cout << "live: sim advanced clock " << startClock << " -> " << elapsedSecFloat(w)
              << " events=" << w.events.size() << " commentary sentences=" << commentary.sentencesStarted()
              << " decodes=" << commentary.decodedSegments()
              << " whistle=" << matchAudio.whistles() << " kicks=" << matchAudio.kicks()
              << " crowd=" << (matchAudio.crowdStarted() ? 1 : 0) << '\n';
    if (igPauseSmoke) { // pass-118timing restart-lane gate: the scripted RestartAsk must have
        // rebuilt the world (confirmYes) and the reset event cursor must have re-fired the
        // kickoff whistle in the restarted match (pre + post = 2 total starts). A stale sfxPos
        // (the pre-pass bug) keeps the post-restart world silent and fails here.
        if (restarts < 1) { std::cerr << "igpause: restart lane never ran\n"; return 1; }
        if (matchAudio.whistles() < 2) {
            std::cerr << "igpause: post-restart SFX cursor stale (whistle="
                      << matchAudio.whistles() << " after restart)\n";
            return 1;
        }
        std::cout << "igpause: restart ok restarts=" << restarts
                  << " whistle=" << matchAudio.whistles() << '\n';
    }
    if (smokeFrames) { // gate assertions: sim moved, events fired, input reached the controller pad word
        if (!(elapsedSecFloat(w) > startClock)) { std::cerr << "live: sim clock did not advance\n"; return 1; }
        if (w.events.empty()) { std::cerr << "live: no sim events fired\n"; return 1; }
        if (!sawPassBit) { std::cerr << "live: input->sim pass bit never reached controller 0\n"; return 1; }
    }
    std::cout << prefix << ": live match ran " << presented << " frames\n";
    return 0;
}

int runLiveMatch(int argc, char** argv) {
    fs::path root;
    if (const char* t = std::getenv("WC98_TREE"); t && *t) root = t;
    int smoke = 0;
    std::string prefix = "/tmp/wc98port-out/live";
    bool record = false; // --record-replay (parsed by main too, consumed here in live mode)
    bool igScript = false; // --ig-pause-smoke N: scripted Esc/Enter pause tour (gate fe_igmenu)
    int boardScript = 0; // --ig-stats-smoke 1 / --ig-half-smoke 2 / --ig-full-smoke 3 (gates fe_igstats/fe_igscsum/fe_igflsum)
    int quality = 1; // --quality N: scene-level camera quality word DAT_00644778 (h23 tier lane)
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--tree" && i + 1 < argc) root = argv[++i];
        else if (a == "--live-match-smoke") smoke = std::atoi(argv[++i]);
        else if (a == "--out-prefix" && i + 1 < argc) prefix = argv[++i];
        else if (a == "--quality") quality = std::atoi(argv[++i]); // h23 tier selector [P]
        else if (a == "--ig-pause-smoke") { smoke = std::atoi(argv[++i]); igScript = true; } // gate fe_igmenu
        else if (a == "--ig-stats-smoke") { smoke = std::atoi(argv[++i]); boardScript = 1; } // gate fe_igstats: IGMENU Stats-row tour -> IGSTATS composite
        else if (a == "--ig-half-smoke") { smoke = std::atoi(argv[++i]); boardScript = 2; }   // gate fe_ighalf: 1-min halves, IGHALF opens at the half whistle
        else if (a == "--ig-full-smoke") { smoke = std::atoi(argv[++i]); boardScript = 3; }   // gates fe_igscsum/fe_igflsum: full time -> IGSCSUM Enter -> IGFLSUM
        else if (a == "--ig-hlite-smoke") { smoke = std::atoi(argv[++i]); boardScript = 4; } // gate fe_highlite_browser: Highlights row -> IGHILITE browser, fixture slots + replay-driver play action
        else if (a == "--ig-hlsav-smoke") { smoke = std::atoi(argv[++i]); boardScript = 5; }  // gate fe_hilsav_list: browser 'S' save button -> IGHILSAV list, Enter writes a new slot via recordToUserDir
        else if (a == "--ig-mgmt-smoke") { smoke = std::atoi(argv[++i]); boardScript = 6; } // gate fe_mgmt_pages: Management row -> IGTEAM72..80 cycle + ab-pair swap + hub IGTEAM89 (§E)
        else if (a == "--ig-manmarks-smoke") { smoke = std::atoi(argv[++i]); boardScript = 7; } // gate fe_manmarks_grid: igteam72 'M' -> MANMARKS ratings board + row cursor (§E)
        else if (a == "--ig-camera-smoke") { smoke = std::atoi(argv[++i]); boardScript = 8; } // gate fe_camera_pages: 'C' -> IGCAMERA option rows + forw nav -> IGCAMSEL grid, Enter applies a camera mode (§G)
        else if (a == "--ig-practise-smoke") { smoke = std::atoi(argv[++i]); boardScript = 9; } // gate fe_practise_pages: 'P' -> PRACT1/IGPRACT2 page cycle + keeper-row toggle (§G)
        else if (a == "--ig-optns-smoke") { smoke = std::atoi(argv[++i]); boardScript = 10; } // gate fe_igoptn2_board: Esc -> Options row -> IGOPTN2 board + tab panes (2/3/4) + value/row cycling (pass-21igscn)
        else if (a == "--record-replay") record = true;
        // Flags owned by other modes of the same CLI pass through untouched.
    }
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) { std::cout << "skip: game tree not found\n"; return 2; }

    const config::Settings cfg = config::load(config::defaultPath()).settings;
    const auto& g = cfg.graphics;
    const int width = int(g.width ? g.width : 1280), height = int(g.height ? g.height : 960);
    const bool fullscreen = g.window != config::WindowMode::Windowed;

    std::unique_ptr<platform::Window> window;
    try {
        window = std::make_unique<platform::Window>("World Cup 98", width, height, fullscreen);
    } catch (const std::exception& e) {
        if (smoke) { std::cout << "skip: no display (" << e.what() << ")\n"; return 2; } // headless CI gate skip
        throw;
    }

    render::RenderSettings settings{
        .mode = g.presentation == config::Presentation::Enhanced ? render::PresentMode::Enhanced : render::PresentMode::Original,
        // kAspects mapping (main.cpp): Auto/R4x3 -> pillarbox 4:3, R16x9+ -> widescreen.
        .aspect = g.aspect <= config::Aspect::R4x3 ? render::Aspect::Pillarbox4x3 : render::Aspect::Widescreen,
        .msaa = g.msaa > 1,
    };
    auto messages = std::make_shared<std::atomic<std::uint32_t>>(0);
    render::VulkanConfig vcfg;
    vcfg.validation = g.validation || std::getenv("WC98_VK_VALIDATE") != nullptr; // render-demo env pattern
    vcfg.validationCounter = messages;
    vcfg.instanceExtensions = platform::Window::vulkanInstanceExtensions();
    vcfg.headlessFallback = platform::dummyLane();
    vcfg.createSurface = [&](VkInstance inst) {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!vcfg.headlessFallback && !SDL_Vulkan_CreateSurface(window->sdl(), inst, nullptr, &surface)) std::cerr << "SDL_Vulkan_CreateSurface: " << SDL_GetError() << '\n';
        return surface;
    };
    vcfg.drawableSize = [&] { return window->pixelSize(); };

    auto renderer = render::createVulkanRenderer(vcfg, settings);
    const int rc = runLiveLoop(root, prefix, smoke, *window, *renderer, record, igScript, boardScript, quality);
    renderer.reset(); // drain validation messages before the verdict (render-demo pattern)
    if (*messages && rc == 0) { std::cerr << *messages << " validation message(s)\n"; return 3; }
    return rc;
}

} // namespace app
} // namespace wc98
