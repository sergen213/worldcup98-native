# Match presentation: snapshot interpolation, cameras, kit textures, player/stadium/ball drawing (docs/development/MILESTONE6.md).
add_library(wc98_presentation STATIC src/snapshot.cpp src/model.cpp src/data.cpp src/kits.cpp src/camera.cpp src/match_scene.cpp src/hud.cpp src/fe_screen.cpp)
target_include_directories(wc98_presentation PUBLIC include)
target_link_libraries(wc98_presentation PUBLIC wc98_renderer wc98_formats PRIVATE wc98::core)
target_link_libraries(wc98_presentation PRIVATE wc98_simulation)

# Interface headers (data.hpp/kits.hpp include renderer + formats): expose for consumers (tests, app).
target_link_libraries(wc98_presentation PUBLIC wc98_renderer)
