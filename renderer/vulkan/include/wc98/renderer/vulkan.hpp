#pragma once
// Vulkan 1.3 backend (dynamic rendering + synchronization2).
#include "wc98/renderer/renderer.hpp"

#include <vulkan/vulkan.h>

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace wc98::render {

// No Vulkan driver or no physical device at all (tests treat this as "skip").
struct NoVulkanDevice : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct VulkanConfig {
    // Khronos validation + debug-utils messenger. Defaults to WC98_VK_VALIDATION=1 in the
    // environment. Requesting it without the layer installed is an error.
    bool validation = false;
    // Optional shared counter of validation warnings + errors; it outlives the renderer so
    // messages emitted during shutdown can be checked too.
    std::shared_ptr<std::atomic<std::uint32_t>> validationCounter;
    // Window integration; leave empty for offscreen-only use (no surface, no swapchain).
    std::vector<const char*> instanceExtensions;
    std::function<VkSurfaceKHR(VkInstance)> createSurface;
    std::function<std::pair<std::uint32_t, std::uint32_t>()> drawableSize;
};

[[nodiscard]] bool validationRequestedByEnv();
// Throws NoVulkanDevice, or std::runtime_error naming the missing feature/extension.
std::unique_ptr<Renderer> createVulkanRenderer(const VulkanConfig& cfg, const RenderSettings& settings = {});

} // namespace wc98::render
