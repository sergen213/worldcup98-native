// wc98-native: window + Vulkan presentation shell with the front-end flow (M4/M9 slice).
// --fe renders FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW05.LAY through presentation::composite; arrow
// keys select menu entries, Enter launches the exhibition match loop (runMatchDemo) and the
// tournament progression driver (runTournamentDemo), Esc returns to the menu. Screens/grammar:
// docs/reverse-engineering/LAY_COMPOSITOR_NOTES.md; navigation model: app/fe_flow.hpp.
#include "fe_flow.hpp"
#include "match_demo.hpp" // headless capture driver (gates) — runMatchDemo
#include "livematch.hpp" // M10 interactive playable match (runLiveLoop / --live-match-smoke)
#include "selftest.hpp" // automated gameplay self-test (--self-test FILE.json)
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
    std::string selfTestPath; // --self-test FILE.json: scripted-input match self-test (goal §14).
    int liveSmoke = 0; // --live-match-smoke N: M10 headless playable-match gate (driver owns window).
    bool recordReplay = false; // --record-replay: live match records user/rplyNNNN.dat + rplyindx.dat

    int saveloadSlot = -1; // --tournament-saveload N: headless save->load scenario on slot N
    int fePick = -1; // --fe-pick ID: scripted team-select pick for the tournament entry (non-interactive gate)
    bool feTeam = false; // --fe-team: open the team-select screen (FEFLOW11 grid) before the smoke frames
    int feSetup = -1; // --fe-setup N: open the friendly (0) / practise (3) match-setup screen before the smoke frames
    bool feLeague = false; // --fe-league: open the FEFLOW73 league-select list before the smoke frames
    int feLeagueCfg = 0; // --fe-league-cfg N: open the league config family screen N (72/74/75/76/78/79/80)
    int feWcCfg = 0; // --fe-wc-cfg N: open the world-cup family screen N (18/23/24/35)
    int feCheatsMask = -1; // --fe-cheats N: open the feflow95 cheats screen with cheat row i ON for bit i (MENU_PARITY.md §8)
    int feHistoryYear = -1; // --fe-history N: open feflow90 (world-cup history board) at year row N (pass 13)
    int feNavHistory = -1;  // --fe-nav-history N: open feflow90, move year cursor to N, Enter -> feflow91 chain
    int feSetup07 = -1;   // --fe-setup07 N: open FEFLOW07 single-player setup, row cursor +N moves (pass 13 §B)
    int feMgmt43 = -1;    // --fe-mgmt-grid N: FEFLOW43 management grid, roster row cursor +N (pass 13 §E)
    int feLoad41 = -1;    // --fe-load-game N: feflow41 LOAD list over defaultSlotDir, rows +N
    int feSave42 = -1;    // --fe-save-manager N: feflow42 SAVE list + tick confirm, rows +N then Enter
    int feStad08 = -1;    // --fe-stadium08 N: friendly chain -> FEFLOW08 stadium select, tile cursor +N moves
    int feControl = -1; // --fe-control N: CONTROL1 controller page from the options chain, nav cursor +N (§H)
    int feIgMode = 0;       // --fe-igoptns a|g|m: open the in-game option popup (igaoptns/iggoptns/igmoptns)
    std::string fePopupName; // --fe-popup NAME: shared popup lane gate (feflow10/37, igconsel/mesg/Feflow86)
    int fePopupKeys = 0;     // --fe-popup-keys N: scripted button-cursor presses before the capture frame
    bool fePopupEnter = false, fePopupBack = false; // --fe-popup-enter / --fe-popup-back: dismiss proof frames
    std::string feScreenName; // --fe-screen NAME: arbitrary LAYOUTS.BIG member capture lane ("make all menus 1:1")
    bool feOptions = false, feSaveLoad = false, feCredits = false; // --fe-options/--fe-saveload/--fe-credits: open the hub scene-graph views (Phase 2/3/4 gates)
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
        else if (a == "--fe-league-cfg") { feMode = true, smokeFrames = 1, feLeagueCfg = std::stoi(next()); } // league config grid/popup capture gate
        else if (a == "--fe-wc-cfg") { feMode = true, smokeFrames = 1, feWcCfg = std::stoi(next()); } // world-cup family screen capture gate
        else if (a == "--fe-cheats") { feMode = true, smokeFrames = 1, feCheatsMask = std::stoi(next()); } // feflow95 checkbox-screen capture gate; N = row bitmask ON at open
        else if (a == "--fe-history") { feMode = true, smokeFrames = 1, feHistoryYear = std::stoi(next()); } // feflow90 capture gate; N = year row index at open
        else if (a == "--fe-nav-history") { feMode = true, smokeFrames = 1, feNavHistory = std::stoi(next()); } // nav chain: feflow90 -> Enter 'fe91' -> feflow91 capture
        else if (a == "--fe-setup07") { feMode = true, smokeFrames = 1, feSetup07 = std::stoi(next()); }   // FEFLOW07 matchup capture gate; N = side-cursor moves
        else if (a == "--fe-stadium08") { feMode = true, smokeFrames = 1, feStad08 = std::stoi(next()); }  // FEFLOW08 stadium-select chain gate; N = tile-cursor moves
        else if (a == "--fe-mgmt-grid") { feMode = true, smokeFrames = 1, feMgmt43 = std::stoi(next()); }     // FEFLOW43 management grid gate; N = row-cursor moves
        else if (a == "--fe-load-game") { feMode = true, smokeFrames = 1, feLoad41 = std::stoi(next()); }      // feflow41 LOAD list gate; N = lstb row-cursor moves
        else if (a == "--fe-control") { feMode = true, smokeFrames = 1, feControl = std::stoi(next()); }       // CONTROL1 controller-page gate; N = npll/nplr nav-cursor moves (§H)
        else if (a == "--fe-save-manager") { feMode = true, smokeFrames = 1, feSave42 = std::stoi(next()); }   // feflow42 SAVE confirm gate; N = row moves + Enter write
        else if (a == "--fe-igoptns") { // igoptns captions lane gate: a=audio g=game m=mouse popup
            feMode = true, smokeFrames = 1;
            const std::string m = next();
            feIgMode = m == "a" ? 1 : m == "g" ? 2 : m == "m" ? 3 : 0;
            if (!feIgMode) throw std::invalid_argument("--fe-igoptns needs a|g|m");
        }
        else if (a == "--fe-popup") { feMode = true, smokeFrames = 1, fePopupName = next(); } // popup-lane gate frame; NAME = feflow10|ok, feflow37|yesno, igconsel, mesg, feflow86
        else if (a == "--fe-popup-keys") { fePopupKeys = std::stoi(next()); }                 // two-axis button cursor moves before the frame
        else if (a == "--fe-popup-enter") { fePopupEnter = true; }                            // Enter on the focused button: closes to caller (payload semantics)
        else if (a == "--fe-popup-back") { fePopupBack = true; }                              // Esc: cancel branch closes to caller
        else if (a == "--fe-screen") { feMode = true, smokeFrames = 1, feScreenName = next(); } // default capture 1 frame; --fe-smoke N overrides
        else if (a == "--fe-options")  { feMode = true, smokeFrames = 1, feOptions = true; }  // FEFLOW15 options view capture gate
        else if (a == "--fe-saveload") { feMode = true, smokeFrames = 1, feSaveLoad = true; } // Feflow89 slot-list capture gate
        else if (a == "--fe-credits")  { feMode = true, smokeFrames = 1, feCredits = true; }  // FEFLOW19 credits view capture gate
        else if (a == "--pad-smoke") { padSmoke = true; }
        else if (a == "--live-match-smoke") { liveSmoke = std::stoi(next()); }
        else if (a == "--self-test") { selfTestPath = next(); } // gameplay self-test: writes JSON, exits 0/1/2
        else if (a == "--ig-pause-smoke") { liveSmoke = std::stoi(next()); } // gate fe_igmenu: scripted pause tour (driver parses the flag itself)
        else if (a == "--ig-stats-smoke") { liveSmoke = std::stoi(next()); } // gates fe_igstats/fe_ighalf/fe_igscsum/fe_igflsum: driver parses its own board flags
        else if (a == "--ig-half-smoke") { liveSmoke = std::stoi(next()); }
        else if (a == "--ig-full-smoke") { liveSmoke = std::stoi(next()); }
        else if (a == "--ig-hlite-smoke") { liveSmoke = std::stoi(next()); } // gates fe_highlite_browser/fe_hilsav_list: driver parses its own flags (§D highlights lane)
        else if (a == "--ig-hlsav-smoke") { liveSmoke = std::stoi(next()); }
        else if (a == "--ig-mgmt-smoke") { liveSmoke = std::stoi(next()); } // gate fe_mgmt_pages: IGTEAM72..80/89 management page cycle (§E), driver parses its own flag
        else if (a == "--ig-camera-smoke") { liveSmoke = std::stoi(next()); }    // gate fe_camera_pages: IGCAMERA/IGCAMSEL popup chain (§G), driver parses its own flag
        else if (a == "--ig-practise-smoke") { liveSmoke = std::stoi(next()); }  // gate fe_practise_pages: PRACT1/PRACT2/IGPRAC1 page cycle + keeper toggle (§G), driver parses its own flag
        else if (a == "--ig-manmarks-smoke") { liveSmoke = std::stoi(next()); } // gate fe_manmarks_grid: MANMARKS ratings board from igteam72 'M' (§E), driver parses its own flag
        else if (a == "--record-replay") { recordReplay = true; }
        else if (a == "--quality") { std::string v = next(); } // h23 tier word: live driver parses its own flag from raw argv
        else if (a == "--video-play") { videoPlay = next(); }
        else if (a == "--no-intro") { noIntro = true; }
        else throw std::invalid_argument("unknown argument '" + a + "'");
    }

    if (saveloadSlot >= 0) return app::runTournamentSaveLoad(saveloadSlot, app::defaultSlotDir());
    if (!selfTestPath.empty()) return app::runSelfTest(tree, selfTestPath); // headless sim-only lane
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
    if (fe) fe->setAudioContext(&cfg, feAudio.get()); // options volume rows drive settings.ini + live mixer gain
    int totalShapes = 0, totalTexts = 0, totalCursors = 0, presented = 0, navRc = -1;
    bool prevLmb = false; // mouse button edge state for widgetHit activation (Phase 1)
        if (fe && feTeam) { fe->selectNav(int(app::FeEntry::Tournament)); fe->enter(); } // hub wedge fe11 -> team-select grid (FEFLOW11)
        if (fe && feSetup >= 0) { fe->selectNav(feSetup); fe->enter(); } // friendly/practise -> FEFLOW09/50 config screen
        if (fe && feLeague) { // League Play -> FEFLOW73 list. On the hub, selectNav opens the list directly (case 1);
            // enter() would re-activate the DEFAULT wedge (friendly setup, feflow09) which preempts drawCurrent's
            // league branch, so only the FEFLOW05 fallback menu needs the Enter press.
            fe->selectNav(int(app::FeEntry::LeaguePlay));
            if (!fe->onHub()) fe->enter();
        }
        if (fe && feLeagueCfg) { // selectNav(league) opens FEFLOW73 directly; the requested config grid/popup opens over it
            fe->selectNav(int(app::FeEntry::LeaguePlay));
            if (!fe->openLeagueCfg(feLeagueCfg)) std::fprintf(stderr, "fe: league config %d unavailable\n", feLeagueCfg);
        }
        if (fe && feWcCfg) { // world-cup family screens (FEFLOW18/23/24/35, analysis/pass12/wcfamily_notes.txt):
            // the gates open them directly; FEFLOW18 sits behind feflow11 team-select in the live flow
            // (SCENEREG site 0x44dc8b), 23/24/35 are between-round boards.
            if (!fe->openWcCfg(feWcCfg)) std::fprintf(stderr, "fe: world-cup config %d unavailable\n", feWcCfg);
        }
        if (fe && feCheatsMask >= 0) { // --fe-cheats N gate frame: feflow95 cheats screen, rows ON per bitmask
            if (!fe->openCheats(feCheatsMask)) std::fprintf(stderr, "fe: cheats screen unavailable\n");
        }
        if (fe && feHistoryYear >= 0) { // --fe-history N gate frame: feflow90 board at year row N
            if (!fe->openHistory(feHistoryYear)) std::fprintf(stderr, "fe: history board unavailable\n");
        }
        if (fe && feNavHistory >= 0) { // nav chain gate: wedge-opened board -> bounded year cursor -> 'fe91' Enter
            if (!fe->openHistory(0)) std::fprintf(stderr, "fe: history board unavailable\n");
            else {
                for (int i = 0; i < feNavHistory; ++i) fe->move(+1); // 'ysrb' cursor moves [C] FUN_00468728
                if (fe->historyScreen() == 90) fe->enter();          // Enter -> feflow91 continuation scene
            }
        }
        if (fe && feSetup07 >= 0) { // --fe-setup07 N: FEFLOW07 opened via the hub fe07 wedge edge, then side cursor
            if (!fe->openSetup07()) std::fprintf(stderr, "fe: setup07 unavailable\n");
            else for (int i = 0; i < feSetup07; ++i) fe->move(+1); // UP/DOWN team-side cursor [C]
        }
        if (fe && feStad08 >= 0) {  // --fe-stadium08 N: hub -> feflow07 Enter 'fe09' -> friendly config confirm -> 'fe08' stadium
            if (!fe->openSetup07()) std::fprintf(stderr, "fe: setup07 unavailable\n");
            else {
                fe->enter(); // forward edge to FEFLOW09 friendly config (feflow09 row screen)
                fe->enter(); // confirm -> the 'fe08' widget tag opens stadium select [C] @0x44C3D3/C3C8
                for (int i = 0; i < feStad08; ++i) fe->move(+1); // mpNN tile ring cursor cycle
            }
        }
        if (fe && feMgmt43 >= 0) { // --fe-mgmt-grid N: FEFLOW43 management board over FCDB squad data, row cursor +N
            if (!fe->openMgmtGrid()) std::fprintf(stderr, "fe: mgmt grid unavailable\n");
            else for (int i = 0; i < feMgmt43; ++i) fe->move(+1); // roster row cursor [H]
        }
        if (fe && feLoad41 >= 0) { // --fe-load-game N: feflow41 LOAD list over the port slot dir, rows +N
            fe->ensureFixtureSlots(); // deterministic SLOT00/SLOT01 fixture when the dir is empty [H]
            if (!fe->openLoadGame()) std::fprintf(stderr, "fe: load game unavailable\n");
            else for (int i = 0; i < feLoad41; ++i) fe->move(+1); // lstb row cursor
        }
        if (fe && feSave42 >= 0) { // --fe-save-manager N: feflow42 SAVE list, rows +N then Enter = write + tick
            if (!fe->openSaveGame()) std::fprintf(stderr, "fe: save manager unavailable\n");
            else {
                for (int i = 0; i < feSave42; ++i) fe->move(+1);
                fe->enter(); // focused row -> saveTournament + 'tick' confirm icon [C] tic chain
            }
        }
        if (fe && feIgMode) { // --fe-igoptns gate frame: in-game option popup captions lane
            if (!fe->openIgOptns(feIgMode)) std::fprintf(stderr, "fe: igoptns popup unavailable\n");
        }
        if (fe && feControl >= 0) { // --fe-control N gate frame: CONTROL1 controller page over the options chain (§H)
            fe->openOptions(); // FEFLOW15 options screen = the caller scene (Esc-back grammar)
            if (!fe->openPopup("control1")) std::fprintf(stderr, "fe: control1 page unavailable\n");
            else for (int i = 0; i < feControl; ++i) fe->move(+1); // npll/nplr nav cursor
        }
        if (fe && !fePopupName.empty()) { // --fe-popup NAME gate frame: popup lane over the hub (caller scene)
            if (!fe->openPopup(fePopupName)) std::fprintf(stderr, "fe: popup %s unavailable\n", fePopupName.c_str());
            else {
                for (int i = 0; i < fePopupKeys; ++i) fe->move(+1); // button cursor cycle ('ok'/'cancel', 'yes'/'no')
                if (fePopupEnter) fe->enter();                      // confirm/cancel both close to the caller -> hub frame
                if (fePopupBack) fe->back();                        // Esc = cancel branch closes to the caller
            }
        }
        if (fe && feOptions) fe->openOptions();   // --fe-options gate frame: FEFLOW15 composite
        if (fe && feSaveLoad) fe->openSaveLoad(); // --fe-saveload gate frame: Feflow89 slot list
        if (fe && feCredits) fe->openCredits();   // --fe-credits gate frame: FEFLOW19 roll

    while (smokeFrames == 0 || presented < smokeFrames) {
        auto ev = window->poll();
        if (ev.quit) break;
        if (fe) { // pointer -> virtual 640x480: naive full-window scale, letterbox offset ignored
            const auto [px, py] = window->pixelSize();
            if (px && py)
                fe->setPointer(std::clamp(ev.mouseX * 640 / int(px), 0, 639),
                               std::clamp(ev.mouseY * 480 / int(py), 0, 479));
        }
        if (fe) { // click edge: widgetHit hit-test -> hub/view scene action (LAY_COMPOSITOR_NOTES.md POINTS polygons)
            float bmX = 0, bmY = 0;
            const bool lmb = (SDL_GetMouseState(&bmX, &bmY) & SDL_BUTTON_LMASK) != 0;
            if (lmb && !prevLmb) {
                const int act = fe->pointerActivate();
                if (act >= 0) { // launches a mode: same path as Enter on that hub wedge
                    navRc = launchEntry(app::FeEntry(act), tree, outPrefix, fe->pickedTeam(),
                                        smokeFrames == 0 && !strictNav, window.get(), renderer.get(), recordReplay);
                    std::cout << "fe: entry " << act << " returned " << navRc << '\n';
                    if (strictNav && navRc != 0 && navRc != 2 && navRc != 77) return navRc;
                }
            }
            prevLmb = lmb;
            fe->setPointerDown(lmb);
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
            const bool launched = fe->enter();
            if (fe->quitRequested()) { navRc = 0; return false; } // hub 'quit' wedge -> clean app exit
            if (!launched) return true; // show the team-select screen (next Enter confirms pickedTeam())
            navRc = launchEntry(app::FeEntry(fe->selected()), tree, outPrefix, fe->pickedTeam(),
                                smokeFrames == 0 && !strictNav, window.get(), renderer.get(), recordReplay);
            std::cout << "fe: entry " << fe->selected() << " returned " << navRc << '\n';
            if (strictNav && navRc != 0 && navRc != 2 && navRc != 77) return false; // gate mode propagates failures
            fe->back(); // leave team-select (no-op on the menu)
            fe->resetCursor(); // hub cursor back to fe07 / fallback menu to entry 0
            return true;
        };
        for (const int sc : ev.keys) { // FE navigation: arrows select, Enter launches, Esc quits to menu.
            if (!fe) break;
            // Setup (FEFLOW09/50) + options (feflow15 lst rows): up/down move the row cursor,
            // left/right cycle its value (the original's two-axis scheme); TAB switches feflow15
            // tab pages ('ops2' event family). On the other screens arrows cycle entries/pick rows.
            const bool setup = fe->onSetup() || fe->onOptions() || fe->onSetup07() || fe->onStad08();
            if (sc == SDL_SCANCODE_UP) fe->move(-1);
            else if (sc == SDL_SCANCODE_DOWN) fe->move(+1);
            else if (sc == SDL_SCANCODE_LEFT) { if (setup) fe->adjustValue(-1); else fe->move(-1); }
            else if (sc == SDL_SCANCODE_RIGHT) { if (setup) fe->adjustValue(+1); else fe->move(+1); }
            else if (sc == SDL_SCANCODE_TAB) fe->cycleTab(+1); // options tab pages tbb1..4
            else if (sc == SDL_SCANCODE_F9) { if (!fe->openCheats()) std::fprintf(stderr, "cheats: %s\n", fe->lastError().c_str()); } // dev entry [H]: payload 0x1E has no decoded key map
            else if (sc == SDL_SCANCODE_ESCAPE) fe->back(); // view -> parent; hub Esc no-op ('quit' wedge exits)
            else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
                if (!launchSelected()) return navRc;
            }
        }
        for (const int d : ev.padNav) { // gamepad nav: same actions as the arrow keys
            if (!fe) break;
            const bool setup = fe->onSetup() || fe->onOptions() || fe->onSetup07() || fe->onStad08();
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
        if (fe && fe->quitRequested()) break; // hub 'quit' wedge: clean app exit after event handling
        if (window->minimized() || !renderer->present(frame)) {
            SDL_Delay(16);
            continue;
        }
        if (fe && smokeFrames && presented == 0) { // gate evidence line (render_demo_lay pattern)
            std::cout << fe->screenName() << ": shapes=" << totalShapes << " texts=" << totalTexts
                      << " cursors=" << totalCursors << " faces=" << fe->fontsLoaded() << " lang=" << fe->language()
                      << (fe->lastError().empty() ? std::string() : (" load-error=" + fe->lastError())) << '\n';
            // Raw-member capture (--fe-screen) legitimately composites 0 shapes when every shape
            // is scene-injected art (IGLAY/IGMENU members reference INGAME.FSH "sh00" which the
            // per-screen FSH lookup cannot resolve); texts>0 below keeps the gate meaningful.
            if (totalShapes <= 0 && feScreenName.empty()) { std::cerr << "FE screen drew no shapes\n"; return 1; }
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

    if (fe && fe->audioDirty()) config::save(config::defaultPath(), cfg); // options volume rows persist to settings.ini
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
                      " [--fe-mgmt-grid N] [--fe-load-game N] [--fe-save-manager N]"
                      " [--fe-nav exhibition|league|tournament|tournament-load|practise] [--fe-team] [--fe-setup N] [--fe-league] [--fe-league-cfg N] [--fe-setup07 N] [--fe-stadium08 N] [--fe-screen NAME]"
                      " [--fe-wc-cfg N]"
                      " [--fe-popup NAME] [--fe-popup-keys N] [--fe-popup-enter|--fe-popup-back] [--fe-control N]"
                      " [--fe-cheats N]"
                      " [--fe-options|--fe-saveload|--fe-credits]"
                      " [--record-replay] [--lang eng|fre|ger|ita|spa|dut|dan|swe|jap]"
                      " [--ig-camera-smoke N] [--ig-practise-smoke N]"
                      " [--fe-present] [--self-test FILE] [--quality N]");
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
