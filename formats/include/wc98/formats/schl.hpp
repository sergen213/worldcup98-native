// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA SCHl audio streams (.ASF music, .STR speech, audio inside .MAD video) and the "PT" patch
// header shared with BNKl banks. See docs/formats/SCHL.md.
#include "wc98/formats/utk.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace wc98::formats {

// Codecs found in FIFA 99 data (selected by PT tag 0x83; absent = PCM, see SCHL.md).
enum class EaCodec { Pcm16, EaXa, MicroTalk };
const char* codecName(EaCodec c);

// Decoded "PT\0" patch header. Only tags observed in FIFA 99 data get named fields;
// every tag/value pair is kept in `tags` in file order.
//
// WC98 delta (research-inbox/F99_REUSABILITY_MATRIX.md section 7): WC98 speech uses PT
// version 2 headers — the platform word carries the version in its high byte (0x0200 on
// 756/756 .SMP), an extra u32 header size (32/36/44) follows it, and tag 0x88 is
// HEADER-relative (resolved to an absolute offset by parse() below). WC98 .BNK banks keep
// v0 headers (47/47: platform 0, absolute 0x88), so the BNK/SCHl paths are unchanged.
struct PtHeader {
    std::uint16_t platform = 0;           // u16le after "PT"; 0 = PC (v0), 0x0200 = PT v2 (WC98 speech)
    bool v2 = false;                      // "PT\0\2": extra u32 header size + relative 0x88
    std::uint32_t headerSize = 0;         // v2 only: bytes 4..7 (observed 0x20 / 0x24 / 0x2c)
    std::uint32_t channels = 1;           // tag 0x82
    std::optional<std::uint32_t> codec1;  // tag 0x83 (0 PCM, 7 EA-XA, 9 MicroTalk 10:1)
    std::uint32_t sampleRate = 22050;     // tag 0x84 (default 22050 when absent)
    std::uint32_t numSamples = 0;         // tag 0x85, per channel
    std::optional<std::uint32_t> loopStart; // tag 0x86 (sample)
    std::optional<std::uint32_t> loopEnd;   // tag 0x87 (last sample of loop, inclusive)
    std::optional<std::uint32_t> dataOffset; // tag 0x88, always resolved to an absolute offset
    std::vector<std::pair<std::uint8_t, std::uint32_t>> tags;
    std::size_t size = 0;                 // bytes consumed from "PT" through the 0xFF end tag

    [[nodiscard]] EaCodec codec() const;  // throws FormatError for codecs absent from FIFA 99
    [[nodiscard]] std::optional<std::uint32_t> tag(std::uint8_t t) const;
    // Parses starting at the "PT\0" id.
    static PtHeader parse(std::span<const std::uint8_t> data, std::size_t offset);
};

// Interleaved signed 16-bit PCM.
struct Pcm {
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::vector<std::int16_t> samples;
    [[nodiscard]] std::size_t frames() const { return channels ? samples.size() / channels : 0; }
};
// RIFF/WAVE (PCM16) serialisation for exported audio.
std::vector<std::uint8_t> toWav(const Pcm& pcm);

struct SchlStream {
    std::size_t offset = 0; // of "SCHl"
    std::size_t end = 0;    // one past the SCEl chunk
    PtHeader header;
    std::vector<std::size_t> dataBlocks; // offsets of SCDl chunks
};

// Parses one stream starting at `offset` ("SCHl"). Non-SC chunks between blocks (MADk/MADm/MADe
// video frames when the stream is embedded in a .MAD) are skipped.
SchlStream parseSchl(std::span<const std::uint8_t> data, std::size_t offset);
// All streams of a file, in order (STR files are concatenations of SCHl..SCEl streams).
std::vector<SchlStream> scanSchl(std::span<const std::uint8_t> data);
// Decodes to PCM; the result has exactly header.numSamples frames (throws on mismatch).
Pcm decodeSchl(std::span<const std::uint8_t> data, const SchlStream& s);

// WC98 (matrix section 7): decodes a standalone PT play track — a PT v2 header at
// `ptOffset` followed by the raw payload (.SMP files, .PBK archive members). No SCHl
// chunk framing; MicroTalk output levels are approximate (see utk::decodeSpeechPayload).
Pcm decodePlayTrack(std::span<const std::uint8_t> data, std::size_t ptOffset);

// Block-by-block decoder for streaming playback; `data` must outlive it.
class SchlDecoder {
public:
    SchlDecoder(std::span<const std::uint8_t> data, const SchlStream& s);
    // Appends the next SCDl block's interleaved samples; false when the stream is exhausted.
    bool next(std::vector<std::int16_t>& out);
    void rewind();
    [[nodiscard]] const PtHeader& header() const { return stream_.header; }

private:
    std::span<const std::uint8_t> data_;
    SchlStream stream_;
    EaCodec codec_;
    std::size_t block_ = 0;
    utk::Decoder mt_;
    std::vector<std::int16_t> tmp_;
};

// ---- codecs (exposed for BNK and tests) ----
namespace eaxa {
// EA-XA v1 frames, 28 samples each. Mono frame = 15 bytes (coef|shift byte + 14 nibble bytes);
// stereo frame = 30 bytes (coefs byte, shifts byte, 28 bytes of L|R nibble pairs).
// `hist` holds {cur, prev} per channel and is updated. Appends interleaved samples to `out`.
void decodeFrames(std::span<const std::uint8_t> in, std::uint32_t channels, std::size_t frames,
                  std::int32_t* hist, std::vector<std::int16_t>& out);
// Encoder used only for synthetic round-trip tests and tooling (exhaustive per-frame search).
std::vector<std::uint8_t> encodeMono(std::span<const std::int16_t> pcm);
} // namespace eaxa

} // namespace wc98::formats
