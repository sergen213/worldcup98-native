#pragma once
#include <string>

namespace wc98::app {

// EA .DCT movie playback through the Vulkan renderer (docs/asset-formats/VIDEO.md).
// gate != 0: headless decode + texture upload of every frame, prints the stat line
// "VIDEO frames=N size=WxH fps=F"; returns 2 when input/Vulkan is unavailable.
// Windowed mode presents at the per-frame interval from the mTCD payload header
// (BE u16 @rec+18 milliseconds; docs/asset-formats/VIDEO.md cadence note).
int runVideoPlay(const std::string& path, int gate, unsigned width = 1280, unsigned height = 960, bool fullscreen = false);

} // namespace wc98::app
