// MDL geometry gate: synthetic magic checks + census over the real FE/*.MDL tree (12 files).
#include "wc98/formats/mdl.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/formats/venue.hpp"
#include <check.hpp>
#include <wc98/file.hpp>

#include <algorithm>
#include <cctype>
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

// pass-105: exe venue table (VA 0x57dec8, 12 x 0x5c) + RefPack'd SHPI .PAK loader.
void venueChecks() {
    const auto dir = treeDirOrSkip() / "FE";
    const auto table = wc98::formats::venueTable();
    CHECK(table.size() == 12);

    // Bind the hand-written table to the shipped exe bytes: the DGROUP table at raw
    // 0x17c4c8 (VA 0x57dec8) holds path pointers into the string pool and a callback
    // column at +0x0c; kVenues must match it verbatim.
    const auto exe = wc98::readFile(treeDirOrSkip() / "FIFAWC.EXE");
    CHECK(exe.size() > 0x17c4c8 + 12 * 0x5c);
    const auto u32At = [&exe](std::size_t off) {
        return static_cast<std::uint32_t>(exe[off]) | (static_cast<std::uint32_t>(exe[off + 1]) << 8) |
               (static_cast<std::uint32_t>(exe[off + 2]) << 16) | (static_cast<std::uint32_t>(exe[off + 3]) << 24);
    };
    const auto exeString = [&exe](std::uint32_t va) {
        const std::size_t off = va - 0x401a00; // DGROUP VA -> file raw
        std::string s;
        while (off + s.size() < exe.size() && exe[off + s.size()] != 0)
            s.push_back(static_cast<char>(exe[off + s.size()]));
        return s;
    };
    unsigned exeCallbacks = 0;
    for (std::size_t i = 0; i < 12; ++i) {
        const std::size_t e = 0x17c4c8 + i * 0x5c;
        CHECK(exeString(u32At(e + 0x00)) == std::string(table[i].pakPath));
        CHECK(exeString(u32At(e + 0x04)) == std::string(table[i].mdlPath));
        CHECK(exeString(u32At(e + 0x08)) == std::string(table[i].fshPath));
        const std::uint32_t cb = u32At(e + 0x0c);
        if (cb != 0) {
            ++exeCallbacks;
            CHECK(cb == 0x47b9a4); // scene-handler frame FUN_0047b9a4
            CHECK(table[i].loadCallback);
        } else {
            CHECK(!table[i].loadCallback);
        }
    }
    CHECK(exeCallbacks == 1); // only lpolybf carries the scene-handler callback

    unsigned callbacks = 0;
    unsigned texturesNamedInPak = 0;
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& v = table[i];
        // Resolve the exe paths case-insensitively against the extracted tree: the exe
        // uses lowercase `fe\...` strings; the CD stores FE/PACK + FE upper case.
         auto stemUpper = std::string(v.mdlPath);
        for (auto& c : stemUpper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const auto mdlPath = dir / stemUpper.substr(3); // "FE/PDPF.MDL" -> "PDPF.MDL"
         auto pakUpper = std::string(v.pakPath);
        for (auto& c : pakUpper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const auto pakPath = dir / "PACK" / pakUpper.substr(8); // "FE/PACK/PDPF.PAK" -> name
        CHECK(std::filesystem::exists(mdlPath));
        CHECK(std::filesystem::exists(pakPath));
        if (v.loadCallback) ++callbacks;

        const auto pak = wc98::readFile(pakPath);
        CHECK(wc98::formats::refpack::isCompressed(std::span<const std::uint8_t>{pak}));
        const auto fsh = wc98::formats::parseVenuePak(std::span<const std::uint8_t>{pak});
        CHECK(!fsh.entries.empty());

        // MDL texture-name records must key shapes in the venue .PAK (loader FUN_00483418
        // material lane: pak record list drives the per-venue material slots).
        const auto mdl = wc98::formats::MdlFile::parse(wc98::readFile(mdlPath));
        bool any = false;
        for (const auto& t : mdl.textures) {
            for (const auto& e : fsh.entries) {
                if (e.tag.rfind(t.name, 0) == 0) { any = true; break; }
            }
            if (any) break;
        }
        if (any) ++texturesNamedInPak;
    }
    CHECK(callbacks == 1); // only lpolybf carries the scene-handler callback (FUN_0047b9a4)
    CHECK(texturesNamedInPak == 12); // every venue pak covers at least one MDL texture tag
}
} // namespace

int main() {
    syntheticChecks();
    treeChecks();
    venueChecks();
    std::printf("test_mdl: ok\n");
    return 0;
}
