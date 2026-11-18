// HUD layout: scoreboard strip, clock and radar quad lists (docs/reverse-engineering/HUD_WC98.md).
// Pure logic; no renderer/GPU access so tests/unit/test_hud.cpp runs it headless.
#include "wc98/presentation/hud.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace wc98::presentation {
namespace {

constexpr float kSpaceAdvance = 6.f; // px at sizePx scale (FFN has no space glyph; knob in layoutText)

[[nodiscard]] const formats::FfnGlyph* glyphAt(const formats::FfnFont& f, unsigned char c) {
    // glyphs are sorted by code (ffn.hpp); binary search.
    auto it = std::lower_bound(f.glyphs.begin(), f.glyphs.end(), c,
                               [](const formats::FfnGlyph& g, unsigned char v) { return g.code < v; });
    return it != f.glyphs.end() && it->code == c ? &*it : nullptr;
}

// Total laid-out width of `text` at scale (shared by layoutText and the centred variant).
[[nodiscard]] float textWidth(const formats::FfnFont& f, std::string_view text, float scale) {
    float w = 0;
    for (char ch : text) {
        const auto* g = glyphAt(f, static_cast<unsigned char>(ch));
        w += (g ? g->advance + 1.f : kSpaceAdvance) * scale; // +1 px tracking, f99 textPrint family
    }
    return w > 0 ? w - 1.f * scale : 0.f; // drop the trailing gap of the last glyph
}

} // namespace

std::vector<HudQuad> layoutText(const formats::FfnFont& font, std::string_view text, float x, float y,
                                float sizePx, std::uint32_t rgba) {
    const int lineH = font.lineHeight > 0 ? font.lineHeight : static_cast<int>(font.atlas.height);
    const float scale = lineH > 0 ? sizePx / static_cast<float>(lineH) : 1.f;
    std::vector<HudQuad> out;
    float cx = x;
    for (char ch : text) {
        const auto* g = glyphAt(font, static_cast<unsigned char>(ch));
        if (!g) { cx += kSpaceAdvance * scale; continue; } // unmapped codepoint: blank fixed advance
        // Pen model (f99 textPrint family): offsets relative to the pen point at line top.
        if (g->width && g->height)
            out.push_back({"font",
                           {cx + g->xOffset * scale, y + g->yOffset * scale,
                            g->width * scale, g->height * scale},
                           {static_cast<float>(g->x) / font.atlas.width,
                            static_cast<float>(g->y) / font.atlas.height,
                            static_cast<float>(g->x + g->width) / font.atlas.width,
                            static_cast<float>(g->y + g->height) / font.atlas.height},
                           rgba});
        cx += (g->advance + 1.f) * scale; // +1 px tracking
    }
    return out;
}

std::vector<HudQuad> layoutTextCentered(const formats::FfnFont& font, std::string_view text, float cx,
                                        float cy, float sizePx, std::uint32_t rgba) {
    const int lineH = font.lineHeight > 0 ? font.lineHeight : static_cast<int>(font.atlas.height);
    const float scale = lineH > 0 ? sizePx / static_cast<float>(lineH) : 1.f;
    return layoutText(font, text, cx - textWidth(font, text, scale) * 0.5f, cy - sizePx * 0.5f, sizePx, rgba);
}
float textWidthPx(const formats::FfnFont& font, std::string_view text, float sizePx) {
    const int lineH = font.lineHeight > 0 ? font.lineHeight : static_cast<int>(font.atlas.height);
    return textWidth(font, text, lineH > 0 ? sizePx / static_cast<float>(lineH) : 1.f);
}

std::string formatClock(int clockSeconds) {
    const int s = std::max(0, clockSeconds);
    return std::format("{:02d}:{:02d}", s / 60, s % 60); // HUD strip draw FUN_00438670@0x43888c sprintf("%02d:%02d", v/60, v%60) — minutes zero-padded (pass10/hud)
}

std::vector<HudQuad> layoutScoreboard(const HudView& hud, const formats::FfnFont& font, const HudGeometry& g) {
    std::vector<HudQuad> q;
    // The exe copies font-table record[0] (eras10) into the text context un-scaled, so draw at
    // native size: sizePx = lineHeight makes layoutText's scale 1.
    const float size = static_cast<float>(font.lineHeight > 0 ? font.lineHeight : 20);
    const auto upper = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    // Pen fields in exe draw order: clock, home name, home score, away name, away score.
    std::array<std::string, 4> fields;
    if (hud.names)
        fields = {upper(hud.teamNames[0]), std::to_string(hud.score[0]),
                  upper(hud.teamNames[1]), std::to_string(hud.score[1])};
    // Panel width = base + per-name-field (measured width + margin), accumulated by the exe
    // only while the name-strip flag is on (FUN_00438670 ebp accumulation, pass-26 disasm).
    float w = g.baseWidth;
    if (hud.names)
        for (const auto& s : fields) w += textWidthPx(font, s, size) + g.gap;
    q.push_back({"", {g.sbX, g.sbY, w, g.sbH}, {0, 0, 1, 1}, g.panelFill}); // exe paints one fill rect, no edge frame
    float pen = g.sbX + g.gap;
    const float y = g.sbY + 2.f; // text top = panel top + 2 (exe `add $0x2` after fill)
    const auto emitAt = [&](const std::string& s) {
        auto v = layoutText(font, s, pen, y, size, g.textColor);
        q.insert(q.end(), v.begin(), v.end());
        pen += textWidthPx(font, s, size) + g.gap;
    };
    emitAt(formatClock(static_cast<int>(std::floor(hud.clockSeconds)))); // clock field: pen then advances by the fixed field
    pen = g.sbX + g.gap + g.clockAdvance;
    if (hud.names)
        for (const auto& s : fields) emitAt(s);
    // Minute countdown chip (pass-39): exe draws it after the name strip, at absolute x =
    // accumulated-width + 20 (0x438958 LEA ESI,[EBP+0x14]), a 30x24 strip fill $0x88000000
    // (rect args ESP+0x40..0x5c) + centred sprintf("%02d", DAT_00641260) (0x4389e9..0x4389fe,
    // field width 0x1e). EBP counts the 1 px glyph-gap the port's measured widths exclude, so
    // the +4 over sbX+w+gap compensates the four name/score fields.
    if (g.minChipMinutes >= 0.f) {
        const int m = std::max(0, static_cast<int>(std::floor(g.minChipMinutes))); // sentinel 9999 -> hidden
        const std::string chip = std::format("{:02d}", m);
        const float chipX = g.sbX + w + g.gap + 4.f;
        q.push_back({"", {chipX, g.sbY, 30.f, g.sbH}, {0, 0, 1, 1}, g.panelFill});
        const float meas = textWidthPx(font, chip, size);
        auto v = layoutText(font, chip, chipX + (30.f - meas) * 0.5f, y, size, g.textColor);
        q.insert(q.end(), v.begin(), v.end());
    }
    return q;
}

std::vector<HudQuad> layoutRadar(const MatchSnapshot& s, const glm::vec2& dims, const HudGeometry& g) {
    std::vector<HudQuad> q;
    // Panel size scales with the pitch (init code: sx = 180/length -> 0x5c619c, sy = 120/width).
    const float len = dims.y > 0 ? dims.y : 1.f, wid = dims.x > 0 ? dims.x : 1.f;
    const float w = g.radarW * std::min(1.f, len / 5300.f), h = g.radarH * std::min(1.f, wid / 4000.f);
    q.push_back({"rpit", {g.radarX, g.radarY, w, h}, {0, 0, 1, 1}, 0xFFFFFFFFu});
    // Game-space -> radar px: x across pitch -> horizontal? The rpit bitmap is a landscape pitch
    // (length along x): map z (pitch length) to screen x, x (width) to screen y. attackDir decides
    // which goal is left; team 0 attacks +z by default (snapshot.hpp), so team 0's own goal (left)
    // puts z=-len/2 at the left edge when attackDir[0] > 0.
    const auto map = [&](const glm::vec3& p, glm::vec2& out) {
        const float u = (p.z + len * 0.5f) / len; // 0..1 along length
        const float v = (p.x + wid * 0.5f) / wid; // 0..1 across width
        const bool flip = s.attackDir[0] < 0;     // team 0 attacks -z: mirror so its goal is left
        out = {g.radarX + (flip ? 1.f - u : u) * w, g.radarY + (1.f - v) * h};
    };
    glm::vec2 b{};
    if (s.ball.visible) {
        map(s.ball.pos, b);
        q.push_back({"ball", {b.x - 4.f, b.y - 4.f, 8.f, 8.f}, {0, 0, 1, 1}, 0xFFFFFFFFu}); // ball marker: MISC 'ball' (no rso* tags in WC98)
    }
    for (const auto& p : s.players) {
        if (!p.visible || p.team > 1) continue;
        glm::vec2 d{};
        map(p.pos, d);
        q.push_back({p.team == 0 ? "rpl0" : "rpl1",
                     {d.x - g.dotHalf, d.y - g.dotHalf, g.dotHalf * 2.f, g.dotHalf * 2.f},
                     {0, 0, 1, 1}, 0xFFFFFFFFu});
    }
    // Controlled-player selector: f99 picks rsp(side*5 + view) from the FSH tag list; side 0 = "rsp0".
    for (int t = 0; t < 2; ++t) {
        const int pid = s.hud.controlled[t];
        if (pid < 0 || pid >= static_cast<int>(s.players.size())) continue;
        glm::vec2 d{};
        map(s.players[pid].pos, d);
        q.push_back({t == 0 ? "rsp0" : "rsp1", {d.x - g.dotHalf, d.y - g.dotHalf, g.dotHalf * 2.f, g.dotHalf * 2.f},
                     {0, 0, 1, 1}, 0xFFFFFFFFu});
    }
    return q;
}


} // namespace wc98::presentation
