add_executable(wc98-native main.cpp fe_flow.cpp match_demo.cpp livematch.cpp commentary.cpp tournament_demo.cpp saveload.cpp)
target_link_libraries(wc98-native PRIVATE wc98_presentation wc98_renderer_vk wc98_simulation wc98_input wc98_config wc98_platform wc98_formats wc98_audio wc98::tournament wc98::core)
# Demo entry points are thin stubs; the drivers (runMatchDemo/runTournamentDemo) also link into
# the windowed shell so the FE flow can launch exhibition and tournament modes.
add_executable(wc98-match-demo match_demo_main.cpp match_demo.cpp livematch.cpp commentary.cpp)
target_link_libraries(wc98-match-demo PRIVATE wc98_presentation wc98_renderer_vk wc98_simulation wc98_input wc98_config wc98_platform wc98_formats wc98_audio wc98::core)
add_executable(wc98-tournament-demo tournament_demo_main.cpp tournament_demo.cpp saveload.cpp)
target_link_libraries(wc98-tournament-demo PRIVATE wc98::tournament wc98_formats wc98_platform wc98::core)
