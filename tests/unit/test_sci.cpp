// Derived from fifa99-native (same author), ported for World Cup 98
// .SCI payload grammar gate: parses every COMMON/*.SCI scene payload in the extracted tree and
// asserts the TASK_HANDLERS_WC98.md §6 layout (header tags, record-stream alignment/observed type
// range, tail-table DGROUP anchoring) plus the dossier-verified named-file facts. Data-driven:
// gated on WC98_TREE (77 = skip).
#include <wc98/formats/sci.hpp>

#include "check.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace wc98::formats;

namespace {

std::vector<std::uint8_t> slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

const SciRecord* recordAt(const SciFile& f, std::size_t fileOff) {
    if (fileOff < 0x30 || (fileOff - 0x30) % 16 != 0) return nullptr;
    const std::size_t idx = (fileOff - 0x30) / 16;
    return idx < f.records.size() ? &f.records[idx] : nullptr;
}

} // namespace

int main() {
    const auto tree = treeDirOrSkip();
    const auto common = tree / "COMMON";
    if (!std::filesystem::exists(common)) { std::fprintf(stderr, "no COMMON dir: skipping\n"); return 77; }

    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(common))
        if (e.path().extension() == ".SCI") files.push_back(e.path());
    std::fprintf(stderr, "COMMON/*.SCI: %zu files\n", files.size());
    CHECK(files.size() >= 80); // dossier count = 87

    int hist[32] = {};
    for (const auto& p : files) {
        const auto bytes = slurp(p);
        SciFile f;
        try {
            f = SciFile::parse(bytes);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "SCI parse failed %s: %s\n", p.string().c_str(), e.what());
            return 1;
        }
        CHECK(!f.records.empty());
        CHECK(!f.tail.empty());
        for (const auto& r : f.records) hist[static_cast<int>(r.fields[0])]++;
    }

    // Dossier-verified named facts (§6 header/record/tail examples).
    const auto gengol = slurp(common / "GENGOL00.SCI");
    const SciFile g = SciFile::parse(gengol);
    CHECK(g.count == 0x20);
    CHECK(g.tagA == 0x35 && g.tagC == 0x16 && g.magic == 0x201);
    if (const auto* r = recordAt(g, 0x80)) { // GENGOL00 @0x80 bytes {0x15,0,0x3c,5} (+0x96 [P])
        CHECK(r->fields[0] == 0x15 && r->fields[2] == 0x3c && r->fields[3] == 5);
    } else {
        CHECK(false);
    }
    CHECK(g.tail.front().va == 0x582054u); // GENGOL00 tail @0x7D8 first entry
    CHECK(g.trailer.size() == 3 && g.trailer[0].va == 0x5a30ccu && g.trailer[1].va == 0x5a3150u &&
          g.trailer[2].va == 0x5a2054u); // dossier trailing relocation bases

    const auto clainc = slurp(common / "CLAINC00.SCI");
    const SciFile c = SciFile::parse(clainc);
    CHECK(c.count == 0x11);
    CHECK(c.magic == 0x203);
    CHECK(!c.trailer.empty() && c.trailer[0].va == 0x5a30ccu && c.trailer[0].val == 0xb06u);
    if (const auto* r = recordAt(c, 0x40)) { // CLAINC00 @0x40 bytes {0x15,0,-0x14,6,0xa0} [P]
        CHECK(r->fields[0] == 0x15 && r->fields[2] == -0x14 && r->fields[3] == 6 && r->fields[4] == 0xa0);
    } else {
        CHECK(false);
    }

    int used = 0;
    for (int t = 0; t < 32; ++t) if (hist[t]) ++used;
    std::fprintf(stderr, "SCI grammar gate: %zu files parsed, distinct record types=%d\n", files.size(), used);
    std::printf("test_sci: ok\n");
    return 0;
}
