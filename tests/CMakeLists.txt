# Test registration. Unit tests are self-contained (synthetic byte buffers, no game
# bytes in the repo). Data-driven checks inside them gate on WC98_TREE (path to an
# extracted World Cup 98 tree) and exit 77 -> CTest marks them Skipped, so CI stays
# green without the copyrighted tree.
function(wc98_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE wc98_formats)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}) # tests/check.hpp
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Werror>
        # Keep asserts live in Release so CI tests are real.
        $<$<CONFIG:Release>:-UNDEBUG>)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
    if(DEFINED WC98_TREE)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "WC98_TREE=${WC98_TREE};WC98_TEST_OUT=/tmp/wc98port-out/png")
    endif()
endfunction()

wc98_test(test_byte_reader unit/test_byte_reader.cpp)
wc98_test(test_big_refpack unit/test_big_refpack.cpp)
wc98_test(test_fsh unit/test_fsh.cpp)
wc98_test(test_ffn unit/test_ffn.cpp)
wc98_test(test_fcdb unit/test_fcdb.cpp)
wc98_test(test_layout unit/test_layout.cpp)
wc98_test(test_pcmd unit/test_pcmd.cpp)
wc98_test(test_anim unit/test_anim.cpp)
wc98_test(test_gen4tbls unit/test_gen4tbls.cpp)
wc98_test(test_audio unit/test_audio.cpp)


# RPLY container codec: Ghidra pass-8 byte constants (572-byte header, BE16 record framing,
# 142-dword table, fixed 100x25 rplyindx.dat) + writer round-trip.
wc98_test(test_rply unit/test_rply.cpp)
# .HIS/.TRI team-history pair: synthetic round trips + tree gate over the 8 language pairs
# (participant id invariance, TRI record/pool parses; docs/asset-formats/TEAMHIST.md).
wc98_test(test_histtri unit/test_histtri.cpp)

# Tournament module slice (docs/reverse-engineering/TOURNAMENT_MODULE_WC98.md §4-§5): standings +
# tiebreak chain, bracket propagation, schedule plan, ZWCUP/ZINTER.RES pair matrix (§2.5).
wc98_test(test_tournament unit/test_tournament.cpp)
target_link_libraries(test_tournament PRIVATE wc98::tournament)

# Save framing + SLOT%.2d.SAV codec (docs/reverse-engineering/SAVESLOT_WC98.md): CRC-16 vector,
# framing round-trip identity, summary header id gate, byte-exact slot round trips, scan loop.
wc98_test(test_slot unit/test_slot.cpp)

# FE team-select paging model (app/team_select.hpp): 40-team pool = 5 pages of 8, cursor wraps.
wc98_test(test_teamselect unit/test_teamselect.cpp)
target_include_directories(test_teamselect PRIVATE ${CMAKE_SOURCE_DIR}/app)

# App-level tournament save->load scenario (no tree needed): new game -> play each round ->
# persist SLOT%02d.SAV after the round -> restore; state identical at every boundary.
add_test(NAME tournament_saveload COMMAND wc98-native --tournament-saveload 7)
set_tests_properties(tournament_saveload PROPERTIES SKIP_RETURN_CODE 2 FIXTURES_SETUP slot)

# App-slice tournament progression gate (TOURNAMENT_MODULE_WC98.md §4-§5): full 64-fixture run
# through standings -> qualifiers -> knockout propagation to a champion; env-gated exit 77 skip.
add_test(NAME tournament_progression COMMAND wc98-tournament-demo)
set_tests_properties(tournament_progression PROPERTIES SKIP_RETURN_CODE 77)
if(DEFINED WC98_TREE)
    set_tests_properties(tournament_progression PROPERTIES ENVIRONMENT "WC98_TREE=${WC98_TREE}")
endif()

# Match simulation (ported from fifa99-native tests/sim.cmake). The F99_SIM_LINKS gate is dropped:
# the linking gap Anim_evalSkeleton 0x460ACC is implemented against the WC98 rig
# (docs/reverse-engineering/SIM_PORT_NOTES.md section 5), so all three suites build.
# test_sim_scenarios: M7 gameplay regression harness (docs/development/M7_REGRESSIONS.md) -
# pure simulation on synthetic rosters, no tree/env gate.
foreach(t IN ITEMS test_sim_core test_sim_players test_sim_team test_sim_scenarios)
    wc98_test(${t} unit/${t}.cpp)
    target_link_libraries(${t} PRIVATE wc98_simulation)
endforeach()

# Input layer: synthetic SDL events (keyboard pad words, virtual gamepad, hot-plug).
wc98_test(test_input unit/test_input.cpp)
target_link_libraries(test_input PRIVATE wc98_input)

# Gamepad platform layer: canonical layout mapping + nav repeat throttle + poll() glue
# through an SDL virtual pad under the dummy video driver (no hardware, no display).
wc98_test(test_gamepad unit/test_gamepad.cpp)
target_link_libraries(test_gamepad PRIVATE wc98_platform)

# App-level gamepad startup contract: --pad-smoke exits 0 with zero pads under the SDL
# dummy driver (headless-safe; skip 2 only if the pad subsystem itself is unavailable).
add_test(NAME gamepad_smoke_dummy COMMAND wc98-native --pad-smoke)
set_tests_properties(gamepad_smoke_dummy PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" SKIP_RETURN_CODE 2)


# Platform/renderer stack: these link the engine modules instead of only formats.
function(wc98_vulkan_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE wc98_renderer_vk wc98_platform wc98_formats wc98::core)
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/vulkan)
  target_compile_options(${name} PRIVATE -Wall -Wextra -Werror $<$<CONFIG:Release>:-UNDEBUG>)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "WC98_TEST_OUT=/tmp/wc98port-out/png" SKIP_REGULAR_EXPRESSION "skipping")
endfunction()
wc98_vulkan_test(test_platform_paths vulkan/test_platform_paths.cpp)
wc98_vulkan_test(test_renderer_layout vulkan/test_renderer_layout.cpp)
wc98_vulkan_test(test_vulkan_offscreen vulkan/test_vulkan_offscreen.cpp)

# M4 slice 1: a real FE bitmap (FE/FEFLOW02/FEFLOW02.FSH, 640x480) decoded from the user's
# tree and presented through the Vulkan renderer; fails if textures/validation break.
if(DEFINED WC98_TREE)
    add_test(NAME render_demo_fsh COMMAND wc98-render-demo /tmp/wc98port-out/fsh_render.png ${WC98_TREE})
    set_tests_properties(render_demo_fsh PROPERTIES SKIP_RETURN_CODE 2)
    # M4 slice 1b: member-select decode of original kit-atlas and FE sponsor art through --fsh.
    add_test(NAME render_demo_fsh_kit COMMAND wc98-render-demo /tmp/wc98port-out/fsh_kit.png --fsh ${WC98_TREE}/INGAME/PLAYER/TEXTURES/PK026A.FSH --tree ${WC98_TREE})
    set_tests_properties(render_demo_fsh_kit PROPERTIES SKIP_RETURN_CODE 2)
    add_test(NAME render_demo_fsh_fe COMMAND wc98-render-demo /tmp/wc98port-out/fsh_fe.png --fsh ${WC98_TREE}/FE/FEFLOW02/DANI02.FSH --tree ${WC98_TREE})
    set_tests_properties(render_demo_fsh_fe PROPERTIES SKIP_RETURN_CODE 2)
    # M4 slice 2: FE main-menu layout composited from original .LAY + FSH art (>=8 shapes).
    add_test(NAME render_demo_lay COMMAND wc98-render-demo /tmp/wc98port-out/lay_gate.png --lay ${WC98_TREE}/FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW05.LAY --tree ${WC98_TREE} --min-shapes 8)
    set_tests_properties(render_demo_lay PROPERTIES SKIP_RETURN_CODE 2)
    # M4 slice 3: original trophy model (GEOM draw-lists) rendered textured through Vulkan.
    add_test(NAME model_view_wcupf COMMAND wc98-model-view /tmp/wc98port-out/mdl_gate.png ${WC98_TREE}/FE/WCUPF.MDL)
    set_tests_properties(model_view_wcupf PROPERTIES SKIP_RETURN_CODE 2)
    # M6 exhibition-match slice: simulate + render kickoff/midplay/sprint frames of a real match.
    add_test(NAME match_demo_frames COMMAND wc98-match-demo /tmp/wc98port-out/m6 --tree ${WC98_TREE})
    set_tests_properties(match_demo_frames PROPERTIES SKIP_RETURN_CODE 2)
    # Native replay (docs/asset-formats/RPLY.md): record the seed-pinned scripted scenario to
    # user-slot files, then re-drive from disk and assert final world state-hash equality.
    add_test(NAME replay_record_play COMMAND wc98-replay --gate /tmp/wc98port-out/replay --tree ${WC98_TREE})
    set_tests_properties(replay_record_play PROPERTIES SKIP_RETURN_CODE 2)
    # M4/M9 FE flow: windowed shell renders FEFLOW05 real art (presentation compositor),
    # captures per-frame PNGs, Enter reaches exhibition + tournament entry points.
    # Skip code 2 = no display / headless CI, like match_demo_frames.
    add_test(NAME fe_flow_render COMMAND wc98-native --fe-smoke 3 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe)
    set_tests_properties(fe_flow_render PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}")
    add_test(NAME fe_nav_tournament COMMAND wc98-native --fe-smoke 2 --fe-nav tournament --fe-pick 35 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe)
    set_tests_properties(fe_nav_tournament PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}")
    add_test(NAME fe_nav_exhibition COMMAND wc98-native --fe-smoke 1 --fe-nav exhibition --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe)
    set_tests_properties(fe_nav_exhibition PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}")
    # M10 interactive playable match (app/livematch.cpp): real-time window loop - sim fixed 1/50 s
    # cadence, scene + HUD drawn each frame with camera follow, commentary pump; synthesized keyboard
    # input drives the ported pad path (FUN_0047836c bit writes) proving the input->sim chain.
    add_test(NAME interactive_match COMMAND wc98-native --live-match-smoke 150 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/live)
    set_tests_properties(interactive_match PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    # Authentic team-select fidelity (FEFLOW11.LAY pick grid, LAY_COMPOSITOR_NOTES.md): render the
    # screen via --fe-team; the shell asserts shapes/texts/cursors > 0 and this gate fails on zero counts.
    add_test(NAME fe_render_tournament COMMAND wc98-native --fe-smoke 2 --fe-team --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/feteam)
    set_tests_properties(fe_render_tournament PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    # Font fidelity (exe font table DAT_0057d2c0 -> FEFONTS/IGFONTS.BIG, fe_screen.hpp): the tree
    # carries exactly 10 faces; every FC*.BIN code must load its string bank (registry Language).
    add_test(NAME fe_text_fonts COMMAND wc98-native --fe-smoke 2 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fefonts)
    set_tests_properties(fe_text_fonts PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "faces=10 lang=" FAIL_REGULAR_EXPRESSION "load-error")
    add_test(NAME fe_text_lang_ger COMMAND wc98-native --fe-smoke 2 --lang ger --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/feger)
    set_tests_properties(fe_text_lang_ger PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "faces=10 lang=ger")
    # the Load entry restores it and verifies byte-identical state (app/saveload.cpp).
    add_test(NAME fe_nav_tournament_save COMMAND wc98-native --fe-smoke 2 --fe-nav tournament --fe-pick 35 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe)
    set_tests_properties(fe_nav_tournament_save PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE};WC98_SAVE_SLOT=7" FIXTURES_REQUIRED slot)
    add_test(NAME fe_nav_tournament_load COMMAND wc98-native --fe-smoke 2 --fe-nav tournament-load --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe)
    set_tests_properties(fe_nav_tournament_load PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE};WC98_SAVE_SLOT=7" FIXTURES_REQUIRED slot)
endif()

# M6 presentation core: snapshot interpolation, camera names, kit-file selection, face composite.
wc98_test(test_presentation unit/test_presentation.cpp)
target_link_libraries(test_presentation PRIVATE wc98_presentation)

# HUD overlay layout gate (pure logic, no GPU): scoreboard/clock/radar quad lists from synthetic font+snapshot.
wc98_test(test_hud unit/test_hud.cpp)
target_link_libraries(test_hud PRIVATE wc98_presentation)

# M8 launcher stack: settings round trip, OPTIONS.SAV codec sync, tree detect + manifest validate.
if(TARGET wc98-launcher)
    add_test(NAME launcher_selftest COMMAND wc98-launcher --self-test)
    set_tests_properties(launcher_selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# M6 commentary substrate: PHRS/TYPE phrase-bank record tables (synthetic BIGF + stripped-mirror
# leniency, tree gates on the 1,290-phrase mirror equality) and the headless SpeechQueue engine-ABI
# model. docs/asset-formats/SPEECH_PHRASES.md.
wc98_test(test_phrs unit/test_phrs.cpp)
wc98_test(test_speechqueue unit/test_speechqueue.cpp)
target_link_libraries(test_speechqueue PRIVATE wc98_audio)

# M6 commentary adapter: headless trigger/queue selection gates + PHRS tree decode gate
# (docs/reverse-engineering/COMMENTARY_WC98.md; app/commentary.cpp compiled in directly).
wc98_test(test_commentary unit/test_commentary.cpp)
target_sources(test_commentary PRIVATE ${CMAKE_SOURCE_DIR}/app/commentary.cpp)
target_link_libraries(test_commentary PRIVATE wc98_audio)
target_include_directories(test_commentary PRIVATE ${CMAKE_SOURCE_DIR}/simulation/include)

# Runtime audio slice (docs/reverse-engineering/AUDIO_RUNTIME.md): OpenAL loopback capture gates —
# FE music SCHl stream audible over >=1 s, GAME00 whistle/kick + CROWD.BNK slot decodes non-silent,
# scripted KickOff/PeriodEnd/Shot events fire SFX voices on the loopback mixer. app/game_audio.cpp
# compiled in directly (test_commentary pattern).
wc98_test(test_gameaudio unit/test_gameaudio.cpp)
target_sources(test_gameaudio PRIVATE ${CMAKE_SOURCE_DIR}/app/game_audio.cpp)
target_link_libraries(test_gameaudio PRIVATE wc98_audio)
target_include_directories(test_gameaudio PRIVATE ${CMAKE_SOURCE_DIR}/simulation/include)
