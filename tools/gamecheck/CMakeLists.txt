# Runtime-view game database gate (goal phases 3/8/11): GameData identity + kit assets.
add_executable(wc98-gamecheck main.cpp)
target_link_libraries(wc98-gamecheck PRIVATE wc98_simulation wc98_formats wc98core)
