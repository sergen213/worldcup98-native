# Platform abstraction (SDL3): window, paths, input mapping. Ported from
# fifa99-native platform/. SDL3 is REQUIRED: wc98-native has no displayless
# binary path (headless work lives in the offline tools/).
find_package(SDL3 CONFIG REQUIRED)

add_library(wc98_platform STATIC src/paths.cpp src/window.cpp)
target_include_directories(wc98_platform PUBLIC include)
target_link_libraries(wc98_platform PUBLIC SDL3::SDL3 wc98::core)
