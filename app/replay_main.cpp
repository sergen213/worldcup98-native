// wc98-replay — deterministic replay CLI (docs/asset-formats/RPLY.md, app/replay.hpp).
//   wc98-replay <file.dat>            re-drive on WC98_TREE/--tree, print hashes + verdict
//   wc98-replay --gate [outdir]       record a seed-pinned scripted scenario into outdir
//                                     (rplyNNNN.dat + rplyindx.dat), then re-drive it and assert
//                                     final state-hash equality (ctest gate; skip-2 without tree)
#include <format>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "replay.hpp"
#include "wc98/formats/rply.hpp"

#include "livematch.hpp" // MatchBoot: shared exhibition bootstrap (app/livematch.cpp)

#include "wc98/sim/sim.hpp"

#include "wc98/sim/world.hpp"

namespace wc98::app {
namespace {
namespace fs = std::filesystem;

// Scripted pad stream: no SDL/Input layer — the gate drives controller 0 directly with a fixed
// pattern (movement sweep + sprint, periodic pass/shoot pulses), plus the live loop's center-
// restart kick protocol (pass pulse on phase 0 entry, force event 0xb after 90 ticks).
struct Scenario {
    std::uint32_t prevBits = 0;

    sim::ControllerState padAt(std::uint32_t tick) {
        sim::ControllerState c{};
        static constexpr int kVec[9][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {0, 0}};
        const int dirIdx = (tick / 37) % 9; // slow sweep so movement vectors change mid-match
        c.dir = c.rawDir = dirIdx;
        c.dirX = kVec[dirIdx][0];
        c.dirZ = kVec[dirIdx][1];
        c.bits = c.raw = (dirIdx != 8 ? 0x100u : 0u) | (tick % 45 == 20 ? 0x20u : 0u) | (tick % 67 == 40 ? 0x40u : 0u);
        return c; // pressed edges folded in by the caller after kick-pulse overrides
    }
};

int gate(const fs::path& tree, const fs::path& outDir) {
    MatchBoot boot;
    if (!boot.init(tree)) return 2; // prints "skip: ..." like the other tree gates
    std::error_code ec;
    fs::create_directories(outDir, ec);

    constexpr std::uint32_t kSeed = 0x2E7A; // test_sim_scenarios seed (pinned)
    sim::SimRunner runner(boot.md, boot.opt, kSeed);
    auto& w = runner.world();
    sim::Input_assignController(w, 0, 1, 0);

    ReplayRecorder rec;
    Scenario sc;
    std::uint32_t tick = 0;
    for (; tick < 400; ++tick) { // pure recorded input stream: everything padAt() emits is replayable
        sim::ControllerState c = sc.padAt(tick);
        c.pressed = c.bits & ~sc.prevBits; // Input edge semantics: newly-set button bits
        sc.prevBits = c.bits;
        runner.setController(0, c);
        rec.append(tick, c);
        if (!runner.tick()) break; // match ended early: seal at the real end state
    }

    const std::uint64_t hash = sim::stateHash(w);
    formats::RplyFile built = rec.build(kSeed, hash);
    const int slot = recordToUserDir(outDir, built);
    const fs::path dat = outDir / ("rply" + std::format("{:04d}.dat", slot));
    std::printf("replay: recorded %zu ticks -> %s seed=0x%X hash=0x%016llX\n", rec.ticks(), dat.string().c_str(),
                kSeed, static_cast<unsigned long long>(hash));

    const ReplayResult r = replayFile(tree, dat); // re-drive from disk, not memory
    std::printf("replay: ticks=%zu sealed=0x%016llX final=0x%016llX %s\n", r.ticksPlayed,
                static_cast<unsigned long long>(r.recordedHash), static_cast<unsigned long long>(r.replayedHash),
                r.matches ? "MATCH" : "FAIL");
    return r.matches && r.ticksPlayed == rec.ticks() ? 0 : 1;
}
} // namespace

int replayMain(int argc, char** argv) {
    fs::path tree, target, outDir = "/tmp/wc98port-out/replay";
    bool gateMode = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--tree" && i + 1 < argc) tree = argv[++i];
        else if (a == "--gate") { gateMode = true; if (i + 1 < argc && argv[i + 1][0] != '-') outDir = argv[++i]; }
        else target = a;
    }
    if (tree.empty())
        if (const char* t = std::getenv("WC98_TREE"); t && *t) tree = t;
    try {
        if (gateMode) return gate(tree, outDir);
        if (target.empty()) { std::puts("usage: wc98-replay [--tree DIR] <rplyNNNN.dat> | --gate [outdir]"); return 1; }
        const ReplayResult r = replayFile(tree, target);
        std::printf("replay: ticks=%zu sealed=0x%016llX final=0x%016llX %s\n", r.ticksPlayed,
                    static_cast<unsigned long long>(r.recordedHash), static_cast<unsigned long long>(r.replayedHash),
                    r.matches ? "MATCH" : "FAIL");
        return r.matches ? 0 : 1;
    } catch (const std::exception& e) {
        if (std::strncmp(e.what(), "skip:", 5) == 0) return 2; // MatchBoot skip pattern
        std::fprintf(stderr, "replay: %s\n", e.what());
        return 1;
    }
}

} // namespace wc98::app

int main(int argc, char** argv) { return wc98::app::replayMain(argc, argv); }
