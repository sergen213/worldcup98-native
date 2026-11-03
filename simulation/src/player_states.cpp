// Player state dispatch (owner: SpStates): handler table 0x589068 and flag table 0x589088, both
// indexed [state >> 7][state & 0x7F] by Player_setState 0x4BDCD8 (flag bit 0 -> player+0x22C
// bit 9). Group sizes are the distances between the per-group arrays in the exe data.
#include "player_states.hpp"

namespace wc98::sim {
namespace {

// group 0x0: handlers exe 0x59f208, flags exe 0x59f21c
constexpr StateFn kG0[5] = {State000, State001, State002, State003, State004};
constexpr std::uint8_t kF0[5] = {3, 3, 3, 3, 3};
// group 0x1: handlers exe 0x59f224, flags exe 0x59f240
constexpr StateFn kG1[7] = {State080, State081, State082, State083, State084, State085, State086};
constexpr std::uint8_t kF1[7] = {0, 0, 0, 0, 0, 0, 0};
// group 0x2: handlers exe 0x59f258, flags exe 0x59f25c
constexpr StateFn kG2[1] = {State100};
constexpr std::uint8_t kF2[1] = {0};
// group 0x3: handlers exe 0x59f278, flags exe 0x59f2b8
constexpr StateFn kG3[16] = {State180, State181, State182, State183, State183, State185, State186, State187, State188, State185, State18A, State183, State18C, State18D, State18A, State18A};
constexpr std::uint8_t kF3[16] = {0, 0, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// group 0x4: handlers exe 0x59f374, flags exe 0x59f434
constexpr StateFn kG4[48] = {State200, State201, State202, State203, State204, State205, State206, State207, State208, State209, State209, State209, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C, State20C};
constexpr std::uint8_t kF4[48] = {3, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// group 0x5: handlers exe 0x59f47c, flags exe 0x59f484
constexpr StateFn kG5[2] = {State280, State281};
constexpr std::uint8_t kF5[2] = {3, 3};
// group 0x6: handlers exe 0x59f5c4, flags exe 0x59f600
constexpr StateFn kG6[15] = {State300, State301, State302, State303, State304, State305, State306, State307, State308, State309, State30A, State30B, State30C, State30D, State30E};
constexpr std::uint8_t kF6[15] = {2, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 3, 0, 0, 0};
// group 0x7: handlers exe 0x58b678, flags exe 0x58b688
constexpr StateFn kG7[4] = {State380, State381, State382, State383};
constexpr std::uint8_t kF7[4] = {0, 0, 0, 0};

struct Group {
    const StateFn* fn;
    const std::uint8_t* flags;
    int count;
};
constexpr Group kGroups[8] = {{kG0, kF0, 5},  {kG1, kF1, 7}, {kG2, kF2, 1},  {kG3, kF3, 16},
                              {kG4, kF4, 48}, {kG5, kF5, 2}, {kG6, kF6, 15}, {kG7, kF7, 4}};

} // namespace

StateFn Player_stateHandler(int state) {
    const int g = state >> 7, i = state & 0x7f;
    return (g >= 0 && g < 8 && i < kGroups[g].count) ? kGroups[g].fn[i] : nullptr;
}

std::uint8_t Player_stateFlags(int state) {
    const int g = state >> 7, i = state & 0x7f;
    return (g >= 0 && g < 8 && i < kGroups[g].count) ? kGroups[g].flags[i] : 0;
}

} // namespace wc98::sim
