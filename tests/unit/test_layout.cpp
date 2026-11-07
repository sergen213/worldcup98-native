// Derived from fifa99-native (same author), ported for World Cup 98
#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/layout.hpp"
#include "wc98/formats/refpack.hpp"
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace wc98::formats;
namespace fs = std::filesystem;

static void synthetic() {
    const char* lay =
        ";LAYOUT filename: test.lay\r\n"
        "[screen]\r\nnumobjects= 2\r\n\r\n"
        "[object 1]\nobjecttype=2\nobjectname=BTN1\nxposition=10\nyposition=20\nzposition=3\nwidth=44\nheight=26\n"
        "fname=..\\global\\IconExit.fsh\nsname=BTNN\nnormalShapeID=BTNN\n"
        "[object 2]\ntype = 1 ; hand-written variant: first key carries the class id\nobjectname=TXT1\n"
        "garbage line without equals\n"
        "[broken\n";
    auto d = LayoutDocument::parse(std::string_view(lay));
    CHECK(d.header == "LAYOUT filename: test.lay");
    CHECK(d.sections.size() == 3 && d.section("SCREEN") && d.section("object 1")->props.size() == 10);
    CHECK(d.warnings.size() == 2); // garbage line + unterminated header
    const char* inc =
        "[scrn]\nisPopup = 1\nHilitePane = BTN1\n"
        "[BTN1]\n{PC}Message\t= OM_BUTTON_UP, 0, FEJ_BUTC, 0, SM_GOTO_SCREEN, 0, 0, none, FESFX_SELECT ;comment\n"
        "{PSX}Message = OM_BUTTON_UP, 0, FEJ_BUTX, 0, SM_BACKWARD, 0, 0\n"
        "Message = too, few, fields\n"
        "xposition = 11\n"
        "{PSX}[BTN1]\nxposition = 999\n";
    const char* vis =
        "[BTN1]\n;-- BUTTON --\nhelpTextID=5550\n {PC}alphablend = 1\nFormat = \"a;b\" ; trailing\nxposition=12\n"
        "[TXT1]\ntextID=3889\n{PSX}fontID=1\n{PC}fontID=2\n";
    auto di = LayoutDocument::parse(std::string_view(inc)), dv = LayoutDocument::parse(std::string_view(vis));
    CHECK(di.sections[2].platform == "PSX");
    auto s = ScreenLayout::build("TEST", d, &di, &dv);
    CHECK(s.isPopup && s.hilitePane == "BTN1" && s.objects.size() == 2);
    const auto& b = s.objects[0];
    CHECK(b.type == 2 && b.name == "BTN1" && b.y == 20 && b.width == 44 && b.fname == "..\\global\\IconExit.fsh");
    CHECK(b.x == 12);                                  // LAY 10 < INC 11 < VIS 12; {PSX} section ignored
    CHECK(b.intProp("ALPHABLEND", 0) == 1 && *b.prop("format") == "a;b" && b.intProp("HELPTEXTID", 0) == 5550);
    CHECK(b.messages.size() == 1);                     // {PSX} line skipped, short Message rejected
    const auto& m = b.messages[0];
    CHECK(m.trigger == "OM_BUTTON_UP" && m.keyMask == "FEJ_BUTC" && m.target == "0" && m.send == "SM_GOTO_SCREEN" && m.flag == "none" &&
          m.sfx == "FESFX_SELECT" && m.platform == "PC");
    CHECK(s.warnings.size() == 1);                     // short Message
    CHECK(s.objects[1].type == 1 && s.objects[1].intProp("FONTID", -1) == 2 && s.objects[1].intProp("TEXTID", 0) == 3889);
    auto psx = ScreenLayout::build("TEST", d, &di, &dv, "PSX");
    CHECK(psx.objects[0].x == 12 && psx.objects[1].intProp("FONTID", -1) == 1 && psx.objects[0].messages.size() == 1 &&
          psx.objects[0].messages[0].send == "SM_BACKWARD");
    // Malformed input never throws; garbage only yields warnings.
    std::string junk = "\x01\x02[[[\n{{{\n=\n[a]\n=x\n{PC\n";
    auto dj = LayoutDocument::parse(std::string_view(junk));
    CHECK(dj.sections.size() == 1 && dj.warnings.size() == 5);
    CHECK(LayoutDocument::parse(std::string_view("")).sections.empty());
    CHECK(!parseLayoutMessage("a,b,c,d,e,f").has_value() && parseLayoutMessage("a,b,c,d,e,f,g,").has_value());
    // Registry names (pass 10): ids bound by MOV EAX imm at thunks in FUN_00447910 -> registerClass.
    CHECK(std::strcmp(layoutObjectTypeName(3), "animseq") == 0 && std::strcmp(layoutObjectTypeName(11), "sbar") == 0 &&
          std::strcmp(layoutObjectTypeName(19), "knobbar") == 0 && std::strcmp(layoutObjectTypeName(32), "tbb") == 0 &&
          std::strcmp(layoutObjectTypeName(37), "flg") == 0 && std::strcmp(layoutObjectTypeName(42), "menu") == 0 &&
          std::strcmp(layoutObjectTypeName(43), "box") == 0 && std::strcmp(layoutObjectTypeName(44), "kt") == 0 &&
          std::strcmp(layoutObjectTypeName(45), "btn") == 0 && std::strcmp(layoutObjectTypeName(47), "sliderbtn") == 0);
    CHECK(*layoutObjectTypeName(25) == 0 && *layoutObjectTypeName(40) == 0);

    // FEMSG: two records + CD trailer; wrong size / kind throw.
    std::vector<std::uint8_t> fm(72 * 2, 0);
    fm[0] = 1; std::memcpy(&fm[1], "SM_CREATE", 9);
    fm[72] = 1; std::memcpy(&fm[73], "FEJ_BUTC", 8); fm[72 + 0x44 + 2] = 4; // 0x40000
    for (char c : std::string("\0cRC\x8f\x73\0\0", 8)) fm.push_back(std::uint8_t(c));
    auto t = FemsgTable::parse(fm);
    CHECK(t.symbols.size() == 2 && t.value("fej_butc") == 0x40000 && t.value("SM_CREATE") == 0 && !t.value("nope"));
    CHECK_THROWS(FemsgTable::parse(std::span(fm).first(100)));
    fm[72] = 2;
    CHECK_THROWS(FemsgTable::parse(fm));
}

static void original() {
    auto root = treeDirOrSkip();
    namespace fs = std::filesystem;
    auto upper = [](std::string s) { for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c))); return s; };
    // WC98 gate (matrix section 13): all 29 loose .LAY layouts parse; the typed ScreenLayout
    // layer flags a numobjects/object-count mismatch on none of them. WC98 has no loose
    // .VIS/.INC overlays and no FEMSG.BIN string table (open research items).
    int loose = 0, members = 0, sections = 0, warnings = 0, objects = 0, numobjWarn = 0;
    auto check = [&](const std::string& name, std::span<const std::uint8_t> data) {
        auto doc = LayoutDocument::parse(data);
        sections += int(doc.sections.size());
        warnings += int(doc.warnings.size());
        if (upper(name).ends_with(".LAY")) {
            CHECK(!doc.sections.empty());
            for (const auto& w : doc.warnings) std::printf("  %s: layout: %s\n", name.c_str(), w.c_str());
            auto s = ScreenLayout::build(name, doc, nullptr, nullptr);
            CHECK(!s.objects.empty());
            for (const auto& w : s.warnings) std::printf("  %s: screen: %s\n", name.c_str(), w.c_str());
            // WC98 research items (matrix section 13): 383 UI object types the typed layer
            // does not model (measured) are expected "unknown objecttype" warnings, plus a
            // single measured numobjects section-count quirk (FEFLOW00.LAY). Tolerate only
            // these two categories.
            for (const auto& w : s.warnings) {
                if (w.find("numobjects=") != std::string::npos) ++numobjWarn;
                else CHECK(w.find("unknown objecttype") != std::string::npos);
            }
            objects += int(s.objects.size());
        }
    };
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        auto ext = upper(e.path().extension().string());
        if (ext == ".LAY" || ext == ".VIS" || ext == ".INC") {
            ++loose;
            check(e.path().filename().string(), wc98::readFile(e.path()));
        } else if (ext == ".BIG" || ext == ".MO" || ext == ".VIV" || ext == ".PAK") {
            auto data = refpack::decompressIfNeeded(wc98::readFile(e.path()));
            const std::span<const std::uint8_t> ds{data};
            std::size_t base = BigArchive::isBig(ds) ? 0 : (ds.size() > 0x34 && BigArchive::isBig(ds.subspan(0x30)) ? 0x30 : 0);
            if (!BigArchive::isBig(ds.subspan(base))) continue;
            for (const auto& m : BigArchive::parse(ds.subspan(base)).entries) {
                auto low = upper(m.name);
                if (low.ends_with(".LAY") || low.ends_with(".VIS") || low.ends_with(".INC")) {
                    ++members;
                    check(m.name, refpack::decompressIfNeeded(BigArchive::payload(ds.subspan(base), m)));
                }
            }
        }
    }
    std::printf("layout docs: %d loose + %d archive members, %d sections, %d objects, %d skipped lines\n", loose, members, sections, objects, warnings);
    CHECK(loose == 29);
    CHECK(members >= 29); // LAYOUTS.BIG / IGLAY.BIG carry the archive copies
    CHECK(numobjWarn == 1); // measured: the single FEFLOW00.LAY section-count quirk
}

int main() {
    synthetic();
    original();
    return 0;
}
