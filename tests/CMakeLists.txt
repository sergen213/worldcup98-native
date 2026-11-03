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

# Match simulation (ported from fifa99-native tests/sim.cmake). The F99_SIM_LINKS gate is dropped:
# the linking gap Anim_evalSkeleton 0x460ACC is implemented against the WC98 rig
# (docs/reverse-engineering/SIM_PORT_NOTES.md section 5), so all three suites build.
foreach(t IN ITEMS test_sim_core test_sim_players test_sim_team)
    wc98_test(${t} unit/${t}.cpp)
    target_link_libraries(${t} PRIVATE wc98_simulation)
endforeach()

# Input layer: synthetic SDL events (keyboard pad words, virtual gamepad, hot-plug).
wc98_test(test_input unit/test_input.cpp)
target_link_libraries(test_input PRIVATE wc98_input)


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
endif()

# M6 presentation core: snapshot interpolation, camera names, kit-file selection, face composite.
wc98_test(test_presentation unit/test_presentation.cpp)
target_link_libraries(test_presentation PRIVATE wc98_presentation)

# M8 launcher stack: settings round trip, OPTIONS.SAV codec sync, tree detect + manifest validate.
if(TARGET wc98-launcher)
    add_test(NAME launcher_selftest COMMAND wc98-launcher --self-test)
    set_tests_properties(launcher_selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
