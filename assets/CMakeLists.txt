# WC98 game-data access: tree discovery, validation manifest, layered VFS. Ported from
# fifa99-native assets/; WC98 deltas (no DATA/ root, no patch layer, no libarchive ISO reader)
# are recorded in the headers and docs/development/MILESTONE8_LAUNCHER_PORT.md.
# Add with add_subdirectory(assets) after formats/.
find_package(OpenSSL REQUIRED) # SHA-256 for validation (same provider as tools/inventory)
add_library(wc98_assets STATIC src/install.cpp src/manifest.cpp src/vfs.cpp)
target_include_directories(wc98_assets PUBLIC include PRIVATE src)
target_link_libraries(wc98_assets PUBLIC wc98::core wc98_formats PRIVATE OpenSSL::Crypto)
