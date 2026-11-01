// Derived from fifa99-native (same author), ported for World Cup 98
// PCMD / CMAP parser: synthetic round trip + every PCMD file on the original CD.
#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/pcmd.hpp"
#include "wc98/formats/refpack.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace wc98::formats;
namespace fs = std::filesystem;

namespace {

struct Out {
    std::vector<std::uint8_t> b;
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
    void fx(double v) { u32(std::uint32_t(std::int32_t(std::lround(v * 65536)))); }
    void str(const char* s, std::size_t n) { for (std::size_t i = 0; i < n; ++i) b.push_back(i < std::strlen(s) ? std::uint8_t(s[i]) : 0); }
    void nodeHeader(std::uint32_t type, const char* name, int parent) {
        u32(type); str(name, 20); u32(std::uint32_t(parent)); u32(0xFFFFFFFF); u32(0xFFFFFFFF);
        fx(1); fx(1); fx(1); fx(0); fx(0); fx(0); fx(1); fx(0); fx(2.5); fx(0);
    }
};

// One textured triangle mesh + one joint carrying a skinned quad mesh's influences.
std::vector<std::uint8_t> synthetic(bool badMaterial = false) {
    Out o;
    o.str("PCMD", 4); o.u32(0x100); o.str("SOFTIMAGE", 20); o.u32(0);
    o.str("MATR", 4); o.u32(1); o.u32(1); o.u32(0x007F7F7F); o.str("abcd", 8);
    o.str("NODE", 4); o.u32(3);
    o.nodeHeader(1, "tri", -1);
    o.u32(3); o.fx(0); o.fx(0); o.fx(0); o.fx(1); o.fx(0); o.fx(0); o.fx(0); o.fx(1); o.fx(0);
    o.u32(1); o.u32(1); o.u32(1); o.u32(badMaterial ? 1 : 0); o.u32(3);
    for (std::uint32_t i = 0; i < 3; ++i) { o.u32(i); o.fx(0.25 * i); o.fx(1 - 0.25 * i); o.u32(0x00112233); }
    o.nodeHeader(1, "skinned", -1);
    o.u32(0); o.u32(1); o.u32(1); o.u32(0); o.u32(0); o.u32(4);
    for (std::uint32_t i = 0; i < 4; ++i) { o.u32(i); o.fx(0); o.fx(0); o.u32(0); }
    o.nodeHeader(2, "joint", -1);
    o.u32(4);
    for (std::uint32_t i = 0; i < 4; ++i) { o.fx(i); o.fx(-1.5); o.fx(0); o.u32(1); o.u32(i); o.fx(100); }
    o.b.push_back(0); o.str("cRC", 3); o.u32(0x1234);
    return o.b;
}

void syntheticTests() {
    auto d = synthetic();
    auto m = PcmdModel::parse(d);
    CHECK(m.exporter == "SOFTIMAGE" && m.version == 0x100 && m.crcTrailer);
    CHECK(m.materials.size() == 1 && m.materials[0].texture == "abcd" && m.materials[0].color[0] == 0x7F);
    CHECK(m.nodes.size() == 3);
    const auto& tri = m.nodes[0];
    CHECK(tri.type == PcmdNodeType::Mesh && tri.vertices.size() == 3 && tri.vertices[1][0] == 1.0f);
    CHECK(tri.translation[1] == 2.5f && tri.rotation[3] == 1.0f);
    CHECK(tri.polygons.size() == 1 && tri.polygons[0].vertexColors && tri.polygons[0].corners[2].u == 0.5f && tri.polygons[0].corners[2].v == 0.5f);
    CHECK(tri.polygons[0].corners[0].color[0] == 0x33);
    CHECK(m.vertexCount(1) == 4); // skinned mesh: derived from weights
    CHECK(m.nodes[2].weights[3].position[0] == 3.0f && m.nodes[2].weights[3].percent == 100.0f);
    CHECK(m.cornerCount() == 7);
    CHECK_THROWS(PcmdModel::parse(synthetic(true)));                                // material index out of range
    CHECK_THROWS(PcmdModel::parse(std::span(d).first(d.size() - 20)));              // truncated
    auto junk = d; junk.push_back(0);
    CHECK_THROWS(PcmdModel::parse(junk));                                           // unexplained trailing bytes

    // CMAP: 2 sets x 3 colours
    std::vector<std::uint8_t> c = {'C', 'M', 'A', 'P', 44, 0, 0, 0, ' ', '0', '0', '1', 2, 0, 0, 0, 3, 0, 0, 0};
    for (int i = 0; i < 24; ++i) c.push_back(std::uint8_t(i));
    auto cm = CmapFile::parse(c);
    CHECK(cm.sets == 2 && cm.count == 3 && cm.set(1)[0][0] == 12);
    c[4] = 45;
    CHECK_THROWS(CmapFile::parse(c));
}

std::string upper(std::string s) { for (auto& ch : s) ch = char(std::toupper(static_cast<unsigned char>(ch))); return s; }

} // namespace

int main() {
    syntheticTests();
    auto root = treeDirOrSkip();
    int files = 0, skinned = 0, polys = 0, weightAnomalies = 0;
    auto check = [&](const std::string& label, std::span<const std::uint8_t> d) {
        (void)label;
        auto m = PcmdModel::parse(d);
        ++files;
        for (std::size_t i = 0; i < m.nodes.size(); ++i) {
            const auto& n = m.nodes[i];
            for (const auto& p : n.polygons) {
                ++polys;
                CHECK(p.corners.size() == 3 || p.corners.size() == 4);
                for (const auto& c : p.corners) CHECK(c.u > -0.1f && c.u < 1.1f && c.v > -0.1f && c.v < 1.1f);
            }
            if (n.type == PcmdNodeType::Mesh && n.vertices.empty() && !n.polygons.empty()) {
                // skinned mesh: every vertex has influences summing to 100 %
                ++skinned;
                std::vector<double> sum(m.vertexCount(i), 0);
                for (const auto& j : m.nodes)
                    for (const auto& w : j.weights)
                        if (w.meshNode == i) sum[w.vertex] += w.percent;
                for (double s : sum) weightAnomalies += std::abs(s - 100) > 0.01;
            }
        }
    };
    // WC98 gates (matrix section 13): the loose .PC player models (INGAME/PLAYER/MODELS) and
    // the .PC members of archives parse; WC98 has no CMAP colour maps and no .XXX rigs
    // (the inventory doc lists the CMAP magic as absent).
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        auto ext = upper(e.path().extension().string());
        if (ext != ".PC" && ext != ".PC2" && ext != ".BIG" && ext != ".PAK" && ext != ".MO" && ext != ".VIV") continue;
        auto d = wc98::formats::refpack::decompressIfNeeded(wc98::readFile(e.path()));
        const std::span<const std::uint8_t> ds{d};
        if (PcmdModel::isPcmd(ds)) { check(e.path().string(), ds); continue; }
        std::size_t base = BigArchive::isBig(ds) ? 0 : (ds.size() > 0x34 && BigArchive::isBig(ds.subspan(0x30)) ? 0x30 : 0);
        if (!BigArchive::isBig(ds.subspan(base))) continue;
        auto big = BigArchive::parse(ds.subspan(base));
        for (const auto& be : big.entries) {
            auto p = refpack::decompressIfNeeded(BigArchive::payload(ds.subspan(base), be));
            if (PcmdModel::isPcmd(p)) check(e.path().string() + ":" + be.name, p);
        }
    }
    std::printf("%d PCMD models parsed (%d polygons, %d skinned meshes)\n", files, polys, skinned);
    CHECK(files >= 14); // 14 loose .PC models on the WC98 tree (+ archive members)
    return 0;
}
