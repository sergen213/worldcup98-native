// MDL geometry gate: synthetic magic checks + census over the real FE/*.MDL tree (12 files).
#include "wc98/formats/mdl.hpp"
#include <check.hpp>
#include <wc98/file.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;

void syntheticChecks() {
    const Bytes empty;
    CHECK(!wc98::formats::MdlFile::isMdl(empty));

    // Minimal GEOM file: magic, 0 objects, 1 texture name, one group with 1 face, 3 verts.
    Bytes good;
    auto put = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) good.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    auto putf = [&](float f) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&f);
        good.insert(good.end(), p, p + 4);
    };
    for (const char* s = "GEOM"; *s; ++s) good.push_back(static_cast<std::uint8_t>(*s));
    put(1);            // texture name count @4
    put(0);            // object count @8
    put(20);           // constant 20 @0x0c (meaning unverified)
    put(0x20);         // derived table size @0x10
    put(0);            // always 0 @0x14
    good.insert(good.end(), {0xB2, 0xB2, 0x00, 0x00, 'w', 'c', '0', '7', 1, 0, 0, 0}); // name record
    good.resize(0x40);
    put(0);            // next?
    putf(0.0f);        // f32 ?
    put(0);            // u32 0
    put(1);            // faceCount
    put(3);            // vertexCount
    put(0x40 + 36);    // facesOff == header+36
    put(0x40 + 36 + 40); // vertsOff
    put(0x40 + 36 + 40 + 48); // vertsEnd
    put(0x40 + 36 + 40 + 48); // end
    put(0);            // face texture idx
    put(0); put(1); put(2); // vertices
    for (int i = 0; i < 6; ++i) putf(0.5f); // uv
    for (int v = 0; v < 3; ++v) { putf(float(v)); putf(0.f); putf(0.f); putf(0.f); }

    CHECK(wc98::formats::MdlFile::isMdl(good));
    const auto m = wc98::formats::MdlFile::parse(good);
    CHECK(m.magic == "GEOM");
    CHECK(m.textures.size() == 1 && m.textures[0].name == "wc07");
    CHECK(m.groups.size() == 1);
    CHECK(m.groups[0].faces.size() == 1);
    CHECK(m.groups[0].positions.size() == 3);

    auto truncated = good;
    truncated.resize(good.size() - 4); // vertsEnd says more bytes than present -> throws
    CHECK_THROWS(wc98::formats::MdlFile::parse(truncated));
}

void treeChecks() {
    const auto dir = treeDirOrSkip() / "FE";
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".MDL") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    CHECK(files.size() == 12);

    unsigned groups = 0, faces = 0, names = 0;
    for (const auto& f : files) {
        const auto data = wc98::readFile(f);
        CHECK(wc98::formats::MdlFile::isMdl(data));
        const auto m = wc98::formats::MdlFile::parse(data);
        if (f.filename() == "LPOLYBF.MDL")
            CHECK(m.magic == "SKEL"); // only skinning model: skeleton format
        else
            CHECK(m.magic == "GEOM");
        CHECK(!m.groups.empty());
        for (const auto& g : m.groups) {
            CHECK(!g.faces.empty());
            for (const auto& face : g.faces) {
                CHECK(face.texture < m.textures.size());
                for (const auto v : face.vertices) CHECK(v < g.positions.size());
            }
            ++groups;
            faces += static_cast<unsigned>(g.faces.size());
        }
        names += static_cast<unsigned>(m.textures.size());
    }

    // WCUPF/BRDXF group/face counts pinned in MDL_GEOMETRY_NOTES census.
    struct { const char* name; unsigned groups; unsigned faces; } expect[] = {
        {"WCUPF.MDL", 2, 416},
        {"BRDXF.MDL", 8, 370},
    };
    for (const auto& e : expect) {
        const auto m = wc98::formats::MdlFile::parse(wc98::readFile(dir / e.name));
        CHECK(m.groups.size() == e.groups);
        unsigned f = 0;
        for (const auto& g : m.groups) f += static_cast<unsigned>(g.faces.size());
        CHECK(f == e.faces);
    }

    // Tree-wide census (measured 2026-11-06): every group/face/name in the 12 FE models.
    CHECK(groups == 113);
    CHECK(faces == 4886);
    CHECK(names == 478);
}
} // namespace

int main() {
    syntheticChecks();
    treeChecks();
    std::printf("test_mdl: ok\n");
    return 0;
}
