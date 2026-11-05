// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/phrs.hpp"
#include "wc98/reader.hpp"
#include "wc98/formats/refpack.hpp"
#include <algorithm>
#include "wc98/formats/schl.hpp"

namespace wc98::formats {
namespace {

// Clamped payload view: tolerant mirror parses leave TOC offsets at/past the member end;
// BigArchive::payload would subspan out of range there.
std::span<const std::uint8_t> safeView(std::span<const std::uint8_t> data, const BigEntry& e) {
    if (e.offset > data.size()) return {};
    const std::size_t n = std::min<std::size_t>(e.size, data.size() - e.offset);
    return data.subspan(e.offset, n);
}

// Class letter from the PT v2 header tag 0x06 ('c' PHRS/PLRS/TEAM, 'a' CHNT — §1 codec split).
char classLetter(std::span<const std::uint8_t> member) {
    if (member.size() < 24 || member[20] != 'P' || member[21] != 'T') return '?';
    try {
        const auto h = PtHeader::parse(member, 20);
        if (const auto v = h.tag(0x06)) return char(*v & 0xFF);
    } catch (const FormatError&) {
    }
    return '?';
}

} // namespace

PhraseBank PhraseBank::parse(std::span<const std::uint8_t> data, bool tolerant) {
    const BigArchive outer = BigArchive::parse(data, tolerant);
    PhraseBank b;
    for (std::uint32_t p = 0; p < outer.entries.size(); ++p) {
        const auto rawView = safeView(data, outer.entries[p]);
        b.members_.push_back(rawView);
        std::uint8_t flag = 0;
        // RefPack-compressed members decode transparently (mirrors the test_audio PBK walk).
        const auto member = refpack::decompressIfNeeded(rawView);
        const std::span<const std::uint8_t> m{member};
        if (BigArchive::isBig(m)) {
            // Nested per-phrase archive: segments, each = 20-byte record + payload. Strict mode
            // throws on TYPE.PBK stubs (offsets past member end); tolerant clamps them away.
            const BigArchive nested = BigArchive::parse(m, tolerant);
            flag = nested.entries.empty() ? 0 : 1; // count=0, hdrSize=16 -> silent phrase (§2)
            for (std::uint32_t s = 0; s < nested.entries.size(); ++s) {
                const auto seg = safeView(m, nested.entries[s]);
                if (seg.size() < 20) continue; // stripped mirror: no record bytes kept in the stub
                PhraseRecord r;
                r.index = std::uint32_t(b.records_.size());
                r.phrase = p;
                r.segment = s;
                r.size = nested.entries[s].size;
                r.classLetter = classLetter(seg);
                std::copy_n(seg.begin(), 20, r.raw.begin());
                r.id = std::uint16_t(r.raw[0x12] | (r.raw[0x13] << 8));
                b.records_.push_back(r);
            }
        } else if (m.size() >= 24 && m[20] == 'P' && m[21] == 'T') {
            // Flat play-track member: PLRS/TEAM/CHNT banks (class-only record, variant = ordinal — §1).
            flag = 1;
            PhraseRecord r;
            r.index = std::uint32_t(b.records_.size());
            r.phrase = p;
            r.size = outer.entries[p].size;
            r.classLetter = classLetter(m);
            std::copy_n(m.begin(), 20, r.raw.begin());
            r.id = std::uint16_t(r.raw[0x12] | (r.raw[0x13] << 8));
            b.records_.push_back(r);
        } else if (m.size() >= 20) {
            // Flat non-PT member: CRWD/COMOS MIDx streams, CROWD raw codec blocks (§4). Record kept.
            flag = 1;
            PhraseRecord r;
            r.index = std::uint32_t(b.records_.size());
            r.phrase = p;
            r.size = outer.entries[p].size;
            std::copy_n(m.begin(), 20, r.raw.begin());
            r.id = std::uint16_t(r.raw[0x12] | (r.raw[0x13] << 8));
            b.records_.push_back(r);
        }
        // else: sub-20-byte member — no record; phrase flagged empty.
        b.flags_.push_back(flag);
    }
    return b;
}

std::uint32_t countMirrorPrefixMatches(const PhraseBank& stub, const PhraseBank& full) {
    if (stub.phraseCount() != full.phraseCount()) return 0;
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < stub.phraseCount(); ++i) {
        const auto s = stub.memberBytes(i), f = full.memberBytes(i);
        if (s.size() > f.size() || !std::equal(s.begin(), s.end(), f.begin())) break;
        ++n;
    }
    return n;
}

} // namespace wc98::formats
