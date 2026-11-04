// Derived from fifa99-native (same author), ported for World Cup 98
// SpeechQueue headless tests: ring overflow (-13 per SNDphraseadd at queued > 0x12), overlap
// clamp, status transitions through the pump (SNDphrasestatus codes 8/7/3/-1), the break-on-
// incomplete live scan, and sentence flag semantics (start -> 3, stop -> 2 + queue flush).
// Engine ABI: docs/reverse-engineering/COMMENTARY_WC98.md; mapping: docs/asset-formats/SPEECH_PHRASES.md.
#include "check.hpp"
#include "wc98/audio/speech_queue.hpp"

#include <vector>

using namespace wc98::audio;

static void ringOverflow() {
    SpeechQueue q;
    CHECK(q.queued() == 0 && q.status(0) == SpeechQueue::kStatusFree);
    for (int i = 0; i < SpeechQueue::kMaxQueued; ++i)
        CHECK(q.addPhrase(std::uint16_t(i)) == i); // slots handed out in order while free
    CHECK(q.queued() == SpeechQueue::kMaxQueued);
    CHECK(q.addPhrase(99) == SpeechQueue::kOverflow);   // 0x12 queued -> -13 (SNDphraseadd)
    CHECK(q.addPhrase(100, 5) == SpeechQueue::kOverflow);
    CHECK(q.queued() == SpeechQueue::kMaxQueued);       // reject leaves the ring untouched
}

static void overlapClamp() {
    SpeechQueue q;
    const int a = q.addPhrase(7, -42);   // negative -> immediate (clamped to 0)
    CHECK(a >= 0 && q.slot(a).overlap == 0 && q.slot(a).id == 7);
    const int b = q.addPhrase(8, 3);     // positive kept: defers start by that many pump frames
    CHECK(b >= 0 && q.slot(b).overlap == 3);
}

static void statusTransitions() {
    SpeechQueue q;
    std::vector<std::pair<int, std::uint16_t>> started;
    bool done = false;
    q.onStart([&](int slot, std::uint16_t bankId) { started.emplace_back(slot, bankId); });
    q.onComplete([&](int) { return done; });

    const int s0 = q.addPhrase(0x1234); // a PHRS record u16@0x12 bank id
    CHECK(s0 == 0 && q.status(0) == SpeechQueue::kStatusBusy); // pending -> -1
    q.pump();
    CHECK(started.size() == 1 && started[0].first == 0 && started[0].second == 0x1234);
    CHECK(q.status(0) == SpeechQueue::kStatusPlaying);         // play-status 0 -> 3

    q.pump();                                                  // complete_ false: stays playing
    CHECK(q.status(0) == SpeechQueue::kStatusPlaying);

    done = true;
    q.pump();                                                  // completion reported once -> 7
    CHECK(q.status(0) == SpeechQueue::kStatusComplete);
    q.pump();                                                  // released next frame -> free
    CHECK(q.status(0) == SpeechQueue::kStatusFree && q.queued() == 0);

    const int s1 = q.addPhrase(9, 2);                          // overlap defers the start thunk
    q.pump();
    CHECK(s1 >= 0 && started.size() == 1);                     // frames 1..2: counting down
    q.pump();
    CHECK(started.size() == 1);
    q.pump();                                                  // frame 3: overlap elapsed -> start
    CHECK(started.size() == 2 && started[1].second == 9);

    CHECK(q.status(SpeechQueue::kSlots) == SpeechQueue::kStatusBusy); // out of range -> -1
}

static void liveScanBreak() {
    // Engine pump breaks on the first non-complete live item: a stuck slot blocks successors.
    SpeechQueue q;
    int starts = 0;
    q.onStart([&](int, std::uint16_t) { ++starts; });
    q.onComplete([](int) { return false; }); // nothing ever completes
    CHECK(q.addPhrase(1) == 0);
    CHECK(q.addPhrase(2) == 1);
    for (int i = 0; i < 5; ++i) q.pump();
    CHECK(starts == 1);                                        // only slot 0 ever starts
    CHECK(q.status(0) == SpeechQueue::kStatusPlaying && q.status(1) == SpeechQueue::kStatusBusy);

    q.onComplete([](int slot) { return slot == 0; });          // slot 0 completes -> slot 1 proceeds
    q.pump();                                                  // report complete for slot 0
    q.pump();                                                  // release slot 0, start slot 1
    CHECK(starts == 2 && q.status(1) == SpeechQueue::kStatusPlaying);
}

static void sentenceFlags() {
    SpeechQueue q;
    CHECK(q.sentenceStatus() == SpeechQueue::kSentenceIdle);   // SNDsentencestatus: 2 idle
    q.addPhrase(3);
    q.sentenceStart();                                         // DAT_0064b15a = 1
    CHECK(q.sentenceStatus() == SpeechQueue::kSentenceSpeaking);
    CHECK(q.queued() == 1);                                    // start does not flush
    q.sentenceStop();                                          // clears flag + flushes the queue
    CHECK(q.sentenceStatus() == SpeechQueue::kSentenceIdle);
    CHECK(q.queued() == 0 && q.status(0) == SpeechQueue::kStatusFree);

    // Stop mid-utterance: playing slot is released, no further start thunks fire.
    int starts = 0;
    q.onStart([&](int, std::uint16_t) { ++starts; });
    q.addPhrase(5);
    q.pump();
    CHECK(starts == 1);
    q.sentenceStop();
    q.pump();
    CHECK(q.queued() == 0 && starts == 1);
}

int main() {
    ringOverflow();
    overlapClamp();
    statusTransitions();
    liveScanBreak();
    sentenceFlags();
    std::printf("SpeechQueue: %d slots, queued<=%d, status codes 8/7/3/-1, sentence 3/2 — ABI gates green\n",
                SpeechQueue::kSlots, SpeechQueue::kMaxQueued);
    return 0;
}
