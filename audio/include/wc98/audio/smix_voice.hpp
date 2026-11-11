#pragma once
// WC98 SMIX DirectSound 2D voice layer (mix\smixer.c + win\sdriver.c, base DAT_00661068
// stride 0xDE4): active flag @+0x00 gates every dsnd_* writer; the FUN_00548a58 pan writer
// stores the s8 pair @+0x6C/+0x6D with prev-pair compare (+0x69/+0x6A) -> signed ramp
// direction flags +0x6E/+0x6F {1 = up, 0xFF = down/equal} and always arms the ramp helper
// FUN_00548a08; the volume/gate byte @+0x6B (FUN_00548ab8) drives it. Ramp output byte
// @+0x71 (init 0xFF in iSNDmixstart). Frequency setter FUN_005489c0 clamps to 0x80000;
// pitch path = FUN_005492b0 centi-ratio table (DAT_00596144, 2^(2^k/1200)) * base + ROUND.
// iSNDplatformPlay FUN_0052d290 parses the TLV sample-format header over the stream
// (tags {0x81 rate, 0x82 loop mode, 0x83 bits, 0x84 waveformat tag u16 default 0x5622,
// 0x85 loop start, 0x86 loop end, 0x87 aux, 0x8a data ptr, 0x91 volume trim, 0x92 play mode,
// 0x93 aux data}, terminator 0xff/0xfe). Serve pump (*DAT_0066eeac) installed by SMIX init
// FUN_00548098; stop voice FUN_00548960 = flag clear + serve call.
// Grammar dossier: analysis/pass17audio/{NOTES.md audio6-audio15,DECOMPILE2/} +
// analysis/pass18voice/ (tlvval_52e160.txt, table_596144.txt).
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "wc98/audio/smix_mixer.hpp"

namespace wc98::audio {

// SMIX init (FUN_00548098) zeroes 16 voice slots of stride 0xDE4.
inline constexpr int kSmixVoiceCount = 16;
inline constexpr std::uint32_t kSmixVoiceStride = 0xDE4;

// Frequency-set clamp ceiling (FUN_005489c0: `if (0x80000 < val) val = 0x80000`).
inline constexpr std::uint32_t kSmixFreqClamp = 0x80000;

// Rate-tag table from SMIX init FUN_00548098 (audio8): the u16 waveformat tag is the rate.
inline constexpr std::uint16_t kSmixRateTags[] = {8000, 11025, 16000, 22050, 32000, 44100};
// Default waveformat tag in iSNDplatformPlay: 0x5622 = 22050 Hz mono16.
inline constexpr std::uint16_t kSmixDefaultWaveFormatTag = 0x5622;

// One voice record, modeled at the documented byte offsets (X = slot base + 0x68 view for
// the ramp helper: gate @+0x6B reads X+3, targets @+0x6C/+0x6D read X+4/X+5, output @+0x71).
struct SmixVoice {
    bool active = false;                     // +0x00: gate for every dsnd_* writer
    std::int8_t prevPanL = 0;                // +0x69 previous L (s8), compared signed
    std::int8_t prevPanR = 0;                // +0x6A previous R (s8)
    std::uint8_t gateVol = 0;                // +0x6B volume/gate byte (FUN_00548ab8, init param_8)
    std::int8_t panL = 0;                    // +0x6C current/target L (s8, first int / EAX)
    std::int8_t panR = 0;                    // +0x6D current/target R (s8, second int / EBX)
    std::uint8_t rampDirL = 0;               // +0x6E {1 = rising, 0xFF = falling/equal}
    std::uint8_t rampDirR = 0;               // +0x6F
    std::uint8_t rampOut = 0;                // +0x71 Q7-scaled ramp output byte (init 0xFF)
    std::uint32_t lastFrequency = 0;         // clamped SetFrequency arg model (FUN_005489c0)
    std::uint32_t rate = kSmixDefaultWaveFormatTag; // TLV waveformat tag -> Hz (FUN_0052d290)

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

// Serve pump slot (*DAT_0066eeac): installed by SMIX init FUN_00548098, invoked by the
// stop-voice writer FUN_00548960 after clearing the active flag.
using SmixServePump = void (*)();
inline SmixServePump& smixServePumpSlot() {
    static SmixServePump slot = nullptr;
    return slot;
}
inline void Smix_callServePump() {
    if (auto* fn = smixServePumpSlot())
        fn();
}

// FUN_00548a08 ramp helper, faithful byte arithmetic on the X = record+0x68 window:
// gate/mult byte @X+3 (+0x6B), target L/R s8 @X+4/+0x6C/X+5 (targets are the stored pan pair),
// output byte @X+8 (+0x71). Gate zero -> output = 0. Else the exe stages the midpoint
// ((s8 X+4) + (s8 X+5)) >> 1 (sign-extended ints, arithmetic shift) and OVERWRITES the same
// byte with the Q7-scaled form (gate * midpoint) >> 7; the scaled store is what survives.
// The exe tail calls (*X+0xDDC)(X+0xDC4, out, out, ...) — native side commits the output via
// the installer volume lane; gate test pins the byte rows.
inline std::uint8_t Smix_rampStep(SmixVoice& v) {
    const int g = static_cast<std::int8_t>(v.gateVol);  // exe reads *(int*)X >> 0x18 (signed)
    if (g == 0) {
        v.rampOut = 0;                                   // *(char*)(EAX+2) = 0
        return 0;
    }
    const int mid = (static_cast<int>(v.panL) + static_cast<int>(v.panR)) >> 1;
    const auto scaled = static_cast<std::uint8_t>(static_cast<char>((g * mid) >> 7));
    v.rampOut = scaled;                                  // second store wins over midpoint
    return scaled;
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
// it updates on ramp completion (FUN_00548808 steps cur toward target, then commits). Model
// the commit as a copy of the current s8 pan pair into the prev slots.
inline void Smix_commitRamp(SmixVoice& v) { v.prevPanL = v.panL; v.prevPanR = v.panR; }

// FUN_00548ab8 volume/gate byte writer: stores +0x6B and arms the ramp helper.
inline std::int32_t Smix_setGateVolume(SmixVoice& v, std::uint8_t gate) {
    if (!v.active) return -1;
    v.gateVol = gate;
    Smix_rampStep(v);
    return 0;
}

// FUN_005483bc iSNDmixstart voice-start grammar (the ctx scaffolding rows): record clears,
// output byte @+0x71 = 0xFF, prev pair (+0x69/+0x6A) and target pair (+0x6C/+0x6D) get the
// initial L/R ints, gate byte +0x6B gets param_8, then FUN_00548a08 arms (computed with the
// fresh pair), installer runs on the initial ints, and the active flag is set last.
inline void Smix_startVoice(SmixVoice& v, std::int32_t volL, std::int32_t volR, std::uint8_t gate) {
    v.active = false;
    v.rampOut = 0xFF;                                   // (&DAT_00661071)[slot] = 0xff
    v.prevPanL = static_cast<std::int8_t>(volL);        // +0x69/+0x6A = param_4/param_5
    v.prevPanR = static_cast<std::int8_t>(volR);
    v.panL = v.prevPanL;                                // +0x6C/+0x6D = same ints
    v.panR = v.prevPanR;
    v.rampDirL = 0;                                     // ramp flags start neutral
    v.rampDirR = 0;
    v.gateVol = gate;                                   // +0x6B = param_8
    Smix_rampStep(v);                                   // FUN_00548a08() pre-vtable
    v.setVolume(volL, volR);                            // FUN_0054ccfe installer on the ints
    v.active = true;                                    // (&DAT_00661068)[slot] = 1 last
}

// FUN_00548960 stop voice: inactive -> -1 (exe CONCAT44 form); active path commits the pan,
// releases the scratch (PTR_FUN_005954ec memset-family call: gate/output go silent), clears
// the flag, and runs the global serve pump (*DAT_0066eeac)().
inline std::int32_t Smix_stopVoice(SmixVoice& v) {
    if (!v.active) return -1;
    Smix_commitRamp(v);                                 // FUN_00548a58() tail call
    v.gateVol = 0;                                      // scratch release: voice silent
    v.rampOut = 0;
    v.active = false;                                   // (&DAT_00661068)[slot] = '\0'
    Smix_callServePump();                               // (*DAT_0066eeac)()
    return 0;
}

// FUN_005489c0 frequency setter: active gate, then clamp the value to 0x80000 before the
// buffer-object vtable[0] call (native backend = SDL stream rate). Returns -1 when inactive.
inline std::int32_t Smix_setFrequency(SmixVoice& v, std::uint32_t freq) {
    if (!v.active) return -1;
    const std::uint32_t clamped = freq > kSmixFreqClamp ? kSmixFreqClamp : freq;
    v.lastFrequency = clamped;
    return static_cast<std::int32_t>(clamped);
}

// FUN_005492b0 centi-ratio stage: product over the set bits of |cents| of the DAT_00596144
// float table (entry k = 2^(2^k/1200)); negative cents divide. Loop bounds replicate the exe
// walk (`while ((uVar3 & v) == 0)` bit scan, stop once uVar3 exceeds |v|). Bits >= 13 read
// past the decoded table entries (garbage @+0x34/0x3C); callers stay in ±8192.
inline float Smix_pitchScale(int cents) {
    // DAT_00596144..0x596174, exact stored floats (analysis/pass18voice/table_596144.txt).
    static constexpr float kPowerTable[13] = {
        1.0005780458f, 1.0011559725f, 1.0023130178f, 1.0046319962f,
        1.0092849731f, 1.0186560154f, 1.0376600027f, 1.0767379999f,
        1.1593639851f, 1.3441239595f, 1.8066699505f, 3.2640581131f,
        10.6540737152f,
    };
    float scale = 1.0f;
    unsigned int uVar3 = 1;
    int byteIndex = 0;  // exe walks the table in dword steps (iVar2 += 4)
    if (cents < 0) {
        const unsigned int mag = static_cast<unsigned int>(-cents);
        if (static_cast<int>(mag) > 0) {
            do {
                while ((uVar3 & mag) == 0) {
                    uVar3 *= 2;
                    byteIndex += 4;
                    if (static_cast<int>(mag) < static_cast<int>(uVar3))
                        return scale;  // exe goto LAB_005492d8
                }
                scale = scale / kPowerTable[byteIndex / 4];
                uVar3 *= 2;
                byteIndex += 4;
            } while (static_cast<int>(uVar3) <= static_cast<int>(mag));
        }
    } else if (cents > 0) {
        const unsigned int mag = static_cast<unsigned int>(cents);
        do {
            if ((uVar3 & mag) != 0)
                scale = scale * kPowerTable[byteIndex / 4];
            uVar3 *= 2;
            byteIndex += 4;
        } while (static_cast<int>(uVar3) <= static_cast<int>(mag));
    }
    return scale;
}

// FUN_005490b0 pitch value: stage + FUN_004973b0 float scale -> ROUND, i.e. the frequency
// the 2D path feeds to SetFrequency (FUN_005489c0 clamps). x87 ROUND = round-to-nearest
// with ties to even under the default FP environment; std::nearbyint matches that mode.
inline std::uint32_t Smix_pitchedFrequency(std::uint32_t baseRate, int cents) {
    const double scaled = static_cast<double>(baseRate) * static_cast<double>(Smix_pitchScale(cents));
    return static_cast<std::uint32_t>(std::nearbyint(scaled));
}

// FUN_005489c0 semantics applied to a pitched value: clamp 0x80000.
inline std::uint32_t Smix_clampFrequency(std::uint32_t freq) {
    return freq > kSmixFreqClamp ? kSmixFreqClamp : freq;
}

// ---- TLV sample-format header (FUN_0052d290 iSNDplatformPlay parse loop) -----------------

struct SmixTlvSample {
    // Exe locals with their init defaults (Ghidra names from the decompile).
    int sampleBits = 0x10;                  // local_18 (tag 0x81): 8 / 0x10 bits
    int channelMode = 1;                    // local_44 (tag 0x82): {1,2} gate the 2D mix path
    std::uint16_t waveFormatTag = kSmixDefaultWaveFormatTag;  // uVar2 (rate tag, default 0x5622)
    int bitsField = 0;                      // local_20 (tag 0x83)
    std::uint32_t loopStart = 0;            // local_24 (tag 0x85)
    std::int32_t loopEnd = -1;              // iVar7   (tag 0x86)
    std::int32_t auxField = -1;             // local_1c (tag 0x87, sets local_2c flag)
    bool auxSet = false;                    // local_2c
    int extraTrim = 0;                      // iVar9 (tag 0x91): + DAT_00659e64 clamp [0,100]
    int playMode = 0;                       // local_14 (tag 0x92) {0/2=2D, 1=Dolby, 3=DS3D}
    std::int32_t dataOffset = -1;           // local_28: payload byte index after the header
    std::int32_t auxDataOffset = -1;        // local_30 (tag 0x93)
    bool ok = false;                        // ran to the 0xff/0xfe terminator inside bounds
};

// FUN_0052e160: width-byte big-endian integer, two's-complement sign-extended at the width
// boundary (width 1: >0x7F -> -0x100; 2: >0x7FFF -> -0x10000; 3: >0x7FFFFF -> -0x1000000).
inline std::int32_t Smix_tlvValue(const std::uint8_t* p, int width) {
    std::int32_t v = 0;
    for (int i = 0; i < width; ++i)
        v = v * 0x100 + static_cast<unsigned>(p[i]);
    if (width == 1 && v > 0x7F) v -= 0x100;
    else if (width == 2 && v > 0x7FFF) v -= 0x10000;
    else if (width == 3 && v > 0x7FFFFF) v -= 0x1000000;
    return v;
}

// Pure TLV parse over a sample block. Tag loop per FUN_0052d290: tag byte, stop at the
// 0xff/0xfe terminators; 0xfc is the exe's escape-guard branch (consumes the tag only);
// each entry = (u8 tag, u8 len, BE value of len bytes) via FUN_0052e160. len == 0xff ->
// extended form: 4-byte BE count + one pad byte [INFERENCE: ECX plumbing opaque], value
// width = that count. Pointer tags {0x8a data, 0x93 aux} capture the current payload index
// and skip their bytes; unknown tags skip len bytes (exe default arm). Post-rows mirror the
// exe tail clamp (non-EBX2 path); the EBX==2 force-mono override is caller-side.
inline SmixTlvSample parseSmixTlv(const std::uint8_t* data, std::size_t size) {
    SmixTlvSample out;
    std::size_t pos = 0;
    while (pos < size) {
        const std::uint8_t tag = data[pos++];
        if (tag == 0xff || tag == 0xfe) {
            out.ok = true;
            break;
        }
        if (tag == 0xfc) continue;    // escape: exe skips processing for this tag byte
        if (pos >= size) break;       // exe reads past the stream; port guards.
        unsigned width = data[pos++];
        if (width == 0xff) {          // extended count form (4-byte BE + pad byte)
            if (pos + 5 > size) break;
            width = static_cast<unsigned>(Smix_tlvValue(data + pos, 4)) & 0xFFFFu;
            pos += 5;
        }
        const std::size_t valuePos = pos;
        if (width == 0 || width > size - pos) break;  // zero/truncated: stop at the overrun
        switch (tag) {
        case 0x81: out.sampleBits = Smix_tlvValue(data + valuePos, static_cast<int>(width)); break;
        case 0x82: out.channelMode = Smix_tlvValue(data + valuePos, static_cast<int>(width)); break;
        case 0x83: out.bitsField = Smix_tlvValue(data + valuePos, static_cast<int>(width)); break;
        case 0x84: {
            const int v = Smix_tlvValue(data + valuePos, static_cast<int>(width));
            out.waveFormatTag = static_cast<std::uint16_t>(v);
            break;
        }
        case 0x85: out.loopStart = static_cast<std::uint32_t>(Smix_tlvValue(data + valuePos, static_cast<int>(width))); break;
        case 0x86: out.loopEnd = Smix_tlvValue(data + valuePos, static_cast<int>(width)); break;
        case 0x87: out.auxField = Smix_tlvValue(data + valuePos, static_cast<int>(width)); out.auxSet = true; break;
        case 0x8a: out.dataOffset = static_cast<std::int32_t>(valuePos); break;      // payload ptr arm
        case 0x91: out.extraTrim = Smix_tlvValue(data + valuePos, static_cast<int>(width)); break;
        case 0x92: out.playMode = Smix_tlvValue(data + valuePos, static_cast<int>(width)); break;
        case 0x93: out.auxDataOffset = static_cast<std::int32_t>(valuePos); break;   // aux ptr arm
        default: break;  // unknown tag: skip payload
        }
        pos = valuePos + width;  // every entry advances by its (possibly extended) length
    }
    // exe tail clamps (non-EBX2 path): values below -1 normalize to the -1 sentinel.
    if (out.loopEnd < -1) out.loopEnd = -1;
    if (out.auxField < -1) out.auxField = -1;
    return out;
}

// Rate-tag lookup: the u16 waveformat tag IS the Hz value for the six SMIX init rates.
inline std::uint32_t Smix_rateFromTag(std::uint16_t tag) {
    for (const std::uint32_t r : kSmixRateTags)
        if (r == tag) return r;
    return 0;
}

// Voice start from a sample block: parse the TLV format header, map the waveformat tag to
// the voice rate, then run the iSNDmixstart record grammar.
inline void Smix_startVoiceFromSample(SmixVoice& v, const std::uint8_t* data, std::size_t size,
                                      std::uint8_t gate) {
    const SmixTlvSample tlv = parseSmixTlv(data, size);
    if (const std::uint32_t hz = Smix_rateFromTag(tlv.waveFormatTag))
        v.rate = hz;
    Smix_startVoice(v, 0x7F, 0x7F, gate);
}

} // namespace wc98::audio
