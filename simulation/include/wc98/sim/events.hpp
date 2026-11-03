#pragma once
// Match events reported by the simulation for logs/commentary/tools. Not part of the original
// state (the exe drives popups/commentary from the same code points); never hashed.
#include <cstdint>
#include <string_view>

namespace wc98::sim {

enum class EventType : std::uint8_t {
    KickOff, PeriodStart, PeriodEnd, MatchEnd,
    Pass, Cross, Shot, Header, Clearance, KeeperThrow, KeeperKick,
    Goal, OwnGoal, PostHit, BarHit, Save, Catch,
    Tackle, SlideTackle, Foul, YellowCard, RedCard, Injury,
    ThrowIn, Corner, GoalKick, FreeKick, Penalty, Offside,
    ShootoutKick, ShootoutGoal, ShootoutMiss,
    Possession, // possessor change (a = new player id, b = previous)
    Substitution, // player = pid whose squad changed, a = squad off, b = squad on
};

constexpr std::string_view eventName(EventType t) {
    constexpr std::string_view k[] = {
        "kickoff", "period_start", "period_end", "match_end",
        "pass", "cross", "shot", "header", "clearance", "keeper_throw", "keeper_kick",
        "goal", "own_goal", "post", "crossbar", "save", "catch",
        "tackle", "slide_tackle", "foul", "yellow_card", "red_card", "injury",
        "throw_in", "corner", "goal_kick", "free_kick", "penalty", "offside",
        "shootout_kick", "shootout_goal", "shootout_miss",
        "possession",
        "substitution",
    };
    return k[static_cast<int>(t)];
}

struct SimEvent {
    std::uint32_t frame = 0;  // total sim frames (DAT_0065f1d4) when raised
    EventType type{};
    std::int8_t team = -1;    // 0/1
    std::int8_t player = -1;  // player id 0..21 (team*11 + index)
    std::int32_t a = 0, b = 0; // type-specific (e.g. goal: game seconds; card: foul class)
};

} // namespace wc98::sim
