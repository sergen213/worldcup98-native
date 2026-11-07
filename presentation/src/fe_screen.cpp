// FE screen composite implementation, ported from tools/render-demo/main.cpp (M4 slice 2); the
// reference compositor now runs on this code. Evidence for every rule: LAY_COMPOSITOR_NOTES.md.
#include "wc98/presentation/fe_screen.hpp"
#include "wc98/presentation/data.hpp"
#include "wc98/presentation/hud.hpp"

#include "wc98/formats/big.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/file.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>

namespace wc98::presentation {
namespace fs = std::filesystem;
using formats::iequals;

std::string slashes(std::string_view s) {
    std::string o(s);
    for (char& c : o) if (c == '\\') c = '/';
    return o;
}

std::uint32_t argbOf(std::string_view v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
    if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v = v.substr(2);
    else if (!v.empty() && (v.front() == 'H' || v.front() == 'h')) v = v.substr(1);
    return std::uint32_t(std::strtoul(std::string(v).c_str(), nullptr, 16));
}

namespace {
std::string lowered(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isNullArt(const fs::path& name) {
    const std::string base = lowered(name.stem().string());
    return base.empty() || base.starts_with("null");
}
} // namespace

void ScreenArt::indexTree() {
    indexed_ = true;
    if (root_.empty()) return;
    for (const auto& e : fs::recursive_directory_iterator(root_, fs::directory_options::skip_permission_denied)) {
        std::error_code ec;
        if (!e.is_regular_file(ec) || ec) continue;
        const std::string ext = lowered(e.path().extension().string());
        if (ext != ".fsh" && ext != ".pak") continue;
        byStem_[lowered(e.path().stem().string())].push_back(e.path());
    }
}

const formats::FshFile* ScreenArt::file(const std::string& vpath, const std::string& fname) {
    const std::string key = lowered(slashes(vpath) + "/" + slashes(fname));
    if (auto it = files_.find(key); it != files_.end()) return it->second.get();

    const std::string rel = slashes(vpath) + "/" + slashes(fname);
    fs::path found;
    // 1. the authored path, resolved against the layout's own directory ("..\global\icons.fsh")
    for (const std::string& n : {rel, rel + ".fsh"})
        if (found.empty()) found = findCaseInsensitive(layDir_, n);
    // 2. anywhere in the tree by base name; prefer the authored directory, then unpacked loose art
    if (found.empty() && !root_.empty()) {
        if (!indexed_) indexTree();
        const std::string want = lowered(fs::path(slashes(fname)).stem().string());
        const std::string dirWant = lowered(fs::path(slashes(vpath)).filename().string());
        if (auto it = byStem_.find(want); it != byStem_.end()) {
            auto paths = it->second;
            std::ranges::sort(paths, [](const fs::path& a, const fs::path& b) { return a.string() < b.string(); });
            auto score = [&](const fs::path& p) {
                int s = 0;
                if (!dirWant.empty() && lowered(p.parent_path().filename().string()) == dirWant) s -= 4;
                if (lowered(p.extension().string()) == ".fsh") s -= 2;
                return s;
            };
            std::stable_sort(paths.begin(), paths.end(), [&](const fs::path& a, const fs::path& b) { return score(a) < score(b); });
            if (!paths.empty()) found = paths.front();
        }
    }

    std::shared_ptr<const formats::FshFile> parsed;
    try {
        if (!found.empty()) {
            auto bytes = formats::refpack::decompressIfNeeded(wc98::readFile(found));
            parsed = std::make_shared<const formats::FshFile>(formats::FshFile::parse(bytes));
        }
    } catch (const std::exception& e) {
        std::cerr << "art: " << found << ": " << e.what() << '\n'; // NOLINT: diagnostics channel for the compositor
    }
    files_[key] = parsed;
    return parsed.get();
}

ScreenArt::Shape ScreenArt::shape(const std::string& vpath, const std::string& fname, const std::string& tag, int index) {
    const bool useTag = !tag.empty() && !iequals(tag, "null");
    const std::string key = lowered(slashes(vpath) + "/" + slashes(fname)) + '#' + (useTag ? lowered(tag) : std::to_string(index));
    if (auto it = shapes_.find(key); it != shapes_.end()) return it->second;

    Shape out;
    do {
        const formats::FshFile* f = file(vpath, fname);
        if (!f) { missing_.push_back(key + " (no such art file)"); break; }
        const formats::FshBitmap* b = nullptr;
        if (useTag)
            for (const auto& e : f->entries)
                if (e.bitmap && iequals(e.tag, tag)) { b = &*e.bitmap; break; }
        if (!b && index >= 0 && std::size_t(index) < f->entries.size()) b = f->entries[std::size_t(index)].bitmap ? &*f->entries[std::size_t(index)].bitmap : nullptr;
        if (b && !b->rgba.empty() && b->width && b->height) {
            out.tex = r_.createTexture(render::Image{b->width, b->height, b->rgba}, render::Wrap::Clamp, key);
            out.w = b->width, out.h = b->height;
            break;
        }
        missing_.push_back(key);
    } while (false);
    shapes_[key] = out;
    return out;
}

// ---- Text resources -------------------------------------------------------------------------

FeText::FeText(render::Renderer& r, fs::path tree, std::string lang) : tree_(std::move(tree)), r_(&r) {
    const GameData data{tree_};
    for (char& c : lang) c = char(std::tolower((unsigned char)c)); // FC*.BIN codes are 3 letters
    try { // default face: HUD-proven eras14bi (fontID 7 in the exe table).
        font_ = formats::FfnFont::parse(data.member("FE/PACK/IGFONTS.BIG", "eras14bi.ffn"));
        tex_ = r.createTexture(toImage(font_->atlas), render::Wrap::Clamp, "fe-ffn-face");
    } catch (const std::exception&) {} // tree without fonts: ready() stays false
    faces_.resize(kFontNames.size());
    for (std::size_t id = 0; id < kFontNames.size(); ++id) {
        const std::string member = std::string(kFontNames[id]) + ".ffn"; // exe loader opens "%s.ffn"
        if (kFontNames[id][0] == '\0') continue;                        // slot 9: NULL in the exe table
        for (const char* arc : {"FE/PACK/FEFONTS.BIG", "FE/PACK/IGFONTS.BIG"}) {
            try {
                faces_[id] = std::make_unique<formats::FfnFont>(
                    formats::FfnFont::parse(data.member(arc, member)));
                ++facesCount_;
                break;
            } catch (const std::exception&) {} // not in this archive: try the next
        }
    }
    auto loadStrings = [&](std::string_view code) {
        std::string up = std::string(code);
        for (char& c : up) c = char(std::toupper((unsigned char)c));
        strings_ = formats::FclgStrings::parse(data.file("COMMON/FC" + up + ".BIN")).strings;
    };
    try { loadStrings(lang); lang_ = lang; }
    catch (const std::exception&) { // unknown code: the game's own default bank
        try { loadStrings("eng"); lang_ = "eng"; } catch (const std::exception&) {}
    }
    if (data.exists("FE/PACK/MSGS/FEMSG.BIN")) // absent on this CD: TEXTID names then never resolve
        try { femsg_ = formats::FemsgTable::parse(data.file("FE/PACK/MSGS/FEMSG.BIN")); } catch (const std::exception&) {}
}

const formats::FfnFont* FeText::face(int fontId) const {
    if (fontId < 0 || size_t(fontId) >= faces_.size()) return nullptr;
    return faces_[size_t(fontId)].get();
}

render::TextureId FeText::faceTexture(int fontId) const {
    // One texture per face id, created on first label draw (FE compositor runs single-threaded).
    const formats::FfnFont* f = face(fontId);
    if (!f || !r_) return 0;
    render::TextureId& slot = faceTex_[size_t(fontId)];
    if (!slot) slot = r_->createTexture(toImage(f->atlas), render::Wrap::Clamp,
                                        std::string("fe-ffn-") + kFontNames[size_t(fontId)]);
    return slot;
}

std::string FeText::label(const formats::LayoutObject& o) const {
    if (!ready()) return {};
    const std::string* t = o.prop("TEXTID");
    if (!t || t->empty()) return {};
    int idx;
    char* end = nullptr;
    if (const long n = std::strtol(t->c_str(), &end, 10); end != t->c_str())
        idx = int(n); // "231" / "209,4": leading FCLG string index
    else { // symbol form ("SM_*") -> FEMSG value; no table on this CD
        const auto v = femsg_ ? femsg_->value(t->substr(0, t->find_first_of(", \t"))) : std::nullopt;
        if (!v) return {};
        idx = *v;
    }
    if (idx <= 0 || size_t(idx) >= strings_.size()) return {}; // id 0 = dynamic/dummy (measured textid=0,5)
    return strings_[size_t(idx)]; // cp1252 bytes == FFN glyph codes
}

namespace {
// Draws one object's label through `txt`; returns the number of glyph quads emitted. Placement
// keys measured in FEFLOW05.LAY: plain labels use TEXTX/YOFFSET + ALIGNMENT, list entries use
// "TEXTXY=x,y,a" (a=3 centers within the box). Colour keys index the global FE palette.
int drawLabel(const formats::LayoutObject& o, render::SpriteBatch& ui, const FeText& txt, bool hilited) {
    if (!txt.ready()) return 0;
    const std::string s = txt.label(o);
    if (s.empty()) return 0;
    int xoff = 0, yoff = 0, align = 1;
    int boxw = o.width > 0 ? o.width : o.intProp("PIXELWIDTH", 0);
    if (const std::string* xy = o.prop("TEXTXY")) { // "x,y[,align]"
        char* e = nullptr;
        xoff = int(std::strtol(xy->c_str(), &e, 10));
        if (e && *e == ',') yoff = int(std::strtol(e + 1, &e, 10));
        if (e && *e == ',') align = int(std::strtol(e + 1, &e, 10));
    } else {
        xoff = o.intProp("TEXTXOFFSET", 0);
        yoff = o.intProp("TEXTYOFFSET", 0);
        align = o.intProp("ALIGNMENT", 1);
    }
    int cidx = -1;
    if (const std::string* tc = o.prop("TEXTCOLOUR")) { // list form "c1,c2,c3": first index wins
        char* e = nullptr;
        const long n = std::strtol(tc->c_str(), &e, 10);
        if (e != tc->c_str()) cidx = int(n);
    }
    // Highlight state (initial pane = [screen] HILITEPANE): the widget repaints its label with
    // HILITECOLOUR. Parse evidence: 0x46e10b maps NORMALCOLOUR/HILITECOLOUR to widget fields
    // +0x68/+0x6C (global-palette indices); listbox draw FUN_00477f54 @0x477f54 picks the hilite
    // text path when the row is the selected one. Falls back to NORMALCOLOUR.
    if (hilited) { if (const int h = o.intProp("HILITECOLOUR", -1); h >= 0) cidx = h; }
    if (cidx < 0) cidx = o.intProp("NORMALCOLOUR", -1);
    std::uint32_t rgba = render::packRgba(0xFF, 0xFF, 0xFF); // white fallback: palette not loaded
    const auto& pal = formats::globalPalette();
    if (cidx >= 0 && size_t(cidx) < pal.size()) {
        const auto& c = pal[size_t(cidx)];
        rgba = render::packRgba(c[0], c[1], c[2], 0xFF);
    }
    // Font selection: TEXTID "index,fontID" (measured FEFLOW05 form) with explicit FONTID key
    // override; face from the exe font table (FeText::kFontNames), drawn at its native line height.
    int fontId = 4; // FEFLOW05 default measured for plain-textid labels
    if (const std::string* t = o.prop("TEXTID"); t && t->find(',') != std::string::npos)
        fontId = int(std::strtol(t->c_str() + t->find(',') + 1, nullptr, 10));
    fontId = o.intProp("FONTID", fontId);
    const formats::FfnFont* sel = txt.face(fontId);
    if (!sel) sel = txt.font(); // id absent on this tree: HUD-proven face
    const render::TextureId tex = txt.faceTexture(fontId) ? txt.faceTexture(fontId) : txt.texture();
    const formats::FfnFont& f = *sel;
    const float sizePx = f.lineHeight ? float(f.lineHeight) : 16.f;
    const float w = presentation::textWidthPx(f, s, sizePx);
    float px = float(o.x) + xoff;
    if (align == 2) px -= w; // right edge at the offset anchor (measured subtitle: textXoffset=550)
    else if (align >= 3) px += (float(boxw) - w) * 0.5f;
    int n = 0;
    for (const auto& q : presentation::layoutText(f, s, px, float(o.y) + yoff, sizePx, rgba)) {
        ui.draw(tex, q.dst, q.uv, q.rgba);
        ++n;
    }
    return n;
}


namespace {
FeWidgetState widgetStateFor(const formats::LayoutObject& o, const FeUiState* s) {
    using T = formats::LayoutObjectType;
    if (!s) return FeWidgetState::Normal;
    if (!(o.type == int(T::Button) || o.type == int(T::ScrollBar) || o.type == int(T::Tbb) ||
          o.type == int(T::AnimButton) || o.type == int(T::Btn)))
        return FeWidgetState::Normal;
    if (!s->pressed.empty() && iequals(o.name, s->pressed)) return FeWidgetState::Press;
    if ((!s->hover.empty() && iequals(o.name, s->hover)) || widgetHit(o, s->mx, s->my)) return FeWidgetState::Hover;
    return FeWidgetState::Normal;
}

// FRAMES="a,b,c,d": two frame ranges over the shape family — idle (a..b) and state (c..d).
// Measured: nvicons qui1 frames=6,9,6,9 -> animated qui6..qui9; franim 0000 frames=1,15,1,15.
std::pair<int, int> frameRange(const formats::LayoutObject& o, FeWidgetState ws) {
    const std::string* f = o.prop("FRAMES");
    if (!f) return {};
    int v[4] = {0, 0, 0, 0};
    int n = 0;
    const char* p = f->c_str();
    while (n < 4 && *p) {
        char* e = nullptr;
        v[n++] = int(std::strtol(p, &e, 10));
        if (!e || e == p) break;
        p = *e == ',' ? e + 1 : e;
    }
    const bool stateAnim = ws != FeWidgetState::Normal;
    if (stateAnim && n >= 4 && v[3] > v[2]) return {v[2], v[3]};
    if (n >= 2 && v[1] > v[0]) return {v[0], v[1]};
    return {};
}

// Animated shape tag: base "qui1" + ordinal -> "qui6"; "0000" + ordinal -> "0006" (digit-width zero pad).
std::string animatedTag(const std::string& base, int ordinal) {
    const std::size_t d = base.find_last_of("0123456789");
    if (d == std::string::npos) return {};
    std::size_t w = 0;
    while (d - w + 1 > 0 && d - w + 1 <= base.size() && isdigit((unsigned char)base[d - w])) ++w;
    const std::string prefix = base.substr(0, d - w + 1);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%0*ld", int(w), long(ordinal));
    return prefix + buf;
}
} // namespace
} // namespace
// Button-class hit box: POINTS= polygon (Feflow06 fe07 fan) else the layout rect. Public: app-side
// pointer routing hits the same geometry the composite uses for hover/press state switching.
bool widgetHit(const formats::LayoutObject& o, float x, float y) {
    const std::string* pts = o.prop("POINTS");
    if (!pts) return x >= o.x && x < o.x + o.width && y >= o.y && y < o.y + o.height;
    // "N+\n,x,y, x,y, ...": scan every number in order; a leading odd token is the "N+" marker.
    std::vector<float> nums;
    const char* p = pts->c_str();
    while (*p) {
        if (!(isdigit((unsigned char)*p) || *p == '-' || (*p == '.' && isdigit((unsigned char)p[1])))) { ++p; continue; }
        char* e = nullptr;
        nums.push_back(std::strtof(p, &e));
        p = e;
    }
    if (nums.size() >= 3 && nums.size() % 2 == 1) nums.erase(nums.begin()); // "7+" count marker
    std::vector<std::pair<float, float>> poly;
    for (std::size_t i = 0; i + 1 < nums.size(); i += 2) poly.emplace_back(nums[i], nums[i + 1]);
    if (poly.size() < 3) return x >= o.x && x < o.x + o.width && y >= o.y && y < o.y + o.height;
    bool in = false; // even-odd ray rule
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const auto [xi, yi] = poly[i];
        const auto [xj, yj] = poly[j];
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) in = !in;
    }
    return in;
}
FeDrawStats composite(const formats::ScreenLayout& screen, render::SpriteBatch& ui, ScreenArt& art,
                      FeText* text, const std::vector<std::string>* visible, const FeUiState* uiState) {
    // Dynamic-content rule (LAY_COMPOSITOR_NOTES.md "Widget-class visibility"): widget-class
    // objects carrying a TEXTID key are engine-driven content - MNMSETUP's list widgets
    // objecttype=4 textid=0,5/209,4/4396,4, edit boxes objecttype=10/14 (dial/init/machine
    // name), buttons objecttype=2. The engine paints their FC-bank captions at runtime from
    // live item state; the port has no such state, so a static composite must not draw them.
    const auto dynamicWidget = [](const formats::LayoutObject& o) {
        using T = formats::LayoutObjectType;
        return (o.type == int(T::List) || o.type == int(T::Edit) || o.type == int(T::CheckBox) ||
                o.type == int(T::Button)) && o.prop("TEXTID") != nullptr;
    };
    FeDrawStats st;
    for (const auto& o : screen.objects) {
        if (!o.shown()) { ++st.hidden; continue; }
        // Initial highlight state of the screen ([screen] HILITEPANE, parsed at layout.cpp:224):
        const bool hl = !screen.hilitePane.empty() && iequals(o.name, screen.hilitePane);
        if (visible && std::ranges::none_of(*visible, [&](const std::string& n) { return iequals(o.name, n); })) { ++st.hidden; continue; }
        // Widget interaction state (button classes): Press -> PUSHEDSHAPE*, Hover/HILITEPANE ->
        // HILITEDSHAPE*; the released-key chain is the static default (runtime state switch
        // FUN_0044e178, key table 0x564458).
        const std::string* tag = nullptr;
        const FeWidgetState ws = widgetStateFor(o, uiState);
        if (ws == FeWidgetState::Press)
            for (std::string_view k : {"PUSHEDSHAPEID", "PUSHEDSHAPE"})
                if (const std::string* v = o.prop(k); v && !v->empty() && !iequals(*v, "null")) { tag = v; break; }
        if (!tag && (ws == FeWidgetState::Hover || hl))
            for (std::string_view k : {"HILITEDSHAPEID", "HILITEDSHAPE"})
                if (const std::string* v = o.prop(k); v && !v->empty() && !iequals(*v, "null")) { tag = v; break; }
        // Disabled modes (port-side): the engine paints DISABLEDSHAPE* while the widget is inert;
        // hover/press states are suppressed for these names by the caller.
        if (!tag && ws == FeWidgetState::Normal && uiState &&
            std::ranges::any_of(uiState->disabled, [&](const std::string& n) { return iequals(o.name, n); }))
            for (std::string_view k : {"DISABLEDSHAPEID", "DISABLEDSHAPE"})
                if (const std::string* v = o.prop(k); v && !v->empty() && !iequals(*v, "null")) { tag = v; break; }
        if (!tag)
            for (std::string_view k : {"SHAPEID", "SNAME", "RELEASEDSHAPEID", "RELEASEDSHAPE", "NORMALSHAPEID", "NSHAPEID", "BARSHAPEID"})
                if (const std::string* v = o.prop(k); v && !v->empty() && !iequals(*v, "null")) { tag = v; break; }
        const fs::path fname{slashes(o.fname)};
        if (isNullArt(fname) || lowered(fname.stem().string()) == "rect") { // flat colour panel / text label
            std::uint32_t argb = 0;
            for (std::string_view k : {"FILLCOLOUR", "COLOUR", "BOXCOLOUR"})
                if (const std::string* v = o.prop(k); v && !v->empty()) { argb = argbOf(*v); break; }
            const int w = o.width > 0 ? o.width : o.intProp("BOXWIDTH", 0);
            const int h = o.height > 0 ? o.height : o.intProp("BOXHEIGHT", 0);
            bool boxDrawn = false;
            if (argb && w > 0 && h > 0) {
                const std::uint32_t rgba = render::packRgba(std::uint8_t(argb >> 16), std::uint8_t(argb >> 8), std::uint8_t(argb), std::uint8_t(argb >> 24));
                if (o.type == int(formats::LayoutObjectType::FrameRect)) { // 1-pixel outline
                    ui.draw(0, {float(o.x), float(o.y), float(w), 1}, {}, rgba);
                    ui.draw(0, {float(o.x), float(o.y + h - 1), float(w), 1}, {}, rgba);
                    ui.draw(0, {float(o.x), float(o.y), 1, float(h)}, {}, rgba);
                    ui.draw(0, {float(o.x + w - 1), float(o.y), 1, float(h)}, {}, rgba);
                } else {
                    ui.draw(0, {float(o.x), float(o.y), float(w), float(h)}, {}, rgba);
                }
                ++st.rects;
                boxDrawn = true;
            }
            const int t = (text && !dynamicWidget(o)) ? drawLabel(o, ui, *text, hl) : 0;
            st.texts += t;
            if (!boxDrawn && !t) ++st.noArt; // unresolvable label: keep the pre-text count exactly
            continue;
        }
        const std::string* vpath = o.prop("VPATH");
        const int index = tag ? -1 : o.intProp("SHAPEINDEX", o.intProp("DEFAULTSHAPENO", -1));
        ScreenArt::Shape sh = art.shape(vpath ? *vpath : std::string(), o.fname, tag ? *tag : std::string(), index);
        // FRAMES= animation over the shape family: idle range while Normal, second range hovered/
        // pressed (measured nvicons qui1 -> qui6..qui9 pulse; franim 0000 + frames 1,15 -> 0001..0015).
        if (uiState && tag) {
            const auto [lo, hi] = frameRange(o, ws);
            if (hi > lo) {
                const int ordinal = lo + int((uiState->animMs / 150) % unsigned(hi - lo + 1));
                if (const std::string at = animatedTag(*tag, ordinal); !at.empty())
                    if (const ScreenArt::Shape a = art.shape(vpath ? *vpath : std::string(), o.fname, at, -1); a.tex)
                        sh = a;
            }
        }
        if (!sh.tex) { ++st.missing; continue; }
        ui.draw(sh.tex, {float(o.x), float(o.y), float(sh.w), float(sh.h)});
        ++st.shapes;
        if (text && !dynamicWidget(o)) st.texts += drawLabel(o, ui, *text, hl); // static labels on the art
    }
    return st;
}

std::string readLayout(const std::string& spec, fs::path& dir) {
    const std::size_t at = spec.find('@');
    if (at == std::string::npos) {
        dir = fs::path(spec).parent_path();
        auto bytes = wc98::readFile(spec);
        return std::string(bytes.begin(), bytes.end());
    }
    const fs::path archive{spec.substr(0, at)};
    const std::string member = spec.substr(at + 1);
    dir = archive.parent_path();
    auto bytes = formats::refpack::decompressIfNeeded(wc98::readFile(archive));
    const std::span<const std::uint8_t> d{bytes};
    // LAYOUTS.BIG/IGLAY.BIG ship RefPack-wrapped; some CD copies carry the 48-byte prefix variant.
    const std::size_t base = formats::BigArchive::isBig(d) ? 0 : (d.size() > 0x34 && formats::BigArchive::isBig(d.subspan(0x30)) ? 0x30 : 0);
    const formats::BigArchive arc = formats::BigArchive::parse(d.subspan(base));
    for (const auto& e : arc.entries) {
        const std::string name = slashes(e.name);
        if (iequals(name, member) || iequals(fs::path(name).filename().string(), member)) {
            auto raw = formats::refpack::decompressIfNeeded(formats::BigArchive::payload(d.subspan(base), e));
            return std::string(raw.begin(), raw.end());
        }
    }
    throw std::runtime_error(spec + ": no such archive member");
}

// ---- Mouse cursor (MAUS.PAK family) ----------------------------------------------------------
bool loadFeCursor(const fs::path& root, FeCursor& out) {
    out = FeCursor{};
    for (const std::string rel : {"FE/PACK/MAUS.PAK", "FE/MAUS.FSH"})
        if (const auto p = findCaseInsensitive(root, rel); !p.empty())
            try {
                const auto bytes = formats::refpack::decompressIfNeeded(wc98::readFile(p));
                for (const auto& e : formats::FshFile::parse(bytes).entries)
                    if (iequals(e.tag, "curs") && e.bitmap)
                        if (++out.entries == 1 && !e.bitmap->rgba.empty() && e.bitmap->width && e.bitmap->height) {
                            out.bmp = render::Image{e.bitmap->width, e.bitmap->height, e.bitmap->rgba};
                            out.hotX = e.bitmap->centerX; // header hotspot (measured 0,0)
                            out.hotY = e.bitmap->centerY;
                        }
                if (out.bmp.width) return true;
            } catch (const std::exception&) { /* decode failure: absent below */ }
    return false;
}

// ---- League-family widget painters (MENU_PARITY.md §4 rank 7) ---------------------------------
namespace {
void captionColor(const formats::LayoutObject& o, bool hilited, std::uint32_t& rgba) {
    int cidx = hilited ? o.intProp("HILITECOLOUR", -1) : -1;
    if (cidx < 0) cidx = o.intProp("NORMALCOLOUR", -1);
    rgba = render::packRgba(0xFF, 0xFF, 0xFF); // palette not loaded: white
    const auto& pal = formats::globalPalette();
    if (cidx >= 0 && size_t(cidx) < pal.size()) {
        const auto& c = pal[size_t(cidx)];
        rgba = render::packRgba(c[0], c[1], c[2], 0xFF);
    }
}
int drawCaption(const formats::LayoutObject& o, std::string_view s, render::SpriteBatch& ui, const FeText& txt,
                int fontId, float xOff, std::uint32_t rgba, FeDrawStats& st) {
    if (s.empty() || !txt.ready()) return 0;
    const formats::FfnFont* sel = txt.face(fontId);
    if (!sel) sel = txt.font();
    if (!sel) return 0;
    render::TextureId tex = txt.faceTexture(fontId) ? txt.faceTexture(fontId) : txt.texture();
    const float sizePx = sel->lineHeight ? float(sel->lineHeight) : 16.f;
    int n = 0;
    for (const auto& q : presentation::layoutText(*sel, s, float(o.x) + xOff, float(o.y), sizePx, rgba)) {
        ui.draw(tex, q.dst, q.uv, q.rgba);
        ++n;
    }
    st.texts += n;
    return n;
}
} // namespace

std::uint32_t kitColor(int idx) {
    // fifa98edit hslCols HSV triples (analysis/fifa98edit/__init__.py:90, community editor table):
    // the FCDB 5-bit shirt/shorts colour indices index this 23-entry palette.
    static constexpr int kHsv[23][3] = {
        {0, 2, 244},   {2, 206, 170}, {5, 255, 255}, {6, 100, 180}, {12, 200, 255},
        {21, 190, 190},{31, 200, 255},{45, 255, 255},{75, 255, 255},{126, 255, 255},
        {158, 255, 255},{171, 255, 255},{190, 255, 255},{214, 255, 255},{235, 255, 255},
        {255, 160, 255},{255, 78, 255},{255, 11, 255},{255, 0, 255},  {249, 0, 153},
        {249, 0, 153}, {255, 20, 170},{164, 84, 185}};
    if (idx < 0 || idx >= 23) return render::packRgba(0, 0, 0, 0xFF);
    const double h = kHsv[idx][0] / 255.0, s = kHsv[idx][1] / 255.0, v = kHsv[idx][2] / 255.0;
    // colorsys.hsv_to_rgb
    const int i = int(h * 6.0);
    const double f = h * 6.0 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    auto ch = [&](double m) { return std::uint8_t(std::round(255.0 * m)); };
    switch (i % 6) {
        case 0: return render::packRgba(ch(v), ch(t), ch(p), 0xFF);
        case 1: return render::packRgba(ch(q), ch(v), ch(p), 0xFF);
        case 2: return render::packRgba(ch(p), ch(v), ch(t), 0xFF);
        case 3: return render::packRgba(ch(p), ch(q), ch(v), 0xFF);
        case 4: return render::packRgba(ch(t), ch(p), ch(v), 0xFF);
        default: return render::packRgba(ch(v), ch(p), ch(q), 0xFF);
    }
}

void paintRowLabel(render::SpriteBatch& ui, const formats::LayoutObject& row, std::string_view caption,
                   const FeText& txt, int fontId, bool hilited, const formats::FcdbKit1998* kit,
                   FeDrawStats& st) {
    // Row background: the engine repaints the clickenable=1 row under its HILITEDSHAPE state; the
    // port paints a filled pane (same idiom as the FEFLOW73 hilite, list_painter_notes.txt §1).
    const float w = float(row.width > 0 ? row.width : 185), h = float(row.height > 0 ? row.height : 19);
    if (hilited) ui.draw(0, {float(row.x) - 3.f, float(row.y) - 2.f, w + 6.f, h + 4.f}, {}, render::packRgba(255, 224, 96, 70));
    float tx = 2.f;
    if (kit && row.intProp("tricolour", 1)) { // tricolour kit swatch at the row start (§1)
        const float sq = h * 0.5f, sy = float(row.y) + (h - sq) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            const int cidx = i == 0 ? kit->shirtColor1 : i == 1 ? kit->shirtColor2 : kit->shirtColor3;
            ui.draw(0, {float(row.x) + tx, sy, sq, sq}, {}, kitColor(cidx));
            tx += sq + 2.f;
        }
        tx += 4.f;
    }
    std::uint32_t rgba;
    captionColor(row, hilited, rgba);
    drawCaption(row, caption, ui, txt, fontId, tx, hilited ? render::packRgba(16, 16, 40, 255) : rgba, st);
}

void paintSliderThumb(render::SpriteBatch& ui, const formats::LayoutObject& track, int value, int lo, int hi) {
    // Sbar (type 11) is vertical (page indicator), KnobBar (type 19) horizontal; the thumb spans a
    // proportional slice of the BARSHAPEID track art at the value position.
    const std::uint32_t c = render::packRgba(210, 230, 255, 220);
    const int span = hi > lo ? hi - lo : 0;
    const float t = span ? std::clamp(float(value - lo) / float(span), 0.f, 1.f) : 0.f;
    if (track.height > track.width) { // vertical: thumb height is the track/rows slice
        const float th = std::max(6.f, float(track.height) / 8.f);
        ui.draw(0, {float(track.x), float(track.y) + t * (float(track.height) - th), float(track.width > 0 ? track.width : 8), th}, {}, c);
    } else {
        const float tw = std::max(6.f, float(track.width) / 12.f);
        ui.draw(0, {float(track.x) + t * (float(track.width) - tw), float(track.y), tw, float(track.height > 0 ? track.height : 8)}, {}, c);
    }
}

int sliderValueAt(const formats::LayoutObject& track, float x, float y, int lo, int hi) {
    const int span = hi > lo ? hi - lo : 0;
    if (!span) return lo;
    const bool vert = track.height > track.width;
    const float pos = vert ? y - float(track.y) : x - float(track.x);
    const float ext = vert ? float(track.height) : float(track.width);
    if (ext <= 0.f) return lo;
    return lo + int(std::clamp(pos / ext, 0.f, 1.f) * float(span) + 0.5f);
}

void paintListRows(render::SpriteBatch& ui, const formats::LayoutObject& box, std::span<const std::string> captions,
                   int cursorRow, const FeText& txt, int fontId, FeDrawStats& st) {
    // objecttype 9 listbox (widget_slice_spec.md §1): ROWHEIGHT stride rows painted inside the LAY
    // rect; FC-bank captions via FeText::stringAt passed in `captions`; cursor row hilite pane.
    const int rowH = box.intProp("ROWHEIGHT", 0) > 0 ? box.intProp("ROWHEIGHT", 0) : (box.height > 0 && !captions.empty() ? box.height / int(captions.size()) : 16);
    std::uint32_t rgba;
    captionColor(box, true, rgba);
    for (std::size_t i = 0; i < captions.size(); ++i) {
        const float y = float(box.y) + float(i) * float(rowH);
        if (int(i) == cursorRow) ui.draw(0, {float(box.x), y, float(box.width), float(rowH)}, {}, render::packRgba(255, 224, 96, 70));
        formats::LayoutObject line = box; // virtual widget: same x, row y
        line.y = int(y);
        line.height = rowH;
        drawCaption(line, captions[i], ui, txt, fontId, 2.f, rgba, st);
    }
}

} // namespace wc98::presentation
