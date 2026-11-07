// wc98-native live match (M10): a real-time playable exhibition match. The simulation is ticked
// from the wall clock in fixed 1/50 s steps (the engine frame clock cadence: camera/commentary use
// DAT_0065F1E0 = 2 sim frames per 1/50 s tick); each render frame builds a MatchSnapshot ->
// presentation::MatchScene with the tele camera following play, plus the HUD strip overlay.
// Commentary (app::Commentary) is pumped every 2 render frames with its trigger seam fed from the
// sim's World::events stream; Esc and window close quit the match.
//
// Input mapping — the ported wc98::input layer assembles the pad word the exe's device readers
// produce, and controller 0 receives it through SimRunner::setController (the same command path
// the M6 demo drives). Bindings are config/src/settings.cpp defaults (settings.ini override):
//   keyboard: move = arrows | pass S | shoot D | sprint W | lob A | evade Q | through-ball E |
//             skill1 Ctrl | skill2 Alt | IGM Space | tactics Z/X/C/V (press) | pause Esc*
//   gamepad:  move = d-pad/left stick | pass A | shoot B | sprint Y | lob X | evade LB |
//             through-ball RB | skill1 LT | skill2 RT | IGM Back | pause Start
//   (*Esc also quits the live match at the app layer, before feeding the sim's pause bit.)
// Controlled-player switching is inside the ported controller code (Player_transferControl sites
// in simulation/src/input.cpp): the sim reassigns ctrl on possession/defensive-context changes.
#include "replay.hpp" // native replay record -> user/rplyNNNN.dat (docs/asset-formats/RPLY.md)
#include "livematch.hpp"
#include "commentary.hpp"
#include "game_audio.hpp"

#include "wc98/config/settings.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/fcdb.hpp"
#include "wc98/formats/png.hpp" // gate PNG evidence (encodePng)
#include "wc98/input/input.hpp"
#include "wc98/platform/window.hpp"
#include "wc98/presentation/camera.hpp"
#include "wc98/presentation/hud.hpp"
#include "wc98/presentation/match_scene.hpp"
#include "wc98/presentation/fe_screen.hpp" // IGMENU/IGQUESHN pause composite (ScreenArt, FeText, composite)
#include "wc98/renderer/vulkan.hpp"
#include "wc98/sim/ball.hpp"
#include "wc98/sim/data.hpp"
#include "wc98/sim/player.hpp"
#include "wc98/sim/rules.hpp"

#include <iomanip> // setw on the replay slot filename
#include "wc98/sim/sim.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <atomic>
#include <optional>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <fstream>

namespace fs = std::filesystem;
using namespace wc98;
using namespace wc98::presentation;
namespace wc98 {
namespace app {

namespace {

// Fixed-point (16.16) -> float, as stored in sim Vec3 fields (fixed.hpp).
float fx(std::int32_t v) { return float(v) / 65536.0f; }

// DAT_0065F1C8 seconds-in-period (plain int32 seconds; HUD clock format site): gate evidence view.
double elapsedSecFloat(const sim::World& w) { return double(w.match.elapsedSec); }

} // namespace

// Gate evidence capture (match_demo savePng pattern): offscreen copy of a presented frame.
bool savePng(render::Renderer& r, const render::Frame& frame, const fs::path& path) {
    const render::Image img = r.renderOffscreen(frame, 640, 480);
    if (img.width != 640 || img.height != 480) return false;
    const std::vector<std::uint8_t> png = formats::encodePng(img.width, img.height, img.rgba);
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    return bool(out);
}

bool MatchBoot::init(const fs::path& root) {
    data = std::make_unique<presentation::GameData>(root);
    // Preflight the assets the slice needs so a missing CD copy skips (ctest SKIP_RETURN_CODE 2).
    const char* need[] = {"COMMON/FCDB.DBI", "COMMON/AISEQS.TBI", "COMMON/ANIM.MO", "COMMON/GEN4TBLS.VIV",
                          "INGAME/MISC/MISC8.FSH", "INGAME/MISC/MISC16.FSH", "INGAME/MISC/TURFS0P0.PAK",
                          "INGAME/PLAYER/MODELS/HIG_30.PC", "INGAME/PLAYER/MODELS/SHI_10.PC",
                          "INGAME/PLAYER/MODELS/MED_70.PC", "INGAME/PLAYER/MODELS/LOW_90.PC",
                          "INGAME/PLAYER/MODELS/SHADOW.PC", "INGAME/PLAYER/MODELS/MODELS2.PAK"};
    for (const char* f : need)
        if (!data->exists(f)) { std::cerr << "skip: missing " << f << '\n'; return false; }

    // Shared FE palette every paletted kit/surface bitmap indexes into (render-demo pattern).
    fs::path pal = wc98::findCaseInsensitive(root, "FE/PACK/PAL.PAK");
    if (pal.empty()) pal = wc98::findCaseInsensitive(root, "PAL.PAK");
    if (!pal.empty()) formats::setGlobalPalette(formats::wc98GlobalPaletteFromPalArchive(wc98::readFile(pal)));

    // Teams: FCDB.DBI carries 56 teams (team table @0x204, count 56, record size 68). Index 0 = id
    // 26 and index 26 = id 67 both ship PK###H + PK###A atlases: home kit of the first, away of the latter.
    auto db = sim::loadFcdb(root);
    if (db.teams.size() <= std::size_t(teamIndex[1])) { std::cerr << "skip: FCDB has " << db.teams.size() << " teams\n"; return false; }

    md.sheets[0] = sim::makeTeamSheet(db, std::size_t(teamIndex[0]));
    md.sheets[1] = sim::makeTeamSheet(db, std::size_t(teamIndex[1]));
    if (!sim::loadFormationTables(md, root)) { std::cerr << "skip: GEN4TBLS.VIV formation tables unreadable\n"; return false; }
    if (md.sheets[0].squad.size() < 11 || md.sheets[1].squad.size() < 11) { std::cerr << "skip: squad under 11 players\n"; return false; }

    // HUD strip names: FCDB STR0 (COMMON/FCDB_ENG.DBI) rows pair 1:1 with the FCDB.DBI team order.
    try {
        const formats::FcdbNames nm = formats::FcdbNames::parse(data->file("COMMON/FCDB_ENG.DBI"));
        for (int t = 0; t < 2; ++t)
            if (std::size_t(teamIndex[std::size_t(t)]) < nm.teams.size())
                abbrv[std::size_t(t)] = nm.teams[std::size_t(teamIndex[std::size_t(t)])].abbreviation;
    } catch (const std::exception& e) { std::cout << "hud: no STR0 abbreviations (" << e.what() << ")\n"; }

    if (!sim::loadMatchAnimData(md, root)) { std::cerr << "skip: ANIM.MO clips unreadable\n"; return false; }
    return true;
}

MatchSnapshot snapshotFromWorld(const sim::World& w, const sim::MatchData& md) {
    MatchSnapshot s;
    s.pitchSize = std::uint8_t(w.opt.pitchSize);
    s.pitchUnits = {float(w.pitch.width), float(w.pitch.length)}; // pitch.hpp width/length game units

    for (int i = 0; i < sim::kPlayers; ++i) {
        const auto& p = w.players[std::size_t(i)];
        auto& v = s.players[std::size_t(i)];
        v.pos = {fx(p.pos.x), fx(p.pos.y), fx(p.pos.z)}; // +0x0BC M+0x04 16.16
        v.facing = float(p.facing & 0x3FF);              // +0x130, stored -0x200..0x1ff
        v.team = std::uint8_t(p.team & 1);               // +0x020
        v.role = p.slot == 0 ? Role::Goalkeeper : Role::Outfield;
        const auto& squad = md.sheets[std::size_t(p.team)].squad;
        if (p.dbPlayer >= 0 && std::size_t(p.dbPlayer) < squad.size()) { // +0x024 -> roster index
            v.shirtNumber = squad[std::size_t(p.dbPlayer)].shirtNumber;
            v.skinTone = std::int8_t(squad[std::size_t(p.dbPlayer)].skinTone & 3);
        }
        // Newest instance that is not fading out (flags bit 0x40, anim.hpp +0x20): the chain head
        // p.anim.inst (+0x10) is index+1; prev links the fading-out partner.
        int inst = p.anim.inst - 1;
        while (inst >= 0 && (w.anim.inst[std::size_t(inst)].flags & 0x40u)) inst = w.anim.inst[std::size_t(inst)].prev - 1;
        if (inst >= 0) {
            const auto& a = w.anim.inst[std::size_t(inst)];
            v.clip = a.clip; // +0x94: AnimData::clips index directly
            v.keyPos = float(a.keyA) + fx(std::uint32_t(a.frac)); // keyA +0xA4, frac 0..0xFFFF
            v.mirror = (a.flags & 1u) != 0; // flags bit 0 mirror (anim.hpp: "1 mirror")
            v.yaw16 = a.angle;              // +0x50 placement yaw
        } else {
            v.clip = -1; // stand pose
        }
    }
    const auto& ref = w.players[std::size_t(sim::kReferee)];
    s.referee.pos = {fx(ref.pos.x), fx(ref.pos.y), fx(ref.pos.z)};
    s.referee.facing = float(ref.facing & 0x3FF);
    s.referee.role = Role::Referee;
    int inst = ref.anim.inst - 1;
    while (inst >= 0 && (w.anim.inst[std::size_t(inst)].flags & 0x40u)) inst = w.anim.inst[std::size_t(inst)].prev - 1;
    if (inst >= 0) {
        const auto& a = w.anim.inst[std::size_t(inst)];
        s.referee.clip = a.clip;
        s.referee.keyPos = float(a.keyA) + fx(std::uint32_t(a.frac));
        s.referee.mirror = (a.flags & 1u) != 0;
    }

    const auto& b = w.ball;
    s.ball.pos = {fx(b.pos.x), fx(b.pos.y), fx(b.pos.z)}; // B+0xDE4 current position
    s.ball.rollPhase = int(b.rollPhase) & 3;              // B+0xDAC (rollAngle >> 16, visual & 3)
    s.ball.heading = float(b.heading & 0x3FF);            // B+0xDB0 atan2(vel), game angle units
    const int holder = (b.flags & 0x20u) ? b.possessor : b.lastToucher; // 0x20 possessed, fallback touch
    s.possessor = (holder >= 0 && holder < sim::kPlayers) ? holder : -1;

    for (int t = 0; t < 2; ++t) {
        // TeamState::side (+0x004 "attack direction"): hypothesis side 0 -> +z, 1 -> -z.
        s.attackDir[std::size_t(t)] = std::int8_t(w.teams[std::size_t(t)].side == 0 ? 1 : -1);
        s.hud.score[std::size_t(t)] = w.match.score[t]; // DAT_0065E050
    }
    s.hud.clockSeconds = float(w.match.elapsedSec); // DAT_0065F1C8 seconds in this period
    s.hud.period = w.match.periodIndex;             // DAT_0065F224
    s.camera.mode = CameraMode::Tele;
    return s;
}

int runLiveLoop(const fs::path& tree, const std::string& prefix, int smokeFrames,
                platform::Window& window, render::Renderer& renderer, bool recordReplay, bool igPauseSmoke) {
    MatchBoot boot;
    if (!boot.init(tree)) return 2;

    presentation::MatchScene scene(renderer, *boot.data, [&] {
        MatchSetup setup{};
        setup.teamIndex = boot.teamIndex;
        setup.pitchSize = std::uint8_t(boot.opt.pitchSize);
        setup.surfaceColumn = 0; // Options::surfaceColumn default; TURFS0P0.PAK (measured members)
        return setup;
    }());

    platform::initGamepads(); // open pads (SDL gamepad events feed input::Input; non-fatal if none)
    const config::LoadResult cfgFile = config::load(config::defaultPath());
    for (const auto& warn : cfgFile.warnings) std::cout << "settings: " << warn << '\n';

    // HUD layout calibration: settings.ini [gameplay] hud_* passthrough keys (HUD_WC98.md).
    HudGeometry geo;
    const auto knob = [&](std::string_view k, float& dst) {
        for (const auto& e : cfgFile.settings.gameplay)
            if (e.key == k) { try { dst = std::stof(std::string(e.value)); } catch (...) {} }
    };
    knob("hud_sb_x", geo.sbX); knob("hud_sb_w", geo.sbW); knob("hud_sb_y", geo.sbY); knob("hud_sb_h", geo.sbH);
    knob("hud_name_cx0", geo.nameCx[0]); knob("hud_name_cx1", geo.nameCx[1]);
    knob("hud_score_cx", geo.scoreCx); knob("hud_clock_cy", geo.clockCy);
    knob("hud_radar_x", geo.radarX); knob("hud_radar_y", geo.radarY); knob("hud_radar_w", geo.radarW); knob("hud_radar_h", geo.radarH);
    knob("hud_dot_half", geo.dotHalf); knob("hud_shirt_head_px", geo.shirtHeadPx); knob("hud_shirt_size", geo.shirtSize);
    scene.setHudGeometry(geo);

    input::Input in(cfgFile.settings);
    in.assign(1, input::Side::Home); // keyboard device id 1 is always present (test_input.cpp l.43)

    // Native replay recording (--record-replay): every controller-0 tick streams into the recorder;
    // sealed with the final world state hash on loop exit.
    ReplayRecorder rec;
    std::uint32_t recTick = 0;
    sim::SimRunner runner(boot.md, boot.opt, 0x2E7A);
    auto& w = runner.world();
    // Bind controller 0 (keyboard device 1) to the kickoff-taker side (Match_run Input_assignController
    // 0x4554C0): unbound controllers never strike center restarts (the demo slice lesson).
    sim::Input_assignController(w, 0, 1, 0);

    MatchCamera camera; // default Tele: follows play behind the ball
    int controlledPid = -1;

    // Commentary: mixer is best-effort — headless CI has no OpenAL device, and the adapter then
    // runs decode-only (queue advances on silent completion).
    std::unique_ptr<audio::Mixer> mixer;
    try { mixer = std::make_unique<audio::Mixer>(); } catch (const std::exception& e) { std::cout << "commentary: no audio device (" << e.what() << ")\n"; }
    app::Commentary commentary(mixer.get());
    const bool haveSpeech = commentary.loadTree(tree);
    if (!haveSpeech) std::cout << "commentary: AUDIO/PHRS/PHRS.PBK unavailable, queue idle\n";
    std::size_t evPos = 0; // World::events is append-only (world.hpp emit)
    // Runtime SFX (docs/reverse-engineering/AUDIO_RUNTIME.md): crowd ambience loop for the whole
    // match + whistle/kick one-shots off the same event seam, independent of speech availability.
    app::MatchAudio matchAudio(tree);
    matchAudio.setMixer(mixer.get());
    matchAudio.startCrowd();
    std::size_t sfxPos = 0; // SFX sweep cursor (append-only events stream)

    auto key = [&](SDL_Scancode sc, bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.scancode = sc;
        e.key.down = down;
        SDL_PushEvent(&e);
    };

    constexpr double kStepMs = 20.0; // 1/50 s wall-clock sim step (engine frame cadence)
    std::uint64_t last = SDL_GetTicks();
    double acc = 0.0;
    int presented = 0, kickIn = 0, kickDur = 0, forceIn = 0, prevPhase = -1;
    bool quit = false, sawPassBit = false;
    const double startClock = elapsedSecFloat(w); // DAT_0065F1C8 via fixed->seconds helper
    // In-game pause menu (MENU_PARITY.md §4 rank 8): FE/INGAME/IGMENU.LAY composited over the live
    // frame on Esc. The original draws its 11 runtime item captions from FC-bank indices the scene
    // handler sets at init (0x4401A4, tags 'gmnc'/'okay'); that caption source is undecoded, so the
    // port's menu domain is {Resume, Quit} (list rows 0..1) and Enter on the quit row opens the
    // IGQUESHN.LAY "are you sure" confirm box ('quit'-tag path). Absent layouts keep Esc = direct exit.
    std::optional<formats::ScreenLayout> igMenu, igQues;
    bool haveIgm = false, haveIq = false;
    try {
        std::filesystem::path igDir;
        const std::string t = presentation::readLayout((tree / "FE/INGAME/IGMENU.LAY").string(), igDir);
        if (!t.empty()) { igMenu = formats::ScreenLayout::build("IGMENU", formats::LayoutDocument::parse(std::string_view{t}), nullptr, nullptr, "PC"); haveIgm = true; }
    } catch (const std::exception&) {}
    try {
        std::filesystem::path igDir;
        const std::string t = presentation::readLayout((tree / "FE/INGAME/IGQUESHN.LAY").string(), igDir);
        if (!t.empty()) { igQues = formats::ScreenLayout::build("IGQUESHN", formats::LayoutDocument::parse(std::string_view{t}), nullptr, nullptr, "PC"); haveIq = true; }
    } catch (const std::exception&) {}
    presentation::ScreenArt igArt(renderer, tree, tree / "FE/INGAME");
    auto igText = std::make_unique<presentation::FeText>(renderer, tree); // FC*.BIN captions for the LAY textid slots
    enum class IgPause { Off, Menu, QuitAsk };
    IgPause pause = IgPause::Off;
    int igSel = 0;                       // menu rows: 0 Resume, 1 Quit
    bool igReported = false, iqReported = false, iqShot = false;
    std::int64_t prevSim = int(w.time.simFrames); // camera delta source (DAT_0065F1E0 frames/tick)

    { // Snap the camera to its tele target once (demo passes a huge frame count per capture; live
      // smoothing climbs eye.y only +/-frames units per tick, so starting at 0 crawls for seconds).
        MatchSnapshot init = snapshotFromWorld(w, boot.md);
        init.hud.teamNames = boot.abbrv;
        camera.tick(init, 600); // enough to converge eye.y/FOV glide; huge values overflow q*frames
    }
    while (!quit) {
        // Gate input synthesis: a real keydown/keyup pair through the SDL queue -> Input layer ->
        // pad word -> controller 0 (proves the whole input->sim command path in --live-match-smoke).
        if (smokeFrames && presented == 20) key(SDL_SCANCODE_S, true);
        if (smokeFrames && presented == 34) key(SDL_SCANCODE_S, false);
        if (igPauseSmoke && presented == 100) key(SDL_SCANCODE_ESCAPE, true); // gate script: Esc opens IGMENU (after the forced kickoff kick ~frame 90)
        if (igPauseSmoke && presented == 130) { key(SDL_SCANCODE_DOWN, true); }   // cursor -> 'quit' row
        if (igPauseSmoke && presented == 131) key(SDL_SCANCODE_DOWN, false);
        if (igPauseSmoke && presented == 135) key(SDL_SCANCODE_RETURN, true);   // Enter -> IGQUESHN confirm
        if (igPauseSmoke && presented == 136) key(SDL_SCANCODE_RETURN, false);
        if (igPauseSmoke && presented == 145) key(SDL_SCANCODE_ESCAPE, true);   // Esc: QuitAsk back to Menu
        if (igPauseSmoke && presented == 160) key(SDL_SCANCODE_ESCAPE, true);   // Esc closes the menu: match resumes

        for (SDL_Event e; SDL_PollEvent(&e);) { // keyboard + gamepad transitions into the ported layer
            if (e.type == SDL_EVENT_QUIT) quit = true;
            else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE) {
                // Esc opens/leaves IGMENU (global-key hub parity); no IG art on this tree -> direct exit.
                if (!haveIgm) quit = true;
                else pause = pause == IgPause::Off ? IgPause::Menu : pause == IgPause::QuitAsk ? IgPause::Menu : IgPause::Off;
            } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && pause != IgPause::Off) {
                switch (e.key.scancode) { // list cursor INC EDX @0x43FCD0: two-row port domain {Resume, Quit}
                    case SDL_SCANCODE_UP: igSel = igSel > 0 ? igSel - 1 : 1; break;
                    case SDL_SCANCODE_DOWN: igSel = igSel < 1 ? igSel + 1 : 0; break;
                    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
                        if (pause == IgPause::Menu && igSel == 1) { if (haveIq) pause = IgPause::QuitAsk; else quit = true; } // 'quit' row: IGQUESHN box, no box -> exit
                        else pause = IgPause::Off;                                          // resume / canc
                        break;
                    default: break;
                }
            }
            if (pause == IgPause::Off) in.handle(e); // gameplay inputs never feed the sim while paused
        }
        if (quit) break;

        // The pause menu freezes the simulation while open (the original halts the match loop).
        const std::uint64_t now = SDL_GetTicks();
        if (pause == IgPause::Off) {
            acc += smokeFrames ? kStepMs : double(std::min<std::uint64_t>(now - last, 100)); // spiral guard
            last = now;
            while (acc >= kStepMs) {
                acc -= kStepMs;
                sim::ControllerState c{};
                in.fill(1, c); // pad word of device 1 into ControllerState::pad words
                if (!sawPassBit && (c.padBits & input::actionBit(config::Action::Pass))) {
                    sawPassBit = true;
                    std::cout << "live: input->sim pass bit 0x20 reached controller 0 pad word\n";
                }
                runner.setController(0, c);
                if (recordReplay) rec.append(recTick++, c); // recorded input word for this tick
                if (!runner.tick()) { quit = true; break; } // match ended (DAT_0057c604 == 2)
                // Center-spot restarts (phase 0) wait for the taker's kick: pulse Pass (key S, bit 0x20),
                // and deliver the exe's "restart taken" event 0xb to Match_onEvent case 11 (0x470F98).
                if (w.match.phase == 0 && prevPhase != 0) { kickIn = smokeFrames ? 40 : 90; forceIn = smokeFrames ? 90 : 150; }
                prevPhase = w.match.phase;
                if (kickIn > 0 && --kickIn == 0) { key(SDL_SCANCODE_S, true); kickDur = 8; }
                if (kickDur > 0 && --kickDur == 0) key(SDL_SCANCODE_S, false);
                if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) {
                    std::cout << "live: restart forced via Match_dispatchEvent(0xb)\n";
                    sim::Match_dispatchEvent(w, 0xb, w.match.phaseTeam);
                }
            }
        } else {
            last = now; acc = 0.0; // keep the clock honest so resuming does not burst-tick
        }

        MatchSnapshot snap = snapshotFromWorld(w, boot.md);
        snap.hud.teamNames = boot.abbrv; // FCDB STR0 abbreviations on the strip
        // Camera smoothing takes the frames SINCE THE LAST RENDER (demo passes it once per capture);
        // feeding the absolute counter collapsed the tele cam to ground height every frame.
        const int simDelta = std::max(1, int(w.time.simFrames) - int(prevSim));
        prevSim = int(w.time.simFrames);
        camera.tick(snap, simDelta);

        for (int pid = 0; pid < sim::kPlayers; ++pid)
            if (w.players[std::size_t(pid)].ctrl == 1) controlledPid = pid;
        snap.hud.controlled[0] = controlledPid; // user 1's player drives the shirt label + ground marker

        if (haveSpeech && (presented % 2) == 0) { // commentary pump every 2 render frames, trigger seam from events
            const std::uint32_t before = commentary.sentencesStarted();
            for (; evPos < w.events.size(); ++evPos) commentary.trigger(w.events[evPos]);
            if (commentary.sentencesStarted() > before)
                std::cout << "live: commentary " << sim::eventName(w.events[evPos - 1].type)
                          << " -> phrase " << commentary.lastPhrase() << '\n';
            commentary.pump(2); // FUN_0048f920 per-frame pump, 2 engine frames per call
        } else {
            evPos = w.events.size();
        }
        if ((presented % 2) == 0) { // SFX trigger seam: same events, own cursor; counts even with no device
            for (; sfxPos < w.events.size(); ++sfxPos) matchAudio.trigger(w.events[sfxPos]);
        }

        // In-game pause menu composite over the live frame (IGMENU scene tick=0x43FC40; list widget
        // 'menu' x=176 y=138 w=286 h=211 -> 19 px rows, FEFLOW73 pattern). The LAY objects carry
        // fname=null.fsh: the scene init installs button art from FE/INGAME/INGAME.FSH (tags
        // bt0n/bt0d..btnN/btnd; RELEASED->*n, HILITED/PRESSED->*d). QuitAsk = the IGQUESHN box
        // ("are you sure" ok/canc pair; 'quit'-tag path of IGMENU init 0x4401A4).
        bool igShotPending = false;
        render::SpriteBatch ov; // outlives scene.build(): pushOverlay stores a pointer consumed there
        if (pause != IgPause::Off) {
            FeDrawStats ig = pause == IgPause::Menu ? composite(*igMenu, ov, igArt, igText.get())
                                                     : composite(*igQues, ov, igArt, igText.get());
            if (pause == IgPause::Menu) {
                for (int row = 0; row <= 1; ++row) { // the port's two-row domain {Resume, Quit}
                    std::string tag = "bt"; tag += char('0' + row); tag += row == igSel ? 'd' : 'n';
                    const ScreenArt::Shape sh = igArt.shape("..\\global\\", "INGAME.FSH", tag, -1);
                    if (sh.tex) { ov.draw(sh.tex, {176.f, 138.f + 24.f * float(row), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++ig.shapes; }
                }
                if (!igReported) { std::cout << "IGMENU: shapes=" << ig.shapes << " texts=" << ig.texts << '\n'; igReported = true; }
            } else if (!iqReported) {
                std::cout << "IGQUESHN: shapes=" << ig.shapes << " texts=" << ig.texts << '\n'; iqReported = true;
            }
            scene.pushOverlay(ov); // consumed by the build() below (SpriteBatch::append after the HUD strip)
            if (igPauseSmoke && presented == 110) igShotPending = true; // visual evidence of the composite
        }

        const render::Frame& frame = scene.build(snap, camera.camera(), /*enhanced=*/true);
        if (igShotPending) savePng(renderer, frame, prefix + "_igmenu.png");
        if (igPauseSmoke && pause == IgPause::QuitAsk && !iqShot) { savePng(renderer, frame, prefix + "_igquesn.png"); iqShot = true; }

        if (smokeFrames && presented == 0) { // gate evidence: the scene drew geometry and the HUD strip
            std::cout << "live: shapes=" << frame.draws.size()
                      << " uiVerts=" << (frame.ui ? frame.ui->vertices().size() : 0u)
                      << " clock=" << snap.hud.clockSeconds << '\n';
            if (frame.draws.empty()) { std::cerr << "live: scene drew no draws\n"; return 1; }
            if (!frame.ui || frame.ui->vertices().empty()) { std::cerr << "live: HUD strip not drawn\n"; return 1; }
        }
        if (smokeFrames && presented == 40) savePng(renderer, frame, prefix + "_mid.png"); // visual evidence

        if (window.minimized() || !renderer.present(frame)) { // swapchain hiccup / minimized: pace and retry
            SDL_Delay(16);
            continue;
        }
        ++presented;
        if (smokeFrames && presented >= smokeFrames) break;
    }
    if (recordReplay && rec.ticks()) { // seal + write user/rplyNNNN.dat + rplyindx.dat (exe path pattern)
        std::error_code recEc;
        fs::create_directories(tree / "user", recEc);
        const std::uint64_t finalHash = sim::stateHash(w);
        const int slot = recordToUserDir(tree / "user", rec.build(0x2E7A, finalHash));
        std::cout << "replay: recorded " << rec.ticks() << " ticks -> user/rply" << std::setw(4) << std::setfill('0')
                  << slot << ".dat hash=0x" << std::hex << finalHash << std::dec << '\n';
    }
    std::cout << "live: sim advanced clock " << startClock << " -> " << elapsedSecFloat(w)
              << " events=" << w.events.size() << " commentary sentences=" << commentary.sentencesStarted()
              << " decodes=" << commentary.decodedSegments()
              << " whistle=" << matchAudio.whistles() << " kicks=" << matchAudio.kicks()
              << " crowd=" << (matchAudio.crowdStarted() ? 1 : 0) << '\n';
    if (smokeFrames) { // gate assertions: sim moved, events fired, input reached the controller pad word
        if (!(elapsedSecFloat(w) > startClock)) { std::cerr << "live: sim clock did not advance\n"; return 1; }
        if (w.events.empty()) { std::cerr << "live: no sim events fired\n"; return 1; }
        if (!sawPassBit) { std::cerr << "live: input->sim pass bit never reached controller 0\n"; return 1; }
    }
    std::cout << prefix << ": live match ran " << presented << " frames\n";
    return 0;
}

int runLiveMatch(int argc, char** argv) {
    fs::path root;
    if (const char* t = std::getenv("WC98_TREE"); t && *t) root = t;
    int smoke = 0;
    std::string prefix = "/tmp/wc98port-out/live";
    bool record = false; // --record-replay (parsed by main too, consumed here in live mode)
    bool igScript = false; // --ig-pause-smoke N: scripted Esc/Enter pause tour (gate fe_igmenu)
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--tree" && i + 1 < argc) root = argv[++i];
        else if (a == "--live-match-smoke") smoke = std::atoi(argv[++i]);
        else if (a == "--out-prefix" && i + 1 < argc) prefix = argv[++i];
        else if (a == "--ig-pause-smoke") { smoke = std::atoi(argv[++i]); igScript = true; } // gate fe_igmenu
        else if (a == "--record-replay") record = true;
        // Flags owned by other modes of the same CLI pass through untouched.
    }
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) { std::cout << "skip: game tree not found\n"; return 2; }

    const config::Settings cfg = config::load(config::defaultPath()).settings;
    const auto& g = cfg.graphics;
    const int width = int(g.width ? g.width : 1280), height = int(g.height ? g.height : 960);
    const bool fullscreen = g.window != config::WindowMode::Windowed;

    std::unique_ptr<platform::Window> window;
    try {
        window = std::make_unique<platform::Window>("World Cup 98", width, height, fullscreen);
    } catch (const std::exception& e) {
        if (smoke) { std::cout << "skip: no display (" << e.what() << ")\n"; return 2; } // headless CI gate skip
        throw;
    }

    render::RenderSettings settings{
        .mode = g.presentation == config::Presentation::Enhanced ? render::PresentMode::Enhanced : render::PresentMode::Original,
        // kAspects mapping (main.cpp): Auto/R4x3 -> pillarbox 4:3, R16x9+ -> widescreen.
        .aspect = g.aspect <= config::Aspect::R4x3 ? render::Aspect::Pillarbox4x3 : render::Aspect::Widescreen,
        .msaa = g.msaa > 1,
    };
    auto messages = std::make_shared<std::atomic<std::uint32_t>>(0);
    render::VulkanConfig vcfg;
    vcfg.validation = g.validation || std::getenv("WC98_VK_VALIDATE") != nullptr; // render-demo env pattern
    vcfg.validationCounter = messages;
    vcfg.instanceExtensions = platform::Window::vulkanInstanceExtensions();
    vcfg.createSurface = [&](VkInstance inst) {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window->sdl(), inst, nullptr, &surface)) std::cerr << "SDL_Vulkan_CreateSurface: " << SDL_GetError() << '\n';
        return surface;
    };
    vcfg.drawableSize = [&] { return window->pixelSize(); };

    auto renderer = render::createVulkanRenderer(vcfg, settings);
    const int rc = runLiveLoop(root, prefix, smoke, *window, *renderer, record, igScript);
    renderer.reset(); // drain validation messages before the verdict (render-demo pattern)
    if (*messages && rc == 0) { std::cerr << *messages << " validation message(s)\n"; return 3; }
    return rc;
}

} // namespace app
} // namespace wc98
