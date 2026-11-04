// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/schl.hpp"
#include "wc98/reader.hpp"
#include "wc98/formats/utk.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <string_view>

namespace wc98::formats {
namespace {

constexpr std::uint32_t id(const char (&s)[5]) {
    return std::uint32_t(std::uint8_t(s[0])) << 24 | std::uint32_t(std::uint8_t(s[1])) << 16 | std::uint32_t(std::uint8_t(s[2])) << 8 | std::uint8_t(s[3]);
}

constexpr std::int32_t kXaCoef[4][2] = {{0, 0}, {240, 0}, {460, -208}, {392, -220}};

std::int16_t xaSample(int nibble, int shiftNibble, int coef, std::int32_t* h) {
    std::int32_t s = (std::int32_t(nibble << 28 >> 28) * (1 << (20 - shiftNibble)) + kXaCoef[coef][0] * h[0] + kXaCoef[coef][1] * h[1] + 0x80) >> 8;
    s = std::clamp(s, -32768, 32767);
    h[1] = h[0];
    h[0] = s;
    return std::int16_t(s);
}

} // namespace

const char* codecName(EaCodec c) {
    switch (c) {
    case EaCodec::Pcm16: return "pcm16le";
    case EaCodec::EaXa: return "ea-xa-v1";
    case EaCodec::MicroTalk: return "microtalk10";
    }
    return "?";
}

std::optional<std::uint32_t> PtHeader::tag(std::uint8_t t) const {
    for (auto& [k, v] : tags)
        if (k == t) return v;
    return std::nullopt;
}

EaCodec PtHeader::codec() const {
    if (!codec1 || *codec1 == 0) return EaCodec::Pcm16;
    if (*codec1 == 7) return EaCodec::EaXa;
    if (*codec1 == 9) return EaCodec::MicroTalk;
    throw FormatError(std::format("EA codec1 {} not used by World Cup 98 and not implemented", *codec1));
}

PtHeader PtHeader::parse(std::span<const std::uint8_t> data, std::size_t offset) {
    Reader r(data, offset);
    if (r.u8() != 'P' || r.u8() != 'T') throw FormatError(std::format("expected PT header at {}", offset));
    PtHeader h;
    h.platform = r.u16le();
    // WC98 (matrix section 7): PT version 2 = "PT\0\2" — the version sits in the high byte
    // of the platform word (0x0200 on 756/756 .SMP), an extra u32 header size follows, and
    // the tag stream starts 4 bytes later. WC98 .BNK banks use v0 headers (47/47: platform
    // 0, absolute 0x88), which is why the BNK path needs no change.
    if ((h.platform >> 8) == 2) {
        h.v2 = true;
        h.headerSize = r.u32le();
        if (h.headerSize < 12 || h.headerSize > 0x10000)
            throw FormatError(std::format("PT v2 header size {} implausible", h.headerSize));
    }
    for (;;) {
        std::uint8_t t = r.u8();
        if (t == 0xFF) break;
        if (t == 0xFC || t == 0xFD || t == 0xFE) continue; // padding / section markers without value
        std::uint8_t len = r.u8();
        auto bytes = r.bytes(len);
        std::uint32_t v = 0;
        if (len <= 4)
            for (auto b : bytes) v = v << 8 | b; // values are big-endian, variable length
        h.tags.emplace_back(t, v);
        switch (t) {
        case 0x82: h.channels = v; break;
        case 0x83: h.codec1 = v; break;
        case 0x84: h.sampleRate = v; break;
        case 0x85: h.numSamples = v; break;
        case 0x86: h.loopStart = v; break;
        case 0x87: h.loopEnd = v; break;
        case 0x88:
            // v0 (FIFA 99 + WC98 .BNK): absolute sample-data offset.
            // v2 (WC98 .SMP/.PBK): header-relative (observed value = headerSize); resolved
            // to absolute here so every consumer sees one convention (matrix section 7).
            h.dataOffset = h.v2 ? std::uint32_t(offset + v) : v;
            break;
        case 0x65:
            // WC98 (1998) PT v0: the meta block nests the standard EA tags inside the
            // 0x65 payload (measured on VIDEO/INT_EASP.DCT audio: 0x85 sample count,
            // 0x82 channels, 0x83 codec = 7 EA-XA). Merge them with tag semantics.
            for (std::size_t k = 0; k + 1 < bytes.size();) {
                const std::uint8_t t2 = bytes[k];
                if (t2 == 0xFF) break;
                const std::uint8_t l2 = bytes[k + 1];
                if (k + 2 + l2 > bytes.size()) break;
                std::uint32_t v2 = 0;
                if (l2 <= 4)
                    for (std::uint8_t i = 0; i < l2; ++i) v2 = v2 << 8 | bytes[k + 2 + i];
                h.tags.emplace_back(t2, v2);
                switch (t2) {
                case 0x82: h.channels = v2; break;
                case 0x83: h.codec1 = v2; break;
                case 0x84: h.sampleRate = v2; break;
                case 0x85: h.numSamples = v2; break;
                case 0x86: h.loopStart = v2; break;
                case 0x87: h.loopEnd = v2; break;
                case 0x88: h.dataOffset = h.v2 ? std::uint32_t(offset + v2) : v2; break;
                default: break;
                }
                k += 2 + l2;
            }
            break;
        default: break;
        }
    }
    h.size = r.pos() - offset;
    if (h.channels < 1 || h.channels > 2) throw FormatError(std::format("PT header: {} channels unsupported", h.channels));
    if (h.sampleRate < 4000 || h.sampleRate > 96000) throw FormatError(std::format("PT header: sample rate {} implausible", h.sampleRate));
    return h;
}

Pcm decodePlayTrack(std::span<const std::uint8_t> data, std::size_t ptOffset) {
    // WC98 (matrix section 7): a standalone PT play track (.SMP, and each .PBK member) is
    // one PT v2 header followed by the raw payload — no SCHl chunk framing.
    // WC98 (SPEECH_INDEXING_NOTES.md §5.2): dispatch on the payload magic at ptOffset, not an
    // assumed PT header. "MIDx" streams (CRWD/COMOS.PBK crowd-MIDI beds), empty nested BIG
    // archives (count=0, hdrSize=16: 268 silent PHRS phrases) and no-magic raw codec blocks
    // (CRWD/CROWD.PBK crowd beds — §4 Hypothesis, not decoded) yield a silent Pcm instead of
    // throwing. The 6,964 "PT" members keep the byte-identical decode path below; callers that
    // need strictness gate on the PT magic first (as test_audio's PBK walk does).
    const auto magicAt = [&](std::string_view m) {
        return ptOffset + m.size() <= data.size() && std::memcmp(data.data() + ptOffset, m.data(), m.size()) == 0;
    };
    if (!magicAt("PT")) return Pcm{}; // MIDx / empty nested BIGF / raw crowd payload: no PCM samples
    const PtHeader h = PtHeader::parse(data, ptOffset);
    if (!h.dataOffset) throw FormatError("PT play track has no data offset (tag 0x88)");
    if (*h.dataOffset > data.size()) throw FormatError(std::format("PT play track: data offset {} past end", *h.dataOffset));
    const auto in = data.subspan(*h.dataOffset);
    Pcm pcm{h.sampleRate, h.channels, {}};
    switch (h.codec()) {
    case EaCodec::EaXa: {
        std::int32_t hist[4] = {};
        eaxa::decodeFrames(in, h.channels, (h.numSamples + 27) / 28, hist, pcm.samples);
        pcm.samples.resize(std::size_t(h.numSamples) * h.channels);
        break;
    }
    case EaCodec::Pcm16: {
        std::size_t n = std::size_t(h.numSamples) * h.channels;
        if (in.size() < n * 2) throw FormatError("PT play track: PCM data truncated");
        for (std::size_t i = 0; i < n; ++i) pcm.samples.push_back(std::int16_t(in[2 * i] | in[2 * i + 1] << 8));
        break;
    }
    case EaCodec::MicroTalk: {
        // WC98 (matrix section 7): the 1998 payload has no per-block flag byte; the
        // decoder gains FIFA 99 framing through decodeSpeechPayload. Amplitudes are
        // approximate (1998 15-bit header/gain layout not calibrated).
        utk::Decoder mt;
        mt.decodeSpeechPayload(in, h.numSamples, pcm.samples);
        pcm.samples.resize(std::size_t(h.numSamples));
        break;
    }
    }
    return pcm;
}

std::vector<std::uint8_t> toWav(const Pcm& pcm) {
    std::vector<std::uint8_t> w;
    auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back(std::uint8_t(v >> (8 * i))); };
    auto u16 = [&](std::uint16_t v) { w.push_back(std::uint8_t(v)); w.push_back(std::uint8_t(v >> 8)); };
    auto tag = [&](const char* t) { w.insert(w.end(), t, t + 4); };
    std::uint32_t bytes = std::uint32_t(pcm.samples.size() * 2);
    w.reserve(44 + bytes);
    tag("RIFF"); u32(36 + bytes); tag("WAVE");
    tag("fmt "); u32(16); u16(1); u16(std::uint16_t(pcm.channels)); u32(pcm.sampleRate);
    u32(pcm.sampleRate * pcm.channels * 2); u16(std::uint16_t(pcm.channels * 2)); u16(16);
    tag("data"); u32(bytes);
    for (auto s : pcm.samples) u16(std::uint16_t(s));
    return w;
}

namespace eaxa {

void decodeFrames(std::span<const std::uint8_t> in, std::uint32_t channels, std::size_t frames, std::int32_t* hist, std::vector<std::int16_t>& out) {
    std::size_t fsize = channels == 1 ? 15 : 30;
    if (channels < 1 || channels > 2) throw FormatError("EA-XA: only mono/stereo supported");
    if (frames > in.size() / fsize) throw FormatError(std::format("EA-XA: {} frames need {} bytes, have {}", frames, frames * fsize, in.size()));
    out.reserve(out.size() + frames * 28 * channels);
    for (std::size_t f = 0; f < frames; ++f) {
        const std::uint8_t* p = in.data() + f * fsize;
        if (channels == 1) {
            int coef = p[0] >> 4, shift = p[0] & 15;
            if (coef > 3) throw FormatError("EA-XA: bad coefficient index");
            for (int i = 0; i < 28; ++i) {
                std::uint8_t b = p[1 + i / 2];
                out.push_back(xaSample((i & 1) ? (b & 15) : (b >> 4), shift, coef, hist));
            }
        } else {
            int cl = p[0] >> 4, cr = p[0] & 15, sl = p[1] >> 4, sr = p[1] & 15;
            if (cl > 3 || cr > 3) throw FormatError("EA-XA: bad coefficient index");
            for (int i = 0; i < 28; ++i) {
                std::uint8_t b = p[2 + i];
                out.push_back(xaSample(b >> 4, sl, cl, hist));
                out.push_back(xaSample(b & 15, sr, cr, hist + 2));
            }
        }
    }
}

std::vector<std::uint8_t> encodeMono(std::span<const std::int16_t> pcm) {
    std::vector<std::uint8_t> out;
    std::int32_t hist[2] = {0, 0};
    for (std::size_t base = 0; base < pcm.size(); base += 28) {
        std::uint8_t best[15]{};
        std::int64_t bestErr = std::numeric_limits<std::int64_t>::max();
        std::int32_t bestHist[2]{};
        for (int coef = 0; coef < 4; ++coef)
            for (int shift = 0; shift <= 12; ++shift) {
                std::uint8_t fr[15]{};
                fr[0] = std::uint8_t(coef << 4 | shift);
                std::int32_t h[2] = {hist[0], hist[1]};
                std::int64_t err = 0;
                for (int i = 0; i < 28; ++i) {
                    std::int32_t target = base + i < pcm.size() ? pcm[base + i] : 0;
                    std::int32_t pred = kXaCoef[coef][0] * h[0] + kXaCoef[coef][1] * h[1] + 0x80;
                    double q = (double(target) * 256 - pred) / double(1 << (20 - shift));
                    int nib = std::clamp(int(q < 0 ? q - 0.5 : q + 0.5), -8, 7);
                    std::int32_t got = xaSample(nib, shift, coef, h);
                    err += std::int64_t(got - target) * (got - target);
                    fr[1 + i / 2] |= std::uint8_t((nib & 15) << ((i & 1) ? 0 : 4));
                }
                if (err < bestErr) {
                    bestErr = err;
                    std::memcpy(best, fr, 15);
                    bestHist[0] = h[0];
                    bestHist[1] = h[1];
                }
            }
        out.insert(out.end(), best, best + 15);
        hist[0] = bestHist[0];
        hist[1] = bestHist[1];
    }
    return out;
}

} // namespace eaxa

SchlStream parseSchl(std::span<const std::uint8_t> data, std::size_t offset) {
    Reader r(data, offset);
    if (r.u32be() != id("SCHl")) throw FormatError(std::format("expected SCHl at {}", offset));
    std::uint32_t hsize = r.u32le();
    if (hsize < 12 || offset + hsize > data.size()) throw FormatError(std::format("SCHl at {}: bad size {}", offset, hsize));
    SchlStream s;
    s.offset = offset;
    s.header = PtHeader::parse(data.first(offset + hsize), offset + 8);
    std::size_t o = offset + hsize;
    std::optional<std::uint32_t> declaredBlocks;
    for (;;) {
        Reader c(data, o);
        std::uint32_t t = c.u32be(), size = c.u32le();
        if (size < 8 || o + size > data.size()) throw FormatError(std::format("chunk at {}: bad size {}", o, size));
        if (t == id("SCCl")) declaredBlocks = c.u32le();
        else if (t == id("SCDl")) s.dataBlocks.push_back(o);
        else if (t == id("SCHl")) throw FormatError(std::format("SCHl at {} before SCEl of stream at {}", o, offset));
        o += size;
        if (t == id("SCEl")) break;
    }
    s.end = o;
    if (declaredBlocks && *declaredBlocks != s.dataBlocks.size())
        throw FormatError(std::format("stream at {}: SCCl declares {} blocks, found {}", offset, *declaredBlocks, s.dataBlocks.size()));
    return s;
}

std::vector<SchlStream> scanSchl(std::span<const std::uint8_t> data) {
    std::vector<SchlStream> out;
    std::size_t o = 0;
    while (o + 8 <= data.size()) {
        Reader c(data, o);
        std::uint32_t t = c.u32be(), size = c.u32le();
        if (t == id("SCHl")) {
            out.push_back(parseSchl(data, o));
            o = out.back().end;
            continue;
        }
        if (size < 8 || o + size > data.size()) throw FormatError(std::format("chunk at {}: bad size {}", o, size));
        o += size;
    }
    if (o != data.size() && std::any_of(data.begin() + std::ptrdiff_t(o), data.end(), [](auto b) { return b != 0; }))
        throw FormatError(std::format("trailing data at {}", o));
    return out;
}

SchlDecoder::SchlDecoder(std::span<const std::uint8_t> data, const SchlStream& s) : data_(data), stream_(s), codec_(s.header.codec()) {
    if (codec_ == EaCodec::MicroTalk && s.header.channels != 1) throw FormatError("MicroTalk: only mono supported");
}

void SchlDecoder::rewind() {
    block_ = 0;
    mt_ = {};
}

bool SchlDecoder::next(std::vector<std::int16_t>& out) {
    if (block_ >= stream_.dataBlocks.size()) return false;
    const auto& h = stream_.header;
    std::size_t b = stream_.dataBlocks[block_++];
    std::uint32_t size = Reader(data_, b + 4).u32le();
    Reader r(data_.subspan(b, size), 8);
    std::uint32_t count = r.u32le();
    auto payload = r.bytes(r.remaining());
    switch (codec_) {
    case EaCodec::Pcm16:
        if (payload.size() != std::size_t(count) * h.channels * 2)
            throw FormatError(std::format("PCM block at {}: {} bytes for {} samples", b, payload.size(), count));
        for (std::size_t i = 0; i < payload.size(); i += 2) out.push_back(std::int16_t(payload[i] | payload[i + 1] << 8));
        break;
    case EaCodec::EaXa: {
        // Block: per-channel {current, previous} history (s16le), then 28-sample frames.
        Reader pr(payload);
        std::int32_t hist[4] = {};
        for (std::uint32_t ch = 0; ch < h.channels; ++ch) {
            hist[2 * ch] = pr.i16le();
            hist[2 * ch + 1] = pr.i16le();
        }
        tmp_.clear();
        eaxa::decodeFrames(payload.subspan(pr.pos()), h.channels, (count + 27) / 28, hist, tmp_);
        out.insert(out.end(), tmp_.begin(), tmp_.begin() + std::ptrdiff_t(count) * h.channels);
        break;
    }
    case EaCodec::MicroTalk:
        mt_.decodeBlock(payload, count, out);
        break;
    }
    return true;
}

Pcm decodeSchl(std::span<const std::uint8_t> data, const SchlStream& s) {
    const auto& h = s.header;
    Pcm pcm{h.sampleRate, h.channels, {}};
    pcm.samples.reserve(std::size_t(h.numSamples) * h.channels);
    SchlDecoder dec(data, s);
    while (dec.next(pcm.samples)) {}
    if (pcm.frames() != h.numSamples)
        throw FormatError(std::format("stream at {}: decoded {} samples, header says {}", s.offset, pcm.frames(), h.numSamples));
    return pcm;
}

} // namespace wc98::formats
