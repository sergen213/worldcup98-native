// Hub wedge -> launch-entry mapping (app/fe_flow.hpp FeFlow::hubButtonEntry): the edges table for
#include <string> // label comparisons (std::to_string-free, but keeps std::string_view usable)
// the Feflow06.lay cursor domain. A wrong index here silently launches the wrong mode from the hub,
// so the whole mapping is pinned; gate lanes: fe_hub / fe_nav_* (tests/CMakeLists.txt).
#include "fe_flow.hpp"

#include <cstdio>
#include <string_view>

namespace {
int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}
    // Runtime checks below (formats::iequals is not constexpr).

bool run() {
    using wc98::app::FeFlow;
    // Launch wedges only: every other hub target is an in-place view change or a documented no-op.
    expect(FeFlow::hubButtonEntry("fe32") == 3, "fe32 -> Practise setup");   // feflow50 edge
    expect(FeFlow::hubButtonEntry("fe11") == 2, "fe11 -> Tournament grid");  // FEFLOW11 world-cup pool
    expect(FeFlow::hubButtonEntry("fe07") == 0, "fe07 -> Exhibition friendly setup");
    for (std::string_view w : {"quit", "fe15", "fe41", "cred", "fe33", "fe90", "fe04", "fe02", "fe89", "repl"})
        expect(FeFlow::hubButtonEntry(w) == -1, ("non-launch wedge " + std::string{w}).c_str());
    // Case-insensitive widget tags (LAY keys are compared case-insensitively by the engine).
    expect(FeFlow::hubButtonEntry("FE07") == 0, "case-insensitive FE07");
    expect(FeFlow::hubButtonEntry("") == -1, "empty name -> no launch");
    // Cheats rows (feflow95.lay objecttype-18 btn0..btn7; MENU_PARITY.md §8): the captions are exe data
    // installed by scene code through FUN_004788b4, NOT FC-bank indices - pinned verbatim so a typo does
    // not silently rename a cheat row. [C] FIFAWC.EXE VA 0x564014/0x564020/0x56402c/0x564038/0x56404c/
    // 0x564058/0x564064/0x564074 (DGROUP raw = VA - 0x401a00), title "Cheats" @ VA 0x56400c.
    static constexpr const char* kExpected[8] = {"Take a Dive", "Hot Potato", "Big Heads", "Skeleton Players",
                                                 "Crazy Ball", "Silly Moves", "Flaming Ball", "Alien Model"};
    expect(FeFlow::kCheatLabels.size() == 8, "eight cheat rows (btn0..btn7 / txt0..txt7)");
    for (int i = 0; i < 8; ++i)
        expect(std::string_view{FeFlow::kCheatLabels[std::size_t(i)]} == kExpected[i], "cheat row caption");
    expect(std::string_view{FeFlow::kCheatTitle} == "Cheats", "'titl' pane caption string");
    return failures == 0;
}
} // namespace

int main() { return run() ? 0 : 1; }
