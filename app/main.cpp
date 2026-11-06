// wc98-native: window + Vulkan presentation shell with the front-end flow (M4/M9 slice).
// --fe renders FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW05.LAY through presentation::composite; arrow
// keys select menu entries, Enter launches the exhibition match loop (runMatchDemo) and the
// tournament progression driver (runTournamentDemo), Esc returns to the menu. Screens/grammar:
// docs/reverse-engineering/LAY_COMPOSITOR_NOTES.md; navigation model: app/fe_flow.hpp.
#include "fe_flow.hpp"
#include "match_demo.hpp"
#include "saveload.hpp"
#include "tournament_demo.hpp"

#include "wc98/config/settings.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/png.hpp"
#include "wc98/log.hpp"
#include "wc98/platform/window.hpp"
#include "wc98/renderer/vulkan.hpp"
#include "wc98/file.hpp"

#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <iostream>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

// Shared FE palette every paletted FE bitmap indexes into (render-demo/match-demo pattern).
void loadFePalette(const std::filesystem::path& root) {
    auto palPath = wc98::findCaseInsensitive(root, "FE/PACK/PAL.PAK");
    if (palPath.empty()) palPath = wc98::findCaseInsensitive(root, "PAL.PAK");
    if (!palPath.empty()) formats::setGlobalPalette(formats::wc98GlobalPaletteFromPalArchive(wc98::readFile(palPath)));
}

// Enter-key entry points. Returns the demo exit code (0 ok, 2 skip, ...).
int launchEntry(app::FeEntry e, const std::filesystem::path& tree, const std::string& prefix) {
    switch (e) {
    case app::FeEntry::Exhibition: {
        const std::string p = prefix + "_fe_exhibition";
        const char* argv[] = {"wc98-native", p.c_str(), "--tree", tree.string().c_str()};
        return runMatchDemo(4, const_cast<char**>(argv));
    }
    case app::FeEntry::Tournament:
        return runTournamentDemo(); // reads $WC98_TREE (set at startup)
    default: return 1;
    }
}

int run(int argc, char** argv) {
    config::Settings cfg = config::load(config::defaultPath()).settings;
    const auto& g = cfg.graphics;
    std::uint32_t width = g.width ? g.width : 1280, height = g.height ? g.height : 960;
    bool fullscreen = g.window != config::WindowMode::Windowed;
    int smokeFrames = 0; // --smoke N: present N frames then exit (CI windowing check).
    std::filesystem::path tree;   // --tree DIR (default $WC98_TREE)
    std::string outPrefix = "/tmp/wc98port-out/fe"; // capture prefix for --fe-smoke PNGs
    bool feMode = false, strictNav = false;
    int navEntry = -1; // --fe-nav target (exhibition/tournament), simulated after the smoke frames
    bool padSmoke = false; // --pad-smoke: headless gamepad startup gate (SDL dummy driver)

    int saveloadSlot = -1; // --tournament-saveload N: headless save->load scenario on slot N
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) throw std::invalid_argument(a + " needs a value"); return argv[++i]; };
        if (a == "--width") width = std::stoul(next());
        else if (a == "--height") height = std::stoul(next());
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--windowed") fullscreen = false;
        else if (a == "--smoke") smokeFrames = std::stoi(next());
        else if (a == "--fe") feMode = true;
        else if (a == "--fe-smoke") { feMode = true, smokeFrames = std::stoi(next()); }
        else if (a == "--out-prefix") outPrefix = next();
        else if (a == "--tree") tree = next();
        else if (a == "--fe-nav") { // gate mode: simulate Enter on the named entry after the frames
            feMode = true, strictNav = true;
            const std::string e = next();
            navEntry = e == "exhibition" ? int(app::FeEntry::Exhibition) : e == "tournament" ? int(app::FeEntry::Tournament) : e == "tournament-load" ? 2 : -1;
        }
        else if (a == "--tournament-saveload") { saveloadSlot = std::stoi(next()); }
        else if (a == "--pad-smoke") { padSmoke = true; }
        else throw std::invalid_argument("unknown argument '" + a + "'");
    }

    if (saveloadSlot >= 0) return app::runTournamentSaveLoad(saveloadSlot, app::defaultSlotDir());
    if (padSmoke) { // headless gamepad gate: graceful startup with no pads present (SDL dummy driver).
        if (!platform::initGamepads()) return 2; // pad subsystem unavailable here: skip, not fail
        std::cout << "gamepads=" << platform::connectedGamepadCount() << '\n';
        return 0;
    }
    if (feMode) {
        if (!std::filesystem::is_directory(tree)) {
            // Gate skip pattern: no tree -> exit 2 (ctest SKIP_RETURN_CODE 2).
            std::cout << "skip: game tree not found: " << tree.string() << '\n';
            return smokeFrames || strictNav ? 2 : (log::error("FE mode needs --tree / $WC98_TREE"), 1);
        }
        setenv("WC98_TREE", tree.string().c_str(), 1); // runTournamentDemo env gate
        loadFePalette(tree);
    }

    std::unique_ptr<platform::Window> window;
    try {
        window = std::make_unique<platform::Window>("World Cup 98", int(width), int(height), fullscreen);
    } catch (const std::exception& e) {
        if (smokeFrames || strictNav) { std::cout << "skip: no display (" << e.what() << ")\n"; return 2; } // headless CI
        throw;
    }

    render::RenderSettings settings{
        .mode = g.presentation == config::Presentation::Enhanced ? render::PresentMode::Enhanced : render::PresentMode::Original,
        .aspect = kAspects[int(g.aspect)],
        .msaa = g.msaa > 1,
    };

    render::VulkanConfig vcfg;
    vcfg.validation = g.validation || std::getenv("WC98_VK_VALIDATE") != nullptr; // render-demo env pattern
    auto messages = std::make_shared<std::atomic<std::uint32_t>>(0);
    vcfg.validationCounter = messages;
    vcfg.instanceExtensions = platform::Window::vulkanInstanceExtensions();
    vcfg.createSurface = [&](VkInstance inst) {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window->sdl(), inst, nullptr, &surface)) log::error("SDL_Vulkan_CreateSurface: {}", SDL_GetError());
        return surface;
    };
    vcfg.drawableSize = [&] { return window->pixelSize(); };

    auto renderer = render::createVulkanRenderer(vcfg, settings);
    log::info("device: {}", renderer->deviceName());

    std::unique_ptr<app::FeFlow> fe;
    if (feMode) fe = std::make_unique<app::FeFlow>(*renderer, tree);
    int totalShapes = 0, totalTexts = 0, presented = 0, navRc = -1;

    while (smokeFrames == 0 || presented < smokeFrames) {
        auto ev = window->poll();
        if (ev.quit) break;
        render::SpriteBatch ui;
        render::Frame frame; // empty scene by default: the shell only proves the presentation path.
        if (fe) {
            const auto st = fe->drawCurrent(ui);
            totalShapes += presented == 0 ? st.shapes : 0; // first-frame stats are gate evidence
            totalTexts += presented == 0 ? st.texts : 0;
            frame.ui = &ui;
        }
        auto launchSelected = [&] { // Enter/confirm: launch the selected entry, back to menu 0.
            navRc = launchEntry(app::FeEntry(fe->selected()), tree, outPrefix);
            std::cout << "fe: entry " << fe->selected() << " returned " << navRc << '\n';
            if (strictNav && navRc != 0 && navRc != 2 && navRc != 77) return false; // gate mode propagates failures
            fe->move(-fe->selected());
            return true;
        };
        for (const int sc : ev.keys) { // FE navigation: arrows select, Enter launches, Esc quits to menu
            if (!fe) break;
            if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_LEFT) fe->move(-1);
            else if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_RIGHT) fe->move(+1);
            else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
                if (!launchSelected()) return navRc;
            }
        }
        for (const int d : ev.padNav) { // gamepad nav: same actions as the arrow keys
            if (!fe) break;
            if (d == platform::Window::kPadNavUp || d == platform::Window::kPadNavLeft) fe->move(-1);
            else if (d == platform::Window::kPadNavDown || d == platform::Window::kPadNavRight) fe->move(+1);
        }
        for (const int b : ev.padButtons) { // confirm = South/Start; East/Back = Esc parity (app-owned exit, no-op here)
            if (!fe) break;
            if (b == platform::Window::kPadSouth || b == platform::Window::kPadStart) {
                if (!launchSelected()) return navRc;
            }
        }
        if (window->minimized() || !renderer->present(frame)) {
            SDL_Delay(16);
            continue;
        }
        if (fe && smokeFrames && presented == 0) { // gate evidence line (render_demo_lay pattern)
            std::cout << "FEFLOW05: shapes=" << totalShapes << " texts=" << totalTexts
                      << (fe->lastError().empty() ? std::string() : (" load-error=" + fe->lastError())) << '\n';
            if (totalShapes <= 0) { std::cerr << "FE main menu drew no shapes\n"; return 1; }
            // Menu labels must be visible: FC*.BIN strings through the FFN face (FeText).
            if (totalTexts <= 0) { std::cerr << "FE main menu drew no text quads\n"; return 1; }
        }
        if (fe && smokeFrames) { // per-frame offscreen capture: the gate asserts non-trivial PNGs
            render::Frame cap{};
            cap.ui = &ui;
            cap.clearColor = {0, 0, 0, 1};
            char suffix[12] = {0};
            std::snprintf(suffix, sizeof suffix, "%02d", presented);
            const auto img = renderer->renderOffscreen(cap, 640, 480);
            const std::vector<std::uint8_t> png = formats::encodePng(img.width, img.height, img.rgba);
            if (png.size() < 1024) { std::cerr << "fe capture frame " << presented << ": trivial PNG (" << png.size() << " bytes)\n"; return 1; }
            const std::filesystem::path out = outPrefix + "_fe" + suffix + ".png";
            std::filesystem::create_directories(out.parent_path());
            std::ofstream f(out, std::ios::binary);
            f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
            if (!f) { std::cerr << "cannot write " << out << '\n'; return 1; }
        }
        ++presented;
    }

    if (navEntry >= 0 && navRc == -1) { // no Enter key was pressed during the loop: simulate it (gate path)
        const int loadSlot = [] { const char* v = std::getenv("WC98_SAVE_SLOT"); return v && *v ? std::atoi(v) : 7; }();
        navRc = navEntry == 2 ? app::runTournamentLoadVerify(loadSlot)
                              : launchEntry(app::FeEntry(navEntry), tree, outPrefix);
        std::cout << "fe: entry " << navEntry << " returned " << navRc << '\n';
        if (navRc != 0 && navRc != 2 && navRc != 77) return navRc;
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
            std::puts("usage: wc98-native [--width N] [--height N] [--fullscreen|--windowed] [--smoke N]"
                      " [--fe] [--tree DIR] [--out-prefix P] [--fe-smoke N] [--tournament-saveload N]"
                      " [--fe-nav exhibition|tournament|tournament-load] [--pad-smoke]");
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
