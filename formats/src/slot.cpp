#include "wc98/formats/slot.hpp"

#include <format>

#include "wc98/file.hpp"
#include "wc98/formats/saveframe.hpp"

namespace wc98::formats {

std::string slotFileName(int slot) { return std::format("SLOT{:02d}.SAV", slot); }

SlotSave SlotSave::parse(std::span<const std::uint8_t> fileBytes) {
    const auto stream = SaveFrameStream::parse(fileBytes);
    for (const auto& c : stream.chunks()) {
        if (c.tag != kSlotChunkTag) continue;
        if (c.version != kSlotFormatId)  // FUN_0047b67c: u32@rec+4 == 0x10000 gate
            throw FormatError("slot: format id " + std::format("0x{:X}", c.version) +
                              " != 0x10000");
        if (c.payload.size() < kSlotSummaryBytes)
            throw FormatError("slot: payload shorter than summary record");
        SlotSave s;
        Reader r(c.payload);
        for (auto& f : s.summary) f = r.u32le();
        const auto tail = r.bytes(c.payload.size() - kSlotSummaryBytes);
        s.image.assign(tail.begin(), tail.end());
        return s;
    }
    throw FormatError("slot: no SLOT record in stream");
}

std::vector<std::uint8_t> SlotSave::serialize() const {
    SaveChunk c{kSlotChunkTag, kSlotFormatId, {}};
    for (const std::uint32_t f : summary) {
        c.payload.push_back(std::uint8_t(f));
        c.payload.push_back(std::uint8_t(f >> 8));
        c.payload.push_back(std::uint8_t(f >> 16));
        c.payload.push_back(std::uint8_t(f >> 24));
    }
    c.payload.insert(c.payload.end(), image.begin(), image.end());
    SaveFrameStream stream;
    stream.add(std::move(c));
    return stream.serialize();
}

std::vector<SlotScanEntry> scanSlots(const std::filesystem::path& dir) {
    std::vector<SlotScanEntry> out;
    for (int i = 0; i < kSlotCount; ++i) {
        SlotScanEntry e;
        e.slot = i;
        try {
            const auto bytes = readFile(dir / slotFileName(i));
            const SlotSave s = SlotSave::parse(bytes);
            e.valid = true;
            e.summary = s.summary;
        } catch (const std::exception&) {  // open/parse/header failure -> invalid slot
        }
        out.push_back(e);
    }
    return out;
}

}  // namespace wc98::formats
