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
endif()
