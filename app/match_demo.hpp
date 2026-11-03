#pragma once
// wc98-match-demo: headless exhibition-match driver (M6 vertical slice).
//   wc98-match-demo <out-prefix> [--tree DIR]        (DIR defaults to $WC98_TREE)
// Simulates a real match with the ported simulation, renders three frames through MatchScene +
// Vulkan offscreen and writes <prefix>_kickoff.png / _midplay.png / _sprint.png.
// Exit codes: 0 ok, 1 failure, 2 missing tree/assets (ctest skip), 3 validation messages seen.
int runMatchDemo(int argc, char** argv);
