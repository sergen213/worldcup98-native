// SND* channel-layer grammar (pass-18envelope, D-54 residual): the exe-side EA sound
// manager over one unified voice record (base DAT_006597e8, stride 0x60, 16 slots).
// Pure functions mirroring: SNDautovol ramp arm FUN_004e2b30 (cmn_sautovol.c), SNDvol setter
// rows FUN_0052c71c/FUN_0052c660 (cmn_svol.c), pan law compose FUN_0052f1f0 (cmn_span.c),
// volume compose FUN_00544bd0 (/127^3 + curve override), pitch compose FUN_00544b50,
// ADSR arm rows in iSNDplaytaggedpatch core FUN_0052e394 (cmn_stagpat.c) and release
// re-arm FUN_0052f020 (cmn_srelease.c). The per-tick ramp application is NOT statically
// reachable (xref scan on DAT_00659800/804/808; see analysis/pass18envelope/NOTES.md) and
// stays Open. Dossier: analysis/pass18envelope/{NOTES.md,DECOMPILE/}. Gate: test_voice_layer.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wc98::audio {

// Voice record geometry (DAT_006597e8; fanout end DAT_00659de8 = base + 16*0x60).
inline constexpr std::size_t kSndVoiceStride = 0x60;
inline constexpr int kSndVoiceSlots = 16;
inline constexpr std::size_t kSndOffHandle = 0x00;    // signed, <0 invalid
inline constexpr std::size_t kSndOffType = 0x0C;      // ==1 active voice
inline constexpr std::size_t kSndOffPitchBase = 0x0E; // word
inline constexpr std::size_t kSndOffPitchOut = 0x12;  // composed pitch word
inline constexpr std::size_t kSndOffPanByte = 0x0F;   // pan law result (char)
inline constexpr std::size_t kSndOffRampStep = 0x18;  // Q16 per-tick delta, signed
inline constexpr std::size_t kSndOffVolTarget = 0x1C; // Q16 = vol << 16
inline constexpr std::size_t kSndOffVolCurrent = 0x20;// Q16 = vol << 16
inline constexpr std::size_t kSndOffAdsrStep = 0x24;  // Q16 signed (ADSR lane)
inline constexpr std::size_t kSndOffVol2 = 0x28;      // ADSR volume Q16
inline constexpr std::size_t kSndOffEnvTime = 0x2C;   // segment time, ticks
inline constexpr std::size_t kSndOffFinalVol = 0x31;  // composed volume byte
inline constexpr std::size_t kSndOffBendByte = 0x33;  // pitchbend (center 0x40)
inline constexpr std::size_t kSndOffScaleByte = 0x34; // curve scale byte
inline constexpr std::size_t kSndOffPanOffset = 0x2F; // pan law offset byte
inline constexpr std::size_t kSndOffEnvSegIdx = 0x35; // envelope segment index byte
inline constexpr std::size_t kSndOffTrigByte = 0x36;  // release trigger (signed)
inline constexpr std::size_t kSndOffGroupTag = 0x3B;  // group fanout tag byte
inline constexpr std::size_t kSndOffPanMode = 0x37;   // ==1 direct pass, else scale byte
inline constexpr std::size_t kSndOffCurveIdx = 0x3E;  // curve index byte
inline constexpr std::size_t kSndOffPitchQ16 = 0x40;  // pitch Q16 scale dword
inline constexpr std::size_t kSndOffBendGate = 0x42;  // bend gate word (==0 inert)
inline constexpr std::size_t kSndOffEnvPtr = 0x48;    // {s32 time, s32 value} pairs stride 8
inline constexpr std::size_t kSndOffPitchCurve = 0x54;// signed char table ptr (bend)
inline constexpr std::size_t kSndOffVolCurve = 0x58;  // signed char table ptr (pan law + vol)
inline constexpr std::size_t kSndOffPanCurve = 0x5C;  // signed char table ptr (pitch term)

// Volume compose normalization: the exe divides by the literal 0x1F417F (= 127^3).
inline constexpr int kSndVolComposeDiv = 0x1F417F;

// SNDautovol gate row (cmn_sautovol.c line 0x61): target accepted in [-1, 0x7f].
constexpr bool Snd_autovolTargetOk(int targetVol) { return targetVol >= -1 && targetVol <= 0x7f; }

struct SndRampArm {
    std::int32_t targetQ16; // record +0x1C
    std::int32_t stepQ16;   // record +0x18 = (target - current) / ticks, C truncation
};

// FUN_004e2b30 rows: ramp arm over the Q16 triple. ticks clamped to >= 1 (exe `if (p<1) p=1`).
constexpr SndRampArm Snd_autovolRamp(int targetVol, std::int32_t currentQ16, int ticks) {
    const int t = ticks < 1 ? 1 : ticks;
    const std::int32_t target = static_cast<std::int32_t>(targetVol) << 16;
    return {target, (target - currentQ16) / t};
}

// FUN_0052c71c -> FUN_0052c660 rows: immediate volume set. vol in [0,0x80); the exe skips
// when (vol<<16)==current and writes step=0 on change.
constexpr bool Snd_volSetOk(int vol) { return vol >= 0 && vol < 0x80; }
constexpr std::int32_t Snd_volSetNewCurrent(int vol) { return static_cast<std::int32_t>(vol) << 16; }

// FUN_0052f1f0 pan-law compose (cmn_span.c line 0x65): mode==1 direct, else scale*(pan-0x40)+0x40;
// + offset - 0x40; clamp [0,0x7f]; signed-char curve table lookup when installed. The stored
// record byte is the char-truncated result (exe `*(...+0xf) = (char)iVar3`).
struct SndPanLaw {
    std::uint8_t modeScale;  // record +0x37
    std::uint8_t offsetByte; // record +0x2F (exe adds dword>>24 as an unsigned byte)
    const signed char* volCurve; // record +0x58 (nullable)
};
constexpr int Snd_panCompose(std::uint8_t panQ7, const SndPanLaw& law) {
    int v = panQ7;
    if (law.modeScale != 1)
        v = static_cast<int>(law.modeScale) * (v - 0x40) + 0x40;
    v += static_cast<int>(law.offsetByte) - 0x40;
    if (v < 0) v = 0;
    else if (v > 0x7f) v = 0x7f;
    return law.volCurve ? static_cast<int>(law.volCurve[v]) : v;
}

// FUN_00544bd0 volume compose: master * currentQ16hi * groupVolByte * vol2Q16hi / 127^3.
constexpr int Snd_volCompose(std::uint8_t masterByte, std::int32_t currentQ16,
                             std::uint8_t groupVolByte, std::int32_t vol2Q16) {
    return (static_cast<int>(masterByte) * static_cast<int>(currentQ16 >> 16) *
            static_cast<int>(groupVolByte) * static_cast<int>(vol2Q16 >> 16)) / kSndVolComposeDiv;
}

// FUN_00544bd0 curve-override row: (curve[idx] * scaleByte) / 0x7f.
constexpr int Snd_volComposeCurve(const signed char* curve, std::uint8_t idx, std::uint8_t scaleByte) {
    return (static_cast<int>(curve[idx]) * static_cast<int>(scaleByte)) / 0x7f;
}

// FUN_00544b50 pitch compose: word@+0x12 base; bend term gated on word@+0x42 != 0 with the
// bend byte optionally curve-mapped via ptr@+0x54; optional pan-curve term via ptr@+0x5C.
struct SndPitchState {
    std::int16_t composed;   // record +0x12 (in/out)
    std::uint8_t bendByte;   // record +0x33
    std::int16_t bendGate;   // record +0x42
    std::int32_t pitchQ16;   // record +0x40 (hi word = scale)
    const signed char* bendCurve; // record +0x54 (nullable)
    const signed char* panCurve;  // record +0x5C (nullable)
    std::uint8_t curveIdx;   // record +0x3E
};
inline std::int16_t Snd_pitchCompose(const SndPitchState& p) {
    int s = p.composed;
    if (p.bendGate != 0) {
        const int pb = p.bendCurve ? static_cast<int>(p.bendCurve[p.bendByte]) : p.bendByte;
        s += static_cast<std::int16_t>((pb - 0x40) * static_cast<int>(p.pitchQ16 >> 16) >> 6);
    }
    if (p.panCurve != nullptr)
        s += static_cast<std::int16_t>(
            (static_cast<int>(p.panCurve[p.curveIdx]) - 0x40) *
                static_cast<int>(p.pitchQ16 >> 16) >> 6);
    return static_cast<std::int16_t>(s);
}

// Envelope segment record: {time, value} stride-8 pair at the +0x48 envelope pointer.
struct SndEnvSeg {
    std::int32_t timeTicks;
    std::int32_t valueQ7ish; // Q16 = value << 16 in the exe rows
};

// FUN_0052e394 ADSR arm rows (cmn_stagpat.c line 0x178 malformed row): time<0 saturates to
// 0x7fffffff, time==0 raises the "ENVELOPE IS MALFORMED" flag; step(+0x24) recomputed from
// vol2. `malformed` suppresses the division (exe divides by zero after signalling).
struct SndAdsrArm {
    std::int32_t timeTicks; // record +0x2C
    std::int32_t stepQ16;   // record +0x24
    bool malformed;         // env[0].time == 0 (line 0x178)
};
constexpr SndAdsrArm Snd_adsrArm(const SndEnvSeg& seg0, std::int32_t vol2Q16) {
    if (seg0.timeTicks == 0)
        return {0, 0, true};
    const std::int32_t time = seg0.timeTicks < 0 ? 0x7fffffff : seg0.timeTicks;
    return {time, ((seg0.valueQ7ish << 16) - vol2Q16) / time, false};
}

// FUN_0052f020 SNDrelease rows (cmn_srelease.c lines 0x8c/0xae): trigger<0 stops the voice;
// a rising segment index re-arms {time, step} from env[seg] over vol2.
struct SndReleaseResult {
    bool stopRequested;        // trig byte @+0x36 < 0 -> iSNDstop dispatch
    bool advanced;             // newSeg > curSeg
    std::int32_t timeTicks;    // record +0x2C (raw store, no clamp on this path)
    std::int32_t stepQ16;      // record +0x24
    bool malformed;            // env[seg].time == 0 ("ENVELOPE IS MALFORMED")
};
constexpr SndReleaseResult Snd_adsrRelease(std::int8_t trigByte, int curSeg, int newSeg,
                                           const SndEnvSeg& seg, std::int32_t vol2Q16) {
    if (trigByte < 0)
        return {true, false, 0, 0, false};
    if (newSeg <= curSeg)
        return {false, false, 0, 0, false};
    if (seg.timeTicks == 0)
        return {false, true, seg.timeTicks, 0, true};
    return {false, true, seg.timeTicks, ((seg.valueQ7ish << 16) - vol2Q16) / seg.timeTicks, false};
}

static_assert(kSndVolComposeDiv == 127 * 127 * 127, "the exe divisor is 127^3");
static_assert(kSndVoiceStride == 0x60 && kSndVoiceSlots == 16, "record geometry");

} // namespace wc98::audio
