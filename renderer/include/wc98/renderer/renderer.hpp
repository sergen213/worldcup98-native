#pragma once
// Backend-neutral renderer interface. See docs/RENDERER.md.
//
// Conventions: right-handed world, +Y up. Units are the caller's (the match uses World Cup 98
// game units, 1/16 ft). Camera::projection() follows the GL clip convention with depth mapped to [0,1]
// (GLM_FORCE_DEPTH_ZERO_TO_ONE); backends apply their own clip-space Y orientation. Front faces
// wind counter-clockwise.
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wc98::render {

// RGBA8 image, row-major, top-down.
struct Image {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

enum class Wrap { Repeat, Clamp };

using TextureId = std::uint32_t; // 0 = none (backends substitute opaque white)
using MeshId = std::uint32_t;    // 0 = invalid

struct Vertex {
    glm::vec3 pos{};
    glm::vec3 normal{0, 1, 0};
    glm::vec2 uv{};
    std::uint32_t rgba = 0xFFFFFFFFu; // bytes R,G,B,A in memory order (little-endian 0xAABBGGRR)
};
static_assert(sizeof(Vertex) == 36);

[[nodiscard]] constexpr std::uint32_t packRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    return std::uint32_t(r) | std::uint32_t(g) << 8 | std::uint32_t(b) << 16 | std::uint32_t(a) << 24;
}

// Per-vertex skin data, parallel to MeshData::vertices (Vertex::pos is then ignored). PCMD skins
// store every influence as a position in its joint's frame (docs/formats/PCMD.md), so the skinned
// position is sum_i weight[i] * (joint[joint_i] * pos[i]) - up to 3 influences, as in the data.
struct SkinVertex {
    std::array<glm::vec3, 3> pos{};
    glm::vec3 weight{};       // unused influences: weight 0
    std::uint32_t joints = 0; // joint index of influence i in byte i, relative to DrawItem::jointOffset
    // Optional 4-direction prelight (FIFA 99 player .lm colour maps): when DrawItem::lightBlend is
    // non-zero the vertex colour is sum_i lightBlend[i] * light[i] instead of Vertex::rgba.
    std::array<std::uint32_t, 4> light{};
};
static_assert(sizeof(SkinVertex) == 68);

// Triangle list. Backends store 16-bit indices when every index fits, otherwise 32-bit.
// A non-empty `skin` (same size as `vertices`) makes the mesh GPU-skinned.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SkinVertex> skin;
};

enum class Blend { Opaque, Alpha };

struct Material {
    TextureId texture = 0;
    Blend blend = Blend::Opaque;
    float alphaCutoff = 0.0f; // fragments with alpha below this are discarded (0 = no alpha test)
    bool doubleSided = false;
    float colorScale = 1.0f; // output = texture * vertex colour * colorScale (2 = D3D MODULATE2X)
    bool operator==(const Material&) const = default;
};

struct DrawItem {
    MeshId mesh = 0;
    Material material;
    glm::mat4 model{1.0f};
    std::uint32_t jointOffset = 0; // skinned meshes: first matrix of this draw in Frame::joints
    glm::vec4 lightBlend{0.0f};    // skinned meshes: weights of SkinVertex::light (all 0 = off)
};

// Perspective camera. The vertical FOV is fixed; a wider viewport widens the horizontal
// FOV (Hor+), so widescreen shows more of the scene and never stretches it.
struct Camera {
    glm::vec3 eye{0, 0, 5}, target{0, 0, 0}, up{0, 1, 0};
    float verticalFovDeg = 45.0f;
    float nearZ = 0.5f, farZ = 1000.0f;
    // Left-handed view space like Direct3D / the 1998 game: looking along +z with +y up, +x is to the
    // right of the screen. Front faces then wind clockwise in world space (backends flip culling).
    bool leftHanded = false;

    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspect) const;
    [[nodiscard]] float horizontalFovDeg(float aspect) const;
    // World-space right and up axes of the view (for billboards).
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> axes() const;
};

// 2D quads in the virtual 640x480 screen (origin top-left, +Y down), drawn after the 3D scene
// with alpha blending and no depth test. Consecutive quads with the same texture batch together.
class SpriteBatch {
public:
    static constexpr float kWidth = 640.0f, kHeight = 480.0f;
    struct Run {
        TextureId texture;
        std::uint32_t firstVertex, vertexCount;
    };

    void clear() { vertices_.clear(), runs_.clear(); }
    // dst = {x, y, w, h} in virtual pixels; uv = {u0, v0, u1, v1}.
    void draw(TextureId tex, glm::vec4 dst, glm::vec4 uv = {0, 0, 1, 1}, std::uint32_t rgba = 0xFFFFFFFFu);
    [[nodiscard]] std::span<const Vertex> vertices() const { return vertices_; }
    [[nodiscard]] std::span<const Run> runs() const { return runs_; }

private:
    std::vector<Vertex> vertices_; // 6 per quad (two triangles)
    std::vector<Run> runs_;
};

// World-space triangles rebuilt every frame (billboards, shadows, markers). Drawn after all meshes
// in submission order, depth-tested (opaque materials also write depth); consecutive quads with an
// equal material batch together.
class WorldBatch {
public:
    struct Run {
        Material material;
        std::uint32_t firstVertex, vertexCount;
    };
    void clear() { vertices_.clear(), runs_.clear(); }
    // c = corners top-left, bottom-left, bottom-right, top-right; uv = {u0, v0, u1, v1}.
    void quad(const Material& m, const std::array<glm::vec3, 4>& c, glm::vec4 uv = {0, 0, 1, 1}, std::uint32_t rgba = 0xFFFFFFFFu);
    void quad(const Material& m, const std::array<glm::vec3, 4>& c, glm::vec4 uv, const std::array<std::uint32_t, 4>& rgba); // per corner
    // Camera-facing quad; `right`/`up` from Camera::axes().
    void billboard(const Material& m, glm::vec3 centre, glm::vec2 halfSize, glm::vec3 right, glm::vec3 up, glm::vec4 uv = {0, 0, 1, 1},
                   std::uint32_t rgba = 0xFFFFFFFFu);
    [[nodiscard]] std::span<const Vertex> vertices() const { return vertices_; }
    [[nodiscard]] std::span<const Run> runs() const { return runs_; }

private:
    std::vector<Vertex> vertices_; // 6 per quad
    std::vector<Run> runs_;
};

// Linear fog over eye distance; disabled when end <= start.
struct Fog {
    glm::vec3 color{0, 0, 0};
    float start = 0, end = 0;
};

// original: 3D at 640x480, nearest-neighbour upscale, always 4:3 pillarboxed.
// enhanced: native resolution, MSAA when supported, 4:3 pillarbox or Hor+ widescreen.
enum class PresentMode { Original, Enhanced };
enum class Aspect { Pillarbox4x3, Widescreen };

struct RenderSettings {
    PresentMode mode = PresentMode::Original;
    Aspect aspect = Aspect::Pillarbox4x3; // enhanced only
    bool msaa = true;                     // enhanced only
};

struct Rect {
    std::int32_t x = 0, y = 0;
    std::uint32_t w = 0, h = 0;
    bool operator==(const Rect&) const = default;
};

// Where things land for an output of outW x outH pixels.
struct Layout {
    std::uint32_t sceneW = 0, sceneH = 0; // internal 3D render target size
    Rect ui;                              // 640x480 virtual UI area inside the scene target
    Rect dest;                            // where the scene target is copied in the output
};
[[nodiscard]] Rect fit4x3(std::uint32_t w, std::uint32_t h); // largest centred 4:3 rect
[[nodiscard]] Layout computeLayout(const RenderSettings& s, std::uint32_t outW, std::uint32_t outH);

struct Frame {
    Camera camera;
    std::span<const DrawItem> draws;
    const SpriteBatch* ui = nullptr;
    glm::vec4 clearColor{0, 0, 0, 1}; // scene background; the area outside Layout::dest is black
    std::span<const glm::mat4> joints; // skin palettes (world-from-joint before DrawItem::model)
    const WorldBatch* world = nullptr;
    Fog fog;
    // Drawn first, without depth test or depth write (sky gradients behind everything); unfogged.
    const WorldBatch* background = nullptr;
};

class Renderer {
public:
    virtual ~Renderer() = default;
    // A non-empty cacheKey returns the existing resource when one was created with that key.
    virtual TextureId createTexture(const Image& img, Wrap wrap, std::string_view cacheKey = {}) = 0;
    virtual MeshId createMesh(const MeshData& mesh, std::string_view cacheKey = {}) = 0;
    virtual void destroyTexture(TextureId id) = 0;
    virtual void destroyMesh(MeshId id) = 0;
    virtual void setSettings(const RenderSettings& s) = 0;
    [[nodiscard]] virtual const RenderSettings& settings() const = 0;
    // Renders and presents to the window surface. Returns false when no frame was presented
    // (zero-sized/minimised window); swapchain recreation is handled internally.
    virtual bool present(const Frame& frame) = 0;
    // Renders into an offscreen width x height image and reads it back (no surface needed).
    virtual Image renderOffscreen(const Frame& frame, std::uint32_t width, std::uint32_t height) = 0;
    // Validation-layer warnings + errors reported so far (0 when validation is disabled).
    [[nodiscard]] virtual std::uint32_t validationMessageCount() const = 0;
    [[nodiscard]] virtual std::string deviceName() const = 0;
};

} // namespace wc98::render
