# Derived from fifa99-native (same author), ported for World Cup 98
find_package(OpenSSL REQUIRED)
add_executable(wc98-inventory main.cpp)
target_link_libraries(wc98-inventory PRIVATE wc98_formats wc98core OpenSSL::Crypto)
target_include_directories(wc98-inventory PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
