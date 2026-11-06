// RPLY container gate (docs/asset-formats/RPLY.md): byte-level asserts against the Ghidra pass-8
// constants — record area @572 (FUN_00495e60 +0x23c), BE16 size-prefixed records (FUN_004963b0
// walk p[0]*0x100+p[1]), 142-dword header table (FUN_00495d10 loop 0x8e), rplyindx.dat fixed
// 100 x 25-byte entries (browser stride 0x19) — plus writer round-trip and framing rejection.
#include "check.hpp"

#include <cstring>
#include <vector>

#include "wc98/formats/rply.hpp"

using namespace wc98::formats;
using Bytes = std::vector<std::uint8_t>;

static void testConstants() {
    CHECK(kRplyHeaderBytes == 572);                    // FUN_00495e60 record-area delta 0x23c
    CHECK(kRplyTableDwords * 4 + 4 == kRplyHeaderBytes); // count dword + loop-0x8e table
    CHECK(kRplyIndexEntries * kRplyIndexEntryBytes == 2500);
}

static void testRecordFraming() {
    const Bytes payload{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}; // native tick+pad dialect size
    const RplyRecord r = makeRplyRecord(payload);
    CHECK(r.size() == payload.size() + 2);
    CHECK(r[0] == 0x00 && r[1] == 0x0A);           // BE16 total incl. prefix (walked by FUN_004963b0)
    CHECK(rplyRecordSize(r) == r.size());
}

static void testFileRoundTrip() {
    RplyFile f;
    f.recordCount = 3;
    f.table[0] = 0x2E7A;        // seed (dialect field)
    f.table[1] = 250;           // tick count
    f.table[4] = 0xDEADBEEF;    // final hash hi
    for (int i = 0; i < 3; ++i) {
        Bytes p(8, std::uint8_t(i));
        f.records.push_back(makeRplyRecord(p));
    }
    const Bytes bytes = f.serialize();
    CHECK(bytes.size() == kRplyHeaderBytes + 3 * 10);
    CHECK(std::memcmp(bytes.data(), "\x03\x00\x00\x00", 4) == 0); // LE recordCount @ 0
    CHECK(bytes[kRplyHeaderBytes] == 0x00 && bytes[kRplyHeaderBytes + 1] == 0x0A); // first BE16 size
    const RplyFile g = RplyFile::parse(bytes);
    CHECK(g.recordCount == 3 && g.records.size() == 3);
    CHECK(g.table[0] == 0x2E7A && g.table[4] == 0xDEADBEEF);
    CHECK(g.serialize() == bytes); // byte-identical round trip

    Bytes bad = bytes;
    bad[kRplyHeaderBytes + 1] = 0xFF; // size past EOF -> framing rejection
    CHECK_THROWS(RplyFile::parse(bad));
    CHECK_THROWS(RplyFile::parse(Bytes(4, 0))); // short header
}

static void testIndex() {
    std::vector<RplyIndexEntry> entries(kRplyIndexEntries);
    entries[0] = {0, 1, "rply0001.dat"};
    entries[9] = {7, 10, "user\\rply0010.dat"}; // exe name form (20-char cap)
    const Bytes b = serializeRplyIndex(entries);
    CHECK(b.size() == 2500);
    CHECK(b[4] == 1 && b[9 * 25 + 4] == 10);            // number byte at entry+4 (browser sprintf)
    CHECK(std::memcmp(b.data() + 5, "rply0001.dat", 12) == 0); // string base entry+5
    const auto e = parseRplyIndex(b);
    CHECK(e.size() == kRplyIndexEntries);
    CHECK(e[9].field0 == 7 && e[9].number == 10 && e[9].name == "user\\rply0010.dat");
    CHECK(serializeRplyIndex(e) == b); // fixed table round-trips (zero pad preserved)
    CHECK_THROWS(parseRplyIndex(Bytes(2499)));
}

int main() {
    testConstants();
    testRecordFraming();
    testFileRoundTrip();
    testIndex();
    std::puts("test_rply: ok");
    return 0;
}
