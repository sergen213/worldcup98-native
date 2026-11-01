add_library(wc98core INTERFACE)
add_library(wc98::core ALIAS wc98core)

target_include_directories(wc98core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(wc98core INTERFACE cxx_std_23)
