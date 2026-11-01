// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/bnk.hpp"
#include "wc98/reader.hpp"
#include <format>

namespace wc98::formats {

Bnk Bnk::parse(std::span<const std::uint8_t> data) {
    Reader r(data);
    if (r.u32be() != 0x424E4B6C) throw FormatError("not a BNKl bank"); // "BNKl"
    Bnk b;
    b.version = r.u8();
    r.u8(); // always 0 (version is stored as a u16-LE; high byte)
    b.slots = r.u16le();
    b.dataStart = r.u32le();
    std::size_t table;
    if (b.version == 2) table = 0x0C;
    else if (b.version == 4) table = 0x14; // 0x0C: end of sample data, 0x10: 0
    else throw FormatError(std::format("BNKl version {} not used by World Cup 98", b.version));
    r.seek(table);
    for (std::uint32_t i = 0; i < b.slots; ++i) {
        std::size_t entry = r.pos();
        std::uint32_t rel = r.u32le();
        if (!rel) continue;
        BnkSound s;
        s.slot = i;
        s.headerOffset = entry + rel; // offsets are relative to their own table entry
        s.header = PtHeader::parse(data, s.headerOffset);
        if (!s.header.dataOffset) throw FormatError(std::format("BNK sound {} has no data offset (tag 0x88)", i));
        b.sounds.push_back(std::move(s));
    }
    return b;
}

Pcm Bnk::decode(std::span<const std::uint8_t> data, const BnkSound& s) {
    const auto& h = s.header;
    Pcm pcm{h.sampleRate, h.channels, {}};
    std::size_t off = *h.dataOffset;
    if (off > data.size()) throw FormatError(std::format("BNK sound {}: data offset {} past end", s.slot, off));
    auto in = data.subspan(off);
    switch (h.codec()) {
    case EaCodec::EaXa: {
        std::int32_t hist[4] = {};
        eaxa::decodeFrames(in, h.channels, (h.numSamples + 27) / 28, hist, pcm.samples);
        pcm.samples.resize(std::size_t(h.numSamples) * h.channels);
        break;
    }
    case EaCodec::Pcm16: {
        std::size_t n = std::size_t(h.numSamples) * h.channels;
        if (in.size() < n * 2) throw FormatError(std::format("BNK sound {}: PCM data truncated", s.slot));
        for (std::size_t i = 0; i < n; ++i) pcm.samples.push_back(std::int16_t(in[2 * i] | in[2 * i + 1] << 8));
        break;
    }
    case EaCodec::MicroTalk: {
        // WC98 (matrix section 7): the 1998 play-track payload has no per-block flag byte;
        // decodeSpeechPayload supplies the FIFA 99 framing. No WC98 .BNK bank uses
        // MicroTalk (47/47 are EA-XA), so this path serves .SMP-shaped banks; amplitudes
        // are approximate (1998 15-bit header/gain layout not calibrated).
        utk::Decoder mt;
        mt.decodeSpeechPayload(in, h.numSamples, pcm.samples);
        pcm.samples.resize(std::size_t(h.numSamples));
        break;
    }
    }
    return pcm;
}

} // namespace wc98::formats
