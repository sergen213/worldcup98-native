# Vulkan 1.3 backend. Ported from fifa99-native renderer/vulkan/. GLSL in shaders/
# is compiled by glslc at build time into SPIR-V word lists (-mfmt=num) that
# src/vk_renderer.cpp #includes into uint32_t arrays, so the binary carries its
# shaders and needs no runtime shader files.
find_package(Vulkan REQUIRED COMPONENTS glslc)

set(WC98_SHADERS mesh.vert skin.vert ui.vert textured.frag)
set(WC98_SHADER_INCS)
foreach(s IN LISTS WC98_SHADERS)
  set(out ${CMAKE_CURRENT_BINARY_DIR}/shaders/${s}.inc)
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMAND Vulkan::glslc --target-env=vulkan1.3 -O -Werror -mfmt=num -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${s}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${s} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common.glsl
    COMMENT "glslc ${s}" VERBATIM)
  list(APPEND WC98_SHADER_INCS ${out})
endforeach()

add_library(wc98_renderer_vk STATIC src/vk_renderer.cpp ${WC98_SHADER_INCS})
target_include_directories(wc98_renderer_vk PUBLIC include PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shaders)
target_link_libraries(wc98_renderer_vk PUBLIC wc98_renderer Vulkan::Vulkan PRIVATE wc98::core)
# Vulkan structs are initialised as `VkFoo x{VK_STRUCTURE_TYPE_FOO}` (rest zeroed) on purpose.
target_compile_options(wc98_renderer_vk PRIVATE -Wno-missing-field-initializers)
