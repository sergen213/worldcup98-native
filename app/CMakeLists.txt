add_executable(wc98-native main.cpp)
target_link_libraries(wc98-native PRIVATE wc98_renderer_vk wc98_config wc98_platform wc98::core)
