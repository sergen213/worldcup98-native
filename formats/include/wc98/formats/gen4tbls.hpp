// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// World Cup 98 AISCRIPT formation tables (BIGF archive of G98*.FMT, e.g.
// COMMON/GEN4TBLS.VIV). FIFA 99's G99* set shares the KO/PL/PK/FK byte layouts
// (research-inbox/F99_REUSABILITY_MATRIX.md section 11). See docs/formats/GEN4TBLS.md.
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace wc98::formats {

inline constexpr int kFormationSlots = 17, kFormationZones = 299;

struct Gen4Tables {
    std::string header;                         // G98HD / G99HD bytes ("400\r\n")
    std::string prefix = "G98";                 // member-name prefix found in the archive
    std::array<std::int8_t, 17 * 4> ko{};       // [slot][x0,z0,x1,z1]
    std::array<std::int8_t, 17 * 299 * 4> pl{}; // [slot][zone][ax,az,dx,dz]
    std::array<std::int16_t, 17 * 4> pk{};      // [slot][x0,z0,x1,z1]
    std::array<std::int8_t, 17 * 299 * 4> fk{}; // [slot][zone][variant][x,z]
    std::array<std::int32_t, 25> at{};          // [mentality*5 + roleClass] (FIFA 99 only)
    std::array<std::int32_t, 5> df{};           // [mentality] (FIFA 99 only)
    bool haveAtDf = false;                      // WC98 archives carry no AT/DF members
    // Whole-file parse; throws FormatError on a missing member or wrong member size.
    static Gen4Tables parse(std::span<const std::uint8_t> data);
};

// WC98 (matrix section 12.1): the G98*.FMT members of COMMON/GEN4TBLS.VIV are not FIFA 99's
// row-oriented 67-byte format records (Gen4Tables::parse above). They are column-oriented
// delta tables keyed by the 17 team attributes: member size = 17 * columns * cellSize
// (G98KO.FMT 68 B = 17 x 4 x i8, G98PK.FMT 136 B = 17 x 8 x i8, G98PL.FMT / G98FK.FMT
// 20332 B = 17 x 598 x i16be). The i16 reading (2 bytes per cell) is chosen when the row
// length is even and >= 256 bytes; the row-major ordering and the 17-attribute keying were
// calibrated against those four members by the tool run in the port verification log.
// The values are signed deltas, not absolute ratings; the generator that consumes them is
// still reverse engineering work, so nothing here claims to reproduce ratings.
struct Gen4FmtTable {
    static constexpr std::size_t kAttributes = 17; // FCLG team attribute count (WC98, matrix 12)

    std::string name;          // member name as stored ("G98KO.FMT", ...)
    std::size_t attributes = 0;
    std::size_t columns = 0;   // entries per attribute row
    bool sixteenBit = false;   // cells read as big-endian i16 instead of i8
    std::vector<std::int16_t> cells; // row-major: attributes x columns signed deltas

    // Column names for the 1998 attribute data: the family's 17 attribute columns
    // (fifa98edit, MIT: https://github.com/ma-akad/fifa98edit).
    [[nodiscard]] static std::array<const char*, kAttributes> attributeNames();

    [[nodiscard]] static bool isFmtMember(std::string_view memberName);
    [[nodiscard]] static Gen4FmtTable parse(std::span<const std::uint8_t> data, std::string_view memberName);

    [[nodiscard]] std::span<const std::int16_t> row(std::size_t attribute) const {
        if (attribute >= attributes) return {};
        return std::span<const std::int16_t>(cells).subspan(attribute * columns, columns);
    }
};

} // namespace wc98::formats
