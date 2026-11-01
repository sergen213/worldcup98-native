// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Front-end screen definitions: LayMagic .LAY layouts, artist .VIS and code .INC extensions
// (INI-like text, merged per object) and the FEMSG.BIN message-symbol table.
// See docs/formats/LAYOUT.md for grammar, key inventory, semantics and EXE evidence.
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wc98::formats {

// ---- Raw INI-like document -----------------------------------------------------------------

struct LayoutProperty {
    std::string key;      // trimmed, original case (lookups are case-insensitive)
    std::string value;    // trimmed, inline ';' comment removed, surrounding quotes removed
    std::string platform; // "" (all), "PC", "PSX", ... from a "{PC}" prefix
    int line = 0;
};

struct LayoutSection {
    std::string name;     // text between '[' and ']'
    std::string platform; // "{PSX}[LIST]": the whole section applies to one platform only
    int line = 0;
    std::vector<LayoutProperty> props;
};

struct LayoutDocument {
    std::string header;                  // first line when it is a ';' comment (";LAYOUT filename: ...")
    std::vector<LayoutProperty> preamble; // key=value lines before the first section (ignored by the game)
    std::vector<LayoutSection> sections;
    std::vector<std::string> warnings;   // unparsable lines (skipped), unterminated section headers

    // Never throws: the game's tokenizer skips what it cannot read, so does this parser.
    static LayoutDocument parse(std::string_view text);
    static LayoutDocument parse(std::span<const std::uint8_t> data);
    // First section with this name (case-insensitive) or nullptr.
    [[nodiscard]] const LayoutSection* section(std::string_view name) const;
};

[[nodiscard]] bool iequals(std::string_view a, std::string_view b);

// ---- FEMSG.BIN ------------------------------------------------------------------------------

// 72-byte records {u8 kind=1, char name[67], i32 value}: SM_* screen messages, OM_*/OM<class>_*
// object messages, FEJ_* input masks, FESFX_* sound ids. Loose CD copies end in an 8-byte "\0cRC" trailer.
struct FemsgSymbol {
    std::string name;
    std::int32_t value = 0;
};

struct FemsgTable {
    std::vector<FemsgSymbol> symbols;
    static FemsgTable parse(std::span<const std::uint8_t> data); // throws FormatError
    [[nodiscard]] std::optional<std::int32_t> value(std::string_view name) const; // case-insensitive
};

// ---- Typed screen model ---------------------------------------------------------------------

// objecttype values, from the class registrations at EXE 0x488000 (see LAYOUT.md for addresses).
enum class LayoutObjectType : int {
    Image = 0, Text = 1, Button = 2, Type3 = 3, List = 4, Bar = 5, ScrollBar = 6, Animation = 7, AnimButton = 8,
    Slider = 9, CheckBox = 10, FilledRect = 12, FrameRect = 13, Edit = 14, Formation = 15, Bracket = 17, Video = 18,
    Type19 = 19, Type20 = 20, Chat = 21, FadeText = 22,
};
[[nodiscard]] const char* layoutObjectTypeName(int type); // "" when the EXE registers no class for it

// "Message = trigger, triggerParam, keyMask, target, send, param1, param2[, flag[, sfx]]" (EXE 0x488104).
struct LayoutMessage {
    std::string trigger;  // OM_* message received by the object (or SM_* by the screen)
    std::string triggerParam;
    std::string keyMask;  // FEJ_* (optionally '-' prefixed) or a number
    std::string target;   // 0 = screen, "this", an object name or a GROUPID
    std::string send;     // message sent to the target
    std::string param1, param2;
    std::string flag;     // optional 8th field ("none", 0, 1)
    std::string sfx;      // optional 9th field: FESFX_*
    std::string platform;
};

// One merged object: LAY [object N] keys, then the INC section with the same name, then the VIS section.
struct LayoutObject {
    int index = 0;  // N of [object N]
    int type = -1;  // integer value of the section's first key (normally "objecttype")
    std::string name;
    int x = 0, y = 0, z = 0, width = 0, height = 0;
    std::string fname, sname;
    std::map<std::string, std::string> props; // all applicable scalar keys, UPPER-CASE, later files override
    std::vector<LayoutMessage> messages;      // accumulated in LAY, INC, VIS order

    [[nodiscard]] const std::string* prop(std::string_view key) const; // case-insensitive
    [[nodiscard]] int intProp(std::string_view key, int fallback) const;
    [[nodiscard]] bool shown() const { return intProp("SHOW", 1) != 0; }
};

struct ScreenLayout {
    std::string name;
    bool isPopup = false;
    std::string hilitePane;               // object initially highlighted
    std::map<std::string, std::string> screenProps; // [screen] / [scrn] keys, UPPER-CASE
    std::vector<LayoutMessage> screenMessages;
    std::vector<LayoutObject> objects;    // LAY order
    std::vector<std::string> warnings;    // missing sections, numobjects mismatch, ...

    // Merges the three documents for `platform` ("PC": keys with no prefix or {PC}). inc/vis may be null.
    static ScreenLayout build(std::string_view name, const LayoutDocument& lay, const LayoutDocument* inc,
                              const LayoutDocument* vis, std::string_view platform = "PC");
    [[nodiscard]] const LayoutObject* object(std::string_view name) const; // case-insensitive
};

// Splits a Message value into fields (comma separated, trimmed). Returns nullopt when < 7 fields.
std::optional<LayoutMessage> parseLayoutMessage(std::string_view value);

} // namespace wc98::formats
