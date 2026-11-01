# Derived from fifa99-native (same author), ported for World Cup 98
# textures area: PNG export (formats/src/png.cpp).
find_package(PNG REQUIRED)
target_link_libraries(wc98_formats PUBLIC PNG::PNG)
