// wc98-match-demo: the M6 exhibition-match vertical slice, headless.
// Drives the real simulation (wc98_simulation) for ~358 game seconds at 30 Hz, feeds one human
// keyboard controller through the ported input layer, and renders three snapshots of the original
// match art via presentation::MatchScene on the offscreen Vulkan renderer:
//   <prefix>_kickoff.png  t = 1 s    (formation line-up, ball at centre spot)
//   <prefix>_midplay.png  t = 60 s   (open play, phase 9, live possession, camera tele)
//   <prefix>_sprint.png   t = 120 s  (after holding W = sprint pad bit 0x100 from t = 58 s)
// Interpolation: single-snapshot alpha = 1 per capture (the render alpha path is exercised by the
// renderer tests; a demo frame needs no blend). Positions/angles come straight from World.
#include "match_demo.hpp"
#include "commentary.hpp" // M6 commentary adapter (trigger seam model: docs/reverse-engineering/COMMENTARY_WC98.md)

#include "wc98/presentation/camera.hpp"
#include "wc98/presentation/kits.hpp"
#include "wc98/presentation/match_scene.hpp"
#include "wc98/presentation/hud.hpp"
#include "wc98/renderer/vulkan.hpp"
#include "wc98/formats/fcdb.hpp"
#include "wc98/formats/png.hpp"
#include "wc98/config/settings.hpp"
#include "wc98/input/input.hpp"
#include "wc98/sim/ball.hpp"
#include "wc98/sim/data.hpp"
#include "wc98/sim/player.hpp"
#include "wc98/sim/rules.hpp"
#include "wc98/sim/sim.hpp"
#include "wc98/sim/world.hpp"
#include "wc98/file.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace wc98;
using namespace wc98::presentation;

namespace {

// Fixed-point (16.16) -> float, as stored in sim Vec3 fields (fixed.hpp).
float fx(std::int32_t v) { return float(v) / 65536.0f; }

// Snapshot builder: World -> MatchSnapshot. Field sources are the exe offsets named in the sim
// headers (player.hpp PlayerState, anim.hpp AnimInstance, ball.hpp BallState, rules.hpp MatchState).
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

bool savePng(render::Renderer& r, const render::Frame& frame, const fs::path& path) {
    const render::Image img = r.renderOffscreen(frame, 640, 480);
    if (img.width != 640 || img.height != 480) {
        std::cerr << "unexpected image size " << img.width << 'x' << img.height << '\n';
        return false;
    }
    const std::vector<std::uint8_t> png = formats::encodePng(img.width, img.height, img.rgba);
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    if (!out) { std::cerr << "cannot write " << path << '\n'; return false; }
    std::cout << path.string() << ": " << png.size() << " bytes\n";
    return true;
}

} // namespace

int runMatchDemo(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: wc98-match-demo <out-prefix> [--tree DIR]\n"; return 2; }
    const std::string prefix = argv[1];
    fs::path root;
    if (const char* t = std::getenv("WC98_TREE"); t && *t) root = t;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--tree" && i + 1 < argc) root = argv[++i];
        else { std::cerr << "unknown option: " << a << '\n'; return 2; }
    }
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) { std::cerr << "game tree not found: " << root << '\n'; return 2; }

    presentation::GameData data(root);
    // Preflight the assets the slice needs so a missing CD copy skips (ctest SKIP_RETURN_CODE 2)
    // instead of failing. All paths measured present in $WC98_TREE via /tmp/m6probe probes.
    const char* need[] = {"COMMON/FCDB.DBI", "COMMON/AISEQS.TBI", "COMMON/ANIM.MO", "COMMON/GEN4TBLS.VIV",
                          "INGAME/MISC/MISC8.FSH", "INGAME/MISC/MISC16.FSH", "INGAME/MISC/TURFS0P0.PAK",
                          "INGAME/PLAYER/MODELS/HIG_30.PC", "INGAME/PLAYER/MODELS/SHI_10.PC",
                          "INGAME/PLAYER/MODELS/MED_70.PC", "INGAME/PLAYER/MODELS/LOW_90.PC",
                          "INGAME/PLAYER/MODELS/SHADOW.PC", "INGAME/PLAYER/MODELS/MODELS2.PAK"};
    for (const char* f : need)
        if (!data.exists(f)) { std::cerr << "skip: missing " << f << '\n'; return 2; }

    // Shared FE palette for the paletted kit/surface bitmaps (render-demo pattern).
    fs::path pal = wc98::findCaseInsensitive(root, "FE/PACK/PAL.PAK");
    if (pal.empty()) pal = wc98::findCaseInsensitive(root, "PAL.PAK");
    if (!pal.empty()) formats::setGlobalPalette(formats::wc98GlobalPaletteFromPalArchive(wc98::readFile(pal)));

    // Teams: FCDB.DBI carries 56 teams (measured probe of COMMON/FCDB.DBI: team table @0x204,
    // count 56, record size 68). Index 0 = id 26 and index 26 = id 67 both ship PK###H + PK###A
    // atlases (measured ls INGAME/PLAYER/TEXTURES) - home kit of the first, away kit of the second.
    auto db = sim::loadFcdb(root);
    constexpr std::array<int, 2> teamIndex{0, 26};
    if (db.teams.size() <= std::size_t(teamIndex[1])) { std::cerr << "skip: FCDB has " << db.teams.size() << " teams\n"; return 2; }

    sim::MatchData md;
    md.sheets[0] = sim::makeTeamSheet(db, std::size_t(teamIndex[0]));
    md.sheets[1] = sim::makeTeamSheet(db, std::size_t(teamIndex[1]));
    if (!sim::loadFormationTables(md, root)) { std::cerr << "skip: GEN4TBLS.VIV formation tables unreadable\n"; return 2; }
    if (md.sheets[0].squad.size() < 11 || md.sheets[1].squad.size() < 11) { std::cerr << "skip: squad under 11 players\n"; return 2; }
    // HUD strip names: FCDB STR0 (COMMON/FCDB_ENG.DBI) rows pair 1:1 with the FCDB.DBI team order.
    std::array<std::string, 2> abbrv{};
    try {
        const formats::FcdbNames nm = formats::FcdbNames::parse(data.file("COMMON/FCDB_ENG.DBI"));
        for (int t = 0; t < 2; ++t)
            if (std::size_t(teamIndex[std::size_t(t)]) < nm.teams.size())
                abbrv[std::size_t(t)] = nm.teams[std::size_t(teamIndex[std::size_t(t)])].abbreviation;
    } catch (const std::exception& e) { std::cout << "hud: no STR0 abbreviations (" << e.what() << ")\n"; }

    sim::Options opt{}; // halfLengthMinutes = 4 (DAT_0065EEE0 default), pitchSize 2
    if (!sim::loadMatchAnimData(md, root)) { std::cerr << "skip: ANIM.MO clips unreadable\n"; return 2; }

    try {
        render::VulkanConfig cfg;
        cfg.validation = std::getenv("WC98_VK_VALIDATE") != nullptr; // render-demo env pattern
        auto messages = std::make_shared<std::atomic<std::uint32_t>>();
        cfg.validationCounter = messages;
        auto r = render::createVulkanRenderer(cfg, {}); // no surface: offscreen only
        r->setSettings({render::PresentMode::Enhanced, render::Aspect::Widescreen, false});

        MatchSetup setup{};
        setup.teamIndex = teamIndex;
        setup.pitchSize = std::uint8_t(opt.pitchSize);
        setup.surfaceColumn = 0; // Options::surfaceColumn default; TURFS0P0.PAK (measured members)
        MatchScene scene(*r, data, setup);

        sim::SimRunner runner(md, opt, 0x2E7A);
        auto& w = runner.world();
        // Bind controller 0 (keyboard device 1) to the kickoff-taker side. The exe does this in
        // Match_run via Input_assignController(c, team, devType) 0x4554C0; the runner ctor
        // (Match_init/startPeriod) leaves controllers unbound, so center restarts (phase 0, seqId 0)
        // never get struck: the taker is human-controlled with an all-zero pad and waits forever.
        sim::Input_assignController(w, 0, 1, 0);

        const config::LoadResult cfgFile = config::load(config::defaultPath());
        for (const auto& warn : cfgFile.warnings) std::cout << "settings: " << warn << '\n';
        input::Input in(cfgFile.settings);
        in.assign(1, input::Side::Home); // keyboard device id 1 is always present (test_input.cpp l.43)
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

        auto key = [&](SDL_Scancode sc, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.scancode = sc;
            e.key.down = down;
            SDL_PushEvent(&e);
        };
        auto pump = [&] {
            SDL_Event e;
            while (SDL_PollEvent(&e)) in.handle(e);
        };

        if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) { std::cerr << "SDL_Init: " << SDL_GetError() << '\n'; return 1; } // headless-safe (test_input.cpp l.40)
        MatchCamera camera; // default Tele
        int controlledPid = -1;
        // Commentary: mixer is best-effort — headless CI has no OpenAL device, and the adapter then
        // runs decode-only (queue advances on silent completion).
        std::unique_ptr<audio::Mixer> mixer;
        try { mixer = std::make_unique<audio::Mixer>(); } catch (const std::exception& e) { std::cout << "commentary: no audio device (" << e.what() << ")\n"; }
        app::Commentary commentary(mixer.get());
        const bool haveSpeech = commentary.loadTree(root);
        if (!haveSpeech) std::cout << "commentary: AUDIO/PHRS/PHRS.PBK unavailable, queue idle\n";
        std::size_t evPos = 0; // World::events is append-only (world.hpp emit)

        constexpr int kHz = 30;
        int prevPhase = -1, kickIn = 0, kickDur = 0, forceIn = 0, prevPeriod = w.match.periodIndex;
        std::string label;
        auto hudLine = [&](int period) {
            std::cout << "hud: period " << period + 1 << " ended " << abbrv[0] << ' ' << w.match.score[0] << '-'
                      << w.match.score[1] << ' ' << abbrv[1] << " clock " << formatClock(int(w.match.elapsedSec)) << '\n';
        };
        for (int tick = 1; tick <= 358 * kHz; ++tick) {
            if (tick == 58 * kHz - 2) key(SDL_SCANCODE_W, true); // sprint held from t ~58 s
            pump();
            sim::ControllerState c{};
            in.fill(1, c);          // pad word of device 1 into ControllerState::padBits
            runner.setController(0, c);
            if (!runner.tick()) { std::cerr << "match ended at tick " << tick << '\n'; break; }
            // Center-spot restarts (phase 0) wait for the taker's kick: pulse Pass (key S, pad bit 0x20).
            if (w.match.phase == 0 && prevPhase != 0) { kickIn = 90; forceIn = 150; } // ~3 s after each PeriodStart
            prevPhase = w.match.phase;
            if (kickIn > 0 && --kickIn == 0) { key(SDL_SCANCODE_S, true); kickDur = 4; }
            if (kickDur > 0 && --kickDur == 0) key(SDL_SCANCODE_S, false);
            // Ported set-piece input gap: no pad word strikes the restart in this slice, so deliver the
            // exe's own "restart taken" event 0xb to Match_onEvent case 11 (rules.cpp 0x470F98).
            if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) {
                std::cout << "restart forced via Match_dispatchEvent(0xb)\n";
                sim::Match_dispatchEvent(w, 0xb, w.match.phaseTeam);
            }
            if (haveSpeech) { // commentary trigger seam: drain new sim events + pump per engine-frame cadence
                const std::uint32_t before = commentary.sentencesStarted();
                for (; evPos < w.events.size(); ++evPos) commentary.trigger(w.events[evPos]);
                if (commentary.sentencesStarted() > before)
                    std::cout << "t=" << tick / kHz << " commentary " << sim::eventName(w.events[evPos - 1].type)
                              << " -> phrase " << commentary.lastPhrase() << '\n';
                commentary.pump(std::max(1, int(w.time.simFrames))); // FUN_004906f8 per-frame pump analog (DAT_0065F1E0 frames/tick)
            } else {
                evPos = w.events.size();
            }
            MatchSnapshot snap = snapshotFromWorld(w, md);
            snap.hud.teamNames = abbrv; // FCDB STR0 abbreviations on the strip (scene falls back to its own)
            camera.tick(snap, w.time.simFrames != 0 ? w.time.simFrames : 2); // DAT_0065F1E0 frames/tick

            for (int pid = 0; pid < sim::kPlayers; ++pid)
                if (w.players[std::size_t(pid)].ctrl == 1) controlledPid = pid;
            snap.hud.controlled[0] = controlledPid; // user 1's player drives the shirt label + ground marker
            if (w.match.periodIndex != prevPeriod) hudLine(prevPeriod), prevPeriod = w.match.periodIndex; // half boundary summary
            float lo = 1e9f, hi = -1e9f, sum = 0; int n = 0;
            for (int pid = 0; pid < sim::kPlayers; ++pid) {
                const auto& p = w.players[std::size_t(pid)];
                if (p.team != 0 || p.slot == 0) continue; // team 0 outfield
                const float sp = fx(p.speed);             // +0x14C M+0x94 16.16 units/frame
                lo = std::min(lo, sp), hi = std::max(hi, sp), sum += sp; ++n;
            }
            std::cout << "t=" << tick / kHz << " spd[" << (n ? lo : 0) << ".." << (n ? hi : 0)
                      << "] avg" << (n ? sum / float(n) : 0) << " ctrl=" << controlledPid
                      << " poss=" << snap.possessor << " phase=" << w.match.phase
                      << " ball[" << snap.ball.pos.x << ',' << snap.ball.pos.z << "]\n";

            if (tick == kHz) label = "kickoff";
            else if (tick == 60 * kHz) label = "midplay";
            else if (tick == 120 * kHz) { key(SDL_SCANCODE_W, false); label = "sprint"; } // release after capture
            if (!label.empty()) {
                const render::Frame& frame = scene.build(snap, camera.camera(), /*enhanced=*/true);
                if (!savePng(*r, frame, fs::path(prefix + "_" + label + ".png"))) return 1;
                label.clear();
            }
        }
        hudLine(prevPeriod); // final half summary
        SDL_QuitSubSystem(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD); // scoped: safe when driven from the FE shell (keeps video alive)
        std::cout << "commentary: " << commentary.sentencesStarted() << " sentence(s), "
                  << commentary.decodedSegments() << " segment decode(s)\n";

        r.reset(); // drain validation messages before the check (render-demo l.436-440)
        if (*messages) { std::cerr << *messages << " validation message(s)\n"; return 3; }
    } catch (const std::exception& e) {
        const std::string what = e.what();
        std::cerr << "match-demo: " << what << '\n';
        if (what.contains("missing") || what.contains("not loaded") || what.contains("unreadable")) return 2;
        return 1;
    }
    return 0;
}

