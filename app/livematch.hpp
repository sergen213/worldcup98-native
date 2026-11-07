#pragma once
// app/livematch.hpp — real-time playable match (M10) + the shared exhibition bootstrap the M6
// demo driver also uses. The live loop drives the ported simulation on a wall-clock cadence and
// presents the original match art through presentation::MatchScene with camera follow + HUD strip,
// feeding the keyboard/gamepad pad word into controller 0 (mapping table in livematch.cpp).
#include <array>
#include <filesystem>
#include <memory>
#include <string>

#include "wc98/presentation/data.hpp"
#include "wc98/presentation/snapshot.hpp"
#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"

namespace wc98::platform {
class Window;
}
namespace wc98::render {
class Renderer;
}

namespace wc98::app {

// Exhibition-match bootstrap shared by runMatchDemo and the live loop: loads the FE PAL palette,
// the two FCDB.DBI teams (+ sheets, formation tables, ANIM.MO clips) and the STR0 HUD abbreviations.
// Prints "skip: ..." lines and returns false when the tree is unusable (ctest SKIP_RETURN_CODE 2).
struct MatchBoot {
    std::unique_ptr<presentation::GameData> data; // valid after init()
    sim::MatchData md;
    sim::Options opt;                    // halfLengthMinutes = 4 default (DAT_0065EEE0), pitchSize 2
    std::array<int, 2> teamIndex{0, 26}; // FCDB.DBI indices with shipped PK###H + PK###A atlases (measured)
    std::array<std::string, 2> abbrv{};  // FCDB STR0 rows pair 1:1 with the FCDB.DBI team order

    bool init(const std::filesystem::path& root);
};

// World -> MatchSnapshot. Field sources are the exe offsets named in the sim headers
// (player.hpp PlayerState, anim.hpp AnimInstance, ball.hpp BallState, rules.hpp MatchState).
presentation::MatchSnapshot snapshotFromWorld(const sim::World& w, const sim::MatchData& md);

// The real-time match loop. window/renderer are caller-owned: the FE shell passes its own windowed
// renderer; --live-match-smoke runs on a throwaway window + swapchain renderer (runLiveMatch).
// smokeFrames > 0 = gate mode: fixed 1/50 s steps per frame, no wall sleep, asserts the scene drew,
// the HUD strip is present, the sim advanced and the input->sim pad-bit path fired. Returns
// 0 ok / 1 assert failure / 2 skip (tree) / 3 validation messages.
// recordReplay: stream every controller-0 tick through app::ReplayRecorder and, on exit, write
// user/rplyNNNN.dat + rplyindx.dat under the tree (docs/asset-formats/RPLY.md).
int runLiveLoop(const std::filesystem::path& tree, const std::string& prefix, int smokeFrames,
                platform::Window& window, render::Renderer& renderer, bool recordReplay = false,
                bool igPauseSmoke = false); // --ig-pause-smoke N: scripted Esc/Enter pause tour (gate fe_igmenu)

// CLI driver: --tree DIR [--live-match-smoke N] [--out-prefix P]. Standalone wrapper that owns its
// window + Vulkan swapchain; "skip:" exits with 2 like the other tree gates.
int runLiveMatch(int argc, char** argv);

} // namespace wc98::app
