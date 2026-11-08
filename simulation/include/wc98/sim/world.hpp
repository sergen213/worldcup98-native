#pragma once
// The complete simulation state: one aggregate mirroring the exe's match globals. Subsystem state
// lives in its own header (ball.hpp, rules.hpp, player.hpp, team.hpp); functions are free
// functions taking World&, named after the exe functions they port (address in the comment).
//
// Rules for every state struct (enforced by static_assert in world.cpp): trivially copyable,
// no pointers (players/teams are referenced by index: player id = team*11 + index, -1 = none),
// no padding (std::has_unique_object_representations_v) so the state hash is well defined.
#include <array>
#include <cstdint>
#include <vector>

#include "wc98/sim/anim.hpp"
#include "wc98/sim/ball.hpp"
#include "wc98/sim/events.hpp"
#include "wc98/sim/fixed.hpp"
#include "wc98/sim/pitch.hpp"
#include "wc98/sim/player.hpp"
#include "wc98/sim/rng.hpp"
#include "wc98/sim/rules.hpp"
#include "wc98/sim/team.hpp"

namespace wc98::sim {

struct MatchData; // loaded, read-only inputs (team sheets, formation tables, ...) — data.hpp

inline constexpr int kPlayersPerTeam = 11;
inline constexpr int kPlayers = 22;
// The referee: movement object M 0x66764C with its anim controller 0x6675F8, laid out exactly like
// player+0xB8 / player+0x64, so it lives in players[kReferee] (only the anim + M fields are used).
// Reset by 0x46F39C/0x46F2EC, moved each frame by Player_frameMove from Match_updateRules 0x46F8B8,
// targeted by Referee_onPhase 0x470C80 and the restart sequences.
inline constexpr int kReferee = 22;

// Sim_frame 0x4550C0 / Sim_dispatchTimeSlice 0x4DFED4 bookkeeping.
struct FrameTime {
    std::int32_t simAcc = 0;        // DAT_0065f1e4 fractional sim frames (16.16)
    std::int32_t clockAcc = 0;      // DAT_0065f1f4 fractional clock frames
    std::int32_t simFrames = 0;     // DAT_0065f1e0 whole sim frames this step
    std::int32_t clockFrames = 0;   // DAT_0065f1f0 whole clock frames this step
    std::int32_t totalFrames = 0;   // DAT_0065f1d4
    std::int32_t stampFrames = 0;   // DAT_0065f1d8 (Sim_accumulateTotalFrames 0x454E94)
    std::int32_t stampAcc = 0;      // DAT_0065f1dc
    std::int32_t thirtyCounter = 0; // DAT_0065f204 (Team_autoMentality every > 30 frames)
    std::int32_t blink = 0;         // DAT_0065f240 toggled with it
    std::int32_t fatigueCounter = 0;  // DAT_0065f20c
    std::int32_t fatigueInterval = 0; // DAT_0065f208 = 5 game minutes in sim frames
    std::int32_t teamOrder = 0;     // DAT_0065f23c (alternates the Team_updateTactics order)
    std::int32_t possessorSlowFrames = 0; // DAT_0065f268 possessor slower than 1.6 u/frame
    std::int32_t possessionFrames = 0;    // DAT_0065f264
    std::int32_t possessingTeam = 0;      // DAT_0065f250
    std::int32_t timer673210 = 0;   // DAT_00673210 countdown (consumer untraced)
    std::int32_t timer65f1f8 = 0;   // DAT_0065f1f8 countdown (consumer untraced)
    std::int32_t sliceCarry = 0;    // DAT_00677570 sim time carried past a split point
    // Split list 0x677474 (20 x {time, prev, next}, delta-sorted): Sim_scheduleSplit 0x4DFFB8 makes
    // a Sim_frame boundary fall exactly N frames ahead (player states use it for contact frames).
    std::int32_t splitTime[20] = {}, splitPrev[20] = {}, splitNext[20] = {};
    std::int32_t splitFree = 0;     // DAT_00677568 free-list head
    std::int32_t splitHead = -1;    // DAT_0067756C active head
    std::int32_t splitCount = 0;    // DAT_00677564
    // DAT_0065F2B4 focus vector (head aim of every player in Player_frameMove 0x4BC124; also the
    // camera): -1 = ball posI 0x669148, 0..21 = players[i].posI (+0xD4), -2 = restart spot 0x66919C,
    // -3 = penalty spot (0, 0, +penSpotZ) 0x65F3A4 (Match_init 0x45485C in shoot-out mode 3).
    std::int32_t focus = -1;
};

// Territory statistics FUN_004560D8 (percent of the period spent per third).
struct Territory {
    std::int32_t frames[3] = {}; // DAT_0065f430 middle, 0x65F434 z > length/6, 0x65F438 z < -length/6
    std::int32_t percent[3] = {}; // 0x65F43C / 0x65F43E / 0x65F440 (u16 in the exe)
};

struct World {
    const MatchData* data = nullptr; // not hashed
    Options opt;
    Pitch pitch;
    Rng rng;
    FrameTime time;
    Territory territory;
    BallState ball;
    MatchState match;
    std::array<TeamState, 2> teams;
    FormationState formation;                      // formation module globals (TeamAI agent, team.hpp)
    std::array<PlayerState, kPlayers + 1> players{}; // [kReferee] = referee
    std::array<ControllerState, kMaxControllers> controllers{};
    std::array<PlayerAttr, 2 * kSquadMax> attrs{}; // 0x65E230 + t*0x60C (Players agent)
    PlayerShared pl{};                              // player-subsystem globals (Players agent)
    AnimShared anim{};                              // anim engine 0x65FFAC pool + clock (SpAnim, anim.hpp)
    std::array<std::int32_t, kPlayers + 1> kickCode{}; // port-internal (not hashed): the kick code of
                                                       //   each pending plan, kept for event mapping
    std::vector<SimEvent> events; // not hashed

    void emit(EventType t, int team = -1, int player = -1, std::int32_t a = 0, std::int32_t b = 0) {
        events.push_back({static_cast<std::uint32_t>(time.totalFrames), t, static_cast<std::int8_t>(team),
                          static_cast<std::int8_t>(player), a, b});
    }
};

// FNV-1a over every hashed state struct (everything except data/events).
std::uint64_t stateHash(const World& w);

} // namespace wc98::sim
