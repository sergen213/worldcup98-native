// EA DCT video (.DCT): SCHl container of mTCD frame chunks, each carrying an
// EA SRE (PlayStation MDEC family) bitstream; MPEG-1 VLC tables + EA scan swap,
// byte-verified on tree renders. docs/asset-formats/VIDEO.md; evidence
// analysis/pass8/dct_*.txt (Ghidra pass 8).
#include "wc98/formats/dct.hpp"
#include "wc98/reader.hpp"   // FormatError
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace wc98::formats {
namespace {

constexpr std::uint32_t fourcc(const char* s) {
    return (std::uint32_t(s[0]) << 24) | (std::uint32_t(s[1]) << 16) |
           (std::uint32_t(s[2]) << 8) | std::uint32_t(s[3]);
}

// MSB-first bit reader over the byte-swapped (16-bit word) payload; verified
// against INT_FIFA.DCT frame renders (docs/asset-formats/VIDEO.md).
class Bits {
public:
    explicit Bits(std::span<const std::uint8_t> d) : d_(d), bits_((long long)d.size() * 8) {}
    bool exhausted() const { return p_ + 16 > bits_; }   // padding tail guard
    void skip(int n) { p_ += n; }
    int get(int n) { int v = 0; for (int i = 0; i < n; ++i) v = (v << 1) | at(p_++); return v; }
    int peek(int n) const { int v = 0; for (int i = 0; i < n; ++i) v = (v << 1) | at(p_ + i); return v; }
    void consume(int n) { p_ += n; }
private:
    int at(long long b) const { return b < bits_ ? (d_[b / 8] >> (7 - b % 8)) & 1 : 0; }
    std::span<const std::uint8_t> d_;
    long long p_ = 0, bits_ = 0;
};

// EA SRE intra dequant matrix, raster order (values >63 clamped); byte-level
// verified against INT_FIFA.DCT frame renders (docs/asset-formats/VIDEO.md).
constexpr std::array<int, 64> kMat = {
     8, 16, 19, 26, 37, 56, 60, 63, 16, 19, 26, 37, 56, 60, 63, 63,
    19, 26, 37, 56, 60, 63, 63, 63, 26, 37, 56, 60, 63, 63, 63, 63,
    37, 56, 60, 63, 63, 63, 63, 63, 56, 60, 63, 63, 63, 63, 63, 63,
    60, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};

// EA SRE coefficient scan: MPEG-1 direct zigzag with the (10,17) pair swapped;
// byte-verified on INT_FIFA.DCT / SEQ_OPEN.DCT renders (docs/asset-formats/VIDEO.md).
constexpr std::array<int, 64> kZigzag = {
     0,  1,  8, 16,  9,  2,  3, 17, 10, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// EA SRE VLC symbol table = MPEG-1 ff_mpeg1_vlc_table (+run/+level) from
// FFmpeg libavcodec/mpeg12data.c (see dct_tables.inc header); EOB/escape
// semantics byte-verified on tree streams. EOB = 112, ESC = 111.
#include "dct_tables.inc"

constexpr int kEsc = 111, kEob = 112;
struct VlcEntry { short idx = -1; short bits = 0; };
// Prefix LUT over 16 peeked bits. The EA table is NOT canonical prefix-free;
// the verified decoder matches longest codeword first, so slots are filled in
// ascending code-width order (longer codes overwrite shorter prefixes).
const std::array<VlcEntry, 65536>& vlcLut() {
    static const std::array<VlcEntry, 65536> lut = [] {
        std::array<VlcEntry, 65536> t{};
        std::array<int, kVlc.size()> order{};
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = int(i);
        for (std::size_t i = 1; i < order.size(); ++i) {   // insertion sort by width
            int key = order[i];
            std::size_t j = i;
            while (j > 0 && kVlc[order[j - 1]].second > kVlc[key].second) { order[j] = order[j - 1]; --j; }
            order[j] = key;
        }
        for (int i : order) {
            auto [code, bits] = kVlc[std::size_t(i)];
            int sh = 16 - bits;
            for (int rep = 0; rep < (1 << sh); ++rep) t[(code << sh) | rep] = {short(i), short(bits)};
        }
        return t;
    }();
    return lut;
}

// EA SRE AC coefficient chain (no DC field; first code carries the DC run):
// LUT codes give (run, signed level) directly; EOB = idx 112 terminates the
// block. Escape codeword "000001" + 5-bit run + 8-bit signed magnitude level
// (MDEC-style; byte-verified on INT_FIFA.DCT / SEQ_OPEN.DCT full streams,
// docs/asset-formats/VIDEO.md). An unmapped tail stops decoding for this
// frame (padding); partial planes are kept. Coefficients land at scan
// position i (raster), dequantized through zigzag + matrix with floor //8.
// Run overflow consumes the code and drops the coefficient. Returns false =
// stop decoding this frame.
bool decodeAcChain(Bits& b, std::array<int, 64>& blk) {
    std::fill(blk.begin(), blk.end(), 0);
    int i = -1;
    for (;;) {
        if (b.exhausted()) return false;        // padding tail: frame done
        if (b.peek(2) == 0b10) { b.consume(2); return true; }  // EOB wins over '10*' codes
        const auto& e = vlcLut()[std::uint16_t(b.peek(16))];
        if (e.idx < 0 || e.idx == kEob) return false;          // no code at tail: stop
        int run, level;
        if (e.idx == kEsc) {                    // "000001" + RRRRR + LLLLLLLL
            b.skip(6);
            run = b.get(5);
            level = b.get(8);
            if (level >= 128) level -= 256;     // signed magnitude on the fly
        } else {
            b.consume(e.bits);
            run = kRun[e.idx];
            level = kLevel[e.idx];
        }
        i += run + 1;
        if (i >= 64) continue;                  // overflow: consume only
        const int a = level * kMat[i];
        blk[kZigzag[i]] = a >= 0 ? a / 8 : -((-a + 7) / 8);   // floor div
    }
}

// Accurate reference float IDCT (JPEG spec) with precomputed cos table,
// coefficient-zero skip and DC-only fast path. out = clip(round(idct)+128).
void idctToPlane(const std::array<int, 64>& c, std::uint8_t* out, int stride) {
    // cos((2x+1)*u*pi/16)
    static const auto cosv = [] {
        std::array<std::array<double, 8>, 8> t{};
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u)
                t[x][u] = std::cos((2 * x + 1) * u * 3.14159265358979 / 16.0);
        return t;
    }();
    static const double cu[8] = {0.7071067812, 1, 1, 1, 1, 1, 1, 1};   // C(u)
    auto clip = [](double v) { return std::uint8_t(std::clamp(int(v + 0.5) + 128, 0, 255)); };
    bool ac = false;
    for (int i = 1; i < 64; ++i) if (c[i]) { ac = true; break; }
    if (!ac) {                                   // DC/8 flat fill
        const std::uint8_t v = clip(c[0] / 8.0);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) out[y * stride + x] = v;
        return;
    }
    double g[64] = {};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            double s = 0;
            for (int v = 0; v < 8; ++v)
                for (int u = 0; u < 8; ++u) {
                    if (!c[v * 8 + u]) continue;
                    s += (cu[u] / 4.0) * cu[v] * c[v * 8 + u] * cosv[x][u] * cosv[y][v];
                }
            g[y * 8 + x] = s;
        }
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) out[y * stride + x] = clip(g[y * 8 + x]);
}

} // namespace

std::vector<DctFrameHeader> dctScan(std::span<const std::uint8_t> data) {
    if (data.size() < 16 || fourcc("SCHl") != (std::uint32_t(data[0]) << 24 | std::uint32_t(data[1]) << 16 |
                                               std::uint32_t(data[2]) << 8 | data[3]))
        throw FormatError("dct: expected SCHl at 0");
    const std::size_t hsize = data[4] | (std::size_t(data[5]) << 8) | (std::size_t(data[6]) << 16) |
                              (std::size_t(data[7]) << 24);
    if (hsize < 12 || hsize > data.size()) throw FormatError("dct: bad SCHl size");
    std::vector<DctFrameHeader> frames;
    for (std::size_t o = hsize; o + 8 <= data.size();) {
        const std::uint32_t tag = std::uint32_t(data[o]) << 24 | std::uint32_t(data[o + 1]) << 16 |
                                  std::uint32_t(data[o + 2]) << 8 | data[o + 3];
        const std::size_t size = data[o + 4] | (std::size_t(data[o + 5]) << 8) |
                                 (std::size_t(data[o + 6]) << 16) | (std::size_t(data[o + 7]) << 24);
        if (size < 8 || o + size > data.size()) break;   // end of well-formed chunks
        if (tag == fourcc("mTCD") && size >= 16) {
            DctFrameHeader h;
            h.offset = o;
            h.size = std::uint32_t(size);
            h.width = data[o + 12] | (int(data[o + 13]) << 8);    // LE u16 @+12
            h.height = data[o + 14] | (int(data[o + 15]) << 8);   // LE u16 @+14
            if (h.width <= 0 || h.height <= 0) throw FormatError("dct: bad dimensions");
            frames.push_back(h);
        }
        o += size;
    }
    if (frames.empty()) throw FormatError("dct: no mTCD chunks");
    return frames;
}

DctFrame dctDecode(std::span<const std::uint8_t> chunk, const DctFrameHeader& hdr) {
    // Payload = bytes after the 16-byte mTCD record; each 16-bit word is byte-swapped (SRE
    // layout), bitstream MSB-first. After a 64-bit payload preamble, an 8x8-pixel cell grid
    // (width/8 x height/8) follows; per cell: 2-bit header ("11" = inter cell + 9-bit MV field
    // skipped), then 6 blocks with a 1-bit present flag each. Blocks 0-3 are luma 4x4 sub-blocks
    // (quadrants ox=(b%2)*4, oy=(b//2)*4 of the cell), block 4 = V, block 5 = U chroma 4x4
    // (analysis/pass8/dct_decomp*.txt VIDEO-module walk; byte-verified renders, docs/asset-formats/VIDEO.md).
    if (chunk.size() < 16) throw FormatError("dct: mTCD chunk too short");
    std::span<const std::uint8_t> data = chunk.subspan(16);
    std::vector<std::uint8_t> sw(data.size());
    for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
        sw[i] = data[i + 1];
        sw[i + 1] = data[i];
    }
    if (data.size() & 1) sw[data.size() - 1] = data[data.size() - 1];
    Bits b(sw);
    b.skip(64);                                   // payload preamble
    const int mbw = hdr.width / 8, mbh = hdr.height / 8;
    DctFrame f;
    f.width = hdr.width; f.height = hdr.height;
    f.y.assign(std::size_t(hdr.width) * hdr.height, 0);   // absent blocks stay black (verified)
    const int cw = mbw * 4, ch = mbh * 4;         // chroma plane dims
    f.u.assign(std::size_t(cw) * ch, 128);
    f.v.assign(std::size_t(cw) * ch, 128);
    for (int by = 0; by < mbh; ++by) {
        for (int bx = 0; bx < mbw; ++bx) {
            if (b.get(2) == 3) b.skip(9);          // inter cell: MV field skipped (intra approx.)
            for (int blkIdx = 0; blkIdx < 6; ++blkIdx) {
                std::array<int, 64> c;
                const bool present = b.get(1) != 0;
                if (present && !decodeAcChain(b, c)) return f;   // stream end: plane partial
                if (!present) continue;            // absent block: luma stays black / chroma neutral
                std::array<std::uint8_t, 64> pix;
                idctToPlane(c, pix.data(), 8);
                auto put = [&](std::vector<std::uint8_t>& pl, int pw, int x0, int y0) {
                    for (int yy = 0; yy < 4 && y0 + yy < std::int32_t(pl.size() / pw); ++yy)
                        for (int xx = 0; xx < 4 && x0 + xx < pw; ++xx)
                            pl[std::size_t(y0 + yy) * pw + x0 + xx] = pix[yy * 8 + xx];
                };
                if (blkIdx < 4) {                  // luma 4x4 quadrant of the 8x8 cell
                    const int ox = (blkIdx % 2) * 4, oy = (blkIdx / 2) * 4;
                    put(f.y, hdr.width, bx * 8 + ox, by * 8 + oy);
                } else if (blkIdx == 4) {          // V chroma
                    put(f.v, cw, bx * 4, by * 4);
                } else {                           // U chroma
                    put(f.u, cw, bx * 4, by * 4);
                }
            }
        }
    }
    return f;
}

void dctToRgba(const DctFrame& f, std::vector<std::uint8_t>& rgba) {
    const int cw = (f.width + 15) / 16 * 8, ch = (f.height + 15) / 16 * 8;
    rgba.assign(std::size_t(f.width) * f.height * 4, 0);
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x) {
            const double Y = f.y[std::size_t(y) * f.width + x];
            const double Cb = f.u[std::size_t(std::min(y / 2, ch - 1)) * cw + std::min(x / 2, cw - 1)];
            const double Cr = f.v[std::size_t(std::min(y / 2, ch - 1)) * cw + std::min(x / 2, cw - 1)];
            auto clamp8 = [](double v) { return std::uint8_t(std::clamp(int(v + 0.5), 0, 255)); };
            // JPEG full-range YCbCr -> RGB (same matrix the exe feeds D3D).
            rgba[std::size_t(y) * f.width * 4 + x * 4 + 0] = clamp8(Y + 1.402 * (Cr - 128));
            rgba[std::size_t(y) * f.width * 4 + x * 4 + 1] = clamp8(Y - 0.34414 * (Cb - 128) - 0.71414 * (Cr - 128));
            rgba[std::size_t(y) * f.width * 4 + x * 4 + 2] = clamp8(Y + 1.772 * (Cb - 128));
            rgba[std::size_t(y) * f.width * 4 + x * 4 + 3] = 255;
        }
}

} // namespace wc98::formats
