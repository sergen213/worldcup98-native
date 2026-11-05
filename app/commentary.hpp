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

class Commentary {
public:
    explicit Commentary(audio::Mixer* mixer = nullptr);

    // Parses PHRS.PBK bytes strict; builds the id->record index and the pick pool (non-empty
    // commentary-class phrases, packed == 16*phrase+4). False on FormatError / empty pool.
    bool loadBank(std::shared_ptr<const std::vector<std::uint8_t>> phrsBytes);
    // Reads AUDIO/PHRS/PHRS.PBK under `treeRoot` (case-insensitive fallback) and calls loadBank.
    bool loadTree(const std::filesystem::path& treeRoot);

    // FUN_004893ec trigger seam: unmapped events are silent; mapped ones flush the queue, stop
    // any in-flight sentence, pick a phrase (rotation — Hypothesis, see commentary.cpp) and
    // enqueue its segment-id run in TOC order before sentenceStart.
    void trigger(const sim::SimEvent& e);

    // SpeechQueue pump = engine FUN_0048f920 per-frame call (1 frame = 1/50 s, camera cadence).
    void pump(int frames = 1) { q_.pump(frames); }

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
    std::size_t rotation_ = 0;                               // round-robin cursor (Hypothesis policy)
    std::array<audio::SoundId, audio::SpeechQueue::kSlots> sounds_{}; // slot -> mixer voice (0 = silent/done)
    std::unordered_map<std::uint32_t, std::vector<std::uint8_t>> phraseCache_; // lazy decompressed phrase members
    int lastPhrase_ = -1;
    std::uint32_t decoded_ = 0, sentences_ = 0, lastFrames_ = 0;
};

} // namespace wc98::app
