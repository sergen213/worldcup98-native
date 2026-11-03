// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Non-GUI launcher helpers shared by the window, the self-test and the troubleshooting page.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace wc98::launcher {

struct Paths {
    std::filesystem::path settings; // $XDG_CONFIG_HOME/wc98-native/settings.ini ($WC98_CONFIG wins)
    std::filesystem::path game;     // $XDG_DATA_HOME/wc98-native/game (auto-detect search dir)
    std::filesystem::path logs;     // $XDG_STATE_HOME/wc98-native/log
};
Paths paths();

struct GpuInfo {
    std::string name, type, apiVersion, driverName, driverInfo, driverVersion, uuid; // uuid: pipelineCacheUUID hex
};
struct VulkanInfo {
    bool loader = false;         // vkCreateInstance succeeded
    std::string instanceVersion; // "1.4.363"
    std::string error;
    std::vector<GpuInfo> gpus;
};
VulkanInfo queryVulkan();
std::vector<std::string> openalDevices();
std::string sdlVersion(); // "3.4.18 (compiled against 3.4.18)"

// Locates the game executable: next to the launcher, then $PATH. Empty when not found.
std::filesystem::path findGame();

// Full multi-line report for "Copy diagnostics" and --self-test.
std::string diagnostics();

} // namespace wc98::launcher
