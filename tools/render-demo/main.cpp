// Offscreen Vulkan scene rendered through the ported renderer, saved as a PNG.
//   wc98-render-demo out.png            — synthetic checkerboard scene
//   wc98-render-demo out.png <tree-dir> — first FEFLOW02.FSH bitmap decoded from the user's
//                                         original game tree and presented full-frame (M4 slice 1).
//   wc98-render-demo out.png --fsh <path>[@member] [--tree <dir>]
//                                   — decode one SHPI archive (.FSH/.PAK, RefPack-aware) from the user's
//                                         CD tree and present ONE bitmap full-frame. @member is a 4-char
//                                         SHPI entry tag (e.g. @hlsv) or a decimal directory index; without
//                                         it the largest bitmap wins. The shared FE palette is loaded from
//                                         --tree (default $WC98_TREE) so global-palette FE art colours right.
//   wc98-render-demo out.png --lay <spec> [--tree <dir>]
//                                   — front-end screen composite: parse a WC98 .LAY layout, resolve
//                                         each element's FSH shape in the user's tree and draw the
//                                         element list through the SpriteBatch (M4 slice 2). <spec> is
//                                         a loose file or "archive.big@MEMBER.LAY"; --tree defaults to
//                                         the layout's own directory. See docs/reverse-engineering/
//                                         LAY_COMPOSITOR_NOTES.md for the element model.
#include "wc98/renderer/vulkan.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/layout.hpp"
#include "wc98/formats/png.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/file.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace wc98;
using namespace wc98::render;
using formats::iequals;
namespace fs = std::filesystem;

namespace {

// Same vertex conventions as the engine tests: front-facing quad in the XY plane.
MeshData quad(float x0, float y0, float x1, float y1, float z, std::uint32_t rgba) {
    MeshData m;
    m.vertices = {{{x0, y1, z}, {0, 0, 1}, {0, 0}, rgba}, {{x0, y0, z}, {0, 0, 1}, {0, 1}, rgba},
                  {{x1, y0, z}, {0, 0, 1}, {1, 1}, rgba}, {{x1, y1, z}, {0, 0, 1}, {1, 0}, rgba}};
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}

Image checkerboard() { // 4x4 cells of 16 px, white at (0,0)
    Image img{64, 64, std::vector<std::uint8_t>(64 * 64 * 4)};
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) {
            std::uint8_t v = ((x / 16 + y / 16) % 2) ? 0 : 255;
            std::uint8_t* p = &img.rgba[(y * 64 + x) * 4];
            p[0] = p[1] = p[2] = v, p[3] = 255;
        }
    return img;
}

// ---- .LAY screen composite -------------------------------------------------------------------

std::string lowered(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ".LAY" texture paths use Windows separators.
std::string slashes(std::string_view s) {
    std::string o(s);
    for (char& c : o) if (c == '\\') c = '/';
    return o;
}

bool isNullArt(const fs::path& name) {
    const std::string base = lowered(name.stem().string());
    return base.empty() || base.starts_with("null");
}

// "HFFFFFFFF" / "0x80202020": the rectangle colour keys are ARGB hex (FIFA 99 parses them at 0x516724).
std::uint32_t argbOf(std::string_view v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
    if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v = v.substr(2);
    else if (!v.empty() && (v.front() == 'H' || v.front() == 'h')) v = v.substr(1);
    return std::uint32_t(std::strtoul(std::string(v).c_str(), nullptr, 16));
}

// Resolves the texture references of one screen's elements to renderer textures. Art is looked up by
// base name under the user's tree: loose .FSH (FE/GLOBAL/…) or packed as a RefPack-wrapped SHPI .PAK
// (FE/PACK/<family>/<name>.pak), which is what the EXE pack redirector at 0x4798dc produces.
class ScreenArt {
public:
    struct Shape {
        TextureId tex = 0; // 0 = unresolved
        std::uint32_t w = 0, h = 0;
    };

    ScreenArt(Renderer& renderer, fs::path root, fs::path layDir)
        : r_(renderer), root_(std::move(root)), layDir_(std::move(layDir)) {}

    // `tag` is a 4-char SHPI entry name (shapeID/sname); an empty tag falls back to entry `index`.
    [[nodiscard]] Shape shape(const std::string& vpath, const std::string& fname, const std::string& tag, int index);
    // "file:tag" references that produced no bitmap, in first-seen order.
    [[nodiscard]] const std::vector<std::string>& missing() const { return missing_; }

private:
    [[nodiscard]] const formats::FshFile* file(const std::string& vpath, const std::string& fname);
    void indexTree();

    Renderer& r_;
    fs::path root_, layDir_;
    std::map<std::string, std::shared_ptr<const formats::FshFile>> files_; // lower(vpath/fname)
    std::map<std::string, Shape> shapes_;                                  // file key + '#' + tag
    std::map<std::string, std::vector<fs::path>> byStem_;                  // lower base name, built lazily
    bool indexed_ = false;
    std::vector<std::string> missing_;
};

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
        std::cerr << "art: " << found << ": " << e.what() << '\n';
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
            out.tex = r_.createTexture(Image{b->width, b->height, b->rgba}, Wrap::Clamp, key);
            out.w = b->width, out.h = b->height;
            break;
        }
        missing_.push_back(key);
    } while (false);
    shapes_[key] = out;
    return out;
}

// Elements drawn, and why the rest were skipped. Text objects need FFN fonts plus FC*.BIN strings and
// are counted as noArt; that is a separate slice.
struct LayStats {
    int shapes = 0, rects = 0, hidden = 0, noArt = 0, missing = 0;
};

// Draws the merged element list into `ui`. Textured elements are blitted at their shape's native size
// anchored at (xposition, yposition): the layout position wins and WIDTH/HEIGHT is the interaction box
// (FIFA 99 LAYOUT.md "FSH header position fields are authoring positions only"; measured here: nvicons
// 'qui1' is 51x55 in a 55x50 box, aflag '0001' is 193x100 in a 322x106 box). Elements with no art file
// (fname null/RECT) fall back to their flat colour key as a filled or 1-pixel-outlined box.
LayStats composite(const formats::ScreenLayout& screen, SpriteBatch& ui, ScreenArt& art) {
    LayStats st;
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
            const std::uint32_t rgba = packRgba(std::uint8_t(argb >> 16), std::uint8_t(argb >> 8), std::uint8_t(argb), std::uint8_t(argb >> 24));
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

// Reads a layout given as "path" or "archive.big@MEMBER.LAY"; `dir` receives the directory that
// authored vpath references are relative to (the archive's own directory for members).
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

} // namespace

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "wc98-render-demo.png";
    std::string laySpec, treeArg, fshSpec;
    int minShapes = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--lay" && i + 1 < argc) laySpec = argv[++i];
        else if (a == "--fsh" && i + 1 < argc) fshSpec = argv[++i];
        else if (a == "--tree" && i + 1 < argc) treeArg = argv[++i];
        else if (a == "--min-shapes" && i + 1 < argc) minShapes = std::atoi(argv[++i]);
        else if (!a.empty() && a[0] == '-') { std::cerr << "unknown option: " << a << '\n'; return 2; }
        else treeArg = a; // legacy positional: <tree-dir> presents the FEFLOW02 bitmap
    }
    try {
        VulkanConfig cfg;
        cfg.validation = validationRequestedByEnv();
        auto messages = std::make_shared<std::atomic<std::uint32_t>>();
        cfg.validationCounter = messages;

        auto r = createVulkanRenderer(cfg, {}); // no surface: offscreen only

        fs::path root{treeArg};
        if (root.empty() && !fshSpec.empty()) { // --fsh without --tree: $WC98_TREE locates the shared palette
            if (const char* t = std::getenv("WC98_TREE"); t && *t) root = t;
        }
        if (!root.empty()) { // the shared FE palette every paletted FE bitmap indexes into
            auto palPath = wc98::findCaseInsensitive(root, "FE/PACK/PAL.PAK");
            if (palPath.empty()) palPath = wc98::findCaseInsensitive(root, "PAL.PAK");
            if (!palPath.empty()) formats::setGlobalPalette(formats::wc98GlobalPaletteFromPalArchive(wc98::readFile(palPath)));
        }

        std::vector<DrawItem> draws;
        SpriteBatch ui;
        glm::vec4 clearColor{0, 0, 0.2f, 1};

        // Aspect-fit blit of one decoded FSH bitmap over the frame (--fsh and legacy tree modes).
        auto blit = [&](const formats::FshBitmap& b, std::string_view label) {
            const TextureId tex = r->createTexture(Image{b.width, b.height, b.rgba}, Wrap::Clamp, "fsh");
            std::cout << label << ": " << b.width << 'x' << b.height << " " << formats::fshTypeName(b.type)
                      << (b.compressed ? " (RefPack)" : "") << ", palette 0x" << std::hex << int(b.paletteType)
                      << (b.usedGlobalPalette ? " global-FE" : " attached") << std::dec << '\n';
            const float ar = float(b.width) / float(b.height); // aspect-fit over the whole frame
            float w = 640.f, h = w / ar;
            if (h > 480.f) h = 480.f, w = h * ar;
            ui.draw(tex, {(640.f - w) * .5f, (480.f - h) * .5f, w, h});
        };

        if (!fshSpec.empty()) { // one SHPI member full-frame: --fsh <path>[@tag|@index]
            std::string path = fshSpec, member;
            if (const std::size_t at = fshSpec.find('@'); at != std::string::npos) {
                path = fshSpec.substr(0, at);
                member = fshSpec.substr(at + 1);
            }
            const fs::path want{path};
            fs::path file = want;
            if (!fs::exists(file)) file = wc98::findCaseInsensitive(want.parent_path(), want.filename().string());
            if (file.empty()) { std::cerr << "--fsh: no such file: " << path << '\n'; return 2; } // gate skip when tree absent
            const auto bytes = formats::refpack::decompressIfNeeded(wc98::readFile(file));
            const formats::FshFile fsh = formats::FshFile::parse(bytes);
            const bool byIndex = !member.empty() && std::all_of(member.begin(), member.end(), ::isdigit);
            const int index = byIndex ? std::atoi(member.c_str()) : -1;
            auto trimTag = [](const formats::FshEntry& e) { return e.tag.substr(0, e.tag.find_last_not_of(" \t\0") + 1); };
            const formats::FshBitmap* best = nullptr;
            std::string label = file.filename().string();
            for (std::size_t i = 0; i < fsh.entries.size(); ++i) {
                const auto& e = fsh.entries[i];
                if (!e.bitmap) continue;
                const bool hit = member.empty() // no selector: largest bitmap wins
                    ? (!best || std::size_t(e.bitmap->width) * e.bitmap->height > std::size_t(best->width) * best->height)
                    : (byIndex ? std::int32_t(i) == index : iequals(trimTag(e), member));
                if (hit) {
                    best = &*e.bitmap;
                    label += '@' + trimTag(e);
                    if (!member.empty()) break;
                }
            }
            if (!best || best->rgba.empty()) {
                std::cerr << "--fsh: " << file << ": no bitmap for member '" << member << "'; tags:";
                for (const auto& e : fsh.entries)
                    if (e.bitmap) std::cerr << ' ' << trimTag(e);
                std::cerr << '\n';
                return 1;
            }
            blit(*best, label);
        } else
        if (!laySpec.empty()) { // front-end screen composite
            fs::path layDir;
            std::string text;
            try {
                text = readLayout(laySpec, layDir);
            } catch (const std::exception& e) {
                std::cerr << "layout: " << e.what() << '\n';
                return 2;
            }
            const auto doc = formats::LayoutDocument::parse(std::string_view(text));
            const std::string name = fs::path(laySpec.substr(laySpec.find('@') + 1)).stem().string();
            const formats::ScreenLayout screen = formats::ScreenLayout::build(name, doc, nullptr, nullptr, "PC");
            ScreenArt art(*r, root, layDir);
            const LayStats st = composite(screen, ui, art);
            clearColor = {0, 0, 0, 1};
            std::cout << name << ": " << screen.objects.size() << " objects -> " << st.shapes << " shapes, " << st.rects
                      << " colour boxes; skipped hidden " << st.hidden << ", no art " << st.noArt << ", unresolved " << st.missing
                      << "; parser warnings " << screen.warnings.size() << '\n';
            for (std::size_t i = 0; i < std::min<std::size_t>(art.missing().size(), 12); ++i)
                std::cerr << "  unresolved: " << art.missing()[i] << '\n';
            if (st.shapes < minShapes) {
                std::cerr << "only " << st.shapes << " shapes resolved, expected at least " << minShapes << '\n';
                return 1;
            }
        } else if (!treeArg.empty()) { // real-asset mode: the largest FE flow bitmap from the user's tree
            const auto fshPath = wc98::findCaseInsensitive(root, "FE/FEFLOW02/FEFLOW02.FSH");
            if (fshPath.empty()) { std::cerr << "FEFLOW02.FSH not found under " << root << '\n'; return 2; }
            const formats::FshFile fsh = formats::FshFile::parse(wc98::readFile(fshPath));
            const formats::FshBitmap* best = nullptr;
            for (const auto& e : fsh.entries)
                if (e.bitmap && (!best || std::size_t(e.bitmap->width) * e.bitmap->height > std::size_t(best->width) * best->height)) best = &*e.bitmap;
            if (!best || best->rgba.empty()) { std::cerr << "no bitmap in FEFLOW02.FSH\n"; return 2; }
            blit(*best, "FEFLOW02.FSH");
        } else { // synthetic checkerboard scene
            const TextureId checker = r->createTexture(checkerboard(), Wrap::Clamp, "checker");
            draws.push_back({r->createMesh(quad(-4.5f, -3.5f, 4.5f, 3.5f, 0, packRgba(255, 255, 255))),
                             Material{.texture = checker},
                             glm::mat4(1)});
            ui.draw(0, {600, 440, 40, 40}, {0, 0, 1, 1}, packRgba(0, 255, 255)); // cyan corner marker
        }

        Frame frame{
            .camera = {.eye = {0, 0, 10}},
            .draws = std::span(draws),
            .ui = &ui,
            .clearColor = clearColor,
            .joints = {},
            .fog = {},
        };

        r->setSettings({PresentMode::Enhanced, Aspect::Widescreen, false});
        Image img = r->renderOffscreen(frame, 640, 480);
        if (img.width != 640 || img.height != 480) {
            std::cerr << "unexpected image size " << img.width << "x" << img.height << '\n';
            return 1;
        }

        std::vector<std::uint8_t> png = formats::encodePng(img.width, img.height, img.rgba);
        std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());
        std::ofstream out(outPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        out.close();
        if (!out) {
            std::cerr << "cannot write " << outPath << '\n';
            return 1;
        }

        r.reset(); // drain validation messages before the check
        if (*messages) {
            std::cerr << *messages << " validation message(s)\n";
            return 3;
        }
        std::cout << outPath << ": " << png.size() << " bytes\n";
    } catch (const std::exception& e) {
        std::cerr << "render-demo failed: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
