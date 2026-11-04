// Derived from fifa99-native (same author), ported for World Cup 98
// Phrase-bank record-table tests: synthetic BIGF banks (flat + nested + stripped mirror stub)
// gate the parser; the WC98 tree gates PHRS.PBK's full 1,290-phrase / 4,665-segment record
// table, the TYPE.PBK tolerant-mirror prefix equality, empty silent phrases, and the CRWD
// MIDx/raw silent decode dispatch. Layout: docs/reverse-engineering/SPEECH_INDEXING_NOTES.md,
// API: docs/asset-formats/SPEECH_PHRASES.md.
#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/phrs.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/formats/schl.hpp"

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

using namespace wc98::formats;
using Bytes = std::vector<std::uint8_t>;

static void be32(Bytes& b, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s)); }
static void le32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }

// WC98 BIGF table (matrix section 3): magic + u32be declaredSize/count/headerSize, packed
// {u32be offset, u32be size, NUL name}. declaredSize carries the last-entry-size quirk (§2);
// all .PBK names are empty — indexing is purely positional.
struct RawEnt { std::uint32_t off, size; };
static Bytes bigf(std::span<const RawEnt> ents) {
    Bytes b{'B', 'I', 'G', 'F'};
    be32(b, ents.empty() ? 0 : ents.back().size); // declaredSize quirk: last entry's size (§2)
    be32(b, std::uint32_t(ents.size()));
    be32(b, std::uint32_t(16 + ents.size() * (8 + 1))); // header = fixed fields + packed name records
    for (const auto& e : ents) { be32(b, e.off); be32(b, e.size); b.push_back(0); }
    return b;
}
static Bytes makeBank(std::vector<RawEnt> ents, const std::vector<Bytes>& members) {
    CHECK(ents.size() == members.size());
    Bytes data = bigf(ents);
    for (const auto& m : members) data.insert(data.end(), m.begin(), m.end());
    return data;
}

// 20-byte phrase record: zeros with u16le@4 = (phrase<<4)|class and u16le@0x12 = id.
static Bytes rec(std::uint16_t packed, std::uint16_t id) {
    Bytes r(20, 0);
    r[4] = std::uint8_t(packed); r[5] = std::uint8_t(packed >> 8);
    r[0x12] = std::uint8_t(id); r[0x13] = std::uint8_t(id >> 8);
    return r;
}

// PT v2 play-track payload with class-letter tag 0x06, minimal decodeable header: one mono
// EA-XA frame behind it (same synthetic as test_audio's playTrack()).
static Bytes ptPayload(char letter) {
    Bytes b = {'P', 'T', 0x00, 0x02}; // platform u16le 0x0200: version 2 in the high byte
    le32(b, 32);                      // headerSize (smallest observed census value — §1)
    auto tag = [&](std::uint8_t t, std::uint32_t v) {
        b.push_back(t);
        const int len = v > 0xFFFFFF ? 4 : v > 0xFFFF ? 3 : v > 0xFF ? 2 : 1;
        b.push_back(std::uint8_t(len));
        for (int s = (len - 1) * 8; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s));
    };
    tag(0x06, std::uint8_t(letter)); // class letter mirrors the record (§1)
    tag(0x82, 1);                    // channels
    tag(0x83, 7);                    // codec: EA-XA
    tag(0x84, 22050);                // sample rate
    tag(0x85, 28);                   // numSamples (one mono EA-XA frame)
    tag(0x88, 32);                   // data offset: header-relative -> absolute = ptOffset + 32
    b.push_back(0xFF);               // end tag
    while (b.size() < 32) b.push_back(0);
    Bytes frame(15, 0);              // coef set 1, shift 0, nibble 7 -> first sample 28672
    frame[0] = 0x10; frame[1] = 0x70;
    b.insert(b.end(), frame.begin(), frame.end());
    return b;
}

static Bytes flatMember(std::uint16_t packed, std::uint16_t id, char letter) {
    Bytes m = rec(packed, id);
    const auto p = ptPayload(letter);
    m.insert(m.end(), p.begin(), p.end());
    return m;
}

static void syntheticFlatBank() {
    // Phrase class 4 (commentary-phrase) + player class 3: PLRS-style flat members.
    const Bytes m0 = flatMember(4, 7, 'c'), m1 = flatMember(3, 9, 'c');
    std::vector<RawEnt> ents{{std::uint32_t(16 + 2 * 9), std::uint32_t(m0.size())}, {}};
    ents[1].off = ents[0].off + ents[0].size;
    ents[1].size = std::uint32_t(m1.size());
    const auto data = makeBank(ents, {m0, m1});

    const auto bank = PhraseBank::parse(data);
    CHECK(bank.phraseCount() == 2 && bank.recordCount() == 2);
    CHECK(bank.record(0).id == 7 && bank.record(1).id == 9);
    CHECK(bank.record(0).classLetter == 'c' && bank.record(1).classLetter == 'c');
    CHECK(bank.record(0).packed() == 4 && bank.record(0).phrase == 0 && bank.record(1).phrase == 1);
    CHECK(!bank.emptyPhrase(0));
    // PT dispatch branch keeps full PCM decode.
    CHECK(decodePlayTrack(bank.memberBytes(0), 20).frames() == 28);

    // MIDx payload at offset 20: silent dispatch, record still kept (§4 COMOS analogue).
    Bytes mm = rec(0x0F, 1);
    const Bytes midi{'M', 'I', 'D', 'x', 0, 0, 0, 0};
    mm.insert(mm.end(), midi.begin(), midi.end());
    std::vector<RawEnt> me{{std::uint32_t(16 + 9), std::uint32_t(mm.size())}};
    const auto mbank = PhraseBank::parse(makeBank(me, {mm}));
    CHECK(mbank.recordCount() == 1 && mbank.record(0).classLetter == '?');
    CHECK(decodePlayTrack(mbank.memberBytes(0), 20).frames() == 0); // MIDx -> silent
}

static void syntheticNestedAndMirror() {
    // PHRS-style: phrase member = nested BIGF of segments; plus one empty nested archive.
    const Bytes seg = flatMember(4, 5, 'c');
    std::vector<RawEnt> ients{{std::uint32_t(16 + 9), std::uint32_t(seg.size())}};
    const auto inner = makeBank(ients, {seg});
    const Bytes empty{'B', 'I', 'G', 'F', 0, 0, 0, 28, 0, 0, 0, 0, 0, 0, 0, 16}; // count=0, hdrSize=16

    std::vector<RawEnt> oents{{std::uint32_t(16 + 2 * 9), std::uint32_t(inner.size())}, {}};
    oents[1].off = oents[0].off + oents[0].size;
    oents[1].size = std::uint32_t(empty.size());
    const auto full = makeBank(oents, {inner, empty});

    const auto bank = PhraseBank::parse(full);
    CHECK(bank.phraseCount() == 2 && bank.recordCount() == 1);
    CHECK(bank.record(0).phrase == 0 && bank.record(0).segment == 0 && bank.record(0).id == 5);
    CHECK(bank.emptyPhrase(1) && !bank.emptyPhrase(0));

    // TYPE.PBK analogue: keep the outer TOC + part of the nested header, drop segment bytes.
    Bytes stub = full;
    stub.resize(oents[0].off + 24); // outer prefix incl. the start of the inner BIGF table
    CHECK_THROWS(PhraseBank::parse(stub)); // strict: entries past member end throw (§2)
    const auto mirror = PhraseBank::parse(stub, true);
    CHECK(mirror.phraseCount() == 2);
    CHECK(countMirrorPrefixMatches(mirror, bank) == 2);

    // Tolerant clamping visible on outer entries whose payload exceeds the file end.
    const auto t = BigArchive::parse(stub, true);
    CHECK(t.entries.size() == 2 && t.entries[0].truncated && t.entries[1].truncated);
    CHECK(BigArchive::payload(stub, t.entries[0]).size() == std::size_t(stub.size() - oents[0].off));
    CHECK(t.entries[1].offset >= stub.size() && t.entries[1].size == 0); // offset past end -> empty view
    CHECK_THROWS(BigArchive::parse(stub)); // strict keeps the old throw (existing callers)

    // Negative: not a BIG archive at all.
    CHECK_THROWS(PhraseBank::parse(Bytes{'n', 'o', 'p', 'e'}));
}

static void original() {
    const auto root = treeDirOrSkip();
    const auto phrsRaw = wc98::readFile(root / "AUDIO/PHRS/PHRS.PBK");
    const std::span<const std::uint8_t> phrs{phrsRaw};
    const auto bank = PhraseBank::parse(phrs); // strict: full sibling image parses clean
    CHECK(bank.phraseCount() == 1290);
    CHECK(bank.recordCount() == 4665);

    // Record-field gates (§1): u16@4 == 16*phrase+4 on all segments; ids form a bijection onto
    // {0..4,664}; class letter 'c' on every PHRS segment.
    std::vector<char> seen(4665, 0);
    int empties = 0;
    for (std::uint32_t i = 0; i < bank.recordCount(); ++i) {
        const auto& r = bank.record(i);
        CHECK(r.packed() == 16 * r.phrase + 4);
        CHECK(r.classLetter == 'c');
        CHECK(r.id < 4665 && !seen[r.id]);
        seen[r.id] = 1;
    }
    for (std::uint32_t p = 0; p < bank.phraseCount(); ++p) empties += bank.emptyPhrase(p);
    CHECK(empties == 268); // §2: 1,022 segment phrases + 268 silent

    // TYPE.PBK: strict throws (truncated mirror), tolerant parses; byte-exact prefix mirror.
    const auto typeRaw = wc98::readFile(root / "AUDIO/PHRS/TYPE.PBK");
    const std::span<const std::uint8_t> type{typeRaw};
    CHECK_THROWS(PhraseBank::parse(type));
    const auto stub = PhraseBank::parse(type, true);
    CHECK(stub.phraseCount() == 1290);
    const std::uint32_t match = countMirrorPrefixMatches(stub, bank);
    std::printf("TYPE/PHRS mirror prefix equality: %u/%u members byte-exact\n", match, stub.phraseCount());
    CHECK(match == 1290);

    // CRWD strays (§4): MIDx and raw payloads are silent decode dispatch, records kept.
    const auto comosRaw = wc98::readFile(root / "AUDIO/CRWD/COMOS.PBK");
    const std::span<const std::uint8_t> comos{comosRaw};
    const auto cb = PhraseBank::parse(comos);
    CHECK(cb.phraseCount() == 6 && cb.recordCount() == 6);
    {
        const auto m = refpack::decompressIfNeeded(cb.memberBytes(0));
        CHECK(m[20] == 'M' && m[21] == 'I' && m[22] == 'D' && m[23] == 'x'); // MIDx magic at 20
        CHECK(decodePlayTrack(m, 20).frames() == 0);                          // silent dispatch
    }
    const auto crowdRaw = wc98::readFile(root / "AUDIO/CRWD/CROWD.PBK");
    const std::span<const std::uint8_t> crowd{crowdRaw};
    const auto kb = PhraseBank::parse(crowd);
    CHECK(kb.phraseCount() == 16 && kb.recordCount() == 16);
    {
        const auto m = refpack::decompressIfNeeded(kb.memberBytes(0));
        CHECK(decodePlayTrack(m, 20).frames() == 0); // raw crowd bed: undecoded -> silent (§4 Hypothesis)
    }

    // Flat player bank: class-only record (PLRS/P00162.PBK @2,956 anchor — §1).
    const auto plrsRaw = wc98::readFile(root / "AUDIO/PLRS/P00162.PBK");
    const std::span<const std::uint8_t> plrs{plrsRaw};
    const auto pb = PhraseBank::parse(plrs);
    CHECK(pb.phraseCount() == 2 && pb.recordCount() == 2);
    CHECK(pb.record(0).packed() == 3 && pb.record(0).classLetter == 'c');

    // Chant bank: EA-XA class letter 'a'.
    const auto chntRaw = wc98::readFile(root / "AUDIO/CHNT/C0003100.PBK");
    const std::span<const std::uint8_t> chnt{chntRaw};
    const auto cb2 = PhraseBank::parse(chnt);
    CHECK(cb2.recordCount() == 1 && cb2.record(0).classLetter == 'a');

    // Real PHRS segments decode end-to-end through the PT dispatch branch (phrase 0: TOC order
    // [5,0,4,2,3,1] — §1 anchor), counts untouched by the dispatch change.
    {
        const auto innerRaw = refpack::decompressIfNeeded(bank.memberBytes(0));
        const std::span<const std::uint8_t> inner{innerRaw};
        const BigArchive nested = BigArchive::parse(inner);
        CHECK(nested.entries.size() == 6);
        for (std::uint32_t s = 0; s < nested.entries.size(); ++s) {
            const auto segRaw = refpack::decompressIfNeeded(BigArchive::payload(inner, nested.entries[s]));
            const std::span<const std::uint8_t> seg{segRaw};
            CHECK(seg[20] == 'P' && seg[21] == 'T');
            const auto h = PtHeader::parse(seg, 20);
            const auto pcm = decodePlayTrack(seg, 20);
            CHECK(pcm.frames() == h.numSamples);
        }
    }
}

int main() {
    syntheticFlatBank();
    syntheticNestedAndMirror();
    original();
    return 0;
}
