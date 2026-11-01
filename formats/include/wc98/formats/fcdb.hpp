// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// World Cup 98 team/player database: FCDB*.DBI ("FCDB" records, "STR0"/"STR1" name
// tables) and FC<lang>.BIN ("FCLG" UI string table).
//
// WC98 schema deltas (research-inbox/F99_REUSABILITY_MATRIX.md section 5 + 12.1):
//   * FCDB header, STR0/STR1 name tables, the league table and the 20-byte player
//     records are byte-compatible with FIFA 99 (only the entity counts differ:
//     24 leagues / 56 teams / 1782 players).
//   * the TEAM record is 68 B (17 u32 words) with NO budget field and the roster
//     u16 list at byte 24 (FIFA 99: 80 B / 20 words / roster base byte 28);
//     record size decides the schema, see FcdbSchema.
//   * the 1998 team-record bit layout (kit styles + colours, bank, stadium,
//     tactics, strategy, nation, roster size) is from the community editor
//     fifa98edit (analysis/fifa98edit, MIT); player skill nibbles are on a
//     39..99 scale in steps of 4.
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wc98::formats {

// The 17 player attribute nibbles (0..15) in record order. Names come from the EXE's
// {index, long string id, short string id} table (file offset 0x184640 in the rip FIFA99.EXE);
// slots 2, 3, 8, 9, 15, 16 are not in that table and stay unnamed (see FCDB.md).
inline constexpr std::array<std::string_view, 17> kFcdbAttributeNames = {
    "acceleration", "agility", "hidden2", "hidden3", "ballControl", "creativity", "fitness", "headingAccuracy",
    "hidden8", "hidden9", "shotPower", "shotAccuracy", "speed", "tackling", "aggression", "hidden15", "hidden16"};

// WC98 (1998) skill nibbles in record order (bytes 8..15 hi/lo, tackle = low nibble of
// byte 16). Names + nibble order from fifa98edit; every nibble n encodes the game value
// n*4+39, i.e. the 1998 range is 39..99 in steps of 4.
inline constexpr std::array<std::string_view, 17> kFcdbAttributeNames1998 = {
    "aggression",   "acceleration", "attackBias",  "agility",     "ballControl",  "awareness",  "fitness",
    "creativity",   "passing",      "heading",     "reaction",    "passBias",     "shotPower",  "shotBias",
    "speed",        "shotAccuracy", "tackle"};

[[nodiscard]] constexpr int wc98SkillValue(std::uint8_t nibble) { return nibble * 4 + 39; }

// Which FCDB record layout a file uses; FcdbDatabase::parse detects it from the
// team-table record size (68 = WC98, 80 = FIFA 99).
enum class FcdbSchema { WorldCup98, Fifa99 };

// A 1998 kit set (shirt/shorts/socks style + palette colour indices), from the
// bit-packed prefix of the 68-byte WC98 team record.
struct FcdbKit1998 {
    std::uint8_t shirtStyle = 0;
    std::uint8_t shirtColor1 = 0, shirtColor2 = 0, shirtColor3 = 0;
    std::uint8_t shortsStyle = 0;
    std::uint8_t shortsColor1 = 0, shortsColor2 = 0;
    std::uint8_t socksStyle = 0;
    std::uint8_t socksColor1 = 0, socksColor2 = 0;
};

struct FcdbLeague {
    std::uint16_t id = 0;     // bits 0..11
    std::uint16_t field12 = 0; // bits 12..23, meaning unknown
    bool flag24 = false, flag25 = false;
    std::vector<std::uint16_t> teamIds;
};

struct FcdbTeam {
    std::uint16_t id = 0;         // FIFA99: t0 bits 0..10 | WC98: bytes 0..1 (w0 bits 0..10)
    std::uint32_t budget = 0;     // FIFA 99 only (t0 bits 11..27); WC98 has no budget field
    bool isClub = false;          // FIFA 99 t4 bit 30; WC98 leaves this unset (use nation)
    std::uint8_t nationality = 0; // FIFA 99 t5 bits 20..27 | WC98: bytes 22..23 nation code
    std::uint16_t rivalTeamId = 0; // FIFA 99 t6 bits 11..21 (unused in the 1998 schema)
    std::vector<std::uint16_t> roster; // player ids (WC98: 20 u16 BE slots at byte 24, 0 = empty)
    std::array<std::uint32_t, 20> raw{}; // FIFA 99 words; WC98 fills the first 17

    // --- WC98 (1998 schema) team fields, from fifa98edit ---
    std::uint32_t bank = 0;      // bytes 1..3: audio sample bank of the team
    std::uint8_t stadium = 0;    // byte 3 high nibble: stadium index
    std::uint8_t tactics = 0;    // byte 21 >> 2
    std::uint8_t strategy = 0;   // byte 17 >> 5
    std::uint8_t rosterSize = 0; // byte 22 low nibble * 2
    FcdbKit1998 home, away;      // kit sets (bytes 4..19)
};

struct FcdbPlayer {
    std::uint16_t id = 0;          // w0 bits 0..13 (ascending)
    std::uint16_t nationality = 0; // w0 bits 14..24
    std::uint8_t overall = 0;      // WC98: mean of the 17 skill values (39..99); FIFA99 w0 bits 25..31
    std::uint16_t value = 0;       // w1 bits 0..15 (WC98: transfer price)
    std::uint8_t position = 0;     // w4 bits 15..19 (0 = goalkeeper)
    std::uint8_t position2 = 0;    // w4 bits 10..14 (alternative position, selected by the team's club flag)
    std::uint8_t shirtNumber = 0;  // FIFA99 w4 bits 4..9 | WC98: 8-bit jersey from bytes 16..17
    std::array<std::uint8_t, 17> attributes{}; // skill nibbles 0..15; scale per schema
    std::array<std::uint32_t, 5> raw{};

    // --- WC98 (1998 schema) player fields, from fifa98edit ---
    std::uint8_t role = 0;         // byte 17 high nibble: player role index
    std::uint8_t hairStyle = 0;    // byte 6 high nibble
    std::uint8_t hairColor = 0;    // byte 6 bits 2..3
    std::uint8_t skinTone = 0;     // byte 6 bits 0..1
    std::uint8_t faceStyle = 0;    // byte 7 high 5 bits
    std::uint8_t beardStyle = 0;   // byte 7 low 3 bits
    std::uint8_t startingSlot = 0; // byte 3: starting/formation slot
    std::uint32_t price = 0;       // bytes 4..5 (LE): transfer price
};

struct FcdbDatabase {
    std::vector<FcdbLeague> leagues;
    std::vector<FcdbTeam> teams;
    std::vector<FcdbPlayer> players;
    FcdbSchema schema = FcdbSchema::WorldCup98; // set by parse() from the team record size
    // FCDB.DBI; throws FormatError. Schema auto-detects from the team record size.
    static FcdbDatabase parse(std::span<const std::uint8_t> data, FcdbSchema schema = FcdbSchema::WorldCup98);
};

struct FcdbTeamName {
    std::string abbreviation, shortName, fullName;
};

// STR0 (FCDB_<lang>.DBI: league + team names) or STR1 (FCDBP<lang>.DBI: player names).
// Entries are in the same order as the corresponding FCDB tables.
struct FcdbNames {
    std::string magic; // "STR0" / "STR1"
    std::vector<std::string> leagues;
    std::vector<FcdbTeamName> teams;
    std::vector<std::string> players;
    static FcdbNames parse(std::span<const std::uint8_t> data); // throws FormatError
};

// FC<lang>.BIN: NUL-separated cp1252 strings; string id = index (id 0 is the "FCLG" magic itself).
struct FclgStrings {
    std::vector<std::string> strings;
    static FclgStrings parse(std::span<const std::uint8_t> data); // throws FormatError
};

// Recognises FCDB / STR0 / STR1 DBI files (all start with a 4-char magic and version 501).
[[nodiscard]] bool isDbi(std::span<const std::uint8_t> data);

// cp1252 -> UTF-8 (for JSON/CSV export of database and UI strings).
std::string cp1252ToUtf8(std::string_view s);

} // namespace wc98::formats
