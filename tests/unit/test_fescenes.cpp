// Hub wedge -> launch-entry mapping (app/fe_flow.hpp FeFlow::hubButtonEntry): the edges table for
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
    return failures == 0;
}
} // namespace

int main() { return run() ? 0 : 1; }
