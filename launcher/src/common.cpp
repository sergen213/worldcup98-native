// Derived from fifa99-native (same author), ported for World Cup 98
#include "common.hpp"

#include "wc98/assets/install.hpp"
#include "wc98/config/settings.hpp"
#include "wc98/platform/paths.hpp"

#include <AL/alc.h>
#include <SDL3/SDL_version.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <dlfcn.h>

#include <cstdlib>
#include <format>
#include <string_view>
#include <unistd.h>

namespace wc98::launcher {
namespace fs = std::filesystem;

Paths paths() {
    auto d = platform::userDirs("wc98-native");
    return {config::defaultPath(), d.data / "game", d.log};
}

namespace {

std::string version(std::uint32_t v) {
    return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
}

std::string driverVersion(std::uint32_t vendor, std::uint32_t v) {
    if (vendor == 0x10DE)
        return std::format("{}.{}.{}.{}", v >> 22, (v >> 14) & 0xff, (v >> 6) & 0xff, v & 0x3f); // NVIDIA
    return version(v);
}

std::string_view typeName(VkPhysicalDeviceType t) {
    switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "cpu";
    default:
        return "other";
    }
}

} // namespace

// The loader is opened at run time so the launcher still starts (and can explain) without it.
VulkanInfo queryVulkan() {
    VulkanInfo r;
    void* lib = ::dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        r.error = "libvulkan.so.1 not found: install the Vulkan loader (vulkan-icd-loader / libvulkan1)";
        return r;
    }
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(::dlsym(lib, "vkGetInstanceProcAddr"));
#define WC98_VK(inst, fn) auto fn = reinterpret_cast<PFN_##fn>(gipa(inst, #fn))
    WC98_VK(nullptr, vkEnumerateInstanceVersion);
    WC98_VK(nullptr, vkCreateInstance);
    std::uint32_t iv = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&iv);
    r.instanceVersion = version(iv);
    VkApplicationInfo app{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "wc98-launcher";
    app.apiVersion = iv >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    VkInstance inst = VK_NULL_HANDLE;
    if (VkResult res = vkCreateInstance ? vkCreateInstance(&ci, nullptr, &inst) : VK_ERROR_INITIALIZATION_FAILED;
        res != VK_SUCCESS) {
        r.error = std::format("vkCreateInstance failed ({})", int(res));
        ::dlclose(lib);
        return r;
    }
    r.loader = true;
    WC98_VK(inst, vkEnumeratePhysicalDevices);
    WC98_VK(inst, vkGetPhysicalDeviceProperties);
    WC98_VK(inst, vkGetPhysicalDeviceProperties2);
    WC98_VK(inst, vkDestroyInstance);
#undef WC98_VK
    std::uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(inst, &n, devs.data());
    for (auto d : devs) {
        VkPhysicalDeviceDriverProperties drv{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 p2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        vkGetPhysicalDeviceProperties(d, &p2.properties);
        bool hasDriver = vkGetPhysicalDeviceProperties2 && app.apiVersion >= VK_API_VERSION_1_1 &&
                         p2.properties.apiVersion >= VK_API_VERSION_1_2;
        if (hasDriver) {
            p2.pNext = &drv;
            vkGetPhysicalDeviceProperties2(d, &p2);
        }
        auto& p = p2.properties;
        GpuInfo g{p.deviceName,
                  std::string(typeName(p.deviceType)),
                  version(p.apiVersion),
                  "",
                  "",
                  driverVersion(p.vendorID, p.driverVersion),
                  ""};
        if (hasDriver) g.driverName = drv.driverName, g.driverInfo = drv.driverInfo;
        for (auto b : p.pipelineCacheUUID) g.uuid += std::format("{:02x}", b);
        r.gpus.push_back(std::move(g));
    }
    vkDestroyInstance(inst, nullptr);
    ::dlclose(lib);
    return r;
}

std::vector<std::string> openalDevices() {
    std::vector<std::string> r;
    const char* list = alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT")
                           ? alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER)
                           : alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
    for (const char* p = list; p && *p; p += std::string_view(p).size() + 1) r.emplace_back(p);
    return r;
}

std::string sdlVersion() {
    int v = SDL_GetVersion();
    return std::format("{}.{}.{} (compiled against {}.{}.{})", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
                       SDL_VERSIONNUM_MICRO(v), SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
}

fs::path findGame() {
    std::error_code ec;
    auto self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        auto p = self.parent_path() / "wc98-native";
        if (fs::is_regular_file(p, ec) && ::access(p.c_str(), X_OK) == 0) return p;
        p = self.parent_path().parent_path() / "app" / "wc98-native"; // CMake build tree
        if (fs::is_regular_file(p, ec) && ::access(p.c_str(), X_OK) == 0) return p;
    }
    const char* path = std::getenv("PATH");
    for (std::string_view rest = path ? path : ""; !rest.empty();) {
        auto c = rest.find(':');
        auto dir = rest.substr(0, c);
        rest = c == std::string_view::npos ? std::string_view{} : rest.substr(c + 1);
        if (dir.empty()) continue;
        auto p = fs::path(dir) / "wc98-native";
        if (fs::is_regular_file(p, ec) && ::access(p.c_str(), X_OK) == 0) return p;
    }
    return {};
}

std::string diagnostics() {
    std::string o = std::format("wc98-launcher {}\nSDL {}\n", WC98_VERSION, sdlVersion());
    auto vk = queryVulkan();
    o += std::format("Vulkan loader: {} (instance {})\n", vk.loader ? "ok" : vk.error, vk.instanceVersion);
    for (auto& g : vk.gpus)
        o += std::format("  GPU: {} [{}] Vulkan {} driver {} {} ({}) uuid {}\n", g.name, g.type, g.apiVersion,
                         g.driverName, g.driverVersion, g.driverInfo, g.uuid);
    if (vk.loader && vk.gpus.empty()) o += "  no Vulkan devices: install the Mesa (RADV/ANV) or NVIDIA Vulkan driver\n";
    auto al = openalDevices();
    o += std::format("OpenAL devices: {}\n", al.size());
    for (auto& d : al) o += "  " + d + "\n";

    auto p = paths();
    auto game = findGame();
    o += std::format("Game executable: {}\n",
                     game.empty() ? "NOT FOUND (expected next to the launcher or in PATH)" : game.string());
    o += std::format("Settings: {}\n", p.settings.string());
    try {
        auto r = config::load(p.settings);
        if (!r.existed) o += "  (not created yet; defaults in use)\n";
        for (auto& w : r.warnings) o += "  warning: " + w + "\n";
        auto& s = r.settings;
        o += std::format("  presentation {}, aspect {}, window {}, msaa {}, device '{}'\n",
                         config::name(s.graphics.presentation), config::name(s.graphics.aspect),
                         config::name(s.graphics.window), s.graphics.msaa, s.graphics.device);
        if (s.game.data.empty()) o += "  game data: NOT SET (Game files page)\n";
        for (auto& d : s.game.data) {
            auto det = assets::detect(d);
            o += std::format("  game data: {} -> {} ({})\n", d.string(), assets::layoutName(det.layout), det.detail);
            if (det.layout == assets::Layout::IsoImage || det.layout == assets::Layout::ZippedIso)
                o += "    the game needs a folder: extract the image with a standard tool and add that folder\n";
        }
        o += std::format("  gameplay options: {} key(s) (Match options page)\n", s.gameplay.size());
    } catch (const std::exception& e) {
        o += std::format("  error: {}\n", e.what());
    }
    o += std::format("Logs: {}\n", p.logs.string());
    return o;
}

} // namespace wc98::launcher
