// Derived from fifa99-native (same author), ported for World Cup 98
// wc98-audio-extract: decodes every WC98 audio bank sound, SCHl stream (.ASF music, .DCT
// video audio), PT play track (.SMP / .PBK members), MIDx sequence and CRDl structure of a
// World Cup 98 tree to WAV, and writes manifest.json describing each output.
//
// WC98 deltas (research-inbox/F99_REUSABILITY_MATRIX.md section 7 + 10):
//   * .DCT video files carry their audio in the SCHl/PT head sub-stream: the audio half is
//     decoded here, frame decode (EA-TREM mTCD) is not — reported as a gap,
//   * .SMP / .PBK use PT version 2 play tracks (decodePlayTrack); MicroTalk amplitudes are
//     approximate (1998 15-bit header/gain layout not calibrated),
//   * WC98 has no .MAD video, no .STR/.OFF/SPEECH.INF commentary index and no
//     PHRS phrase-bank index reader — the speech index layer stays an open item.
#include "common/json.hpp"
#include "wc98/reader.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/bnk.hpp"
#include "wc98/formats/crd.hpp"
#include "wc98/formats/midx.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/formats/schl.hpp"
#include "wc98/log.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace wc98::formats;
using wc98::tools::jstr;

namespace {

struct Job {
    std::string source;              // path relative to the game root
    fs::path out;                    // relative output path ("" = no file)
    std::string extra;               // pre-rendered JSON members (",\"k\":v...")
    std::function<std::string(const fs::path& absOut)> run; // returns JSON members describing the output
};

std::string upper(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return s;
}

std::string pcmJson(const Pcm& p, const PtHeader& h, std::size_t offset) {
    std::string s = std::format(",\"offset\":{},\"rate\":{},\"channels\":{},\"samples\":{},\"codec\":{}", offset, p.sampleRate,
                                p.channels, p.frames(), jstr(codecName(h.codec())));
    if (h.loopStart) s += std::format(",\"loopStart\":{}", *h.loopStart);
    if (h.loopEnd) s += std::format(",\"loopEnd\":{}", *h.loopEnd);
    if (h.v2) s += std::format(",\"ptVersion\":2,\"ptHeaderSize\":{}", h.headerSize);
    if (h.codec() == EaCodec::MicroTalk)
        s += ",\"levelsApprox\":true"; // WC98 UTK gain calibration is open (matrix section 7)
    return s;
}

void writeWav(const fs::path& p, const Pcm& pcm) { wc98::writeFile(p, toWav(pcm)); }

using Data = std::shared_ptr<const std::vector<std::uint8_t>>;

// SCHl container streams: .ASF music; the .DCT cinematics carry their audio the same way
// (matrix section 7: 8/8 .ASF + 29/29 .DCT audio decode), frames are a separate codec.
void addSchl(std::vector<Job>& jobs, const fs::path& abs, const std::string& rel, const fs::path& outDir) {
    Data d = std::make_shared<std::vector<std::uint8_t>>(wc98::readFile(abs));
    auto streams = scanSchl(*d);
    for (std::size_t i = 0; i < streams.size(); ++i) {
        const std::string stem = streams.size() == 1 ? std::string("stream") : std::format("s{:05}", i);
        Job j{rel, outDir / (stem + ".wav"), std::format(",\"stream\":{}", i), {}};
        j.run = [d, s = streams[i]](const fs::path& o) {
            auto pcm = decodeSchl(*d, s);
            writeWav(o, pcm);
            return pcmJson(pcm, s.header, s.offset);
        };
        jobs.push_back(std::move(j));
    }
}

void addBnk(std::vector<Job>& jobs, const fs::path& abs, const std::string& rel, const fs::path& outDir) {
    Data d = std::make_shared<std::vector<std::uint8_t>>(wc98::readFile(abs));
    auto bank = Bnk::parse(*d);
    for (auto& s : bank.sounds) {
        Job j{rel, outDir / std::format("slot{:03}.wav", s.slot), std::format(",\"slot\":{}", s.slot), {}};
        j.run = [d, s](const fs::path& o) {
            auto pcm = Bnk::decode(*d, s);
            writeWav(o, pcm);
            return pcmJson(pcm, s.header, *s.header.dataOffset);
        };
        jobs.push_back(std::move(j));
    }
}

// WC98 (matrix section 7): a standalone .SMP is one PT v2 play track at offset 0.
void addPlayTrack(std::vector<Job>& jobs, const fs::path& abs, const std::string& rel, const fs::path& outDir) {
    Data d = std::make_shared<std::vector<std::uint8_t>>(wc98::readFile(abs));
    Job j{rel, outDir / "stream.wav", ",\"kind\":\"playtrack\"", {}};
    j.run = [d](const fs::path& o) {
        const PtHeader h = PtHeader::parse(*d, 0);
        auto pcm = decodePlayTrack(*d, 0);
        writeWav(o, pcm);
        return pcmJson(pcm, h, *h.dataOffset);
    };
    jobs.push_back(std::move(j));
}

// WC98 (matrix section 3 + 7): a .PBK is a 48-byte prefix, then BIGF; members are PT play
// tracks, and the phrase bank (PHRS.PBK) nests one more BIGF level per phrase. The phrase
// INDEX (PHRS.INF / .HDR / .TYPE) has no reader yet — that mapping stays an open item.
void addPlayBank(std::vector<Job>& jobs, Data d, const std::string& rel, const fs::path& outDir, int depth, std::size_t prefix) {
    // WC98 .PBK files put BIGF behind a 48-byte prefix (0x30), and a minority at offset 0
    // (matrix section 2/3); nested phrase banks (PHRS.PBK members) are plain BIGF.
    // Each member is a 20-byte phrase record followed by a "PT" v2 play track (gate test_audio
    // walks all 6,964 members this way); RefPack-compressed members expand first; PHRS.PBK nests
    // second-level BIG containers. Mirrors tests/unit/test_audio.cpp walkBig.
    const std::span<const std::uint8_t> body(d->data() + prefix, d->size() - prefix);
    const auto big = BigArchive::parse(body);
    for (std::size_t i = 0; i < big.entries.size(); ++i) {
        const auto raw = BigArchive::payload(body, big.entries[i]);
        const Data m = std::make_shared<std::vector<std::uint8_t>>(refpack::decompressIfNeeded(raw));
        std::string sub = std::format("{}#{}#{}", rel, i, big.entries[i].name);
        fs::path subDir = outDir / std::format("m{:05}", i);
        if (m->size() > 24 && (*m)[20] == 'P' && (*m)[21] == 'T') {
            Job j{sub, subDir / "stream.wav", ",\"kind\":\"playtrack\",\"member\":true", {}};
            j.run = [m](const fs::path& o) {
                const std::span<const std::uint8_t> member(m->data(), m->size());
                const PtHeader h = PtHeader::parse(member, 20);
                auto pcm = decodePlayTrack(member, 20);
                writeWav(o, pcm);
                return pcmJson(pcm, h, *h.dataOffset);
            };
            jobs.push_back(std::move(j));
        } else if (m->size() > 4 && (*m)[0] == 'B' && (*m)[1] == 'I' && (*m)[2] == 'G') {
            if (depth > 0) addPlayBank(jobs, m, sub, subDir, depth - 1, 0);
        }
    }
}

std::string midxJson(const Midx& m) { return std::format(",\"kind\":\"midx\",\"events\":{},\"endTick\":{}", m.events.size(), m.endTick); }

void addStructures(std::vector<Job>& jobs, const fs::path& abs, const std::string& rel) {
    auto d = wc98::readFile(abs);
    std::string ext = upper(abs.extension().string());
    if (ext == ".CRD") {
        auto c = Crd::parse(d);
        jobs.push_back({rel, {}, std::format(",\"kind\":\"crd\",\"records\":{},\"tables\":{}", c.records.size(), c.tables.size()), nullptr});
    } else if (ext == ".EAM") {
        jobs.push_back({rel, {}, midxJson(Midx::parse(d)), nullptr});
    } else if (BigArchive::isBig(d)) { // crowd BIGs: MIDx sequences, possibly nested one level
        std::function<void(std::span<const std::uint8_t>, const std::string&)> walk = [&](auto data, const std::string& path) {
            auto a = BigArchive::parse(data);
            for (std::size_t i = 0; i < a.entries.size(); ++i) {
                auto p = BigArchive::payload(data, a.entries[i]);
                std::string sub = std::format("{}#{}#{}", path, i, a.entries[i].name);
                if (BigArchive::isBig(p)) walk(p, sub);
                else if (p.size() >= 4 && std::equal(p.begin(), p.begin() + 4, "MIDx")) jobs.push_back({sub, {}, midxJson(Midx::parse(p)), nullptr});
            }
        };
        walk(d, rel);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: wc98-audio-extract <wc98-dir> [out-dir] [--threads N] [--max-pbk-members N]\n"
                             "  default out-dir: /tmp/wc98port-out/audio; .PBK member decode is capped per file\n"
                             "  (default 64) so the 88 MiB PHRS.PBK phrase bank does not flood the output\n");
        return 2;
    }
    fs::path root = argv[1], out = "/tmp/wc98port-out/audio";
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    int maxPbkMembers = 64;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--threads" && i + 1 < argc) threads = unsigned(std::max(1, std::stoi(argv[++i])));
        else if (a == "--max-pbk-members" && i + 1 < argc) maxPbkMembers = std::stoi(argv[++i]);
        else out = a;
    }

    std::vector<fs::path> files;
    for (auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) files.push_back(e.path());
    std::ranges::sort(files);

    std::vector<Job> jobs;
    std::vector<std::string> errors;
    for (auto& abs : files) {
        std::string rel = fs::relative(abs, root).generic_string();
        std::string ext = upper(abs.extension().string());
        fs::path outDir = out / fs::path(rel).replace_extension("");
        try {
            if (ext == ".ASF" || ext == ".DCT") addSchl(jobs, abs, rel, outDir);
            else if (ext == ".BNK") addBnk(jobs, abs, rel, outDir);
            else if (ext == ".SMP") addPlayTrack(jobs, abs, rel, outDir);
            else if (ext == ".PBK") {
                // 48-byte prefix before BIGF (matrix section 3); members are PT play tracks.
                auto raw = std::make_shared<std::vector<std::uint8_t>>(wc98::readFile(abs));
                const std::size_t prefix = BigArchive::isBig(*raw) ? 0 : 0x30;
                const std::size_t before = jobs.size();
                addPlayBank(jobs, raw, rel, outDir, /*depth=*/1, prefix);
                if (int n = int(jobs.size() - before); n > maxPbkMembers) jobs.resize(std::size_t(before) + std::size_t(maxPbkMembers));
            } else if (ext == ".CRD" || ext == ".EAM" || (ext == ".BIG" && upper(abs.parent_path().filename().string()) == "CRWD"))
                addStructures(jobs, abs, rel);
        } catch (const std::exception& ex) {
            errors.push_back(std::format("{}: {}", rel, ex.what()));
        }
    }

    std::vector<std::string> results(jobs.size());
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> failed{0};
    {
        std::vector<std::jthread> pool;
        for (unsigned t = 0; t < threads; ++t)
            pool.emplace_back([&] {
                for (std::size_t i; (i = next++) < jobs.size();) {
                    auto& j = jobs[i];
                    std::string r = std::format("{{\"source\":{}", jstr(j.source));
                    if (!j.out.empty()) r += std::format(",\"file\":{}", jstr(fs::relative(j.out, out).generic_string()));
                    r += j.extra;
                    try {
                        if (j.run) r += j.run(j.out);
                    } catch (const std::exception& ex) {
                        r += std::format(",\"error\":{}", jstr(ex.what()));
                        ++failed;
                    }
                    results[i] = r + "}";
                }
            });
    }

    std::string manifest = "{\"entries\":[\n";
    for (std::size_t i = 0; i < results.size(); ++i) manifest += results[i] + (i + 1 < results.size() ? ",\n" : "\n");
    manifest += "],\"errors\":[";
    for (std::size_t i = 0; i < errors.size(); ++i) manifest += (i ? "," : "") + jstr(errors[i]);
    manifest += "]}\n";
    wc98::writeFile(out / "manifest.json", std::span(reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size()));
    for (auto& e : errors) wc98::log::error("{}", e);
    wc98::log::info("{} entries, {} failed, {} file errors -> {}", jobs.size(), failed.load(), errors.size(), (out / "manifest.json").string());
    return failed || !errors.empty() ? 1 : 0;
}
