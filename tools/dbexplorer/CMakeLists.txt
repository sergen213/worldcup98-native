# Derived from fifa99-native (same author), ported for World Cup 98
add_executable(wc98-dbexplorer main.cpp)
target_link_libraries(wc98-dbexplorer PRIVATE wc98_formats wc98core)
target_include_directories(wc98-dbexplorer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
