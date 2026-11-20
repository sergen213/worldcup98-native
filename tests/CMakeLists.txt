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
# (team id invariance, 120 multitext cells, TRI record/pool parses; docs/asset-formats/TEAMHIST.md).
wc98_test(test_histtri unit/test_histtri.cpp)
# "[multitext]" section-blob parser: synthetic grammar gates + 8x120-cell tree sweep with
# Ireland DU/EN pins (docs/asset-formats/TEAMHIST.md).
wc98_test(test_multitext unit/test_multitext.cpp)
# EA .DCT movie container (SCHl + mTCD, SRE v2 video plane): header framing walk across all
# 30 tree videos, deterministic decode checksums; docs/asset-formats/VIDEO.md.
wc98_test(test_dct unit/test_dct.cpp)

# EA front-end .MDL polygon models (FE/*.MDL, 12 files): synthetic GEOM parse + tree census with
# group/face counts and SKEL magic pin on LPOLYBF; docs/reverse-engineering/MDL_GEOMETRY_NOTES.md.
wc98_test(test_mdl unit/test_mdl.cpp)

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
# Hub wedge -> launch-entry mapping table (FeFlow::hubButtonEntry): pins the Feflow06 cursor-domain
# edges so a table edit that swaps modes is caught without a tree.
wc98_test(test_fescenes unit/test_fescenes.cpp)
target_include_directories(test_fescenes PRIVATE ${CMAKE_SOURCE_DIR}/app ${CMAKE_SOURCE_DIR}/simulation/include)
target_link_libraries(test_fescenes PRIVATE wc98_formats wc98_renderer wc98_presentation wc98_simulation)

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

# Extraction-layer integrity gate (pass-44forensic): the dbexplorer JSON must be
# parseable JSON (brace-balance regressions in playerJson/teams broke it once) and
# carry the WC98 raw/offset fields. python3 ships on CI runners.
if(DEFINED WC98_TREE)
    add_test(NAME db_json_valid
        COMMAND sh -c "$<TARGET_FILE:wc98-dbexplorer> \"$WC98_TREE/COMMON\" --json /tmp/wc98_db_gate.json && python3 -m json.tool /tmp/wc98_db_gate.json > /dev/null && grep -c recordOffset /tmp/wc98_db_gate.json > /dev/null && grep -c position2 /tmp/wc98_db_gate.json | grep -q '^0$'")
    set_tests_properties(db_json_valid PROPERTIES ENVIRONMENT "WC98_TREE=${WC98_TREE}")
    # Phase-10 byte round trip through the CLI lane: decode -> encode must rebuild
    # FCDB.DBI byte-for-byte and every documented bit field must be symmetric.
    add_test(NAME db_roundtrip_valid
        COMMAND sh -c "$<TARGET_FILE:wc98-dbexplorer> \"$WC98_TREE/COMMON\" --roundtrip")
    set_tests_properties(db_roundtrip_valid PROPERTIES ENVIRONMENT "WC98_TREE=${WC98_TREE}")
    # Game-data lane (goal phases 3/8/11): the runtime GameData (same loader the FE and the
    # match boot use) must resolve every roster slot and ship parseable JSON.
    add_test(NAME game_data_valid
        COMMAND sh -c "$<TARGET_FILE:wc98-gamecheck> \"$WC98_TREE\" --json /tmp/wc98_game_gate.json && python3 -m json.tool /tmp/wc98_game_gate.json > /dev/null && grep -q '\"unresolved\": 0' /tmp/wc98_game_gate.json")
    set_tests_properties(game_data_valid PROPERTIES ENVIRONMENT "WC98_TREE=${WC98_TREE}")
    # Identity gate: the runtime view and the extraction export are the same records -
    # team names/abbrevs/roster ids must agree index-by-index (no second database).
    add_test(NAME game_data_identity
        COMMAND sh -c "$<TARGET_FILE:wc98-dbexplorer> \"$WC98_TREE/COMMON\" --json /tmp/wc98_db_gate.json && $<TARGET_FILE:wc98-gamecheck> \"$WC98_TREE\" --json /tmp/wc98_game_gate.json && python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/identity_check.py /tmp/wc98_db_gate.json /tmp/wc98_game_gate.json")
    set_tests_properties(game_data_identity PROPERTIES ENVIRONMENT "WC98_TREE=${WC98_TREE}")
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
# D-27 set-piece grammar gate (pass-17setpiece): band initializer rows, box-band writer
# arithmetic, selector arbiter segments, per-case exact rounded placement triples. No tree gate.
wc98_test(test_setpiece_grammar unit/test_setpiece_grammar.cpp)
target_link_libraries(test_setpiece_grammar PRIVATE wc98_simulation)
# Trig-table provenance gate (pass-15): the sim's sine quarter-wave + atan octant tables pinned
# byte-exact to FIFAWC.EXE DGROUP rows (DAT_00585898 / DAT_00594CAC), FIFA-99 cite addresses asserted
# absent, quadrant-boundary exactness of sinCos/atan2i. Tree-gated (exit 77 skip).
wc98_test(test_trig_tables unit/test_trig_tables.cpp)
target_link_libraries(test_trig_tables PRIVATE wc98_simulation)
# Ball-table provenance gate (pass-15): runtime tables byte-exact at measured WC98 VAs, flight-set
# rows present, 13 closed-form kFlightConst rows asserted absent from the image (solver-constant gap).
wc98_test(test_ball_tables unit/test_ball_tables.cpp)
target_include_directories(test_ball_tables PRIVATE ${CMAKE_SOURCE_DIR}/simulation/src)
# Motion cap/turn-table provenance gate (pass-15 S2): speed caps + turn-class rows byte-exact at
# DAT_0058aee4 / DAT_0058aef0, FIFA-99 cite addresses (0x58AD60/0x58AD6C) asserted zeros; the cls-0
# row 228/469 the port never used is pinned (register D-55). Tree-gated.
wc98_test(test_motion_tables unit/test_motion_tables.cpp)
target_include_directories(test_motion_tables PRIVATE ${CMAKE_SOURCE_DIR}/simulation/src)
target_link_libraries(test_motion_tables PRIVATE wc98_simulation)

# Input layer: synthetic SDL events (keyboard pad words, virtual gamepad, hot-plug).
wc98_test(test_input unit/test_input.cpp)
target_link_libraries(test_input PRIVATE wc98_input)

# Input-table provenance gate (pass-15/16, BYTES5.md): button masks @0x58c7b4, context slot
# pointers @0x58d314 decoded to action-table ids (dev5 ctx10 = 0x58cf14 -> id 8, register D-71),
# full action-table/keep-mask lanes + the single 96-byte queue gate @0x58c750 (D-66). Tree-gated.
wc98_test(test_input_tables unit/test_input_tables.cpp)
target_include_directories(test_input_tables PRIVATE ${CMAKE_SOURCE_DIR}/simulation/src)

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
    # pass-108 venue fly-in lane: real venues through the GPU mesh pipeline (mdl + pak + companion
    # fsh); bitmap/variance floors catch uv, texture-binding and framing regressions, not parses.
    add_test(NAME render_demo_venue_lensf COMMAND wc98-render-demo /tmp/wc98port-out/venue_lensf.png --venue 3 --tree ${WC98_TREE} --min-tris 400 --venue-min-bitmaps 8 --venue-min-variance 0.01)
    set_tests_properties(render_demo_venue_lensf PROPERTIES SKIP_RETURN_CODE 2)
    add_test(NAME render_demo_venue_brdxf COMMAND wc98-render-demo /tmp/wc98port-out/venue_brdxf.png --venue 8 --tree ${WC98_TREE} --min-tris 200 --venue-min-bitmaps 4 --venue-min-variance 0.005)
    set_tests_properties(render_demo_venue_brdxf PROPERTIES SKIP_RETURN_CODE 2)
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
    # .DCT movie playback (docs/asset-formats/VIDEO.md): headless decode + Vulkan texture upload
    # of every INT_FIFA.DCT frame; frames=0 output fails the gate.
    add_test(NAME video_play COMMAND wc98-video-play --gate ${WC98_TREE}/VIDEO/INT_FIFA.DCT)
    set_tests_properties(video_play PROPERTIES SKIP_RETURN_CODE 2 PASS_REGULAR_EXPRESSION "VIDEO frames=[1-9][0-9]* .*movie-audio-rendered=1494277 movie-audio-played=1494277 video-ms=[0-9]* wall-ms=[0-9]* peak=[1-9][0-9]*")
    # M4/M9 FE flow: windowed shell renders FEFLOW05 real art (presentation compositor),
    # captures per-frame PNGs, Enter reaches exhibition + tournament entry points.
    # Skip code 2 = no display / headless CI, like match_demo_frames.
    add_test(NAME fe_flow_render COMMAND wc98-native --fe-smoke 3 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe)
    set_tests_properties(fe_flow_render PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}"
        FAIL_REGULAR_EXPRESSION "FEFLOW05: shapes=0 |FEFLOW05: texts=0 ")
    # Friendly/practise match-setup screens (FE_MENU_SCREENS_WC98.md §5): FEFLOW09/FEFLOW50 real
    # art + flag shields + team names; the practise entry then reaches its match driver.
    add_test(NAME fe_flow_setup09 COMMAND wc98-native --fe-smoke 3 --fe-setup 0 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/setup09)
    set_tests_properties(fe_flow_setup09 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}"
        FAIL_REGULAR_EXPRESSION "FEFLOW09: shapes=0 |FEFLOW09: texts=0 ")
    add_test(NAME fe_flow_setup50 COMMAND wc98-native --fe-smoke 3 --fe-setup 3 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/setup50)
    set_tests_properties(fe_flow_setup50 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}"
        FAIL_REGULAR_EXPRESSION "FEFLOW50: shapes=0 |FEFLOW50: texts=0 ")
    # pass-108 app lane: venue fly-in backdrop behind the feflow35 tournament board (--fe-venue
    # opens the board + loads venue art; the printed stats line proves the load, tris>=1 the meshes).
    add_test(NAME fe_venue_wc35 COMMAND wc98-native --fe-wc-cfg 35 --fe-venue 3 --fe-smoke 2 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fevenue)
    set_tests_properties(fe_venue_wc35 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}"
        PASS_REGULAR_EXPRESSION "fe: venue slot 3: parts=[1-9][0-9]* tris=[1-9][0-9]* bitmaps=[1-9]")
    add_test(NAME fe_nav_practise COMMAND wc98-native --fe-smoke 1 --fe-nav practise --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe)
    set_tests_properties(fe_nav_practise PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}")
    add_test(NAME fe_nav_tournament COMMAND wc98-native --fe-smoke 2 --fe-nav tournament --fe-pick 35 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe)
    set_tests_properties(fe_nav_tournament PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}")
    add_test(NAME fe_nav_exhibition COMMAND wc98-native --fe-smoke 1 --fe-nav exhibition --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe)
    set_tests_properties(fe_nav_exhibition PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}")
    add_test(NAME fe_nav_league COMMAND wc98-native --fe-smoke 1 --fe-nav league --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe)
    set_tests_properties(fe_nav_league PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "entry 1 returned 0")
    # M10 interactive playable match (app/livematch.cpp): real-time window loop - sim fixed 1/50 s
    # cadence, scene + HUD drawn each frame with camera follow, commentary pump; synthesized keyboard
    # input drives the ported pad path (FUN_0047836c bit writes) proving the input->sim chain.
    add_test(NAME interactive_match COMMAND wc98-native --live-match-smoke 150 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/live)
    set_tests_properties(interactive_match PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    # Automated gameplay self-test (goal §14, app/selftest.cpp): real playable match via the
    # exhibition bootstrap, scripted pad words drive controller 0; steps measure internal sim
    # state (movement/sprint/turn/dribble/pass/shoot/physics/AI/keeper/clock) and write the JSON
    # report. Exit code is the verdict: 0 all-pass, 1 any failure, 2 tree skip.
    add_test(NAME self_test_gameplay COMMAND wc98-native --self-test /tmp/wc98port-out/selftest.json --tree ${WC98_TREE})
    set_tests_properties(self_test_gameplay PROPERTIES SKIP_RETURN_CODE 2 TIMEOUT 300 ENVIRONMENT "WC98_TREE=${WC98_TREE};QT_QPA_PLATFORM=offscreen")
    # Phase-11 structured-result schema (goal doc PHASE 11): every step must carry subsystem,
    # expected event, actual event, selected asset, playback timestamp, measured result,
    # tolerance, pass/fail and diagnostic. Shape-only gate; step verdicts stay with the test above.
    add_test(NAME selftest_json_schema COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/selftest_json_schema.sh
            $<TARGET_FILE:wc98-native> ${WC98_TREE})
    set_tests_properties(selftest_json_schema PROPERTIES SKIP_RETURN_CODE 2 TIMEOUT 600 ENVIRONMENT "WC98_TREE=${WC98_TREE};QT_QPA_PLATFORM=offscreen")
    # Authentic team-select fidelity (FEFLOW11.LAY pick grid, LAY_COMPOSITOR_NOTES.md): render the
    # screen via --fe-team; the shell asserts shapes/texts/cursors > 0 and this gate fails on zero counts.
    add_test(NAME fe_render_tournament COMMAND wc98-native --fe-smoke 2 --fe-team --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/feteam)
    set_tests_properties(fe_render_tournament PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    # Arbitrary-screen capture lane ("make all menus 1:1"): --fe-screen NAME renders + captures any
    # LAYOUTS.BIG member through the FE compositor (app/fe_flow.cpp loadCaptureScreen). Gate on
    # shapes>0; raw members carry no scene-code dynamic labels, so texts may legitimately be 0.
    add_test(NAME fe_screen_capture_menu COMMAND wc98-native --fe-screen FEFLOW05 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fescreen)
    set_tests_properties(fe_screen_capture_menu PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" FAIL_REGULAR_EXPRESSION "FEFLOW05: shapes=0 ")
    add_test(NAME fe_screen_capture_team COMMAND wc98-native --fe-screen FEFLOW11 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fescreen)
    set_tests_properties(fe_screen_capture_team PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" FAIL_REGULAR_EXPRESSION "FEFLOW11: shapes=0 ")
    # Full-layout capture sweep (MENU_PARITY.md §6): every LAYOUTS.BIG + IGLAY.BIG member and the
    # loose FE/INGAME .LAY files through --fe-screen; each frame must be a real PNG. Extract tool
    # enumerates archive members by dumping to a temp dir (tests/scripts/fe_screen_sweep.sh).
    add_test(NAME fe_screen_sweep COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fe_screen_sweep.sh
        $<TARGET_FILE:wc98-native> ${WC98_TREE} $<TARGET_FILE:wc98-extract>)
    set_tests_properties(fe_screen_sweep PROPERTIES ENVIRONMENT "WC98_TREE=${WC98_TREE}" TIMEOUT 1200)
    # League Play select (FE_MENU_SCREENS_WC98.md §4): --fe-league opens FEFLOW73.LAY, the pn00..pn08
    # league list; row labels are scene-code content at runtime, so shapes>0 is the gate.
    add_test(NAME fe_league_select COMMAND wc98-native --fe-league --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/league73)
    set_tests_properties(fe_league_select PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW73: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    # League configuration family (MENU_PARITY.md §4 rank 7; edges_table.txt SCENEREG feflow73 cluster):
    # League config family capture gates (pass-20uitick): each grid/popup must paint art + captions.
    # --fe-league-cfg N opens the grid/popup over FEFLOW73. kt/menu rows paint FC captions + kit swatches,
    # s001 KnobBar paints sbar/sbar art + value knob (analysis/pass12/{list_painter,keyloop}_notes.txt).
    foreach(_lq 72 74 75 76 78 79 80)
        add_test(NAME fe_league_cfg${_lq} COMMAND wc98-native --fe-league-cfg ${_lq} --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/lcfg${_lq})
        set_tests_properties(fe_league_cfg${_lq} PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW${_lq}: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    endforeach()

    # Retail Enter-edge grammar (pass-38League): FEFLOW75 strategy page, Enter (event 8005,
    # dispatch 0x45f49f mov eax,0x76; call 0x466cb8) opens the FEFLOW76 aggression popup plate.
    add_test(NAME fe_league_popup76 COMMAND wc98-native --fe-league-popup --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/lpop76)
    set_tests_properties(fe_league_popup76 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW76: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    # World-cup family interactive screens (MENU_NAV_WC98.md rank-2; analysis/pass12/wcfamily_notes.txt +
    # analysis/pass20uitick/NOTES.md scene-switch edges): --fe-wc-cfg N opens FEFLOW18 (zones draw, tmNN
    # rows + 'rand'), FEFLOW19 (groups board g{g}t{s} + rand), FEFLOW20 (today's games tgt/tns/tsd +
    # simu/frwd/stsc/strk), FEFLOW21 (QF bracket tm0N/qtmN/qfN), FEFLOW23/24 (R16 bracket boards),
    # FEFLOW35 (standings, nglt/ngrt pages), FEFLOW81 (results mult pane), FEFLOW82 (stats t00N rows).
    # Dynamic cells paint through the app overlays (pool names / TourneyModel), texts>0 proves content.
    foreach(_wq 18 19 20 21 23 24 35 81 82)
        add_test(NAME fe_wc_cfg${_wq} COMMAND wc98-native --fe-wc-cfg ${_wq} --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/wcfg${_wq})
        set_tests_properties(fe_wc_cfg${_wq} PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW${_wq}: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    endforeach()
    # Cheats screen (feflow95.lay, MENU_PARITY.md §8): objecttype-18 checkbox rows btn0..btn7 with the
    # cheatbox `1box` frames + exe-string captions ("Take a Dive" .. "Alien Model"). --fe-cheats N opens it
    # with row i ON for bit i: mask 85 = rows 0/2/4/6 checked (toggle glyph visible), mask 0 = all empty.
    add_test(NAME fe_cheats COMMAND wc98-native --fe-cheats 85 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/cheats)
    set_tests_properties(fe_cheats PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW95: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    add_test(NAME fe_cheats_off COMMAND wc98-native --fe-cheats 0 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/cheatsoff)
    set_tests_properties(fe_cheats_off PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW95: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    # World-Cup history boards (feflow90/91, analysis/pass13/CLASSICS_FE33.md): --fe-history N opens the
    # board at year row N (FCDB finalist pair + scores painted from the app-side dataset); --fe-nav-history
    # moves the bounded 'ysrb' cursor and presses Enter ('fe91') to capture the feflow91 continuation scene.
    add_test(NAME fe_history_select COMMAND wc98-native --fe-history 0 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/hist90)
    set_tests_properties(fe_history_select PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW90: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    add_test(NAME fe_history_select_1994 COMMAND wc98-native --fe-history 14 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/hist94)
    set_tests_properties(fe_history_select_1994 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW90: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    add_test(NAME fe_nav_history COMMAND wc98-native --fe-nav-history 5 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/histnav)
    set_tests_properties(fe_nav_history PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW91: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    # Single-player setup family (FEFLOW07 matchup + FEFLOW08 stadium select; analysis/pass13/IG_FAMILIES_SPEC.md §B,
    # MENU_NAV_WC98.md feflow07/08 rows): --fe-setup07 N opens the hub fe07-wedge screen (ltnm/rtnm names, FNNN shields,
    # FC-bank row captions 1691/1330/1741/573/1278, FCDB squad-skill bars) and moves the two-axis side cursor N times;
    # --fe-stadium08 N chains feflow07 Enter -> FEFLOW09 friendly config confirm -> 'fe08' stadium select (mpNN tiles +
    # stadium name overlays from FC-bank strings), cycling the tile ring cursor N times. Esc = back, Enter = advance.
    add_test(NAME fe_setup07 COMMAND wc98-native --fe-setup07 1 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/setup07)
    set_tests_properties(fe_setup07 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW07: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    add_test(NAME fe_stadium08 COMMAND wc98-native --fe-stadium08 3 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/stad08)
    set_tests_properties(fe_stadium08 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW08: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    # Practise stadium board (pass 33, analysis/pass33/NOTES.md): hub fe32-wedge Enter opens FEFLOW50 config,
    # confirm opens FEFLOW51 (France-map back collage + mpNN ball pins + hbal cursor hilite + nslt/nsrt arrow
    # pair + sint/wint/hint value icons + help-pane stadium name); --fe-stadium51 N cycles the pin ring N times.
    add_test(NAME fe_stad51 COMMAND wc98-native --fe-stadium51 3 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/stad51)
    set_tests_properties(fe_stad51 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW51: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    # Shared popup lane (FEFLOW10/37 + IG boxes; analysis/pass13/POPUP_LANE_SPEC.md): blur+popp modal with
    # the nvicons ok_1/canc and yes/no button pair, FC-bank runtime captions ("Are you sure you want to
    # Quit?", index 220) + help pane (button help= indices 12/13). Scripted-key gates: two-axis cursor over
    # the pair; Enter/Esc close to the caller scene (dismiss proof frames capture hub FEFLOW06 restored).
    add_test(NAME fe_popup_ok COMMAND wc98-native --fe-popup feflow10 --fe-popup-keys 1 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/popok)
    set_tests_properties(fe_popup_ok PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW10: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    add_test(NAME fe_popup_yesno COMMAND wc98-native --fe-popup feflow37 --fe-popup-keys 2 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/popyes)
    set_tests_properties(fe_popup_yesno PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW37: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    add_test(NAME fe_popup_dismiss_enter COMMAND wc98-native --fe-popup feflow37 --fe-popup-keys 2 --fe-popup-enter --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/popdis)
    set_tests_properties(fe_popup_dismiss_enter PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW06: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |cursors=0 ")
    add_test(NAME fe_popup_dismiss_esc COMMAND wc98-native --fe-popup feflow10 --fe-popup-back --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/popesc)
    set_tests_properties(fe_popup_dismiss_esc PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW06: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |cursors=0 ")
    # Raw panel + IG-box captures (--fe-screen pattern gates; raw members legitimately paint 0 texts/0
    # shapes — the full trio FAIL regex applies to the interactive lane above). fe_igconsel runs through
    # --fe-popup: the 'okay' box paints INGAME.FSH btnn/btnd chrome + "OK" caption over null.fsh region
    # placeholders (legit per IG_FAMILIES_SPEC §A), so shapes/texts>0 are provable there.
    add_test(NAME fe_popup_feflow86 COMMAND wc98-native --fe-screen FEFLOW86 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/poppanel)
    set_tests_properties(fe_popup_feflow86 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" FAIL_REGULAR_EXPRESSION "FEFLOW86: shapes=0 ")
    add_test(NAME fe_igconsel COMMAND wc98-native --fe-popup igconsel --fe-popup-keys 1 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/igconsel)
    set_tests_properties(fe_igconsel PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "igconsel: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    add_test(NAME fe_mesg COMMAND wc98-native --fe-screen mesg --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/mesg)
    set_tests_properties(fe_mesg PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "mesg: .*texts=[1-9]" FAIL_REGULAR_EXPRESSION "texts=0 |cursors=0 ")
    # IGMSG netplay message box + IGFLOW86 eligibility box (pass-37igpop): full frame composite (box/RECT
    # quads) + INGAME.FSH ok-button chrome + textid=60 "OK" caption via the remap lane; runtime message
    # strings (tick setters [57c926]/[57d164..6c]) are caller-provided content, textid=0 slots paint empty.
    foreach(_igpop igmsg igflow86)
        add_test(NAME fe_popup_${_igpop} COMMAND wc98-native --fe-popup ${_igpop} --fe-popup-keys 1 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/${_igpop})
        set_tests_properties(fe_popup_${_igpop} PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "(${_igpop}|IGMSG|IGFLOW86): .*shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 ")
    endforeach()
    # In-game option popups (igaoptns/iggoptns/igmoptns, analysis/pass13/IGOPTNS_ART.md): captions lane —
    # ROWS tuples + tab/help FC-bank captions painted; ok/canc/defa chck art is PROVEN ABSENT on this media,
    # so the widgets render as text captions (documented legit-missing slots), never invented art.
    foreach(_ig a g m)
        add_test(NAME fe_igoptns_${_ig} COMMAND wc98-native --fe-igoptns ${_ig} --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/igopt${_ig})
        set_tests_properties(fe_igoptns_${_ig} PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IG[AGM]OPTNS: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    endforeach()
    # Hub screen (Feflow06.lay, MENU_NAV_WC98.md §3 global-key table): the 13-wedge menu hub becomes
    # the main-menu composite when present; help pane paints FC captions -> texts>0.
    add_test(NAME fe_hub COMMAND wc98-native --fe-smoke 1 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/hub)
    set_tests_properties(fe_hub PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW06: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    # Options screen (feflow15.lay, MENU_PARITY.md §4 rank 1): feflow15 key handler 0x44f22c; --fe-options
    # opens the FEFLOW15 composite over the hub.
    add_test(NAME fe_flow_options COMMAND wc98-native --fe-options --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/opts)
    set_tests_properties(fe_flow_options PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW15: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    # Save/load entry (Feflow89.lay slot list b000..b005, MENU_PARITY.md §4 rank 6): --fe-saveload opens it.
    add_test(NAME fe_flow_saveload COMMAND wc98-native --fe-saveload --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/sl)
    set_tests_properties(fe_flow_saveload PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW89: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 ")
    # Credits roll (feflow94 behind the hub cred wedge, MENU_PARITY.md §4 rank 10): --fe-credits opens
    # it. The retail FEFLOW94 art pack is absent from the CD, so shapes are legitimately 0; the roll
    # contents gate asserts the CRED.BIN row stream parsed (formats/credits.hpp grammar).
    add_test(NAME fe_credits COMMAND wc98-native --fe-credits --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/cred)
    set_tests_properties(fe_credits PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW94: roll rows=[1-9]" FAIL_REGULAR_EXPRESSION "roll rows=0 ")
    # In-game pause menu (IGMENU.LAY + IGQUESHN.LAY over the live match, MENU_PARITY.md §4 rank 8):
    # scripted Esc opens the decoded friendly-1p row table (VA 0x57c7d8 = raw 0x17add8: 10
    # {textid,code} rows, 18-px lanes centred into box2); 'L' opens the CONTROL1 controller-select
    # board (pass-26); Down x5 walks the cursor to Quit Match (Instant Replay row masked), Enter
    # opens the IGQUESHN confirm, Esc cancels, Up x5 resumes at Continue (evidence lines + PNGs).
    add_test(NAME fe_igmenu COMMAND wc98-native --ig-pause-smoke 260 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/igmenu)
    set_tests_properties(fe_igmenu PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}"
        PASS_REGULAR_EXPRESSION "IGMENU: shapes=[1-9][0-9]* texts=[1-9];CONTROL1: shapes=[1-9][0-9]* texts=[1-9][0-9]*;IGQUESHN: shapes=[1-9][0-9]* texts=[1-9][0-9]*;igmenu: mouse activate row=6 code=6"
        FAIL_REGULAR_EXPRESSION "IGMENU: shapes=0 ;CONTROL1: shapes=0 ;IGQUESHN: texts=0 ")
    # IGOPTN2 in-game options board (pass-21igscn, analysis/pass21igmenu): the pause-menu Options row opens
    # the retail IGOPTN2 scene board (IGLAY.BIG, 17 widgets); the four tab panes lst1..lst4 = Match/Game
    # play/controller/audio with FC-bank caption rows from the ROWS tuples; '2'/'3'/'4' switch tabs;
    # UP/DOWN row cursor + LEFT/RIGHT value cycling; help pane = tbb help= captions via remap.
    # Per-tab/per-cycle evidence lines + PNGs (_igoptn2_t{N}.png, _igoptn2_cyc.png) pin panes 2/3/4.
    add_test(NAME fe_igoptn2_board COMMAND wc98-native --ig-optns-smoke 356 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/igoptn2b)
    set_tests_properties(fe_igoptn2_board PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IGOPTN2: shapes=[1-9][0-9]* texts=[1-9];IGOPTN2: tab=3 rows=[1-9] val=[0-9]+;IGOPTN2: cycle tab=0 row=0 val=1" FAIL_REGULAR_EXPRESSION "IGOPTN2: shapes=0 ;IGOPTN2: tab=[0-9]+ rows=0 ;IGOPTN2: cycle tab=0 row=0 val=0 ")
    # IG presentation boards (IG_FAMILIES_SPEC.md §C): scripted livematch lanes open the composite scenes
    # from last-match data. fe_igstats tours IGMENU's Stats row; fe_ighalf forces the half whistle via the
    # exe period-end grammar; fe_igscsum/fe_igflsum run to full time and page the chain (PNGs per board).
    add_test(NAME fe_igstats COMMAND wc98-native --ig-stats-smoke 380 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/igstats)
    set_tests_properties(fe_igstats PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IGSTATS: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "IGSTATS: shapes=0 |texts=0 ")
    add_test(NAME fe_ighalf COMMAND wc98-native --ig-half-smoke 5400 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/ighalf)
    set_tests_properties(fe_ighalf PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IGHALF: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "IGHALF: shapes=0 |texts=0 ")
    add_test(NAME fe_igscsum COMMAND wc98-native --ig-full-smoke 9000 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/igfull)
    set_tests_properties(fe_igscsum PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IGSCSUM: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "IGSCSUM: shapes=0 |texts=0 ")
    add_test(NAME fe_igflsum COMMAND wc98-native --ig-full-smoke 9000 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/igfull)
    set_tests_properties(fe_igflsum PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IGFLSUM: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "IGFLSUM: shapes=0 |texts=0 ")
    # Highlights browser + save list (IG_FAMILIES_SPEC.md §D): deterministic fixture slots seed the
    # gate user dir; IGMENU Highlights row opens IGHILITE over them, Enter = app/replay.cpp re-drive.
    # fe_hilsav_list: 'S' on the browser opens IGHILSAV (rply index rows + flag/shield pair), Enter writes.
    add_test(NAME fe_highlite_browser COMMAND wc98-native --ig-hlite-smoke 460 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/hlite)
    set_tests_properties(fe_highlite_browser PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IGHILITE: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "IGHILITE: shapes=0 |rply-play.*FAILED|texts=0$ ")
    add_test(NAME fe_hilsav_list COMMAND wc98-native --ig-hlsav-smoke 470 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/hlsav)
    set_tests_properties(fe_hilsav_list PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IGHILSAV: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "IGHILSAV: shapes=0 |hilsav: saved rply0000|texts=0$ ")
    # fe_mgmt_pages (§E): IGMENU Management row opens IGTEAM72 over the seeded fixture; gobk/gofd cycle 73->74->75->79->80, Enter swaps the 73 ab-pair (FCDB data), 'B' opens hub IGTEAM89 rows.
    add_test(NAME fe_mgmt_pages COMMAND wc98-native --ig-mgmt-smoke 370 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/mgmt)
    set_tests_properties(fe_mgmt_pages PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IGTEAM89: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "IGTEAM[0-9]+: shapes=0 |igmgmt: swap pair ab01$|texts=0$ ")
    # fe_manmarks_grid (§E): igteam72 'M' opens the MANMARKS ratings board over the seeded fixture: FCDB pNNN squad grid rows (shirt + name + kit swatch; row cursor pans a five-row window, sbar thumb), mm00..mm02 home-defender <-> away-forward pair captions.
    add_test(NAME fe_manmarks_grid COMMAND wc98-native --ig-manmarks-smoke 370 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/manmarks)
    set_tests_properties(fe_manmarks_grid PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "MANMARKS: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "MANMARKS: shapes=0 |MANMARKS: texts=0$ ")
    # fe_camera_pages (§G): live pause -> 'C' opens IGCAMERA (FC#353 title, 5 key-assignment rows FC 1468/1011/1545/1546/1104 + binding strings); RIGHT forw nav opens the IGCAMSEL selection grid (7 FC camera names), row cursor + Enter applies a MatchCamera mode; Esc x2 resumes.
    add_test(NAME fe_camera_pages COMMAND wc98-native --ig-camera-smoke 360 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/cam)
    set_tests_properties(fe_camera_pages PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IGCAMSEL: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "IGCAMERA: shapes=0 |IGCAMSEL: shapes=0 |igcam: camera (nil|)$ ")
    # fe_practise_pages (§G): live pause -> 'P' opens PRACT1 (FC#1249 stepper row + keeper toggles FC 1920/374, On/Off 1267/1263); btn-nav RIGHT tabs IGPRACT1->IGPRACT2, LEFT wraps back; DOWN+Enter flips the User Keeper row; Esc x2 resumes.
    add_test(NAME fe_practise_pages COMMAND wc98-native --ig-practise-smoke 360 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/prac)
    set_tests_properties(fe_practise_pages PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "IGPRACT1: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "IGPRACT1: shapes=0 |IGPRACT2: shapes=0 |IGPRAC1: shapes=0 ")
    # fe_control_page (§H): options chain -> CONTROL1 controller page: FC#258 'Attacking' ctx caption, grv* Gravis bars from the DAT_00632778 scene-default percent table (+ live pad axis readout when a gamepad is open), npll/nplr team-mates numbers under tmtl/tmtr (FC#4594).
    add_test(NAME fe_control_page COMMAND wc98-native --fe-control 2 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/control1)
    set_tests_properties(fe_control_page PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "CONTROL1: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0$ ")
    # FEFLOW43 management grid (MENU_PARITY rows 29/30, pass13 §E): composite iNNn button chain + back
    # collage via FE/PACK/FEFLOW43; overlays paint FCDB squad rows ("#shirt name" + kit swatch on vNNN),
    # FC-bank position labels (GK=810 SW=1801 RB=1531 ... FW=751) and the aNNN icon columns from
    # FE/PACK/FEFLOW73 (redc/yell/but1..3); sbar thumb pans the four-row window.
    add_test(NAME fe_mgmt_grid43 COMMAND wc98-native --fe-mgmt-grid 2 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/mgmt43)
    set_tests_properties(fe_mgmt_grid43 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW43: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 ")
    # feflow41 LOAD list: scratch slot dir (XDG_DATA_HOME) seeded by the deterministic SLOT00/SLOT01
    # fixture; lstb rows scan real SLOT%.2d.SAV files and print summary facts, sbar thumb pans pages.
    add_test(NAME fe_loadsave41 COMMAND wc98-native --fe-load-game 1 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/load41)
    set_tests_properties(fe_loadsave41 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE};XDG_DATA_HOME=/tmp/wc98port-slots-load" PASS_REGULAR_EXPRESSION "FEFLOW41: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 ")
    # feflow42 SAVE confirm: list rows over the scratch dir, Enter writes the focused slot through the
    # existing saveTournament path and flips cros(idle) -> tick(tic1); evidence line pins the write.
    add_test(NAME fe_savemgr42 COMMAND wc98-native --fe-save-manager 0 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/save42)
    set_tests_properties(fe_savemgr42 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE};XDG_DATA_HOME=/tmp/wc98port-slots-save" PASS_REGULAR_EXPRESSION "fe: saved slot 00" FAIL_REGULAR_EXPRESSION "save FAILED |FEFLOW42: shapes=0 ")
    # FEFLOW57 replay browser (pass-29): hub wedge 'repl' [C] leaf 0x4491d5; scratch dir seeded with the
    # deterministic rplyindx + rply0001/0002 fixture (RPLY.md container), lstb rows = index filenames,
    # sbar thumb 10-row windows, fe06 crs1 close icon; Enter re-drives the focused replay via replayFile.
    add_test(NAME fe_replay57 COMMAND wc98-native --fe-replay-load 1 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/repl57)
    set_tests_properties(fe_replay57 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE};XDG_DATA_HOME=/tmp/wc98port-slots-repl" PASS_REGULAR_EXPRESSION "FEFLOW57: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "FEFLOW57: shapes=0 ")
    # Enter lane: move to row 2 (bit-8 sentinel), Enter re-drives the focused rply file through
    # replayFile — sealed hash == re-driven state hash proves deterministic re-drive (match=1).
    add_test(NAME fe_replay57_enter COMMAND wc98-native --fe-replay-load 257 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/repl57e)
    set_tests_properties(fe_replay57_enter PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE};XDG_DATA_HOME=/tmp/wc98port-slots-repl-enter" PASS_REGULAR_EXPRESSION "replay rply0002.dat ticks=180 recorded=0x[0-9A-F]+ replayed=0x[0-9A-F]+ match=1" FAIL_REGULAR_EXPRESSION "FAILED replay|FAILED to record")
    # feflow16 lineups board (pass-21igscn grid grammar): 6 shapes (4 LAY + 2 team-flag overdraws),
    # 46 texts = 2 FC-bank captions + 22 left rows (x=6 y=0..169) + 22 right rows (x=630/640 y=293..463).
    add_test(NAME fe_lineups16 COMMAND wc98-native --fe-lineups16 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/fe16)
    set_tests_properties(fe_lineups16 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW16: shapes=[1-9][0-9]* texts=46 " FAIL_REGULAR_EXPRESSION "texts=0 |shapes=0 ")
    # Records/trivia family FEFLOW28..31/34/88 (MENU_PARITY rank 5, pass-20records): composite
    # scene art + overlays from FE/TEAMHIST.ENG (.HIS multitext cells, .TRI pool) and .RES
    # head-to-head tables; 2905 = team-cursor nav proof (page 29, five pool-id ring moves).
    add_test(NAME fe_records28 COMMAND wc98-native --fe-records 2800 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/rec28)
    set_tests_properties(fe_records28 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW28: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 ")
    add_test(NAME fe_records29 COMMAND wc98-native --fe-records 2900 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/rec29)
    set_tests_properties(fe_records29 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW29: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 ")
    add_test(NAME fe_records30 COMMAND wc98-native --fe-records 3000 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/rec30)
    set_tests_properties(fe_records30 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW30: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 ")
    add_test(NAME fe_records31 COMMAND wc98-native --fe-records 3100 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/rec31)
    set_tests_properties(fe_records31 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW31: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 ")
    add_test(NAME fe_records34 COMMAND wc98-native --fe-records 3400 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/rec34)
    set_tests_properties(fe_records34 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW34: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 ")
    add_test(NAME fe_records88 COMMAND wc98-native --fe-records 8800 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/rec88)
    set_tests_properties(fe_records88 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW88: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 ")
    add_test(NAME fe_records_nav COMMAND wc98-native --fe-records 2905 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/recnav)
    set_tests_properties(fe_records_nav PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "rec: page=29 team=5" FAIL_REGULAR_EXPRESSION "records page unavailable")
    add_test(NAME fe_records_window COMMAND wc98-native --fe-records 2900 --fe-records-row 12 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/recwin)
    set_tests_properties(fe_records_window PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "rec: page=29 team=0 row=12" FAIL_REGULAR_EXPRESSION "records page unavailable")
    # Full-scroll over-read edge (pass-20records): slider max 31 [C records_dec11] with
    # own==offset==31 -> running idx reaches pool idx 40; paint site guards oi>=n (blank
    # row, no OOB). The exe over-reads mapped DGROUP (D-73); port must render, not abort.
    add_test(NAME fe_records_window_tail COMMAND wc98-native --fe-records 2931 --fe-records-row 31 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/recwin31)
    set_tests_properties(fe_records_window_tail PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "rec: page=29 team=31 row=31" FAIL_REGULAR_EXPRESSION "records page unavailable")
    # Controller-select family FEFLOW32/33 (MENU_PARITY rank 2, pass-20cfg): composite art +
    # team-name/shield/bar overlays; 3203 = pool-cursor nav proof (three ltrb ring moves);
    # 3310 side=1 = right-side focus (UP/DOWN two-axis grammar, rtlb/rtrb lane).
    add_test(NAME fe_cfg32 COMMAND wc98-native --fe-cfg 3200 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/cfg32)
    set_tests_properties(fe_cfg32 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW32: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |controller-select screen unavailable")
    add_test(NAME fe_cfg33 COMMAND wc98-native --fe-cfg 3300 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/cfg33)
    set_tests_properties(fe_cfg33 PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FEFLOW33: shapes=[1-9][0-9]* texts=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |controller-select screen unavailable")
    add_test(NAME fe_cfg32_nav COMMAND wc98-native --fe-cfg 3203 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/cfg32nav)
    set_tests_properties(fe_cfg32_nav PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "cfg: screen=32 idx=3" FAIL_REGULAR_EXPRESSION "controller-select screen unavailable")
    add_test(NAME fe_cfg33_side COMMAND wc98-native --fe-cfg 3302 --fe-cfg-side 1 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/cfg33side)
    set_tests_properties(fe_cfg33_side PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "cfg: screen=33 idx=0 left=0 right=3 side=1" FAIL_REGULAR_EXPRESSION "controller-select screen unavailable")
    # Between-round tournament presentation (FEFLOW35/25/26/27, LAY_COMPOSITOR_NOTES.md "Tournament
    # presentation scenes"): one stat line per screen; fails on zero shapes/texts/cursors.
    add_test(NAME fe_render_present COMMAND wc98-native --fe-smoke 1 --fe-present --fe-pick 26 --tree ${WC98_TREE} --out-prefix /tmp/wc98port-out/present)
    set_tests_properties(fe_render_present PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "WC98_TREE=${WC98_TREE}" PASS_REGULAR_EXPRESSION "FE-PRESENT FEFLOW35: shapes=[1-9]" FAIL_REGULAR_EXPRESSION "shapes=0 |texts=0 |cursors=0 |FE-ANNOUNCER decoded=0 |FE-ANNOUNCER decoded=[1-9][0-9]* plays=0 ")
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

# Camera parameter provenance gate (pass-16cam, register D-54): FOV/task-height table DAT_0057FB82
# byte-exact + accessor read shape/instruction bytes + caminit FUN_004a0810 and preset-row code
# immediates; FIFA-99 cites (FOV 0x57FAD8, kModes zone tables) proven absent. Tree-gated.
wc98_test(test_cam_tables unit/test_cam_tables.cpp)
target_include_directories(test_cam_tables PRIVATE ${CMAKE_SOURCE_DIR}/presentation/src)
target_link_libraries(test_cam_tables PRIVATE wc98_presentation)

# FC widget-id remap table provenance gate (pass-21igmenu): DAT_005774a4 full 4096-dword byte-exact
# dump + FUN_0043c9a0/FUN_004c31d0 accessor bytes; list painter (fc_painter.txt:1231-1257) resolves
# ROWS labels through it, so the IGOPTN2 trio's sentinel rows draw empty, not raw FC-bank soup.
wc98_test(test_fcwidget unit/test_fcwidget.cpp)
target_include_directories(test_fcwidget PRIVATE ${CMAKE_SOURCE_DIR}/presentation/src)
target_link_libraries(test_fcwidget PRIVATE wc98_presentation)

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
target_link_libraries(test_gameaudio PRIVATE wc98_simulation) # (e) boot lane: sim::loadGameData
# Scene sound-level layer grammar (DAT_00632778 percent table + Q7 gain + notifier dispatch).
wc98_test(test_scene_levels unit/test_scene_levels.cpp)
target_link_libraries(test_scene_levels PRIVATE wc98_audio)

# SMIX native mixer grammar (pass-18smix): Q16 resampler FUN_0054d4fb boundary rows, pan-law
# installer combo table + i/127 normalization, 2x2 pan-matrix handlers, voice-block ramp flags.
wc98_test(test_smix_grammar unit/test_smix_grammar.cpp)
target_link_libraries(test_smix_grammar PRIVATE wc98_audio)

# SMIX voice-layer grammar (pass-18voice): ramp helper byte rows + i/127 convergence, start/stop
# record grammar with serve-pump hook, frequency clamp boundaries, centi-ratio pitch table, TLV
# sample-format parser tag-table round-trip.
wc98_test(test_voice_layer unit/test_voice_layer.cpp)
target_link_libraries(test_voice_layer PRIVATE wc98_audio)
wc98_test(test_sqf unit/test_sqf.cpp)
wc98_test(test_sci unit/test_sci.cpp)
wc98_test(test_res unit/test_res.cpp)
