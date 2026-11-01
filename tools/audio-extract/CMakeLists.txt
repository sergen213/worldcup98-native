# Derived from fifa99-native (same author), ported for World Cup 98
add_executable(wc98-audio-extract main.cpp)
target_link_libraries(wc98-audio-extract PRIVATE wc98_formats wc98core)
target_include_directories(wc98-audio-extract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
