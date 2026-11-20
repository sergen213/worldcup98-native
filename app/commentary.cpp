// Derived from fifa99-native (same author), ported for World Cup 98
#include "commentary.hpp"

#include "wc98/reader.hpp" // wc98::FormatError
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/formats/schl.hpp"

#include <algorithm>
#include <string_view>

namespace wc98::app {
namespace {

// Event -> commentary trigger table (mirrors the FUN_004893ec seam: 46 callers in the match-engine
// event region, "kickoff/goal/foul per-caller-site" — COMMENTARY_WC98.md §Game-side layer, Trigger
// gate; builder = pending-event scan + pick + enqueue, FUN_00489460). Event codes are the ported
// sim vocabulary (simulation/include/wc98/sim/events.hpp; raised from Match_onEvent 0x470F98 in
// rules.cpp). The exact site->code alignment is open item 1 ("remains open"), so rows beyond the
// three doc-named kinds are marked Hypothesis. All other events stay silent (default).
constexpr struct {
    sim::EventType type;
    bool docCited; // true = named in COMMENTARY_WC98.md; false = Hypothesis (contract list)
} kTriggerTable[] = {
    {sim::EventType::KickOff,  true},   // "kickoff" — COMMENTARY_WC98.md trigger-gate band (0x452af0..0x458cf0)
    {sim::EventType::Goal,     true},   // "goal"    — same line; goal commentary noted at rules.cpp case 6 @0x470F98
    {sim::EventType::Foul,     true},   // "foul"    — COMMENTARY_WC98.md trigger-gate band
    {sim::EventType::Shot,     false},  // Hypothesis: commentary fires on shots (no doc site map yet)
    {sim::EventType::Save,     false},  // Hypothesis: keeper-save call (open item 1 alignment pending)
    {sim::EventType::Substitution, false}, // Hypothesis: substitution line (open item 1 alignment pending)
    {sim::EventType::OwnGoal,  false},  // Hypothesis: shares the goal band; own-goal text unmeasured
};

[[nodiscard]] bool fires(sim::EventType t) {
    for (const auto& row : kTriggerTable)
        if (row.type == t) return true;
    return false;
}

// Class-letter gain table. COMMENTARY_WC98.md gives only the API volume domain — SNDspeechinit
// vol in [0,0x80) and the autovol ramp (SNDspeechautovol, DAT_00588d6c), "not modelled" per
// SPEECH_PHRASES.md §2. Chosen constants: mid-domain 0x40 normalized = 0.5f for the PT class
// letters 'c' (PHRS/PLRS/TEAM MicroTalk) and 'a' (CHNT EA-XA); '?' payloads decode silent and
// are never played. // Hypothesis: per-class split of that volume is unmeasured.
[[nodiscard]] float gainForClass(char letter) {
    return (letter == 'c' || letter == 'a') ? 0.5f : 1.0f;
}

constexpr std::uint32_t kRecordSize = 20; // 20-byte phrase record || payload (NOTES §1)

} // namespace

Commentary::Commentary(audio::Mixer* mixer) : mixer_(mixer) {
    q_.onStart([this](int slot, std::uint16_t id) { onStart(slot, id); });
    q_.onComplete([this](int slot) { return onComplete(slot); });
    // Pass-12 dispatcher cluster model (FUN_0048f4b0 family): shares this instance's queue + gate.
    dispatch_.reset(new SpeechEventDispatch{q_, preload_});
}

bool Commentary::loadBank(std::shared_ptr<const std::vector<std::uint8_t>> phrsBytes) {
    try {
        bank_ = formats::PhraseBank::parse(*phrsBytes); // strict: full PHRS sibling parses clean (NOTES §2)
    } catch (const FormatError&) {
        return false;
    }
    bytes_ = std::move(phrsBytes);
    byId_.clear();
    segIds_.assign(bank_.phraseCount(), {});
    for (std::uint32_t i = 0; i < bank_.recordCount(); ++i) {
        const auto& r = bank_.record(i);
        if (r.phrase < segIds_.size()) segIds_[r.phrase].push_back(r.id); // record table is (phrase, segment) TOC order
        byId_[r.id] = i;
    }
    candidates_.clear();
    for (std::uint32_t p = 0; p < bank_.phraseCount(); ++p) {
        if (bank_.emptyPhrase(p) || segIds_[p].empty()) continue; // 268 silent phrases in PHRS (NOTES §2)
        const auto& first = bank_.record(byId_[segIds_[p].front()]);
        if (first.packed() == 16 * p + 4) candidates_.push_back(p); // commentary class 4, gated 1022/1022 (§1)
    }
    return !candidates_.empty();
}

bool Commentary::loadTree(const std::filesystem::path& treeRoot) {
    auto path = treeRoot / "AUDIO/PHRS/PHRS.PBK";
    if (!std::filesystem::exists(path)) {
        const auto ci = findCaseInsensitive(treeRoot, "AUDIO/PHRS/PHRS.PBK");
        if (ci.empty()) return false;
        path = ci;
    }
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(readFile(path));
    return loadBank(bytes);
}

void Commentary::trigger(const sim::SimEvent& e) {
    if (!fires(e.type)) return; // unmapped events -> silent (contract §2)
    // FUN_004893ec gate: flush the queue and stop any in-flight sentence before enqueuing
    // (SNDsentencestop loop + FUN_004901e0 flush, COMMENTARY_WC98.md Trigger gate). The stream-kill
    // fan-out (FUN_004e2c60) is the mixer's job: stop live voices first (SPEECH_PHRASES.md §2).
    if (mixer_) for (auto& sid : sounds_) { if (sid) mixer_->stop(sid); sid = 0; }
    q_.sentenceStop();
    // FUN_0048d0dc pick = qfind (FUN_004c4ce0) first-match over the class-keyed ready list; the
    // activation path FUN_0048d1f8 removes each picked record from its queue via FUN_004c4a70
    // qremove, so a phrase plays exactly once and an exhausted pool gives a null pick -> the
    // builder skips enqueue (event silent). No rotation/counter exists in the original chain.
    // Deviation: the exe's per-record slot tags (+0xC, predicate FUN_0048cf10) are preloaded by
    // the tournament presentation lane — pass 76: ring DAT_0057e380 slots filled by FUN_004894ac
    // {slot,type,id}, loader FUN_0048953c formats AUDIO\team\G_T%04d.pbk /
    // AUDIO\pbp\schedule|standing files, callback FUN_0048970c tags records into DAT_00632428
    // class queues. The FUN_0048d4f4 lane (gate DAT_006324b4, pointer-called: zero direct E8
    // sites) registers its records through FUN_0048d114 with tag = [obj+4] and pushes the
    // object onto queue DAT_00632524 — chant-lane registration (pass-56: chant pump
    // FUN_0048da50 drain branch consumes qlength(DAT_00632524)), not a commentary-tag preload;
    // the in-play dispatcher tags nothing, so seam slots share the storage-order pool;
    // first-match over a shared list is then sequential consumption.
    // Evidence: COMMENTARY_WC98.md §pick, analysis/pass76pbp/NOTES.md.
    // Pass-88/89: selector(0) = ready-queue head entry. Pass-90 decoded the registry-stamped
    // class byte (driver -> FUN_0048ffe8 -> two-level TYPE.PBK lookup -> inner segment-record
    // name byte 0, analysis/pass90reg/NOTES.md): retail tags are {0x07} (PHRS preload arm) and
    // {0x40} ((byte&0x3f)==0 -> engine callback), never {2,4,6}, so the jump-table selector
    // arms (0x48f2bc) never run. The engine-callback lane 0x49001c runs a second registry find
    // (0x48ff64, writes the SUM of the two located-slot values to the driver slot — pass-92
    // full wrapper disasm analysis/pass92cbr/e5.txt, no doubling instruction; the pass-90
    // "2*found-index" wording was unbacked; pass-93 decoded the slots: sum of BIGF record
    // offset fields, high slot = segment length — analysis/pass93wr/) — no queue head pick
    // is proven there,
    // so the sequential head pick here stays a deviation [H]. Class-7 events route to the PHRS
    // preload lane, mirrored pass-91 as triggerLive7. Gate feed decoded pass-94: the
    // accessor slots hold pointers to the static home/away team-record images stamped
    // by match setup FUN_004d4950, and the +0x34 gate u16 = the team's FCDB record id
    // (dword0 & 0x7ff, written by loader FUN_004cc060) — analysis/pass94gate/. Live
    // events still carry no retail class-7 byte [H].
    startPick(0);
}

// Pass-86 in-play lane (0x48f830): the start attempt rejects while a sentence speaks
// (SNDsentencestatus FUN_004dfc0c == 3); the record recycles to free queue DAT_006326c8, so the
// event phrase is dropped, never deferred. Idle start path shares the presentation pick; the
// lane issues no stop (nothing interrupts).
void Commentary::triggerLive(const sim::SimEvent& e) {
    if (!fires(e.type)) return; // unmapped events never enter the record lane
    if (q_.sentenceStatus() == audio::SpeechQueue::kSentenceSpeaking && q_.queued() > 0) {
        ++rejected_; // reject while speaking: record recycles, phrase dropped
        return;
    }
    if (candidates_.empty()) {
        ++rejected_; // exhausted ready pool: qfindindex null, record recycles (pass-89)
        return;
    }
    startPick(0); // pool head pick (port deviation [H], cb lane does no qfindindex); no selector-class tags exist (pass-90)
}

// Pass-91 class-7 preload lane (dispatch arm 0x48f354, analysis/pass91c7/NOTES.md):
// accessor FUN_0048f290 reads the match-record table (home slot DAT_00641014, away slot
// base+0xfc = DAT_00641110) field +0x34 u16 (pass-94: the slots are pointers to the
// static home/away team-record images stamped by match setup FUN_004d4950; +0x34 = the
// team's FCDB record id = team record dword0 & 0x7ff, stamped by loader FUN_004cc060);
// gate 0 -> accessor returns 0, gate nonzero ->
// queue lane slot DAT_00632428 + 1*0x1c selector successor pick (EBX 0 home / 1 away). The
// dispatch returns 0 when both picks are 0 (record recycled, event silent). Busy-reject and
// exhausted-pool lanes shared with triggerLive.
void Commentary::triggerLive7(std::uint16_t homeId, std::uint16_t awayId) {
    if (q_.sentenceStatus() == audio::SpeechQueue::kSentenceSpeaking && q_.queued() > 0) {
        ++rejected_; // record lane 0x48f830 reject: recycles to free queue, phrase dropped
        return;
    }
    if (candidates_.empty()) {
        ++rejected_; // exhausted ready pool: null pick, record recycles (pass-89 lane)
        return;
    }
    if (homeId != 0) {
        startPick(0); // home gate open: selector 0 = head node of the class-1 queue
        return;
    }
    if (awayId != 0) {
        startPick(1); // away gate open (home closed): selector 1 = successor of head
        return;
    }
    ++rejected_; // both +0x34 gates zero: dispatch return 0, record recycled, event silent
}

void Commentary::startPick(std::size_t selector) {
    // qfindindex semantics: sel-th successor of the ready-list head node; a short (or empty)
    // chain returns NULL -> the builder skips the enqueue, event stays silent.
    if (selector >= candidates_.size()) return; // qfind/qfindindex null: no commentary for this event
    const std::uint32_t p = candidates_[selector];
    candidates_.erase(candidates_.begin() + selector); // activation qremove FUN_004c4a70
    lastPhrase_ = int(p);
    // Segment run: each phrase owns a contiguous id block (NOTES §1, 1022/1022 confirmed). The
    // within-phrase ordering is UNKNOWN (permutation source unmeasured) — deviation noted: play
    // in TOC order, per phrase-record table.
    for (const std::uint16_t id : segIds_[p]) q_.addPhrase(id); // ring cap 20; overflow -> -13 skipped
    q_.sentenceStart(); // SNDsentencestart = ordered run of segment ids (NOTES §5.3 model)
    ++sentences_;
}

void Commentary::onStart(int slot, std::uint16_t id) {
    sounds_[std::size_t(slot)] = 0;
    const auto it = byId_.find(id);
    if (it == byId_.end()) return; // unknown handle: stay silent, complete via onComplete
    const auto& r = bank_.record(it->second);
    const auto cached = phraseCache_.find(r.phrase);
    const std::vector<std::uint8_t>* member = nullptr;
    if (cached != phraseCache_.end()) {
        member = &cached->second;
    } else {
        member = &phraseCache_.emplace(r.phrase, formats::refpack::decompressIfNeeded(bank_.memberBytes(r.phrase))).first->second;
    }
    const std::span<const std::uint8_t> m{*member};
    if (!formats::BigArchive::isBig(m)) return; // flat member (not PHRS): not handled here
    try {
        const auto nested = formats::BigArchive::parse(m);
        if (r.segment >= nested.entries.size()) return;
        const auto seg = formats::BigArchive::payload(m, nested.entries[r.segment]);
        // decodePlayTrack dispatches on the payload magic at record end (SPEECH_PHRASES.md §1):
        // "PT" decodes, MIDx/empty-BIGF/raw beds return silent Pcm{}.
        const auto pcm = formats::decodePlayTrack(seg, std::min<std::size_t>(kRecordSize, seg.size()));
        ++decoded_;
        lastFrames_ = std::uint32_t(pcm.frames());
        const float g = gainForClass(r.classLetter); // Hypothesis per-class level (see above)
        slotGain_[std::size_t(slot)] = g;
        if (mixer_ && pcm.frames() > 0) sounds_[std::size_t(slot)] = mixer_->play(pcm, g * gain_.frac());
    } catch (const FormatError&) {
        // Malformed segment: silent skip; onComplete reports done.
    }
}

bool Commentary::onComplete(int slot) {
    const auto sid = sounds_[std::size_t(slot)];
    if (mixer_ && sid != 0) return !mixer_->playing(sid); // play-status probe (FUN_004d9250 analog)
    slotGain_[std::size_t(slot)] = 0.f;
    return true; // silent decode or no device: complete immediately
}

// Per-frame: SpeechQueue pump (FUN_0048f920) + speech state-cb tick (0x4dfca0 semantics: advance
// the 16.16 master volume, clamp at target) + per-voice apply (the original applies cur>>16 via
// SNDvol to the live streams each tick). Scene-ramp seam: a sentence is ACTIVE from its start
// (queued segments) until the queue drains; rising edge ramps toward the scene level, draining
// edge back toward silence (autovol sites 0x48fcb7 / 0x491125).
void Commentary::pump(int frames) {
    for (int f = 0; f < frames; ++f) {
        q_.pump(1);
        // Pass-12 scheduler queue-depth gate FUN_004c4af0 tracks this instance's phrase ring.
        scheduler_.setQueueDepth(q_.queued());
        const bool active = q_.sentenceStatus() == audio::SpeechQueue::kSentenceSpeaking && q_.queued() > 0;
        if (active != prevSpeaking_) {
            gain_.ramp(active ? volume_ : 0, kRampTicks);
            prevSpeaking_ = active;
        }
        gain_.tick();
        if (!mixer_) continue;
        const float m = gain_.frac();
        for (std::size_t s = 0; s < audio::SpeechQueue::kSlots; ++s)
            if (sounds_[s] && slotGain_[s] > 0.f) {
                // SNDvol per-frame apply + channel duck/mix table DAT_0064b9dc (+1 CC7 byte): key-on
                // formula voiceVol * cc7 / 0x7f, and every CC7 write re-applies SNDvol to all active
                // voices in MIDI timer cb LAB_004e3bd4 (analysis/pass7/duck3.txt). Default vol 0x7f =
                // identity. Deviation: port has no MIDI music bus; speech slots occupy the channel
                // window (COMMENTARY_WC98.md §pass 7d).
                const int base = int(slotGain_[s] * SpeechGain::kMaxVol * m + 0.5f);
                mixer_->setGain(sounds_[s], float(mix_.scaledFor(int(s), base)) / SpeechGain::kMaxVol);
            }
    }
}

} // namespace wc98::app
