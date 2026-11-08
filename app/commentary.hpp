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

// EACSND MIDI module channel state table DAT_0064b9dc (stride 4 bytes, range 0x64b9dc..0x64bddc =
// 64 slots: 16 sequence tracks x 16 MIDI channels; init loop in SNDmidiinit FUN_004e41d4, reset
// loop in SNDseqstop FUN_004e48d0). Per channel: +0 CC10 expression (default 0x40), +1 CC7 volume
// (default 0x7f full), +2 pitch-bend merge byte (default 0x40 center), +3 program (default 0xff).
// The MIDI timer callback LAB_004e3bd4 (registered at DAT_006597cc by SNDmidiinit; loops the 16
// sequence tracks @DAT_0064bee8 per tick) consumes the event stream: key-on voice vol =
// velocity * cc7 / 0x7f, and a CC7 write stores tbl[chan].vol then re-applies SNDvol (FUN_0052c71c)
// to every active voice on that channel — the "channel duck/mix table" of the gain section.
// Evidence: analysis/pass7/duck2.txt + duck3.txt; docs/reverse-engineering/COMMENTARY_WC98.md §pass 7d.
struct ChannelMix {
    struct Chan { std::uint8_t expr = 0x40, vol = 0x7F, bend = 0x40, prog = 0xFF; }; // SNDmidiinit defaults
    static constexpr int kSlots = 64; // DAT_0064b9dc..0x64bddc stride-4 window

    void reset() { for (auto& c : tbl_) c = Chan{}; } // SNDmidiinit init + SNDseqstop reset loops
    std::uint8_t volume(int ch) const { return tbl_[std::size_t(ch & 63)].vol; }
    void setVolume(int ch, int byteVal) { tbl_[std::size_t(ch & 63)].vol = std::uint8_t(byteVal & 0x7f); } // CC7 handler: data byte masked
    [[nodiscard]] static constexpr int scaled(int chVol, int voiceVol) { return voiceVol * chVol / 0x7f; } // key-on formula (int div)
    [[nodiscard]] int scaledFor(int ch, int voiceVol) const { return scaled(volume(ch), voiceVol); }
    private:
        std::array<Chan, kSlots> tbl_{};
};

// ---- Pass 12 per-event speech preload + scheduler family -------------------------------------
// Dossier: analysis/pass12speech/SPEECH_PRELOAD_TABLE.md (+ speech_pick*.txt raw decompiles).
// Headless models of the gated runtime around SNDphraseadd (0x4df968): the preload enable/disable
// gate, the per-event-object phrase dispatch, and the RNG-gated match-clock scheduler. There is
// no static event->phrase table in the original; these structs are that architecture.

// Preload gate family (pass 12 §3). Enable FUN_0048d378: requires the speech-ready getter
// FUN_00490674() != 0 AND a cleared gate DAT_0063241c, then sets the gate once, drains the qadd
// queue (FUN_004c46a0 loop), reserves an arena (reservememadr FUN_0049f5a0 -> DAT_00632424) and
// creates a bank via SNDbankcreate FUN_004e33c0 ('BNkl', max 0x50 banks in the DAT_00659de8 table);
// the returned handle is stored as DAT_00632420. Disable FUN_0048d3f4: gate=0, queue flush, arena
// release (the bank-table slot frees via hook FUN_004e37cc; DAT_00632420 itself is not cleared).
class SpeechPreload {
public:
    static constexpr int kMaxBanks = 0x50;   // SNDbankcreate bank table slots (DAT_00659de8)
    static constexpr std::int32_t kNoBank = -1, kBankTableFull = -9; // FUN_004e33c0 error codes

    void setSceneEnabled(bool on) { sceneEnabled_ = on; } // FUN_00490674 source (scene flag DAT_00632774)
    bool sceneEnabled() const { return sceneEnabled_; }

    // FUN_0048d378: once-only gate transition. True = the gate flipped 0->1 this call.
    bool enable() {
        if (!sceneEnabled_ || gate_) return false;
        gate_ = true;
        ++enables_;
        arena_ = true;                                   // reservememadr(DAT_00632424)
        bankHandle_ = banksCreated_ < kMaxBanks ? std::int32_t(banksCreated_) : kBankTableFull;
        if (bankHandle_ >= 0) ++banksCreated_;           // SNDbankcreate(DAT_00632420)
        return true;
    }

    // FUN_0048d3f4: no-op while the gate is clear.
    void disable() {
        if (!gate_) return;
        gate_ = false;                                   // queue flush (qdel loop) is port-side bookkeeping
        ++disables_;
        arena_ = false;                                  // release via hook FUN_004e37cc
    }

    bool enabled() const { return gate_; }               // DAT_0063241c
    bool arena() const { return arena_; }                // DAT_00632424 != 0
    std::int32_t bankHandle() const { return bankHandle_; } // DAT_00632420
    // FUN_0048d2f0 phrase-id accessor: gate ? DAT_00632420 : 0 — the "id" source passed in EAX to
    // every SNDphraseadd call site.
    std::int32_t phraseId() const { return gate_ ? bankHandle_ : 0; }
    int enables() const { return enables_; }
    int disables() const { return disables_; }
    int banksCreated() const { return banksCreated_; }

private:
    bool sceneEnabled_ = false, gate_ = false, arena_ = false;
    std::int32_t bankHandle_ = kNoBank;
    int enables_ = 0, disables_ = 0, banksCreated_ = 0;
};

// Per-event commentary record: the exe event object owns {event byte @+0x20 (dispatch selector),
// phrase-id candidate field @+0xE, SNDphraseadd queue-slot result write-back @+6}. Pre-dispatch
// hook FUN_0048f2d8 fills +0xE for cases {2,4,6} via the bank iterator FUN_0048d070 (high nibble
// event>>6 selects the bank) and for case 7 via the FUN_0048f290 pair.
struct SpeechEventRecord {
    static constexpr std::int32_t kNoPhrase = -1; // +0xE unset sentinel (port-side; exe value is object data)
    static constexpr int kNoSlot = -1;            // +6 writeback not fired yet
    std::int32_t phraseId = kNoPhrase;            // [record+0xE] u16 handle, CX into SNDphraseadd
    int queueSlot = kNoSlot;                      // [record+6] <- SNDphraseadd return (ring index)
};

// Event dispatcher cluster FUN_0048f4b0..0x48f790: event byte ([obj+0x20]&0x3f) selects a case via
// jump table 0x48f45c (cases 0..7; >7 jumps straight to the exit label). Every non-null record in
// the case's slot set {primary [ESI+0x28], hook candidates ESP+0x8/0xc/0x10} runs the per-case body:
//   MOV EDX,[rec+0xE]; XOR EBX,EBX (overlap=0); CALL FUN_0048d2f0 (EAX = preload phrase-id source);
//   CALL SNDphraseadd; MOV [rec+6],EAX. Exit label 0x48f4ea = SNDsentencestart + return 1 — the
//   sentence start fires for every dispatched event, including the >7 fallthrough.
class SpeechEventDispatch {
public:
    static constexpr std::uint8_t kMaxCase = 7; // jump table bounds: CMP AL,0x7 / JA exit
    SpeechEventDispatch(audio::SpeechQueue& q, SpeechPreload& preload) : q_(q), preload_(preload) {}

    void setCandidate(int i, SpeechEventRecord* r) { cands_[i < 3 ? i : i - 3] = r; } // ESP+0x8/0xc/0x10

    // Returns true = the handler exited through 0x48f4ea (SNDsentencestart fired, EAX=1).
    bool dispatch(std::uint8_t eventByte, SpeechEventRecord* primary) {
        const std::uint8_t ev = eventByte & 0x3f; // AND AL,0x3f at 0x48f4bc
        if (ev > kMaxCase) { q_.sentenceStart(); ++sentences_; return true; } // JA 0x48f4ea
        for (SpeechEventRecord* r : {primary, cands_[0], cands_[1], cands_[2]}) {
            if (!r) continue; // TEST reg,reg / JZ skip per slot
            const std::int32_t bank = preload_.phraseId();       // CALL 0x48d2f0 -> EAX
            r->queueSlot = q_.addPhrase(std::uint16_t(r->phraseId), 0, bank); // EBX=0 overlap; CX=id
            ++adds_;                                              // MOV [record+6],EAX write-back
        }
        q_.sentenceStart();                                       // SNDsentencestart at exit label
        ++sentences_;
        return true;
    }

    int addCalls() const { return adds_; }          // SNDphraseadd calls fired
    std::uint32_t sentencesStarted() const { return sentences_; }
    void clearCandidates() { for (auto& c : cands_) c = nullptr; }

private:
    audio::SpeechQueue& q_;
    SpeechPreload& preload_;
    SpeechEventRecord* cands_[3] = {};              // pre-dispatch hook outputs (FUN_0048f2d8)
    int adds_ = 0;
    std::uint32_t sentences_ = 0;
};

// Scheduler record: the queue item's +0xc subrecord fields (pass 12 §4):
//   +8 sceneId, +0xc delay (0x7f primary; RNG variant patches the first record to 0x4a),
//   +0x10 t0 (clock*100/60 ticks; second variant: rng%200 + t0 + 400 jitter),
//   +0x14 priority = DAT_00636a14 + 3, +0x18 start = sceneBase + t0 + durC, +0x1c end.
struct SpeechScheduleRecord {
    static constexpr std::int32_t kDelayPrimary = 0x7f, kDelayRngVariant = 0x4a;
    std::int32_t sceneId = 0, delay = kDelayPrimary, t0 = 0, priority = 0, start = 0, end = 0;
};

// Match-time scheduler core FUN_0048d7ec + per-frame sweep FUN_0048d9e0 (gate DAT_006324b4).
// Deterministic under a fixed seed: the RNG FUN_004c321c model is injected via setRandSeed.
class SpeechScheduler {
public:
    static constexpr int kQueueCap = 8; // FUN_004c4e70 alloc fails at queue-full (port-side bound)

    void start() { gate_ = true; }                       // FUN_0048dfc4 @0x48e071: DAT_006324b4 = 1
    bool running() const { return gate_; }
    void setSceneBase(std::int32_t ticks) { sceneBase_ = ticks; } // DAT_00636a14 frame clock
    std::int32_t sceneBase() const { return sceneBase_; }
    void setSpeechLevel(int level) { level_ = level; }    // FUN_004906d0 model (>0: speech enabled + scene level)
    void setQueueDepth(int depth) { depth_ = depth; }     // FUN_004c4af0 model (queue items)
    int gateQueueDepth() const { return depth_; } // FUN_004c4af0 model value fed by the caller (Commentary::pump)
    const std::vector<SpeechScheduleRecord>& pending() const { return pending_; } // queued (+0xc subrecords)
    void setRandSeed(std::uint32_t seed) { rand_ = seed; }
    std::uint32_t randState() const { return rand_; }
    // FUN_004c321c model step, exposed so gates can predict the %5/%200 draws deterministically.
    static std::uint32_t randAdvance(std::uint32_t& state) { return nextRand(state); }

    // FUN_0048d7ec: gates on running && level > 0 && queue depth < 2. Clock inputs are raw match
    // clocks converted with the original t*100/60 tick formula. Returns records emitted (0..2).
    int scheduleEvent(std::int32_t sceneId, std::int32_t clockA, std::int32_t durB, std::int32_t durC) {
        if (!gate_ || level_ <= 0 || depth_ >= 2) return 0;
        const std::int32_t t0 = clockA * 100 / 60;   // uVar3 = (ESI*100)/0x3c
        const std::int32_t dB = durB * 100 / 60;     // uVar4
        const std::int32_t dC = durC * 100 / 60;     // uVar5
        if (pending_.size() >= kQueueCap) return 0;  // FUN_004c4e70 alloc null
        SpeechScheduleRecord r{};
        r.sceneId = sceneId;
        r.delay = SpeechScheduleRecord::kDelayPrimary;
        r.t0 = t0;
        r.start = sceneBase_ + t0 + dC;              // DAT_00636a14 + uVar3 + uVar5
        r.end = r.start + dB;                        // +0x1c = +0x18 + uVar4
        r.priority = sceneBase_ + 3;                 // DAT_00636a14 + 3
        pending_.push_back(r);
        ++emits_;                                    // FUN_0048d678 emit/commit
        // RNG-gated echo variant: fires iff sceneId != 2 && FUN_004c321c() % 5 == 0, and the
        // second record alloc succeeds. Patches the primary delay to 0x4a.
        if (sceneId != 2 && nextRand() % 5 == 0 && pending_.size() < kQueueCap) {
            pending_.back().delay = SpeechScheduleRecord::kDelayRngVariant;
            const std::int32_t jitter = std::int32_t(nextRand() % 200) + t0 + 400; // rng%200 + uVar3 + 400
            SpeechScheduleRecord r2{};
            r2.sceneId = sceneId;
            r2.delay = SpeechScheduleRecord::kDelayPrimary;
            r2.t0 = jitter;
            r2.start = sceneBase_ + jitter + dC;     // DAT_00636a14 + [+0x10] + uVar5
            r2.end = r2.start + dB;                  // uVar4 + [+0x18] (extraout ambiguity: own start)
            r2.priority = sceneBase_ + 3;
            pending_.push_back(r2);
            ++emits_;
            return 2;
        }
        return 1;
    }

    // FUN_0048d9e0 per-frame sweep: for each queued record rewrite +0x1c = DAT_00636a14 +
    // (EAX*0x20 + EDI*-7)*4/0x3c (pace-modulated duration; explicit inputs) then emit and free.
    int sweepPace(std::int32_t paceA, std::int32_t paceB) {
        if (!gate_) return 0;
        const int n = int(pending_.size());
        for (auto& r : pending_) {
            r.end = sceneBase_ + ((paceA * 0x20 - paceB * 7) * 4) / 60; // *(extraout+0xc)+0x1c rewrite
            emitted_.push_back(r);                                       // FUN_0048d6fc emit
        }
        pending_.clear();                                                // FUN_004c4800 free
        return n;
    }

    const std::vector<SpeechScheduleRecord>& emitted() const { return emitted_; }
    std::uint32_t emits() const { return emits_; }
    void reset() { pending_.clear(); emitted_.clear(); emits_ = 0; depth_ = 0; }

private:
    // FUN_004c321c model. Hypothesis: the generator body is not decoded yet (see docs pass-12
    // section); port-side LCG keeps scheduling deterministic under a fixed seed.
    static std::uint32_t nextRand(std::uint32_t& state) {
        state = state * 0x41c5e7u + 0x3039u;   // Knuth MMIX LCG constants (port-side stand-in)
        return (state >> 16) & 0x7fff;
    }
    std::uint32_t nextRand() { return nextRand(rand_); }

    bool gate_ = false;                                   // DAT_006324b4
    std::int32_t sceneBase_ = 0, level_ = 0, depth_ = 0;  // DAT_00636a14 / FUN_004906d0 / FUN_004c4af0
    std::vector<SpeechScheduleRecord> pending_, emitted_;
    std::uint32_t emits_ = 0, rand_ = 0;
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

    // Percent-table API (DAT_00632778): FUN_004906a4 = pct * 0x7f / 100, clamp >= 1 (NO upper clamp;
    // setter FUN_0049067c writes DAT_00632778[idx] + fires the per-index notify cb at DAT_004904b0[idx*4]).
    // Scene seam: game scene handlers (FUN_0046b074/0x46b130/...) convert percent -> vol and feed the
    // autovol ramp (site 0x48fcb2). Scene DEFAULT percents MEASURED live pass 7e (winedbg launch-under-
    // debugger, analysis/pass7/percent1.txt): setter triples FUN_0046b074 writes idx4=65 (@0x46b095) +
    // idx3=80 (@0x46b0f8), FUN_0046b130 quintet loop writes idx{0,2,1,5}=80; live table snapshot
    // DAT_00632778 = {80,80,80,80,65,80,1} (idx6=1 static). Speech scene level = idx4 getter
    // FUN_0048ed9c@0x48ee63 -> 65 %.
    static constexpr int kScenePct[7] = {80, 80, 80, 80, 65, 80, 1};
    static constexpr int kSceneSpeechPct = 65; // table idx4 (speech volume getter consumer)
    static constexpr int percentToVol(int pct) { const int v = pct * SpeechGain::kMaxVol / 100; return v < 1 ? 1 : v; }
    void setScenePercent(int pct) { volume_ = percentToVol(pct); gain_.ramp(volume_, kRampTicks); }

    // Channel duck/mix table (ChannelMix above). Deviation note: the original table scales MIDI music
    // voices; this port has no MIDI backend, so Commentary::pump applies scaledFor() to live speech
    // slots per frame (SNDvol-per-event semantics), default vol 0x7f = identity.
    ChannelMix& mix() { return mix_; }
    const ChannelMix& mix() const { return mix_; }

    // SpeechQueue pump = engine FUN_0048f920 per-frame call (1 frame = 1/50 s, camera cadence) +
    // speech state-cb tick + per-voice gain apply. Defined in commentary.cpp.
    void pump(int frames = 1);

    // Pass-12 parity surface (models above; dossier analysis/pass12speech/SPEECH_PRELOAD_TABLE.md):
    // commentary-scene preload gate DAT_0063241c family, per-event dispatcher cluster and the
    // DAT_006324b4-gated match-clock scheduler. The scheduler queue-depth gate FUN_004c4af0 tracks
    // this instance's SpeechQueue (set in pump()); scene base clock + RNG seed stay caller-driven.
    SpeechPreload& preload() { return preload_; }
    const SpeechPreload& preload() const { return preload_; }
    SpeechEventDispatch& dispatcher() { return *dispatch_; }
    SpeechScheduler& scheduler() { return scheduler_; }
    void seedCommentaryRand(std::uint32_t seed) { scheduler_.setRandSeed(seed); }


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
    ChannelMix mix_;                                           // DAT_0064b9dc model (duck/mix table)
    SpeechGain gain_;                                          // speech master ramp
    int volume_ = SpeechGain::kMaxVol;                         // SNDspeechinit vol (percent table FUN_004906a4, values runtime)
    std::unordered_map<std::uint32_t, std::vector<std::uint8_t>> phraseCache_; // lazy decompressed phrase members
    int lastPhrase_ = -1;
    std::uint32_t decoded_ = 0, sentences_ = 0, lastFrames_ = 0;
    bool prevSpeaking_ = false; // autovol scene-ramp seam edge detector (0x48fcb7/0x491125)
    // Pass-12 family (dispatch_ built in the ctor once q_/preload_ are live).
    SpeechPreload preload_;
    SpeechScheduler scheduler_;
    std::unique_ptr<SpeechEventDispatch> dispatch_;
};

} // namespace wc98::app
