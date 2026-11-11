// SMIX voice-layer grammar (pass-18voice): ramp helper FUN_00548a08 byte rows on the
// record+0x68 window (gate/mult @+0x6B, targets @+0x6C/+0x6D midpoint, Q7 scaled output
// @+0x71), pan setter prev-compare + direction flags (FUN_00548a58), iSNDmixstart voice
// start (FUN_005483bc record rows) and stop (FUN_00548960 flag clear + serve pump
// (*DAT_0066eeac)), FUN_005489c0 frequency clamp boundaries, the FUN_005492b0 centi-ratio
// pitch table (DAT_00596144 = 2^(2^k/1200)) feeding ROUND + SetFrequency, and the TLV
// sample-format parser of iSNDplatformPlay FUN_0052d290 (+ FUN_0052e160 sign-extend rows).
// Grammar: analysis/pass17audio/{NOTES.md audio6-audio15,DECOMPILE2/} + pass18voice/.
// SND channel envelope rows (pass-18envelope): autovol ramp, vol-set, pan law, volume/pitch
// compose, ADSR arm/release. Dossier: analysis/pass18envelope/{NOTES.md,DECOMPILE/}.
#include "check.hpp"

#include "wc98/audio/smix_voice.hpp"
#include "wc98/audio/snd_channel.hpp"
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

using namespace wc98::audio;

namespace {
std::uint32_t gServeCalls = 0;
void servePumpProbe() { ++gServeCalls; }

// Byte-sink helper: build a TLV stream from (tag, len, value-byets) triples.
void appendTag(std::vector<std::uint8_t>& v, std::uint8_t tag, std::initializer_list<std::uint8_t> bytes) {
    v.push_back(tag);
    v.push_back(static_cast<std::uint8_t>(bytes.size()));
    for (const std::uint8_t b : bytes)
        v.push_back(b);
}

// Independent replica of the exe centi-ratio product for boundary checks (float steps,
// same rounding as the port).
float expectedScale(int cents) {
    static constexpr float kTab[13] = {
        1.0005780458f, 1.0011559725f, 1.0023130178f, 1.0046319962f,
        1.0092849731f, 1.0186560154f, 1.0376600027f, 1.0767379999f,
        1.1593639851f, 1.3441239595f, 1.8066699505f, 3.2640581131f,
        10.6540737152f,
    };
    float s = 1.0f;
    const unsigned mag = cents < 0 ? static_cast<unsigned>(-cents) : static_cast<unsigned>(cents);
    for (unsigned k = 0, bit = 1; bit <= mag && k < 13; ++k, bit *= 2) {
        if ((mag & bit) == 0) continue;
        if (cents < 0) s = s / kTab[k];
        else s = s * kTab[k];
    }
    return s;
}
} // namespace

int main() {
    // ---- ramp helper FUN_00548a08: exact byte rows on the record+0x68 window --------------
    {
        SmixVoice v;
        // Gate zero (+0x6B == 0): output byte @+0x71 clears, midpoint path skipped.
        v.panL = 60; v.panR = 40; v.gateVol = 0; v.rampOut = 0xAB;
        CHECK(Smix_rampStep(v) == 0);
        CHECK(v.rampOut == 0);

        // gate 0x7F: midpoint (60+40)>>1 = 50, scaled (127*50)>>7 = 49.
        v.panL = 60; v.panR = 40; v.gateVol = 0x7F;
        CHECK(Smix_rampStep(v) == ((127 * 50) >> 7));
        CHECK(v.rampOut == static_cast<std::uint8_t>((127 * 50) >> 7));

        // Negative pair: sum -20 >>1 (arithmetic) = -10; gate 0x50 -> (-500)>>7.
        v.panL = -8; v.panR = -12; v.gateVol = 0x50;
        const std::uint8_t wantNeg = static_cast<std::uint8_t>(static_cast<char>((0x50 * (-20 >> 1)) >> 7));
        CHECK(Smix_rampStep(v) == wantNeg);

        // gate byte stored as +0xFF reads signed (exe *(int*)X>>0x18): mid 50 -> -1.
        v.panL = 60; v.panR = 40; v.gateVol = 0xFF;
        CHECK(Smix_rampStep(v) == static_cast<std::uint8_t>(static_cast<char>((-1 * 50) >> 7)));

        // Convergence at i/127 fractions: gate 0x7F (unity volume byte) over the full s8
        // sweep; the installer float for the same int is exactly i*(1.0f/127).
        for (int i = 0; i <= 0x7F; ++i) {
            SmixVoice r;
            r.panL = static_cast<std::int8_t>(i);
            r.panR = static_cast<std::int8_t>(i);
            r.gateVol = 0x7F;
            CHECK(Smix_rampStep(r) == static_cast<std::uint8_t>(static_cast<char>((127 * i) >> 7)));
            const SmixVoiceGain g = Smix_install(i, i);
            CHECK(g.volL == static_cast<float>(i) * kSmixVolumeScale);
        }
        // Unity row: 0x7F scales to exactly 1.0f (DAT_00596560 installer invariant).
        CHECK(Smix_install(0x7F, 0x7F).volL == 1.0f);

        // FUN_00548ab8 gate-byte writer arms the helper on the active slot; inactive -> -1.
        SmixVoice off;
        CHECK(Smix_setGateVolume(off, 0x7F) == -1);
        SmixVoice w;
        w.active = true;
        CHECK(Smix_setGateVolume(w, 0x40) == 0 && w.gateVol == 0x40);
    }

    // ---- voice start FUN_005483bc record rows + stop FUN_00548960 serve pump --------------
    {
        SmixVoice v;
        Smix_startVoice(v, 0x40, 0x20, 0x7F);
        CHECK(v.active);                                   // flag set last (exe l.155)
        CHECK(v.rampOut == static_cast<std::uint8_t>((127 * ((64 + 32) >> 1)) >> 7));
        CHECK(v.prevPanL == 0x40 && v.prevPanR == 0x20);   // +0x69/+0x6A = initial ints
        CHECK(v.panL == 0x40 && v.panR == 0x20);           // +0x6C/+0x6D pair
        CHECK(v.rampDirL == 0 && v.rampDirR == 0);         // flags neutral at start
        CHECK(v.gateVol == 0x7F);                          // +0x6B = param_8

        smixServePumpSlot() = servePumpProbe;
        const std::uint32_t before = gServeCalls;
        CHECK(Smix_stopVoice(v) == 0);
        CHECK(gServeCalls == before + 1);                  // (*DAT_0066eeac)() ran
        CHECK(!v.active && v.gateVol == 0 && v.rampOut == 0);
        CHECK(Smix_stopVoice(v) == -1);                    // inactive gate
        CHECK(gServeCalls == before + 1);                  // no serve for the -1 return
        smixServePumpSlot() = nullptr;

        // Pan writer on a started voice: prev-pair compare sets {up, down/equal} flags.
        SmixVoice u;
        Smix_startVoice(u, 0x40, 0x20, 0x7F);
        CHECK(Smix_setPan(u, 0x50, 0x10) == 0);
        CHECK(u.rampDirL == 1 && u.rampDirR == 0xFF);      // prev 0x40<0x50 up; 0x20!…>0x10 down
        Smix_commitRamp(u);
        CHECK(Smix_setPan(u, 0x50, 0x50) == 0);
        CHECK(u.rampDirL == 0xFF && u.rampDirR == 1);      // equality is NOT rising per exe test
    }

    // ---- frequency clamp boundaries (FUN_005489c0: `if (0x80000 < v) v = 0x80000`) --------
    {
        SmixVoice v;
        Smix_startVoice(v, 0x7F, 0x7F, 0x7F);
        CHECK(Smix_setFrequency(v, 0x7FFFF) == 0x7FFFF);   // just below clamp passes through
        CHECK(Smix_setFrequency(v, 0x80000) == 0x80000);   // boundary value exact
        CHECK(Smix_setFrequency(v, 0x80001) == 0x80000);   // one above clamps
        CHECK(Smix_setFrequency(v, 0xFFFFFFFFu) == 0x80000);
        CHECK(v.lastFrequency == 0x80000);
        CHECK(Smix_clampFrequency(0x80001u) == kSmixFreqClamp);

        SmixVoice off;
        CHECK(Smix_setFrequency(off, 44100) == -1);        // inactive gate (exe 0xffffffff)
    }

    // ---- pitch path: FUN_005492b0 centi-ratio + ROUND -> SetFrequency ----------------------
    {
        // Exact stored table rows (analysis/pass18voice/table_596144.txt).
        CHECK(Smix_pitchScale(0) == 1.0f);                 // empty product
        CHECK(Smix_pitchScale(1) == 1.0005780458f);
        CHECK(Smix_pitchScale(4) == 1.0023130178f);
        CHECK(Smix_pitchScale(8) == 1.0046319962f);       // bit-3 row = table index 3
        // Bit-composite rows, exe multiplication order (bits ascending).
        CHECK(Smix_pitchScale(5) == 1.0005780458f * 1.0023130178f);
        CHECK(Smix_pitchScale(7) == 1.0005780458f * 1.0011559725f * 1.0023130178f);
        // Negative rows: divide per set bit, exe scan stops when the walker exceeds |v|.
        CHECK(Smix_pitchScale(-1) == 1.0f / 1.0005780458f);
        CHECK(Smix_pitchScale(-4) == 1.0f / 1.0023130178f);

        // Round-trip: scale(bend)*scale(-bend) ~ 1 (float steps, reciprocal pair per row set).
        for (const int b : {0, 1, 5, 64, 2032})
            CHECK(std::fabs(Smix_pitchScale(b) * Smix_pitchScale(-b) - 1.0f) < 1e-5f);

        // FUN_005490b0 pitch value = ROUND(base * scale): identity row + replica rows.
        CHECK(Smix_pitchedFrequency(22050, 0) == 22050);
        for (const int b : {1, 7, 12, 64, 300, 2032}) {
            const std::uint32_t got = Smix_pitchedFrequency(44100, b);
            CHECK(got == static_cast<std::uint32_t>(std::nearbyint(44100.0 * expectedScale(b))));
        }
        // The 2D pitchbend wrapper feeds the value into SetFrequency -> clamp boundary row:
        const std::uint32_t big = Smix_pitchedFrequency(kSmixFreqClamp, 2032);
        CHECK(Smix_clampFrequency(big) == kSmixFreqClamp); // scaled-up base lands on the ceiling

        // Boundary magnitude: bit-12 row (t12), finite and > 1.
        const float top = Smix_pitchScale(0x1FFF);
        CHECK(top > 1.0f && std::isfinite(top));
    }

    // ---- TLV sample-format parser FUN_0052d290 (+ FUN_0052e160 sign rows) -----------------
    {
        const std::uint8_t oneByte[1] = {0x80};
        CHECK(Smix_tlvValue(oneByte, 1) == -128);          // width 1: >0x7F -> -0x100
        CHECK(Smix_tlvValue(oneByte, 1) + 128 == 0);
        const std::uint8_t w2[2] = {0xFF, 0xFF};
        CHECK(Smix_tlvValue(w2, 2) == -1);                 // width 2: >0x7FFF -> -0x10000
        const std::uint8_t w3[3] = {0xFF, 0xFF, 0xFF};
        CHECK(Smix_tlvValue(w3, 3) == -1);                 // width 3: >0x7FFFFF -> -0x1000000
        const std::uint8_t w2p[2] = {0x7F, 0xFF};
        CHECK(Smix_tlvValue(w2p, 2) == 0x7FFF);            // positive boundary passes through

        // Empty stream: exe never reaches the terminator; defaults survive.
        {
            const SmixTlvSample e = parseSmixTlv(nullptr, 0);
            CHECK(!e.ok && e.waveFormatTag == kSmixDefaultWaveFormatTag && e.sampleBits == 0x10 &&
                  e.channelMode == 1 && e.loopEnd == -1 && e.dataOffset == -1);
        }

        // Terminator rows: 0xff and 0xfe close the chain.
        {
            const std::uint8_t t1[1] = {0xff};
            const std::uint8_t t2[1] = {0xfe};
            CHECK(parseSmixTlv(t1, 1).ok);
            CHECK(parseSmixTlv(t2, 1).ok);
        }

        // Full tag-table round-trip: every tagged arm + pointer arms + unknown skip.
        std::vector<std::uint8_t> s;
        appendTag(s, 0x84, {0xAC, 0x44});                  // waveformat tag -> 44100
        appendTag(s, 0x81, {0x10});                        // sampleBits = 16
        appendTag(s, 0x82, {0x02});                        // channelMode = 2 (stereo)
        appendTag(s, 0x83, {0x07});                        // bitsField
        appendTag(s, 0x85, {0x12, 0x34});                  // loopStart = 0x1234
        appendTag(s, 0x86, {0xFF, 0xFF});                  // loopEnd -> -1 (sign row)
        appendTag(s, 0x87, {0x80});                        // auxField -> -128 + auxSet
        appendTag(s, 0x91, {0x64});                        // extraTrim = 100
        appendTag(s, 0x92, {0x03});                        // playMode = DS3D
        const std::size_t dataIdx = s.size() + 2;          // tag+len then the 4 "payload" bytes
        appendTag(s, 0x8A, {0xAA, 0xBB, 0xCC, 0xDD});      // data pointer arm
        const std::size_t auxIdx = s.size() + 2;          // tag+len then the 1 "aux" byte
        appendTag(s, 0x93, {0x55});                        // aux pointer arm
        appendTag(s, 0x9A, {0x00, 0x00});                  // unknown tag: skipped by len
        s.push_back(0xFE);                                 // terminator
        const SmixTlvSample t = parseSmixTlv(s.data(), s.size());
        CHECK(t.ok);
        CHECK(t.waveFormatTag == 0xAC44 && t.sampleBits == 16 && t.channelMode == 2);
        CHECK(t.bitsField == 7 && t.loopStart == 0x1234 && t.loopEnd == -1);
        CHECK(t.auxField == -1 && t.auxSet && t.extraTrim == 100 && t.playMode == 3);
        CHECK(t.dataOffset == static_cast<std::int32_t>(dataIdx));
        CHECK(t.auxDataOffset == static_cast<std::int32_t>(auxIdx));

        // Escape tag 0xfc consumes only the tag byte; truncated stream stays not-ok.
        {
            const std::uint8_t esc[2] = {0xFC, 0xFF};
            CHECK(parseSmixTlv(esc, 2).ok);
            const std::uint8_t trunc[1] = {0x84};          // tag without len
            CHECK(!parseSmixTlv(trunc, 1).ok);
            const std::uint8_t shortVal[3] = {0x84, 0x02, 0xAC};  // len runs past end
            CHECK(!parseSmixTlv(shortVal, 3).ok);
        }

        // Rate-tag table rows (SMIX init FUN_00548098): the u16 tag IS the Hz value.
        for (const std::uint32_t hz : {8000u, 11025u, 16000u, 22050u, 32000u, 44100u})
            CHECK(Smix_rateFromTag(static_cast<std::uint16_t>(hz)) == hz);
        CHECK(Smix_rateFromTag(0x5622) == 22050);          // default mono16 tag
        CHECK(Smix_rateFromTag(0xAC44) == 44100);
        CHECK(Smix_rateFromTag(0x1234) == 0);              // unknown tag: no lane

        // Voice start wiring: TLV header -> record rate, then the iSNDmixstart rows.
        SmixVoice v;
        const std::uint8_t block[] = {0x84, 0x02, 0xAC, 0x44, 0xFF};
        Smix_startVoiceFromSample(v, block, sizeof(block), 0x7F);
        CHECK(v.active && v.rate == 44100);
    }
    // ---- SND channel envelope rows (pass-18envelope) --------------------------------
    {
        // Record geometry: fanout loop end DAT_00659de8 = base + 16*0x60.
        CHECK(kSndVoiceSlots * kSndVoiceStride == 0x600);
        CHECK(kSndOffEnvPtr + 2 * sizeof(std::int32_t) <= kSndVoiceStride);

        // SNDautovol gate row (cmn_sautovol.c line 0x61): target accepted [-1, 0x7f].
        CHECK(Snd_autovolTargetOk(-1) && Snd_autovolTargetOk(0) && Snd_autovolTargetOk(0x7f));
        CHECK(!Snd_autovolTargetOk(-2) && !Snd_autovolTargetOk(0x80));

        // Ramp arm: target<<16, step = (target - current)/ticks with ticks >= 1 clamp.
        for (int t = 0; t <= 0x7f; ++t) {
            for (const int ticks : {1, 2, 3, 5, 64}) {
                const auto arm = Snd_autovolRamp(t, 0, ticks);
                CHECK(arm.targetQ16 == static_cast<std::int32_t>(t) << 16);
                CHECK(arm.stepQ16 == (static_cast<std::int32_t>(t) << 16) / ticks);
            }
        }
        // Negative current keeps C truncation-toward-zero semantics.
        CHECK(Snd_autovolRamp(0, -0x10000, 8).stepQ16 == 0x2000);
        // ticks clamped to 1 (exe `if (ticks < 1) ticks = 1`).
        CHECK(Snd_autovolRamp(5, 0, 0).stepQ16 == 0x50000);
        CHECK(Snd_autovolRamp(5, 0, -3).stepQ16 == 0x50000);

        // SNDvol rows (cmn_svol.c line 0x92): range [0, 0x80), Q16 store.
        CHECK(Snd_volSetOk(0) && Snd_volSetOk(0x7f));
        CHECK(!Snd_volSetOk(-1) && !Snd_volSetOk(0x80));
        for (int v = 0; v < 0x80; ++v) {
            CHECK(Snd_volSetNewCurrent(v) == static_cast<std::int32_t>(v) << 16);
            CHECK((Snd_volSetNewCurrent(v) >> 16) == v);   // equality-skip reads hi word
        }
    }

    {
        // Pan law (cmn_span.c line 0x65): mode==1 direct, scale/offset compose, clamp.
        const SndPanLaw direct{1, 0x40, nullptr};
        CHECK(Snd_panCompose(0x00, direct) == 0);           // lower clamp boundary
        CHECK(Snd_panCompose(0x40, direct) == 0x40);        // center identity
        CHECK(Snd_panCompose(0x7f, direct) == 0x7f);        // upper identity
        const SndPanLaw scaled{2, 0x40, nullptr};
        CHECK(Snd_panCompose(0x60, scaled) == 0x7f);        // 2*(0x20)+0x40 = 0x80 -> clamp
        const SndPanLaw offZero{1, 0x00, nullptr};
        CHECK(Snd_panCompose(0x7f, offZero) == 0x3f);       // unsigned byte offset row
        const SndPanLaw under{0x55, 0x00, nullptr};
        CHECK(Snd_panCompose(0x00, under) == 0);            // negative -> clamp 0
        static const signed char kCurve[128] = {
            0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,
            16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,
            32,  33,  34,  35,  36,  37,  38,  39,  40, -1,  42,  43,  44,  45,  46,  47,
            48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
            64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
            80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,
            96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
            112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127};
        const SndPanLaw curved{1, 0x40, kCurve};
        CHECK(Snd_panCompose(0x2a, curved) == 42);          // identity entry row (idx 42)
        CHECK(Snd_panCompose(0x29, curved) == -1);          // signed table value (idx 41)
    }

    {
        // Volume compose (FUN_00544bd0): /127^3 normalization + curve override rows.
        for (const int vol : {0, 1, 0x3f, 0x40, 0x7e, 0x7f})
            CHECK(Snd_volCompose(127, vol << 16, 127, 127 << 16) == vol);
        CHECK(Snd_volCompose(100, 60 << 16, 90, 50 << 16) == (100 * 60 * 90 * 50) / 0x1F417F);
        static const signed char kTab[8] = {0x40, -40, 0, 0, 0, 0, 0, 0};
        CHECK(Snd_volComposeCurve(kTab, 0, 127) == 64);     // (0x40*127)/0x7f
        CHECK(Snd_volComposeCurve(kTab, 1, 127) == -40);    // signed curve row
    }

    {
        // Pitch compose (FUN_00544b50): gate word, bend term >>6, pan-curve term.
        SndPitchState p{0x1234, 0x40, 0, 0x40000, nullptr, nullptr, 0};
        CHECK(Snd_pitchCompose(p) == 0x1234);               // gate word zero: passthrough
        p.bendGate = 1;
        CHECK(Snd_pitchCompose(p) == 0x1234);               // center bend 0x40: no term
        p.bendByte = 0x7f;
        CHECK(Snd_pitchCompose(p) == 0x1234 + ((0x3f * 4) >> 6));   // (pb-0x40)*hi>>6
        p.bendByte = 0x00;
        CHECK(Snd_pitchCompose(p) == 0x1234 - 4);           // (-0x40*4)>>6 = -4
        static const signed char kBend[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        p.bendCurve = kBend;                                // curve maps pb 0x00 -> kTab value
        CHECK(Snd_pitchCompose(p) == 0x1234 - 4);           // curve[0]=0 -> same term
        p.bendCurve = nullptr;
        SndPitchState q{0x1234, 0x40, 0, 0x40000, nullptr, nullptr, 0};
        static const signed char kPan[8] = {0x40, 0x00, 0, 0, 0, 0, 0, 0};
        q.panCurve = kPan; q.curveIdx = 1;                  // panCurve[1]=0 -> -4 term
        CHECK(Snd_pitchCompose(q) == 0x1234 - 4);           // independent of bend gate
    }

    {
        // ADSR arm (FUN_0052e394 rows): time/value step math + malformed row.
        const auto a = Snd_adsrArm({4, 0x40}, 0);
        CHECK(a.timeTicks == 4 && a.stepQ16 == 0x100000 && !a.malformed); // (0x40<<16)/4
        const auto neg = Snd_adsrArm({-8, 0}, 0x400000);
        CHECK(neg.timeTicks == 0x7fffffff && neg.stepQ16 == 0);   // saturation + trunc
        const auto bad = Snd_adsrArm({0, 0x20}, 0);
        CHECK(bad.malformed && bad.stepQ16 == 0);                 // line 0x178 row

        // Release re-arm (FUN_0052f020 rows): trig<0 stop, rising seg step math.
        CHECK(Snd_adsrRelease(-1, 0, 1, {8, 0x3f}, 0x40 << 16).stopRequested);
        CHECK(!Snd_adsrRelease(0, 3, 3, {8, 0x3f}, 0x40 << 16).advanced);
        const auto r = Snd_adsrRelease(0, 2, 3, {8, 0x3f}, 0x40 << 16);
        CHECK(r.advanced && r.timeTicks == 8 && r.stepQ16 == -0x2000); // (0x3F0000-0x400000)/8
        const auto rb = Snd_adsrRelease(0, 0, 1, {0, 0x3f}, 0);
        CHECK(rb.advanced && rb.malformed);                          // line 0x8c/0xae row
    }
    std::printf("test_voice_layer: ALL CHECKS PASS\n");
    return 0;
}
