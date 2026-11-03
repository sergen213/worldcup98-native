# wc98-launcher (Qt 6 Widgets): game files, match options, controls, graphics, audio, logs,
# diagnostics, Play. Ported from fifa99-native launcher/; WC98 deltas recorded in
# docs/development/MILESTONE8_LAUNCHER_PORT.md. Skipped with a message when Qt 6 Widgets is not
# installed; the game itself does not need Qt.
find_package(Qt6 6.4 COMPONENTS Widgets Concurrent)
if(NOT Qt6_FOUND)
  message(STATUS "Qt 6 Widgets not found: wc98-launcher is not built")
  return()
endif()
find_package(OpenAL CONFIG REQUIRED)
find_package(Vulkan REQUIRED) # headers only: the loader is dlopen()ed at run time

# Pages live in a library so tests can drive them offscreen (registration lives in tests/).
add_library(wc98_launcher_ui STATIC src/common.cpp src/mainwindow.cpp src/pages.cpp src/controls.cpp)
target_include_directories(wc98_launcher_ui PUBLIC src)
add_executable(wc98-launcher src/main.cpp)
# The resource alias keeps the source name; expose it as :/icon.png.
set_source_files_properties(${PROJECT_SOURCE_DIR}/packaging/linux/wc98-native.png
  PROPERTIES QT_RESOURCE_ALIAS icon.png)
qt_add_resources(wc98-launcher icon PREFIX / BASE ${PROJECT_SOURCE_DIR}/packaging/linux
  FILES ${PROJECT_SOURCE_DIR}/packaging/linux/wc98-native.png)
target_compile_options(wc98_launcher_ui PRIVATE -Wno-missing-field-initializers) # Vk*Info{sType} idiom
target_compile_definitions(wc98_launcher_ui PUBLIC WC98_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)
target_link_libraries(wc98-launcher PRIVATE wc98_launcher_ui)
target_link_libraries(wc98_launcher_ui PUBLIC wc98_config wc98_assets wc98_formats Qt6::Widgets Qt6::Concurrent
  OpenAL::OpenAL Vulkan::Headers ${CMAKE_DL_LIBS})
