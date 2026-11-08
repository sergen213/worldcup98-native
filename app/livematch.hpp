#pragma once
// app/livematch.hpp — real-time playable match (M10) + the shared exhibition bootstrap the M6
// demo driver also uses. The live loop drives the ported simulation on a wall-clock cadence and
// presents the original match art through presentation::MatchScene with camera follow + HUD strip,
// feeding the keyboard/gamepad pad word into controller 0 (mapping table in livematch.cpp).
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "wc98/formats/layout.hpp"
#include "wc98/presentation/data.hpp"
#include "wc98/presentation/fe_screen.hpp" // IgBoards members: ScreenArt, FeText, FeDrawStats, composite
#include "wc98/presentation/snapshot.hpp"
#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"

namespace wc98::platform {
class Window;
}
namespace wc98::render {
class Renderer;
class SpriteBatch;
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
    std::array<std::string, 2> names{};  // FCDB STR0 fullName rows (IG board typ1/typ2 captions)

    bool init(const std::filesystem::path& root);
};

// World -> MatchSnapshot. Field sources are the exe offsets named in the sim headers
// (player.hpp PlayerState, anim.hpp AnimInstance, ball.hpp BallState, rules.hpp MatchState).
presentation::MatchSnapshot snapshotFromWorld(const sim::World& w, const sim::MatchData& md);

// ---- IG presentation boards (analysis/pass13/IG_FAMILIES_SPEC.md §C) ----------------------------
// One match's data for the IGSTATS/IGHALF/IGFLSUM/IGSCSUM composites: TeamStats block
// World.rules.stats[2] (rules.hpp, DAT_0065E1FC..), score MatchState::score (DAT_0065E050), goalLog
// entries (DAT_0065E06C) with running scores, and possession percentages from the per-team
// accumulator over World.time.possessionFrames deltas (DAT_0065f264; DAT_0065f250 owning team).
struct IgTeamStats {
    int corners = 0, offsides = 0, fouls = 0, cards = 0, goals = 0, shotsWide = 0;
};
struct IgGoalRow {
    int minute = 0, second = 0;       // goalLog u16 time (seconds) split for the "Time" column
    int team = 0;                     // byte0 bit0
    int shirtNo = 0;                  // byte1 squad number ("Player" column "#NN")
    bool ownGoal = false, penalty = false; // byte0 bits 1/2
    std::array<int, 2> scoreAfter{};  // running "Score" column after this entry
};
struct IgMatchData {
    std::array<std::string, 2> names{};      // FCDB STR0 fullName (typ1/typ2)
    std::array<std::string, 2> flags{{"F000", "F026"}}; // shield art files FNNN = FCDB record index
    std::array<int, 2> score{};              // MatchState::score DAT_0065E050
    std::array<int, 2> possessionPct{{50, 50}}; // Possession row % (sums to 100)
    std::array<IgTeamStats, 2> stats{};      // World.rules.stats[2]
    std::vector<IgGoalRow> goals;            // goalLog DAT_0065E06C rows, chronological + running score
};

// The four IGLAY.BIG stat/summary boards rendered from IgMatchData. Composite = scene-ownership
// visible whitelist (null.fsh placeholder widgets are legit invisible; static textid soup slots —
// igstats ty12..ty16, summary typ3..typ5/typ8/typ9/up/down — excluded per f0f8109 rule) plus the
// runtime caption overlay: FC-bank titles (28 'Match Statistics', 858 'HALF-TIME', 776 'Full-Time',
// 1553 'SCORE SUMMARY'), names/scores/shields, stat grid rows and summary list columns.
class IgBoards {
public:
    IgBoards(render::Renderer& r, const std::filesystem::path& tree);

    // name = lowercase IGLAY.BIG member (igstats | ighalf | igflsum | igscsum). False = absent.
    bool open(std::string_view name);
    [[nodiscard]] bool isOpen() const { return kind_ != 0; }
    [[nodiscard]] int kind() const { return kind_; } // 1 igstats, 2 ighalf, 3 igflsum, 4 igscsum
    void movePage(int dir);                           // 'igbC' btn1..btn4 page switch (L/R axis) [C] FUN_0043a3f4
    presentation::FeDrawStats draw(render::SpriteBatch& ui, const IgMatchData& d);

private:
    std::filesystem::path tree_;
    presentation::ScreenArt art_;
    std::unique_ptr<presentation::FeText> text_;
    formats::ScreenLayout sc_;
    int kind_ = 0;
    int page_ = 0;
};

// The real-time match loop. window/renderer are caller-owned: the FE shell passes its own windowed
// renderer; --live-match-smoke runs on a throwaway window + swapchain renderer (runLiveMatch).
// smokeFrames > 0 = gate mode: fixed 1/50 s steps per frame, no wall sleep, asserts the scene drew,
// the HUD strip is present, the sim advanced and the input->sim pad-bit path fired. Returns
// 0 ok / 1 assert failure / 2 skip (tree) / 3 validation messages.
// recordReplay: stream every controller-0 tick through app::ReplayRecorder and, on exit, write
// user/rplyNNNN.dat + rplyindx.dat under the tree (docs/asset-formats/RPLY.md).
int runLiveLoop(const std::filesystem::path& tree, const std::string& prefix, int smokeFrames,
                platform::Window& window, render::Renderer& renderer, bool recordReplay = false,
                bool igPauseSmoke = false, int boardScript = 0); // 1 stats tour / 2 half board / 3 full-time boards (gates fe_igstats/fe_ighalf/fe_igscsum/fe_igflsum)

// CLI driver: --tree DIR [--live-match-smoke N] [--out-prefix P]. Standalone wrapper that owns its
// window + Vulkan swapchain; "skip:" exits with 2 like the other tree gates.
int runLiveMatch(int argc, char** argv);

} // namespace wc98::app
