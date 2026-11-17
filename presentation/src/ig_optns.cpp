#include "wc98/presentation/ig_optns.hpp"

#include "wc98/formats/fsh.hpp"    // globalPalette (caption colour)
#include "wc98/renderer/renderer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <span>
#include <string>
#include <vector>

#include "wc98/presentation/fe_screen.hpp" // paintListRows
#include "wc98/presentation/hud.hpp"

namespace wc98::presentation {

std::vector<IgOptRow> parseIgRows(std::string_view rowsProp) {
    std::vector<int> nums;
    std::size_t p = 0;
    while (p < rowsProp.size()) {
        const std::size_t c = rowsProp.find(',', p);
        std::string_view tok = rowsProp.substr(p, (c == std::string_view::npos ? rowsProp.size() : c) - p);
        const std::size_t n0 = tok.find_first_not_of(" \t");
        if (n0 != std::string_view::npos) {
            tok = tok.substr(n0);
            int v = 0;
            if (std::from_chars(tok.data(), tok.data() + tok.size(), v).ptr == tok.data() + tok.size()) nums.push_back(v);
        }
        if (c == std::string_view::npos) break;
        p = c + 1;
    }
    std::vector<IgOptRow> rows;
    if (nums.size() < 4) return rows;
    std::size_t i = 1; // nums[0] = tuple count
    for (int t = 0; t < nums[0] && i + 1 < nums.size(); ++t) {
        IgOptRow r;
        r.label = nums[i++];
        r.mode = nums[i++];
        if (r.mode >= 2) { // mode-2 = toggle row: count + FC value indices; mode-1 = text row, no tail
            int cnt = nums[i++];
            while (cnt-- > 0 && i < nums.size()) r.values.push_back(nums[i++]);
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

namespace {

// Centred widget-box caption (same painter grammar as the FE shell's caption lane: palette text
// colour, vertical centring, horizontal centring inside the widget rect).
int paintIgCaption(render::SpriteBatch& ui, FeText* text, const formats::LayoutObject& o,
                   std::string_view s, FeDrawStats& st) {
    if (!text || !text->ready() || s.empty()) return 0;
    const int fid = o.intProp("FONTID", 4);
    const formats::FfnFont* f = text->face(fid);
    render::TextureId ftex = text->faceTexture(fid);
    if (!f || !ftex) { f = text->font(); ftex = text->texture(); }
    if (!f || !ftex) return 0;
    const float sizePx = f->lineHeight ? float(f->lineHeight) : 16.f;
    const int cidx = o.intProp("NORMALCOLOUR", -1);
    std::uint32_t rgba = render::packRgba(255u, 236u, 190u, 255u);
    if (const auto& pal = formats::globalPalette(); cidx >= 0 && std::size_t(cidx) < pal.size()) {
        const auto& c = pal[std::size_t(cidx)];
        if (int(c[0]) + int(c[1]) + int(c[2]) > 240) rgba = render::packRgba(c[0], c[1], c[2], 0xFF);
    }
    auto quads = layoutText(*f, s, float(o.x), float(o.y) + (float(o.height) - sizePx) * 0.5f, sizePx, rgba);
    if (quads.empty()) return 0;
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& q : quads) minX = std::min(minX, q.dst.x), maxX = std::max(maxX, q.dst.x + q.dst.w);
    const float off = float(o.x) + (float(o.width) - (maxX - minX)) * 0.5f - minX;
    int n = 0;
    for (auto& q : quads) { q.dst.x += off; ui.draw(ftex, q.dst, q.uv, q.rgba); ++n; }
    st.texts += n;
    return n;
}

// Tab-button caption: clip to the widget box (this media's tab ids are long dev strings), centred.
void paintIgTab(render::SpriteBatch& ui, FeText* text, const formats::LayoutObject& o,
                const std::string& tid, FeDrawStats& st) {
    int idx = 0;
    std::from_chars(tid.data(), tid.data() + std::min<std::size_t>(tid.size(), 8), idx);
    std::string s(text->stringAt(idx)); // e.g. the game-tab caption (1174)
    if (s.empty()) return;
    const formats::FfnFont* f = text->face(4);
    render::TextureId ftex = text->faceTexture(4);
    if (!f || !ftex) { f = text->font(); ftex = text->texture(); }
    if (!f || !ftex) return;
    const float px = f->lineHeight ? float(f->lineHeight) : 16.f;
    while (s.size() > 1 && textWidthPx(*f, s, px) > float(o.width) - 8.f) s.pop_back();
    const float w = textWidthPx(*f, s, px);
    for (const auto& q : layoutText(*f, s, float(o.x) + (float(o.width) - w) * 0.5f,
                                    float(o.y) + (float(o.height) - px) * 0.5f, px,
                                    render::packRgba(235, 235, 235, 255)))
        ui.draw(ftex, q.dst, q.uv, q.rgba);
    ++st.texts;
}

} // namespace

void paintIgOptns(render::SpriteBatch& ui, const formats::ScreenLayout& sc, ScreenArt& /*art*/,
                  FeText* text, const std::vector<IgOptRow>& rows, const std::vector<int>& vals,
                  int curRow, bool audioTabs, FeDrawStats& st) {
    if (!text || !text->ready()) return;
    // List rows: label + value captions resolve through the FC remap lane (fc_painter.txt:1231-1257
    // [C]: s16 label at rows+0x40; >=0 -> FUN_0043c9a0 remap -> FUN_004c31d0 bank; <0 -> FUN_0047a8b4
    // dynamic string, not state-fed here). The trio's ids (2000+) are remap sentinels: the accessor
    // reads heap[-4] (bank is runtime-allocated, FUN_004c3150), so retail's caption is heap-dependent
    // and the trio ids likely never reach this painter [H]; the port renders "" for sentinels [H].
    if (const formats::LayoutObject* box = sc.object("list")) {
        std::vector<std::string> caps;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const IgOptRow& r = rows[i];
            std::string cap;
            if (r.label >= 0) { if (const std::string_view s = text->widgetStringAt(std::size_t(r.label)); !s.empty()) cap = std::string(s); }
            if (!r.values.empty() && i < vals.size()) {
                const int v = r.values[std::size_t(vals[i]) % r.values.size()]; // value caption suffix [H]
                if (v >= 0) { if (const std::string_view s = text->widgetStringAt(std::size_t(v)); !s.empty()) cap += "  " + std::string(s); }
            }
            caps.push_back(std::move(cap));
        }
        const int fontId = box->intProp("TEXTFONT", 2); // list widget textFont=2 (measured)
        const int rowH = box->intProp("ROWHEIGHT", 36); // slots that fit the list box (engine clips + scrolls)
        const int vis = std::max(1, (box->height - 2) / rowH);
        const int top = std::min(std::max(0, curRow - vis + 1), std::max(0, int(caps.size()) - vis));
        std::vector<std::string> view;
        for (int i = top; i < std::min(int(caps.size()), top + vis); ++i) view.push_back(caps[std::size_t(i)]);
        paintListRows(ui, *box, std::span(view), curRow - top, *text, fontId, st);
    }
    // Tab buttons: objecttype-4 widgets with TEXTID are dynamic content -> composite skips the label;
    // paint the bank captions over them (textid=1174/1175/1144 measured on go_g/go_a/go_c). The audio
    // popup names its three tab buttons "2", "3", "04" (measured igaoptns.lay widget map).
    const char* kTab[3] = {"go_g", "go_a", "go_c"};
    if (audioTabs) { kTab[0] = "2"; kTab[1] = "3"; kTab[2] = "04"; }
    for (int i = 0; i < 3; ++i) {
        const formats::LayoutObject* o = sc.object(kTab[i]);
        if (!o) continue;
        if (const std::string* t = o->prop("TEXTID")) paintIgTab(ui, text, *o, *t, st);
    }
    // ok/canc/defa: art PROVEN ABSENT (igicon.fsh#chck dead reference, analysis/pass13/IGOPTNS_ART.md)
    // -> paint the widgets' own help= FC captions as text over their boxes; no invented placeholder art.
    const char* kCtl[3] = {"ok", "canc", "defa"}; // control widgets (chck art dead on this media)
    const int kCap[3] = {60, 62, 1176};           // their help= FC-bank caption ids (measured)
    for (int i = 0; i < 3; ++i) {
        const formats::LayoutObject* o = sc.object(kCtl[i]);
        if (!o) continue;
        std::string s(text->stringAt(kCap[i]));
        if (s.empty()) continue;
        // Same widget-box clip as the engine's font painter: this media's ids are long dev strings.
        const formats::FfnFont* f = text->face(4);
        if (f && f->lineHeight) {
            const std::size_t maxChars = static_cast<std::size_t>(std::max(1.f, float(o->width) - 8.f) / (float(f->lineHeight) * 0.62f));
            if (s.size() > maxChars) s.resize(maxChars);
        }
        paintIgCaption(ui, text, *o, s, st);
    }
}

// ---- IGOPTN2 board (pass-21igscn) --------------------------------------------------------------
// FE/INGAME/IGOPTN2.LAY: one window, four tab panes lst1..lst4 (objecttype 17, rows= grammar
// label,mode,count,ids; mode 1 = caption-only rows like ",4175,1 Strictness"). Widget fname
// opslider.fsh = FE/PACK/FEFLOW15/OPSLIDER.PAK tags unsl/sllf/btnn/sbtn (measured tag list):
// hotspots=198,0,218,0,218,0 -> track x = box.x+198 (absolute 290); unsl track 188x17 at (x,+1),
// fill sllf fraction f = v/(n-1) dst w 185*f, knob btnn 12x12 at (198+(185-12)*f, +1+(17-12)/2),
// sbtn at the right end of the track. Row captions = label + current value caption (FC bank ids,
// widgetStringAt remap lane), windowed at ROWHEIGHT stride by paintListRows. Help pane = the
// active tbb's help= caption widget ids 78/81/1299/82 -> remap FC 1037/798/486/267 "Match
// Options"/"Gameplay Options"/"Controller Options"/"Audio/Visual Options" (FCENG.BIN.json).
void paintIgOptns2(render::SpriteBatch& ui, const formats::ScreenLayout& sc, ScreenArt& art,
                   FeText* text, const std::vector<IgOptRow>& rows, const std::vector<int>& vals,
                   int curRow, int tab, FeDrawStats& st) {
    if (!text || !text->ready()) return;
    static constexpr std::array<const char*, 4> kLst{"lst1", "lst2", "lst3", "lst4"};
    static constexpr int kHelpId[4] = {78, 81, 1299, 82}; // tbb1..4 help= FC captions (measured)
    const int t = std::clamp(tab, 0, 3);
    const formats::LayoutObject* box = sc.object(kLst[std::size_t(t)]);
    if (!box) return;
    const int rowH = box->intProp("ROWHEIGHT", 21);
    std::vector<std::string> caps;
    caps.reserve(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        std::string cap;
        if (rows[i].label >= 0) cap = std::string(text->widgetStringAt(std::size_t(rows[i].label)));
        if (!rows[i].values.empty() && i < vals.size()) {
            const int v = rows[i].values[std::size_t(vals[i]) % rows[i].values.size()];
            if (v >= 0) {
                if (const std::string_view s = text->widgetStringAt(std::size_t(v)); !s.empty()) {
                    if (!cap.empty()) cap += ' ';
                    cap += s;
                }
            }
        }
        caps.push_back(std::move(cap));
    }
    const int vis = std::max(1, (box->height - 4) / rowH);
    const int top = std::clamp(curRow - vis + 1, 0, std::max(0, int(caps.size()) - vis));
    std::vector<std::string> view;
    for (int i = top; i < std::min(int(caps.size()), top + vis); ++i) view.push_back(caps[std::size_t(i)]);
    // Slider art (mode-2 rows) paints under the captions: the engine draws the track row first,
    // then the label/value text on top (opslider.fsh = OPSLIDER.PAK, stem lookup by fname).
    std::string stem = box->fname;
    if (const std::size_t dot = stem.find_last_of('.'); dot != std::string::npos) stem = stem.substr(0, dot);
    const std::string vpath = box->prop("VPATH") ? *box->prop("VPATH") : std::string();
    const int trackX = box->x + 198; // hotspots=198,0,... -> absolute 290 (IGOPTN2 lst1..4)
    const ScreenArt::Shape trk = art.shape(vpath, stem, "unsl", -1);
    const ScreenArt::Shape fill = art.shape(vpath, stem, "sllf", -1);
    const ScreenArt::Shape knob = art.shape(vpath, stem, "btnn", -1);
    const ScreenArt::Shape sbtn = art.shape(vpath, stem, "sbtn", -1);
    for (std::size_t i = 0; i < view.size(); ++i) {
        const std::size_t row = std::size_t(top) + i;
        if (row >= rows.size() || rows[row].values.empty()) continue; // mode 1: caption only
        const float y = float(box->y) + float(i) * float(rowH) + 1.f;
        const int n = std::max(1, int(rows[row].values.size()));
        const float f = n > 1 ? float(row < vals.size() ? vals[row] : 0) / float(n - 1) : 0.f;
        if (trk.tex) ui.draw(trk.tex, {float(trackX), y, float(trk.w), float(trk.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu), ++st.shapes;
        if (fill.tex && f > 0.f) {
            const float w = 185.f * f; // fill fraction of the 185px track (unsl art is 188 wide)
            ui.draw(fill.tex, {float(trackX), y, w, float(fill.h)}, {0.f, 0.f, f, 1.f}, 0xFFFFFFFFu), ++st.shapes;
        }
        if (knob.tex) ui.draw(knob.tex, {float(trackX) + (185.f - float(knob.w)) * f, y + (17.f - float(knob.h)) * 0.5f, float(knob.w), float(knob.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu), ++st.shapes;
        if (sbtn.tex) ui.draw(sbtn.tex, {float(trackX) + 185.f, y + (17.f - float(sbtn.h)) * 0.5f, float(sbtn.w), float(sbtn.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu), ++st.shapes;
    }
    paintListRows(ui, *box, std::span(view), curRow - top, *text, box->intProp("TEXTFONT", 0), st);
    // Help pane: the active tab's help= caption (tbb1..4 grammar, objecttype 32).
    if (const formats::LayoutObject* help = sc.object("help")) {
        if (std::string_view s = text->widgetStringAt(std::size_t(kHelpId[t])); !s.empty())
            paintIgCaption(ui, text, *help, s, st);
    }
}

} // namespace wc98::presentation
