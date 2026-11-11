#pragma once
// Motion cap / turn-class tables, byte-exact to FIFAWC.EXE DGROUP (pass-15 S2,
// analysis/pass15motion/BYTES.md). Provenance gate: tests/unit/test_motion_tables.cpp pins these
// rows against the file lane. The FIFA-99 cites 0x58AD60 / 0x58AD6C name zeros in WC98 — the rows
// below are the WC98 addresses (data VA = file offset + 0x401a00).
#include <cstdint>

#include "wc98/sim/fixed.hpp"

namespace wc98::sim {
// DAT_0058aee4, stride 4: speed cap per turn class (12.0 / 5.0 / 5.0 fixed-point).
inline constexpr Fixed kSpeedCap[3] = {0xc0000, 0x50000, 0x50000};
// DAT_0058aef0, stride 8: turn-class reclassification thresholds per current class (angle units).
inline constexpr std::int32_t kTurnClass[3][2] = {{228, 469}, {171, 455}, {114, 398}};
} // namespace wc98::sim
