# M4 slice 3: render original FE GEOM/SKEL .MDL models offscreen through the Vulkan backend.
add_executable(wc98-model-view main.cpp)
target_link_libraries(wc98-model-view PRIVATE wc98_renderer_vk wc98_formats wc98::core)
target_include_directories(wc98-model-view PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
