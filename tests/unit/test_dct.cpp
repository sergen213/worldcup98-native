// EA .DCT movie container + SRE v2 video plane (docs/asset-formats/VIDEO.md): tree gate over all
// 30 VIDEO/*.DCT files (SCHl/mTCD framing walk, fixed 320x240 dims), full-stream decode of
// INT_FIFA.DCT with deterministic non-zero sampled pixel checksums (two runs), SEQ_OPEN.DCT smoke.
#include "check.hpp"
#include <algorithm> // std::sort on the file list
#include "wc98/file.hpp"
using wc98::readFile;
#include "wc98/formats/dct.hpp"

#include <filesystem>
#include <string>
#include <vector>

using namespace wc98::formats;

static std::uint64_t fnv1a(const std::uint8_t* p, std::size_t n) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

// FNV over sampled pixels of the luma plane + RGBA corners: deterministic, non-zero.
static std::uint64_t frameSampleSum(const DctFrame& f) {
    std::uint64_t h = 0;
    const int pts[][2] = {{8, 8}, {97, 53}, {160, 120}, {319, 239}, {172, 130}};
    for (const auto& p : pts) h = h * 31 + f.y[std::size_t(p[1]) * f.width + p[0]];
    std::vector<std::uint8_t> rgba;
    dctToRgba(f, rgba);
    for (const auto& p : pts) {
        const std::size_t o = (std::size_t(p[1]) * f.width + p[0]) * 4;
        h = h * 31 + fnv1a(rgba.data() + o, 4);
    }
    return h;
}

int main() {
    const auto tree = treeDirOrSkip();
    const auto vid = tree / "VIDEO";
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(vid))
        if (e.path().extension() == ".DCT") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    CHECK(files.size() == 29); // 23 flyovers + INT_EASP/INT_FIFA + SEQ_OPEN/CLAS/CLOS/QUAL

    int totalFrames = 0;
    for (const auto& f : files) {
        const auto bytes = readFile(f);
        const auto frames = dctScan(bytes);
        CHECK(frames.size() >= 47); // smallest movie INT_EASP.DCT has 47 mTCD chunks
        for (const auto& h : frames) {
            CHECK(h.width == 320 && h.height == 240); // every tree video is a fixed 320x240 stream
            CHECK(h.size >= 16 && h.offset + h.size <= bytes.size());
        }
        totalFrames += int(frames.size());
    }
    std::printf("dct: %zu files, %d frames\n", files.size(), totalFrames);

    // INT_FIFA.DCT: decode the full stream; sampled pixels must be non-zero and reproducible.
    const auto fifa = readFile(vid / "INT_FIFA.DCT");
    const auto hdrs = dctScan(fifa);
    CHECK(hdrs.size() > 1);
    std::uint64_t sum1 = 0;
    int nzFrames = 0;
    for (std::size_t i = 0; i < hdrs.size(); ++i) {
        const DctFrame f = dctDecode({fifa.data() + hdrs[i].offset, hdrs[i].size}, hdrs[i]);
        if (i == 0) CHECK(f.y[0] != 0); // first frame is intra: carries real luma data
        if (frameSampleSum(f) != 0) ++nzFrames;
        sum1 = sum1 * 31 + frameSampleSum(f);
    }
    CHECK(nzFrames > int(hdrs.size()) / 2); // decode generalizes: majority of frames carry data
    std::uint64_t sum2 = 0;
    for (const auto& h : hdrs) {
        const DctFrame f = dctDecode({fifa.data() + h.offset, h.size}, h);
        sum2 = sum2 * 31 + frameSampleSum(f);
    }
    CHECK(sum1 != 0 && sum1 == sum2); // deterministic across two full-stream runs
    std::printf("dct: INT_FIFA %zu frames, checksum=%llu\n", hdrs.size(), (unsigned long long)sum1);

    // Second-file smoke: SEQ_OPEN.DCT decodes with the same framing rules.
    const auto seq = readFile(vid / "SEQ_OPEN.DCT");
    const auto shdr = dctScan(seq);
    CHECK(shdr.size() >= 47 && shdr.front().width == 320);
    const DctFrame f0 = dctDecode({seq.data() + shdr[0].offset, shdr[0].size}, shdr[0]);
    CHECK(frameSampleSum(f0) != 0);
    return 0;
}
