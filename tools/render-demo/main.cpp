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
//   wc98-render-demo out.png --lay <spec> [--tree <dir>] [--lang eng]
//                                   — same composite with `--lay-text`: the shipped text resources (FFN
//                                         faces + FC<LANG>.BIN labels) load and every object's TEXTID label
//                                         is drawn. Reference captures of the Windows FE menus are produced
//                                         this way; without the flag the composite keeps its old behaviour
//                                         (labels keep counting as noArt).
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
#include "wc98/presentation/fe_screen.hpp"
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
// The compositor (ScreenArt / composite / readLayout) lives in the presentation module
// (presentation/fe_screen.*); this tool is a thin CLI around it. Docs/reverse-engineering/
// LAY_COMPOSITOR_NOTES.md keeps the element model and measured evidence.
using presentation::composite;
using presentation::FeDrawStats;
using presentation::readLayout;
using presentation::ScreenArt;

} // namespace

int main(int argc, char** argv) {
    std::string laySpec, treeArg, fshSpec, lang = "eng";
    const std::string outPath = argc > 1 ? argv[1] : "wc98-render-demo.png";
    int minShapes = 0;
    bool layText = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--lay" && i + 1 < argc) laySpec = argv[++i];
        else if (a == "--fsh" && i + 1 < argc) fshSpec = argv[++i];
        else if (a == "--tree" && i + 1 < argc) treeArg = argv[++i];
        else if (a == "--lay-text") layText = true;
        else if (a == "--min-shapes" && i + 1 < argc) minShapes = std::atoi(argv[++i]);
        else if (a == "--lang" && i + 1 < argc) lang = argv[++i];
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
            // --lay-text: the shipped faces + FC<LANG>.BIN bank load, so every object's TEXTID label
            // draws. Reference captures of the Windows FE menus run through this path; without the flag
            // `fe` stays null and the composite keeps its pre-text behaviour exactly (labels = noArt).
            std::unique_ptr<presentation::FeText> fe;
            if (layText) fe = std::make_unique<presentation::FeText>(*r, root, lang);
            const FeDrawStats st = composite(screen, ui, art, fe.get());
            clearColor = {0, 0, 0, 1};
            std::cout << name << ": " << screen.objects.size() << " objects -> " << st.shapes << " shapes, " << st.rects
                      << " colour boxes, " << st.texts << " texts; skipped hidden " << st.hidden << ", no art " << st.noArt
                      << ", unresolved " << st.missing << "; parser warnings " << screen.warnings.size() << '\n';
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
