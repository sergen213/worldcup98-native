// Headless .DCT playback tool (docs/asset-formats/VIDEO.md): decodes + uploads
// every frame of a movie through the Vulkan renderer. `--gate` mode prints the
// stat line "VIDEO frames=N size=WxH fps=F" and exits 2 when unavailable.
#include "video.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: wc98-video-play [--gate] FILE.DCT\n"); return 1; }
    int gate = 0;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--gate") gate = 1;
        else path = a;
    }
    if (path.empty()) return 1;
    return wc98::app::runVideoPlay(path, gate);
}
