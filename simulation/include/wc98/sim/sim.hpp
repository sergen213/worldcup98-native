#pragma once
// Frame loop and match driver. Independent of the wall clock: the caller feeds 30 Hz game ticks
// (the exe derives them from a 100 Hz multimedia timer, MatchLoop_tickTimerCallback 0x41B704).
#include <cstdint>

#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"

namespace wc98::sim {

inline constexpr Fixed kTickDt = 0x3c0000 / 30; // 0x20000 = 2 frames of 1/60 s (0x41B7FC)
inline constexpr Fixed kSliceCap = 0x60000;     // Sim_dispatchTimeSlice cap, 6 frames (0x4550A9)

void Sim_frame(World& w, Fixed simDt, Fixed clockDt);    // 0x4550C0
void Sim_dispatchTimeSlice(World& w, Fixed dt, Fixed clockDt, Fixed cap); // 0x4DFED4
void Sim_advance(World& w, Fixed simDt, Fixed clockDt);  // 0x454ED8 (game-speed scaling)
void Sim_initSplitList(World& w);                        // 0x4DFE80 (from Match_setupOptions 0x454652)
// 0x4DFFB8: request a frame boundary `frames` sim frames from now. Returns 1 if accepted (or a
// split already exists at that time), 0 if the list is full or frames <= 0.
int Sim_scheduleSplit(World& w, std::int32_t frames);

// Render interpolation (MatchLoop_runPendingTicks return, stored in DAT_0057c620): frames since
// the last tick in 16.16.
constexpr Fixed renderInterpolation(std::int32_t msSinceLastTick) {
    return static_cast<Fixed>((static_cast<std::int64_t>(msSinceLastTick) * 0x3c0000) / 1000);
}

// Match_run 0x40A664 + MatchLoop_runPeriod 0x41B480 + MatchLoop_stepTick 0x41B8AC without the
// presentation layer. Owns the world; one tick() = one 30 Hz game tick.
class SimRunner {
public:
    SimRunner(const MatchData& data, const Options& opt, std::uint32_t seed);
    World& world() { return w_; }
    const World& world() const { return w_; }
    // Set the pad state a controller presents from the next tick on (Input_readPadBits source).
    void setController(int index, const ControllerState& c) { w_.controllers.at(index) = c; }
    // Advance one game tick. Returns false once the match is over (DAT_0057c604 == 2).
    bool tick();
    bool finished() const { return finished_; }
    int periodIndex() const { return period_; } // DAT_005aab98

private:
    World w_;
    int period_ = 0;
    bool finished_ = false;
};

} // namespace wc98::sim
