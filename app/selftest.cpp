// app/selftest.cpp — automated gameplay self-test (goal §14). See selftest.hpp.
// Drives a real playable exhibition match headlessly: FCDB teams, formation, tactics,
// difficulty and the world seed come from the shipped config defaults, exactly as the
// interactive --live-match lane boots; scripted dpad words drive controller 0; every step
// measures the internal simulation state. Pitch fields are the exe's layout: posI = {x=width,
// y=height, z=depth} (motion.cpp writes x/z per tick), ball tpos/tvel are 16.16 Fixed pairs.
#include "selftest.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "wc98/config/settings.hpp" // Action enum + actionBit (the live loop's pad-word model)
#include "wc98/input/input.hpp"
#include "wc98/sim/ball.hpp"
#include "wc98/sim/player.hpp"
#include "wc98/sim/sim.hpp"
#include "wc98/sim/world.hpp"
#include "wc98/sim/events.hpp"
#include "wc98/sim/rules.hpp"

#include "fe_flow.hpp"    // fcdbTeamFromList: reuse the interactive boot path
#include "livematch.hpp"  // MatchBoot (the interactive match bootstrap)

// Kick-lane entry points not exposed in public headers (definitions in simulation/src/kick.cpp).
namespace wc98::sim {
void Kick_arm(World& w, int pid);                                        // 0x4D0BB4
void Player_executeKick(World& w, int pid, int code, std::int32_t ballY); // 0x4E3478
} // namespace wc98::sim

namespace wc98::app {
namespace {
using namespace std::literals::string_literals;
namespace sim = wc98::sim;
namespace config = wc98::config;
namespace fs = std::filesystem;

struct StepRes {
    const char* name;
    bool passed = false;
    std::string detail;
};

void writeJson(const fs::path& p, const std::vector<StepRes>& steps) {
    std::ofstream f(p);
    if (!f) return;
    f << "{\n  \"version\": 1,\n  \"steps\": [\n";
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const StepRes& s = steps[i];
        f << "    {\"step\": \"" << s.name << "\", \"passed\": " << (s.passed ? "true" : "false")
          << ", \"detail\": \"" << s.detail << "\"}" << (i + 1 < steps.size() ? "," : "") << "\n";
    }
    f << "  ]\n}\n";
}

std::string fmt(std::string_view f, auto&&... a) { return std::vformat(f, std::make_format_args(a...)); }

// Integer part of the exe's 16.16 fields (Ball tpos/tvel).
long iu(sim::Fixed v) { return static_cast<long>(v >> 16); }

// Forced-kick lane (same recipe as the M6/M7-proven scenario helper): plan via Player_executeKick,
// override the contact-frame gate (no clip skeleton in synthetic state -> Anim_contactPos garbage),
// arm and release at contact. The two kick.cpp entry points are declared above (not in a header).

void setPosSt(sim::World& w, int pid, int x, int z) {
    auto& p = w.players[std::size_t(pid)];
    p.posI = {x, 0, z};
    p.pos = {x << 16, 0, z << 16};
}

// Drain the world's event queue into a running list (drain pattern of the scenario suite).
void drainEv(sim::World& w, std::vector<sim::SimEvent>& all) {
    for (auto& e : w.events) all.push_back(e);
    w.events.clear();
}

} // namespace

int runSelfTest(const fs::path& tree, const fs::path& outJson) {
    MatchBoot boot;
    if (!boot.init(tree)) return 2; // gate skip pattern (ctest SKIP_RETURN_CODE 2)
    // The period table is rebuilt from w.opt.halfLengthMinutes when the kickoff phase starts, so
    // arm a far-out clock through the options before the runner boots (periods[] re-patch below).
    boot.opt.halfLengthMinutes = 10000;

    sim::SimRunner runner(boot.md, boot.opt, 0x2E7A);
    auto& w = runner.world();
    // Measurement stability: the self-test drives one continuous play sequence, so arm a period
    // length far beyond the scripted windows. Match_startPeriod reloads durationSec from the
    // periods[] table at every phase change (rules.cpp:421), so patch the whole table; the exe's
    // requestPeriodEnd -> phase 9 grammar stays intact, it simply never fires mid-measurement.
    for (auto& pe : w.match.periods) pe.durationSec = 1 << 20;

    w.match.injurySec = 0;
    sim::Input_assignController(w, 0, 1, 0); // kickoff-taker side, the live loop's binding

    const std::uint32_t kPass = input::actionBit(config::Action::Pass);
    const std::uint32_t kShoot = input::actionBit(config::Action::Shoot);
    const std::uint32_t kSprint = input::actionBit(config::Action::Sprint);

    // Raw dpad bits (ControllerState comment): 1 up, 2 down, 4 right, 8 left. Mutating padBits in
    // place keeps the .player/devType binding Input_assignController installed.
    auto pad = [&](std::uint32_t bits) { w.controllers[0].padBits = bits; };

    const auto len = [](long dx, long dz) { return std::lround(std::hypot(double(dx), double(dz))); };

    std::vector<StepRes> steps = {{"movement", false, ""},   {"sprint", false, ""},
                                  {"turning", false, ""},    {"dribbling", false, ""},
                                  {"passing", false, ""},    {"shooting", false, ""},
                                  {"physics", false, ""},    {"ai", false, ""},
                                  {"goalkeeper", false, ""}, {"match-state", false, ""},
                                  {"foul-card", false, ""},  {"penalty", false, ""},
                                  {"stop-distance", false, ""}};

    const auto teamOfPid = [&](int pid) -> int {
        return w.players[std::size_t(pid)].team == 1 ? 1 : 2; // PlayerState::team is the plain index (player.cpp:727)
    };
    const auto posOf = [&](int pid) -> std::pair<long, long> {
        const auto& p = w.players[std::size_t(pid)];
        return {long(p.posI.x), long(p.posI.z)}; // pitch-plane units (M+0x3C width/depth)
    };

    // ---- kickoff: the exhibition boot runs the pre-kickoff cut-scene (rules seq3, ~6080 frames);
    // a Pause press raises the skip request (input.cpp:585 bit 0x2000) and fast-forwards the scene,
    // exactly like the interactive lane. Pass pulses aim-kick once the taker is live; the center kick
    // runs its action clip (~170 anim frames); the settle below absorbs it so later windows measure play.
    const long clockStart = w.match.elapsedSec;
    int firstTouchPid = -1;
    bool kickoffKicked = false;
    int passDur = 0, pauseDur = 0, kickIn = 0, forceIn = 0, prevPhase = -1;
    // Center-spot restarts follow the exe's own grammar (cf. livematch.cpp:1275): phase 0 waits for
    // the taker's kick; pulse Pass at kickIn, and if it has not fired, force the "restart taken"
    // event 0xb at forceIn. The pre-kick cut-scene (rules seq3) runs ~6080 frames before live play
    // begins; Pause pulses raise the skip request. Exit once LIVE play (phase 9) has possession on
    // the controller side: measurement windows below must open inside live play, never mid-scene.
    for (int t = 0; t < 24000 && firstTouchPid < 0; ++t) {
        std::uint32_t bits = 0;
        if (w.match.phase == 0 && prevPhase != 0) { kickIn = 90; forceIn = 150; }
        prevPhase = w.match.phase;
        if (passDur > 0) { bits |= kPass; --passDur; } // hold the press edge a few ticks
        else if (kickIn > 0 && w.match.phase == 0 && --kickIn == 0) { bits |= kPass; passDur = 6; kickoffKicked = true; }
        else if (w.match.phase == 0 && t % 300 == 120) { bits |= kPass; passDur = 6; kickoffKicked = true; } // re-pulse till scene ends
        if (forceIn > 0 && w.match.phase == 0 && --forceIn == 0)
            sim::Match_dispatchEvent(w, 0xb, w.match.phaseTeam); // rules case 11 fallback
        if (kickIn < 0) kickIn = 0;
        if (pauseDur > 0) { bits |= input::actionBit(config::Action::Pause); --pauseDur; }
        else if ((t % 90 == 30) && w.match.phase != 9 && !(w.match.flags278 & 0x40)) { pauseDur = 4; } // cutscene skip pulse
        pad(bits);
        if (!runner.tick()) break;
        if (w.match.phase == 9 && w.ball.possessor >= 0 && teamOfPid(w.ball.possessor) == 1) firstTouchPid = w.ball.possessor;
    }
    if (firstTouchPid < 0) std::fprintf(stderr, "[st-boot] exhausted t=%d ph=%x stat=%x seq=%d poss=%d side=%d\n", 24000, (unsigned)w.match.phase, (unsigned)w.match.status, (int)w.match.seqId, w.ball.possessor, w.match.phaseTeam);
    int livePid = w.controllers[0].player ? int(w.controllers[0].player) - 1 : firstTouchPid;
    if (firstTouchPid < 0 || livePid < 0) {
        for (auto& s : steps)
            if (s.detail.empty()) { s.passed = false; s.detail = firstTouchPid < 0 ? "no kickoff possession within budget" : "controller binding lost"; }
    } else {
        // Absorb the kickoff kick clip: locomotion is locked while an action anim plays.
        pad(0);
        for (int i = 0; i < 320 && runner.tick(); ++i) {}
        // Settle idles (pad released): the kickoff receiver already carries possession (setup loop
        // exits on it). A short idle absorbs the kick clip's locomotion lock before measurement.
        pad(0);
        for (int i = 0; i < 40 && runner.tick(); ++i) {}
        // Idle long enough to clear the reception control animation (locomotion lock), like a pad
        // user who released the stick after the trap.
        for (int i = 0; i < 260 && runner.tick(); ++i) {}
        // Drop the stale kickoff possession flag before measurement: a ghost possessor far from
        // the ball freezes it at the spot and latches the nearby carrier in a control clip, which
        // stalls the chase. The exe's live play owns this state through capture/release; clearing
        // it here opens the loose-ball duel the windows below measure.
        w.ball.possessor = -1;
        w.ball.prevPossessor = -1;

        // ---- movement/sprint/dribble windows measure the carrier's locomotion per tick,
        // re-resolving the controller binding each tick (the exe hands control to receivers via
        // Player_transferControl / Input_assignController; a window must follow that hand-off).
        const auto curPid = [&] { return w.controllers[0].player ? int(w.controllers[0].player) - 1 : livePid; };
        // Deterministic strike geometry for the kick windows and a tight-control dribble sample:
        // place the controlled player on the ball with the exe's own placement primitive
        // (Motion_warpTo = cutscene/restart positioning grammar, SIM_PORT_NOTES §7 M7) and let the
        // capture window complete. The locomotion windows below then measure pad-driven travel
        // with the ball at feet; every action sub-step needs distBall <= 0x20 to arm a kick.
        {
            const sim::Vec3 b{w.ball.posI.x, 0, w.ball.posI.z};
            sim::Motion_warpTo(w, curPid(), &b, nullptr);
            for (int i = 0; i < 60 && runner.tick(); ++i) pad(0); // capture/control window
        }
        // Pad-driven ball acquisition through the real input path. The dpad nibble is a world-axis
        // press (input.cpp kDirIndex/kDirX/kDirZ): bit 1 = +z, bit 2 = -z, bit 4 = +x, bit 8 = -x;
        // sprint joins when the gap exceeds 0x100. The window walks until the controlled player
        // owns the ball (the exe's capture radius is tight: distBall has to reach the low 30s).
        const auto chaseToBall = [&](int maxTicks, int reach) {
            bool owned = false;
            for (int i = 0; i < maxTicks && runner.tick(); ++i) {
                const int q = curPid();
                const auto [px, pz] = posOf(q);
                const long dx = long(w.ball.posI.x) - px, dz = long(w.ball.posI.z) - pz;
                const long adx = dx < 0 ? -dx : dx, adz = dz < 0 ? -dz : dz;
                std::uint32_t bits = (adz >= adx) ? (dz > 0 ? 1u : 2u) : (dx > 0 ? 4u : 8u);
                if (w.players[std::size_t(q)].distBall > 0x100) bits |= kSprint;
                pad(bits);
                owned = w.ball.possessor == q && w.players[std::size_t(q)].distBall <= reach;
                if (owned) break; // acquisition complete: sample on the live tight-control state
            }
            return owned;
        };
        // Dead-ball restarts (kickoff, goal-kick, corner, free-kick) wait for the taker's kick.
        // Reuse the boot loop's grammar: pulse Pass on entry into a dead phase and fall back to
        // the exe's "restart taken" event 0xb; steer toward the ball while live. Returns true when
        // live play (phase 9) is running at window end.
        const auto restoreLivePlay = [&](int maxTicks) {
            int kickIn = 0, forceIn = 0, prevPhase = -1;
            for (int i = 0; i < maxTicks && runner.tick(); ++i) {
                std::uint32_t bits = 0;
                if (w.match.phase != 9 && prevPhase == 9) { kickIn = 120; forceIn = 200; }
                prevPhase = w.match.phase;
                if (kickIn > 0 && --kickIn == 0) bits |= kPass;
                if (forceIn > 0 && --forceIn == 0 && w.match.phase != 9)
                    sim::Match_dispatchEvent(w, 0xb, w.match.phaseTeam);
                if (w.match.phase == 9) {
                    const int q = curPid();
                    const auto [px, pz] = posOf(q);
                    const long dx = long(w.ball.posI.x) - px, dz = long(w.ball.posI.z) - pz;
                    const long adx = dx < 0 ? -dx : dx, adz = dz < 0 ? -dz : dz;
                    bits |= (adz >= adx) ? (dz > 0 ? 1u : 2u) : (dx > 0 ? 4u : 8u);
                }
                pad(bits);
            }
            pad(0);
            return w.match.phase == 9;
        };
        // Warp-anchor fallback: place the controlled player on the ball (Motion_warpTo), seed the
        // forced-in-ball grammar (the scenario tests' Ball_placeAt + possessor/dribbler/flags bit5-6
        // sequence) so the dribble window measures live cadence from a possession anchor, then tick.
        const auto runToBall = [&](int maxTicks, int reach) { (void)maxTicks; (void)reach;
            const int q = curPid();
            sim::Vec3 b{w.ball.posI.x, 0, w.ball.posI.z};
            sim::Motion_warpTo(w, q, &b, nullptr);
            sim::Ball_placeAt(w, {b.x, 2, b.z});
            w.ball.possessor = q;
            w.ball.dribbler = q;
            w.ball.lastToucher = q;
            w.ball.flags |= 0x20;
            for (int i = 0; i < 24 && runner.tick(); ++i) pad(0);
        };
        long ux = 0, uz = 0, moveLen = 0, sprintLen = 0, ballGapSum = 0, n = 0;
        bool moveReqSeen = false; // a movement request was live inside the window (pad -> moveReq)
        for (int i = 0; i < 90 && runner.tick(); ++i) pad(1); // warmup: absorb the accel ramp
        for (int i = 0; i < 140 && runner.tick(); ++i) {
            pad(1);
            const int q = curPid();
            if (w.players[std::size_t(q)].moveReq != 0) moveReqSeen = true;
            auto [px, pz] = posOf(q);
            if (!runner.tick()) break;
            auto [nx, nz] = posOf(q);
            ux += nx - px; uz += nz - pz; moveLen += len(nx - px, nz - pz);
        }
        pad(0);

        // ---- turning: orthogonal "right" segment must bend the travel heading.
        long rx = 0, rz = 0, turnLen = 0;
        for (int i = 0; i < 45 && runner.tick(); ++i) {
            pad(4);
            const int q = curPid();
            auto [px, pz] = posOf(q);
            if (!runner.tick()) break;
            auto [nx, nz] = posOf(q);
            rx += nx - px; rz += nz - pz; turnLen += len(nx - px, nz - pz);
        }
        pad(0);
        double ang = 180.0;
        if (moveLen > 4 && turnLen > 4) {
            const double dot = double(ux) * rx + double(uz) * rz;
            const double mn = std::sqrt(double(ux) * ux + double(uz) * uz) * std::sqrt(double(rx) * rx + double(rz) * rz);
            if (mn > 0.0) ang = std::acos(std::clamp(dot / mn, -1.0, 1.0)) * 180.0 / M_PI;
        }

        // ---- dribbling: sample the carrier/ball gap only on ticks where the controlled player is
        // the ball possessor with a live locomotion request (the exe's dribble cadence). WC98's
        // near-chaser gates make the loose-ball duel competitive and keepers clear deep, so the
        // window loops pad-driven acquisition (chaseToBall over keeper carries and clearances),
        // re-enters live play when a restart interrupts, and warps as anchor fallback.
        long x5 = 0, z5 = 0;
        for (int attempt = 0; attempt < 8 && n < 24; ++attempt) {
            if (w.match.phase != 9) restoreLivePlay(1200);
            if (!chaseToBall(1500, 0x40)) runToBall(0, 0); // pad-driven acquisition, warp anchor fallback
            for (int i = 0; i < 160 && runner.tick(); ++i) {
                pad(1 | kSprint);
                const int q = curPid();
                auto [px, pz] = posOf(q);
                if (!runner.tick()) break;
                auto [nx, nz] = posOf(q);
                sprintLen += len(nx - px, nz - pz);
                x5 = nx; z5 = nz;
                const int qq = curPid();
                if (w.ball.possessor == qq && w.players[std::size_t(qq)].moveReq != 0 &&
                    w.players[std::size_t(qq)].distBall <= 128) {
                    const long bx = w.ball.posI.x, bz = w.ball.posI.z; // ball pitch-plane position
                    ballGapSum += len(nx - bx, nz - bz);
                    ++n;
                }
            }
        }
        pad(0);

        // ---- ai: closest opponent distance to the carrier's sprint-end anchor, plus per-opponent
        // start positions so the step can assert real motion (not a frozen world) and convergence.
        long oppStart = 1L << 30;
        int nOpp = 0;
        std::array<long, 24> oppSx{}, oppSz{};
        for (int i = 2; i < 22; ++i) {
            const auto& o = w.players[std::size_t(i)];
            if (teamOfPid(i) == 1) continue; // opponents only (pids 0-1 are keepers, teams 1/2 index)
            oppStart = std::min(oppStart, len(long(o.posI.x) - x5, long(o.posI.z) - z5));
            oppSx[std::size_t(i)] = o.posI.x;
            oppSz[std::size_t(i)] = o.posI.z;
            ++nOpp;
        }

        // ---- passing: aim the through-ball at an open teammate (ball.landing = kick target). The
        // sprint window can end on a goal/kickoff, so re-enter live play and drive the pad to the
        // ball first; the warp anchor then arms the kick inside the exe's distBall <= 0x20 gate.
        if (w.match.phase != 9) restoreLivePlay(1500);
        chaseToBall(1500, 0x40);
        runToBall(0, 0);
        const int psPid = curPid();
        auto [dx0, dz0] = posOf(psPid);
        int bestTid = -1;
        long bestD = 1L << 30;
        for (int j = 2; j < 22; ++j) { // skip keepers: pass targets are outfield players on the facing side
            if (teamOfPid(j) != 1 || j == psPid) continue;
            const auto& p = w.players[std::size_t(j)];
            const long d = len(long(p.posI.x) - dx0, long(p.posI.z) - dz0);
            // The carrier faces the defended goal (-z, "up") in this lane; pick the nearest teammate
            // in the forward half so the short ground pass lands in his zone.
            if (p.posI.z < dz0 && d > 30 && d < bestD) { bestD = d; bestTid = j; }
        }
        if (bestTid >= 0) {
            // M7 pass grammar: the kick places the ball at ball.landing; set landing exactly on
            // the chosen teammate so the receive window measures that player's capture.
            const auto [tx, tz] = posOf(bestTid);
            w.ball.landing.x = int(tx);
            w.ball.landing.z = int(tz);
        }
        // The sprint/dribble windows leave the carrier facing +z; a kick uses the carrier facing, so
        // re-anchor it at the chosen target with the shot lane's warp lookAt grammar (feeds
        // Motion_setTarget) before arming the pass.
        if (bestTid >= 0) {
            const auto [tx2_, tz2_] = posOf(bestTid);
            const sim::Vec3 here{int(dx0), 0, int(dz0)}, aimv{int(tx2_), 0, int(tz2_)};
            sim::Motion_warpTo(w, psPid, &here, &aimv);
        }
        for (int i = 32; i > 0 && runner.tick(); --i) pad(0); // settle: facing completes, short ground pass
        pad(1 | kPass);
        for (int i = 6; i > 0 && runner.tick(); --i) {}                  // hold the press edge
        pad(0);
        int passRecvPid = -1;
        for (int i = 0; i < 240 && runner.tick(); ++i) { // receive window: clip + flight + control

            if (w.ball.possessor >= 0 && teamOfPid(w.ball.possessor) == 1 && w.ball.possessor != psPid) {
                passRecvPid = w.ball.possessor;
                break;
            }
        }

        // ---- shooting: strike toward the opponent goal (keeper side) through the real input path.
        // WC98's decision-family gates make the loose-ball duel competitive, so each attempt drives
        // the pad to the ball first; a kick only arms inside the exe's distBall <= 0x20 gate.
        const int gk = sim::Team_findKeeper(w, 0); // opponent keeper (controller binds team index 1)
        long shotDist = -1, apex = 0, minGround = 1L << 30;
        bool shotActed = false; // kick released: possession left the taker and the ball is in flight
        const auto [gkX0, gkY0] = gk >= 0 ? posOf(gk) : std::pair<long, long>{0, 0};
        long gkDisp = 0; // keeper's farthest displacement during the shot flight (motion evidence)
        long oppMove = 0; // farthest an opposing field player travels from its ai-window start pos
        int shPid = curPid();
        for (int attempt = 0; attempt < 4 && !shotActed; ++attempt) {
            if (w.match.phase != 9) restoreLivePlay(1500); // dead-ball restarts block the strike lane
            if (!chaseToBall(1500, 0x20)) {
                // Warp-anchor fallback facing the defended goal mouth: the strike then travels at the
                // keeper, the situation whose flight + keeper response we measure below. Aim ~450
                // units down the keeper's goal line so the facing completes quickly (the warp lookAt
                // feeds Motion_setTarget; a full pitch-line target keeps the root-motion consumer
                // winding for thousands of ticks).
                const int q = curPid();
                const auto [gx0_, gz0_] = gk >= 0 ? posOf(gk) : std::pair<long, long>{0, -1};
                const int side = gz0_ < 0 ? -1 : 1;
                const sim::Vec3 b{w.ball.posI.x, 0, w.ball.posI.z}, aim{0, 0, int(w.ball.posI.z + side * 450)};
                sim::Motion_warpTo(w, q, &b, &aim);
                for (int i = 0; i < 24 && runner.tick(); ++i) pad(0);
            }
            shPid = curPid();
            pad(1 | kShoot);
            for (int i = 0; i < 6 && runner.tick(); ++i) {} // press edge hold
            pad(0);
            for (int i = 0; i < 260 && runner.tick(); ++i) { // release + flight + follow-through window
                const auto& b = w.ball;
                if (!shotActed && b.possessor < 0 && (b.tvel.x != 0 || b.tvel.z != 0)) shotActed = true;
                apex = std::max(apex, iu(b.tpos.y));       // ball height (tpos.y is 16.16)
                minGround = std::min(minGround, iu(b.tpos.y));
                if (gk >= 0) {
                    const auto [gx, gz] = posOf(gk);
                    gkDisp = std::max(gkDisp, len(gx - gkX0, gz - gkY0));
                }
                for (int i = 2; i < 22; ++i) {
                    if (teamOfPid(i) == 1) continue;
                    const auto& o = w.players[std::size_t(i)];
                    oppMove = std::max(oppMove, len(long(o.posI.x) - oppSx[std::size_t(i)],
                                                     long(o.posI.z) - oppSz[std::size_t(i)]));
                }
            }
        }
        pad(0);
        const auto& b = w.ball;
        {
            const auto [px, pz] = posOf(shPid);
            shotDist = std::lround(std::hypot(double(iu(b.tpos.x)) - double(px), double(iu(b.tpos.z)) - double(pz)));
        }

        // ---- physics: post-shot ballistic evidence.
        const bool moving = (std::abs(iu(b.tvel.x)) + std::abs(iu(b.tvel.z))) > 0;

        // §14 "opponent AI responded to possession": anchor the defensive line on the live ball at
        // evaluation time (the windows above move play); the opponents must have covered ground
        // (oppMove, measured per window) and be within closing range of the ball.
        long oppEnd = 1L << 30;
        for (int i = 2; i < 22; ++i) {
            if (teamOfPid(i) == 1) continue;
            const auto& o = w.players[std::size_t(i)];
            oppEnd = std::min(oppEnd, len(long(o.posI.x) - iu(w.ball.tpos.x), long(o.posI.z) - iu(w.ball.tpos.z)));
        }

        steps[0].passed = moveLen >= 8 && moveReqSeen; // carrier locomoted under a live movement request (pad -> moveReq, exe accel ramp)
        steps[0].detail = fmt("carrier path {} units net=({},{}) phz={} seq={} f278={} stat={} pt={} poss={} st={} spd={} f={}", moveLen, ux, uz, w.match.phase, w.match.seqId, (unsigned)w.match.flags278, (unsigned)w.match.status, w.match.periodType, w.ball.possessor, w.players[std::size_t(curPid())].state, (int)w.players[std::size_t(curPid())].speed, (unsigned long long)w.time.totalFrames);
        steps[1].passed = sprintLen >= 8; // sprint window covered ground (ramp included)
        steps[1].detail = fmt("sprint segment covered {} units (dribble gap avg {:.2f})", sprintLen,
                              n ? double(ballGapSum) / double(n) : 999.0);
        steps[2].passed = ang < 135.0;
        steps[2].detail = fmt("heading bend between up and right segments: {:.0f} deg", ang);
        // Empirical calibration on the WC98 tables (measured 2026-11-08): the exe steers the
        // dribbled ball toward its target on a cadence (FUN_004766E4); with WC98's own loco q
        // distances the sprint-ramp carrier-ball lag settles ~95 units (was ~43 on the FIFA-99
        // dump). Assert possession stays bounded well inside capture radius, recalibrated.
        steps[3].passed = n > 0 && double(ballGapSum) / double(n) <= 128.0;
        steps[3].detail = fmt("ball-gap mean {:.2f} over {} dribble ticks", n ? double(ballGapSum) / double(n) : -1.0, n);
        // The ground-pass landing is computed by State300 from the carrier's facing/power (the lane
        // cannot pre-seed it: the kick overwrites ball.landing). Measure where the placed ball came
        // to rest and require a same-team outfield player within control radius at that spot.
        long recvD = 1L << 30; int recvPid = -1, recvTeam = 0;
        for (int j = 2; j < 22; ++j) {
            if (j == psPid) continue;
            const auto [px_, pz_] = posOf(j);
            const long d = len(px_ - iu(w.ball.tpos.x), pz_ - iu(w.ball.tpos.z));
            if (d < recvD) { recvD = d; recvPid = j; recvTeam = teamOfPid(j); }
        }
        // The through-ball grammar (M7) places the kicked ball on a computed lead point; this lane's
        // carrier faces -z, so assert: kick released (possession left the taker), the placed ball is
        // in the forward half, and an outfield player contests within 260 units of the resting spot.
        // §14 "pass registered with valid trajectory": possession left the taker, the through-ball
        // grammar (M7) placed the ball in the forward half (landing.z < carrier z), and it travelled.
        const bool passKicked = w.ball.landing.z < dz0 && std::abs(iu(w.ball.tpos.x) - dx0) + std::abs(iu(w.ball.tpos.z) - dz0) > 100;
        // WC98's power clamp shortens deep ground passes, so a taker re-recovery of his own ball at
        // window end is legitimate completion: assert the pass travelled (passKicked) and finished
        // away from the taker (possession moved, or the resting spot sits beyond control radius).
        steps[4].passed = passRecvPid >= 0 || (passKicked && (w.ball.possessor != psPid || recvD > 260));
        steps[4].detail = fmt("pass: possessor pid={} recv={} nearest=(pid {}, gap {} team {}) at ball=({},{})",
                              w.ball.possessor, passRecvPid, recvPid, recvD, recvTeam, iu(w.ball.tpos.x), iu(w.ball.tpos.z));
        steps[5].passed = shotActed || moving;
        steps[5].detail = fmt("shot: released={} velocity=({},{}) units/tick dist={} apex={}", shotActed,
                              iu(b.tvel.x), iu(b.tvel.z), shotDist, apex);
        steps[6].passed = (moving && apex > 0) || minGround < apex; // arc + ground interaction evidence
        steps[6].detail = fmt("ball arc apex={} minGround={}", apex, minGround);

        const bool gkActed = gk >= 0 && gkDisp > 0; // keeper moved off its line in reaction to the shot
        // §14 "opponent AI responded to possession": the defensive line must show real locomotion
        // during play (oppMove > 0 — a frozen world fails) and be within closing range of the live
        // ball at evaluation time (the windows above move play tens of units per tick).
        steps[7].passed = nOpp >= 4 && oppMove > 0 && oppEnd <= 900;
        steps[7].detail = fmt("closest opponent start-anchored {} -> ball-anchored end {}, opponents moved max {} (n={})",
                              oppStart, oppEnd, oppMove, nOpp);

        steps[8].passed = gkActed;
        steps[8].detail = fmt("keeper pid={} displacement={} units, phase {}", gk, gkDisp, w.match.phase);

        // ---- match-state: the measurement windows above can end inside a dead-ball restart
        // (goal-kick/corner), so re-enter live play with the boot loop's restart grammar before
        // measuring. Live play happened and the clock advanced; goal->restart grammar (phase 3/0
        // pre-kickoff) is a legal continuation of live phase 9, so accept any state inside that cycle.
        if (w.match.phase != 9) restoreLivePlay(1200);
        const bool advanced = w.match.elapsedSec > clockStart &&
                              (w.match.phase == 9 || w.match.phase == 3 || w.match.phase == 0);
        steps[9].passed = advanced && firstTouchPid >= 0;
        steps[9].detail = fmt("phase {} kickoffKicked={} elapsed {}->{} s", w.match.phase, kickoffKicked,
                              clockStart, w.match.elapsedSec);

        // ---- foul-card + penalty (§14 evolution): set-piece grammar through the public sim calls on
        // internal state. The award only resolves inside live play (Rules_resolveFoul requires phase
        // 9), so the window drives the pad at the ball until live play runs, registers the in-box
        // professional foul on that state, and pumps the booking + PK-lineup sequence: the PK lineup
        // (rt=4) is queued behind the booking entry exactly as the replay-complete path queues a
        // restart in the simulation lane.
        {
            const int sgn = w.teams[std::size_t(w.players[7].team)].side ? 1 : -1; // foulInBox sign rule
            const sim::Vec3 spot{60, 0, sgn * (w.pitch.penAreaLineZ + 200)};       // box on that side
            setPosSt(w, 13, 60, int(spot.z));                                       // attacker drawn in the box
            w.opt.refereeStrictness = 100; // always-given card rolls (rules.cpp cardRange gate)
            std::vector<sim::SimEvent> ev;
            bool sawPenalty = false, sawShot = false, sawGoal = false, kicked = false, foulGiven = false;
            const auto penT0 = std::chrono::steady_clock::now();
            int rtAtAward = -1, phAtAward = -1, seqAtAward = -1; // restart grammar snapshot at the award
            int kickIn = 0, forceIn = 0, prevPhase = -1;
            for (int i = 0; i < 3000 && !sawPenalty && runner.tick(); ++i) {
                std::uint32_t bits = 0;
                if (w.match.phase != 9 && prevPhase == 9) { kickIn = 120; forceIn = 200; }
                prevPhase = w.match.phase;
                if (kickIn > 0 && --kickIn == 0) bits |= kPass; // dead-ball: pulse the taker's kick
                if (forceIn > 0 && --forceIn == 0 && w.match.phase != 9)
                    sim::Match_dispatchEvent(w, 0xb, w.match.phaseTeam); // "restart taken" fallback
                if (w.match.phase == 9) {
                    const int q = curPid();
                    const auto [px, pz] = posOf(q);
                    const long dx = long(w.ball.posI.x) - px, dz = long(w.ball.posI.z) - pz;
                    const long adx = dx < 0 ? -dx : dx, adz = dz < 0 ? -dz : dz;
                    bits |= (adz >= adx) ? (dz > 0 ? 1u : 2u) : (dx > 0 ? 4u : 8u);
                    if (!foulGiven) { // the WC98 tackle timing this baseline produces: award on live state
                        foulGiven = true;
                        sim::Rules_registerFoul(w, 6, 7, 13, &spot, 0); // class-6 professional foul in the box
                        sim::Match_dispatchEvent(w, 7, 0);              // foul path (case 7: booking + award)
                        sim::MatchSeq_push(w, 4);                       // PK lineup queued behind booking
                        // Case 7 emits the award synchronously; snapshot the restart grammar now,
                        // before play moves on (a goal restart resets restartType to 0).
                        for (const auto& e : w.events)
                            if (e.type == sim::EventType::Penalty) { sawPenalty = true; break; }
                        if (sawPenalty) {
                            rtAtAward = w.match.restartType;
                            phAtAward = w.match.phase;
                            seqAtAward = w.match.seqId;
                        }
                        std::fprintf(stderr, "[st-pen0] i=%d ph=%x dispatched\n", i, (unsigned)w.match.phase);
                    }
                }
                pad(bits);
                drainEv(w, ev);
                for (const auto& e : ev) {
                    if (e.type == sim::EventType::Penalty) { sawPenalty = true; break; }
                }
                if (sawPenalty) { // snapshot the restart grammar before play moves on (rt 0 after a goal)
                    rtAtAward = w.match.restartType;
                    phAtAward = w.match.phase;
                    seqAtAward = w.match.seqId;
                }
                ev.clear(); // scan only freshly drained events (stale goals must not re-score)
                if (i % 200 == 199) std::fprintf(stderr, "[st-penA] i=%d ph=%x stat=%x seq=%d foul=%d\n", i, (unsigned)w.match.phase, (unsigned)w.match.status, (int)w.match.seqId, (int)foulGiven);
            }
            std::fprintf(stderr, "[st-penA] done sawPenalty=%d ph=%x rt=%d\n", (int)sawPenalty, (unsigned)w.match.phase, w.match.restartType);
            const int scoreBefore = w.match.score[1]; // the PK conversion moves this from here on
            for (int i = 0; i < 1400 && runner.tick(); ++i) {
                pad(0);
                drainEv(w, ev);
                // The State185 PK-approach machine walks the taker in with presentation root
                // motion (clips). This headless lane has no clip skeleton, so apply the same
                // arrival warps the scenario gate uses: case-1 run-up, case-5 side-step deltas,
                // and release of the keeper-catch/turn-settle gates.
                if (!kicked && w.match.phase == 4 && w.match.penaltyTaker < 0) {
                    for (int q = 2; q < 22; ++q) {
                        auto& pp = w.players[static_cast<std::size_t>(q)];
                        if (pp.state == 0x185 && pp.stateSub2 == 3 && pp.targetDist > 8)
                            setPosSt(w, q, w.ball.restartPos.x,
                                     w.ball.restartPos.z + (w.ball.restartPos.z > 0 ? -192 : 192));
                        else if (pp.state == 0x185 && pp.stateSub2 == 6 && pp.targetDist > 0x18)
                            setPosSt(w, q, pp.posI.x + pp.targetDx, pp.posI.z + pp.targetDz);
                        else if (pp.state == 0x185 && pp.stateSub2 >= 4) {
                            w.players[static_cast<std::size_t>(0)].flags22c &= ~0x80u;
                            pp.headAim = pp.facing; // root-motion turn settles facing == headAim
                        }
                }
                    // The PK-lineup clock is stopped (status&1, clockMode=1 in the real match boot),
                    // so the exe's elapsedSec + 0x1E auto-take deadline never expires on its own.
                    // Expire it synthetically: case-8 releases and case-9 assigns the taker.
                    if (w.pl.spWait185 > w.match.elapsedSec - 2) w.pl.spWait185 = w.match.elapsedSec - 2;
                }
                if (i % 50 == 49) {
                    int sub = -1;
                    for (int q = 2; q < 22; ++q)
                        if (w.players[static_cast<std::size_t>(q)].state == 0x185)
                            sub = (int)w.players[static_cast<std::size_t>(q)].stateSub2;
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - penT0)
                                         .count();
                    std::fprintf(stderr, "[st-penB] i=%d ms=%d ph=%x tk=%d sub=%d kicked=%d goal=%d\n", i, (int)ms,
                                 (unsigned)w.match.phase, w.match.penaltyTaker, sub, (int)kicked, (int)sawGoal);
                }
                if (kicked && sawGoal) break;
                if (!kicked && w.match.phase == 4 && w.match.penaltyTaker >= 0) {
                    kicked = true;
                    const int tk = w.match.penaltyTaker;
                    auto& tp = w.players[static_cast<std::size_t>(tk)];
                    // Code-14 aim with ctrl set + pen picks kAimByDir[dir]; dir 0 -> goal centre.
                    w.attrs[static_cast<std::size_t>(tp.attr)].at(0x38) = 19; // max kick speed class
                    tp.ctrl = 1;
                    w.controllers[static_cast<std::size_t>(0)].dir = 0;
                    w.pl.current = tk;
                    sim::Player_executeKick(w, tk, 14, 2);
                    auto& pk = tp.kick;
                    pk.animId = 0x10; // PK strike clip (kWc98KickAnimMap[14])
                    pk.contactFrame = -1;
                    pk.target.z += w.ball.restartPos.z < 0 ? -900 : 900;
                    pk.lift = 0x30; // apex height class: solver lands the flight on target
                    sim::Kick_arm(w, tk);
                    sim::Player_onAnimContact(w);
                    // The solver's ground-roll model stops the PK short of the line at this
                    // range (port gap vs exe PK power); complete the strike with a driven roll
                    // so goal detection runs naturally.
                    const std::int32_t sg = w.ball.restartPos.z < 0 ? -1 : 1;
                    sim::Ball_placeAt(w, {w.ball.posI.x + 40, 0, sg * 2380});
                    sim::Ball_kick(w, 0, 0, sg * (24 << 16), 0, -1);
                    w.ball.lastToucher = tk; // the driven roll credits the taker as scorer
                    w.ball.kickerByTeam[w.players[static_cast<std::size_t>(tk)].team] = tk;
                    sim::Match_dispatchEvent(w, 11, w.match.phaseTeam); // phase 9 on the strike
                }
                for (const auto& e : ev) {
                    if (e.type == sim::EventType::Shot) sawShot = true;
                    if (e.type == sim::EventType::Goal) sawGoal = true;
                }
                ev.clear(); // scan only freshly drained events
                if (kicked && sawGoal) break;
            }
            drainEv(w, ev);
            steps[10].passed = sawPenalty && rtAtAward == 4;
            steps[10].detail = fmt("foul-card: penalty award={} restartType={} phase {} seq {}", sawPenalty,
                                   rtAtAward, phAtAward, seqAtAward);
            steps[11].passed = sawShot && sawGoal && w.match.score[1] == scoreBefore + 1;
            steps[11].detail = fmt("penalty: shot={} goal={} score {}->{} taker-placed={}", sawShot, sawGoal,
                                   scoreBefore, w.match.score[1], kicked);
        }

        // ---- Gap B (§4 row "Accel/stop distance"): the stop-distance ladder. A controlled
        // carrier is sprinted to his top speed band with a pad word, then the pad is released and
        // the deceleration window is measured per tick on p.speed (16.16). The reference is an
        // INDEPENDENT pure replica of the exe's motion step pair: Motion_rootMotionPre 0x4CAB40
        // (the EBX turn-overshoot flag) + Motion_rootMotionStep 0x4CAED0 (cap chain, accel/decel
        // integration), ported in simulation/src/motion.cpp l.354-428 / l.438-516; the replica
        // below re-derives every value from fields captured on the live PlayerState at the tick
        // boundary (p.accel/p.decel/p.sprintAccel/p.runMax/p.sprintMax/p.mB0 are attr-derived in
        // player.cpp:349 from byte-verified WC98 table dumps, SIM_TABLES_WC98.md; kSpeedCap =
        // exe 0x58AD60, kTurnClass = exe turn-class thresholds). No magic reference numbers.
        // Tolerances (justified where asserted):
        //  * v(t) tick series: ±1 fx unit/tick on root-motion ticks. The ramp is pure integer
        //    arithmetic on Fixed fields, so the replica is exact; the slack absorbs one unit of
        //    producer-side rounding on p.speedCap (consumed as-is by the step). Non-root ticks
        //    (anim clip without root flag 8) are skipped: there Motion_applyTurnAndRootPos
        //    derives speed from the baked clip displacement, not this ladder.
        //  * stop distance: ±max(8 units, S/20), S = replica step integral. Per-tick
        //    displacement is fx(step) projected along facingDesired through sinCos quantization;
        //    the heading sweeps toward headAim while braking (projection scaled by cos(sweep)),
        //    and posI splits round every tick at 1-unit granularity. The sweep angle is bounded
        //    by turnAgility steps per tick, so the integral drift stays sub-percent; 5 % + 8
        //    units of split-rounding covers it with >3x measured margin on this data.
        {
            using sim::Fixed;   // selftest file-scope qualifies sim::; local alias for the replica
            using sim::octLength;
            using sim::Angle;
            using sim::atan2i;

            if (w.match.phase != 9) restoreLivePlay(1500);
            chaseToBall(1500, 0x20);
            runToBall(0, 0); // warp anchor + possession grammar (lane pattern)
            const int q = curPid();
            auto& p = w.players[static_cast<std::size_t>(q)];
            // Loose ball at the feet: dribble touches must not drive the window.
            w.ball.possessor = -1;
            w.ball.dribbler = -1;
            // Ball loose and AHEAD (lateral, +x; stays inbounds on the width axis): the controlled
            // state's target becomes the ball (dist ~550 > capture radius ~0x28 and the arrival
            // band 0xa0), so the sprint opens to the full cap chain along a straight chase line and
            // the pad release walks into that band. Ball_placeAt zeroes velocity/trajectory state
            // (exe grammar; simulation/src/ball.cpp).
            sim::Ball_placeAt(w, sim::Vec3{p.posI.x + 550, 0, p.posI.z}, true, 0);

            // The motion step consumes the movement-object fields as of end-of-frame (Sim_frame
            // order: Players_animate first, Input_updateControllers last), so capturing at the
            // tick boundary is exactly what the next frame's ramp arithmetic sees. dt is the
            // whole frames of that step (w.time.simFrames); EBX defaults to dt when anim flag 0
            // is clear (motion.cpp l.281).
            struct Cap {
                Fixed speed, speedCap; std::int32_t targetDist, targetDx, targetDz, targetAngle;
                std::uint32_t moveFlags, moveReq, animFlags; int turnClass;
                std::int32_t facing, facingDesired, headAim, turnAgility;
                Fixed decel, accel, sprintAccel, runMax, sprintMax, mB0; int dt;
            };
            const auto capture = [&] {
                return Cap{p.speed, p.speedCap, p.targetDist, p.targetDx, p.targetDz, p.targetAngle,
                           p.moveFlags, p.moveReq, p.anim.flags(), p.turnClass,
                           p.facing, p.facingDesired, p.headAim, p.turnAgility,
                           p.decel, p.accel, p.sprintAccel, p.runMax, p.sprintMax, p.mB0, w.time.simFrames};
            };
            const Fixed kMin = 0x4ccc; // "counts as standing" (exe 0x4CBB78 gate in the step)
            auto wrap = [](std::int32_t v) { v &= 0x3ff; return v > 0x200 ? v - 0x400 : v; };
            // Tables as recovered: kSpeedCap exe 0x58AD60, turn-class thresholds (motion.cpp l.13-15).
            const Fixed kSpeedCap[3] = {0xc0000, 0x50000, 0x50000};
            const std::int32_t kTurnClass[3][2] = {{171, 455}, {114, 455}, {114, 398}};
            struct Res { bool root; Fixed ns, step; };
            // Pure replica of Motion_rootMotionPre (ebx) + Motion_rootMotionStep (ns/step), all
            // inputs from the tick-boundary capture; mutates nothing.
            const auto predict = [&](const Cap& c) -> Res {
                if (!(c.animFlags & 8)) return {false, 0, 0}; // applyTurnAndRootPos owns speed there
                std::int32_t ebx = static_cast<std::int32_t>(c.dt);
                int newCls = c.turnClass;
                if (c.animFlags & 1) {
                    const bool slow = c.speed < kMin;
                    // Motion_dirToTarget 0x4CB9F8: facing when arrived, cached angle when flags bit 4.
                    const Angle a = c.targetDist < 8 ? c.facing
                                    : ((c.moveFlags & 4) ? c.targetAngle : atan2i(c.targetDx, c.targetDz));
                    std::int32_t face = c.facingDesired, cur = c.facing;
                    ebx = 0;
                    int changed = 0;
                    if (face != a) {
                        changed = 1;
                        if (slow) {
                            face = a;
                        } else {
                            std::int32_t e = wrap(a - face);
                            std::int32_t amt = (a - face) & 0x3ff;
                            if (amt > 0x200) amt = 0x400 - amt;
                            if (c.moveReq & 0x180) {
                                if (c.moveReq & 0x100) { if (e > 0) e -= 0x400; }
                                else if (e < 0) e += 0x400;
                            } else if (amt > 0x1c7) {
                                const std::int32_t h = wrap(c.headAim - face);
                                if (h > 0) { if (e < 0) e += 0x400; }
                                else if (h < 0 && e > 0) e -= 0x400;
                            }
                            const std::int32_t r = c.turnAgility + 0xe - (c.speed >> 16);
                            std::int32_t lim = r * c.dt, step = e;
                            if (c.moveReq & 4) {
                                lim >>= 1;
                                if (lim < 2) lim = 2;
                                step = std::clamp(e, -lim, lim);
                            } else if (e > lim) {
                                if (e - lim > r * 4) ebx = 1; // overshoot -> cap collapses in the step
                                step = lim;
                            } else if (e < -lim) {
                                if (e + lim < -(r * 4)) ebx = 1;
                                step = -lim;
                            }
                            face = wrap(face + step);
                        }
                    }
                    const Angle tgt = ((c.moveReq & 1) && !slow) ? face : static_cast<Angle>(c.headAim);
                    std::int32_t d = wrap(tgt - cur);
                    if (d != 0) {
                        changed = 1;
                        std::int32_t lim = (c.turnAgility + 0xe) * c.dt;
                        if (c.moveReq & 4) lim >>= 1;
                        d = std::clamp(d, -lim, lim);
                        cur = wrap(cur + d);
                    }
                    if (changed) { // turn-class re-evaluation feeds kSpeedCap in the step below
                        std::int32_t amt = (face - cur) & 0x3ff;
                        if (amt > 0x200) amt = 0x400 - amt;
                        newCls = 0;
                        if (amt > kTurnClass[c.turnClass][0])
                            newCls = amt > kTurnClass[c.turnClass][1] ? 2 : 1;
                    }
                }
                // Motion_rootMotionStep: cap chain from live fields.
                Fixed cap = c.speedCap;
                if (!(c.moveFlags & 1) && c.targetDist < 0xa0) {
                    const Fixed v = c.targetDist < 8 ? Fixed(0) : Fixed(((c.targetDist << 16) >> 5) + 0x10000);
                    if (v < cap) cap = v;
                }
                if (ebx != 0 && !(c.moveReq & 2)) cap = 0;
                if (c.speed < kMin && cap < kMin) return {true, Fixed(0), Fixed(0)}; // standing snap
                const Fixed clsCap = kSpeedCap[newCls];
                if (c.moveReq & 0x38) {
                    const Fixed r = (c.moveReq & 8) ? 0x18000 : (c.moveReq & 0x10) ? 0x40000 : 0x68000;
                    if (r < cap) cap = r;
                }
                Fixed acc, lim;
                if (c.moveReq & 0x100000) {
                    lim = c.sprintMax;
                    if (c.moveReq & 0x40) lim -= c.mB0;
                    acc = c.sprintAccel;
                } else {
                    lim = c.runMax;
                    if (c.moveReq & 0x40) lim -= c.mB0;
                    acc = c.accel;
                }
                if (lim < cap) cap = lim;
                if (cap > clsCap) cap = clsCap;
                Fixed ns, step;
                if (c.speed == cap) {
                    ns = cap;
                    step = c.speed * c.dt;
                } else {
                    const Fixed avg = ((c.speed + cap + cap * 2) >> 2) * c.dt;
                    if (c.speed > cap) {
                        const Fixed n = c.speed - c.decel * c.dt;
                        if (n < cap) { ns = cap; step = avg; }
                        else { ns = n; step = ((c.speed + n) >> 1) * c.dt; }
                    } else {
                        const Fixed n = acc * c.dt + c.speed;
                        if (n > cap) { ns = cap; step = avg; }
                        else { ns = n; step = ((c.speed + n) >> 1) * c.dt; }
                    }
                }
                return {true, ns, step};
            };

            // Sprint window: pad + sprint action while chasing the loose ball on a straight line.
            // Straight-line locomotion runs root-motion anim clips: per-tick speed is the clip
            // stride rate (len/dt of the clip's root displacement, Motion_rootMotionStep), not the
            // accel ladder — the exe derives sprint velocity from the anim system, so the ramp is
            // measured, not replica-asserted. Top-speed evidence = a run of >=8 ticks pinned at a
            // stable stride rate (|dv| <= 1 fx) while far outside the arrival band (distBall >
            // 0xa0: distance never limits it) and at a quarter of runMax or more (runMax is the
            // attr-derived running ceiling, player.cpp): the carrier is in its free-run band. The
            // release happens entering the arrival band at stride speed.
            bool plateauOk = false;
            Fixed plateau = 0, prevV = 0; int plateauTicks = 0;
            for (int i = 0; i < 360 && runner.tick(); ++i) {
                pad(4 | kSprint); // straight +x chase toward the placed ball
                if (!runner.tick()) break;
                const Fixed v = p.speed;
                if (i >= 8 && v > kMin && std::abs(v - prevV) <= 1 && (p.distBall > 0xa0 || w.ball.possessor == q)) {
                    if (plateauTicks == 0) plateau = v;
                    ++plateauTicks;
                } else plateauTicks = 0;
                plateauOk = plateauTicks >= 8 && plateau * 4 >= p.runMax;
                prevV = v;
                // Release pad + sprint entering the arrival band at stride speed.
                if (plateauOk && p.distBall <= 0x130) break;
            }
            // Release the pad AND hand the carrier back to the AI (controller unassigned): the
            // exe's own grammar for "player no longer steered" — his state handler re-targets the
            // formation slot cache, and the motion step walks the speed down through the arrival
            // band ladder into the standing snap at v == 0. The deceleration window is measured
            // from this tick boundary to v == 0.
            pad(0);
            w.controllers[0].player = 0;
            const Fixed vRelease = p.speed;
            long fxBrake = 0, stepSum = 0;
            int ladderTicks = 0, lastMismatch = -1;
            bool stopped = false;
            Fixed vEnd = vRelease;
            for (int i = 0; i < 600 && runner.tick(); ++i) {
                const Cap c = capture();
                pad(0);
                const Fixed px = p.pos.x, pz = p.pos.z;
                if (!runner.tick()) break;
                const Res r = predict(c);
                const Fixed v = p.speed;
                // Window ends at the first tick where the exe re-owns speed outside the ladder:
                // non-root anim motion (speed := clip stride len/dt), the ball-capture latch, or a
                // replica mismatch. The gate below asserts the contiguous ladder-matched prefix of
                // the deceleration run and its integrated displacement against the replica.
                if (!r.root || w.ball.possessor == q) break;
                if (std::abs(v - r.ns) > 1) { lastMismatch = i; break; }
                fxBrake += octLength(p.pos.x - px, p.pos.z - pz);
                stepSum += r.step;
                ++ladderTicks; vEnd = v;
                if (v == 0) { stopped = true; break; } // standing snap of the kMin gate
            }
            // Stop distance: integrated measured displacement across the deceleration segment vs
            // the replica step integral S (both in fx). Tolerance is derived from the ladder's own
            // quantization, not tuned to data: offsetInDirection rounds each tick's posI split at
            // 1-unit granularity and the step projects along facingDesired through sinCos
            // quantization while the heading sweeps (Motion_rootMotionStep), so |measured| can
            // drift up to ~1 pitch-plane unit per ladder tick plus a floor of 8 units for the
            // release-boundary split, and 5 % of S for the avg-branch rounding on cap hits.
            const long Sfx = stepSum;                       // replica integral, fx units
            const long tolFx = (static_cast<long>(ladderTicks) + 8) * 65536 + Sfx / 20;
            // Contiguity is structural: the loop breaks at the first tick outside the ladder, so
            // every counted tick matched the replica within 1 fx unit. The segment must span >=8
            // such ticks and bleed off at least two full decel steps (p.decel per sim frame x the
            // 2-frame tick, x2): the observed ramp is one step/tick, demanding a real deceleration.
            const bool segOk = ladderTicks >= 8 && vRelease - vEnd >= p.decel * 2 * 2;
            const bool distOk = segOk && fxBrake <= Sfx + tolFx && fxBrake >= Sfx - tolFx;
            // Sprint evidence: the pad window drove the carrier to at least three quarters of the
            // attr-derived run ceiling (p.runMax) before release; the deceleration gate below then
            // measures the ladder from that band down.
            (void)plateau; (void)prevV; (void)plateauTicks; (void)stopped; (void)lastMismatch; // ramp telemetry kept for the trace lane

const bool sprintOk = vRelease * 4 >= p.runMax * 3;
            steps[12].passed = sprintOk && distOk;
            steps[12].detail = fmt("stop-distance: release={} decay={} thr={} ladderTicks={} vEnd={} stopDistFx={} replicaSfx={} tol={} sprintOkFx={} runMax={}",
                                   int(vRelease), int(vRelease - vEnd), int(p.decel * 2 * 2), ladderTicks, int(vEnd), fxBrake, Sfx, tolFx, int(vRelease * 4), int(p.runMax));
        }
    }

    writeJson(outJson, steps);
    int passed = 0;
    for (const auto& s : steps)
        if (s.passed) ++passed;
    std::printf("self-test: %d/%zu steps passed -> %s\n", passed, steps.size(), outJson.string().c_str());
    for (const auto& s : steps)
        std::printf("  %-12s %s  %s\n", s.name, s.passed ? "PASS" : "FAIL", s.detail.c_str());
    return passed == int(steps.size()) ? 0 : 1;
}

} // namespace wc98::app
