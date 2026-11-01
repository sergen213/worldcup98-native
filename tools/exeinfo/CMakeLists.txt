# Derived from fifa99-native (same author), ported for World Cup 98
add_executable(wc98-exeinfo main.cpp)
target_link_libraries(wc98-exeinfo PRIVATE wc98_formats wc98core)
