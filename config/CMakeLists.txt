# INI settings (parser keeps unknown keys, round-trips on save). Ported from
# fifa99-native config/; WC98-specific: no language/control binding surface.
add_library(wc98_config STATIC src/settings.cpp)
target_include_directories(wc98_config PUBLIC include)
target_link_libraries(wc98_config PUBLIC wc98_platform)
