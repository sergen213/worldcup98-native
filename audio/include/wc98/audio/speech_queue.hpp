// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Speech phrase-queue runtime model. Mirrors the EACSND SNDphrase* engine ABI measured in
// docs/reverse-engineering/COMMENTARY_WC98.md: ring of 20 slots x 12 B (DAT_0064b070),
// sentence flags (DAT_0064b15a) and the per-frame pump (FUN_0048f920). Self-contained and
// headless-testable: no audio device — playback start and completion are injected callbacks;
// presentation/app wires them to the mixer + .PBK segment fetch at M6 integration.
// Function-by-function VA mapping: docs/asset-formats/SPEECH_PHRASES.md.
#include <array>
#include <cstdint>
#include <functional>

namespace wc98::audio {

class SpeechQueue {
public:
    // Engine ABI constants (SNDphraseadd / SNDphrasestatus / SNDsentencestatus).
    static constexpr int kSlots = 20;            // ring size, 12-byte slots
    static constexpr int kMaxQueued = 0x12;      // queued-count ceiling before reject
    static constexpr int kOverflow = -13;        // SNDphraseadd "OVERLAP/queue out of range" code
    static constexpr int kStatusFree = 8;        // slot state -100 -> 8
    static constexpr int kStatusComplete = 7;    // play-status 1 -> 7
    static constexpr int kStatusPlaying = 3;     // play-status 0 -> 3
    static constexpr int kStatusBusy = -1;       // any other state (pending) -> -1
    static constexpr int kSentenceSpeaking = 3;  // SNDsentencestatus while DAT_0064b15a set
    static constexpr int kSentenceIdle = 2;

    struct Slot { // mirrors the engine slot: +0 state, +4 u16 id (CX), +6 i16 overlap, +8 param2
        int state = -100;      // raw engine codes: -100 free, 0 playing, 1 complete; -50 = port-side pending
        std::uint16_t id = 0;  // bank/segment handle (phrase record u16@0x12 — the pump's _DAT_006326bc)
        std::int16_t overlap = 0; // frames to defer start; clamped on add ("OVERLAP OF %i OUT OF RANGE.")
        std::int32_t param2 = 0;
    };

    using StartFn = std::function<void(int slot, std::uint16_t bankId)>; // per-item start thunk (**item+0x1c) + id latch
    using CompleteFn = std::function<bool(int slot)>;                    // play-status backend (FUN_004d9250 passthrough)

    void onStart(StartFn f) { start_ = std::move(f); }
    void onComplete(CompleteFn f) { complete_ = std::move(f); }

    // SNDphraseadd(id, param2): first free slot, queued <= 0x12, else kOverflow (-13).
    int addPhrase(std::uint16_t id, std::int16_t overlap = 0, std::int32_t param2 = 0);

    // SNDphrasestatus(slot): -100 -> 8 (free), 1 -> 7 (complete), 0 -> 3 (playing), else -1.
    [[nodiscard]] int status(int slot) const;

    // SNDphrasesqueued: non-free slots in the ring.
    [[nodiscard]] int queued() const;

    void sentenceStart();                       // sets the speaking flag (DAT_0064b15a = 1)
    void sentenceStop();                        // clears it and flushes all slots (queue-flush analog)
    [[nodiscard]] int sentenceStatus() const { return speaking_ ? kSentenceSpeaking : kSentenceIdle; }

    // FUN_0048f920 per-frame pump: report completions of the first live slot (break on incomplete),
    // release slots reported complete last frame, then start the next pending slot whose overlap
    // has counted down. `frames` = ticks to advance (1 per frame).
    void pump(int frames = 1);

    [[nodiscard]] const Slot& slot(int i) const { return slots_.at(std::size_t(i)); }

private:
    static constexpr int kFreeState = -100, kPending = -50, kPlaying = 0, kComplete = 1;
    std::array<Slot, kSlots> slots_{}; // Slot NSMI state=-100 fills every slot free
    bool speaking_ = false;            // DAT_0064b15a analog
    StartFn start_;
    CompleteFn complete_;
};

} // namespace wc98::audio
