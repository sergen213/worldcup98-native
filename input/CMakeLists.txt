# SDL3 keyboard/mouse/gamepad input -> exe pad words for the simulation.
find_package(SDL3 CONFIG REQUIRED)
# Uses only the header-only sim types (ControllerState), not the wc98_sim library.
add_library(wc98_input STATIC src/input.cpp)
target_include_directories(wc98_input PUBLIC include ${PROJECT_SOURCE_DIR}/simulation/include)
target_link_libraries(wc98_input PUBLIC wc98_config SDL3::SDL3)
