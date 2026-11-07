// wc98-native: window + Vulkan presentation shell with the front-end flow (M4/M9 slice).
// --fe renders FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW05.LAY through presentation::composite; arrow
// keys select menu entries, Enter launches the exhibition match loop (runMatchDemo) and the
// tournament progression driver (runTournamentDemo), Esc returns to the menu. Screens/grammar:
// docs/reverse-engineering/LAY_COMPOSITOR_NOTES.md; navigation model: app/fe_flow.hpp.
#include "fe_flow.hpp"
#include "match_demo.hpp" // headless capture driver (gates) — runMatchDemo
#include "livematch.hpp" // M10 interactive playable match (runLiveLoop / --live-match-smoke)
#include "saveload.hpp"
#include "tournament_demo.hpp"
#include "game_audio.hpp" // FE menu music + shared runtime-audio helpers
#include "video.hpp" // --video-play FILE.DCT (docs/asset-formats/VIDEO.md)

#include "wc98/config/settings.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/png.hpp"
#include "wc98/log.hpp"
#include "wc98/platform/window.hpp"
#include "wc98/renderer/vulkan.hpp"
#include "wc98/file.hpp"

#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>

#include <algorithm>
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

// Enter-key entry points. Returns the demo exit code (0 ok, 2 skip, ...). liveMatch = M10
// interactive playable match on the shell's own window; gates (--fe-nav/strictNav) keep the
// headless capture driver runMatchDemo for their existing evidence semantics.
int launchEntry(app::FeEntry e, const std::filesystem::path& tree, const std::string& prefix, int userTeam = -1,
                bool liveMatch = false, platform::Window* window = nullptr, render::Renderer* renderer = nullptr,
                bool recordReplay = false) {
    switch (e) {
    case app::FeEntry::Exhibition: {
        if (liveMatch && window && renderer) return app::runLiveLoop(tree, prefix + "_fe_live", 0, *window, *renderer, recordReplay);
        const std::string p = prefix + "_fe_exhibition";
        const char* argv[] = {"wc98-native", p.c_str(), "--tree", tree.string().c_str()};
        return runMatchDemo(4, const_cast<char**>(argv));
    }
    case app::FeEntry::Tournament:
        return runTournamentDemo(userTeam); // reads $WC98_TREE (set at startup)
    case app::FeEntry::Practise: { // same match driver, distinct capture prefix (FEFLOW50 flow)
        const std::string p = prefix + "_fe_practise";
        const char* argv[] = {"wc98-native", p.c_str(), "--tree", tree.string().c_str()};
        return runMatchDemo(4, const_cast<char**>(argv));
    }
    case app::FeEntry::LeaguePlay: { // league select confirmed: same match driver, distinct prefix
        const std::string p = prefix + "_fe_league";
        const char* argv[] = {"wc98-native", p.c_str(), "--tree", tree.string().c_str()};
        return runMatchDemo(4, const_cast<char**>(argv));
    }
    default: return 1; // Count sentinel / unmapped entry codes
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
    int navEntry = -1; // --fe-nav target (exhibition/league/tournament/practise), simulated after the frames
    int presentGate = 0; // --fe-present: draw each between-round presentation screen, print stat lines
    bool padSmoke = false; // --pad-smoke: headless gamepad startup gate (SDL dummy driver)
    int liveSmoke = 0; // --live-match-smoke N: M10 headless playable-match gate (driver owns window).
    bool recordReplay = false; // --record-replay: live match records user/rplyNNNN.dat + rplyindx.dat

    int saveloadSlot = -1; // --tournament-saveload N: headless save->load scenario on slot N
    int fePick = -1; // --fe-pick ID: scripted team-select pick for the tournament entry (non-interactive gate)
    bool feTeam = false; // --fe-team: open the team-select screen (FEFLOW11 grid) before the smoke frames
    int feSetup = -1; // --fe-setup N: open the friendly (0) / practise (3) match-setup screen before the smoke frames
    bool feLeague = false; // --fe-league: open the FEFLOW73 league-select list before the smoke frames
    std::string feScreenName; // --fe-screen NAME: arbitrary LAYOUTS.BIG member capture lane ("make all menus 1:1")
    std::string lang = "eng"; // --lang CODE: FC*.BIN UI-string bank (registry Language key)
    std::string videoPlay; // --video-play FILE.DCT: movie playback through the Vulkan renderer (VIDEO.md)
    bool noIntro = false; // --no-intro: skip the startup .DCT movies (headless gates never play them)
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
        else if (a == "--lang") { lang = next(); } // FC*.BIN bank; used by --fe/--fe-smoke gates
        else if (a == "--fe-nav") { // gate mode: simulate Enter on the named entry after the frames
            feMode = true, strictNav = true;
            const std::string e = next();
            navEntry = e == "exhibition" ? int(app::FeEntry::Exhibition) : e == "league" ? int(app::FeEntry::LeaguePlay)
                     : e == "tournament" || e == "tournament-load" ? 2 : e == "practise" ? int(app::FeEntry::Practise) : -1;
        }
        else if (a == "--tournament-saveload") { saveloadSlot = std::stoi(next()); }
        else if (a == "--fe-pick") { fePick = std::stoi(next()); }
        else if (a == "--fe-present") { feMode = true, presentGate = 1; }
        else if (a == "--fe-team") { feTeam = true; }
        else if (a == "--fe-setup") { feMode = true, feSetup = std::stoi(next()); }
        else if (a == "--fe-league") { feMode = true, smokeFrames = 1, feLeague = true; } // FEFLOW73 list capture (1 frame; --fe-smoke N overrides)
        else if (a == "--fe-screen") { feMode = true, smokeFrames = 1, feScreenName = next(); } // default capture 1 frame; --fe-smoke N overrides
        else if (a == "--pad-smoke") { padSmoke = true; }
        else if (a == "--live-match-smoke") { liveSmoke = std::stoi(next()); }
        else if (a == "--record-replay") { recordReplay = true; }
        else if (a == "--video-play") { videoPlay = next(); }
        else if (a == "--no-intro") { noIntro = true; }
        else throw std::invalid_argument("unknown argument '" + a + "'");
    }

    if (saveloadSlot >= 0) return app::runTournamentSaveLoad(saveloadSlot, app::defaultSlotDir());
    if (padSmoke) { // headless gamepad gate: graceful startup with no pads present (SDL dummy driver).
        if (!platform::initGamepads()) return 2; // pad subsystem unavailable here: skip, not fail
        std::cout << "gamepads=" << platform::connectedGamepadCount() << '\n';
        return 0;
    }
    if (liveSmoke > 0) return app::runLiveMatch(argc, argv); // M10 gate: driver-owned window + swapchain; skip-2 pattern.
    // .DCT movie playback: driver owns its window + renderer (skip-2 on no display).
    if (!videoPlay.empty()) return app::runVideoPlay(videoPlay, 0, width, height, fullscreen);
    if (feMode) {
        if (!std::filesystem::is_directory(tree)) {
            // Gate skip pattern: no tree -> exit 2 (ctest SKIP_RETURN_CODE 2).
            std::cout << "skip: game tree not found: " << tree.string() << '\n';
            return smokeFrames || strictNav ? 2 : (log::error("FE mode needs --tree / $WC98_TREE"), 1);
        }
        setenv("WC98_TREE", tree.string().c_str(), 1); // runTournamentDemo env gate
        loadFePalette(tree);
    }

    // Startup movies, Windows-original order: EA Sports logo then the FIFA intro, each through the
    // .DCT player (Esc skips one, docs/asset-formats/VIDEO.md), then the front-end. Interactive FE
    // runs only; headless gates (--fe-smoke/--fe-nav) never open a movie window.
    if (feMode && !noIntro && smokeFrames == 0 && !strictNav && presentGate == 0 && videoPlay.empty()) {
        for (const char* m : {"VIDEO/INT_EASP.DCT", "VIDEO/INT_FIFA.DCT"}) {
            const auto p = tree / m;
            if (!std::filesystem::exists(p)) continue;
            std::cout << "intro: playing " << m << '\n';
            app::runVideoPlay(p.string(), 0, width, height, fullscreen);
        }
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
    if (feMode) fe = std::make_unique<app::FeFlow>(*renderer, tree, lang);
    if (!feScreenName.empty()) { // arbitrary-screen capture lane (app/fe_flow.cpp): bad member name -> exit 2
        std::string err;
        if (!fe->loadCaptureScreen(feScreenName, err)) { std::cerr << "fe-screen " + feScreenName << ": " << err << '\n'; return 2; }
    }
    std::unique_ptr<audio::Mixer> feAudio; // owns the one OpenAL context during the FE loop
    if (feMode) { // menu music: AUDIO/MUSC FE_*.ASF as a looping SCHl stream (AUDIO_RUNTIME.md);
        // best-effort like the live-match mixer: headless CI has no OpenAL device.
        try { feAudio = std::make_unique<audio::Mixer>(); } catch (const std::exception& e) { std::cout << "audio: no output device (" << e.what() << ")\n"; }
        if (feAudio) {
            std::string track;
            if (app::startMenuMusic(*feAudio, tree, &track)) std::cout << "fe: menu music " << track << '\n';
        }
    }
    int totalShapes = 0, totalTexts = 0, totalCursors = 0, presented = 0, navRc = -1;
        if (fe && feTeam) { fe->move(int(app::FeEntry::Tournament)); fe->enter(); } // Tournament entry -> team-select grid
        if (fe && feSetup >= 0) { fe->move(feSetup); fe->enter(); } // friendly/practise -> FEFLOW09/50 config screen
        if (fe && feLeague) { fe->move(int(app::FeEntry::LeaguePlay)); fe->enter(); } // League Play -> FEFLOW73 list

    while (smokeFrames == 0 || presented < smokeFrames) {
        auto ev = window->poll();
        if (ev.quit) break;
        if (fe) { // pointer -> virtual 640x480: naive full-window scale, letterbox offset ignored
            const auto [px, py] = window->pixelSize();
            if (px && py)
                fe->setPointer(std::clamp(ev.mouseX * 640 / int(px), 0, 639),
                               std::clamp(ev.mouseY * 480 / int(py), 0, 479));
        }
        render::SpriteBatch ui;
        render::Frame frame; // empty scene by default: the shell only proves the presentation path.
        if (fe) {
            const auto st = fe->drawCurrent(ui);
            totalShapes += presented == 0 ? st.shapes : 0; // first-frame stats are gate evidence
            totalTexts += presented == 0 ? st.texts : 0;
            frame.ui = &ui;
            totalCursors += presented == 0 ? st.cursors : 0;
        }
        auto launchSelected = [&] { // Enter/confirm: Tournament entry opens team-select; confirm launches with the pick.
            if (!fe->enter()) return true; // show the team-select screen (next Enter confirms pickedTeam())
            navRc = launchEntry(app::FeEntry(fe->selected()), tree, outPrefix, fe->pickedTeam(),
                                smokeFrames == 0 && !strictNav, window.get(), renderer.get(), recordReplay);
            std::cout << "fe: entry " << fe->selected() << " returned " << navRc << '\n';
            if (strictNav && navRc != 0 && navRc != 2 && navRc != 77) return false; // gate mode propagates failures
            fe->back(); // leave team-select (no-op on the menu)
            fe->move(-fe->selected());
            return true;
        };
        for (const int sc : ev.keys) { // FE navigation: arrows select, Enter launches, Esc quits to menu.
            if (!fe) break;
            // Setup screen (FEFLOW09/50): up/down move the config row, left/right cycle its value
            // (the original's two-axis scheme); on the other screens arrows cycle entries/pick rows.
            const bool setup = fe->onSetup();
            if (sc == SDL_SCANCODE_UP) fe->move(-1);
            else if (sc == SDL_SCANCODE_DOWN) fe->move(+1);
            else if (sc == SDL_SCANCODE_LEFT) { if (setup) fe->adjustValue(-1); else fe->move(-1); }
            else if (sc == SDL_SCANCODE_RIGHT) { if (setup) fe->adjustValue(+1); else fe->move(+1); }
            else if (sc == SDL_SCANCODE_ESCAPE) fe->back(); // setup/team-select -> menu (no-op on the menu)
            else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
                if (!launchSelected()) return navRc;
            }
        }
        for (const int d : ev.padNav) { // gamepad nav: same actions as the arrow keys
            if (!fe) break;
            const bool setup = fe->onSetup();
            if (d == platform::Window::kPadNavUp) fe->move(-1);
            else if (d == platform::Window::kPadNavDown) fe->move(+1);
            else if (d == platform::Window::kPadNavLeft) { if (setup) fe->adjustValue(-1); else fe->move(-1); }
            else if (d == platform::Window::kPadNavRight) { if (setup) fe->adjustValue(+1); else fe->move(+1); }
        }
        for (const int b : ev.padButtons) { // confirm = South/Start; East/Back = Esc parity (team-select -> menu, no-op there)
            if (!fe) break;
            if (b == platform::Window::kPadSouth || b == platform::Window::kPadStart) {
                if (!launchSelected()) return navRc;
            } else if (b == platform::Window::kPadEast || b == platform::Window::kPadBack) fe->back();
        }
        if (window->minimized() || !renderer->present(frame)) {
            SDL_Delay(16);
            continue;
        }
        if (fe && smokeFrames && presented == 0) { // gate evidence line (render_demo_lay pattern)
            std::cout << fe->screenName() << ": shapes=" << totalShapes << " texts=" << totalTexts
                      << " cursors=" << totalCursors << " faces=" << fe->fontsLoaded() << " lang=" << fe->language()
                      << (fe->lastError().empty() ? std::string() : (" load-error=" + fe->lastError())) << '\n';
            if (totalShapes <= 0) { std::cerr << "FE screen drew no shapes\n"; return 1; }
            // Labels must be visible: FC*.BIN strings through the FFN face (FeText). The --fe-screen
            // lane renders raw LAY members whose dynamic row labels are scene-code content, so a
            // zero-texts composite is legitimate there; shapes>0 above is the capture gate.
            if (totalTexts <= 0 && feScreenName.empty() && !fe->onLeague()) { std::cerr << "FE screen drew no text quads\n"; return 1; }
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
                              : launchEntry(app::FeEntry(navEntry), tree, outPrefix, fePick); // --fe-pick: team-select pick
        std::cout << "fe: entry " << navEntry << " returned " << navRc << '\n';
        if (navRc != 0 && navRc != 2 && navRc != 77) return navRc;
    }

    if (presentGate && fe) { // between-round presentation gate (LAY_COMPOSITOR_NOTES "Tournament
                             // presentation scenes"): one stat line per screen, real bracket data.
        const int rounds = fe->tourneyPresentCount();
        if (!rounds) return 2; // tree lacks LAYOUTS/pool: skip
        for (int r = 0; r < rounds; ++r) {
            render::SpriteBatch ui;
            const auto st = fe->drawTourneyPresent(ui, r);
            std::cout << "FE-PRESENT " << fe->tourneyPresentName(r) << ": shapes=" << st.shapes
                      << " texts=" << st.texts << " cursors=" << st.cursors << '\n';
            if (st.shapes <= 0 || st.texts <= 0) { std::cerr << "presentation screen drew nothing\n"; return 1; }
        }
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
                      " [--fe] [--tree DIR] [--out-prefix P] [--fe-smoke N] [--tournament-saveload N] [--fe-pick N]"
                      " [--fe-nav exhibition|league|tournament|tournament-load|practise] [--fe-team] [--fe-setup N] [--fe-league] [--fe-screen NAME]"
                      " [--record-replay] [--lang eng|fre|ger|ita|spa|dut|dan|swe|jap]"
                      " [--fe-present]");
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
