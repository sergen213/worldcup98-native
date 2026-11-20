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
        // sc4 header-rewrite boundary mapping (pass-110sci §6): the install FUN_004a75d0
        // section-boundary pointers reconcile with these spans for every tree file — loop1
        // patches (magic & 0xff) stride-12 views at tailStart, loop2 patches tagC more views
        // from @0x18 = @0x14 + (magic & 0xff) * 0xC, and the stride-8 trailer starts at
        // @0x1C + 4 = tailStart + tail.size() * 12; the last (magic >> 8) + 1 pair entries
        // end at EOF. (@0x14 itself = tailStart - 4, the tail-anchor x16 field + 0x24.)
        const std::size_t lo = f.magic & 0xff;
        const std::size_t hi = f.magic >> 8;
        CHECK(f.tail.size() == lo + f.tagC);
        CHECK(f.trailer.size() == hi + 1);
        CHECK(bytes.size() == 0x24 + std::size_t{f.offset} * 16 + lo * 0xC +
                             std::size_t{f.tagC} * 0xC + 4 + f.trailer.size() * 8);
        for (const auto& r : f.records) hist[static_cast<int>(r.fields[0])]++;
    }

    // Pass-17sci opcode-stream gate: every record stream decodes under the observed-type
    // grammar (INTERPRETER.md); head sequences pinned from tree bytes.
    std::size_t totalOps = 0;
    for (const auto& p : files) {
        const auto ops = decodeOpcodeStream(SciFile::parse(slurp(p)));
        CHECK(!ops.empty());
        totalOps += ops.size();
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

    if (const auto gops = decodeOpcodeStream(g); gops.size() >= 8) {
        const std::uint16_t want[] = {0x10, 0x13, 0x13, 0x13, 0x13, 0x15, 0xb, 0xa}; // GENGOL00 head [P]
        for (int i = 0; i < 8; ++i) CHECK(gops[static_cast<std::size_t>(i)].type == want[i]);
        CHECK(gops[0].offset == 0x30);
    } else {
        CHECK(false);
    }
    if (const auto cops = decodeOpcodeStream(c); cops.size() >= 8) {
        const std::uint16_t want[] = {0x10, 0x15, 0xb, 0xa, 0x4, 0xd, 0xb, 0xa}; // CLAINC00 head [P]
        for (int i = 0; i < 8; ++i) CHECK(cops[static_cast<std::size_t>(i)].type == want[i]);
    } else {
        CHECK(false);
    }
    std::fprintf(stderr, "SCI opcode gate: %zu ops decoded across tree\n", totalOps);

    int used = 0;
    for (int t = 0; t < 32; ++t) if (hist[t]) ++used;
    std::fprintf(stderr, "SCI grammar gate: %zu files parsed, distinct record types=%d\n", files.size(), used);

    // Pass-110sci exe registry gate (TASK_HANDLERS_WC98.md §10): the boot-time script registry is
    // static data in the exe image — pool @0x580520 (86 name entries), 22 registration records
    // @0x580A80 feeding registration FUN_004a7090. Pinned from retail FIFAWC.EXE bytes.
    const auto exe = tree / "FIFAWC.EXE";
    if (std::filesystem::exists(exe)) {
        const SciScriptRegistry reg = parseExeScriptRegistry(slurp(exe));
        CHECK(reg.records.size() == 22);
        CHECK(reg.pool.size() == 86);
        std::uint32_t sum = 0;
        for (const auto& r : reg.records) sum += r.count;
        CHECK(sum == 86);
        CHECK(reg.tableVa == 0x580a80u && reg.poolEndVa == 0x580a70u);
        CHECK(reg.records[0].flags == 1 && reg.records[0].ctxId == 0xffffffffu &&
              reg.records[0].count == 0x1f && reg.records[0].first == 0x580520u);
        CHECK(reg.records[1].count == 5 && reg.records[1].first == 0x580710u);
        CHECK(reg.records[16].flags == 0x3ca && reg.records[16].ctxId == 0x30 &&
              reg.records[16].count == 1 && reg.records[16].first == 0x580a20u);
        CHECK(reg.records[21].flags == 0x3cc && reg.records[21].ctxId == 0x2e &&
              reg.records[21].first == 0x580a70u);
        for (const auto& r : reg.records) CHECK(r.moduleByte == 0); // byte @+0xc statically zero
        int clsHist[11] = {};
        for (const auto& e : reg.pool) ++clsHist[e.cls];
        const int want[11] = {24, 5, 2, 7, 20, 5, 4, 7, 8, 2, 2}; // pool class histogram [P]
        for (int c = 0; c < 11; ++c) CHECK(clsHist[c] == want[c]);
        CHECK(reg.pool[0].name == "gengol00.sci" && reg.pool[0].cls == 4);
        CHECK(reg.pool.back().name == "genans03.sci" && reg.pool.back().cls == 0);
        CHECK(std::find_if(reg.pool.begin(), reg.pool.end(),
                           [](const SciPoolEntry& e) { return e.name == "gengol31.sci"; }) == reg.pool.end()); // 87th file unregistered
        std::fprintf(stderr, "SCI exe registry gate: 22 records, 86 pool entries\n");
    } else {
        std::fprintf(stderr, "FIFAWC.EXE absent: skipping exe registry gate\n");
    }
    std::printf("test_sci: ok\n");
    return 0;
}
