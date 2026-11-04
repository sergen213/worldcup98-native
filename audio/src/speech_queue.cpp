// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/audio/speech_queue.hpp"

namespace wc98::audio {

int SpeechQueue::addPhrase(std::uint16_t id, std::int16_t overlap, std::int32_t param2) {
    // SNDphraseadd (0x4df968): reject while the queued count would exceed 0x12 -> -13.
    if (queued() >= kMaxQueued) return kOverflow;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Slot& s = slots_[i];
        if (s.state != kFreeState) continue;
        s.id = id;
        // "OVERLAP OF %i OUT OF RANGE." guards the engine slot field; exact original bound not
        // measured, so the port clamps negatives to 0 (immediate start) — Hypothesis.
        s.overlap = overlap < 0 ? std::int16_t{0} : overlap;
        s.param2 = param2;
        s.state = kPending;
        return int(i);
    }
    return kOverflow; // ring full
}

int SpeechQueue::status(int slot) const {
    if (slot < 0 || slot >= kSlots) return kStatusBusy;
    switch (slots_[std::size_t(slot)].state) {
    case kFreeState:  return kStatusFree;     // -100 -> 8
    case kComplete:   return kStatusComplete; // 1 -> 7
    case kPlaying:    return kStatusPlaying;  // 0 -> 3
    default:          return kStatusBusy;     // pending (and anything else) -> -1
    }
}

int SpeechQueue::queued() const {
    int n = 0;
    for (const auto& s : slots_) n += s.state != kFreeState;
    return n;
}

void SpeechQueue::sentenceStart() { speaking_ = true; } // DAT_0064b15a = 1 ("speaking")

void SpeechQueue::sentenceStop() {
    speaking_ = false;
    for (auto& s : slots_) s = Slot{}; // flush: engine clears the queue + kills streams (FUN_004e2c60)
}

void SpeechQueue::pump(int frames) {
    while (frames-- > 0) {
        for (auto& s : slots_)
            if (s.state == kComplete) s.state = kFreeState; // release slots reported complete last frame
        bool blocked = false;
        for (std::size_t i = 0; i < slots_.size(); ++i) {   // live scan: first non-complete item breaks
            Slot& s = slots_[i];
            if (s.state != kPlaying) continue;
            if (complete_ && complete_(int(i))) s.state = kComplete;
            else { blocked = true; break; }                 // engine pump stops processing this frame
        }
        if (!blocked) {                                     // pick next: overlap countdown, then start thunk
            for (std::size_t i = 0; i < slots_.size(); ++i) {
                Slot& s = slots_[i];
                if (s.state != kPending) continue;
                if (s.overlap > 0) { --s.overlap; continue; }
                s.state = kPlaying;
                if (start_) start_(int(i), s.id); // pump latches record u16@0x12 then **(item+0x1c)
                break; // engine picks one new item per frame
            }
        }
    }
}
} // namespace wc98::audio
