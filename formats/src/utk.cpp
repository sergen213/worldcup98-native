// Derived from fifa99-native (same author), ported for World Cup 98
// EA MicroTalk 10:1 decoder. Bitstream semantics follow the public reverse-engineering of the
// codec (Andrew D'Addesio's utkdecode, as also used by vgmstream); reimplemented here. The
// FIFA 99 block framing (leading flag byte) was established from the data: decoding every block of
// the original .STR files consumes exactly the block payload (see tests/test_audio.cpp).
#include "wc98/formats/utk.hpp"
#include "wc98/reader.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace wc98::formats::utk {
namespace {

// Reflection-coefficient quantiser (mirrored: t[64-i] = -t[i]); coefficients 4..11 use the
// 5-bit middle range 16..47. Values as published by vgmstream (utkdec.c) from the EA executables.
constexpr float kRcTable[64] = {
    +0.000000f, -0.996776f, -0.990327f, -0.983879f, -0.977431f, -0.970982f, -0.964534f, -0.958085f,
    -0.951637f, -0.930754f, -0.904960f, -0.879167f, -0.853373f, -0.827579f, -0.801786f, -0.775992f,
    -0.750198f, -0.724405f, -0.698611f, -0.670635f, -0.619048f, -0.567460f, -0.515873f, -0.464286f,
    -0.412698f, -0.361111f, -0.309524f, -0.257937f, -0.206349f, -0.154762f, -0.103175f, -0.051587f,
    +0.000000f, +0.051587f, +0.103175f, +0.154762f, +0.206349f, +0.257937f, +0.309524f, +0.361111f,
    +0.412698f, +0.464286f, +0.515873f, +0.567460f, +0.619048f, +0.670635f, +0.698611f, +0.724405f,
    +0.750198f, +0.775992f, +0.801786f, +0.827579f, +0.853373f, +0.879167f, +0.904960f, +0.930754f,
    +0.951637f, +0.958085f, +0.964534f, +0.970982f, +0.977431f, +0.983879f, +0.990327f, +0.996776f};

// Huffman-coded excitation commands of the multi-pulse model.
enum : int { kNormal = 0, kLarge = 1 };
struct Command { int nextModel; int codeSize; float pulse; };
constexpr Command kCommands[29] = {
    {kLarge, 8, 0}, {kLarge, 7, 0}, {kNormal, 8, 0}, {kNormal, 7, 0},           // 0,1 big pulse; 2,3 zero run
    {kNormal, 2, 0}, {kNormal, 2, -1}, {kNormal, 2, +1}, {kNormal, 3, -1}, {kNormal, 3, +1},
    {kLarge, 4, -2}, {kLarge, 4, +2}, {kLarge, 3, -2}, {kLarge, 3, +2},
    {kLarge, 5, -3}, {kLarge, 5, +3}, {kLarge, 4, -3}, {kLarge, 4, +3},
    {kLarge, 6, -4}, {kLarge, 6, +4}, {kLarge, 5, -4}, {kLarge, 5, +4},
    {kLarge, 7, -5}, {kLarge, 7, +5}, {kLarge, 6, -5}, {kLarge, 6, +5},
    {kLarge, 8, -6}, {kLarge, 8, +6}, {kLarge, 7, -6}, {kLarge, 7, +6}};

// Prefix codes (LSB-first values) per model: {command, codeword}.
struct Code { int cmd; unsigned word; };
constexpr Code kNormalCodes[] = {{4, 0}, {6, 1}, {5, 2}, {9, 3}, {10, 11}, {13, 7}, {14, 23}, {17, 15}, {18, 47},
                                 {21, 31}, {22, 95}, {25, 63}, {26, 191}, {0, 127}, {2, 255}};
constexpr Code kLargeCodes[] = {{4, 0}, {11, 1}, {7, 2}, {12, 5}, {8, 6}, {15, 3}, {16, 11}, {19, 7}, {20, 23},
                                {23, 15}, {24, 47}, {27, 31}, {28, 95}, {1, 63}, {3, 127}};

struct Codebooks {
    std::uint8_t lut[2][256]{};
    constexpr Codebooks() {
        auto fill = [&](int model, const auto& codes) {
            for (unsigned v = 0; v < 256; ++v)
                for (const auto& c : codes) {
                    unsigned mask = (1u << kCommands[c.cmd].codeSize) - 1;
                    if ((v & mask) == c.word) { lut[model][v] = std::uint8_t(c.cmd); break; }
                }
        };
        fill(kNormal, kNormalCodes);
        fill(kLarge, kLargeCodes);
    }
};
constexpr Codebooks kBooks;

} // namespace

unsigned Decoder::bits(int n) {
    unsigned r = value_ & ((1u << n) - 1);
    value_ >>= n;
    count_ -= n;
    consumed_ += std::size_t(n);
    if (count_ < 8) {
        unsigned byte = pos_ < in_.size() ? in_[pos_] : 0; // reference decoder reads zeros past the end
        ++pos_;
        value_ |= byte << count_;
        count_ += 8;
    }
    return r;
}

void Decoder::excitation(bool multipulse, float* out, int stride) {
    int i = 0;
    if (multipulse) {
        int model = kNormal;
        while (i < 108) {
            int cmd = kBooks.lut[model][value_ & 0xFF];
            model = kCommands[cmd].nextModel;
            bits(kCommands[cmd].codeSize);
            if (cmd > 3) {
                out[i] = kCommands[cmd].pulse;
                i += stride;
            } else if (cmd > 1) {
                int run = 7 + int(bits(6));
                if (i + run * stride > 108) run = (108 - i) / stride;
                for (; run > 0; --run, i += stride) out[i] = 0.0f;
            } else {
                int x = 7;
                while (bits(1)) ++x;
                if (!bits(1)) x = -x;
                out[i] = float(x);
                i += stride;
            }
        }
    } else {
        for (; i < 108; i += stride) out[i] = !bits(1) ? 0.0f : !bits(1) ? -2.0f : 2.0f;
    }
}

void Decoder::synthesis(int offset, int blocks) {
    // Direct-form predictor from the reflection coefficients, computed the way the EA decoder
    // does it (impulse response of the lattice); a textbook step-up recursion differs by ~1e-3 in
    // float, so the original procedure is kept for faithful output. y[n] = e[n] + sum a[k] y[n-1-k].
    const float* rc = &m_[kRc];
    float a[12], resp[12], lat[12];
    for (int i = 10; i >= 0; --i) lat[i + 1] = rc[i];
    lat[0] = 1.0f;
    for (int i = 0; i < 12; ++i) {
        float x = -(rc[11] * lat[11]);
        for (int j = 10; j >= 0; --j) {
            x -= rc[j] * lat[j];
            lat[j + 1] = x * rc[j] + lat[j];
        }
        lat[0] = resp[i] = x;
        for (int j = 0; j < i; ++j) x -= resp[i - 1 - j] * a[j];
        a[i] = x;
    }
    float* hist = &m_[kHist]; // circular: hist[0] = newest sample at block boundaries
    float* p = &m_[kFrame + offset];
    for (int b = 0; b < blocks; ++b)
        for (int j = 0; j < 12; ++j) {
            float x = *p;
            for (int k = 0; k < j; ++k) x += a[k] * hist[k - j + 12];
            for (int k = j; k < 12; ++k) x += a[k] * hist[k - j];
            hist[11 - j] = x;
            *p++ = x;
        }
}

void Decoder::decodeFrame() {
    float exc[5 + 108 + 5] = {};
    float rcDelta[12];
    bool multipulse = false;
    for (int i = 0; i < 12; ++i) {
        int idx;
        if (i == 0) {
            idx = int(bits(6));
            multipulse = idx < multipulseThresh_;
        } else if (i < 4) {
            idx = int(bits(6));
        } else {
            idx = 16 + int(bits(5));
        }
        rcDelta[i] = (kRcTable[idx] - m_[kRc + i]) * 0.25f;
    }
    for (int sf = 0; sf < 4; ++sf) {
        int lag = int(bits(8));
        float pitchGain = float(bits(4)) / 15.0f;
        float fixedGain = m_[kGains + bits(6)];
        if (!reducedBw_) {
            excitation(multipulse, &exc[5], 1);
        } else {
            // Every other sample is coded; the rest are zeroed (spectral mirror) or interpolated.
            int align = int(bits(1));
            bool zero = bits(1);
            excitation(multipulse, &exc[5 + align], 2);
            float* q = &exc[5 + (1 - align)];
            if (zero) {
                for (int j = 0; j < 108; j += 2) q[j] = 0.0f;
            } else {
                for (int j = 0; j < 108; j += 2)
                    q[j] = q[j - 5] * 0.01803267933428287506103515625f - q[j - 3] * 0.114591561257839202880859375f +
                           q[j - 1] * 0.597385942935943603515625f + q[j + 1] * 0.597385942935943603515625f -
                           q[j + 3] * 0.114591561257839202880859375f + q[j + 5] * 0.01803267933428287506103515625f;
                fixedGain *= 0.5f;
            }
        }
        for (int j = 0; j < 108; ++j)
            m_[kFrame + 108 * sf + j] = fixedGain * exc[5 + j] + pitchGain * m_[kAdapt + 108 * sf + 216 - lag + j];
    }
    std::copy_n(m_.data() + kFrame + 108, 324, m_.data() + kAdapt);
    for (int sf = 0; sf < 4; ++sf) {
        for (int j = 0; j < 12; ++j) m_[kRc + j] += rcDelta[j];
        synthesis(12 * sf, sf < 3 ? 1 : 33);
    }
}

std::size_t Decoder::decodeBlock(std::span<const std::uint8_t> payload, std::size_t samples, std::vector<std::int16_t>& out) {
    if (samples == 0) throw FormatError("MicroTalk block with 0 samples");
    if (payload.size() < 2) throw FormatError("MicroTalk block too short");
    std::uint8_t flag = payload[0];
    if (flag > 1) throw FormatError(std::format("MicroTalk block flag {} unknown", flag));
    in_ = payload;
    pos_ = 2;
    value_ = payload[1];
    count_ = 8;
    consumed_ = 8; // flag byte
    if (flag == 1) {
        reducedBw_ = bits(1);
        multipulseThresh_ = 32 - int(bits(4));
        m_[kGains] = 8.0f * float(1 + bits(4));
        float mul = 1.04f + float(bits(6)) * 0.001f;
        for (int i = 1; i < 64; ++i) m_[kGains + i] = m_[kGains + i - 1] * mul;
        haveHeader_ = true;
    } else if (!haveHeader_) {
        throw FormatError("MicroTalk stream starts without header block");
    }
    std::size_t frames = (samples + 431) / 432; // a stream's last block may end mid-frame
    for (std::size_t f = 0; f < frames; ++f) {
        decodeFrame();
        int n = int(std::min<std::size_t>(432, samples - f * 432));
        for (int i = 0; i < n; ++i)
            out.push_back(std::int16_t(std::clamp(std::lround(m_[kFrame + i]), -32768L, 32767L)));
    }
    if (consumed_ > payload.size() * 8)
        throw FormatError(std::format("MicroTalk block overrun: {} bits used, {} available", consumed_, payload.size() * 8));
    return consumed_;
}

std::size_t Decoder::decodeSpeechPayload(std::span<const std::uint8_t> payload, std::size_t samples,
                                         std::vector<std::int16_t>& out) {
    // WC98 (matrix section 7): prepend the FIFA 99 first-block flag byte (0x01) in front of
    // the 1998 payload, then decode it as a single block. See the header note for the
    // amplitude caveat (measured int16 saturation on every WC98 clip probed).
    if (payload.empty() || samples == 0) throw FormatError("MicroTalk: empty WC98 speech payload");
    std::vector<std::uint8_t> framed;
    framed.reserve(payload.size() + 1);
    framed.push_back(1);
    framed.insert(framed.end(), payload.begin(), payload.end());
    return decodeBlock(framed, samples, out);
}

} // namespace wc98::formats::utk
