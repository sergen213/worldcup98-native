// Vulkan 1.3 backend: dynamic rendering, synchronization2, one graphics+present queue.
//
// Frame structure (identical for window and offscreen output):
//   1. scene pass  -> internal colour target (MSAA + resolve in enhanced mode) + depth:
//                     opaque draws, back-to-front alpha draws, then the 2D UI batch
//   2. output      -> clear the output image black and blit the scene target into the
//                     Layout::dest rectangle (nearest in original mode: pixelated 640x480)
//   3. present the swapchain image, or copy the output image to a host buffer.
#include "wc98/renderer/vulkan.hpp"

#include "wc98/log.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <bit>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

namespace wc98::render {

bool validationRequestedByEnv() {
    const char* v = std::getenv("WC98_VK_VALIDATION");
    return v && std::string_view(v) == "1";
}

namespace {

const std::uint32_t kMeshVert[] = {
#include "mesh.vert.inc"
};
const std::uint32_t kSkinVert[] = {
#include "skin.vert.inc"
};
const std::uint32_t kUiVert[] = {
#include "ui.vert.inc"
};
const std::uint32_t kTexturedFrag[] = {
#include "textured.frag.inc"
};

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::format("{} failed: VkResult {}", what, static_cast<int>(r)));
}
#define VK(x) check((x), #x)

constexpr std::uint32_t kFramesInFlight = 2;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
// ponytail: fixed descriptor pool sized for 4096 live textures; grow into a pool list when exceeded.
constexpr std::uint32_t kMaxTextures = 4096;

struct Buffer {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

struct Img {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkExtent2D extent{};
    std::uint32_t mips = 1;
};

struct Texture {
    Img img;
    std::array<VkDescriptorSet, 2> sets{}; // [0] nearest (original), [1] linear+aniso (enhanced)
    std::string key;
    bool live = false;
};

struct Mesh {
    Buffer vb, ib, sb; // sb: SkinVertex stream for skinned meshes
    std::uint32_t indexCount = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT16;
    std::uint32_t maxJoint = 0; // highest joint index referenced by the skin
    std::string key;
    bool live = false;
};

struct PerFrame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    Buffer ubo, ui, world, background, joints;
    VkDescriptorSet uboSet = VK_NULL_HANDLE;
};

struct FrameUbo {
    glm::mat4 viewProj, uiProj, view;
    glm::vec4 fogColor, fogRange;
};
struct Push {
    glm::mat4 model;
    glm::vec4 params;     // x = alpha cutoff, y = colour scale
    glm::uvec4 iparams{}; // x = first joint matrix
    glm::vec4 light{};    // SkinVertex::light weights
};

enum class Kind { Opaque, Alpha, Ui, SkinOpaque, SkinAlpha, Background };

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    ++*static_cast<std::atomic<std::uint32_t>*>(user);
    log::write(severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? log::Level::Error : log::Level::Warn,
               std::format("vulkan validation: {}", data->pMessage));
    return VK_FALSE;
}

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    return std::ranges::any_of(list, [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

class VulkanRenderer final : public Renderer {
public:
    VulkanRenderer(const VulkanConfig& cfg, const RenderSettings& s)
        : settings_(s), drawableSize_(cfg.drawableSize),
          validationCount_(cfg.validationCounter ? cfg.validationCounter : std::make_shared<std::atomic<std::uint32_t>>(0)) {
        try {
            createInstance(cfg);
            if (cfg.createSurface) {
                surface_ = cfg.createSurface(instance_);
                if (!surface_) throw std::runtime_error("window surface creation failed");
            }
            pickDevice();
            createDevice();
            createObjects();
        } catch (...) {
            destroy();
            throw;
        }
    }
    ~VulkanRenderer() override { destroy(); }

    TextureId createTexture(const Image& img, Wrap wrap, std::string_view key) override;
    MeshId createMesh(const MeshData& mesh, std::string_view key) override;
    void destroyTexture(TextureId id) override;
    void destroyMesh(MeshId id) override;
    void setSettings(const RenderSettings& s) override { settings_ = s; }
    const RenderSettings& settings() const override { return settings_; }
    bool present(const Frame& frame) override;
    Image renderOffscreen(const Frame& frame, std::uint32_t width, std::uint32_t height) override;
    std::uint32_t validationMessageCount() const override { return validationCount_->load(); }
    std::string deviceName() const override { return props_.deviceName; }

private:
    // --- setup ---
    void createInstance(const VulkanConfig& cfg);
    void pickDevice();
    void createDevice();
    void createObjects();
    void destroy();
    bool recreateSwapchain();
    void destroySwapchain(VkSwapchainKHR sc);

    // --- resources ---
    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags props) const;
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void destroyBuffer(Buffer& b);
    Img createImage(std::uint32_t w, std::uint32_t h, std::uint32_t mips, VkSampleCountFlagBits samples, VkFormat fmt,
                    VkImageUsageFlags usage, VkImageAspectFlags viewAspect);
    void destroyImage(Img& i);
    template <class F> void immediate(F&& record);
    VkPipeline pipeline(Kind k, VkSampleCountFlagBits samples);
    void writeJointDescriptor(PerFrame& pf);
    void ensureTargets(std::uint32_t w, std::uint32_t h, VkSampleCountFlagBits samples);
    VkSampleCountFlagBits currentSamples() const {
        return settings_.mode == PresentMode::Enhanced && settings_.msaa ? maxSamples_ : VK_SAMPLE_COUNT_1_BIT;
    }

    // --- frame ---
    void recordFrame(PerFrame& pf, const Frame& f, VkImage out, VkExtent2D outExt, VkImageLayout finalLayout, const Buffer* readback);
    void beginLabel(VkCommandBuffer c, const char* name) const {
        if (!cmdBeginLabel_) return;
        VkDebugUtilsLabelEXT l{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        l.pLabelName = name;
        cmdBeginLabel_(c, &l);
    }
    void endLabel(VkCommandBuffer c) const {
        if (cmdEndLabel_) cmdEndLabel_(c);
    }

    RenderSettings settings_;
    std::function<std::pair<std::uint32_t, std::uint32_t>()> drawableSize_;
    std::shared_ptr<std::atomic<std::uint32_t>> validationCount_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    PFN_vkCmdBeginDebugUtilsLabelEXT cmdBeginLabel_ = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT cmdEndLabel_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props_{};
    VkPhysicalDeviceMemoryProperties memProps_{};
    bool anisotropy_ = false;
    std::uint32_t queueFamily_ = 0;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits maxSamples_ = VK_SAMPLE_COUNT_1_BIT;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent_{};
    std::vector<VkImage> swapImages_;
    std::vector<VkSemaphore> renderDone_; // one per swapchain image
    bool swapDirty_ = true;

    std::array<VkSampler, 4> samplers_{}; // [filter*2 + wrap], filter 0 nearest / 1 linear, wrap 0 repeat / 1 clamp
    VkDescriptorSetLayout uboLayout_ = VK_NULL_HANDLE, texLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkShaderModule meshVert_ = VK_NULL_HANDLE, skinVert_ = VK_NULL_HANDLE, uiVert_ = VK_NULL_HANDLE, frag_ = VK_NULL_HANDLE;
    std::unordered_map<std::uint32_t, VkPipeline> pipelines_;
    VkCommandPool uploadPool_ = VK_NULL_HANDLE;
    VkFence uploadFence_ = VK_NULL_HANDLE;
    std::array<PerFrame, kFramesInFlight> frames_{};
    std::uint32_t frameIndex_ = 0;

    struct Targets {
        Img color, resolve, depth;
        std::uint32_t w = 0, h = 0;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    } targets_;

    std::vector<Texture> textures_; // index = TextureId; slot 0 is the built-in white texture
    std::vector<Mesh> meshes_;      // index = MeshId; slot 0 unused
    std::unordered_map<std::string, TextureId> textureCache_;
    std::unordered_map<std::string, MeshId> meshCache_;
};

// ---------------------------------------------------------------- setup

void VulkanRenderer::createInstance(const VulkanConfig& cfg) {
    std::uint32_t version = 0;
    if (vkEnumerateInstanceVersion(&version) != VK_SUCCESS || version < VK_API_VERSION_1_3)
        throw std::runtime_error(std::format("Vulkan loader {}.{} found, 1.3 required", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version)));

    std::uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> available(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, available.data());

    std::vector<const char*> exts = cfg.instanceExtensions;
    // Debug utils also powers command-buffer labels for RenderDoc etc., so enable it when present.
    bool debugUtils = hasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (cfg.validation && !debugUtils) throw std::runtime_error("validation requested but VK_EXT_debug_utils is unavailable");
    if (debugUtils) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    const char* layer = "VK_LAYER_KHRONOS_validation";
    if (cfg.validation) {
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> layers(n);
        vkEnumerateInstanceLayerProperties(&n, layers.data());
        if (!std::ranges::any_of(layers, [&](const VkLayerProperties& l) { return std::strcmp(l.layerName, layer) == 0; }))
            throw std::runtime_error("validation requested but VK_LAYER_KHRONOS_validation is not installed");
    }

    VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    dbg.pfnUserCallback = debugCallback;
    dbg.pUserData = validationCount_.get();

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "wc98-native";
    app.pEngineName = "wc98";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pNext = cfg.validation ? &dbg : nullptr; // also reports instance creation/destruction issues
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = cfg.validation ? 1 : 0;
    ci.ppEnabledLayerNames = &layer;
    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r == VK_ERROR_INCOMPATIBLE_DRIVER) throw NoVulkanDevice("no Vulkan driver (VK_ERROR_INCOMPATIBLE_DRIVER)");
    check(r, "vkCreateInstance");

    if (debugUtils) {
        cmdBeginLabel_ = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance_, "vkCmdBeginDebugUtilsLabelEXT"));
        cmdEndLabel_ = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance_, "vkCmdEndDebugUtilsLabelEXT"));
    }
    if (cfg.validation) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        VK(create(instance_, &dbg, nullptr, &messenger_));
        log::info("vulkan: validation layer enabled");
    }
}

void VulkanRenderer::pickDevice() {
    std::uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    if (n == 0) throw NoVulkanDevice("no Vulkan physical device");
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance_, &n, devs.data());

    std::string rejected;
    int bestScore = -1;
    for (VkPhysicalDevice d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        auto reject = [&](std::string_view why) { rejected += std::format("\n  {}: {}", p.deviceName, why); };
        if (p.apiVersion < VK_API_VERSION_1_3) {
            reject("Vulkan 1.3 not supported");
            continue;
        }
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f13};
        vkGetPhysicalDeviceFeatures2(d, &f2);
        if (!f13.dynamicRendering || !f13.synchronization2) {
            reject("dynamicRendering/synchronization2 feature missing");
            continue;
        }
        if (surface_) {
            vkEnumerateDeviceExtensionProperties(d, nullptr, &n, nullptr);
            std::vector<VkExtensionProperties> exts(n);
            vkEnumerateDeviceExtensionProperties(d, nullptr, &n, exts.data());
            if (!hasExtension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
                reject("VK_KHR_swapchain missing");
                continue;
            }
        }
        vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
        std::vector<VkQueueFamilyProperties> qf(n);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &n, qf.data());
        std::optional<std::uint32_t> family;
        for (std::uint32_t i = 0; i < n && !family; ++i) {
            if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_TRUE;
            if (surface_) vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
            if (present) family = i;
        }
        if (!family) {
            reject("no queue family with graphics + present");
            continue;
        }
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 1000
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 500
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? 100
                                                                             : 10;
        if (score > bestScore) bestScore = score, phys_ = d, props_ = p, queueFamily_ = *family, anisotropy_ = f2.features.samplerAnisotropy;
    }
    if (!phys_) throw std::runtime_error("no suitable Vulkan 1.3 device (dynamicRendering + synchronization2 required):" + rejected);
    vkGetPhysicalDeviceMemoryProperties(phys_, &memProps_);
    log::info("vulkan: using {} (API {}.{}.{})", props_.deviceName, VK_API_VERSION_MAJOR(props_.apiVersion),
              VK_API_VERSION_MINOR(props_.apiVersion), VK_API_VERSION_PATCH(props_.apiVersion));
}

void VulkanRenderer::createDevice() {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    f13.shaderDemoteToHelperInvocation = VK_TRUE; // mandatory in 1.3; glslc emits demote for `discard`
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f13};
    f2.features.samplerAnisotropy = anisotropy_;
    const char* swapExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2};
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.enabledExtensionCount = surface_ ? 1 : 0;
    ci.ppEnabledExtensionNames = &swapExt;
    VK(vkCreateDevice(phys_, &ci, nullptr, &device_));
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    for (VkFormat f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(phys_, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat_ = f;
            break;
        }
    }
    if (depthFormat_ == VK_FORMAT_UNDEFINED) throw std::runtime_error("no depth-only attachment format");
    VkSampleCountFlags counts = props_.limits.framebufferColorSampleCounts & props_.limits.framebufferDepthSampleCounts;
    maxSamples_ = counts & VK_SAMPLE_COUNT_4_BIT ? VK_SAMPLE_COUNT_4_BIT : counts & VK_SAMPLE_COUNT_2_BIT ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;
}

void VulkanRenderer::createObjects() {
    // Samplers: nearest for the original look, trilinear + anisotropy for enhanced.
    for (int filter = 0; filter < 2; ++filter)
        for (int wrap = 0; wrap < 2; ++wrap) {
            VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            si.magFilter = si.minFilter = filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            si.mipmapMode = filter ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
            si.addressModeU = si.addressModeV = si.addressModeW = wrap ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
            si.anisotropyEnable = filter && anisotropy_;
            si.maxAnisotropy = si.anisotropyEnable ? std::min(16.0f, props_.limits.maxSamplerAnisotropy) : 1.0f;
            si.maxLod = VK_LOD_CLAMP_NONE;
            VK(vkCreateSampler(device_, &si, nullptr, &samplers_[filter * 2 + wrap]));
        }

    std::array<VkDescriptorSetLayoutBinding, 2> ub{{{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                                    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}}};
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = static_cast<std::uint32_t>(ub.size());
    lci.pBindings = ub.data();
    VK(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &uboLayout_));
    VkDescriptorSetLayoutBinding tb{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    lci.bindingCount = 1;
    lci.pBindings = &tb;
    VK(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &texLayout_));

    std::array<VkDescriptorSetLayout, 2> layouts{uboLayout_, texLayout_};
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 2;
    plci.pSetLayouts = layouts.data();
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VK(vkCreatePipelineLayout(device_, &plci, nullptr, &pipeLayout_));

    std::array<VkDescriptorPoolSize, 3> sizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * kMaxTextures},
                                               {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
                                               {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight}}};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = 2 * kMaxTextures + kFramesInFlight;
    dpci.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    dpci.pPoolSizes = sizes.data();
    VK(vkCreateDescriptorPool(device_, &dpci, nullptr, &descPool_));

    auto module = [&](const std::uint32_t* code, std::size_t bytes) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = bytes;
        ci.pCode = code;
        VkShaderModule m;
        VK(vkCreateShaderModule(device_, &ci, nullptr, &m));
        return m;
    };
    meshVert_ = module(kMeshVert, sizeof(kMeshVert));
    skinVert_ = module(kSkinVert, sizeof(kSkinVert));
    uiVert_ = module(kUiVert, sizeof(kUiVert));
    frag_ = module(kTexturedFrag, sizeof(kTexturedFrag));

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cpci.queueFamilyIndex = queueFamily_;
    VK(vkCreateCommandPool(device_, &cpci, nullptr, &uploadPool_));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK(vkCreateFence(device_, &fci, nullptr, &uploadFence_));

    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (PerFrame& pf : frames_) {
        VK(vkCreateCommandPool(device_, &cpci, nullptr, &pf.pool));
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pf.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK(vkAllocateCommandBuffers(device_, &ai, &pf.cmd));
        VK(vkCreateFence(device_, &fci, nullptr, &pf.fence));
        VK(vkCreateSemaphore(device_, &sci, nullptr, &pf.acquired));
        pf.ubo = createBuffer(sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descPool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &uboLayout_;
        VK(vkAllocateDescriptorSets(device_, &dai, &pf.uboSet));
        VkDescriptorBufferInfo bi{pf.ubo.buf, 0, sizeof(FrameUbo)};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = pf.uboSet;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
        pf.joints = createBuffer(256 * sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        writeJointDescriptor(pf);
    }

    meshes_.emplace_back(); // MeshId 0 = invalid
    createTexture(Image{1, 1, {255, 255, 255, 255}}, Wrap::Repeat, {}); // TextureId 0 = white
}

void VulkanRenderer::destroy() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        for (Texture& t : textures_)
            if (t.live) destroyImage(t.img); // descriptor sets die with the pool
        for (Mesh& m : meshes_)
            if (m.live) destroyBuffer(m.vb), destroyBuffer(m.ib), destroyBuffer(m.sb);
        destroyImage(targets_.color), destroyImage(targets_.resolve), destroyImage(targets_.depth);
        destroySwapchain(swapchain_);
        for (PerFrame& pf : frames_) {
            destroyBuffer(pf.ubo), destroyBuffer(pf.ui), destroyBuffer(pf.world), destroyBuffer(pf.background), destroyBuffer(pf.joints);
            vkDestroySemaphore(device_, pf.acquired, nullptr);
            vkDestroyFence(device_, pf.fence, nullptr);
            vkDestroyCommandPool(device_, pf.pool, nullptr);
        }
        vkDestroyFence(device_, uploadFence_, nullptr);
        vkDestroyCommandPool(device_, uploadPool_, nullptr);
        for (auto& [k, p] : pipelines_) vkDestroyPipeline(device_, p, nullptr);
        for (VkShaderModule m : {meshVert_, skinVert_, uiVert_, frag_}) vkDestroyShaderModule(device_, m, nullptr);
        vkDestroyDescriptorPool(device_, descPool_, nullptr);
        vkDestroyPipelineLayout(device_, pipeLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, uboLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, texLayout_, nullptr);
        for (VkSampler s : samplers_) vkDestroySampler(device_, s, nullptr);
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (messenger_) {
            auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
            destroyMessenger(instance_, messenger_, nullptr);
        }
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroySwapchain(VkSwapchainKHR sc) {
    for (VkSemaphore s : renderDone_) vkDestroySemaphore(device_, s, nullptr);
    renderDone_.clear();
    if (sc) vkDestroySwapchainKHR(device_, sc, nullptr);
}

bool VulkanRenderer::recreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps));
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) { // surface size follows the swapchain (Wayland)
        auto [w, h] = drawableSize_ ? drawableSize_() : std::pair<std::uint32_t, std::uint32_t>{640, 480};
        extent.width = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return false; // minimised
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) throw std::runtime_error("swapchain images cannot be transfer destinations");

    if (swapFormat_ == VK_FORMAT_UNDEFINED) {
        std::uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &n, formats.data());
        // UNORM on purpose: the 1998 game blended in gamma space, so no sRGB conversion anywhere.
        for (VkFormat want : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_A2B10G10R10_UNORM_PACK32})
            for (const VkSurfaceFormatKHR& f : formats) {
                VkFormatProperties fp;
                vkGetPhysicalDeviceFormatProperties(phys_, f.format, &fp);
                if (swapFormat_ == VK_FORMAT_UNDEFINED && f.format == want && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                    (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
                    swapFormat_ = f.format;
            }
        if (swapFormat_ == VK_FORMAT_UNDEFINED) throw std::runtime_error("no UNORM blit-capable surface format");
    }

    VkSwapchainKHR old = swapchain_;
    vkDeviceWaitIdle(device_);
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = caps.maxImageCount ? std::min(caps.minImageCount + 1, caps.maxImageCount) : caps.minImageCount + 1;
    ci.imageFormat = swapFormat_;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                            : static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available; vsync
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    VK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_));
    destroySwapchain(old);

    std::uint32_t n = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    swapImages_.resize(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapImages_.data());
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderDone_.resize(n);
    for (VkSemaphore& s : renderDone_) VK(vkCreateSemaphore(device_, &sci, nullptr, &s));
    swapExtent_ = extent;
    swapDirty_ = false;
    log::info("vulkan: swapchain {}x{}, {} images", extent.width, extent.height, n);
    return true;
}

// ---------------------------------------------------------------- resources

std::uint32_t VulkanRenderer::memoryType(std::uint32_t bits, VkMemoryPropertyFlags want) const {
    for (std::uint32_t i = 0; i < memProps_.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & want) == want) return i;
    throw std::runtime_error("no suitable Vulkan memory type");
}

// ponytail: one VkDeviceMemory per resource. Fine for the few hundred textures/meshes a match
// uses; maxMemoryAllocationCount is >= 4096. Switch to a sub-allocator (or VMA) past that.
Buffer VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = usage;
    VK(vkCreateBuffer(device_, &ci, nullptr, &b.buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, b.buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                                    : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(device_, &ai, nullptr, &b.mem));
    VK(vkBindBufferMemory(device_, b.buf, b.mem, 0));
    if (hostVisible) VK(vkMapMemory(device_, b.mem, 0, VK_WHOLE_SIZE, 0, &b.mapped));
    return b;
}

void VulkanRenderer::destroyBuffer(Buffer& b) {
    if (b.buf) vkDestroyBuffer(device_, b.buf, nullptr);
    if (b.mem) vkFreeMemory(device_, b.mem, nullptr);
    b = {};
}

Img VulkanRenderer::createImage(std::uint32_t w, std::uint32_t h, std::uint32_t mips, VkSampleCountFlagBits samples, VkFormat fmt,
                                VkImageUsageFlags usage, VkImageAspectFlags viewAspect) {
    Img i;
    i.extent = {w, h};
    i.mips = mips;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {w, h, 1};
    ci.mipLevels = mips;
    ci.arrayLayers = 1;
    ci.samples = samples;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK(vkCreateImage(device_, &ci, nullptr, &i.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, i.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(device_, &ai, nullptr, &i.mem));
    VK(vkBindImageMemory(device_, i.image, i.mem, 0));
    if (viewAspect) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = i.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt;
        vci.subresourceRange = {viewAspect, 0, mips, 0, 1};
        VK(vkCreateImageView(device_, &vci, nullptr, &i.view));
    }
    return i;
}

void VulkanRenderer::destroyImage(Img& i) {
    if (i.view) vkDestroyImageView(device_, i.view, nullptr);
    if (i.image) vkDestroyImage(device_, i.image, nullptr);
    if (i.mem) vkFreeMemory(device_, i.mem, nullptr);
    i = {};
}

void barrier(VkCommandBuffer cmd, std::span<const VkImageMemoryBarrier2> images, std::span<const VkMemoryBarrier2> memory = {}) {
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = static_cast<std::uint32_t>(memory.size());
    di.pMemoryBarriers = memory.data();
    di.imageMemoryBarrierCount = static_cast<std::uint32_t>(images.size());
    di.pImageMemoryBarriers = images.data();
    vkCmdPipelineBarrier2(cmd, &di);
}

VkImageMemoryBarrier2 imageBarrier(VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                   std::uint32_t mip = 0, std::uint32_t mipCount = VK_REMAINING_MIP_LEVELS) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {aspect, mip, mipCount, 0, 1};
    return b;
}

template <class F> void VulkanRenderer::immediate(F&& record) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = uploadPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK(vkAllocateCommandBuffers(device_, &ai, &cmd));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK(vkBeginCommandBuffer(cmd, &bi));
    beginLabel(cmd, "upload");
    record(cmd);
    endLabel(cmd);
    VK(vkEndCommandBuffer(cmd));
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = cmd;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    VK(vkQueueSubmit2(queue_, 1, &si, uploadFence_));
    VK(vkWaitForFences(device_, 1, &uploadFence_, VK_TRUE, UINT64_MAX));
    VK(vkResetFences(device_, 1, &uploadFence_));
    vkFreeCommandBuffers(device_, uploadPool_, 1, &cmd);
}

TextureId VulkanRenderer::createTexture(const Image& img, Wrap wrap, std::string_view key) {
    if (!key.empty())
        if (auto it = textureCache_.find(std::string(key)); it != textureCache_.end()) return it->second;
    if (img.width == 0 || img.height == 0 || img.rgba.size() != std::size_t(img.width) * img.height * 4)
        throw std::invalid_argument("createTexture: RGBA8 size mismatch");
    if (img.width > props_.limits.maxImageDimension2D || img.height > props_.limits.maxImageDimension2D)
        throw std::invalid_argument("createTexture: image too large for device");

    Texture t;
    t.key = key;
    t.live = true;
    auto mips = static_cast<std::uint32_t>(std::bit_width(std::max(img.width, img.height)));
    t.img = createImage(img.width, img.height, mips, VK_SAMPLE_COUNT_1_BIT, kColorFormat,
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Buffer staging = createBuffer(img.rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, img.rgba.data(), img.rgba.size());

    immediate([&](VkCommandBuffer cmd) {
        constexpr auto T = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, FS = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        constexpr auto DST = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, SRC = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, RO = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkImage im = t.img.image;
        VkImageMemoryBarrier2 b = imageBarrier(im, VK_IMAGE_LAYOUT_UNDEFINED, DST, VK_PIPELINE_STAGE_2_NONE, 0, T, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        barrier(cmd, {&b, 1});
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {img.width, img.height, 1};
        vkCmdCopyBufferToImage(cmd, staging.buf, im, DST, 1, &copy);
        // Mip chain by successive linear blits; each finished level moves to shader-read.
        auto w = static_cast<std::int32_t>(img.width), h = static_cast<std::int32_t>(img.height);
        for (std::uint32_t m = 1; m < mips; ++m) {
            b = imageBarrier(im, DST, SRC, T, VK_ACCESS_2_TRANSFER_WRITE_BIT, T, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1);
            barrier(cmd, {&b, 1});
            std::int32_t nw = std::max(w / 2, 1), nh = std::max(h / 2, 1);
            VkImageBlit2 region{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, 1};
            region.srcOffsets[1] = {w, h, 1};
            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1};
            region.dstOffsets[1] = {nw, nh, 1};
            VkBlitImageInfo2 bi{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
            bi.srcImage = bi.dstImage = im;
            bi.srcImageLayout = SRC;
            bi.dstImageLayout = DST;
            bi.regionCount = 1;
            bi.pRegions = &region;
            bi.filter = VK_FILTER_LINEAR;
            vkCmdBlitImage2(cmd, &bi);
            b = imageBarrier(im, SRC, RO, T, 0, FS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1);
            barrier(cmd, {&b, 1});
            w = nw, h = nh;
        }
        b = imageBarrier(im, DST, RO, T, VK_ACCESS_2_TRANSFER_WRITE_BIT, FS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, mips - 1, 1);
        barrier(cmd, {&b, 1});
    });
    destroyBuffer(staging);

    std::array<VkDescriptorSetLayout, 2> layouts{texLayout_, texLayout_};
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descPool_;
    dai.descriptorSetCount = 2;
    dai.pSetLayouts = layouts.data();
    VK(vkAllocateDescriptorSets(device_, &dai, t.sets.data()));
    for (int filter = 0; filter < 2; ++filter) {
        VkDescriptorImageInfo ii{samplers_[filter * 2 + (wrap == Wrap::Clamp)], t.img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = t.sets[filter];
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii;
        vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    }

    auto slot = std::ranges::find_if(textures_, [](const Texture& x) { return !x.live; });
    auto id = static_cast<TextureId>(slot - textures_.begin());
    if (slot == textures_.end()) textures_.push_back(std::move(t));
    else *slot = std::move(t);
    if (!key.empty()) textureCache_.emplace(key, id);
    return id;
}

MeshId VulkanRenderer::createMesh(const MeshData& mesh, std::string_view key) {
    if (!key.empty())
        if (auto it = meshCache_.find(std::string(key)); it != meshCache_.end()) return it->second;
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0) throw std::invalid_argument("createMesh: empty or non-triangle mesh");
    if (std::ranges::max(mesh.indices) >= mesh.vertices.size()) throw std::invalid_argument("createMesh: index out of range");
    if (!mesh.skin.empty() && mesh.skin.size() != mesh.vertices.size()) throw std::invalid_argument("createMesh: skin/vertex count mismatch");

    Mesh m;
    m.key = key;
    m.live = true;
    m.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    bool small = mesh.vertices.size() <= 0x10000;
    m.indexType = small ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    VkDeviceSize vbytes = mesh.vertices.size() * sizeof(Vertex), ibytes = mesh.indices.size() * (small ? 2 : 4);
    VkDeviceSize sbytes = mesh.skin.size() * sizeof(SkinVertex);
    for (const SkinVertex& s : mesh.skin)
        for (int i = 0; i < 3; ++i)
            if (s.weight[i] != 0) m.maxJoint = std::max(m.maxJoint, (s.joints >> (8 * i)) & 0xFFu);
    Buffer staging = createBuffer(vbytes + ibytes + sbytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    auto* dst = static_cast<std::uint8_t*>(staging.mapped);
    std::memcpy(dst, mesh.vertices.data(), vbytes);
    if (sbytes) std::memcpy(dst + vbytes + ibytes, mesh.skin.data(), sbytes);
    if (small)
        for (std::size_t i = 0; i < mesh.indices.size(); ++i) reinterpret_cast<std::uint16_t*>(dst + vbytes)[i] = static_cast<std::uint16_t>(mesh.indices[i]);
    else
        std::memcpy(dst + vbytes, mesh.indices.data(), ibytes);
    m.vb = createBuffer(vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    m.ib = createBuffer(ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    if (sbytes) m.sb = createBuffer(sbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    immediate([&](VkCommandBuffer cmd) {
        VkBufferCopy vc{0, 0, vbytes}, ic{vbytes, 0, ibytes}, sc{vbytes + ibytes, 0, sbytes};
        vkCmdCopyBuffer(cmd, staging.buf, m.vb.buf, 1, &vc);
        vkCmdCopyBuffer(cmd, staging.buf, m.ib.buf, 1, &ic);
        if (sbytes) vkCmdCopyBuffer(cmd, staging.buf, m.sb.buf, 1, &sc);
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        mb.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
        barrier(cmd, {}, {&mb, 1});
    });
    destroyBuffer(staging);

    auto slot = std::ranges::find_if(meshes_.begin() + 1, meshes_.end(), [](const Mesh& x) { return !x.live; });
    auto id = static_cast<MeshId>(slot - meshes_.begin());
    if (slot == meshes_.end()) meshes_.push_back(std::move(m));
    else *slot = std::move(m);
    if (!key.empty()) meshCache_.emplace(key, id);
    return id;
}

// ponytail: destruction waits for the device to go idle; add a per-frame deferred-delete
// queue if resources ever get destroyed every frame.
void VulkanRenderer::destroyTexture(TextureId id) {
    if (id == 0 || id >= textures_.size() || !textures_[id].live) throw std::invalid_argument("destroyTexture: invalid id");
    vkDeviceWaitIdle(device_);
    Texture& t = textures_[id];
    VK(vkFreeDescriptorSets(device_, descPool_, 2, t.sets.data()));
    destroyImage(t.img);
    if (!t.key.empty()) textureCache_.erase(t.key);
    t = {};
}

void VulkanRenderer::destroyMesh(MeshId id) {
    if (id == 0 || id >= meshes_.size() || !meshes_[id].live) throw std::invalid_argument("destroyMesh: invalid id");
    vkDeviceWaitIdle(device_);
    Mesh& m = meshes_[id];
    destroyBuffer(m.vb), destroyBuffer(m.ib), destroyBuffer(m.sb);
    if (!m.key.empty()) meshCache_.erase(m.key);
    m = {};
}

VkPipeline VulkanRenderer::pipeline(Kind k, VkSampleCountFlagBits samples) {
    std::uint32_t key = static_cast<std::uint32_t>(k) << 8 | samples;
    if (auto it = pipelines_.find(key); it != pipelines_.end()) return it->second;
    const bool skin = k == Kind::SkinOpaque || k == Kind::SkinAlpha;
    const bool opaque = k == Kind::Opaque || k == Kind::SkinOpaque;

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                 k == Kind::Ui ? uiVert_ : skin ? skinVert_ : meshVert_, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag_, "main", nullptr};
    std::array<VkVertexInputBindingDescription, 2> bindings{{{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
                                                             {1, sizeof(SkinVertex), VK_VERTEX_INPUT_RATE_VERTEX}}};
    // The normal (location 1) stays in the vertex layout but no shader reads it (everything is prelit).
    std::vector<VkVertexInputAttributeDescription> attrs{{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
                                                         {3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, rgba)}};
    if (skin) {
        for (std::uint32_t i = 0; i < 3; ++i) attrs.push_back({4 + i, 1, VK_FORMAT_R32G32B32_SFLOAT, std::uint32_t(offsetof(SkinVertex, pos) + i * sizeof(glm::vec3))});
        attrs.push_back({7, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SkinVertex, weight)});
        attrs.push_back({8, 1, VK_FORMAT_R8G8B8A8_UINT, offsetof(SkinVertex, joints)});
        for (std::uint32_t i = 0; i < 4; ++i) attrs.push_back({9 + i, 1, VK_FORMAT_R8G8B8A8_UNORM, std::uint32_t(offsetof(SkinVertex, light) + i * 4)});
    } else {
        attrs.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)});
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = skin ? 2 : 1;
    vi.pVertexBindingDescriptions = bindings.data();
    vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT; // dynamic
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = samples;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = k != Kind::Ui && k != Kind::Background;
    ds.depthWriteEnable = opaque && k != Kind::Background;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cb{};
    cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (!opaque) {
        cb.blendEnable = VK_TRUE;
        cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cb.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cb.colorBlendOp = cb.alphaBlendOp = VK_BLEND_OP_ADD;
        cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbs.attachmentCount = 1;
    cbs.pAttachments = &cb;
    std::array<VkDynamicState, 4> dyn{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = static_cast<std::uint32_t>(dyn.size());
    dy.pDynamicStates = dyn.data();
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &kColorFormat;
    ri.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &ri};
    ci.stageCount = static_cast<std::uint32_t>(stages.size());
    ci.pStages = stages.data();
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cbs;
    ci.pDynamicState = &dy;
    ci.layout = pipeLayout_;
    VkPipeline p;
    VK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &p));
    pipelines_.emplace(key, p);
    return p;
}

void VulkanRenderer::writeJointDescriptor(PerFrame& pf) {
    VkDescriptorBufferInfo bi{pf.joints.buf, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = pf.uboSet;
    w.dstBinding = 1;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

void VulkanRenderer::ensureTargets(std::uint32_t w, std::uint32_t h, VkSampleCountFlagBits samples) {
    if (targets_.w == w && targets_.h == h && targets_.samples == samples) return;
    vkDeviceWaitIdle(device_);
    destroyImage(targets_.color), destroyImage(targets_.resolve), destroyImage(targets_.depth);
    bool msaa = samples != VK_SAMPLE_COUNT_1_BIT;
    targets_.color = createImage(w, h, 1, samples, kColorFormat,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (msaa ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
                                 VK_IMAGE_ASPECT_COLOR_BIT);
    if (msaa)
        targets_.resolve = createImage(w, h, 1, VK_SAMPLE_COUNT_1_BIT, kColorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                       VK_IMAGE_ASPECT_COLOR_BIT);
    targets_.depth = createImage(w, h, 1, samples, depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                                 VK_IMAGE_ASPECT_DEPTH_BIT);
    targets_.w = w, targets_.h = h, targets_.samples = samples;
}

// ---------------------------------------------------------------- frame

void VulkanRenderer::recordFrame(PerFrame& pf, const Frame& f, VkImage out, VkExtent2D outExt, VkImageLayout finalLayout, const Buffer* readback) {
    for (const DrawItem& d : f.draws) {
        if (d.mesh == 0 || d.mesh >= meshes_.size() || !meshes_[d.mesh].live) throw std::invalid_argument("draw: invalid mesh id");
        if (d.material.texture >= textures_.size() || !textures_[d.material.texture].live) throw std::invalid_argument("draw: invalid texture id");
        const Mesh& m = meshes_[d.mesh];
        if (m.sb.buf && std::size_t(d.jointOffset) + m.maxJoint >= f.joints.size()) throw std::invalid_argument("draw: skinned mesh joints outside Frame::joints");
    }
    for (const WorldBatch* wb : {f.world, f.background})
        for (const WorldBatch::Run& r : wb ? wb->runs() : std::span<const WorldBatch::Run>{})
            if (r.material.texture >= textures_.size() || !textures_[r.material.texture].live) throw std::invalid_argument("world batch: invalid texture id");
    const Layout layout = computeLayout(settings_, outExt.width, outExt.height);
    const VkSampleCountFlagBits samples = currentSamples();
    ensureTargets(layout.sceneW, layout.sceneH, samples);
    const int filter = settings_.mode == PresentMode::Enhanced ? 1 : 0;

    FrameUbo ubo;
    const glm::mat4 flipY = glm::scale(glm::mat4(1.0f), glm::vec3(1, -1, 1)); // GL-style clip -> Vulkan (+Y down)
    ubo.viewProj = flipY * f.camera.projection(float(layout.sceneW) / float(layout.sceneH)) * f.camera.view();
    ubo.uiProj = glm::orthoRH_ZO(0.0f, SpriteBatch::kWidth, 0.0f, SpriteBatch::kHeight, -1.0f, 1.0f); // y=0 -> top
    ubo.view = f.camera.view();
    const bool fog = f.fog.end > f.fog.start;
    ubo.fogColor = glm::vec4(f.fog.color, fog ? 1.0f : 0.0f);
    ubo.fogRange = glm::vec4(f.fog.start, f.fog.end, 0, 0);
    std::memcpy(pf.ubo.mapped, &ubo, sizeof ubo);

    // Host-visible per-frame streams; this frame's fence was waited on, so its buffers are idle.
    auto stream = [&](Buffer& b, std::size_t bytes, VkBufferUsageFlags usage, const void* src) {
        if (bytes > b.size) {
            destroyBuffer(b);
            b = createBuffer(std::max<VkDeviceSize>(bytes * 2, 64 * 1024), usage, true);
            if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) writeJointDescriptor(pf);
        }
        if (bytes) std::memcpy(b.mapped, src, bytes);
    };
    std::span<const Vertex> uiVerts = f.ui ? f.ui->vertices() : std::span<const Vertex>{};
    std::span<const Vertex> worldVerts = f.world ? f.world->vertices() : std::span<const Vertex>{};
    std::span<const Vertex> bgVerts = f.background ? f.background->vertices() : std::span<const Vertex>{};
    stream(pf.ui, uiVerts.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, uiVerts.data());
    stream(pf.world, worldVerts.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, worldVerts.data());
    stream(pf.background, bgVerts.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, bgVerts.data());
    stream(pf.joints, f.joints.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, f.joints.data());

    // Opaque in submission order, then alpha-blended back to front.
    std::vector<const DrawItem*> order;
    order.reserve(f.draws.size());
    for (const DrawItem& d : f.draws)
        if (d.material.blend == Blend::Opaque) order.push_back(&d);
    auto firstAlpha = order.size();
    for (const DrawItem& d : f.draws)
        if (d.material.blend == Blend::Alpha) order.push_back(&d);
    auto dist2 = [&](const DrawItem* d) { glm::vec3 p(d->model[3]); return glm::dot(p - f.camera.eye, p - f.camera.eye); };
    std::stable_sort(order.begin() + firstAlpha, order.end(), [&](auto* a, auto* b) { return dist2(a) > dist2(b); });

    VK(vkResetCommandPool(device_, pf.pool, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkCommandBuffer cmd = pf.cmd;
    VK(vkBeginCommandBuffer(cmd, &bi));

    constexpr auto ALL = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, T = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    constexpr auto COLOR_OUT = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    constexpr auto CA = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    const bool msaa = samples != VK_SAMPLE_COUNT_1_BIT;
    const Img& sceneOut = msaa ? targets_.resolve : targets_.color;

    beginLabel(cmd, "scene");
    {
        // Targets are fully overwritten each frame; the previous frame's reads/writes finish first.
        std::vector<VkImageMemoryBarrier2> bs{
            imageBarrier(targets_.color.image, VK_IMAGE_LAYOUT_UNDEFINED, CA, ALL, VK_ACCESS_2_MEMORY_WRITE_BIT, COLOR_OUT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT),
            imageBarrier(targets_.depth.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, ALL, VK_ACCESS_2_MEMORY_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)};
        if (msaa)
            bs.push_back(imageBarrier(targets_.resolve.image, VK_IMAGE_LAYOUT_UNDEFINED, CA, ALL, VK_ACCESS_2_MEMORY_WRITE_BIT, COLOR_OUT,
                                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT));
        barrier(cmd, bs);
    }
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = targets_.color.view;
    color.imageLayout = CA;
    if (msaa) {
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView = targets_.resolve.view;
        color.resolveImageLayout = CA;
    }
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{f.clearColor.r, f.clearColor.g, f.clearColor.b, f.clearColor.a}};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = targets_.depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {layout.sceneW, layout.sceneH}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    ri.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &ri);

    VkViewport vp{0, 0, float(layout.sceneW), float(layout.sceneH), 0, 1};
    VkRect2D sc{{0, 0}, {layout.sceneW, layout.sceneH}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    // Both clip conventions end in the same Y flip, so a left-handed view mirrors screen winding.
    vkCmdSetFrontFace(cmd, f.camera.leftHanded ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &pf.uboSet, 0, nullptr);
    if (!bgVerts.empty()) {
        beginLabel(cmd, "background");
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &pf.background.buf, &off);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline(Kind::Background, samples));
        vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
        for (const WorldBatch::Run& r : f.background->runs()) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 1, 1, &textures_[r.material.texture].sets[filter], 0, nullptr);
            Push push{glm::mat4(1.0f), {0, r.material.colorScale, 0, 0}};
            vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
            vkCmdDraw(cmd, r.vertexCount, 1, r.firstVertex, 0);
        }
        endLabel(cmd);
    }
    VkPipeline bound = VK_NULL_HANDLE;
    for (const DrawItem* d : order) {
        const Mesh& m = meshes_[d->mesh];
        const bool skinned = m.sb.buf != VK_NULL_HANDLE, opaque = d->material.blend == Blend::Opaque;
        VkPipeline p = pipeline(skinned ? (opaque ? Kind::SkinOpaque : Kind::SkinAlpha) : (opaque ? Kind::Opaque : Kind::Alpha), samples);
        if (p != bound) vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bound = p);
        vkCmdSetCullMode(cmd, d->material.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 1, 1, &textures_[d->material.texture].sets[filter], 0, nullptr);
        Push push{d->model, {d->material.alphaCutoff, d->material.colorScale, 0, 0}, {d->jointOffset, 0, 0, 0}, d->lightBlend};
        vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
        std::array<VkBuffer, 2> vbs{m.vb.buf, m.sb.buf};
        std::array<VkDeviceSize, 2> offs{0, 0};
        vkCmdBindVertexBuffers(cmd, 0, skinned ? 2 : 1, vbs.data(), offs.data());
        vkCmdBindIndexBuffer(cmd, m.ib.buf, 0, m.indexType);
        vkCmdDrawIndexed(cmd, m.indexCount, 1, 0, 0, 0);
    }
    if (!worldVerts.empty()) {
        beginLabel(cmd, "world");
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &pf.world.buf, &off);
        for (const WorldBatch::Run& r : f.world->runs()) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline(r.material.blend == Blend::Opaque ? Kind::Opaque : Kind::Alpha, samples));
            vkCmdSetCullMode(cmd, r.material.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 1, 1, &textures_[r.material.texture].sets[filter], 0, nullptr);
            Push push{glm::mat4(1.0f), {r.material.alphaCutoff, r.material.colorScale, 0, 0}};
            vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
            vkCmdDraw(cmd, r.vertexCount, 1, r.firstVertex, 0);
        }
        endLabel(cmd);
    }
    if (!uiVerts.empty()) {
        beginLabel(cmd, "ui");
        VkViewport uvp{float(layout.ui.x), float(layout.ui.y), float(layout.ui.w), float(layout.ui.h), 0, 1};
        VkRect2D usc{{layout.ui.x, layout.ui.y}, {layout.ui.w, layout.ui.h}};
        vkCmdSetViewport(cmd, 0, 1, &uvp);
        vkCmdSetScissor(cmd, 0, 1, &usc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline(Kind::Ui, samples));
        vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
        Push push{glm::mat4(1.0f), {0, 1, 0, 0}};
        vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &pf.ui.buf, &off);
        for (const SpriteBatch::Run& r : f.ui->runs()) {
            if (r.texture >= textures_.size() || !textures_[r.texture].live) throw std::invalid_argument("sprite: invalid texture id");
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 1, 1, &textures_[r.texture].sets[filter], 0, nullptr);
            vkCmdDraw(cmd, r.vertexCount, 1, r.firstVertex, 0);
        }
        endLabel(cmd);
    }
    vkCmdEndRendering(cmd);
    endLabel(cmd);

    beginLabel(cmd, "output");
    {
        std::array bs{imageBarrier(sceneOut.image, CA, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, COLOR_OUT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, T,
                                   VK_ACCESS_2_TRANSFER_READ_BIT),
                      imageBarrier(out, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ALL, 0, T, VK_ACCESS_2_TRANSFER_WRITE_BIT)};
        barrier(cmd, bs);
    }
    VkClearColorValue black{{0, 0, 0, 1}};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, out, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    {
        VkImageMemoryBarrier2 b = imageBarrier(out, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, T,
                                               VK_ACCESS_2_TRANSFER_WRITE_BIT, T, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        barrier(cmd, {&b, 1});
    }
    VkImageBlit2 region{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
    region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {std::int32_t(layout.sceneW), std::int32_t(layout.sceneH), 1};
    region.dstOffsets[0] = {layout.dest.x, layout.dest.y, 0};
    region.dstOffsets[1] = {layout.dest.x + std::int32_t(layout.dest.w), layout.dest.y + std::int32_t(layout.dest.h), 1};
    VkBlitImageInfo2 blit{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
    blit.srcImage = sceneOut.image;
    blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blit.dstImage = out;
    blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blit.regionCount = 1;
    blit.pRegions = &region;
    blit.filter = filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; // original: chunky 640x480 pixels
    vkCmdBlitImage2(cmd, &blit);
    {
        VkImageMemoryBarrier2 b = imageBarrier(out, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout, T, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                               readback ? T : VK_PIPELINE_STAGE_2_NONE, readback ? VK_ACCESS_2_TRANSFER_READ_BIT : 0);
        barrier(cmd, {&b, 1});
    }
    if (readback) {
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {outExt.width, outExt.height, 1};
        vkCmdCopyImageToBuffer(cmd, out, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->buf, 1, &copy);
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mb.srcStageMask = T;
        mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        mb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        barrier(cmd, {}, {&mb, 1});
    }
    endLabel(cmd);
    VK(vkEndCommandBuffer(cmd));
}

bool VulkanRenderer::present(const Frame& f) {
    if (!surface_) throw std::logic_error("present() on a renderer created without a surface");
    if (drawableSize_) {
        auto [w, h] = drawableSize_();
        if (w == 0 || h == 0) return false;
        if (w != swapExtent_.width || h != swapExtent_.height) swapDirty_ = true;
    }
    if (swapDirty_ && !recreateSwapchain()) return false;

    PerFrame& pf = frames_[frameIndex_];
    VK(vkWaitForFences(device_, 1, &pf.fence, VK_TRUE, UINT64_MAX));
    std::uint32_t image = 0;
    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, pf.acquired, VK_NULL_HANDLE, &image);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        swapDirty_ = true;
        return false;
    }
    if (r == VK_SUBOPTIMAL_KHR) swapDirty_ = true; // still usable: present it, recreate next frame
    else check(r, "vkAcquireNextImageKHR");
    VK(vkResetFences(device_, 1, &pf.fence));

    recordFrame(pf, f, swapImages_[image], swapExtent_, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, nullptr);

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = pf.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = renderDone_[image];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = pf.cmd;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = 1;
    si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos = &signal;
    VK(vkQueueSubmit2(queue_, 1, &si, pf.fence));

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderDone_[image];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &image;
    r = vkQueuePresentKHR(queue_, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) swapDirty_ = true;
    else check(r, "vkQueuePresentKHR");
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
    return true;
}

Image VulkanRenderer::renderOffscreen(const Frame& f, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || width > props_.limits.maxImageDimension2D || height > props_.limits.maxImageDimension2D)
        throw std::invalid_argument("renderOffscreen: bad size");
    PerFrame& pf = frames_[frameIndex_];
    VK(vkWaitForFences(device_, 1, &pf.fence, VK_TRUE, UINT64_MAX));
    VK(vkResetFences(device_, 1, &pf.fence));

    Img out = createImage(width, height, 1, VK_SAMPLE_COUNT_1_BIT, kColorFormat, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0);
    Buffer rb = createBuffer(VkDeviceSize(width) * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    Image result{width, height, {}};
    try {
        recordFrame(pf, f, out.image, {width, height}, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, &rb);
        VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cbi.commandBuffer = pf.cmd;
        VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        si.commandBufferInfoCount = 1;
        si.pCommandBufferInfos = &cbi;
        VK(vkQueueSubmit2(queue_, 1, &si, pf.fence));
        VK(vkWaitForFences(device_, 1, &pf.fence, VK_TRUE, UINT64_MAX));
        auto* p = static_cast<const std::uint8_t*>(rb.mapped);
        result.rgba.assign(p, p + rb.size);
    } catch (...) {
        vkDeviceWaitIdle(device_);
        // Leave the fence signalled so the next frame does not wait forever.
        VkSubmitInfo2 empty{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        vkQueueSubmit2(queue_, 1, &empty, pf.fence);
        vkDeviceWaitIdle(device_);
        destroyBuffer(rb), destroyImage(out);
        throw;
    }
    destroyBuffer(rb), destroyImage(out);
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
    return result;
}

} // namespace

std::unique_ptr<Renderer> createVulkanRenderer(const VulkanConfig& cfg, const RenderSettings& settings) {
    return std::make_unique<VulkanRenderer>(cfg, settings);
}

} // namespace wc98::render
