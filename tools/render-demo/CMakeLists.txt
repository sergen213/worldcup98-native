add_executable(wc98-render-demo main.cpp)
target_link_libraries(wc98-render-demo PRIVATE wc98_renderer_vk wc98_formats wc98::core)
