# Renderer. Ported from fifa99-native renderer/. The public interface is pure data;
# the Vulkan implementation lives in renderer/vulkan (wc98_renderer_vk).
find_package(glm REQUIRED CONFIG)
# Distro GLM packaging varies: some ship only glm::glm (Ubuntu), some add the
# header-only variant (Arch). Map one onto the other so both work.
if(NOT TARGET glm::glm-header-only AND TARGET glm::glm)
  add_library(glm::glm-header-only INTERFACE IMPORTED GLOBAL)
  target_link_libraries(glm::glm-header-only INTERFACE glm::glm)
endif()

add_library(wc98_renderer STATIC src/renderer.cpp)
target_include_directories(wc98_renderer PUBLIC include)
target_link_libraries(wc98_renderer PUBLIC wc98::core glm::glm-header-only)
# Renderer math: Vulkan depth range + radians (GLM defaults are GL-shaped).
target_compile_definitions(wc98_renderer PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)

add_subdirectory(vulkan)
