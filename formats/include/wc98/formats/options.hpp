// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// USER/OPTIONS.SAV — WC98 option block. The file is a raw image of the live DGROUP block at
// DAT_0060cba8 (FIFAWC.EXE): magic 'FIFA' twice, language byte validated in [0,8), then the
// option fields. Layout + consumer map: docs/reverse-engineering/OPTIONS_SAV_WC98.md (pass 7).
// Byte-level label semantics are a documented hypothesis pending the M7 Wine oracle; this parser
// is round-trip safe for every byte it does not name, so ground truth can only widen accessors.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {

inline constexpr std::uint32_t kOptionsMagic = 0x46494641u; // 'FIFA' LE, twice at +0/+4
inline constexpr std::size_t kOptionsLangOffset = 0x23;      // DAT_0060cbcb, validated [0,8)
inline constexpr std::size_t kOptionsBlockSize = 0x84;       // observed live extent cb a8..cc2c

struct OptionsSave {
    // Named fields are the pass-7 consumer clusters (hypothesis-tagged in the doc). All offsets
    // are block-relative LE. Fields not named here stay reachable through `raw`.
    std::uint16_t stadium = 0;       // +0x08 (stadium preview/apply cluster, low word)
    std::uint8_t dayNight = 0;       // +0x0a (day/night analogue of FIFA99 +22)
    std::uint32_t gameSpeed = 0;     // +0x14 (mode machine / renderer apply)
    std::uint32_t detailFields = 0;  // +0x18 (detail slider pair, option-screen widgets)
    std::uint32_t detailStadium = 0; // +0x1c
    std::uint32_t halfLength = 0;    // +0x24 (camera/game fn cluster)
    std::uint32_t clockStyle = 0;    // +0x28
    std::uint32_t volumeBlock = 0;   // +0x60 (volume fader refs, FUN_0046b990/46b9d4)
    std::array<std::uint32_t, 6> cameraModes{}; // +0x64..+0x78 (camera-mode per view type)
    std::uint8_t lang = 0;           // +0x23; loader range-checks [0,8), defaults on failure

    std::vector<std::uint8_t> raw;   // full block image; serialize() returns it verbatim

    // Throws FormatError if size < kOptionsBlockSize or either magic word mismatches.
    static OptionsSave parse(std::span<const std::uint8_t> data);
    [[nodiscard]] std::vector<std::uint8_t> serialize() const; // == raw (round-trip identity)

    // Full-default block written by FUN_0046ae8c when the load check fails: magic pair + language,
    // rest zero. Non-magic defaults live in code clusters not yet captured (doc §Defaults); the
    // Wine oracle widens this image — until then it is the provably-safe subset.
    static OptionsSave defaults(std::uint8_t lang = 0);

    // Re-encode named fields into raw after mutation (raw stays authoritative).
    void syncToRaw();
    void syncFromRaw();
};

} // namespace wc98::formats
