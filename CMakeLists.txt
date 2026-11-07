cmake_minimum_required(VERSION 3.28)

project(worldcup98_native
    VERSION 0.3.1
    DESCRIPTION "Native Linux reimplementation of EA Sports World Cup 98 (1998) — preservation project"
    LANGUAGES CXX)

# C++ standard: C++26 (finalized; supported by the GCC 16 / Clang 23 toolchains used
# on the primary CachyOS dev environment). CI pins a standard explicitly.
if(NOT DEFINED WC98_CXX_STANDARD)
    set(WC98_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD ${WC98_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WC98_BUILD_TESTS "Build automated tests" ${PROJECT_IS_TOP_LEVEL})

# Format parsers and research tools compile warnings-as-errors (GCC 16 / C++26
# and the C++23 CI fallback).
add_compile_options(-Wall -Wextra -Werror)

add_subdirectory(core)
add_subdirectory(formats)
add_subdirectory(assets)
add_subdirectory(simulation)
add_subdirectory(platform)
add_subdirectory(renderer)
add_subdirectory(config)
add_subdirectory(audio)
add_subdirectory(input)
add_subdirectory(presentation)
add_subdirectory(tournament)
add_subdirectory(tools)
add_subdirectory(launcher)

if(WC98_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_subdirectory(app)
