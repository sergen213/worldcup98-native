// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// M6 commentary adapter: sim match events -> phrase selection -> SpeechQueue -> .PBK segment
// fetch/decode -> Mixer. Trigger-seam model = FUN_004893ec / FUN_00489460 (gate <=1 sentence,
// queue flush, pick + enqueue) and the per-frame pump FUN_0048f920; see
// docs/reverse-engineering/COMMENTARY_WC98.md. Segment ids resolve through the PhraseBank record
// table u16@0x12 bijection (docs/reverse-engineering/SPEECH_INDEXING_NOTES.md §1).
// `mixer` may be null (headless): segments are fetched + decoded, then reported complete — no
// audio device needed.
#include "wc98/audio/mixer.hpp"
#include "wc98/audio/speech_queue.hpp"
#include "wc98/formats/phrs.hpp"
#include "wc98/sim/events.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace wc98::app {

// Speech master-gain ramp — mirrors the EACSND speech volume chain (cmn_sspeech.c state cb
// 0x4dfca0 + SNDspeechautovol 0x4df8a4): live volume in 16.16, per-tick delta toward a target
// clamped at the target with delta zeroed; applied vol = cur>>16 (SAR) into the SNDvol domain
// [0,0x7f] (win_sdriver.c FUN_0052c660 stores vol<<16 per voice). Game-side percent -> vol:
// FUN_004906a4 = pct*0x7f/100 clamp>=1. Evidence: analysis/pass7/gain*.txt; gain section of
// docs/reverse-engineering/COMMENTARY_WC98.md.
struct SpeechGain {
    static constexpr int kMaxVol = 0x7F; // SNDvol/SNDspeechinit vol domain [0,0x80)

    void set(int vol) { // SNDspeechinit semantics: immediate, no ramp
        if (vol < 0) vol = 0;
        if (vol > kMaxVol) vol = kMaxVol;
        cur_ = target_ = vol << 16;
        delta_ = 0;
    }

    // SNDspeechautovol(target, steps): delta = (target<<16 - cur)/steps, steps clamped >=1.
    void ramp(int targetVol, int steps) {
        if (targetVol < 0) targetVol = 0;
        if (targetVol > kMaxVol) targetVol = kMaxVol;
        if (steps < 1) steps = 1;
        target_ = targetVol << 16;
        delta_ = (target_ - cur_) / steps;
    }

    void tick() { // state cb semantics: advance, clamp at target in both directions
        if (delta_ == 0) return;
        cur_ += delta_;
        if ((delta_ > 0 && cur_ >= target_) || (delta_ < 0 && cur_ <= target_)) {
            cur_ = target_;
            delta_ = 0;
        }
    }

    int applied() const { return cur_ >> 16; }                // SNDvol byte domain, arithmetic SAR
    float frac() const { return float(applied()) / kMaxVol; } // normalized OpenAL gain factor

private:
    std::int32_t cur_ = kMaxVol << 16, target_ = kMaxVol << 16, delta_ = 0;
};

class Commentary {
public:
    explicit Commentary(audio::Mixer* mixer = nullptr);

    // Parses PHRS.PBK bytes strict; builds the id->record index and the pick pool (non-empty
    // commentary-class phrases, packed == 16*phrase+4). False on FormatError / empty pool.
    bool loadBank(std::shared_ptr<const std::vector<std::uint8_t>> phrsBytes);
    // Reads AUDIO/PHRS/PHRS.PBK under `treeRoot` (case-insensitive fallback) and calls loadBank.
    bool loadTree(const std::filesystem::path& treeRoot);

    // FUN_004893ec trigger seam: unmapped events are silent; mapped ones flush the queue, stop
    // any in-flight sentence, pick a phrase (qfind first-match over the ready pool, consumed on
    // use — see commentary.cpp / COMMENTARY_WC98.md §pick) and enqueue its segment-id run in TOC
    // order before sentenceStart. Exhausted pool -> null pick -> event stays silent.
    void trigger(const sim::SimEvent& e);
    // Speech master gain (SNDspeechinit vol + SNDspeechautovol scene ramp seam): the original
    // ramps the speech stream volume to the scene level on sentence start and back toward 0 at
    // scene stop (sites 0x48fcb7 / 0x491125); every tick applies cur>>16 to the live voices.
    void setVolume(int vol) { volume_ = vol; gain_.set(vol); }
    void autoVol(int target, int steps) { gain_.ramp(target, steps); }
    int volume() const { return volume_; }
    float masterGain() const { return gain_.frac(); }
    // Ramp tick count toward the scene level: the autovol step argument at 0x48fcb7 is runtime
    // data (undecomposed band); ponytail: constant stand-in, replace when measured.
    static constexpr int kRampTicks = 5;

    // SpeechQueue pump = engine FUN_0048f920 per-frame call (1 frame = 1/50 s, camera cadence) +
    // speech state-cb tick + per-voice gain apply. Defined in commentary.cpp.
    void pump(int frames = 1);


    const audio::SpeechQueue& queue() const { return q_; }
    [[nodiscard]] const formats::PhraseBank& bank() const { return bank_; }
    std::uint32_t decodedSegments() const { return decoded_; }   // decode callbacks fired
    std::uint32_t lastDecodeFrames() const { return lastFrames_; } // frames of the newest decode
    std::uint32_t sentencesStarted() const { return sentences_; }
    int lastPhrase() const { return lastPhrase_; }               // -1 = none picked yet

private:
    void onStart(int slot, std::uint16_t id);
    bool onComplete(int slot);

    audio::Mixer* mixer_;
    audio::SpeechQueue q_;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes_; // bank image (outlives spans)
    formats::PhraseBank bank_;
    std::unordered_map<std::uint16_t, std::uint32_t> byId_;  // record u16@0x12 -> record index
    std::vector<std::vector<std::uint16_t>> segIds_;         // phrase ordinal -> segment ids (TOC order)
    std::vector<std::uint32_t> candidates_;                  // pick pool: non-empty commentary phrases
    std::size_t head_ = 0;                               // ready-list head: picks consume entries (FUN_004c4a70 qremove)
    std::array<audio::SoundId, audio::SpeechQueue::kSlots> sounds_{}; // slot -> mixer voice (0 = silent/done)
    std::array<float, audio::SpeechQueue::kSlots> slotGain_{}; // per-slot class gain (Hypothesis)
    SpeechGain gain_;                                          // speech master ramp
    int volume_ = SpeechGain::kMaxVol;                         // SNDspeechinit vol (percent table FUN_004906a4, values runtime)
    std::unordered_map<std::uint32_t, std::vector<std::uint8_t>> phraseCache_; // lazy decompressed phrase members
    int lastPhrase_ = -1;
    std::uint32_t decoded_ = 0, sentences_ = 0, lastFrames_ = 0;
    bool prevSpeaking_ = false; // autovol scene-ramp seam edge detector (0x48fcb7/0x491125)
};

} // namespace wc98::app
