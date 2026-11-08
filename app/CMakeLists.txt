add_executable(wc98-native main.cpp fe_flow.cpp match_demo.cpp livematch.cpp selftest.cpp replay.cpp commentary.cpp game_audio.cpp tournament_demo.cpp saveload.cpp video.cpp)

# .DCT movie player (docs/asset-formats/VIDEO.md): --gate decodes + uploads every frame of
# VIDEO/*.DCT through the Vulkan renderer, printing "VIDEO frames=N size=WxH fps=F".
add_executable(wc98-video-play video_main.cpp video.cpp)
target_link_libraries(wc98-video-play PRIVATE wc98_renderer_vk wc98_platform wc98_formats wc98::core)
target_link_libraries(wc98-native PRIVATE wc98_presentation wc98_renderer_vk wc98_simulation wc98_input wc98_config wc98_platform wc98_formats wc98_audio wc98::tournament wc98::core)
# Demo entry points are thin stubs; the drivers (runMatchDemo/runTournamentDemo) also link into
# the windowed shell so the FE flow can launch exhibition and tournament modes.
add_executable(wc98-match-demo match_demo_main.cpp match_demo.cpp livematch.cpp replay.cpp commentary.cpp game_audio.cpp)
target_link_libraries(wc98-match-demo PRIVATE wc98_presentation wc98_renderer_vk wc98_simulation wc98_input wc98_config wc98_platform wc98_formats wc98_audio wc98::core)

# Headless deterministic replay tool (docs/asset-formats/RPLY.md): records the scripted seed-pinned
# scenario (--gate), re-drives .dat files; links MatchBoot via livematch.cpp, opens no window.
add_executable(wc98-replay replay_main.cpp replay.cpp livematch.cpp commentary.cpp game_audio.cpp)
target_link_libraries(wc98-replay PRIVATE wc98_presentation wc98_renderer_vk wc98_simulation wc98_input wc98_config wc98_platform wc98_formats wc98_audio wc98::core)
add_executable(wc98-tournament-demo tournament_demo_main.cpp tournament_demo.cpp saveload.cpp)
target_link_libraries(wc98-tournament-demo PRIVATE wc98::tournament wc98_formats wc98_platform wc98::core)
