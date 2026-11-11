#pragma once
// WC98 SMIX DirectSound 2D voice record (mix\smixer.c, base DAT_00661068 stride 0xDE4):
// active flag @+0x00, pan s8 pair @+0x6C/+0x6D written by the FUN_00548a58 pan writer with
// prev-pair compare (+0x69/+0x6A) -> signed ramp direction flags +0x6E/+0x6F {1 = up, 0xFF = down}
// and ramp-helper arming (FUN_00548a08); frequency setter FUN_005489c0 clamps to 0x80000.
// Grammar dossier: analysis/pass17audio/{NOTES.md audio7/audio12,DECOMPILE2/dsnd_548*.txt}.
#include <array>
#include <cstdint>
#include "wc98/audio/smix_mixer.hpp"

namespace wc98::audio {

// SMIX init (FUN_00548098) zeroes 16 voice slots of stride 0xDE4.
inline constexpr int kSmixVoiceCount = 16;
inline constexpr std::uint32_t kSmixVoiceStride = 0xDE4;

// One voice record, modeled at the documented byte offsets. The exe ramps on a dword register
// band: {raw[0]@+0x00, raw[1]@+0x04, raw[2]@+0x08, raw[5]@+0x14} with flag byte @+0x03; the
// pan-law installer floats (FUN_0054ccfe) live in SmixVoiceGain.
struct SmixVoice {
    bool active = false;                     // +0x00: gate for every dsnd_* writer
    std::int8_t prevPanL = 0;                // +0x69 previous L (s8), compared signed
    std::int8_t prevPanR = 0;                // +0x6A previous R (s8)
    std::int8_t panL = 0;                    // +0x6C current L (s8, first int / EAX)
    std::int8_t panR = 0;                    // +0x6D current R (s8, second int / EBX)
    std::uint8_t rampDirL = 0;               // +0x6E {1 = rising, 0xFF = falling}
    std::uint8_t rampDirR = 0;               // +0x6F
    std::uint8_t rampStop = 0;               // +0x03 helper stop flag
    std::array<std::uint32_t, 6> raw{};      // @+0x00,+0x04..+0x14 dword band (little-endian)
    std::uint32_t lastFrequency = 0;         // clamped SetFrequency arg model (FUN_005489c0)

    // FUN_0054ccfe: install the L/R volume ints as float mix gains + handler.
    void setVolume(std::int32_t volL, std::int32_t volR) { gain = Smix_install(volL, volR); }
    [[nodiscard]] const SmixVoiceGain& gainRef() const { return gain; }

private:
    SmixVoiceGain gain;
};

// The exe table is the global DAT_00661068 block (16 slots x 0xDE4).
inline std::array<SmixVoice, kSmixVoiceCount>& smixVoices() {
    static std::array<SmixVoice, kSmixVoiceCount> table;
    return table;
}

// FUN_00548a08 ramp helper on the dword band (byte arithmetic via >>24 extracts):
// stop flag set -> clear the byte at +0x08 (low byte of raw[2]) and return; else stage the
// midpoint ((raw[0]>>24)+(raw[1]>>24)>>1) into that byte, then OVERWRITE it with the
// scaled form (raw[0]>>24)*(raw[5]>>24)>>7 — the exe stores both, second write wins.
inline void Smix_rampStep(SmixVoice& v) {
    if (v.rampStop == 0) {
        v.raw[2] &= ~0xFFu;  // *(char*)(EAX+2) = 0: low byte of the dword @+0x08.
        return;
    }
    const auto hi = [](std::uint32_t w) { return static_cast<std::int32_t>(w) >> 24; };
    const std::int32_t a = hi(v.raw[0]);
    const std::int32_t b = hi(v.raw[1]);
    const std::int32_t c = hi(v.raw[5]);
    const auto mid = static_cast<std::uint8_t>(static_cast<char>((a + b) >> 1));
    v.raw[2] = (v.raw[2] & ~0xFFu) | mid;                              // first store (exe, dead)
    const auto scaled = static_cast<std::uint8_t>(static_cast<char>((a * c) >> 7));
    v.raw[2] = (v.raw[2] & ~0xFFu) | scaled;                           // second store wins
    // exe tail: (*voice+0xDDC)(voice+0xDC4, c, c, param_1, param_2) — native backend serve hook.
}

// FUN_00548a58 2D pan writer: voice index, new L (EAX) / R (EBX). Inactive slot -> -1
// (exe returns 0xffffffff); stores s8 pair, sets signed ramp flags on prev<new compare
// (equality falls to the 0xFF branch), and always arms the ramp helper.
inline std::int32_t Smix_setPan(SmixVoice& v, std::int32_t newL, std::int32_t newR) {
    if (!v.active) return -1;
    v.panL = static_cast<std::int8_t>(newL);
    v.panR = static_cast<std::int8_t>(newR);
    v.rampDirL = (static_cast<int>(v.prevPanL) < newL) ? 1 : 0xFF;
    v.rampDirR = (static_cast<int>(v.prevPanR) < newR) ? 1 : 0xFF;
    Smix_rampStep(v);  // FUN_00548a08(param_1): both decompile branches call it.
    return 0;
}

// Ramp-completion promotion: the exe's +0x69/+0x6A prev pair is NOT written by FUN_00548a58;
// it updates on ramp completion (state machine outside the pan writer). Model the commit as a
// copy of the current s8 pan pair into the prev slots.
inline void Smix_commitRamp(SmixVoice& v) { v.prevPanL = v.panL; v.prevPanR = v.panR; }

// FUN_005489c0 frequency setter: active gate, then clamp the value to 0x80000 before the
// buffer-object vtable[0] call (native backend = SDL stream rate). Returns -1 when inactive.
inline std::int32_t Smix_setFrequency(SmixVoice& v, std::uint32_t freq) {
    if (!v.active) return -1;
    const std::uint32_t clamped = freq > 0x80000 ? 0x80000 : freq;
    v.lastFrequency = clamped;
    return static_cast<std::int32_t>(clamped);
}

} // namespace wc98::audio
