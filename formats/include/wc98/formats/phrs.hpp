// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// WC98 phrase-bank (.PBK speech) record tables. Every speech member is a 20-byte phrase
// record followed by its payload; PHRS.PBK phrases nest second-level BIG archives of segments,
// TYPE.PBK is the byte-exact stripped mirror (header-only twin), CRWD banks carry flat
// MIDx/raw members. Measured layout: docs/reverse-engineering/SPEECH_INDEXING_NOTES.md §1-§4;
// API + queue mapping: docs/asset-formats/SPEECH_PHRASES.md.
#include "wc98/formats/big.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {

struct PhraseRecord {
    std::uint32_t index = 0;   // ordinal in the bank record table (TOC order)
    std::uint32_t phrase = 0;  // top-level member ("phrase") that owns this segment
    std::uint32_t segment = 0; // ordinal inside the nested archive; 0 for flat banks
    std::uint32_t size = 0;    // member bytes (record + payload); clamped view in tolerant mirrors
    char classLetter = '?';    // PT header tag 0x06 letter: 'c' PHRS/PLRS/TEAM, 'a' CHNT, '?' non-PT payload
    std::uint16_t id = 0;      // u16le at record+0x12: global sound-segment handle (consumed by the pump)
    std::array<std::uint8_t, 20> raw{}; // full per-member phrase record (§1 field census)

    [[nodiscard]] std::uint32_t packed() const { return raw[4] | (std::uint32_t(raw[5]) << 8); } // u16le @+0x04
};

class PhraseBank {
public:
    // Parses the bank's record table. PHRS/PLRS/TEAM/CHNT/CRWD parse strict; TYPE.PBK
    // (stripped mirror whose nested TOC offsets run past member ends) needs tolerant=true.
    // Throws FormatError (strict mode). `data` must outlive this object and its spans.
    static PhraseBank parse(std::span<const std::uint8_t> data, bool tolerant = false);

    [[nodiscard]] std::uint32_t phraseCount() const { return flags_.size(); } // top-level BIG members
    [[nodiscard]] std::uint32_t recordCount() const { return records_.size(); } // 20-byte records (segments)
    [[nodiscard]] const PhraseRecord& record(std::uint32_t i) const { return records_.at(i); }

    // Nested BIG archive declaring count=0 (16-byte members in PHRS/TYPE): silent phrase, no segments.
    [[nodiscard]] bool emptyPhrase(std::uint32_t p) const { return flags_.at(p) == 0; }
    // Raw (pre-RefPack) top-level member view, for byte-exact mirror comparison against the full sibling bank.
    [[nodiscard]] std::span<const std::uint8_t> memberBytes(std::uint32_t p) const { return members_.at(p); }

private:
    std::vector<std::span<const std::uint8_t>> members_; // raw payload views into the caller's data
    std::vector<std::uint8_t> flags_;                    // 0 = empty nested archive, 1 = has content
    std::vector<PhraseRecord> records_;                  // record table in (phrase, segment) TOC order
};

// TYPE.PBK == PHRS.PBK[:len(TYPE[j])] for all j (§2, measured byte-exact on 1,290/1,290).
// Returns the number of leading top-level members whose stub bytes are an exact prefix of the
// full bank's (phraseCount must match, else 0). `stub` must come from a tolerant parse.
std::uint32_t countMirrorPrefixMatches(const PhraseBank& stub, const PhraseBank& full);

} // namespace wc98::formats
