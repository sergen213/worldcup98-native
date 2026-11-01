# Parsers for original World Cup 98 file formats (EA FIFA-97/98/99 family).
# Each parser lives in src/<name>.cpp with a header in include/wc98/formats/<name>.hpp.
# Ported from fifa99-native (formats/) with the WC98 deltas recorded in
# docs/research and in code comments; see research-inbox/F99_REUSABILITY_MATRIX.md.
file(GLOB WC98_FORMAT_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(wc98_formats STATIC ${WC98_FORMAT_SOURCES})
target_include_directories(wc98_formats PUBLIC include)
target_link_libraries(wc98_formats PUBLIC wc98::core)
# Optional per-area link dependencies (e.g. libpng) are declared in formats/deps/*.cmake.
file(GLOB WC98_FORMAT_DEPS CONFIGURE_DEPENDS deps/*.cmake)
foreach(d IN LISTS WC98_FORMAT_DEPS)
  include(${d})
endforeach()
