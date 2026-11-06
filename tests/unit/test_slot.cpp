// Save framing + SLOT%.2d.SAV codec gate (docs/reverse-engineering/SAVESLOT_WC98.md):
// CRC-16 vector, framing round-trip identity, payload-CRC corruption rejection, slot summary
// header id 0x10000, byte-exact serialize(parse(x)) == x, FUN_0047b67c-style slot scan.
#include "check.hpp"

#include <cstring>
#include <filesystem>
#include <vector>
#include <unistd.h>

#include "wc98/file.hpp"
#include "wc98/formats/saveframe.hpp"
#include "wc98/formats/slot.hpp"

using namespace wc98::formats;
using Bytes = std::vector<std::uint8_t>;

// CRC-16/ARC published check value: crc("123456789", seed 0) == 0xBBDA. Seed 0xFBEA is the
// FUN_004c8070 initial; identity of a no-op walk over empty data is the seed itself.
static void testCrc() {
    const char msg[] = "123456789";
    const std::span<const std::uint8_t> data{reinterpret_cast<const std::uint8_t*>(msg), 9};
    CHECK(crc16SaveFormat(data, 0) == 0xBB3D);
    CHECK(crc16SaveFormat({}, kCrcSeed) == kCrcSeed);
}

static void testFraming() {
    SaveFrameStream s;
    s.add(SaveChunk{fourcc('S', 'L', 'O', 'T'), 0x10000u, Bytes{1, 2, 3, 4}});
    s.add(SaveChunk{fourcc('A', 'B', 'C', 'D'), 7u, Bytes(300, 0xAB)});
    const auto bytes = s.serialize();
    const auto back = SaveFrameStream::parse(bytes);
    CHECK(back.serialize() == bytes);  // round-trip identity
    CHECK(back.chunks().size() == 2 && back.chunks()[1].payload.size() == 300);

    Bytes corrupt = bytes;
    corrupt[16] ^= 0xFF;  // flip a payload byte of the first chunk (header is 12 bytes)
    CHECK_THROWS(SaveFrameStream::parse(corrupt));
}

static void testSlot() {
    SlotSave s;
    s.summary = {1u, 48u, 16u, 0x123u, 0x98D19A8u, 0u};
    s.image.assign({0xDE, 0xAD, 0xBE, 0xEF});
    const auto bytes = s.serialize();
    const SlotSave back = SlotSave::parse(bytes);
    CHECK(back.summary == s.summary && back.image == s.image);

    const SlotSave empty;
    CHECK(SlotSave::parse(empty.serialize()).image.empty());

    Bytes badVersion = bytes;
    badVersion[4] ^= 0x01;  // chunk version low byte: reject format id != 0x10000
    CHECK_THROWS(SlotSave::parse(badVersion));

    CHECK(slotFileName(7) == "SLOT07.SAV");
    CHECK(slotFileName(19) == "SLOT19.SAV");
}

static void testScan() {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("wc98_slot_scan_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    SlotSave s;
    s.summary = {1u, 6u, 0u, 0xFFFFFFFFu, 42u, 0u};
    wc98::writeFile(dir / slotFileName(3), s.serialize());
    wc98::writeFile(dir / slotFileName(1), Bytes{'j', 'u', 'n', 'k'});

    const auto scan = scanSlots(dir);
    CHECK(scan.size() == static_cast<std::size_t>(kSlotCount));
    CHECK(!scan[0].valid && !scan[1].valid && scan[3].valid);
    CHECK(scan[3].summary == s.summary);

    std::filesystem::remove_all(dir);
}

int main() {
    testCrc();
    testFraming();
    testSlot();
    testScan();
    return 0;
}
