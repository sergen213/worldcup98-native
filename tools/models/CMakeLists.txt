# Derived from fifa99-native (same author), ported for World Cup 98
# PCMD model / player animation tools (models area).
foreach(t model-export anim-inspect)
  string(REPLACE "-" "_" src ${t})
  add_executable(wc98-${t} ${src}.cpp)
  target_link_libraries(wc98-${t} PRIVATE wc98_formats wc98core)
  target_include_directories(wc98-${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
endforeach()
