// SMIX native mixer grammar (pass-18smix): Q16 resampler FUN_0054d4fb/0054d4b5 boundary rows,
// pan-law installer FUN_0054ccfe combo table + i/127 normalization (DAT_00596560), the 2x2
// pan-matrix mix handlers {cd8e, ce8d, cf09, ce1e, cf86, d00b, d091} incl. exe frame-count
// unroll + mono tail grammar, and the DAT_00661068 voice block (FUN_00548a58 pan writer ramp
// flags, FUN_00548a08 ramp helper byte math, FUN_005489c0 frequency clamp).
// Dossier: analysis/pass17audio/{NOTES.md audio11-audio15,DECOMPILE2/,DECOMPILE3/}.
#include "check.hpp"

#include "wc98/audio/smix_mixer.hpp"
#include "wc98/audio/smix_resampler.hpp"
#include "wc98/audio/smix_voice.hpp"

#include <cmath>
#include <cstdint>

using namespace wc98::audio;

namespace {
// 1 kHz sine at 48 kHz, pair-duplicated (mono voice -> L=R), deterministic.
void fillSinePairs(float* dst, int pairs) {
    for (int k = 0; k < pairs; ++k) {
        const float s = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * k / 48000.0));
        dst[k * 2] = s;
        dst[k * 2 + 1] = s;
    }
}
} // namespace

int main() {
    // ---- resampler (harness gate, FUN_0054d4fb / FUN_0054d4b5) ----------------------------
    // Case 1: passthrough step == 0x10000 copies source verbatim.
    {
        SmixResamplerState s{0, 0x10000, 0, 0};
        const std::uint32_t src[4] = {0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u};
        std::uint32_t dst[4] = {};
        Smix_resample(s, src, dst, 4);
        for (int i = 0; i < 4; ++i) CHECK(dst[i] == src[i]);
    }
    // Case 2: step > 1.0 (0x20000): integer skip; pos accumulates acc>>16, mode stays 0.
    {
        SmixResamplerState s{0, 0x20000, 0, 0};
        const std::uint32_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        std::uint32_t dst[4] = {};
        Smix_resample(s, src, dst, 4);
        CHECK(dst[0] == 1 && dst[1] == 3 && dst[2] == 5 && dst[3] == 7);  // every 2nd source dword.
        CHECK(s.pos == (s.step * 4 >> 16));  // exe: pos = acc>>16.
        CHECK(s.mode == 0);                  // hi(2) < step(0x20000) -> mode cleared, per exe tail.
    }
    // Case 3: fractional fill (mode==1 entry): head run emits lastSample hold word once.
    {
        SmixResamplerState s{0x8000 /* .5 fraction */, 0x8000 /* 0.5 rate */, 0xAAAABBBBu, 1};
        const std::uint32_t src[6] = {0xA1A1A1A1u, 0xB2B2B2B2u, 0xC3C3C3C3u,
                                      0xD4D4D4D4u, 0xE5E5E5E5u, 0xF6F6F6F6u};
        std::uint32_t dst[8] = {};
        Smix_resample(s, src, dst, 8);
        CHECK(dst[0] == 0xAAAABBBBu);  // fractional fill uses hold register.
        CHECK((s.mode == 0) || (s.mode == 1));  // mode is only 0/1 by construction.
        if (s.mode == 1) CHECK(s.lastSample == dst[7]);  // hold updated to last output.
    }
    // Case 4: FUN_0054d4b5 specials — exact expressions from the decompile.
    {
        SmixResamplerState a{0x1234, 0xFFFF, 0, 0};  // step < 0x10000 branch.
        const std::int32_t wantA = static_cast<std::int32_t>(((0xFFFFu * (4 - 1) + 0x1234u) >> 16) + 1 - 0);
        CHECK(Smix_stepDelta(a, 4) == wantA);
        SmixResamplerState b{0, 0x10000, 0, 5};  // boundary <0x10001: returns count verbatim.
        CHECK(Smix_stepDelta(b, 4) == 4);
        SmixResamplerState c{0, 0x20000, 0, 2};  // >0x10001 branch: (step*count>>16) - mode.
        const std::int32_t wantC = static_cast<std::int32_t>(((static_cast<std::uint64_t>(0x20000) * 6 + 0) >> 16) - 2);
        CHECK(Smix_stepDelta(c, 6) == wantC);
    }

    // ---- pan-law installer (FUN_0054ccfe combo table, harness2 gate) ----------------------
    CHECK(Smix_selectHandler(0, 0) == SmixPanHandler::Silent);
    CHECK(Smix_selectHandler(0, 5) == SmixPanHandler::LeftSilent);
    CHECK(Smix_selectHandler(5, 0) == SmixPanHandler::RightSilent);
    CHECK(Smix_selectHandler(0x7F, 0x7F) == SmixPanHandler::FullCentre);
    CHECK(Smix_selectHandler(0x7F, 9) == SmixPanHandler::RightFull);
    CHECK(Smix_selectHandler(9, 0x7F) == SmixPanHandler::LeftFull);
    CHECK(Smix_selectHandler(9, 5) == SmixPanHandler::General);
    {   // Installer: i/127 normalization (DAT_00596560); 0x7F scales to exactly 1.0f.
        auto g = Smix_install(0x7F, 64);
        CHECK(g.volL == 1.0f);
        CHECK(g.volR == static_cast<float>(64) * (1.0f / 127.0f));
        CHECK(Smix_install(0, 0).volL == 0.0f);
    }
    {   // Pan matrix accumulation: dst += {L*vl, L*vr, R*vl, R*vr} (FUN_0054cd8e row form).
        const float src[4] = {2.0f, 3.0f, 4.0f, 5.0f};
        float dst[8];
        for (auto& d : dst) d = 1.0f;
        Smix_mixGeneral(src, dst, 2, 0.5f, 0.25f);
        CHECK(dst[0] == 1.0f + 2.0f * 0.5f && dst[1] == 1.0f + 2.0f * 0.25f);
        CHECK(dst[2] == 1.0f + 3.0f * 0.5f && dst[3] == 1.0f + 3.0f * 0.25f);
        CHECK(dst[4] == 1.0f + 4.0f * 0.5f && dst[5] == 1.0f + 4.0f * 0.25f);
        CHECK(dst[6] == 1.0f + 5.0f * 0.5f && dst[7] == 1.0f + 5.0f * 0.25f);
    }

    // ---- deterministic sine-pair mix {volL=0x40, volR=0x7F} (exact floats) -----------------
    {
        const auto g = Smix_install(0x40, 0x7F);  // (x,7F) -> cf86 LeftFull.
        CHECK(g.handler == SmixPanHandler::LeftFull);
        const float vl = 64.0f * (1.0f / 127.0f);
        constexpr int kPairs = 16;
        float src[kPairs * 2];
        fillSinePairs(src, kPairs);
        float dst[kPairs * 4] = {};
        Smix_mix(g, src, kPairs * 2, dst);  // frame form: 32 samples.
        for (int k = 0; k < kPairs; ++k) {
            const float l = src[k * 2], r = src[k * 2 + 1];
            CHECK(dst[k * 4 + 0] == l * vl);   // L scaled on the left slots...
            CHECK(dst[k * 4 + 1] == l);        // right slots passthrough (vr = 1.0f).
            CHECK(dst[k * 4 + 2] == r * vl);
            CHECK(dst[k * 4 + 3] == r);
        }
        // Special handler == general matrix with vr staged to unity (dossier audio14).
        float ref[kPairs * 4] = {};
        Smix_mixFramesGeneral(src, kPairs * 2, ref, vl, 1.0f);
        for (int i = 0; i < kPairs * 4; ++i) CHECK(dst[i] == ref[i]);
    }

    // ---- exe frame-count grammar: unrolled body + mono tail -------------------------------
    {   // n=5 stereo stream -> 2 full pairs + 1 mono tail sample (FUN_0054cd8e bounds).
        const float src[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        float dst[16] = {};
        Smix_mixFramesGeneral(src, 5, dst, 0.5f, 0.25f);
        CHECK(dst[0] == 0.5f && dst[1] == 0.25f);           // pair (1,2) -> quad 0
        CHECK(dst[2] == 2.0f * 0.5f && dst[3] == 2.0f * 0.25f);
        CHECK(dst[4] == 3.0f * 0.5f && dst[5] == 3.0f * 0.25f);  // pair (3,4) -> quad 1
        CHECK(dst[8] == 5.0f * 0.5f && dst[9] == 5.0f * 0.25f);  // tail src[4] -> mono row
    }
    {   // n=1: tail-only write; n=0: nothing touched.
        const float src[2] = {7.0f, 9.0f};
        float dst[4];
        for (auto& d : dst) d = -1.0f;
        Smix_mixFramesGeneral(src, 1, dst, 2.0f, 3.0f);
        CHECK(dst[0] == -1.0f + 7.0f * 2.0f && dst[1] == -1.0f + 7.0f * 3.0f);
        CHECK(dst[2] == -1.0f && dst[3] == -1.0f);
        Smix_mixFramesGeneral(src, 0, dst, 2.0f, 3.0f);
        CHECK(dst[0] == -1.0f + 7.0f * 2.0f && dst[1] == -1.0f + 7.0f * 3.0f);  // n=0: untouched
    }
    {   // (x,0) FUN_0054ce8d: mono stream into left slots only (n=6 = 4-sample body + tail).
        const float src[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
        float dst[16] = {};
        Smix_mixFramesRightSilent(src, 6, dst, 0.25f);
        CHECK(dst[0] == 0.25f && dst[2] == 0.5f && dst[4] == 0.75f && dst[6] == 1.0f);
        CHECK(dst[8] == 5.0f * 0.25f && dst[10] == 6.0f * 0.25f);  // tail samples
        for (int i : {1, 3, 5, 7, 9, 11}) CHECK(dst[i] == 0.0f);   // right slots untouched
    }
    {   // (0,x) FUN_0054cf09: mono stream into right slots only.
        const float src[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
        float dst[16] = {};
        Smix_mixFramesLeftSilent(src, 6, dst, 0.5f);
        CHECK(dst[1] == 0.5f && dst[3] == 1.0f && dst[5] == 1.5f && dst[7] == 2.0f);
        CHECK(dst[9] == 5.0f * 0.5f && dst[11] == 6.0f * 0.5f);
    }
    {   // (7F,7F) FUN_0054ce1e full-centre: quad = {L, L, R, R} passthrough accumulation.
        const float src[4] = {2.0f, 3.0f, 4.0f, 5.0f};
        float dst[8] = {};
        Smix_mixFramesFullCentre(src, 4, dst);
        CHECK(dst[0] == 2.0f && dst[1] == 2.0f && dst[2] == 3.0f && dst[3] == 3.0f);
        CHECK(dst[4] == 4.0f && dst[5] == 4.0f && dst[6] == 5.0f && dst[7] == 5.0f);
    }
    {   // (0,0) FUN_0054d091 silent: dst untouched through the dispatcher.
        const float src[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        float dst[8];
        for (auto& d : dst) d = -3.0f;
        Smix_mix(Smix_install(0, 0), src, 4, dst);
        for (auto& d : dst) CHECK(d == -3.0f);
    }

    // ---- voice block: FUN_00548a58 pan writer + FUN_005489c0 clamp ------------------------
    {
        SmixVoice off;  // inactive slot gate (exe returns 0xffffffff)
        CHECK(Smix_setPan(off, 0x10, 0x20) == -1);
        CHECK(Smix_setFrequency(off, 44100) == -1);

        SmixVoice v;
        v.active = true;
        CHECK(Smix_setPan(v, 0x40, 0x7F) == 0);
        CHECK(v.panL == 0x40 && v.panR == 0x7F);           // s8 pair @+0x6C/+0x6D
        CHECK(v.rampDirL == 1 && v.rampDirR == 1);         // prev(0) < new -> rising
        Smix_commitRamp(v);                                // prev pair updates on ramp completion
        Smix_setPan(v, 0x20, 0x7F);                        // L falls (prev 0x40 !< 0x20), R equal
        CHECK(v.panL == 0x20);
        CHECK(v.rampDirL == 0xFF && v.rampDirR == 0xFF);   // equality is NOT rising per exe test
        Smix_commitRamp(v);
        Smix_setPan(v, -5, -100);                          // s8 negatives stored; signed compare
        CHECK(v.panL == -5 && v.panR == static_cast<std::int8_t>(-100));
        CHECK(v.rampDirL == 0xFF);                         // prev(0x20)=32 !< -5 -> falling flag
        Smix_commitRamp(v);
        Smix_setPan(v, 0x7F, 0x7F);
        CHECK(v.rampDirL == 1);                            // prev(-5) < 0x7F signed -> rising

        CHECK(Smix_setFrequency(v, 0x90000) == 0x80000);   // clamp to 0x80000 (FUN_005489c0)
        CHECK(v.lastFrequency == 0x80000);
        CHECK(Smix_setFrequency(v, 0x4000) == 0x4000);     // below clamp passes through
    }

    // ---- ramp helper FUN_00548a08: byte band math on the dword registers ------------------
    {
        SmixVoice v;
        v.rampStop = 0;
        v.raw[2] = 0xDEADBEEFu;
        Smix_rampStep(v);                                   // stop flag clear: low byte -> 0
        CHECK((v.raw[2] & 0xFFu) == 0u);
        CHECK((v.raw[2] >> 8) == (0xDEADBEu));

        v.rampStop = 1;                                     // midpoint store, then scaled overwrite
        v.raw[0] = 0x50000000u;                             // hi byte a = +80
        v.raw[1] = 0x30000000u;                             // hi byte b = +48
        v.raw[5] = 0x02000000u;                             // hi byte c = 2
        Smix_rampStep(v);
        CHECK((v.raw[2] & 0xFFu) == static_cast<std::uint32_t>(static_cast<unsigned char>((80 * 2) >> 7)));
        v.raw[2] = 0;                                       // scaled form wins over midpoint (64)
        Smix_rampStep(v);
        CHECK((v.raw[2] & 0xFFu) == 1u);

        v.raw[0] = 0xB0000000u;                             // a = -80 signed >>24
        const std::uint32_t want = static_cast<std::uint8_t>(static_cast<char>((-160) >> 7));
        Smix_rampStep(v);
        CHECK((v.raw[2] & 0xFFu) == want);                  // arithmetic shift, signed char store
    }

    std::printf("test_smix_grammar: ALL CHECKS PASS\n");
    return 0;
}
