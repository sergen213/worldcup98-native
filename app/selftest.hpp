#pragma once
// app/selftest.hpp — automated gameplay self-test (--self-test FILE.json): launches a real
// playable match through the exhibition bootstrap (MatchBoot + SimRunner), drives controller 0
// with scripted pad words, and measures core operations from the simulation's internal state
// (World: players/ball/match/clock), never from presentation. Writes the structured JSON report
// {version, steps:[{step,passed,detail}]} to FILE and exits 0 when every step passed, 1 on any
// failure, 2 when the game tree is unavailable (gate skip pattern).

#include <filesystem>

namespace wc98::app {

int runSelfTest(const std::filesystem::path& tree, const std::filesystem::path& outJson);

} // namespace wc98::app
