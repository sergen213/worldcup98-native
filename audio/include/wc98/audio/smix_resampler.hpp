#pragma once
// WC98 SMIX Q16 resample stage — faithful port of FIFAWC.EXE FUN_0054d4fb / FUN_0054d4b5.
// Grammar dossier: analysis/pass17audio/DECOMPILE2/{loop_54d4fb.txt,loop_54d4b5.txt} (pass-17audio audio11).
#include <cstdint>

namespace wc98::audio {

// State block mirrors the exe ctx: {pos, step, lastSample, mode}. All four are u32 slots.
struct SmixResamplerState {
    std::uint32_t pos = 0;         // *param_3   fractional accumulator (Q16 low word kept)
    std::uint32_t step = 0;        // param_3[1] Q16 rate step
    std::uint32_t lastSample = 0;  // param_3[2] sample-and-hold register (packed s16 pair dword)
    std::uint32_t mode = 0;        // param_3[3] fractional-fill flag (1 = hold runs output-only)
};

// FUN_0054d4b5: per-step position math. Exact expressions, exe test order kept.
inline std::int32_t Smix_stepDelta(SmixResamplerState& s, std::uint32_t count) {
    if (s.step < 0x10000)
        return static_cast<std::int32_t>(((s.step * (count - 1) + s.pos) >> 16) + 1 -
                                         static_cast<std::int32_t>(s.mode));
    if (s.step < 0x10001) return static_cast<std::int32_t>(count);
    return static_cast<std::int32_t>(((static_cast<std::uint64_t>(s.step) * count + s.pos) >> 16) -
                                     static_cast<std::int32_t>(s.mode));
}

// FUN_0054d4fb: nearest-neighbour Q16 resampler over packed stereo-pair dwords.
// Faithful to the do-while loops incl. CARRY4 accumulator and mode==1 fractional fill head run.
inline void Smix_resample(SmixResamplerState& s, const std::uint32_t* srcDwords,
                          std::uint32_t* dstDwords, int outCount) {
    if (s.step == 0x10000) {
        // passthrough: exe installs identity copy semantics.
        for (int i = 0; i < outCount; ++i) dstDwords[i] = srcDwords[i];
        return;
    }
    std::uint32_t acc = s.pos << 16;
    const std::uint32_t step = s.step;
    const int hi = static_cast<int>(step >> 16);
    std::uint32_t srcIdx = 0;  // exe: (param_5>>2) then *4 -> dword stream index.
    std::uint32_t* out = dstDwords;
    if (s.mode == 1) {
        do {  // fractional fill run at block head (mode==1 entry only).
            *out++ = s.lastSample;
            const bool carry = (acc + step) < acc;  // CARRY4: full 32-bit add carry-out.
            acc += step;
            if (hi + static_cast<int>(carry) == 0) break;  // exe: while (uVar3 + bVar6 == 0).
        } while (false);
    }
    for (;;) {
        *out = srcDwords[srcIdx];
        const bool carry = (acc + step) < acc;
        acc += step;
        srcIdx += static_cast<std::uint32_t>(hi) + (carry ? 1u : 0u);
        if (out - dstDwords + 1 >= outCount) break;  // exe: do{write;advance;}while(out+1 < end).
        ++out;
    }
    s.pos = acc >> 16;
    const std::uint32_t lastWord = *out;  // exe: uVar2 = *puVar5 (last written slot).
    if (static_cast<std::int32_t>(hi) < static_cast<std::int32_t>(step)) {
        s.mode = 0;
    } else {
        s.mode = 1;
        s.lastSample = lastWord;
    }
}

} // namespace wc98::audio
