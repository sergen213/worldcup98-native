// Headless Vulkan render of a synthetic scene, read back and pixel-checked in each mode.
#include "vk_test.hpp"
#include "wc98/formats/png.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace wc98::render;

namespace {

MeshData quad(float x0, float y0, float x1, float y1, float z, std::uint32_t rgba, bool facingCamera = true) {
    MeshData m;
    m.vertices = {{{x0, y1, z}, {0, 0, 1}, {0, 0}, rgba}, {{x0, y0, z}, {0, 0, 1}, {0, 1}, rgba},
                  {{x1, y0, z}, {0, 0, 1}, {1, 1}, rgba}, {{x1, y1, z}, {0, 0, 1}, {1, 0}, rgba}};
    m.indices = facingCamera ? std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3} : std::vector<std::uint32_t>{0, 2, 1, 0, 3, 2};
    return m;
}

Image checkerboard() { // 4x4 cells of 16 px, white at (0,0)
    Image img{64, 64, std::vector<std::uint8_t>(64 * 64 * 4)};
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) {
            std::uint8_t v = ((x / 16 + y / 16) % 2) ? 0 : 255;
            std::uint8_t* p = &img.rgba[(y * 64 + x) * 4];
            p[0] = p[1] = p[2] = v, p[3] = 255;
        }
    return img;
}

struct Scene {
    Camera cam;
    std::vector<DrawItem> draws;
    SpriteBatch ui;
    Frame frame() const { return {cam, draws, &ui, {0, 0, 0.2f, 1}, {}, nullptr, {}, {}}; }
};

Scene build(Renderer& r) {
    Scene s;
    s.cam.eye = {0, 0, 10};
    s.cam.verticalFovDeg = 45;
    TextureId checker = r.createTexture(checkerboard(), Wrap::Clamp, "checker");
    MeshData tri;
    tri.vertices = {{{-4, -1, 0}, {0, 0, 1}, {}, packRgba(255, 0, 0)}, {{-2, -1, 0}, {0, 0, 1}, {}, packRgba(255, 0, 0)},
                    {{-3, 1, 0}, {0, 0, 1}, {}, packRgba(255, 0, 0)}};
    tri.indices = {0, 1, 2};
    s.draws.push_back({r.createMesh(tri), {}, glm::mat4(1)});
    // Near green quad drawn first, larger far blue quad second: depth test must keep green.
    s.draws.push_back({r.createMesh(quad(-1, -1, 1, 1, 1, packRgba(0, 255, 0))), {}, glm::mat4(1)});
    s.draws.push_back({r.createMesh(quad(-0.5f, -1, 2.5f, 1, -1, packRgba(0, 0, 255))), {}, glm::mat4(1)});
    s.draws.push_back({r.createMesh(quad(2.5f, -1, 4.5f, 1, 0, 0xFFFFFFFFu)), {checker}, glm::mat4(1)});
    // Back-facing yellow quad is culled; the same quad double-sided (magenta) is drawn.
    s.draws.push_back({r.createMesh(quad(-1, -3, 1, -2, 0, packRgba(255, 255, 0), false)), {}, glm::mat4(1)});
    Material twoSided;
    twoSided.doubleSided = true;
    s.draws.push_back({r.createMesh(quad(-1, 2, 1, 3, 0, packRgba(255, 0, 255), false)), twoSided, glm::mat4(1)});
    // Alpha blend (50% white over the clear colour) and alpha test (alpha 0.2 < cutoff 0.5).
    Material blend;
    blend.blend = Blend::Alpha;
    s.draws.push_back({r.createMesh(quad(-4, 2, -2, 3, 0, packRgba(255, 255, 255, 128))), blend, glm::mat4(1)});
    Material tested;
    tested.alphaCutoff = 0.5f;
    s.draws.push_back({r.createMesh(quad(2.5f, 2, 4.5f, 3, 0, packRgba(255, 255, 255, 51))), tested, glm::mat4(1)});
    // UI sprite (cyan) in the bottom-right corner of the virtual 640x480 screen.
    s.ui.draw(0, {600, 440, 40, 40}, {0, 0, 1, 1}, packRgba(0, 255, 255));
    return s;
}

// World point -> pixel in a w x h scene rendered with the camera (GL clip, Y up).
glm::ivec2 project(const Camera& c, glm::vec3 p, std::uint32_t w, std::uint32_t h) {
    glm::vec4 clip = c.projection(float(w) / float(h)) * c.view() * glm::vec4(p, 1);
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {int((ndc.x * 0.5f + 0.5f) * w), int((0.5f - ndc.y * 0.5f) * h)};
}

void checkScene(const Image& img, const Scene& s, int tol) {
    auto at = [&](glm::vec3 p) {
        glm::ivec2 q = project(s.cam, p, img.width, img.height);
        return pixel(img, q.x, q.y);
    };
    CHECK(near(at({-3, -0.33f, 0}), 255, 0, 0, tol)); // triangle
    CHECK(near(at({0, 0, 1}), 0, 255, 0, tol));       // near quad wins the depth test
    CHECK(near(at({2, 0, -1}), 0, 0, 255, tol));      // far quad where uncovered
    CHECK(near(at({2.75f, 0.75f, 0}), 255, 255, 255, tol)); // checker cell (0,0) white (v=0 at top)
    CHECK(near(at({3.25f, 0.75f, 0}), 0, 0, 0, tol));       // cell (1,0) black
    CHECK(near(at({2.75f, 0.25f, 0}), 0, 0, 0, tol));       // cell (0,1) black
    CHECK(near(at({0, -2.5f, 0}), 0, 0, 51, tol));          // culled back face -> clear colour
    CHECK(near(at({0, 2.5f, 0}), 255, 0, 255, tol));        // double-sided back face drawn
    CHECK(near(at({-3, 2.5f, 0}), 128, 128, 153, tol + 2)); // 50% white over (0,0,51)
    CHECK(near(at({3.5f, 2.5f, 0}), 0, 0, 51, tol));        // alpha-tested away
    CHECK(near(pixel(img, img.width * 620 / 640, img.height * 460 / 480), 0, 255, 255, tol)); // UI sprite
    CHECK(near(pixel(img, 1, 1), 0, 0, 51, tol));
}

// Height and width in pixels of the green quad along the centre row/column.
glm::ivec2 greenExtent(const Image& img) {
    auto green = [&](std::uint32_t x, std::uint32_t y) { auto c = pixel(img, x, y); return c[1] > 250 && c[0] < 5 && c[2] < 5; };
    glm::ivec2 e{0, 0};
    for (std::uint32_t x = 0; x < img.width; ++x) e.x += green(x, img.height / 2);
    for (std::uint32_t y = 0; y < img.height; ++y) e.y += green(img.width / 2, y);
    return e;
}

} // namespace

int main() {
    VkTest t;
    std::fprintf(stderr, "device: %s\n", t.r->deviceName().c_str());
    Scene s = build(*t.r);

    // original: scene at 640x480, output 640x480 -> 1:1, nearest sampling.
    t.r->setSettings({PresentMode::Original, Aspect::Pillarbox4x3, false});
    Image orig = t.r->renderOffscreen(s.frame(), 640, 480);
    CHECK(orig.width == 640 && orig.height == 480 && orig.rgba.size() == 640u * 480 * 4);
    checkScene(orig, s, 3);

    // original at 1280x720: 960x720 pillarbox; the 640x480 image is upscaled 1.5x.
    Image origWide = t.r->renderOffscreen(s.frame(), 1280, 720);
    CHECK(near(pixel(origWide, 100, 360), 0, 0, 0, 0));  // left bar
    CHECK(near(pixel(origWide, 1200, 360), 0, 0, 0, 0)); // right bar
    CHECK(near(pixel(origWide, 161, 2), 0, 0, 51, 0));   // inside the 4:3 area
    CHECK(near(pixel(origWide, 640, 360), 0, 255, 0, 0)); // centre green, unfiltered

    // enhanced 4:3 without MSAA at 320x240: same picture at another resolution, linear filtering.
    t.r->setSettings({PresentMode::Enhanced, Aspect::Pillarbox4x3, false});
    checkScene(t.r->renderOffscreen(s.frame(), 320, 240), s, 3);
    // enhanced with MSAA: interiors identical, only edges change.
    t.r->setSettings({PresentMode::Enhanced, Aspect::Pillarbox4x3, true});
    checkScene(t.r->renderOffscreen(s.frame(), 640, 480), s, 3);

    // Widescreen is Hor+: same height -> same object size in pixels (no stretch), wider view.
    t.r->setSettings({PresentMode::Enhanced, Aspect::Widescreen, false});
    Image wide = t.r->renderOffscreen(s.frame(), 854, 480);
    t.r->setSettings({PresentMode::Enhanced, Aspect::Pillarbox4x3, false});
    Image narrow = t.r->renderOffscreen(s.frame(), 640, 480);
    glm::ivec2 ew = greenExtent(wide), en = greenExtent(narrow);
    std::fprintf(stderr, "green quad: wide %dx%d, 4:3 %dx%d\n", ew.x, ew.y, en.x, en.y);
    CHECK(en.x > 50 && std::abs(ew.x - en.x) <= 1 && std::abs(ew.y - en.y) <= 1);
    // UI stays in the centred 4:3 area: the sprite sits at x = 107 + 620.
    CHECK(near(pixel(wide, 107 + 620, 460), 0, 255, 255, 3));
    CHECK(near(pixel(wide, 853, 479), 0, 0, 51, 3)); // scene fills the whole width


    // The rendered frame must survive a PNG round-trip on disk: encode, read the file
    // back, check the signature and IHDR dimensions, and that it is non-trivial bytes.
    namespace fs = std::filesystem;
    fs::path outDir = std::getenv("WC98_TEST_OUT") ? std::getenv("WC98_TEST_OUT") : "/tmp/wc98port-out/png";
    fs::create_directories(outDir);
    fs::path pngPath = outDir / "wc98_offscreen.png";
    std::vector<std::uint8_t> png = wc98::formats::encodePng(orig.width, orig.height, orig.rgba);
    {
        std::ofstream f(pngPath, std::ios::binary | std::ios::trunc);
        CHECK(f.good());
        f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        CHECK(f.good());
    }
    {
        std::ifstream f(pngPath, std::ios::binary);
        CHECK(f.is_open());
        std::vector<std::uint8_t> disk((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        // 8-byte PNG signature + IHDR width/height (big-endian) at bytes 16..23.
        static constexpr std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        CHECK(disk.size() > 33 && std::equal(std::begin(kSig), std::end(kSig), disk.begin()));
        auto be32 = [&](std::size_t at) { return (std::uint32_t(disk[at]) << 24) | (std::uint32_t(disk[at + 1]) << 16) | (std::uint32_t(disk[at + 2]) << 8) | disk[at + 3]; };
        CHECK(be32(16) == orig.width && be32(20) == orig.height);
        CHECK(disk.size() > png.size() / 4); // a real rendered frame, not an empty stub
    }
    t.finish();
    return 0;
}
