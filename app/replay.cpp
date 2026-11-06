// Replay recorder + deterministic re-drive (app/replay.hpp). Playback rebuilds the match from the
// recorded seed and the per-tick controller words, so the final world state hash must equal the
// sealed one — that equality is the gate evidence (docs/asset-formats/RPLY.md native dialect).
#include "replay.hpp"

#include <cstring>
#include <format>
#include <fstream>
#include "livematch.hpp" // MatchBoot: shared exhibition bootstrap for the re-drive
#include "wc98/sim/sim.hpp"
#include "wc98/sim/world.hpp"

namespace wc98::app {
namespace fs = std::filesystem;

void ReplayRecorder::append(std::uint32_t tick, const sim::ControllerState& c) {
    std::vector<std::uint8_t> p(4 + sizeof c);
    std::memcpy(p.data(), &tick, 4);                    // LE tick index (dialect payload header)
    std::memcpy(p.data() + 4, &c, sizeof c);            // controller 0 image verbatim
    payloads_.push_back(std::move(p));
}

formats::RplyFile ReplayRecorder::build(std::uint32_t seed, std::uint64_t finalHash) const {
    formats::RplyFile f;
    f.recordCount = std::uint32_t(payloads_.size());
    f.table[0] = seed;
    f.table[1] = std::uint32_t(payloads_.size());
    f.table[2] = 1;                                     // dialect version
    f.table[3] = std::uint32_t(finalHash);
    f.table[4] = std::uint32_t(finalHash >> 32);
    for (const auto& p : payloads_) f.records.push_back(formats::makeRplyRecord(p));
    return f;
}

ReplayResult replayFile(const fs::path& tree, const fs::path& datPath) {
    std::ifstream in(datPath, std::ios::binary);
    if (!in) throw FormatError("rply: cannot open " + datPath.string());
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const formats::RplyFile f = formats::RplyFile::parse(bytes);

    MatchBoot boot;
    if (!boot.init(tree)) throw FormatError("rply: tree unusable for re-drive");
    sim::SimRunner runner(boot.md, boot.opt, f.table[0]); // recorded seed (dialect table[0])
    auto& w = runner.world();
    sim::Input_assignController(w, 0, 1, 0);              // live-loop binding: controller 0 -> team 1

    ReplayResult res;
    res.recordedHash = (std::uint64_t(f.table[4]) << 32) | f.table[3]; // table[3]=low, [4]=high (build())
    for (const formats::RplyRecord& rec : f.records) {
        if (formats::rplyRecordSize(rec) != rec.size()) throw FormatError("rply: record size mismatch");
        if (rec.size() < 2 + 4 + sizeof(sim::ControllerState)) throw FormatError("rply: short tick record"); // BE16 framing + LE tick + word
        sim::ControllerState c;
        std::memcpy(&c, rec.data() + 2 + 4, sizeof c); // payload follows the BE16 size prefix and tick index
        runner.setController(0, c);
        ++res.ticksPlayed;
        if (!runner.tick()) break;                        // match ended (DAT_0057c604 == 2)
    }
    res.replayedHash = sim::stateHash(w);
    res.matches = res.replayedHash == res.recordedHash;
    return res;
}

int recordToUserDir(const fs::path& dir, const formats::RplyFile& file) {
    std::vector<std::uint8_t> indexBytes(formats::kRplyIndexEntries * formats::kRplyIndexEntryBytes, 0);
    if (std::ifstream ix(dir / "rplyindx.dat", std::ios::binary); ix) {
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(ix), std::istreambuf_iterator<char>()};
        if (bytes.size() == indexBytes.size()) indexBytes = bytes; // exact fixed table, tolerate junk file
    }
    auto entries = formats::parseRplyIndex(indexBytes);

    int slot = 0;
    for (const auto& e : entries) slot = std::max(slot, int(e.number));
    for (const auto& ent : fs::directory_iterator{dir}) { // existing on-disk slots set the next number too
        const std::string n = ent.path().filename().string();
        const std::string d = n.size() == 12 ? n.substr(4, 4) : ""; // rplyindx.dat shares the prefix; digits only
        if (n.starts_with("rply") && !d.empty() && n.ends_with(".dat") && d.find_first_not_of("0123456789") == std::string::npos) slot = std::max(slot, std::stoi(d));
    }
    const int number = (slot + 1) % 10000; // wrap within the four-digit rply%04d.dat pattern
    const std::string name = "rply" + std::format("{:04d}.dat", number); // exe pattern user\rply%04d.dat (0x1604B8)
    std::ofstream out(dir / name, std::ios::binary);
    const std::vector<std::uint8_t> bytes = file.serialize();
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));

    formats::RplyIndexEntry e{0, std::uint8_t(number), name};
    entries[std::size_t(number) - 1] = e;
    const std::vector<std::uint8_t> idx = formats::serializeRplyIndex(entries);
    std::ofstream(dir / "rplyindx.dat", std::ios::binary).write(reinterpret_cast<const char*>(idx.data()), std::streamsize(idx.size()));
    return number;
}

} // namespace wc98::app
