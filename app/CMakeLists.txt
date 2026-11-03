add_executable(wc98-native main.cpp)
target_link_libraries(wc98-native PRIVATE wc98_renderer_vk wc98_config wc98_platform wc98::core)
add_executable(wc98-match-demo match_demo.cpp)
target_link_libraries(wc98-match-demo PRIVATE wc98_presentation wc98_renderer_vk wc98_simulation wc98_input wc98_config wc98_platform wc98_formats wc98::core)
