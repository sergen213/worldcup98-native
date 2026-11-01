# Standalone research/extraction tools for World Cup 98 assets: one
# subdirectory per tool, each with its own CMakeLists.txt.
# Ported from fifa99-native (tools/), with WC98-specific schema handling.
file(GLOB WC98_TOOL_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(d IN LISTS WC98_TOOL_DIRS)
  if(IS_DIRECTORY ${d} AND EXISTS ${d}/CMakeLists.txt)
    add_subdirectory(${d})
  endif()
endforeach()
