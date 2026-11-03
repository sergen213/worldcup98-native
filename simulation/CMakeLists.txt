# Match simulation reconstructed from FIFAWC.EXE (World Cup 98) via the ported FIFA 99 engine family
# (same EA Gen3 architecture, GHIDRA_PASS5_IG.md); no rendering/platform dependencies.
# Added to the root with add_subdirectory(simulation). Port deltas: docs/reverse-engineering/SIM_PORT_NOTES.md.
file(GLOB WC98_SIM_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(wc98_simulation STATIC ${WC98_SIM_SOURCES})
target_include_directories(wc98_simulation PUBLIC include)
target_link_libraries(wc98_simulation PUBLIC wc98_formats wc98::core)
add_library(wc98::simulation ALIAS wc98_simulation)
