#pragma once
// Native replay recording + deterministic playback re-drive over the WC98 .dat container
// (formats/rply.hpp, docs/asset-formats/RPLY.md). A recorded tick = one sim step's full
// ControllerState image of controller 0 (the exe records a delta state stream; our dialect stores
// the input word verbatim — lockstep input replay per SAVES.md "replay = inputs + seed").
#include <cstdint>
#include <filesystem>
#include <vector>

#include "wc98/formats/rply.hpp"
#include "wc98/sim/player.hpp"

namespace wc98::app {

class ReplayRecorder {
public:
    void append(std::uint32_t tick, const sim::ControllerState& c); // payload = [u32 LE tick][state image]
    [[nodiscard]] std::size_t ticks() const { return payloads_.size(); }

    // Container with dialect header: table[0]=seed, [1]=tick count, [2]=dialect version 1,
    // [3]/[4]=final hash lo/hi (sim::stateHash at seal time).
    [[nodiscard]] formats::RplyFile build(std::uint32_t seed, std::uint64_t finalHash) const;

private:
    std::vector<std::vector<std::uint8_t>> payloads_;
};

struct ReplayResult {
    std::uint64_t recordedHash = 0; // sealed in the header table
    std::uint64_t replayedHash = 0; // sim::stateHash after re-driving every tick
    std::size_t ticksPlayed = 0;
    bool matches = false;
};

// Re-drive: MatchBoot init on `tree`, seed from table[0], controller 0 bound to team 1 exactly as
// the live loop (Input_assignController(w,0,1,0)), feed each recorded ControllerState image and
// tick. Throws formats::FormatError / std::filesystem/other sim errors upward.
ReplayResult replayFile(const std::filesystem::path& tree, const std::filesystem::path& datPath);

// Write `file` into `dir` as rply%04d.dat (slot = 1 + max existing slot number in the dir) and
// update the 100x25-byte rplyindx.dat there. Returns the slot number used.
int recordToUserDir(const std::filesystem::path& dir, const formats::RplyFile& file);

} // namespace wc98::app
