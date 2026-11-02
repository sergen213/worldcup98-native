// M4 slice 3: render an original WC98 front-end .MDL (GEOM/SKEL) through the Vulkan renderer.
//   wc98-model-view out.png <mdl-file>            — one model, textures from its sibling .FSH/.PAK
//   wc98-model-view out.png <tree-dir> [name]     — FE/<name> (default WCUPF.MDL) inside a game tree
// Geometry is the parsed draw-list groups expanded to per-corner vertices (positions + stored UV,
// geometric face normals); each texture name in the model's name table becomes one textured draw.
// Layout evidence: docs/reverse-engineering/MDL_GEOMETRY_NOTES.md.
#include "models/model_common.hpp" // Texture / harvestTextures / loadInput shared with tools/models
#include "wc98/formats/mdl.hpp"
#include "wc98/formats/png.hpp"
#include "wc98/renderer/vulkan.hpp"

#include <glm/glm.hpp>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace wc98;
using namespace wc98::render;
using namespace wc98::tools;

namespace {

struct Bucket { // faces sharing one material
    std::uint32_t textureIndex = 0xFFFFFFFFu; // index into MdlFile::textures, or none
    bool textured = false;
    render::MeshData mesh;
};

void appendCorners(Bucket& b, const render::Vertex (&c)[3]) {
    const std::uint32_t base = std::uint32_t(b.mesh.vertices.size());
    for (const auto& v : c) b.mesh.vertices.push_back(v);
    b.mesh.indices.insert(b.mesh.indices.end(), {base, base + 1, base + 2});
}

std::vector<std::uint8_t> tryRead(const std::filesystem::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec) ? readFile(p) : std::vector<std::uint8_t>{};
}

} // namespace

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "wc98-model-view.png";
    try {
        if (argc < 3) {
            std::cerr << "usage: wc98-model-view out.png <mdl-file|tree-dir> [name]\n";
            return 2;
        }
        namespace fs = std::filesystem;
        const fs::path inPath{argv[2]};
        fs::path mdlPath = inPath, fshPath, pakPath;
        if (fs::is_directory(inPath)) {
            const std::string name = argc > 3 ? argv[3] : "WCUPF.MDL";
            auto found = wc98::findCaseInsensitive(inPath, "FE/" + name);
            if (found.empty()) found = wc98::findCaseInsensitive(inPath, name);
            if (found.empty()) { std::cerr << name << " not found under " << inPath << '\n'; return 2; }
            mdlPath = found;
        }
        const fs::path stem = mdlPath.parent_path() / mdlPath.stem(); // FE/WCUPF -> textures beside it
        fshPath = wc98::findCaseInsensitive(mdlPath.parent_path(), std::string(mdlPath.stem().native()) + ".FSH");
        pakPath = wc98::findCaseInsensitive(mdlPath.parent_path() / "PACK", std::string(mdlPath.stem().native()) + ".PAK");

        // WC98 FE bitmaps index past their attached palettes (matrix section 4): load the shared one.
        auto palPath = wc98::findCaseInsensitive(inPath, "FE/PACK/PAL.PAK");
        if (palPath.empty()) palPath = wc98::findCaseInsensitive(mdlPath.parent_path(), "PAL.PAK");
        if (palPath.empty()) palPath = wc98::findCaseInsensitive(inPath, "PAL.PAK");
        if (!palPath.empty()) formats::setGlobalPalette(formats::wc98GlobalPaletteFromPalArchive(readFile(palPath)));

        std::map<std::string, Texture> textures;
        if (auto bytes = tryRead(fshPath); !bytes.empty()) harvestTextures(bytes, textures, 0);
        if (auto bytes = tryRead(pakPath); !bytes.empty()) harvestTextures(bytes, textures, 0);

        const formats::MdlFile mdl = formats::MdlFile::parse(readFile(mdlPath));
        std::cout << mdlPath.filename() << ": " << mdl.magic << ' ' << mdl.groups.size() << " groups, "
                  << mdl.textures.size() << " texture names, " << textures.size() << " FSH bitmaps\n";

        // One mesh per name-table entry; faces without a resolvable bitmap keep the record colour.
        std::vector<Bucket> buckets(mdl.textures.size());
        for (std::size_t i = 0; i < mdl.textures.size(); ++i) {
            buckets[i].textureIndex = std::uint32_t(i);
            buckets[i].textured = textures.contains(mdl.textures[i].name) && !textures[mdl.textures[i].name].rgba.empty();
        }

        glm::vec3 lo{1e30f}, hi{-1e30f};
        std::size_t faceCount = 0, untexturedFaces = 0;
        for (const auto& g : mdl.groups) {
            for (const auto& f : g.faces) {
                Bucket& b = buckets[f.texture];
                const auto& e = mdl.textures[f.texture];
                // Vertex colour: name-record RGB when untextured (0x00b2b2b2 grey), white otherwise; alpha forced open.
                const std::uint32_t rgba = b.textured ? 0xFFFFFFFFu : (e.color & 0x00FFFFFFu) | 0xFF000000u;
                if (!b.textured) ++untexturedFaces;
                std::array<glm::vec3, 3> p{};
                for (int c = 0; c < 3; ++c) {
                    const auto& q = g.positions[f.vertices[c]];
                    p[c] = {q[0], q[1], q[2]};
                    lo = glm::min(lo, p[c]);
                    hi = glm::max(hi, p[c]);
                }
                glm::vec3 n = glm::cross(p[1] - p[0], p[2] - p[0]);
                const float len = glm::length(n);
                n = len > 1e-9f ? n / len : glm::vec3{0, 1, 0};
                appendCorners(b, {{p[0], n, {f.uv[0], f.uv[1]}, rgba},
                                  {p[1], n, {f.uv[2], f.uv[3]}, rgba},
                                  {p[2], n, {f.uv[4], f.uv[5]}, rgba}});
                ++faceCount;
            }
        }

        VulkanConfig cfg;
        cfg.validation = validationRequestedByEnv();
        auto messages = std::make_shared<std::atomic<std::uint32_t>>();
        cfg.validationCounter = messages;
        auto r = createVulkanRenderer(cfg, {}); // offscreen only

        std::vector<DrawItem> draws;
        for (auto& b : buckets) {
            if (b.mesh.vertices.empty()) continue;
            Material m;
            m.doubleSided = true; // face winding in the original data is unverified
            const Texture* t = b.textured ? &textures[mdl.textures[b.textureIndex].name] : nullptr;
            if (t) m.texture = r->createTexture(Image{t->w, t->h, t->rgba}, Wrap::Repeat, mdl.textures[b.textureIndex].name);
            draws.push_back({r->createMesh(b.mesh), m, glm::mat4(1)});
        }

        const glm::vec3 centre = (lo + hi) * .5f;
        const float radius = std::max(1.0f, glm::length(hi - lo) * .5f);
        const float dist = radius * 2.6f; // vertical fov 45deg: full object plus margin in frame
        Camera cam;
        cam.eye = centre + glm::vec3{std::sin(0.8f) * dist, radius * 0.7f, std::cos(0.8f) * dist};
        cam.target = centre;
        cam.nearZ = radius * 0.01f;
        cam.farZ = dist + radius * 4.f;

        SpriteBatch ui; // empty: pure 3D frame
        Frame frame{
            .camera = cam,
            .draws = std::span(draws),
            .ui = &ui,
            .clearColor = {0.02f, 0.02f, 0.05f, 1},
            .joints = {},
            .world = nullptr,
            .fog = {},
            .background = nullptr,
        };

        r->setSettings({PresentMode::Original, Aspect::Pillarbox4x3, false});
        Image img = r->renderOffscreen(frame, 640, 480);
        if (img.width != 640 || img.height != 480) {
            std::cerr << "unexpected image size " << img.width << 'x' << img.height << '\n';
            return 1;
        }

        std::cout << "faces=" << faceCount << " untextured=" << untexturedFaces
                  << " draws=" << draws.size() << " bbox=(" << lo.x << ',' << lo.y << ',' << lo.z << ")..("
                  << hi.x << ',' << hi.y << ',' << hi.z << ")\n";

        std::vector<std::uint8_t> png = formats::encodePng(img.width, img.height, img.rgba);
        if (outPath.find('/') != std::string::npos) fs::create_directories(fs::path(outPath).parent_path());
        std::ofstream out(outPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        out.close();
        if (!out) { std::cerr << "cannot write " << outPath << '\n'; return 1; }

        r.reset(); // drain validation messages before the check
        if (*messages) {
            std::cerr << *messages << " validation message(s)\n";
            return 3;
        }
        std::cout << outPath << ": " << png.size() << " bytes\n";
    } catch (const std::exception& e) {
        std::cerr << "model-view failed: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
