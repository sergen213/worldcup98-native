// Derived from fifa99-native (same author), ported for World Cup 98
// Commentary adapter headless gate: a synthetic PHRS-style nested bank (commentary-class phrases,
// one empty silent phrase, one >20-segment overflow phrase) drives the trigger table -> SpeechQueue
// selection, sentence gating, decode callbacks and drain through the null-mixer path — no audio
// device. The WC98 tree gate loads real PHRS.PBK (1,290 phrases / 4,665 segments), fires doc-cited
// events and asserts segment fetch/decode. Model: docs/reverse-engineering/COMMENTARY_WC98.md;
// record/index layout: docs/reverse-engineering/SPEECH_INDEXING_NOTES.md §1-§2.
#include "check.hpp"
#include "../app/commentary.hpp"

#include "wc98/file.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace wc98;
using Bytes = std::vector<std::uint8_t>;

namespace {

// --- synthetic bank builders (same shape as test_phrs.cpp) ---
void be32(Bytes& b, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s)); }
void le32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }

struct RawEnt { std::uint32_t off, size; };
Bytes bigf(std::span<const RawEnt> ents) {
    Bytes b{'B', 'I', 'G', 'F'};
    be32(b, ents.empty() ? 0 : ents.back().size); // declaredSize quirk (NOTES §2)
    be32(b, std::uint32_t(ents.size()));
    be32(b, std::uint32_t(16 + ents.size() * (8 + 1)));
    for (const auto& e : ents) { be32(b, e.off); be32(b, e.size); b.push_back(0); }
    return b;
}
Bytes makeBank(std::vector<RawEnt> ents, const std::vector<Bytes>& members) {
    CHECK(ents.size() == members.size());
    Bytes data = bigf(ents);
    for (const auto& m : members) data.insert(data.end(), m.begin(), m.end());
    return data;
}

Bytes rec(std::uint16_t packed, std::uint16_t id) { // 20-byte phrase record (§1 layout)
    Bytes r(20, 0);
    r[4] = std::uint8_t(packed); r[5] = std::uint8_t(packed >> 8);
    r[0x12] = std::uint8_t(id); r[0x13] = std::uint8_t(id >> 8);
    return r;
}

Bytes ptPayload(char letter) { // minimal PT v2 mono EA-XA track: decodes to 28 frames
    Bytes b = {'P', 'T', 0x00, 0x02};
    le32(b, 32);
    auto tag = [&](std::uint8_t t, std::uint32_t v) {
        b.push_back(t);
        const int len = v > 0xFFFFFF ? 4 : v > 0xFFFF ? 3 : v > 0xFF ? 2 : 1;
        b.push_back(std::uint8_t(len));
        for (int s = (len - 1) * 8; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s));
    };
    tag(0x06, std::uint8_t(letter));
    tag(0x82, 1);
    tag(0x83, 7); // EA-XA
    tag(0x84, 22050);
    tag(0x85, 28);
    tag(0x88, 32);
    b.push_back(0xFF);
    while (b.size() < 32) b.push_back(0);
    Bytes frame(15, 0);
    frame[0] = 0x10; frame[1] = 0x70;
    b.insert(b.end(), frame.begin(), frame.end());
    return b;
}

// Phrase member: nested BIGF of `nSeg` segments with ids base..base+nSeg-1, packed class-4.
Bytes phraseMember(std::uint32_t phrase, std::uint16_t base, int nSeg) {
    std::vector<RawEnt> ents;
    std::vector<Bytes> segs;
    for (int i = 0; i < nSeg; ++i) {
        Bytes s = rec(std::uint16_t(16 * phrase + 4), std::uint16_t(base + i));
        const auto p = ptPayload('c');
        s.insert(s.end(), p.begin(), p.end());
        ents.push_back({std::uint32_t(16 + (i + 1) * 9), std::uint32_t(s.size())}); // running layout fixed below
        segs.push_back(std::move(s));
    }
    std::uint32_t off = 16 + ents.size() * 9;
    for (auto& e : ents) { e.off = off; off += e.size; }
    return makeBank(ents, segs);
}

Bytes emptyMember() { return Bytes{'B', 'I', 'G', 'F', 0, 0, 0, 28, 0, 0, 0, 0, 0, 0, 0, 16}; } // count=0 (§2)

sim::SimEvent ev(sim::EventType t, int team = 0) { sim::SimEvent e; e.type = t; e.team = std::int8_t(team); return e; }

// --- headless selection gate (null mixer) ---
void synthetic() {
    // Phrases: 0=3 segs ids{10..12}, 1=empty, 2=2 segs ids{13,14}, 3=5 segs ids{15..19},
    // 4=1 seg id20, 5=25 segs ids{100..124} (ring-overflow case).
    const std::vector<int> nSeg{3, 0, 2, 5, 1, 25};
    std::uint16_t base = 10;
    std::vector<Bytes> members;
    for (std::size_t p = 0; p < nSeg.size(); ++p)
        if (nSeg[p]) { members.push_back(phraseMember(std::uint32_t(p), base, nSeg[p])); base += std::uint16_t(nSeg[p]); }
        else members.push_back(emptyMember());
    std::vector<RawEnt> ents;
    std::uint32_t off = std::uint32_t(16 + nSeg.size() * 9);
    for (auto& m : members) { ents.push_back({off, std::uint32_t(m.size())}); off += std::uint32_t(m.size()); }
    auto bytes = std::make_shared<Bytes>(makeBank(ents, members));

    app::Commentary c; // null mixer: decode-only, silent segments complete immediately
    CHECK(c.loadBank(bytes));
    CHECK(c.bank().phraseCount() == 6 && c.bank().recordCount() == 36);
    CHECK(c.lastPhrase() == -1 && c.decodedSegments() == 0);

    // Doc-cited kickoff fires; pick pool skips the empty phrase (§2).
    c.trigger(ev(sim::EventType::KickOff));
    CHECK(c.sentencesStarted() == 1 && c.lastPhrase() == 0);
    CHECK(c.queue().sentenceStatus() == audio::SpeechQueue::kSentenceSpeaking);
    CHECK(c.queue().queued() == 3);

    // One pickup per frame (FUN_0048f920), decode callback fires on start; null mixer drains fast.
    c.pump(1);
    CHECK(c.decodedSegments() == 1 && c.lastDecodeFrames() == 28);
    c.pump(8);
    CHECK(c.queue().queued() == 0 && c.decodedSegments() == 3);

    // Pick consumes the pool head in storage order (empty phrase 1 skipped): goal -> phrase 2.
    c.trigger(ev(sim::EventType::Goal));
    CHECK(c.lastPhrase() == 2 && c.queue().queued() == 2);
    c.pump(1);
    c.trigger(ev(sim::EventType::Shot)); // FUN_004893ec gate: flush + replace, never two sentences
    CHECK(c.lastPhrase() == 3 && c.queue().queued() == 5);
    // Ring cap: SNDphraseadd rejects beyond 0x12 queued -> only 18 of the 25-segment phrase land.
    c.pump(40);
    c.trigger(ev(sim::EventType::Substitution)); // Hypothesis row fires; consumed head -> phrase 4
    c.pump(8);
    c.trigger(ev(sim::EventType::Save)); // Hypothesis row fires; next pool entry = the 25-segment phrase
    CHECK(c.lastPhrase() == 5 && c.queue().queued() == audio::SpeechQueue::kMaxQueued); // cap 0x12 = 18
    c.pump(40);
    CHECK(c.queue().queued() == 0);

    // Picks consume the pool (activation removes records: FUN_004c4a70 qremove): all 5 candidates
    // used up -> qfind returns null, builder skips enqueue -> events stay silent.
    c.trigger(ev(sim::EventType::KickOff));
    CHECK(c.sentencesStarted() == 5 && c.queue().queued() == 0 && c.lastPhrase() == 5);

    // --- speech master-gain seam (SNDspeechinit / SNDspeechautovol; state cb 0x4dfca0) on a
    // fresh instance: pick consumption is per-instance, so the seam needs live picks. ---
    app::Commentary g;
    CHECK(g.loadBank(bytes));
    g.setVolume(0x40);             // pct->vol domain: applied = 0x40, normalized = 64/127
    CHECK(g.masterGain() == float(0x40) / 127.f && g.volume() == 0x40);
    g.autoVol(0, 1);               // steps clamped >=1: one tick lands on target
    g.pump(1);
    CHECK(g.masterGain() == 0.f);
    g.trigger(ev(sim::EventType::KickOff)); // sentence start -> rising edge ramp to scene level
    g.pump(1);                               // delta = (0x40<<16)/5 trunc: applied = 838860>>16
    CHECK(g.masterGain() == float(12) / 127.f && g.masterGain() < float(0x40) / 127.f);
    g.pump(64);                    // sentence drains (null mixer) -> scene-stop edge ramps to silence
    for (int i = 0; i < app::Commentary::kRampTicks + 1; ++i) g.pump(1);
    CHECK(g.masterGain() == 0.f);
    std::printf("commentary synthetic gates OK: %u sentences, %u segment decodes\n",
                c.sentencesStarted(), c.decodedSegments());
}

// --- pure ramp-model gates: exact 16.16 arithmetic of the EACSND state cb (0x4dfca0),
// SNDvol domain clamp + SNDspeechautovol semantics; evidence analysis/pass7/gain*.txt ---
void gainModel() {
    app::SpeechGain g; // default = full-open (SNDspeechinit vol 0x7f)
    CHECK(g.applied() == 0x7F && g.frac() == 1.0f);
    g.set(0x40);
    CHECK(g.applied() == 0x40 && g.frac() == float(0x40) / 127.f);
    g.set(0x80); // SNDvol domain clamp [0,0x7f]
    CHECK(g.applied() == 0x7F);

    g.ramp(0, 4); // linear down: cur -= cur/4 per tick; SAR apply floors toward zero
    g.tick(); CHECK(g.applied() == 95);   // (8323072 - 2080768) >> 16
    g.tick(); CHECK(g.applied() == 63);
    g.tick(); CHECK(g.applied() == 31);
    g.tick(); CHECK(g.applied() == 0);    // clamp at target, delta zeroed
    g.tick(); CHECK(g.applied() == 0);

    g.set(0);
    g.ramp(100, 3); // truncating delta: one tick short of target, overshoot clamps next tick
    g.tick(); CHECK(g.applied() == 33);
    g.tick(); CHECK(g.applied() == 66);
    g.tick(); CHECK(g.applied() == 99);   // truncation artifact matches the original SAR clamp path
    g.tick(); CHECK(g.applied() == 100);
    g.tick(); CHECK(g.applied() == 100);

    g.ramp(10, 0); // steps clamped >= 1 (SNDspeechautovol)
    g.tick(); CHECK(g.applied() == 10);
}
// Duck/mix table model (DAT_0064b9dc, pass 7d): SNDmidiinit byte defaults + CC7 writes, key-on
// scaling formula, percent-table getter FUN_004906a4 clamp + scene seam. analysis/pass7/duck2.txt, duck3.txt.
void duckModel() {
    app::ChannelMix m;
    for (int c = 0; c < app::ChannelMix::kSlots; ++c) CHECK(m.volume(c) == 0x7F); // full-volume default bytes
    CHECK(app::ChannelMix::scaled(0x40, 0x40) == 32 && app::ChannelMix::scaled(0x7f, 100) == 100);
    m.setVolume(3, 64); // CC7 write: tbl[chan][+1] = data byte
    CHECK(m.volume(3) == 64 && m.scaledFor(3, 100) == 50 && m.scaledFor(4, 100) == 100); // 100*64/127 int div; other chan untouched
    m.setVolume(3, 0x1FF); // MIDI data byte masked & 0x7f (CC handler stores the raw byte)
    CHECK(m.volume(3) == 0x7F && m.scaledFor(3, 65) == 65);
    m.setVolume(3, 0);
    CHECK(m.scaledFor(3, 100) == 0); // full duck = silence
    m.reset();
    CHECK(m.volume(3) == 0x7F); // SNDseqstop default restore
    // Percent table FUN_004906a4: pct*0x7f/100, clamp >= 1, NO upper clamp (original has none).
    CHECK(app::Commentary::percentToVol(100) == 0x7F && app::Commentary::percentToVol(50) == 63);
    CHECK(app::Commentary::percentToVol(0) == 1 && app::Commentary::percentToVol(-5) == 1);
    // Scene seam (site 0x48fcb2): percent -> vol, then autovol ramp toward the scene level.
    app::Commentary c;
    c.setScenePercent(50);
    CHECK(c.volume() == 63 && c.masterGain() == 1.f); // ramp is per-tick (state cb), not immediate
    c.pump();                                         // one pump tick advances cur toward the scene level
    CHECK(c.masterGain() < 1.f && c.masterGain() > 0.f);
    // Scene defaults MEASURED live (pass 7e, analysis/pass7/percent1.txt): table snapshot
    // DAT_00632778 = {80,80,80,80,65,80,1}; speech scene level = idx4 getter -> 65 %.
    CHECK(app::Commentary::kScenePct[4] == 65 && app::Commentary::kSceneSpeechPct == 65);
    c.setScenePercent(app::Commentary::kSceneSpeechPct); // live setter triple @0x46b095 value
    CHECK(c.volume() == app::Commentary::percentToVol(65));
}
// --- tree gate: real PHRS.PBK selection + decode (skipping when the tree is absent) ---

void original() {
    const auto root = treeDirOrSkip();
    app::Commentary c; // null mixer keeps the gate headless on CI runners with audio hardware
    CHECK(c.loadTree(root));
    CHECK(c.bank().phraseCount() == 1290 && c.bank().recordCount() == 4665);

    for (const auto t : {sim::EventType::KickOff, sim::EventType::Goal, sim::EventType::Foul}) {
        const std::uint32_t before = c.decodedSegments();
        c.trigger(ev(t));
        CHECK(c.lastPhrase() >= 0 && c.queue().queued() > 0);
        c.pump(16);
        // Real PHRS segments are MicroTalk 'c' tracks: the fetch/decode callback must produce audio.
        CHECK(c.decodedSegments() > before && c.lastDecodeFrames() > 0);
        std::printf("PHRS tree commentary: %s -> phrase %d (%d queued segment(s), last decode %u frames)\n",
                    std::string(sim::eventName(t)).c_str(), c.lastPhrase(), c.queue().queued(), c.lastDecodeFrames());
    }
    CHECK(c.sentencesStarted() == 3);
    std::printf("commentary tree gates OK: %u sentences, %u segment decodes\n",
                c.sentencesStarted(), c.decodedSegments());
}

// ---- Pass-12 per-event preload + scheduler parity gates ----------------------------------------
// Models: app/commentary.hpp SpeechPreload / SpeechEventDispatch / SpeechScheduler; dossier
// analysis/pass12speech/SPEECH_PRELOAD_TABLE.md (+ raw decompiles speech_pick*.txt).

// Predict the port RNG model's first FUN_004c321c-equivalent draw from a seed.
bool rngFiresAt(std::uint32_t seed) {
    std::uint32_t st = seed;
    return app::SpeechScheduler::randAdvance(st) % 5 == 0;
}

// (a) FUN_0048d378/0x48d3f4 gate semantics: scene-enabled precondition, once-only transition,
// bank handle = SNDbankcreate(DAT_00632420) as the phrase-id source (FUN_0048d2f0).
void preloadGate() {
    app::SpeechPreload p;
    CHECK(!p.enable());                                  // FUN_00490674() == 0: no transition
    CHECK(p.enables() == 0 && !p.enabled() && p.phraseId() == 0);
    p.setSceneEnabled(true);
    CHECK(p.enable());                                   // gate 0->1, arena reserved, bank created
    CHECK(p.enabled() && p.arena());
    CHECK(p.bankHandle() == 0 && p.phraseId() == p.bankHandle());
    CHECK(!p.enable());                                  // once-only: DAT_0063241c already set
    CHECK(p.enables() == 1);
    p.disable();                                         // FUN_0048d3f4: gate=0, arena released
    CHECK(!p.enabled() && !p.arena());
    CHECK(p.phraseId() == 0);                            // FUN_0048d2f0 with cleared gate -> 0
    p.disable();                                         // no-op while gate clear (disables stays 1)
    CHECK(p.disables() == 1 && p.enable());              // re-enable: next bank-table slot
    CHECK(p.bankHandle() == 1 && p.banksCreated() == 2);
}

// (b) Jump table 0x48f45c dispatch: every case routes its record set through SNDphraseadd with
// overlap = 0 and writes the queue-slot result back into record+6; the exit label fires
// SNDsentencestart for every dispatched event, including the >7 fallthrough.
void eventDispatchGates() {
    audio::SpeechQueue q;
    app::SpeechPreload p;
    p.setSceneEnabled(true);
    CHECK(p.enable());                                   // phrase-id source = bank handle 0
    app::SpeechEventDispatch d{q, p};
    int sentences = 0;
    for (std::uint8_t ev = 0; ev <= 7; ++ev) {
        const bool withCands = (ev == 2 || ev == 4 || ev == 6); // hook FUN_0048f2d8 fills {2,4,6}
        app::SpeechEventRecord prim, c1, c2;
        prim.phraseId = std::int32_t(100 + ev);
        c1.phraseId = 400; c2.phraseId = 401;
        d.clearCandidates();
        if (withCands) { d.setCandidate(0, &c1); d.setCandidate(1, &c2); }
        const int addsBefore = d.addCalls();
        CHECK(d.dispatch(ev, &prim));                    // handler returns 1 through 0x48f4ea
        ++sentences;
        CHECK(d.sentencesStarted() == std::uint32_t(sentences)); // SNDsentencestart at exit label
        CHECK(prim.queueSlot >= 0 && prim.queueSlot < audio::SpeechQueue::kSlots); // [record+6] = EAX
        const auto& slot = q.slot(prim.queueSlot);
        CHECK(slot.id == std::uint16_t(100 + ev));       // CX = record+0xE u16 handle
        CHECK(slot.overlap == 0);                        // XOR EBX,EBX: overlap = 0
        CHECK(slot.param2 == p.phraseId());              // EAX from FUN_0048d2f0 (bank global)
        if (withCands) {                                 // hook-filled candidates run the same body
            CHECK(c1.queueSlot >= 0 && c2.queueSlot >= 0);
            CHECK(q.slot(c1.queueSlot).id == 400 && q.slot(c2.queueSlot).id == 401);
            CHECK(d.addCalls() == addsBefore + 3);       // primary + two candidates
        } else {
            CHECK(d.addCalls() == addsBefore + 1);
        }
    }
    // Ring overflow: slot write-back stores the SNDphraseadd reject code verbatim (record+6 = -13).
    int guard = 0;
    while (q.queued() < audio::SpeechQueue::kMaxQueued && guard++ < 40) {
        app::SpeechEventRecord r; r.phraseId = 300;
        CHECK(d.dispatch(1, &r)); ++sentences;
    }
    CHECK(q.queued() == audio::SpeechQueue::kMaxQueued);
    d.clearCandidates();
    app::SpeechEventRecord over; over.phraseId = 500;
    CHECK(d.dispatch(1, &over)); ++sentences;
    CHECK(over.queueSlot == audio::SpeechQueue::kOverflow); // EAX = -13 stored at [record+6]
    // >7 fallthrough: JA 0x48f4ea -> no phraseadd, exit label still fires SNDsentencestart.
    app::SpeechEventRecord prim2; prim2.phraseId = 999;
    const int addsBefore = d.addCalls();
    CHECK(d.dispatch(8, &prim2)); ++sentences;
    CHECK(d.addCalls() == addsBefore);                   // no body: event byte > 7 skips dispatch
    CHECK(prim2.queueSlot == app::SpeechEventRecord::kNoSlot);
    CHECK(d.sentencesStarted() == std::uint32_t(sentences));
    // AND AL,0x3f mask: 0xC2 & 0x3f == 2 -> dispatches as case 2. Drain the ring first (completion
    // callback reports every slot done) so the add lands in a free slot.
    q.onComplete([](int) { return true; });
    q.pump(48);
    CHECK(q.queued() == 0);
    app::SpeechEventRecord prim3; prim3.phraseId = 7;
    CHECK(d.dispatch(0xC2, &prim3)); ++sentences;
    CHECK(prim3.queueSlot >= 0 && q.slot(prim3.queueSlot).id == 7);
}

// (c)+(d) FUN_0048d7ec record fields + RNG-gated variant + FUN_0048d9e0 pace rewrite. Deterministic
// under a fixed seed: the gates recompute the %5/%200 draws with the exposed randAdvance step.
void schedulerGates() {
    app::SpeechScheduler s;
    // Gate chain: DAT_006324b4 off, FUN_004906d0 level <= 0, FUN_004c4af0 depth >= 2 all reject.
    CHECK(s.scheduleEvent(1, 60, 30, 12) == 0);          // scheduler gate off (FUN_0048dfc4 not run)
    s.start();
    s.setSceneBase(1000);                                // DAT_00636a14
    CHECK(s.scheduleEvent(1, 60, 30, 12) == 0);          // FUN_004906d0() == 0
    s.setSpeechLevel(1);
    s.setQueueDepth(2);                                  // FUN_004c4af0() >= 2
    CHECK(s.scheduleEvent(1, 60, 30, 12) == 0);
    s.setQueueDepth(0);

    constexpr std::int32_t kBase = 1000;                 // sceneBase -> priority = kBase + 3
    for (std::uint32_t seed = 1; seed <= 48; ++seed) {
        s.setRandSeed(seed);
        const bool fires = rngFiresAt(seed);             // FUN_004c321c() % 5 == 0 gate
        const int n = s.scheduleEvent(1, 90, 30, 60);    // t*100/60: t0=150, dB=50, dC=100
        CHECK(n == (fires ? 2 : 1));                     // (d) second variant fires iff seeded %5==0
        const auto& recs = s.pending();
        CHECK(recs.size() == std::size_t(n));
        const auto& r1 = recs[0];
        CHECK(r1.sceneId == 1);                          // +8
        CHECK(r1.t0 == 90 * 100 / 60);                   // +0x10 clock conversion
        CHECK(r1.start == kBase + 150 + 100);            // +0x18 = DAT_00636a14 + t0 + durC
        CHECK(r1.end == r1.start + 30 * 100 / 60);       // +0x1c = start + durB
        CHECK(r1.priority == kBase + 3);                 // +0x14 = DAT_00636a14 + 3
        CHECK(r1.delay == (fires ? app::SpeechScheduleRecord::kDelayRngVariant
                                 : app::SpeechScheduleRecord::kDelayPrimary));
        if (fires) {
            const auto& r2 = recs[1];
            std::uint32_t st = seed;
            app::SpeechScheduler::randAdvance(st);       // consume the %5 gate draw
            const std::int32_t jitter = std::int32_t(app::SpeechScheduler::randAdvance(st) % 200) + 150 + 400;
            CHECK(r2.t0 == jitter);                      // rng%200 + t0 + 400
            CHECK(r2.start == kBase + jitter + 100);
            CHECK(r2.end == r2.start + 50 && r2.priority == kBase + 3);
            CHECK(r2.delay == app::SpeechScheduleRecord::kDelayPrimary);
        }
        s.reset();                                       // keep one event per scheduler state
    }
    // sceneId == 2 exclusion: the RNG variant never fires for scene 2 (rec[+8] != 2 test).
    std::uint32_t seed = 1;
    while (!rngFiresAt(seed)) ++seed;                    // find a firing seed
    s.setRandSeed(seed);
    CHECK(s.scheduleEvent(2, 90, 30, 60) == 1);          // gate hits but sceneId==2 -> single record
    CHECK(s.pending()[0].delay == app::SpeechScheduleRecord::kDelayPrimary);
    s.reset();
    // (c) sweep rewrite: +0x1c = sceneBase + (paceA*0x20 - paceB*7)*4/60 for every queued record.
    std::uint32_t quiet = 1;                             // non-firing RNG seed (variant stays off)
    while (rngFiresAt(quiet)) ++quiet;
    s.setRandSeed(quiet);
    CHECK(!rngFiresAt(quiet));
    const int n = s.scheduleEvent(1, 90, 30, 60);
    CHECK(n == 1);
    const int swept = s.sweepPace(3, 7);                 // ((96-49)*4)/60 = 3
    CHECK(swept == n && s.emitted().size() == std::size_t(n) && s.pending().empty());
    for (const auto& e : s.emitted()) { CHECK(e.end == kBase + 3); CHECK(e.t0 == 150); }
}

// Commentary wiring: the scheduler queue-depth gate FUN_004c4af0 tracks the live phrase ring.
void schedulerWiring() {
    const std::vector<int> nSeg{3, 2};
    std::uint16_t base = 10;
    std::vector<Bytes> members;
    for (std::size_t p = 0; p < nSeg.size(); ++p) {
        members.push_back(phraseMember(std::uint32_t(p), base, nSeg[p]));
        base += std::uint16_t(nSeg[p]);
    }
    std::vector<RawEnt> ents;
    std::uint32_t off = std::uint32_t(16 + nSeg.size() * 9);
    for (auto& m : members) { ents.push_back({off, std::uint32_t(m.size())}); off += std::uint32_t(m.size()); }
    auto bytes = std::make_shared<Bytes>(makeBank(ents, members));

    app::Commentary w;
    CHECK(w.loadBank(bytes));
    w.scheduler().start();
    w.trigger(ev(sim::EventType::KickOff));              // 3 segments queued
    CHECK(w.queue().queued() == 3);
    w.pump(1);                                           // pump feeds FUN_004c4af0 depth from the ring
    CHECK(w.scheduler().gateQueueDepth() == w.queue().queued());
    app::SpeechEventRecord none;                         // exit-label-only dispatch (no records)
    CHECK(w.dispatcher().dispatch(9, &none));
    CHECK(none.queueSlot == app::SpeechEventRecord::kNoSlot);
    std::printf("commentary pass-12 wiring gates OK: depth %d tracked\n", w.scheduler().gateQueueDepth());
}

} // namespace

int main() {
    synthetic();
    original();
    duckModel();
    gainModel();
    preloadGate();
    eventDispatchGates();
    schedulerGates();
    schedulerWiring();
    std::puts("test_commentary OK");
    return 0;
}
