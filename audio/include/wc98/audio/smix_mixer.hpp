#pragma once
// WC98 SMIX 2D float mixer — faithful port of FIFAWC.EXE pan-law handlers (FUN_0054ccfe installer,
// FUN_0054cd8e general handler; combo table {d091 silent, ce8d, cf09, ce1e, cf86, d00b}).
// Grammar dossier: analysis/pass17audio/DECOMPILE3/mix_*.txt + NOTES.md audio14/audio15.
#include <cstdint>

namespace wc98::audio {

// Volume/pan ints are 0..0x7F; exe normalizes by DAT_00596560 = 1.0f/127.
inline constexpr float kSmixVolumeScale = 1.0f / 127.0f;

enum class SmixPanHandler : std::uint8_t { Silent, LeftSilent, RightSilent, FullCentre, RightFull, LeftFull, General };

// FUN_0054ccfe handler selection on (volL, volR) int combos — exact exe test order.
inline SmixPanHandler Smix_selectHandler(std::int32_t volL, std::int32_t volR) {
    if (volL == 0) {
        return volR == 0 ? SmixPanHandler::Silent : SmixPanHandler::LeftSilent;  // (0,x) -> cf09.
    }
    if (volR == 0) return SmixPanHandler::RightSilent;  // (x,0) -> ce8d.
    if (volL == 0x7f) {
        return volR == 0x7f ? SmixPanHandler::FullCentre  // (7F,7F) -> ce1e
                            : SmixPanHandler::RightFull;  // (7F,x) -> d00b
    }
    if (volR == 0x7f) return SmixPanHandler::LeftFull;  // (x,7F) -> cf86.
    return SmixPanHandler::General;                     // general   -> cd8e.
}

// FUN_0054cd8e: stereo source pairs accumulate into a quad dst [4 floats] with the 2x2 pan matrix
// row per pair: dst[0]+=L*vl, dst[1]+=L*vr, dst[2]+=R*vl (slot order exe: dst[3]=R*vr, dst[2]=R*vl).
inline void Smix_mixGeneral(const float* srcPairs, float* dstQuad, int pairCount, float vl, float vr) {
    for (int i = 0; i < pairCount; ++i) {
        const float l = srcPairs[i * 2];
        const float r = srcPairs[i * 2 + 1];
        dstQuad[i * 4 + 0] += l * vl;
        dstQuad[i * 4 + 1] += l * vr;
        dstQuad[i * 4 + 2] += r * vl;
        dstQuad[i * 4 + 3] += r * vr;
    }
}

// Installer semantics (FUN_0054ccfe): stage floats and pick the handler.
struct SmixVoiceGain {
    float volL = 0.0f;
    float volR = 0.0f;
    SmixPanHandler handler = SmixPanHandler::Silent;
};

inline SmixVoiceGain Smix_install(std::int32_t volL, std::int32_t volR) {
    SmixVoiceGain g;
    g.volL = static_cast<float>(volL) * kSmixVolumeScale;
    g.volR = static_cast<float>(volR) * kSmixVolumeScale;
    g.handler = Smix_selectHandler(volL, volR);
    return g;
}

// ---- Per-handler mix loops, exe frame-count grammar (unrolled body + mono tail). ----
// All handlers take the float stream `src` of `sampleCount` samples accumulating into interleaved
// quad dst frames [4 floats each]; vl/vr are the installer-staged slot0/slot1 floats. The do-while
// bounds replicate the decompile (iVar7 = n-1 / n-3 forms), incl. the odd-count mono tail run.

// FUN_0054d091: silent voice, no-op mix.
inline void Smix_mixSilent(const float*, int, float*) {}

// FUN_0054cd8e general 2x2 pan matrix (frame form): pairs -> quad {L*vl, L*vr, R*vl, R*vr}.
inline void Smix_mixFramesGeneral(const float* src, int n, float* dst, float vl, float vr) {
    int rem = n - 1;
    if (1 < n) {
        int i = rem;
        do {
            i = rem;
            dst[0] += src[0] * vl; dst[1] += src[0] * vr;
            dst[2] += src[1] * vl; dst[3] += src[1] * vr;
            src += 2; dst += 4; rem = i - 2;
        } while (i - 2 != 0 && 1 < i);
        n = i - 1;
    }
    for (;;) {
        if (n < 1) break;                       // exe tail: mono remainder sample.
        dst[0] += src[0] * vl; dst[1] += src[0] * vr;
        src += 1; dst += 2; rem = n - 1;
        const bool pos = 0 < n;
        n = rem;
        if (rem == 0 || !pos) break;
    }
}

// FUN_0054ce1e (7F,7F): full-centre passthrough accumulation {L, L, R, R}.
inline void Smix_mixFramesFullCentre(const float* src, int n, float* dst) {
    int rem = n - 1;
    if (1 < n) {
        int i = rem;
        do {
            i = rem;
            dst[0] += src[0]; dst[1] += src[0];
            dst[2] += src[1]; dst[3] += src[1];
            src += 2; dst += 4; rem = i - 2;
        } while (i - 2 != 0 && 1 < i);
        n = i - 1;
    }
    for (;;) {
        if (n < 1) break;
        dst[0] += src[0]; dst[1] += src[0];
        src += 1; dst += 2; rem = n - 1;
        const bool pos = 0 < n;
        n = rem;
        if (rem == 0 || !pos) break;
    }
}

// FUN_0054cf86 (x,7F): slot0 gain on L/R into left slots, right slots passthrough.
inline void Smix_mixFramesLeftFull(const float* src, int n, float* dst, float vl) {
    int rem = n - 1;
    if (1 < n) {
        int i = rem;
        do {
            i = rem;
            dst[0] += src[0] * vl; dst[1] += src[0];
            dst[2] += src[1] * vl; dst[3] += src[1];
            src += 2; dst += 4; rem = i - 2;
        } while (i - 2 != 0 && 1 < i);
        n = i - 1;
    }
    for (;;) {
        if (n < 1) break;
        dst[0] += src[0] * vl; dst[1] += src[0];
        src += 1; dst += 2; rem = n - 1;
        const bool pos = 0 < n;
        n = rem;
        if (rem == 0 || !pos) break;
    }
}

// FUN_0054d00b (7F,x): slot1 gain on L/R into right slots, left slots passthrough.
inline void Smix_mixFramesRightFull(const float* src, int n, float* dst, float vr) {
    int rem = n - 1;
    if (1 < n) {
        int i = rem;
        do {
            i = rem;
            dst[0] += src[0]; dst[1] += src[0] * vr;
            dst[2] += src[1]; dst[3] += src[1] * vr;
            src += 2; dst += 4; rem = i - 2;
        } while (i - 2 != 0 && 1 < i);
        n = i - 1;
    }
    for (;;) {
        if (n < 1) break;
        dst[0] += src[0]; dst[1] += src[0] * vr;
        src += 1; dst += 2; rem = n - 1;
        const bool pos = 0 < n;
        n = rem;
        if (rem == 0 || !pos) break;
    }
}

// FUN_0054ce8d (x,0): mono stream * slot0 into the left output slots (stride-2 dst walk).
inline void Smix_mixFramesRightSilent(const float* src, int n, float* dst, float vl) {
    int rem = n - 3;
    if (3 < n) {
        int i = rem;
        do {
            i = rem;
            dst[0] += src[0] * vl; dst[2] += src[1] * vl;
            dst[4] += src[2] * vl; dst[6] += src[3] * vl;
            src += 4; dst += 8; rem = i - 4;
        } while (i - 4 != 0 && 3 < i);
        n = i - 1;
    }
    for (;;) {
        if (n < 1) break;
        dst[0] += src[0] * vl;
        src += 1; dst += 2; rem = n - 1;
        const bool pos = 0 < n;
        n = rem;
        if (rem == 0 || !pos) break;
    }
}

// FUN_0054cf09 (0,x): mono stream * slot1 into the right output slots.
inline void Smix_mixFramesLeftSilent(const float* src, int n, float* dst, float vr) {
    int rem = n - 3;
    if (3 < n) {
        int i = rem;
        do {
            i = rem;
            dst[1] += src[0] * vr; dst[3] += src[1] * vr;
            dst[5] += src[2] * vr; dst[7] += src[3] * vr;
            src += 4; dst += 8; rem = i - 4;
        } while (i - 4 != 0 && 3 < i);
        n = i - 1;
    }
    for (;;) {
        if (n < 1) break;
        dst[1] += src[0] * vr;
        src += 1; dst += 2; rem = n - 1;
        const bool pos = 0 < n;
        n = rem;
        if (rem == 0 || !pos) break;
    }
}

// Dispatcher: install(volL, volR) picks the handler; mix runs it over the frame stream.
inline void Smix_mix(const SmixVoiceGain& g, const float* src, int sampleCount, float* dst) {
    switch (g.handler) {
    case SmixPanHandler::Silent:      Smix_mixSilent(src, sampleCount, dst); break;
    case SmixPanHandler::LeftSilent:  Smix_mixFramesLeftSilent(src, sampleCount, dst, g.volR); break;
    case SmixPanHandler::RightSilent: Smix_mixFramesRightSilent(src, sampleCount, dst, g.volL); break;
    case SmixPanHandler::FullCentre:  Smix_mixFramesFullCentre(src, sampleCount, dst); break;
    case SmixPanHandler::RightFull:   Smix_mixFramesRightFull(src, sampleCount, dst, g.volR); break;
    case SmixPanHandler::LeftFull:    Smix_mixFramesLeftFull(src, sampleCount, dst, g.volL); break;
    case SmixPanHandler::General:     Smix_mixFramesGeneral(src, sampleCount, dst, g.volL, g.volR); break;
    }
}

} // namespace wc98::audio
