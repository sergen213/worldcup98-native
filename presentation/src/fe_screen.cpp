// FE screen composite implementation, ported from tools/render-demo/main.cpp (M4 slice 2); the
// reference compositor now runs on this code. Evidence for every rule: LAY_COMPOSITOR_NOTES.md.
#include "wc98/presentation/fe_screen.hpp"

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

FeDrawStats composite(const formats::ScreenLayout& screen, render::SpriteBatch& ui, ScreenArt& art) {
    FeDrawStats st;
    for (const auto& o : screen.objects) {
        if (!o.shown()) { ++st.hidden; continue; }
        const std::string* tag = nullptr;
        for (std::string_view k : {"SHAPEID", "SNAME", "RELEASEDSHAPEID", "RELEASEDSHAPE", "NORMALSHAPEID", "NSHAPEID", "BARSHAPEID"})
            if (const std::string* v = o.prop(k); v && !v->empty() && !iequals(*v, "null")) { tag = v; break; }
        const fs::path fname{slashes(o.fname)};
        if (isNullArt(fname) || lowered(fname.stem().string()) == "rect") { // flat colour panel
            std::uint32_t argb = 0;
            for (std::string_view k : {"FILLCOLOUR", "COLOUR", "BOXCOLOUR"})
                if (const std::string* v = o.prop(k); v && !v->empty()) { argb = argbOf(*v); break; }
            const int w = o.width > 0 ? o.width : o.intProp("BOXWIDTH", 0);
            const int h = o.height > 0 ? o.height : o.intProp("BOXHEIGHT", 0);
            if (!argb || w <= 0 || h <= 0) { ++st.noArt; continue; }
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
            continue;
        }
        const std::string* vpath = o.prop("VPATH");
        const int index = tag ? -1 : o.intProp("SHAPEINDEX", o.intProp("DEFAULTSHAPENO", -1));
        const ScreenArt::Shape sh = art.shape(vpath ? *vpath : std::string(), o.fname, tag ? *tag : std::string(), index);
        if (!sh.tex) { ++st.missing; continue; }
        ui.draw(sh.tex, {float(o.x), float(o.y), float(sh.w), float(sh.h)});
        ++st.shapes;
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

} // namespace wc98::presentation
