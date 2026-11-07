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
} // namespace
FeDrawStats composite(const formats::ScreenLayout& screen, render::SpriteBatch& ui, ScreenArt& art,
                      FeText* text, const std::vector<std::string>* visible) {
    // Dynamic-content rule (LAY_COMPOSITOR_NOTES.md "Widget-class visibility"): widget-class
    // objects carrying a TEXTID key are engine-driven content — MNMSETUP's list widgets
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
        // that one pane starts hilited, drawing its label in HILITECOLOUR and its art in
        // HILITEDSHAPEID (button class key table 0x564458; runtime state switch FUN_0044e178).
        const bool hl = !screen.hilitePane.empty() && iequals(o.name, screen.hilitePane);
        if (visible && std::ranges::none_of(*visible, [&](const std::string& n) { return iequals(o.name, n); })) { ++st.hidden; continue; }
        const std::string* tag = nullptr;
        if (hl) { if (const std::string* h = o.prop("HILITEDSHAPEID"); h && !h->empty() && !iequals(*h, "null")) tag = h; }
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
        const ScreenArt::Shape sh = art.shape(vpath ? *vpath : std::string(), o.fname, tag ? *tag : std::string(), index);
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


} // namespace wc98::presentation
