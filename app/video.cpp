// EA .DCT movie playback: SCHl/mTCD frames -> SRE luma decode -> RGBA texture
// upload per frame through the Vulkan renderer (docs/asset-formats/VIDEO.md).
#include "video.hpp"

#include "wc98/file.hpp"
#include "wc98/formats/dct.hpp"
#include "wc98/platform/window.hpp"
#include "wc98/renderer/vulkan.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>


#include <atomic>
#include <cstdio>
#include <memory>
#include <vector>

namespace wc98::app {
namespace {

// Per-frame presentation interval: BE u16 at mTCD record +18 (0x38 = 56 ms in
// every INT_*/SEQ_* file; docs/asset-formats/VIDEO.md, Hypothesis-tagged).
int frameDelayMs(const std::vector<std::uint8_t>& bytes, const formats::DctFrameHeader& h) {
    if (h.offset + 20 > bytes.size()) return 56;
    const int ms = (bytes[h.offset + 18] << 8) | bytes[h.offset + 19];
    return ms >= 8 && ms <= 1000 ? ms : 56;
}

} // namespace

int runVideoPlay(const std::string& path, int gate, unsigned width, unsigned height, bool fullscreen) {
    const std::filesystem::path file{path};
    if (!std::filesystem::exists(file)) {
        std::printf("skip: no video file %s\n", path.c_str());
        return gate ? 2 : 1;
    }
    std::vector<std::uint8_t> bytes;
    std::vector<formats::DctFrameHeader> frames;
    try {
        bytes = readFile(file);
        frames = formats::dctScan(bytes);
    } catch (const std::exception& e) {
        std::printf("skip: dct open failed: %s\n", e.what());
        return gate ? 2 : 1;
    }

    std::unique_ptr<render::Renderer> renderer;
    std::unique_ptr<platform::Window> window;
    auto messages = std::make_shared<std::atomic<std::uint32_t>>(0);
    render::VulkanConfig vcfg;
    vcfg.validationCounter = messages;
    if (gate == 0) {
        try {
            window = std::make_unique<platform::Window>("World Cup 98 video", int(width), int(height), fullscreen);
        } catch (const std::exception& e) {
            std::printf("skip: no display (%s)\n", e.what());
            return 2;
        }
        vcfg.instanceExtensions = platform::Window::vulkanInstanceExtensions();
        vcfg.createSurface = [&](VkInstance inst) {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (!SDL_Vulkan_CreateSurface(window->sdl(), inst, nullptr, &surface))
                std::printf("SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
            return surface;
        };
        vcfg.drawableSize = [&] { return window->pixelSize(); };
    }
    try {
        renderer = render::createVulkanRenderer(vcfg);
    } catch (const render::NoVulkanDevice& e) {
        std::printf("skip: %s\n", e.what());
        return 2;
    }

    render::Camera cam;
    cam.eye = {0, 0, 10};
    cam.verticalFovDeg = 45;
    int done = 0;
    double fps = 0;
    for (const auto& h : frames) {
        formats::DctFrame f;
        try {
            f = formats::dctDecode({bytes.data() + h.offset, h.size}, h);
        } catch (const std::exception&) {
            break; // corrupt stream end: present what decoded so far
        }
        std::vector<std::uint8_t> rgba;
        formats::dctToRgba(f, rgba);
        const render::Image img{std::uint32_t(f.width), std::uint32_t(f.height), std::move(rgba)};
        const auto tex = renderer->createTexture(img, render::Wrap::Clamp);
        render::SpriteBatch ui;
        ui.draw(tex, {0, 0, render::SpriteBatch::kWidth, render::SpriteBatch::kHeight});
        const render::Frame frame{cam, {}, &ui, {0, 0, 0, 1}, {}, nullptr, {}, nullptr};
        if (gate) renderer->renderOffscreen(frame, std::uint32_t(f.width), std::uint32_t(f.height));
        else renderer->present(frame);
        renderer->destroyTexture(tex);
        const int ms = frameDelayMs(bytes, h);
        fps += 1000.0 / ms;
        ++done;
        if (!gate) {
            SDL_Delay(Uint32(ms));
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
                if (ev.type == SDL_EVENT_QUIT || ev.key.scancode == SDL_SCANCODE_ESCAPE) goto done_presenting;
        }
    }
done_presenting:
    renderer.reset(); // drain validation messages
    std::printf("VIDEO frames=%d size=%dx%d fps=%.1f\n", done, frames.front().width, frames.front().height,
                done ? fps / done : 0.0);
    if (*messages) { std::printf("%u validation message(s)\n", messages->load()); return gate ? 3 : 3; }
    return 0;
}

} // namespace wc98::app
