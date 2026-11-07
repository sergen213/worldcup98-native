// HUD overlay layout gate: pure logic (presentation/src/hud.cpp), no renderer/GPU. Synthetic FFN
// font + snapshot; asserts quad positions, sizes and which glyphs the text layer selects.
#include "check.hpp"

#include "wc98/presentation/hud.hpp"

#include <cctype>
#include <cmath>
#include <string>
#include <vector>

using namespace wc98;
using namespace wc98::presentation;
using formats::FfnFont;
using formats::FfnGlyph;

namespace {

// Atlas 64x160, lineHeight 20. Every printable ASCII code except space maps to a glyph of size
// 5x8, advance 6, at x = (code % 12) * 5, y = (code / 12) * 8; space is left unmapped so the
// layout's blank advance path runs.
FfnFont makeFont() {
    FfnFont f;
    f.lineHeight = 20;
    f.atlas.width = 64;
    f.atlas.height = 160;
    for (unsigned c = '!'; c <= '~'; ++c) {
        FfnGlyph g{};
        g.code = std::uint16_t(c);
        g.width = 5;
        g.height = 8;
        g.advance = 6;
        g.x = std::uint16_t((c % 12) * 5);
        g.y = std::uint16_t((c / 12) * 8);
        f.glyphs.push_back(g); // ascending code order (parse() sorts by code)
    }
    return f;
}

// Quads carrying the "font" key, in layout order.
std::vector<HudQuad> textQuads(const std::vector<HudQuad>& q) {
    std::vector<HudQuad> t;
    for (const auto& x : q)
        if (x.texture == "font") t.push_back(x);
    return t;
}

void clockFormat() {
    // Original strip sprintf is "%02d:%02d" (FUN_00438670@0x43888c, pass10/hud): zero-padded minutes.
    CHECK(formatClock(0) == "00:00");
    CHECK(formatClock(59) == "00:59");
    CHECK(formatClock(60) == "01:00");
    CHECK(formatClock(600) == "10:00");
    CHECK(formatClock(5399) == "89:59"); // DAT_0065F1C8 is seconds in this period, not match time
    CHECK(formatClock(-5) == "00:00");
}

void textLayout() {
    const FfnFont f = makeFont();
    // scale = sizePx/lineHeight = 1 at sizePx 20: one quad per glyph, step advance(6)+tracking(1).
    auto q = layoutText(f, "AB", 10.f, 20.f, 20.f, 0xFFFFFFFFu);
    CHECK(q.size() == 2);
    CHECK(q[0].dst.x == 10.f && q[0].dst.y == 20.f);
    CHECK(q[0].dst.w == 8.f && q[0].dst.z == 5.f); // dst = {x, y, w, h} -> vec4 .z width .w height
    CHECK(q[1].dst.x == 17.f);
    // UV selects the atlas cell of 'A' (code 65: x 25, y 40).
    CHECK(q[0].uv.x == 25.f / f.atlas.width && q[0].uv.y == 40.f / f.atlas.height);
    CHECK(q[0].uv.z > q[0].uv.x && q[0].uv.w > q[0].uv.y);

    // Half scale: sizes halve, step halves.
    auto h = layoutText(f, "AB", 10.f, 20.f, 10.f, 0xFFFFFFFFu);
    CHECK(h.size() == 2 && h[0].dst.z == 2.5f && h[1].dst.x == 13.5f);

    // Unmapped space: no quad, fixed blank advance (6 at scale 1).
    auto s = layoutText(f, "A B", 0.f, 0.f, 20.f, 0xFFFFFFFFu);
    CHECK(s.size() == 2 && s[1].dst.x == 13.f); // A step 7 + blank 6 -> B at 13
}

void scoreboardLayout() {
    const FfnFont f = makeFont();
    HudGeometry g{};
    HudView hud{};
    hud.score = {2, 1};
    hud.clockSeconds = 95.f; // 1:35 in this period
    hud.period = 0;
    hud.teamNames = {"fra", "bra"};
    hud.names = true;

    auto q = layoutScoreboard(hud, f, g);
    CHECK(q.size() > 5); // panel + edges + text
    const auto t = textQuads(q);
    CHECK(!t.empty());
    for (const auto& x : q) { // everything inside the top strip band on the virtual screen
        CHECK(x.dst.x >= g.sbX - 30.f && x.dst.x <= g.sbX + g.sbW + 30.f);
        CHECK(x.dst.y >= 0.f && x.dst.y + x.dst.w <= 480.f);
    }

    // The score string "2-1" contributes exactly three glyphs, centred on the strip centre line.
    auto sq = layoutTextCentered(f, "2-1", g.scoreCx, g.sbY + 8.f, 14.f, 0xFFFFFFFFu);
    CHECK(sq.size() == 3);
    const float w = sq[2].dst.x + sq[2].dst.z - sq[0].dst.x;
    CHECK(std::abs((sq[0].dst.x + w / 2) - g.scoreCx) < 1.5f);

    // Clock text "01:35": five glyphs, minutes zero-padded per FUN_00438670 "%02d:%02d".
    auto cq = layoutText(f, formatClock(95), 0.f, 0.f, 14.f, 0xFFFFFFFFu);
    CHECK(cq.size() == 5);

    // Lowercase names render as the uppercase glyph cells (strip uppercases them).
    HudView lower = hud;
    auto a = layoutScoreboard(lower, f, g);
    lower.teamNames = {"FRA", "BRA"};
    auto b = layoutScoreboard(lower, f, g);
    CHECK(a.size() == b.size());

    // Names off: the strip keeps panel + score + clock only.
    HudView noNames = hud;
    noNames.names = false;
    CHECK(textQuads(layoutScoreboard(noNames, f, g)).size() < textQuads(a).size());

    // Knob check: moving every knob by 100 moves every quad with it.
    HudGeometry shifted = g;
    shifted.sbX += 100.f;
    shifted.nameCx[0] += 100.f;
    shifted.nameCx[1] += 100.f;
    shifted.scoreCx += 100.f;
    auto qs = layoutScoreboard(hud, f, shifted);
    CHECK(qs.size() == q.size());
    for (std::size_t i = 0; i < qs.size(); ++i)
        CHECK(std::abs((qs[i].dst.x - q[i].dst.x) - 100.f) < 1e-3f);
}

void radarLayout() {
    const HudGeometry g{};
    MatchSnapshot s{};
    s.pitchUnits = {4000.f, 5300.f}; // full tele pitch -> panel is the exe size (533,67) 180x120
    s.attackDir = {1, -1};
    s.ball.pos = {0.f, 0.f, 0.f};
    for (auto& p : s.players) p.visible = true;
    s.players[0].team = 0;
    s.players[0].pos = {-2000.f, 0.f, -2650.f}; // width min, length min -> corner of the panel
    s.players[1].team = 0;
    s.players[1].pos = {2000.f, 0.f, 2650.f};   // far corner
    s.players[12].team = 1;
    s.hud.controlled = {3, -1};

    auto q = layoutRadar(s, s.pitchUnits, g);
    HudQuad panel{};
    bool hasPanel = false;
    for (const auto& x : q)
        if (x.texture == "rpit") panel = x, hasPanel = true;
    CHECK(hasPanel);
    // exe-confirmed rect: float block VA 0x55a9a8 -> (533, 67), tele size 180x120.
    CHECK(panel.dst.x == g.radarX && panel.dst.y == g.radarY);
    CHECK(std::abs(panel.dst.z - 180.f) < 1e-3f && std::abs(panel.dst.w - 120.f) < 1e-3f);

    // Ball at centre -> panel centre.
    bool hasBall = false;
    for (const auto& x : q)
        if (x.texture == "ball") {
            hasBall = true;
            CHECK(std::abs(x.dst.x + 4.f - (g.radarX + 90.f)) < 1e-3f);
            CHECK(std::abs(x.dst.y + 4.f - (g.radarY + 60.f)) < 1e-3f);
        }
    CHECK(hasBall);

    // Team dots: keyed by side, one per visible player.
    int t0 = 0, t1 = 0;
    for (const auto& x : q) {
        if (x.texture == "rpl0") ++t0;
        if (x.texture == "rpl1") ++t1;
    }
    CHECK(t0 >= 2 && t1 >= 1);

    // Corner mapping: players[0] at (-width/2, -length/2) lands on the panel corner with team 0
    // attacking +z (its goal line left/bottom), size dotHalf*2.
    bool corner = false;
    for (const auto& x : q)
        if (x.texture == "rpl0" && std::abs(x.dst.x - (g.radarX - g.dotHalf)) < 1e-3f) {
            corner = true;
            CHECK(std::abs(x.dst.y - (g.radarY + panel.dst.w - g.dotHalf)) < 1e-3f);
            CHECK(x.dst.z == x.dst.w && x.dst.z > 0.f);
        }
    CHECK(corner);

    // Controlled selector rides the controlled player.
    bool sel = false;
    for (const auto& x : q)
        if (x.texture == "rsp0") {
            sel = true;
            CHECK(x.dst.z > 0.f && x.dst.w > 0.f);
        }
    CHECK(sel);

    // Half-length pitch: panel scales along its length only, dots stay inside it.
    MatchSnapshot half = s;
    half.pitchUnits = {4000.f, 2650.f};
    auto hq = layoutRadar(half, half.pitchUnits, g);
    for (const auto& x : hq)
        if (x.texture == "rpit") CHECK(std::abs(x.dst.z - 90.f) < 1e-3f && std::abs(x.dst.w - 120.f) < 1e-3f);

    // Flipped attack direction mirrors the dots about the panel centre line.
    MatchSnapshot flip = s;
    flip.attackDir = {-1, 1};
    auto fq = layoutRadar(flip, flip.pitchUnits, g);
    for (const auto& x : q) {
        if (x.texture != "rpl0") continue;
        bool mirrored = false;
        const float want = 2.f * g.radarX + panel.dst.z - x.dst.x - x.dst.z;
        for (const auto& y : fq)
            if (y.texture == "rpl0" && std::abs(y.dst.x - want) < 1e-3f) mirrored = true;
        CHECK(mirrored);
    }
}

} // namespace

int main() {
    clockFormat();
    textLayout();
    scoreboardLayout();
    radarLayout();
    std::puts("hud layout ok");
    return 0;
}
