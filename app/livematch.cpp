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

bool MatchBoot::init(const fs::path& root) {
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

    // Teams: FCDB.DBI carries 56 teams (team table @0x204, count 56, record size 68). Index 0 = id
    // 26 and index 26 = id 67 both ship PK###H + PK###A atlases: home kit of the first, away of the latter.
    auto db = sim::loadFcdb(root);
    if (db.teams.size() <= std::size_t(teamIndex[1])) { std::cerr << "skip: FCDB has " << db.teams.size() << " teams\n"; return false; }

    md.sheets[0] = sim::makeTeamSheet(db, std::size_t(teamIndex[0]));
    md.sheets[1] = sim::makeTeamSheet(db, std::size_t(teamIndex[1]));
    if (!sim::loadFormationTables(md, root)) { std::cerr << "skip: GEN4TBLS.VIV formation tables unreadable\n"; return false; }
    if (md.sheets[0].squad.size() < 11 || md.sheets[1].squad.size() < 11) { std::cerr << "skip: squad under 11 players\n"; return false; }

    // HUD strip names: FCDB STR0 (COMMON/FCDB_ENG.DBI) rows pair 1:1 with the FCDB.DBI team order.
    try {
        const formats::FcdbNames nm = formats::FcdbNames::parse(data->file("COMMON/FCDB_ENG.DBI"));
        for (int t = 0; t < 2; ++t)
            if (std::size_t(teamIndex[std::size_t(t)]) < nm.teams.size()) {
                abbrv[std::size_t(t)] = nm.teams[std::size_t(teamIndex[std::size_t(t)])].abbreviation;
                names[std::size_t(t)] = nm.teams[std::size_t(teamIndex[std::size_t(t)])].fullName; // IG board typ1/typ2 captions
            }
    } catch (const std::exception& e) { std::cout << "hud: no STR0 abbreviations (" << e.what() << ")\n"; }

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
          : n == "igteam79" ? 12 : n == "igteam80" ? 13 : n == "igteam89" ? 14 : n == "manmarks" ? 15 : 0; // §D highlights + §E IGTEAM management mirrors + MANMARKS ratings board (IGHILIT1 = dummy hotkey-transition page, LAY comment)
    page_ = 0;
    hlSel_ = 0;
    mgSel_ = 0;
    return kind_ != 0;
}

void IgBoards::movePage(int dir) {
    if (!kind_) return;
    if (kind_ >= 8) return; // IGTEAM pages: the gobk/gofd page cycle is caller-side (openBoard chain)
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

presentation::FeDrawStats IgBoards::draw(render::SpriteBatch& ui, const IgMatchData& d) {
    using namespace presentation;
    FeDrawStats st;
    if (!kind_) return st;
    // Scene-ownership whitelist: everything shown() except the static textid soup slots.
    std::vector<std::string> excl;
    if (kind_ == 1) excl = {"titl", "ty12", "ty13", "ty14", "ty15", "ty16", "typ8", "typ9"}; // ty12.. static engine captions
    else if (kind_ >= 3) excl = {"titl", "typ3", "typ4", "typ5", "typ8", "typ9", "up", "down"}; // header soup + list arrows
    std::vector<std::string> visible;
    for (const auto& o : sc_.objects) {
        if (!o.shown()) continue;
        const std::string n = lowerName(o.name);
        if (std::ranges::find(excl, n) != excl.end()) continue;
        visible.push_back(n); // composite() applies the whitelist by name (duplicates included)
    }
    st = composite(sc_, ui, art_, text_.get(), &visible);
    const auto cap = [&](std::size_t id) { return text_ && text_->ready() ? std::string(text_->stringAt(id)) : std::string(); };

    // Title (box1 x=116 w=408): FC-bank caption centered across the header pane.
    static constexpr int kTitle[5] = {0, 28, 858, 776, 1553}; // Match Statistics / HALF-TIME / Full-Time / SCORE SUMMARY
    if (const formats::LayoutObject* t = layObj(sc_, "titl")) st.texts += paintCentered(text_.get(), ui, cap(std::size_t(kTitle[kind_])), 320.f, float(t->y) + 8.f, 400.f);

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
        // Page button captions + active-page chrome (btn1 = page 0 top-left, btn2 = page 1 below).
        static constexpr std::size_t kPageCap[2] = {1771, 329}; // 'Statistics' / 'Booking Summary'
        for (int p = 0; p < 2; ++p) {
            const formats::LayoutObject* box = layObj(sc_, p == 0 ? "box6" : "box7");
            if (box) st.texts += paintCentered(text_.get(), ui, cap(kPageCap[std::size_t(p)]), float(box->x) + 84.f, float(box->y) + 8.f, 170.f,
                                               p == page_ ? render::packRgba(255u, 236u, 90u, 255u) : render::packRgba(255u, 236u, 190u, 255u));
            if (p == page_) { // pressed chrome on the active page button ('igbC' grammar: btnd over btnn)
                if (const formats::LayoutObject* b = layObj(sc_, p == 0 ? "btn1" : "btn2")) {
                    const ScreenArt::Shape sh = art_.shape("..\\global\\", "INGAME.FSH", "btnd", -1); // button.fsh stem alias
                    if (sh.tex) { ui.draw(sh.tex, {float(b->x), float(b->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
                }
            }
        }
    } else { // summary list: Time / Player / Score columns over lst1..lst3 (objecttype 9)
        static constexpr std::array<const char*, 3> kCols{"lst1", "lst2", "lst3"};
        static constexpr std::size_t kHeadIds[3] = {1862, 1351, 1603}; // 'Time' / 'Player' / 'Score'
        for (int c = 0; c < 3; ++c) {
            const formats::LayoutObject* lst = layObj(sc_, kCols[std::size_t(c)]);
            if (!lst) continue;
            st.texts += paintCentered(text_.get(), ui, cap(kHeadIds[std::size_t(c)]), float(lst->x) + float(std::max(45, lst->width)) * 0.5f, 143.f, 120.f);
            std::vector<std::string> rows;
            for (const auto& g : d.goals) {
                if (c == 0) rows.push_back(std::format("{}'", g.minute));
                else if (c == 1) rows.push_back(std::format("#{}{}", g.shirtNo, g.penalty ? " pen" : g.ownGoal ? " og" : ""));
                else rows.push_back(std::format("{}-{}", g.scoreAfter[0], g.scoreAfter[1]));
            }
            if (!rows.empty()) paintListRows(ui, *lst, rows, -1, *text_, 4, st);
        }
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
    std::vector<std::string> excl = kind_ == 6 ? std::vector<std::string>{"titl", "typ6", "typ8"}
                                               : std::vector<std::string>{"titl", "typ4", "typ5", "typ6", "typ7"};
    std::vector<std::string> visible;
    for (const auto& o : sc_.objects) {
        if (!o.shown()) continue;
        const std::string n = lowerName(o.name);
        if (std::ranges::find(excl, n) != excl.end()) continue;
        visible.push_back(n); // composite() applies the whitelist by name (duplicates included)
    }
    st = composite(sc_, ui, art_, text_.get(), &visible);
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


int runLiveLoop(const fs::path& tree, const std::string& prefix, int smokeFrames,
                platform::Window& window, render::Renderer& renderer, bool recordReplay, bool igPauseSmoke, int boardScript) {
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
    knob("hud_sb_x", geo.sbX); knob("hud_sb_w", geo.sbW); knob("hud_sb_y", geo.sbY); knob("hud_sb_h", geo.sbH);
    knob("hud_name_cx0", geo.nameCx[0]); knob("hud_name_cx1", geo.nameCx[1]);
    knob("hud_score_cx", geo.scoreCx); knob("hud_clock_cy", geo.clockCy);
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
    int controlledPid = -1;

    // Commentary: mixer is best-effort — headless CI has no OpenAL device, and the adapter then
    // runs decode-only (queue advances on silent completion).
    std::unique_ptr<audio::Mixer> mixer;
    try { mixer = std::make_unique<audio::Mixer>(); } catch (const std::exception& e) { std::cout << "commentary: no audio device (" << e.what() << ")\n"; }
    app::Commentary commentary(mixer.get());
    const bool haveSpeech = commentary.loadTree(tree);
    if (!haveSpeech) std::cout << "commentary: AUDIO/PHRS/PHRS.PBK unavailable, queue idle\n";
    std::size_t evPos = 0; // World::events is append-only (world.hpp emit)
    // Runtime SFX (docs/reverse-engineering/AUDIO_RUNTIME.md): crowd ambience loop for the whole
    // match + whistle/kick one-shots off the same event seam, independent of speech availability.
    app::MatchAudio matchAudio(tree);
    matchAudio.setMixer(mixer.get());
    matchAudio.startCrowd();
    std::size_t sfxPos = 0; // SFX sweep cursor (append-only events stream)

    auto key = [&](SDL_Scancode sc, bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.scancode = sc;
        e.key.down = down;
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
    enum class IgPause { Off, Menu, QuitAsk, Board }; // + IG presentation boards (IG_FAMILIES_SPEC.md §C)
    IgPause pause = IgPause::Off;
    int igSel = 0;                       // menu rows: 0 Resume, 1 Stats, 2 Highlights, 3 Management (IGTEAM §E), 4 Quit (original item set is code-driven, undecoded captions)
    bool igReported = false, iqReported = false, iqShot = false;
    std::int64_t prevSim = int(w.time.simFrames); // camera delta source (DAT_0065F1E0 frames/tick)
    // IG presentation boards: composite over the live/paused frame from last-match data. igstats opens
    // from the IGMENU Stats row, ighalf at half-time whistle, full time = igscsum then igflsum [C]; Esc
    // dismisses/resumes (payload grammar ESC=0x65), L/R moves pages ('igbC' btn1..btn4 FUN_0043a3f4).
    std::unique_ptr<IgBoards> boards;
    int boardSrc = 0;                    // open source: 1 stats menu, 2 half-time, 3 full-time chain, 4 highlights browser (menu), 5 save list (browser 'save' button), 6 IGTEAM management pages (§E)
    bool boardReported[16] = {}, halfShown = false, ftShown = false;
    int boardShotAt = -1;                // capture frame of the current board (gate auto-script pacing)
    static constexpr std::array<const char*, 16> kBoardName{"", "IGSTATS", "IGHALF", "IGFLSUM", "IGSCSUM", "IGHILITE", "IGHILSAV", "IGHILIT1", "IGTEAM72", "IGTEAM73", "IGTEAM74", "IGTEAM75", "IGTEAM79", "IGTEAM80", "IGTEAM89", "MANMARKS"};
    const int initPeriod = w.match.periodIndex; // DAT_0065F224: 0 -> >=1 transition marks the half whistle
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
            if (src == 1 || src == 6) pause = IgPause::Menu; // IGSTATS / management pages: layout absent -> back to the menu
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
    auto buildIgMgmt = [&] {
        IgMgmtData d;
        static std::unique_ptr<formats::FcdbDatabase> db;   // FCDB.DBI + STR1 player names, loaded once
        static std::vector<std::string> pnames;
        static bool loaded = false;
        if (!loaded) {
            loaded = true;
            try { const auto p = wc98::findCaseInsensitive(tree, "COMMON/FCDB.DBI"); if (!p.empty()) db = std::make_unique<formats::FcdbDatabase>(formats::FcdbDatabase::parse(wc98::readFile(p))); } catch (const std::exception&) {}
            try { const auto p = wc98::findCaseInsensitive(tree, "COMMON/FCDBPENG.DBI"); if (!p.empty()) pnames = formats::FcdbNames::parse(wc98::readFile(p)).players; } catch (const std::exception&) {}
        }
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
                if (db) { // STR1 names are parallel to FCDB.DBI players (FC-bank rule of the league rows)
                    const auto it = std::ranges::find(db->players, p.id, &formats::FcdbPlayer::id);
                    if (it != db->players.end()) { const std::size_t idx = std::size_t(std::distance(db->players.begin(), it)); if (idx < pnames.size()) r.name = pnames[idx]; }
                }
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
        if (igPauseSmoke && presented == 100) key(SDL_SCANCODE_ESCAPE, true); // gate script: Esc opens IGMENU (after the forced kickoff kick ~frame 90)
        if (igPauseSmoke && presented == 130) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); } // cursor x4: Resume -> Stats -> Highlights -> Management -> 'quit' row (five-row domain)
        if (igPauseSmoke && presented == 132) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); }
        if (igPauseSmoke && presented == 134) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); }
        if (igPauseSmoke && presented == 135) key(SDL_SCANCODE_RETURN, true);   // Enter -> IGQUESHN confirm
        if (igPauseSmoke && presented == 136) key(SDL_SCANCODE_RETURN, false);
        if (igPauseSmoke && presented == 145) key(SDL_SCANCODE_ESCAPE, true);   // Esc: QuitAsk back to Menu
        if (igPauseSmoke && presented == 160) key(SDL_SCANCODE_ESCAPE, true);   // Esc closes the menu: match resumes
        // IG board gate scripts. Mode 1 = stats tour through IGMENU (Esc opens, Down -> Stats row, Enter
        // opens the IGSTATS composite, Esc closes to menu, Esc resumes). Modes 2/3 auto-dismiss/auto-advance
        // N frames after each board capture (boardShotAt pacing below).
        if (boardScript == 1) {
            if (presented == 200) key(SDL_SCANCODE_ESCAPE, true);   // IGMENU opens after kickoff progress
            if (presented == 215) key(SDL_SCANCODE_DOWN, true);     // cursor -> Stats row
            if (presented == 216) key(SDL_SCANCODE_DOWN, false);
            if (presented == 222) { key(SDL_SCANCODE_RETURN, true); } // Enter: IGSTATS board
            if (presented == 223) key(SDL_SCANCODE_RETURN, false);
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
            if (presented == 215) key(SDL_SCANCODE_DOWN, true);     // cursor: Resume -> Stats
            if (presented == 216) key(SDL_SCANCODE_DOWN, false);
            if (presented == 218) key(SDL_SCANCODE_DOWN, true);     // Stats -> Highlights row
            if (presented == 219) key(SDL_SCANCODE_DOWN, false);
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
            if (presented == 216) key(SDL_SCANCODE_DOWN, false);
            if (presented == 218) key(SDL_SCANCODE_DOWN, true);     // -> Highlights row
            if (presented == 219) key(SDL_SCANCODE_DOWN, false);
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
            for (int i = 0; i < 3; ++i)                                    // cursor x3: Resume -> Stats -> Highlights -> Management row (index 3 of five-row domain)
                if (presented == 210 + i * 4) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); }
            if (presented == 230) { key(SDL_SCANCODE_RETURN, true); } // Enter: IGTEAM72 formation mirror (FCDB/sim data)
            if (presented == 231) key(SDL_SCANCODE_RETURN, false);
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
            for (int i = 0; i < 3; ++i)                             // cursor x3: -> Management row
                if (presented == 210 + i * 4) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); }
            if (presented == 230) { key(SDL_SCANCODE_RETURN, true); } // Enter: IGTEAM72 (frozen FCDB/sim data)
            if (presented == 231) key(SDL_SCANCODE_RETURN, false);
            if (presented == 246) { key(SDL_SCANCODE_M, true); }      // 'M' -> MANMARKS ratings board (§E)
            if (presented == 247) key(SDL_SCANCODE_M, false);
            for (int i = 0; i < 3; ++i)                              // row cursor: pan window slides
                if (presented == 260 + i * 6) { key(SDL_SCANCODE_DOWN, true); key(SDL_SCANCODE_DOWN, false); }
            if (presented == 300) key(SDL_SCANCODE_ESCAPE, true);     // MANMARKS -> IGMENU (§E Esc grammar)
            if (presented == 315) key(SDL_SCANCODE_ESCAPE, true);     // resume
        }

        for (SDL_Event e; SDL_PollEvent(&e);) { // keyboard + gamepad transitions into the ported layer
            if (e.type == SDL_EVENT_QUIT) quit = true;
            else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE) {
                // Esc opens/leaves IGMENU (global-key hub parity); no IG art on this tree -> direct exit.
                if (pause == IgPause::Board) dismissBoard(); // ESC=0x65 grammar: resume / dismiss to caller [C]
                else if (!haveIgm) quit = true;
                else pause = pause == IgPause::Off ? IgPause::Menu : pause == IgPause::QuitAsk ? IgPause::Menu : IgPause::Off;
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
                switch (e.key.scancode) { // list cursor INC EDX @0x43FCD0: port domain {Resume, Stats, Highlights, Management, Quit}
                    case SDL_SCANCODE_UP:
                        if (pause == IgPause::Board && boards && boards->kind() >= 8) {
                            const int bk = boards->kind();
                            if ((bk == 11 || bk == 12) && mgRows(bk) == 0) mgmt.teams[0].attackBias = std::max(0, mgmt.teams[0].attackBias - 10); // s001 slider (75/79 keyloop_notes: up/down = value)
                            else boards->moveMgmtRow(-1, mgRows(bk));
                        }
                        else if (pause == IgPause::Board && boards && boards->kind() >= 5) boards->moveHl(-1, int(hlRows().size())); // slol: slot arrow
                        else { igSel = (igSel + 4) % 5; } break; // five-row domain: wrap-up to 'resume'
                    case SDL_SCANCODE_DOWN:
                        if (pause == IgPause::Board && boards && boards->kind() >= 8) {
                            const int bk = boards->kind();
                            if ((bk == 11 || bk == 12) && mgRows(bk) == 0) mgmt.teams[0].attackBias = std::min(100, mgmt.teams[0].attackBias + 10);
                            else boards->moveMgmtRow(+1, mgRows(bk)); // slor on the highlights lane; row cursor / pair select on §E pages
                        }
                        else if (pause == IgPause::Board && boards) { if (boards->kind() >= 5) boards->moveHl(+1, int(hlRows().size())); else boards->movePage(-1); } // L/R axis pages the 'igbC' btn1..btn4 pair [C] FUN_0043a3f4
                        else { igSel = (igSel + 1) % 5; } break; // five-row domain cursor
                    case SDL_SCANCODE_LEFT: if (pause == IgPause::Board && boards) { if (boards->kind() >= 8 && boards->kind() <= 13) mgmtCycle(-1); else if (boards->kind() < 8) boards->movePage(-1); } break; // btlt slot page arrow / gobk management chain
                    case SDL_SCANCODE_RIGHT: if (pause == IgPause::Board && boards) { if (boards->kind() >= 8 && boards->kind() <= 13) mgmtCycle(+1); else if (boards->kind() < 8) boards->movePage(1); } break; // btrt / gofd
                    case SDL_SCANCODE_S: if (pause == IgPause::Board && boards && boards->kind() == 5) openBoard("ighilsav", 5); break; // 'save' button -> IGHILSAV slot list
                    case SDL_SCANCODE_B: if (pause == IgPause::Board && boards && boards->kind() >= 8) openBoard("igteam89", 6); else if (pause == IgPause::Menu) { mgmt = buildIgMgmt(); openBoard("igteam89", 6); } break; // tmf3 icon -> management hub (§E)
                    case SDL_SCANCODE_M: if (pause == IgPause::Board && boards && boards->kind() >= 8 && boards->kind() <= 14) openBoard("manmarks", 6); break; // igmgoptns hotkey: man-marking ratings board (§E)
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
                            else dismissBoard();
                        }
                        else if (pause == IgPause::Menu && igSel == 1) openBoard("igstats", 1); // Stats row: live last-match data board
                        else if (pause == IgPause::Menu && igSel == 2) openBoard("ighilite", 4); // Highlights row -> IGHILITE browser (§D)
                        else if (pause == IgPause::Menu && igSel == 3) { mgmt = buildIgMgmt(); openBoard("igteam72", 6); } // Management row: IGTEAM mirror pages (§E), FCDB/sim data frozen on entry
                        else if (pause == IgPause::Menu && igSel == 4) { if (haveIq) pause = IgPause::QuitAsk; else quit = true; } // 'quit' row: IGQUESHN box, no box -> exit
                        else pause = IgPause::Off;                                          // resume / canc
                        break;
                    default: break;
                }
            }
            if (pause == IgPause::Off) in.handle(e); // gameplay inputs never feed the sim while paused
        }
        if (quit) break;

        // The pause menu freezes the simulation while open (the original halts the match loop).
        const std::uint64_t now = SDL_GetTicks();
        if (pause == IgPause::Off) {
            acc += smokeFrames ? kStepMs : double(std::min<std::uint64_t>(now - last, 100)); // spiral guard
            last = now;
            while (acc >= kStepMs) {
                acc -= kStepMs;
                sim::ControllerState c{};
                in.fill(1, c); // pad word of device 1 into ControllerState::pad words
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

        if (haveSpeech && (presented % 2) == 0) { // commentary pump every 2 render frames, trigger seam from events
            const std::uint32_t before = commentary.sentencesStarted();
            for (; evPos < w.events.size(); ++evPos) commentary.trigger(w.events[evPos]);
            if (commentary.sentencesStarted() > before)
                std::cout << "live: commentary " << sim::eventName(w.events[evPos - 1].type)
                          << " -> phrase " << commentary.lastPhrase() << '\n';
            commentary.pump(2); // FUN_0048f920 per-frame pump, 2 engine frames per call
        } else {
            evPos = w.events.size();
        }
        if ((presented % 2) == 0) { // SFX trigger seam: same events, own cursor; counts even with no device
            for (; sfxPos < w.events.size(); ++sfxPos) matchAudio.trigger(w.events[sfxPos]);
        }

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
            FeDrawStats bd = bk >= 8 ? boards->draw(ov, mgmt) : bk >= 5 ? boards->draw(ov, IgHlData{hlRows(), &d}) : boards->draw(ov, d);
            if (!boardReported[std::size_t(bk)]) { // gate evidence line + PNG (legible stat rows)
                boardReported[std::size_t(bk)] = true;
                std::cout << kBoardName[std::size_t(bk)] << ": shapes=" << bd.shapes << " texts=" << bd.texts << '\n';
                boardShotAt = presented; // gate auto-script pacing (loop head)
                boardShot = true;
            }
            scene.pushOverlay(ov); // consumed by the build() below
        } else if (pause != IgPause::Off) {
            FeDrawStats ig = pause == IgPause::Menu ? composite(*igMenu, ov, igArt, igText.get())
                                                     : composite(*igQues, ov, igArt, igText.get());
            if (pause == IgPause::Menu) {
                for (int row = 0; row <= 4; ++row) { // the port's five-row domain {Resume, Stats, Highlights, Management, Quit}
                    std::string tag = "bt"; tag += char('0' + row); tag += row == igSel ? 'd' : 'n';
                    const ScreenArt::Shape sh = igArt.shape("..\\global\\", "INGAME.FSH", tag, -1);
                    if (sh.tex) { ov.draw(sh.tex, {176.f, 138.f + 24.f * float(row), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++ig.shapes; }
                }
                if (!igReported) { std::cout << "IGMENU: shapes=" << ig.shapes << " texts=" << ig.texts << '\n'; igReported = true; }
            } else if (!iqReported) {
                std::cout << "IGQUESHN: shapes=" << ig.shapes << " texts=" << ig.texts << '\n'; iqReported = true;
            }
            scene.pushOverlay(ov); // consumed by the build() below (SpriteBatch::append after the HUD strip)
            if (igPauseSmoke && presented == 110) igShotPending = true; // visual evidence of the composite
        }

        const render::Frame& frame = scene.build(snap, camera.camera(), /*enhanced=*/true);
        if (igShotPending) savePng(renderer, frame, prefix + "_igmenu.png");
        if (boardShot && boards) { std::string nm(lowerName(kBoardName[std::size_t(boards->kind())])); for (char& c : nm) c = char(std::tolower(static_cast<unsigned char>(c))); savePng(renderer, frame, prefix + "_" + nm + ".png"); }
        if (igPauseSmoke && pause == IgPause::QuitAsk && !iqShot) { savePng(renderer, frame, prefix + "_igquesn.png"); iqShot = true; }

        if (smokeFrames && presented == 0) { // gate evidence: the scene drew geometry and the HUD strip
            std::cout << "live: shapes=" << frame.draws.size()
                      << " uiVerts=" << (frame.ui ? frame.ui->vertices().size() : 0u)
                      << " clock=" << snap.hud.clockSeconds << '\n';
            if (frame.draws.empty()) { std::cerr << "live: scene drew no draws\n"; return 1; }
            if (!frame.ui || frame.ui->vertices().empty()) { std::cerr << "live: HUD strip not drawn\n"; return 1; }
        }
        if (smokeFrames && presented == 40) savePng(renderer, frame, prefix + "_mid.png"); // visual evidence

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
    int boardScript = 0; // --ig-stats-smoke 1 / --ig-half-smoke 2 / --ig-full-smoke 3 (gates fe_igstats/fe_ighalf/fe_igscsum/fe_igflsum)
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--tree" && i + 1 < argc) root = argv[++i];
        else if (a == "--live-match-smoke") smoke = std::atoi(argv[++i]);
        else if (a == "--out-prefix" && i + 1 < argc) prefix = argv[++i];
        else if (a == "--ig-pause-smoke") { smoke = std::atoi(argv[++i]); igScript = true; } // gate fe_igmenu
        else if (a == "--ig-stats-smoke") { smoke = std::atoi(argv[++i]); boardScript = 1; } // gate fe_igstats: IGMENU Stats-row tour -> IGSTATS composite
        else if (a == "--ig-half-smoke") { smoke = std::atoi(argv[++i]); boardScript = 2; }   // gate fe_ighalf: 1-min halves, IGHALF opens at the half whistle
        else if (a == "--ig-full-smoke") { smoke = std::atoi(argv[++i]); boardScript = 3; }   // gates fe_igscsum/fe_igflsum: full time -> IGSCSUM Enter -> IGFLSUM
        else if (a == "--ig-hlite-smoke") { smoke = std::atoi(argv[++i]); boardScript = 4; } // gate fe_highlite_browser: Highlights row -> IGHILITE browser, fixture slots + replay-driver play action
        else if (a == "--ig-hlsav-smoke") { smoke = std::atoi(argv[++i]); boardScript = 5; }  // gate fe_hilsav_list: browser 'S' save button -> IGHILSAV list, Enter writes a new slot via recordToUserDir
        else if (a == "--ig-mgmt-smoke") { smoke = std::atoi(argv[++i]); boardScript = 6; } // gate fe_mgmt_pages: Management row -> IGTEAM72..80 cycle + ab-pair swap + hub IGTEAM89 (§E)
        else if (a == "--ig-manmarks-smoke") { smoke = std::atoi(argv[++i]); boardScript = 7; } // gate fe_manmarks_grid: igteam72 'M' -> MANMARKS ratings board + row cursor (§E)
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
    vcfg.createSurface = [&](VkInstance inst) {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window->sdl(), inst, nullptr, &surface)) std::cerr << "SDL_Vulkan_CreateSurface: " << SDL_GetError() << '\n';
        return surface;
    };
    vcfg.drawableSize = [&] { return window->pixelSize(); };

    auto renderer = render::createVulkanRenderer(vcfg, settings);
    const int rc = runLiveLoop(root, prefix, smoke, *window, *renderer, record, igScript, boardScript);
    renderer.reset(); // drain validation messages before the verdict (render-demo pattern)
    if (*messages && rc == 0) { std::cerr << *messages << " validation message(s)\n"; return 3; }
    return rc;
}

} // namespace app
} // namespace wc98
