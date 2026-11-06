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

    // Rotation: goal -> next candidate (phrase 2, skipping empty phrase 1). Gate flush mid-sentence.
    c.trigger(ev(sim::EventType::Goal));
    CHECK(c.lastPhrase() == 2 && c.queue().queued() == 2);
    c.pump(1);
    c.trigger(ev(sim::EventType::Shot)); // FUN_004893ec gate: flush + replace, never two sentences
    CHECK(c.lastPhrase() == 3 && c.queue().queued() == 5);

    // Unmapped event stays silent (contract §2).
    const std::uint32_t started = c.sentencesStarted();
    c.trigger(ev(sim::EventType::Pass));
    CHECK(c.sentencesStarted() == started && c.queue().queued() == 5);

    // Ring cap: SNDphraseadd rejects beyond 0x12 queued -> only 18 of the 25-segment phrase land.
    c.pump(40);
    c.trigger(ev(sim::EventType::Substitution)); // Hypothesis row fires; rotation cursor 3 -> phrase 4
    c.pump(8);
    c.trigger(ev(sim::EventType::Save)); // Hypothesis row fires; next pool entry = the 25-segment phrase
    CHECK(c.lastPhrase() == 5 && c.queue().queued() == audio::SpeechQueue::kMaxQueued); // cap 0x12 = 18
    c.pump(40);
    CHECK(c.queue().queued() == 0);
    // --- speech master-gain seam (SNDspeechinit / SNDspeechautovol; state cb 0x4dfca0) ---
    CHECK(c.masterGain() == 0.f); // burst drained -> scene-stop ramp silenced the speech stream
    c.setVolume(0x40);             // pct->vol domain: applied = 0x40, normalized = 64/127
    CHECK(c.masterGain() == float(0x40) / 127.f && c.volume() == 0x40);
    c.autoVol(0, 1);               // steps clamped >=1: one tick lands on target
    c.pump(1);
    CHECK(c.masterGain() == 0.f);
    c.trigger(ev(sim::EventType::KickOff)); // sentence start -> rising edge ramp to scene level
    c.pump(1);                                // delta = (0x40<<16)/5 trunc: applied = 838860>>16
    CHECK(c.masterGain() == float(12) / 127.f && c.masterGain() < float(0x40) / 127.f);
    c.pump(64);                    // sentence drains (null mixer) -> scene-stop edge ramps to silence
    for (int i = 0; i < app::Commentary::kRampTicks + 1; ++i) c.pump(1);
    CHECK(c.masterGain() == 0.f);
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

} // namespace

int main() {
    synthetic();
    original();
    gainModel();
    std::puts("test_commentary OK");
    return 0;
}
