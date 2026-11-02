// Offscreen Vulkan scene rendered through the ported renderer, saved as a PNG.
//   wc98-render-demo out.png            — synthetic checkerboard scene
//   wc98-render-demo out.png <tree-dir> — first FEFLOW02.FSH bitmap decoded from the user's
//                                         original game tree and presented full-frame (M4 slice 1).
#include "wc98/renderer/vulkan.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/png.hpp"
#include "wc98/file.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <atomic>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace wc98;
using namespace wc98::render;

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

} // namespace

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "wc98-render-demo.png";
    try {
        VulkanConfig cfg;
        cfg.validation = validationRequestedByEnv();
        auto messages = std::make_shared<std::atomic<std::uint32_t>>();
        cfg.validationCounter = messages;

        auto r = createVulkanRenderer(cfg, {}); // no surface: offscreen only

        // Optional real-asset mode: decode the largest FE flow bitmap from the user's tree.
        std::optional<Image> fshImg;
        if (argc > 2) {
            namespace fs = std::filesystem;
            const fs::path root{argv[2]};
            auto palPath = wc98::findCaseInsensitive(root, "FE/PACK/PAL.PAK");
            if (palPath.empty()) palPath = wc98::findCaseInsensitive(root, "PAL.PAK");
            if (!palPath.empty()) formats::setGlobalPalette(formats::wc98GlobalPaletteFromPalArchive(wc98::readFile(palPath)));
            const auto fshPath = wc98::findCaseInsensitive(root, "FE/FEFLOW02/FEFLOW02.FSH");
            if (fshPath.empty()) { std::cerr << "FEFLOW02.FSH not found under " << root << '\n'; return 2; }
            const formats::FshFile fsh = formats::FshFile::parse(wc98::readFile(fshPath));
            const formats::FshBitmap* best = nullptr;
            for (const auto& e : fsh.entries)
                if (e.bitmap && (!best || std::size_t(e.bitmap->width) * e.bitmap->height > std::size_t(best->width) * best->height)) best = &*e.bitmap;
            if (!best || best->rgba.empty()) { std::cerr << "no bitmap in FEFLOW02.FSH\n"; return 2; }
            fshImg = Image{best->width, best->height, best->rgba};
            std::cout << "FSH bitmap " << best->width << 'x' << best->height << " (palette type 0x" << std::hex << int(best->paletteType) << ")\n";
        }
        TextureId checker = fshImg ? r->createTexture(*fshImg, Wrap::Clamp, "fsh") : r->createTexture(checkerboard(), Wrap::Clamp, "checker");

        std::vector<DrawItem> draws;
        SpriteBatch ui;
        if (fshImg) { // present the decoded bitmap aspect-fit over the whole frame, no synthetic geometry
            const float ar = float(fshImg->width) / float(fshImg->height);
            float w = 640.f, h = w / ar;
            if (h > 480.f) h = 480.f, w = h * ar;
            ui.draw(checker, {(640.f - w) * .5f, (480.f - h) * .5f, w, h});
        } else {
            draws.push_back({r->createMesh(quad(-4.5f, -3.5f, 4.5f, 3.5f, 0, packRgba(255, 255, 255))),
                             Material{.texture = checker},
                             glm::mat4(1)});
            ui.draw(0, {600, 440, 40, 40}, {0, 0, 1, 1}, packRgba(0, 255, 255)); // cyan corner marker
        }

        Frame frame{
            .camera = {.eye = {0, 0, 10}},
            .draws = std::span(draws),
            .ui = &ui,
            .clearColor = {0, 0, 0.2f, 1},
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
