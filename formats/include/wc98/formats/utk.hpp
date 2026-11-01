// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA MicroTalk 10:1 ("UTK", PT codec1 = 9): 12th-order LPC/CELP speech codec used for all FIFA 99
// commentary (.STR). 432 samples per frame (4 subframes of 108). See docs/formats/SCHL.md.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats::utk {

class Decoder {
public:
    // Decodes one SCDl payload holding `samples` samples (a multiple of 432) and appends them.
    // The payload starts with a flag byte: 1 = the 15-bit stream header follows (first block of a
    // stream), 0 = continue with the header already seen. Returns the number of payload bits
    // consumed (used to validate the bit layout against the block size).
    std::size_t decodeBlock(std::span<const std::uint8_t> payload, std::size_t samples, std::vector<std::int16_t>& out);

    // WC98 (research-inbox/F99_REUSABILITY_MATRIX.md section 7): the 1998 .SMP/.PBK
    // MicroTalk payload has no per-block flag byte. This prepends the FIFA 99 first-block
    // flag (0x01 = the 15-bit stream header starts this block) and decodes the payload as
    // one block of `samples` samples. 125/125 sampled WC98 clips decode this way.
    //
    // SCALING CAVEAT (measured in the matrix probe): every decode peaks at 32767/32768
    // (int16 saturation) while the mean level is speech-like (3800-5800). The 1998 15-bit
    // stream-header/gain layout is NOT calibrated, so decoded amplitudes are approximate;
    // do not treat them as bit-exact.
    std::size_t decodeSpeechPayload(std::span<const std::uint8_t> payload, std::size_t samples, std::vector<std::int16_t>& out);

private:
    void decodeFrame();
    unsigned bits(int n);
    void excitation(bool multipulse, float* out, int stride);
    void synthesis(int offset, int blocks);

    std::span<const std::uint8_t> in_;
    std::size_t pos_ = 0;    // next byte to load
    std::uint32_t value_ = 0; // LSB-first bit buffer
    int count_ = 0;
    std::size_t consumed_ = 0;
    bool haveHeader_ = false;
    bool reducedBw_ = false;
    int multipulseThresh_ = 0;
    // One contiguous array mirrors the reference decoder's memory layout:
    // fixedGains[64] | rc[12] | synthHistory[12] | adaptCb[324] | frame[432].
    // Long pitch lags (>216) read back into rc/history and short lags read forward into the
    // frame being built; keeping them adjacent reproduces that behaviour exactly.
    static constexpr int kGains = 0, kRc = 64, kHist = 76, kAdapt = 88, kFrame = 412, kTotal = 844;
    std::array<float, kTotal> m_{};
};

} // namespace wc98::formats::utk
