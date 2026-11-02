// wc98-native: window + Vulkan presentation shell. Derived from fifa99-native app/main.cpp;
// gameplay scenes are not ported yet, so this presents an empty (cleared) frame each vsync.
#include "wc98/config/settings.hpp"
#include "wc98/log.hpp"
#include "wc98/platform/window.hpp"
#include "wc98/renderer/vulkan.hpp"

#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
using namespace wc98;

namespace {

constexpr render::Aspect kAspects[] = {
    render::Aspect::Pillarbox4x3, // Auto: original 4:3 presentation
    render::Aspect::Pillarbox4x3,
    render::Aspect::Widescreen,
    render::Aspect::Widescreen,
    render::Aspect::Widescreen,
};

int run(int argc, char** argv) {
    config::Settings cfg = config::load(config::defaultPath()).settings;
    const auto& g = cfg.graphics;
    std::uint32_t width = g.width ? g.width : 1280, height = g.height ? g.height : 960;
    bool fullscreen = g.window != config::WindowMode::Windowed;
    int smokeFrames = 0; // --smoke N: present N frames then exit (CI windowing check).

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) throw std::invalid_argument(a + " needs a value"); return argv[++i]; };
        if (a == "--width") width = std::stoul(next());
        else if (a == "--height") height = std::stoul(next());
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--windowed") fullscreen = false;
        else if (a == "--smoke") smokeFrames = std::stoi(next());
        else throw std::invalid_argument("unknown argument '" + a + "'");
    }

    platform::Window window("World Cup 98", int(width), int(height), fullscreen);

    render::RenderSettings settings{
        .mode = g.presentation == config::Presentation::Enhanced ? render::PresentMode::Enhanced : render::PresentMode::Original,
        .aspect = kAspects[int(g.aspect)],
        .msaa = g.msaa > 1,
    };

    render::VulkanConfig vcfg;
    vcfg.validation = g.validation;
    auto messages = std::make_shared<std::atomic<std::uint32_t>>(0);
    vcfg.validationCounter = messages;
    vcfg.instanceExtensions = platform::Window::vulkanInstanceExtensions();
    vcfg.createSurface = [&](VkInstance inst) {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window.sdl(), inst, nullptr, &surface)) log::error("SDL_Vulkan_CreateSurface: {}", SDL_GetError());
        return surface;
    };
    vcfg.drawableSize = [&] { return window.pixelSize(); };

    auto renderer = render::createVulkanRenderer(vcfg, settings);
    log::info("device: {}", renderer->deviceName());

    render::Frame frame; // empty scene: the shell only proves the presentation path.
    int presented = 0;
    while (smokeFrames == 0 || presented < smokeFrames) {
        auto ev = window.poll();
        if (ev.quit) break;
        if (window.minimized() || !renderer->present(frame)) {
            SDL_Delay(16);
            continue;
        }
        ++presented;
    }
    renderer.reset(); // shutdown validation messages must count too.
    if (*messages) log::error("{} validation messages", messages->load());
    return *messages ? 3 : 0;
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("wc98-native 0.1.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("usage: wc98-native [--width N] [--height N] [--fullscreen|--windowed] [--smoke N]");
            return 0;
        }
    }
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        log::error("{}", e.what());
        return 1;
    }
}
