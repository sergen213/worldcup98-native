// FE flow implementation — see fe_flow.hpp for the design notes.
#include "fe_flow.hpp"

#include "wc98/presentation/hud.hpp"
#include "wc98/tournament/tournament.hpp"
#include "wc98/file.hpp"
#include "wc98/renderer/renderer.hpp"

#include "saveload.hpp"                    // USER/SLOTnn.SAV load path (Phase 3)
#include "wc98/config/settings.hpp"        // settings.ini [audio] volume rows (Phase 2)
#include "wc98/audio/mixer.hpp"            // live master-gain updates from the options screen
#include "wc98/formats/slot.hpp"           // slotFileName()

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <charconv>   // row-tuple parsing (parseOptionRows)
#include <cctype>
#include <format>
#include <span>
#include <array>
#include <random>

namespace wc98::app {
namespace fs = std::filesystem;
using presentation::FeDrawStats;
using presentation::ScreenArt;
// Hub cursor domain: the 13 button widgets of Feflow06.lay in LAY order (objects 6..19). Wedge tag
// compares live in the feflow06 scene handler body (fe11 @0x449e29, fe07 @0x449e30, fe32 @0x449ce5,
// fe33 @0x449d4e, fe90 @0x449c4d/0x449d92, repl @0x449c01, cred @0x449c11); keyboard payload codes
// {2..9,0x1E} (hub FUN_00445664, MENU_NAV_WC98.md §3) route to the same wedge tags.
constexpr std::array<const char*, 13> kHubButtons{"fe33", "fe90", "fe32", "fe11", "fe07", "fe04", "fe02",
                                                  "quit", "fe15", "fe89", "fe41", "repl", "cred"};
// Modes the port has no simulation for paint their DISABLEDSHAPE* and Enter is a documented no-op.
// fe33 (classic/world-cup history boards feflow90/91) left the disabled list with the pass-13 slice:
// hub wedge 'fe33' opens FEFLOW90 [C] SITE 0x449dc5, analysis/pass13/CLASSICS_FE33.md.
constexpr std::array<const char*, 5> kHubDisabled{"fe90", "fe04", "fe02", "fe89", "repl"};

FeFlow::FeFlow(render::Renderer& r, fs::path tree, std::string lang) : tree_(std::move(tree)), art_(r, tree_, tree_ / "FE/PACK/LAYOUTS") {
    // Main menu screen (LAY_COMPOSITOR_NOTES.md measured table: FEFLOW05 -> 26 objects -> 11 shapes).
    std::string text;
    try {
        fs::path layDir;
        text = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW05.LAY", layDir);
        const auto doc = formats::LayoutDocument::parse(std::string_view(text));
        screen_ = formats::ScreenLayout::build("FEFLOW05", doc, nullptr, nullptr, "PC");
        haveScreen_ = true;
    } catch (const std::exception& e) {
        // Menu art absent: the shell still navigates; drawCurrent reports shapes == 0.
        lastError_ = e.what();
    }
    cfgLang_ = lang; // FCDBP<lang>.DBI filename below (loadTeams)
    for (char& c : cfgLang_) c = char(std::tolower(static_cast<unsigned char>(c)));
    // Text resources for the whole session (fe_screen.hpp FeText): FFN face, FC*.BIN strings, FEMSG.
    text_ = std::make_unique<presentation::FeText>(r, tree_, std::move(lang));
    // Team-select screen (FEFLOW11.LAY = the original ZWCUP pick grid: map, flags, group cells;
    // LAY_COMPOSITOR_NOTES.md). Absent -> drawTeams keeps the flat usability list.
    try {
        fs::path gdir;
        const std::string gtext = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW11.LAY", gdir);
        gridScreen_ = formats::ScreenLayout::build("FEFLOW11", formats::LayoutDocument::parse(std::string_view(gtext)), nullptr, nullptr, "PC");
        haveGrid_ = true;
    } catch (const std::exception&) { // no grid art on this tree: usability list remains
    }
    // Friendly/practise match-setup screens (FE_MENU_SCREENS_WC98.md §5): FEFLOW09.LAY and
    // FEFLOW50.LAY, same tolerant reader as the grid. Absent -> Enter on friendly/practise keeps
    // launching directly (pre-slice behaviour).
    try {
        fs::path d;
        const std::string t9 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW09.LAY", d);
        setup09_ = formats::ScreenLayout::build("FEFLOW09", formats::LayoutDocument::parse(std::string_view(t9)), nullptr, nullptr, "PC");
        haveSetup_ = true;
    } catch (const std::exception&) { // no friendly-setup art on this tree
    }
    try {
        fs::path d;
        const std::string t50 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW50.LAY", d);
        setup50_ = formats::ScreenLayout::build("FEFLOW50", formats::LayoutDocument::parse(std::string_view(t50)), nullptr, nullptr, "PC");
        haveSetup_ = true;
    } catch (const std::exception&) { // no practise-setup art on this tree
    }
    // League Play select screen (FE_MENU_SCREENS_WC98.md §4): FEFLOW73.LAY = the league list.
    try {
        fs::path d;
        const std::string t73 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW73.LAY", d);
        league73_ = formats::ScreenLayout::build("FEFLOW73", formats::LayoutDocument::parse(std::string_view(t73)), nullptr, nullptr, "PC");
        haveLeague_ = true;
    } catch (const std::exception&) { // no league-select art on this tree: League Play launches directly
    }
    // League configuration family (MENU_PARITY.md §4 rank 7; edges_table.txt SCENEREG cluster of the
    // feflow73 scene): FEFLOW72 formation/players, 74 kit editor, 75 strategy slider, 76 aggression
    // popup, 79 attack-bias, 80 player-attributes grid. Same tolerant reader; absent -> Enter on the
    // league row keeps launching directly (pre-slice behaviour).
    {
        const char* kCfg[] = {"FEFLOW72", "FEFLOW74", "FEFLOW75", "FEFLOW76", "FEFLOW78", "FEFLOW79", "FEFLOW80"};
        formats::ScreenLayout* tgt[] = {&lg72_, &lg74_, &lg75_, &lg76_, &lg78_, &lg79_, &lg80_};
        for (int i = 0; i < 7; ++i) {
            try {
                fs::path d;
                const std::string t = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@" + kCfg[i] + ".LAY", d);
                *tgt[i] = formats::ScreenLayout::build(kCfg[i], formats::LayoutDocument::parse(std::string_view(t)), nullptr, nullptr, "PC");
                haveLeagueCfg_ = true;
            } catch (const std::exception&) { // no family art on this tree: that screen stays closed
            }
        }
    }
    if (haveLeagueCfg_) {
        // Composite whitelists per screen (scene-ownership rule, LAY_COMPOSITOR_NOTES.md): hide the
        // netplay chat widgets (chat/cha2/cha3), the animseq placeholder rows (pNN + ptop/pbot) and
        // runtime surfaces (plyr portrait, tt20 edit box). kt/menu rows keep their shape art here;
        // captions/swatches paint as overlays in drawLeagueCfg.
        // t1xx header slots are engine-composed at runtime (sprintf %s%d from FCDB tactics/strategy,
        // fourcc 't110'..'t112' @0x45af41); their static tid=0 caption = bank #0 soup ("Art Director").
        lg72Visible_ = {"back", "bnnr", "titl", "vit0", "vit1",
                        "lb00", "lb01", "lb02", "lb03",
                        "b000", "b001", "v000", "b002", "b003", "v001", "b004", "b005", "v002",
                        "tmf0", "tmf1", "tmf2", "tmf3", "gobk", "gofd", "help"};
        // defaults are other contexts' strings, e.g. quit prompts) — hidden at rest by the scene.
        lg74Visible_ = {"back", "bnnr", "titl", "sbar", "vit0", "lb00", "lb01",
                        "ab00", "ab01", "tmf0", "tmf1", "tmf2", "tmf3", "gobk", "gofd", "help"};
        lg75Visible_ = {"back", "bnnr", "titl", "vit0", "vit1",
                        "lb00", "lb01", "lb02", "lb03",
                        "s001", "tt00", "tt01", "tt03",
                        "tmf0", "tmf1", "tmf2", "tmf3", "gobk", "gofd", "help"};
        lg76Visible_ = {"canc", "alok", "0000", "s001", "help"}; // popup: 1x1 null back widget excluded
        lg78Visible_ = {"canc", "alok", "0000", "s001", "help"}; // popup twin of FEFLOW76 (attack bias)
        lg79Visible_ = {"back", "bnnr", "titl", "sbar", "s001", "tt00", "tt01", "tt03",
                        "ab00", "ab01", "vit0", "vit3", "lb00", "lb01", "lb02", "lb03",
                        "oval", "tmf0", "tmf1", "tmf2", "tmf3", "gobk", "gofd", "help"};
        // feflow80 = custom-kit grid (squad left, kit swatch columns): the tt*/ad*/at* textID label
        // families are other modes' grids (engine hides per scene; FC defaults collide with pn rows).
        lg80Visible_ = {"back", "bnnr", "titl", "cmtx", "plnm", "bpll", "bplr", "btll", "btlr",
                        "vit0", "sb00", "sb01", "cb00", "cb01",
                        "cb03", "ncml", "ncmr",
                        "bl00", "br00", "bl01", "br01", "bl02", "br02", "bl03", "br03", "bl04",
                        "br04", "bl05", "br05", "bl06", "br06",
                        "al00", "ar00", "al01", "ar01", "al02", "ar02", "al03", "ar03", "al04",
                        "ar04", "al05", "ar05", "al06", "ar06", "al07", "ar07", "al08", "ar08",
                        "al09", "ar09", "al10", "ar10", "al11", "ar11", "al12", "ar12",
                        "tmf0", "tmf1", "tmf2", "tmf3", "gobk", "gofd", "help"};
    }
    // World-cup family interactive screens (MENU_NAV_WC98.md rank-2; analysis/pass12/wcfamily_notes.txt):
    // FEFLOW18 = zones draw screen ('rand' staged shuffle of the 32 tmNN slots), FEFLOW23/24 = R16 bracket
    // boards ('strk'/'stsc' confirm/stats buttons), Feflow35.lay = group standings ('nglt'/'ngrt' mod-8
    // group repaint). Same tolerant reader; absent layouts keep the gate closed (documented no-op).
    {
        const char* kSpec[] = {"FEFLOW18.LAY", "FEFLOW23.LAY", "FEFLOW24.LAY", "Feflow35.lay"};
        static constexpr const char* kName[] = {"FEFLOW18", "FEFLOW23", "FEFLOW24", "FEFLOW35"};
        formats::ScreenLayout* tgt[] = {&wc18_, &wc23_, &wc24_, &wc35_};
        for (int i = 0; i < 4; ++i) {
            try {
                fs::path d;
                const std::string t = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@" + kSpec[i], d);
                *tgt[i] = formats::ScreenLayout::build(kName[i], formats::LayoutDocument::parse(std::string_view(t)), nullptr, nullptr, "PC");
                haveWc_ = true;
            } catch (const std::exception&) { // no family art on this tree: that screen stays closed
            }
        }
    }
    // Cheats screen (feflow95.lay; MENU_PARITY.md §8). The scene registry row is [C]
    // analysis/pass12/registry_all.txt:56 and the hub global-key payload 0x1E -> feflow95 is [C]
    // docs/reverse-engineering/MENU_NAV_WC98.md §3 (evidence 0x4456A3-0x4456A8); payloads have no
    // decoded keyboard mapping, so the port exposes a documented dev key (F9). Absent layout = lane closed.
    {
        try {
            fs::path d;
            const std::string t = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@feflow95.lay", d);
            cheats95_ = formats::ScreenLayout::build("FEFLOW95", formats::LayoutDocument::parse(std::string_view(t)), nullptr, nullptr, "PC");
            haveCheats_ = true;
        } catch (const std::exception&) { // no cheats art on this tree: the lane stays closed
        }
    }
    // World-cup history boards feflow90/91 (hub wedge fe33 -> FEFLOW90, SITE 0x449dc5; handlers +
    // dataset analysis/pass13/CLASSICS_FE33.md). Absent layouts keep the lane closed (no-op wedge).
    {
        const char* kSpec[] = {"Feflow90.lay", "Feflow91.lay"};
        const char* kName[] = {"FEFLOW90", "FEFLOW91"};
        formats::ScreenLayout* tgt[] = {&hist90_, &hist91_};
        for (int i = 0; i < 2; ++i) {
            try {
                fs::path d;
                const std::string t = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@" + kSpec[i], d);
                *tgt[i] = formats::ScreenLayout::build(kName[i], formats::LayoutDocument::parse(std::string_view(t)), nullptr, nullptr, "PC");
                haveHistory_ = true;
            } catch (const std::exception&) { // no board art on this tree: that screen stays closed
            }
        }
        if (haveHistory_) {
            // Scene-ownership whitelists: back/flag/titl + the two shield slots + nav icons + year
            // buttons + volume bars + help pane are scene art; chat/cha2/cha3/masc runtime surfaces,
            // the five null-named FC-bank stat captions (textid 429..433 = other contexts' soup) and
            // the ball-shading null widgets stay hidden. ltnm/rtnm/ltsc/rtsc/year paint as overlays.
            hist90Visible_ = {"back", "flag", "titl", "shdl", "shdr", "quit", "fe15", "fe89", "fe41",
                              "fe06", "fe91", "yslb", "ysrb", "help",
                              "rb01", "rb02", "rb03", "rb04", "rb05", "lb01", "lb02", "lb03", "lb04", "lb05"};
            hist91Visible_ = {"back", "flag", "titl", "ltfg", "rtfg", "quit", "fe06", "fe15", "fe07",
                              "fe08", "help", "ovr0", "ovr1", "ovr2", "ovr3",
                              "alp0", "alp1", "alp2", "alp3", "arp0", "arp1", "arp2", "arp3"};
        }
    }
        // Single-player setup family (IG_FAMILIES_SPEC.md §B; MENU_NAV_WC98.md feflow07/08 rows):
        // feflow07 matchup screen opens from the hub 'fe07' wedge payload 4 [C] (FUN_00445664
        // 0x4456B9-0x4456CC), feflow08 stadium select follows the friendly-config confirm via the
        // 'fe08' widget tag (@0x44C3D3/C3C8 in feflow09's handler). Key slot = default thunk
        // FUN_0044a210 (no custom tags): standard two-axis row grammar.
        fs::path d;
        const std::string t07 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW07.LAY", d);
        const std::string t08 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW08.LAY", d);
        try {
            if (!t07.empty()) {
                setup07_ = formats::ScreenLayout::build("FEFLOW07", formats::LayoutDocument::parse(std::string_view(t07)), nullptr, nullptr, "PC");
                haveSetup07_ = true;
            }
        } catch (const std::exception& e) { lastError_ = std::string("FEFLOW07: ") + e.what(); }
        try {
            if (!t08.empty()) {
                stad08_ = formats::ScreenLayout::build("FEFLOW08", formats::LayoutDocument::parse(std::string_view(t08)), nullptr, nullptr, "PC");
                haveStad08_ = true;
            }
        } catch (const std::exception& e) { lastError_ = std::string("FEFLOW08: ") + e.what(); }
        // Scene-ownership whitelists (§B-evidence widget map). Excluded on FEFLOW07: chat/cha2/cha3
        // (netplay chat surfaces), ltll/ltlr/rtlr/rtrr + the null shad shadow (invisible hit polygons),
        // shtg/pssg/rnng/dfce/ovrl (soup textids 429-433 = FC strings "Confirms selection..."/
        // "Congratulations..." of other contexts; real captions painted from FCENG 1691/1330/1741/
        // 573/1278), ltnm/rtnm paint as overlays (team names), the lbNN/rbNN bar0 quads draw full-
        // length and the painter fills them to FCDB-derived value fractions. Excluded on FEFLOW08:
        if (haveSetup07_)
            setup07Visible_ = {"back", "titl", "flag", "quit", "fe15", "fe89", "fe41", "fe28", "fe06",
                               "fe09", "ltlb", "ltrb", "rtlb", "rtrb", "shdl", "shdr", "ltnm", "rtnm",
                               "fotl", "fotr", "lb01", "lb02", "lb03", "lb04", "lb05",
                               "rb01", "rb02", "rb03", "rb04", "rb05", "help"};
        if (haveStad08_)
            stad08Visible_ = {"back", "titl", "flag", "quit", "fe06", "fe15", "fe89", "fe41", "fe07",
                              "fe16", "flgp", "nslt", "nsrt", "stme", "sint", "wint", "hint"};
    // FEFLOW43 management grid + feflow41 LOAD / feflow42 SAVE lists (MENU_PARITY.md rows 29/30, §7):
    // same tolerant reader; absent -> the lanes stay closed. FEFLOW43 is not in the exe scene registry
    // (superseded by the feflow73-80 family [H]); the port opens it as a standalone FCDB management
    // board. The LOAD/SAVE pair lives at registry rows 0x562484/0x562490 (edges_table.txt hub helper
    // sites 0x449e19 + feflow11/19 init chains).
    {
        const char* kSpec[] = {"FEFLOW43.LAY", "FEFLOW41.LAY", "FEFLOW42.LAY"};
        formats::ScreenLayout* tgt[] = {&mgmt43_, &load41_, &save42_};
        for (int i = 0; i < 3; ++i) {
            try {
                fs::path d;
                const std::string t = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@" + kSpec[i], d);
                *tgt[i] = formats::ScreenLayout::build(std::string(kSpec[i]).substr(0, 8), formats::LayoutDocument::parse(std::string_view(t)), nullptr, nullptr, "PC");
            } catch (const std::exception&) { // member absent on this tree: that lane stays closed
            }
        }
    }
    haveMgmt43_ = mgmt43_.objects.size() > 0;
    haveLoad41_ = load41_.objects.size() > 0;
    haveSave42_ = save42_.objects.size() > 0;
    if (haveMgmt43_) {
        // Scene-ownership whitelist: back collage + aflag anim + nav icons (quit/retn/fwrd) + team
        // name/arrows + the eight edit-button tiles bt00..bt07 + function buttons canc/save/rest/resa
        // (all feflow43.fsh iNNn/iNNh/iNNp art, resolved through the FE/PACK/FEFLOW43 PAK stem index)
        // + b000/b001 scroll arrows + help pane. Excluded: mmod (textid=0 mode slot), the vNNN/aNNN/
        // oNNN animseq runtime surfaces (engine-filled rows/icons/overlays), pyNN shirt slots (plyr
        // portrait tiles at x=0 y=0, engine positions them per row) and chat/cha2/cha3 netplay boxes.
        mgmt43Visible_ = {"back", "flag", "quit", "retn", "fwrd", "tmnm", "tmlt", "tmrt",
                          "bt00", "bt01", "bt02", "bt03", "bt04", "bt05", "bt06", "bt07",
                          "canc", "save", "rest", "resa", "b000", "b001", "help"};
    }
    if (haveLoad41_)
        load41Visible_ = {"blur", "bgnd", "exit", "lstb", "sbar"}; // "0000" soup caption excluded (textid 6)
    if (haveSave42_)
        save42Visible_ = {"blur", "bgnd", "tick", "lstb", "sbar"}; // 'cros' paints as overlay in the idle state
    // Shared popup lane (analysis/pass13/POPUP_LANE_SPEC.md): FEFLOW10.LAY = the generic ok/cancel modal,
    // FEFLOW37.LAY = the yes/no confirm family (quit prompts). Both share popp=feflow86.fsh#back + blur.
    {
        const char* kSpec[] = {"FEFLOW10.LAY", "FEFLOW37.LAY"};
        formats::ScreenLayout* tgt[] = {&pop10_, &pop37_};
        bool* have[] = {&havePop10_, &havePop37_};
        for (int i = 0; i < 2; ++i) {
            try {
                fs::path d;
                const std::string t = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@" + kSpec[i], d);
                *tgt[i] = formats::ScreenLayout::build(std::string(kSpec[i]).substr(0, 8), formats::LayoutDocument::parse(std::string_view(t)), nullptr, nullptr, "PC");
                *have[i] = true;
            } catch (const std::exception&) { // popup art absent on this tree: that box stays closed
            }
        }
    }
    // In-game option popups igaoptns/iggoptns/igmoptns (analysis/pass13/IGOPTNS_ART.md): captions lane.
    {
        const char* kSpec[] = {"igaoptns.lay", "iggoptns.lay", "igmoptns.lay"};
        const char* kName[] = {"IGAOPTNS", "IGGOPTNS", "IGMOPTNS"};
        formats::ScreenLayout* tgt[] = {&igA_, &igG_, &igM_};
        for (int i = 0; i < 3; ++i) {
            try {
                fs::path d;
                const std::string t = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@" + kSpec[i], d);
                *tgt[i] = formats::ScreenLayout::build(kName[i], formats::LayoutDocument::parse(std::string_view(t)), nullptr, nullptr, "PC");
                haveIg_ |= 1 << i;
            } catch (const std::exception&) { // popup layout absent on this tree: the lane stays closed
            }
        }
        if (haveIg_) {
            // Scene-ownership whitelist: fill-colour window rect ("back" / audio's "1"), title bar, tab
            // buttons (go_g/go_a/go_c in iggoptns/igmoptns; audio names them 2/3/04), slider track.
            // "list" paints its ROWS textids statically and "12" (x=640) is a hidden FC-bank dump widget; the
            // app overlay painter draws real per-tab rows/values, so both stay composite-hidden [C] IGOPTNS_ART.md.
            igVisible_ = {"back", "titl", "go_g", "go_a", "go_c", "scrl", "1", "2", "3", "04"};
        }
    }
    if (haveCheats_) {
        // Scene-ownership whitelist: blur background, cheatart pane, the eight cheatbox frames, the
        // eight objecttype-18 toggle widgets, 'yes' and the help pane. txt0..txt7 are excluded: their
        // textid=0 default resolves to the empty string (captions are installed at runtime from exe
        // data), so they emit no composite quads; excluding them keeps the gate stats honest.
        cheatsVisible_ = {"blur", "bgnd",                                                            // blur.fsh + cheatart.pak pane
                          "box1", "box2", "box3", "box4", "box5", "box6", "box7", "box8",            // cheatbox `1box` frames
                          "btn0", "btn1", "btn2", "btn3", "btn4", "btn5", "btn6", "btn7",            // objecttype 18 rows
                          "yes", "help"};                                                           // nvicons tic3/tic5 + pane
    }
    if (haveWc_) {
        // Composite whitelists per screen (scene-ownership rule): the tm/gm/qtm/tf/qf/ssN dynamic cells
        // are objecttype 0/4 with shapeID null = engine-painted content, overlaid in drawWcFamily; netplay
        // chat widgets + masc runtime surfaces and the gpnm textid=514 caption slot stay hidden.
        wc18Visible_ = {"back", "flag", "titl", "quit", "fe06", "fe15", "fe89", "fe41", "fe42",
                        "fe11", "fe19", "help"};
        wc23Visible_ = {"back", "flag", "titl", "quit", "fe06", "fe15", "fe89", "fe41", "fe42",
                        "frwd", "strk", "stsc", "help"};
        wc24Visible_ = {"back", "flag", "titl", "quit", "fe06", "fe15", "fe89", "fe41", "fe42",
                        "frwd", "strk", "stsc", "help"};
        wc35Visible_ = {"back", "flag", "titl", "quit", "fe06", "fe15", "fe89", "fe41", "fe42",
                        "frwd", "nglt", "ngrt", "strk", "stsc", "help"};
    }
    // Hub screen (Phase 1): Feflow06.lay — the Windows main-menu hub (mixed-case member; MENU_NAV_
    // WC98.md §3 global-key table + analysis/pass12/edges_table.txt). When present it replaces the
    // FEFLOW05/MNMSETUP fallback as the menu-level screen.
    try {
        fs::path d;
        const std::string t6 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@Feflow06.lay", d);
        hubScreen_ = formats::ScreenLayout::build("FEFLOW06", formats::LayoutDocument::parse(std::string_view(t6)), nullptr, nullptr, "PC");
        haveHub_ = true;
    } catch (const std::exception&) { // no hub layout on this tree: FEFLOW05 menu fallback stays
    }
    // Options screen (feflow15.lay; key handler 0x44f22c, tags 'quit'/'deft'/'back'/'ops2'; the
    // tabs tbb1..4 + lists lst1..4 grammar is measured in MENU_PARITY.md §4 rank 1). Absent -> Enter
    // on fe15 keeps painting the hub (pre-slice behaviour). Raw text kept for the rows=N+ tuples.
    try {
        fs::path d;
        const std::string t15 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@feflow15.lay", d);
        optsRaw_ = t15;
        optsScreen_ = formats::ScreenLayout::build("FEFLOW15", formats::LayoutDocument::parse(std::string_view(t15)), nullptr, nullptr, "PC");
        haveOpts_ = true;
    } catch (const std::exception&) { // no options layout on this tree
    }
    // Save/Load entry (Feflow89.lay): the slot list scene behind the hub's fe41 button. Six slot
    // buttons b000..b005 (x=171, y=196+32*i, w=262 h=25) over USER/SLOTnn.SAV files (saveload.hpp).
    try {
        fs::path d;
        const std::string t89 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@Feflow89.lay", d);
        slScreen_ = formats::ScreenLayout::build("FEFLOW89", formats::LayoutDocument::parse(std::string_view(t89)), nullptr, nullptr, "PC");
        haveSl_ = true;
    } catch (const std::exception&) { // no save-load layout on this tree: fe41 is a documented no-op
    }
    // Credits scene (FEFLOW19.LAY): the core-team roll behind the hub's cred button. The tournament
    // presentation code already composites FEFLOW35/25/26/27; 19 is its credits sibling (MENU_NAV
    // §5: feflow19 handlers at 0x4526.., events 1001/1002 = button activate/hover family).
    try {
        fs::path d;
        const std::string t19 = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW19.LAY", d);
        credScreen_ = formats::ScreenLayout::build("FEFLOW19", formats::LayoutDocument::parse(std::string_view(t19)), nullptr, nullptr, "PC");
        haveCred_ = true;
    } catch (const std::exception&) { // no credits layout on this tree: cred is a documented no-op
    }
    // Options row tables (feflow15.lay lst1..4 `rows=N+` tuples; parsed from the raw member text,
    // the LayoutDocument drops continuation lines). Value indices start at 0 (first FC caption).
    if (haveOpts_)
        for (int t = 0; t < 4; ++t) {
            optRows_[std::size_t(t)] = parseOptionRows(optsRaw_, t);
            optVals_[std::size_t(t)].assign(optRows_[std::size_t(t)].size(), 0);
        }
    // Mouse cursor sprite (MAUS.PAK, exe string offset 1451894): absent art => no sprite drawn.
    if (presentation::loadFeCursor(tree_, cursor_) && !cursorTex_)
        cursorTex_ = r.createTexture(cursor_.bmp, render::Wrap::Clamp, "fe/maus-curs");
    loadTeams();
}

// Team-select list data: ZWCUP.RES pool ids (40) + FCDB/FCDB_ENG.DBI STR0 fullName names.
// Non-throwing: a tree without these assets leaves the pool empty and Enter keeps launching
// the tournament driver directly (pre-team-select behaviour).
void FeFlow::loadTeams() {
    try {
        const auto res = wc98::readFile(tree_ / "FE/TEAMHIST.ENG/ZWCUP.RES");
        tournament::ZwcupRes z;
        if (!tournament::parseZwcupRes(std::as_bytes(std::span(res)), z)) return;
        poolIds_ = z.ids;
        const auto dbPath = wc98::findCaseInsensitive(tree_, "COMMON/FCDB.DBI");
        const auto nmPath = wc98::findCaseInsensitive(tree_, "COMMON/FCDB_ENG.DBI");
        if (!dbPath.empty() && !nmPath.empty()) {
            const auto db = formats::FcdbDatabase::parse(wc98::readFile(dbPath));
            const auto names = formats::FcdbNames::parse(wc98::readFile(nmPath));
            // League-config roster rows (FEFLOW73 pnNN, 74/79 prNN, 80 pnNN): keep the full record
            // set + STR1 player-name table (COMMON/FCDBP<LANG>.DBI, entries parallel to players).
            fcdb_ = db;
            std::string up = cfgLang_;
            for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
            if (const auto pp = wc98::findCaseInsensitive(tree_, "COMMON/FCDBP" + up + ".DBI"); !pp.empty()) {
                try { fcdbPlayerNames_ = formats::FcdbNames::parse(wc98::readFile(pp)).players; }
                catch (const std::exception&) {} // name table absent: rows draw jersey numbers only
            }
            fcdbTeamFullNames_.clear();
            fcdbTeamFullNames_.reserve(names.teams.size());
            for (const auto& t : names.teams) fcdbTeamFullNames_.push_back(t.fullName);
            for (const std::uint16_t id : poolIds_) {
                const auto it = std::ranges::find(db.teams, id, &formats::FcdbTeam::id);
                const auto idx = std::size_t(std::distance(db.teams.begin(), it));
                poolNames_.push_back(it != db.teams.end() && idx < names.teams.size() ? names.teams[idx].fullName
                                                                                      : std::string());
                // Flag art file: "FNNN" = record index, not the id (F030 = Argentina, F031 = Australia).
                char ff[8];
                std::snprintf(ff, sizeof ff, "F%03u", unsigned(idx));
                poolFlagFile_.push_back(it != db.teams.end() ? ff : std::string{});
            }
        } else {
            poolNames_.assign(poolIds_.size(), std::string());
            poolFlagFile_.assign(poolIds_.size(), std::string());
        }
    } catch (const std::exception&) {
        poolIds_.clear();
        poolNames_.clear();
        poolFlagFile_.clear();
    }
    teams_.setCount(int(poolIds_.size()));
}

// Pool ids are FCDB team ids; the art file name carries the record INDEX (F030 = Argentina, id 86).
std::string FeFlow::flagFileForTeam(std::uint32_t id) const {
    const auto it = std::ranges::find(poolIds_, std::uint16_t(id));
    if (it == poolIds_.end()) return {};
    const std::size_t i = std::size_t(std::distance(poolIds_.begin(), it));
    return i < poolFlagFile_.size() ? poolFlagFile_[i] : std::string();
}

// --fe-screen capture lane ("make all menus 1:1"): an arbitrary member of FE/PACK/LAYOUTS/
// LAYOUTS.BIG through the same tolerant reader as the menu/grid screens (LAY_COMPOSITOR_NOTES.md).
// The composite draws every shown() object — no scene-ownership whitelist: these are reference
// captures for visual review, not in-game scenes.
bool FeFlow::loadCaptureScreen(std::string_view name, std::string& err) {
    // Search order: FE/PACK/LAYOUTS/LAYOUTS.BIG member, then IGLAY.BIG (in-game popups), then the
    // loose tree copies (FE/INGAME/*.LAY). All go through the same tolerant reader.
    const std::string n = std::string(name) + ".LAY";
    static constexpr std::array<const char*, 2> archives = {"LAYOUTS.BIG", "IGLAY.BIG"};
    try {
        fs::path dir;
        std::string text;
        bool found = false;
        for (const char* arc : archives) {
            try {
                text = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS" / arc).string() + "@" + n, dir);
                found = true;
                break;
            } catch (const std::exception&) { /* member absent in this archive: try the next */ }
        }
        if (!found) {
            const fs::path loose = wc98::findCaseInsensitive(tree_, "FE/INGAME/" + n);
            if (loose.empty()) throw std::runtime_error(n + ": no such layout member/file");
            text = presentation::readLayout(loose.string(), dir);
        }
        captureScreen_ = formats::ScreenLayout::build(std::string(name), formats::LayoutDocument::parse(std::string_view(text)), nullptr, nullptr, "PC");
        captureName_ = name;
        useCapture_ = true;
        return true;
    } catch (const std::exception& e) {
        err = e.what(); // formats::FormatError / readLayout's "no such archive member"
        return false;
    }
}

FeDrawStats FeFlow::drawCaptureScreen(render::SpriteBatch& ui) const {
    FeDrawStats st = presentation::composite(captureScreen_, ui, const_cast<ScreenArt&>(art_), text_.get());
    drawCursor(ui, st); // MAUS.PAK arrow overlay like every other FE composite
    return st;
}

FeDrawStats FeFlow::drawCurrent(render::SpriteBatch& ui) const {
    if (useCapture_) return drawCaptureScreen(ui); // --fe-screen lane replaces the menu composite
    if (popKind_) return drawPopup(ui); // shared popup lane over the caller scene (pass 13)
    if (setup07View_) return drawSetup07(ui); // FEFLOW07 matchup screen (hub fe07 wedge)
    if (stad08View_) return drawStad08(ui);   // FEFLOW08 stadium select (fe08 confirm chain)
    if (mgmtView_) return drawMgmtGrid(ui);  // FEFLOW43 management grid (--fe-mgmt-grid gate)
    if (lsView_)   return drawLoadSave(ui);  // feflow41 LOAD / feflow42 SAVE lists over the slot dir
    if (teamScreen_) return drawTeams(ui);
    if (setupScreen_) return drawSetup(ui); // FEFLOW09/50 friendly/practise config screens
    if (leagueCfg_) return drawLeagueCfg(ui); // FEFLOW72/74/75/76/79/80 config family
    if (wcCfg_) return drawWcFamily(ui); // FEFLOW18/23/24/35 world-cup family screens
    if (histScreen_) return drawHistory(ui); // feflow90/91 world-cup history boards (pass 13)
    if (igMode_) return drawIgOptns(ui);     // igaoptns/iggoptns/igmoptns captions lane (pass 13)
    if (cheatsView_) return drawCheats(ui); // feflow95 cheats screen (MENU_PARITY.md §8)
    if (leagueView_) return drawLeague(ui);
    if (optsView_ && haveOpts_) return drawOptions(ui);   // FEFLOW15 config screen (Phase 2)
    if (slView_ && haveSl_) return drawSaveLoad(ui);      // Feflow89 slot list (Phase 3)
    if (credView_ && haveCred_) {                         // FEFLOW19 credits roll behind the hub cred wedge (Phase 4)
        if (++credFrames_ > kCredDurationFrames) credView_ = false; // auto-return after the presentation duration
        else return drawCredits(ui);
    }
    if (onHub()) return drawHub(ui); // Feflow06 hub replaces the MNMSETUP fallback composite
    FeDrawStats st;
    // Scene-ownership visibility: MNMSETUP's netplay widgets (mneb edit boxes, modem lists,
    // dial strings) belong to scenes the port never shows -> hidden. Only the FE-flow art
    // objects are painted: back collage + titl logos + flag anim (evidence in the header).
    if (haveScreen_) st = presentation::composite(screen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &menuVisible_);
    // Menu item labels: drawn by scene-handler code in the original, not by LAY widgets. The four
    // item strings are embedded in FIFAWC.EXE right after the "main menu" label string at 0x564f89:
    // 'friendly' (0x564fb1), 'league play' (0x564fbd), 'cup competition' (0x564fcd), 'practise'
    // (0x564fe1); no shipped LAY carries kt/menu objects for them (FE_MENU_SCREENS_WC98.md §4).
    // Labels sit to the right of the selection markers; marker geometry is port-fixed ([H]: the
    // original positions come from the runtime mainmenu window-object, not recoverable statically).
    static constexpr std::string_view kLabels[kFeEntryCount] = {"Friendly", "League Play", "Cup Competition", "Practise"};
    // Plain face eras10 (fontID 0, exe font table 0x57d2c0) for menu items; the bold-italic
    // HUD face is a caption font and reads muddy over the collage. Fallback: primary face.
    const formats::FfnFont* f = text_ ? text_->face(0) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(0) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    const bool hasText = text_ && text_->ready() && f && ftex;
    for (int i = 0; i < kFeEntryCount; ++i) {
        const float y = 356.f + 30.f * i; // four rows, bottom row fully on screen (480 px)
        if (i == sel_) ui.draw(0, {36.f, y, 18.f, 18.f}, {}, render::packRgba(90, 200, 255, 255));
        else { // 1-px outline
            const std::uint32_t w = render::packRgba(255, 255, 255, 220);
            ui.draw(0, {36.f, y, 18.f, 1}, {}, w), ui.draw(0, {36.f, y + 17.f, 18.f, 1}, {}, w);
            ui.draw(0, {36.f, y, 1.f, 18.f}, {}, w), ui.draw(0, {53.f, y, 1.f, 18.f}, {}, w);
        }
        if (!hasText) continue;
        const std::uint32_t rgba = i == sel_ ? render::packRgba(255, 224, 96, 255) : render::packRgba(235, 235, 235, 255);
        for (const auto& q : presentation::layoutText(*f, kLabels[std::size_t(i)], 90.f, y + 2.f, 16.f, rgba))
            ui.draw(ftex, q.dst, q.uv, q.rgba);
        ++st.texts;
    }
    drawCursor(ui, st);
    return st;
}

// Team-select frame. With FEFLOW11.LAY present (the authentic ZWCUP pick grid), composite the
// layout and overlay the dynamic rows; else keep the flat usability list.
FeDrawStats FeFlow::drawTeams(render::SpriteBatch& ui) const {
    if (haveGrid_) return drawTeamsGrid(ui);
    FeDrawStats st;
    ui.draw(0, {24.f, 24.f, 592.f, 432.f}, {}, render::packRgba(10, 14, 28, 225));
    const formats::FfnFont* f = text_ ? text_->font() : nullptr;
    if (!f || !text_->ready()) return st; // no font: the panel alone draws
    auto line = [&](std::string_view s, float x, float y, float px, std::uint32_t rgba) {
        for (const auto& q : presentation::layoutText(*f, s, x, y, px, rgba)) ui.draw(text_->texture(), q.dst, q.uv, q.rgba);
    };
    line("SELECT YOUR TEAM", 48.f, 40.f, 18.f, render::packRgba(255, 255, 255, 255));
    const int pg = teams_.page(), rows = TeamListNav::kRows;
    for (int i = 0; i < rows; ++i) {
        const int idx = pg * rows + i;
        if (idx >= int(poolIds_.size())) break;
        std::string label = poolNames_[std::size_t(idx)];
        if (label.empty()) label = "team " + std::to_string(poolIds_[std::size_t(idx)]); // FCDB name table absent
        const bool cur = idx == teams_.sel;
        line(label, 84.f, 96.f + 32.f * i, 16.f,
             render::packRgba(cur ? 255 : 235, cur ? 224 : 235, cur ? 96 : 235, 255));
        ++st.texts;
    }
    // Cursor marker: the same flat quad as the menu selection.
    ui.draw(0, {52.f, 96.f + 32.f * (teams_.sel % rows) + 4.f, 18.f, 18.f}, {}, render::packRgba(90, 200, 255, 255));
    line("PAGE " + std::to_string(pg + 1) + "/" + std::to_string(teams_.pageCount()), 48.f, 420.f, 14.f,
         render::packRgba(200, 200, 200, 255));
    drawCursor(ui, st); // MAUS.PAK arrow on the mouse pointer as well
    return st;
}

// FEFLOW11 frame: static screen (map background, big flag tmfg, scoreboard bar sbar, group cells,
// help text) through the compositor, then per-row flag + name drawn at the LAY's own row-slot
// positions (stfNN/ttnn objects). The layout ships exactly 8 row slots == TeamListNav::kRows;
// pages of the 40-team pool swap the slot contents. Flags = FE/GLOBAL FNNN.FSH where NNN is the
// team's FCDB.DBI record index (measured: id 86 -> idx 30 -> F030 = Argentina shield), single shape
// tag "f000" (every FNNN.FSH carries exactly one entry, named f000).
FeDrawStats FeFlow::drawTeamsGrid(render::SpriteBatch& ui) const {
    FeDrawStats st = presentation::composite(gridScreen_, ui, const_cast<ScreenArt&>(art_), text_.get());
    const formats::FfnFont* f = text_ ? text_->font() : nullptr;
    const bool hasText = f && text_->ready();
    auto line = [&](std::string_view s, float x, float y, float px, std::uint32_t rgba) {
        if (!hasText) return;
        for (const auto& q : presentation::layoutText(*f, s, x, y, px, rgba)) ui.draw(text_->texture(), q.dst, q.uv, q.rgba);
    };
    const int rows = TeamListNav::kRows;
    const int pg = teams_.page();
    for (int i = 0; i < rows; ++i) {
        const int idx = pg * rows + i;
        if (idx >= int(poolIds_.size())) break;
        const std::string sfx = std::format("{:02d}", i); // ttNN name slots (zero-padded in the LAY)
        const bool cur = idx == teams_.sel;
        const std::string flagFile = std::size_t(idx) < poolFlagFile_.size() ? poolFlagFile_[std::size_t(idx)]
                                                                           : std::string(); // FNNN = FCDB record index
        if (const formats::LayoutObject* stf = gridScreen_.object("stf" + std::to_string(i))) { // stf0..stf7
            if (flagFile.empty()) ++st.missing;
            else if (const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", flagFile, "f000", -1);
                     sh.tex) {
                // Flag bitmaps are full-size shields; the LAY slot is a small icon cell -> scale into it.
                ui.draw(sh.tex, {float(stf->x), float(stf->y), float(stf->width > 0 ? stf->width : 27),
                                  float(stf->height > 0 ? stf->height : 21)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
                ++st.shapes;
            } else
                ++st.missing;
        }
        std::string label = poolNames_[std::size_t(idx)];
        if (label.empty()) label = "team " + std::to_string(poolIds_[std::size_t(idx)]); // FCDB name absent
        const formats::LayoutObject* tt = gridScreen_.object("tt" + sfx);
        const int tx = tt ? tt->x : 294; // LAY name-column slot (measured fallback)
        line(label, float(tx), float(172 + 32 * i), 16.f,
             render::packRgba(cur ? 255 : 235, cur ? 224 : 235, cur ? 96 : 235, 255)); // current row yellow (measured menu highlight)
        ++st.texts;
    }
    // Current-team big flag over the tmfg slot (the LAY's static default is f002): same shape rule.
    if (!poolIds_.empty() && teams_.sel >= 0 && std::size_t(teams_.sel) < poolFlagFile_.size()) {
        const std::string& flagFile = poolFlagFile_[std::size_t(teams_.sel)]; // record-index name (F030 = Argentina)
        if (const formats::LayoutObject* tmfg = gridScreen_.object("tmfg"); tmfg && !flagFile.empty()) {
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", flagFile, "f000", -1);
            if (sh.tex) ui.draw(sh.tex, {float(tmfg->x), float(tmfg->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
        }
    }
    // Scrollbar thumb in the sbar track: flat quad, page-proportional.
    if (const formats::LayoutObject* sbar = gridScreen_.object("sbar")) {
        const int pages = teams_.pageCount();
        const float track = float(sbar->height > 0 ? sbar->height : 245);
        const float thumb = track / float(pages > 0 ? pages : 1);
        const float y = float(sbar->y) + (track - thumb) * float(teams_.page()) / float(pages > 0 ? pages : 1);
        ui.draw(0, {float(sbar->x), y, float(sbar->width > 0 ? sbar->width : 8), thumb}, {}, render::packRgba(210, 230, 255, 255));
    }
    drawCursor(ui, st);
    return st;
}

// MAUS.PAK cursor arrow (entry 0 = the 28x26 arrow): drawn last over each FE composite. The
// original engine loads .\fe\pack\maus.pak in code (exe string block offset 1451894) and blits
// it after screen composition; the decoded RGBA carries transparent pixels (measured: 250 of
// 728 alpha=0), blended by the UI pipeline's SRC_ALPHA state. Hotspot = FSH header centerX/Y.
void FeFlow::drawCursor(render::SpriteBatch& ui, presentation::FeDrawStats& st) const {
    if (!cursorTex_) return;
    const float w = float(cursor_.bmp.width), h = float(cursor_.bmp.height);
    const float x = std::clamp(float(ptrX_ + cursor_.hotX), 0.f, render::SpriteBatch::kWidth - w);
    const float y = std::clamp(float(ptrY_ + cursor_.hotY), 0.f, render::SpriteBatch::kHeight - h);
    ui.draw(cursorTex_, {x, y, w, h}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
    ++st.cursors;
}

// Friendly/practise config screens (FE_MENU_SCREENS_WC98.md §5): the original uses up/down to move
// the row cursor and left/right to cycle that row's option values; Enter starts the match, Esc
// returns. The port mirrors the navigation — row selection is drawn as a hilite outline over the
// alp{n} pane (the LAY's HILITECOLOUR analogue), the option index is kept per row for the match
// model to consume when the config plumbing lands.
void FeFlow::move(int delta) {
    if (popKind_) { movePopup(delta); return; } // two-button cursor (ok/cancel, yes/no payload pair)
    if (setup07View_) { s07Row_ = std::clamp(s07Row_ + delta, 0, 1); return; } // UP/DOWN = team side [C]
    if (mgmtView_) { // FEFLOW43 UP/DOWN = roster row cursor; four-row window pages via sbar [H]
        const int n = gridRosterSize();
        mgmtRow_ = std::clamp(mgmtRow_ + delta, 0, std::max(1, n) - 1);
        return;
    }
    if (lsView_) { // lstb rows over the SLOT-file scan (page window 10, sbar pan thumb)
        const int n = std::max<int>(1, int(slotFiles().size()));
        lsRow_ = std::clamp(lsRow_ + delta, 0, n - 1);
        return;
    }
    if (stad08View_) { s08Sel_ = (s08Sel_ + delta + 10) % 10; return; }         // mpNN tile ring cursor
    if (wcCfg_) { // keyloop_notes.txt/wcfamily_notes.txt: 18 = tmNN row cursor [H]; 23/24 strk/stsc button
        // focus (engine mouse-only, port maps up/down to the two buttons); 35 = ssnN table rows.
        if (wcCfg_ == 18) wcRow_ = std::clamp(wcRow_ + delta, 0, 31);
        else if (wcCfg_ == 23 || wcCfg_ == 24) { wcRow_ = (wcRow_ + delta + 2) % 2; if (delta < 0 && wcRow_ < 0) wcRow_ += 2; }
        else wcRow_ = std::clamp(wcRow_ + delta, 0, 3);
        return;
    }
    if (histScreen_) { moveHistory(delta); return; } // feflow90 year cursor ('ysla'/'yslc'/'ysrb')
    if (igMode_) { moveIgOption(delta); return; }     // igoptns list rows (up/down, bounded)
    if (cheatsView_) { moveCheat(delta); return; } // feflow95 rows btn0..btn7 (y=183+25i)
    if (leagueCfg_) { // config grids: up/down = kt/menu row cursor / s001 slider value (keyloop_notes.txt)
        switch (leagueCfg_) {
            case 74: case 80: cfgRow_ = std::clamp(cfgRow_ + delta, 0, std::max(1, rosterSize()) - 1); break;
            case 75: stratVal_ = std::clamp(stratVal_ + delta, 0, 4); break; // strategy: 5-step domain [H]
            case 76: aggrVal_ = std::clamp(aggrVal_ + delta, 0, 15); break; // FCDB skill-nibble scale
            case 78: case 79: biasVal_ = std::clamp(biasVal_ + delta, 0, 8); break; // attack-bias slider [H]
            default: break; // FEFLOW72: continue-button row, no cursor domain (b010..b012)
        }
        return;
    }
    if (optsView_ && haveOpts_) {                         // options rows lst{tab} (up/down, clamped)
        const int n = std::max<int>(1, int(optRows_[std::size_t(optTab_)].size()));
        optRow_ = std::clamp(optRow_ + delta, 0, n - 1);
        return;
    }
    if (slView_ && haveSl_) {                             // slot rows b000..b005
        slSlot_ = std::clamp(slSlot_ + delta, 0, kSaveSlots - 1);
        return;
    }
    if (credView_) return;                                // roll: no cursor domain
    if (onHub()) {                                        // hub buttons in LAY order (object 6..19)
        hubSel_ = (hubSel_ + delta) % int(kHubButtons.size());
        if (hubSel_ < 0) hubSel_ += int(kHubButtons.size());
        return;
    }
    if (teamScreen_) teams_.move(delta);
    else if (leagueView_) { moveLeague(delta); return; } // arrows = pn00..pn08 rows
    else if (setupScreen_) {
        setupRow_ = (setupRow_ + delta) % 4;
        if (setupRow_ < 0) setupRow_ += 4; // four config rows (ovr0..3 family)
    } else {
        sel_ = (sel_ + delta) % kFeEntryCount;
        if (sel_ < 0) sel_ += kFeEntryCount;
    }
}
void FeFlow::moveLeague(int dir) {
    if (!haveLeague_) return;
    leagueSel_ = (leagueSel_ + dir) % 9; // pn00..pn08 rows
    if (leagueSel_ < 0) leagueSel_ += 9;
}

bool FeFlow::enterLeague() {
    return haveLeague_; // row confirm: fixtures aren't simulated -> shell launches the default match
}

void FeFlow::adjustValue(int dir) {
    if (mgmtView_) { // tmlt/tmrt arrows: cycle the pool team shown on the grid (L/R axis of two-axis [H])
        const int n = std::max<int>(1, int(poolIds_.size()));
        mgmtTeam_ = (mgmtTeam_ + dir + n) % n;
        mgmtRow_ = 0; // team switch resets the row window
        return;
    }
    if (lsView_) { lstbPage(dir); return; } // left/right pan the list page via sbar [H]
    if (setup07View_) { // fotl/fotr arrows (ltlb..rtrb): cycle the focused side's pool entry [C two-axis]
        const int n = std::max<int>(1, int(poolIds_.size()));
        int& v = s07Row_ == 0 ? s07Left_ : s07Right_;
        v = (v + dir + n) % n;
        if (v == (s07Row_ == 0 ? s07Right_ : s07Left_)) // sides never mirror: skip the collision
            v = (v + dir + n) % n;
        return;
    }
    if (stad08View_) { move(dir); return; } // nslt/nsrt arrows = tile ring (L/R axis of two-axis grammar)
    if (leagueCfg_) { cycleLeagueScreen(dir); return; } // left/right = gofd/gobk screen chain
    if (wcCfg_) { // feflow35 'nglt'/'ngrt': group index (v +/- 1) & 7, full table repaint (decoded 1:1)
        if (wcCfg_ == 35) wcGroup_ = (wcGroup_ + dir + 8) & 7; // feflow18/23/24 keyloops carry no L/R tag
        return;
    }
    if (histScreen_) return; // feflow90/91 keyloops carry no L/R tag [C] FUN_00468728/FUN_00468b50
    if (igMode_) {           // left/right cycles the focused row's FC-caption value (two-axis rows)
        if (igRow_ >= 0 && std::size_t(igRow_) < igRows_.size() && igVals_.size() > std::size_t(igRow_)) {
            const OptRow& r = igRows_[std::size_t(igRow_)];
            if (!r.values.empty()) {
                int& v = igVals_[std::size_t(igRow_)];
                v = (v + dir + int(r.values.size())) % int(r.values.size());
            }
        }
        return;
    }
    // Left/right cycles one option per row on the setup screen and on the options list rows
    // (the original's two-axis scheme: up/down = row, left/right = value).
    if (setupScreen_) {
        setupValue_[std::size_t(setupRow_)] = (setupValue_[std::size_t(setupRow_)] + dir + 8) % 8;
        return;
    }
    if (!optsView_ || !haveOpts_) return;
    auto& rows = optRows_[std::size_t(optTab_)];
    auto& vals = optVals_[std::size_t(optTab_)];
    if (optRow_ < 0 || std::size_t(optRow_) >= rows.size()) return;
    const OptRow& r = rows[std::size_t(optRow_)];
    if (!r.values.empty() && !vals.empty()) { // FC-caption value cycle (mode 2 list row)
        int& v = vals[std::size_t(optRow_)];
        v = (v + dir + int(r.values.size())) % int(r.values.size());
    } else if (r.label == 1186 && settings_) { // "Music Volume" slider -> port [audio] music volume, live gain
        settings_->audio.music = std::clamp(settings_->audio.music + dir * 5, 0, 100);
        audioDirty_ = true;
        if (mixer_) mixer_->setMasterGain(float(settings_->audio.music) / 100.f); // [H] port mapping: music row drives the FE stream gain
    }
}

// Enter dispatch (per-scene key handlers, event 2001): feflow15 init 0x44f22c tags 'quit'/'deft'/
// 'back'; feflow41/feflow89 family init 0x459420 ('quit' @0x4595cc = back-out path); hub feflow06
// handler per-wedge tag compares (fe11 @0x449e29, fe07 @0x449e30, fe32 @0x449ce5, repl @0x449c01,
// cred @0x449c11). Enter on hub = the wedge tag's scene edge (hubActivate below).
bool FeFlow::enter() {
    if (popKind_) { activatePopup(); return false; } // Enter = the focused button's payload event -> close to caller
    if (mgmtView_) { // Enter = the focused function button [H]: canc closes, save writes the custom
        // tactic to .\DATA\TEAMC%.2d.dat (FE/PACK/TEAMCUS family), rest/resa reset/restore. The port
        // lanes: canc -> close; rest/resa -> row cursor home; save/resta1-style writes stay open items.
        if (mgmtRow_ < 0) { mgmtView_ = false; return false; } // 'canc' focus (row -1 domain [H])
        mgmtRow_ = 0;                                          // rest/resa: reset view to row 0
        return false;
    }
    if (lsView_ == 41) { loadSelectedSlot(); return false; }   // LOAD row -> existing verify/load path
    if (lsView_ == 42) { saveSelectedSlot(); return false; }   // SAVE row -> tick confirm + write
    if (setup07View_) { // 'fe09' forward edge: the scene switch leaves feflow07 and opens FEFLOW09
        setup07View_ = false; stad08View_ = false;       // [C] FUN_004456B9 chain: one screen at a time
        if (haveSetup_) { setupScreen_ = true; setupPractise_ = false; return false; }
        return true;                                     // no friendly-config art: shell launches the match
    }
    if (stad08View_) { stad08View_ = false; return true; } // stadium confirm: shell starts the match [H]
    if (wcCfg_) { // Enter = the scenes' button press: feflow18 'rand' refreshes the draw (+ counter, cap 99)
        if (wcCfg_ == 18 && wcRandCount_ < 99) ++wcRandCount_;
        return false; // 23/24 strk/stsc + 35 stats-view: no launch edge from these boards
    }
    if (histScreen_ == 90) { histScreen_ = 91; return false; } // 'fe91' Enter chain -> feflow91 [C] FUN_004857a8
    if (histScreen_) return false;                             // feflow91 confirm: presentation-only board
    if (igMode_) return false; // row Enter = value select is mouse-only in the port; L/R cycles values
    if (cheatsView_) { toggleCheat(cheatRow_); return false; } // Enter flips the focused checkbox row
    if (leagueCfg_) {                                   // Enter = 'alok' confirm: popups close to the grid, grids stay put
        if (leagueCfg_ == 76 || leagueCfg_ == 78) cycleLeagueScreen(-1);
        return false;
    }
    if (optsView_ && haveOpts_) return false; // row Enter = value select is mouse-only in the port; L/R cycles values
    if (slView_ && haveSl_) {                 // slot confirm (Phase 3): WCT1 load via app/saveload.cpp
        const auto file = defaultSlotDir() / formats::slotFileName(slSlot_);
        try {
            const TournamentState st = loadTournament(file);
            const auto f = summaryFields(st);
            std::printf("fe: slot %d loaded (mode=%u champion=%u)\n", slSlot_, f[0], f[3]);
        } catch (const std::exception&) { // no save on this slot yet: the original offers New/Load choices
            std::printf("fe: slot %d empty\n", slSlot_);
        }
        slView_ = false;                      // back to the hub after the load attempt ('quit' tag path)
        return false;
    }
    if (credView_) { credView_ = false; return false; } // Enter/Esc exits the roll (event 2001)
    if (onHub()) return hubActivate(hubSel_) >= 0;      // wedge -> scene edge; true only for launch targets
    if (leagueView_) { // row confirm -> formation-budget screen first (SCENEREG cluster feflow72)
        if (openLeagueCfg(72)) return false;
        return true; // no FEFLOW72 layout on this tree: shell launches the default match
    }
    if (teamScreen_) { // confirm the highlighted team: Enter launches with pickedTeam()
        pick_ = poolIds_.empty() ? -1 : int(poolIds_[std::size_t(teams_.sel)]);
        return true;
    }
    if (setupScreen_) { // friendly config accepted: the 'fe08' widget tag opens stadium select first
        if (!setupPractise_ && haveStad08_ && openStad08()) return false;
        return true; // no FEFLOW08 layout on this tree: shell starts the match from here (pre-slice)
    }
    if ((sel_ == int(FeEntry::Exhibition) || sel_ == int(FeEntry::Practise)) && haveSetup_) {
        // Friendly / practise entries open their match-setup screen first (feflow09/feflow50).
        setupScreen_ = true;
        setupPractise_ = sel_ == int(FeEntry::Practise);
        return false;
    }
    if (sel_ == int(FeEntry::LeaguePlay) && haveLeague_) { // League Play opens FEFLOW73 first
        leagueView_ = true;
        return false;
    }
    if (sel_ == int(FeEntry::Tournament) && !poolIds_.empty()) { // Tournament entry: pick your country first
        teamScreen_ = true;
        teams_.setCount(int(poolIds_.size()));
        return false;
    }
    return true;
}

void FeFlow::back() {
    if (popKind_) { popKind_ = 0; return; } // Esc closes the popup to its caller scene (the 'cancel' branch)
    if (mgmtView_) { mgmtView_ = false; hubView_ = true; return; } // Esc -> hub ('quit' tag family [C])
    if (lsView_)   { lsView_ = 0; lsConfirm_ = 0; return; }        // 'exit' crs button closes the list
    if (setup07View_) { setup07View_ = false; hubView_ = true; return; } // Esc -> hub ('fe06' exit wedge [C] help=77)
    if (stad08View_) { stad08View_ = false; if (!setupScreen_) hubView_ = true; return; } // Esc -> parent scene
    // Esc = the scenes' 'back'/'quit' widget tags (feflow15 tag 'back' @0x44f29a, feflow41 family
    // 'quit' @0x4595cc): every view closes to its parent; the hub has no parent scene.
    if (optsView_) { optsView_ = false; return; }
    if (slView_)   { slView_ = false; return; }
    if (credView_) { credView_ = false; return; }
    if (histScreen_) { histScreen_ = 0; return; } // Esc from feflow90/91 -> hub ('fe06'/'quit' exit tags)
    if (igMode_) { igMode_ = 0; return; }         // Esc closes the in-game option popup to its parent
    if (onHub()) return; // hub Esc: no parent scene (the Windows hub exits via the 'quit' wedge)
    if (leagueCfg_) { leagueCfg_ = 0; return; } // Esc from a config grid/popup -> FEFLOW73 stays open
    if (wcCfg_) { wcCfg_ = 0; return; } // Esc closes the family screen to its parent flow
    if (cheatsView_) { cheatsView_ = false; return; } // Esc closes the cheats screen to the menu [H] dev lane
    if (leagueView_) { leagueView_ = false; return; } // Esc from the league list: straight to menu
    if (setupScreen_) { setupScreen_ = false; return; } // Esc from the config screen: straight to menu
    teamScreen_ = false;
    pick_ = -1; // no selection: default behaviour restored
}

// Slider thumb geometry (LAY x/y/w/h) + value domain. The LAY `barshapeid`/`knobshapeid` keys point
// at real art (FEFLOW75/79 PAKs ship sbar+sbut); composite skips KnobBar widgets entirely, so the
// bar texture + proportional knob are painted here (flat quad when the knob tag is absent [H]).
void paintSliderThumb(render::SpriteBatch& ui, presentation::ScreenArt& art, const formats::LayoutObject& o,
                      int val, int lo, int hi) {
    const float x = float(o.x), y = float(o.y);
    const float w = float(o.width > 0 ? o.width : 16), h = float(o.height > 0 ? o.height : 8);
    const bool vert = h > w;
    const std::string stem = [&] {
        std::string f = o.fname;
        const auto dot = f.find_last_of('.');
        if (dot != std::string::npos) f = f.substr(0, dot);
        std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return f;
    }();
    const std::string vpath = o.prop("vpath") ? *o.prop("vpath") : std::string();
    if (const std::string* barTag = o.prop("barshapeid")) {
        const auto sh = art.shape(vpath, stem, *barTag, -1);
        if (sh.tex) ui.draw(sh.tex, {x, y, w, h}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
    }
    const float frac = hi > lo ? float(val - lo) / float(hi - lo) : 0.f;
    constexpr std::uint32_t kKnob = 0x3CF5F5F5u; // ARGB soft white [H]
    if (vert) {
        const float k = std::clamp((h - 8.f) * frac, 0.f, h - 8.f);
        ui.draw(0, {x, y + k, w, 8.f}, {}, kKnob);
    } else {
        const float k = std::clamp((w - 12.f) * frac, 0.f, w - 12.f);
        if (const std::string* knobTag = o.prop("knobshapeid")) {
            const auto sh = art.shape(vpath, stem, *knobTag, -1);
            if (sh.tex) ui.draw(sh.tex, {x + k, y, float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
            else ui.draw(0, {x + k, y, 12.f, h}, {}, kKnob);
        } else ui.draw(0, {x + k, y, 12.f, h}, {}, kKnob);
    }
}
// League Play select (FEFLOW73.LAY): composite art whitelist + the kt/menu row content the engine
// paints from live state — pn00..pn08 rows get the active team's FCDB roster (#jersey name) with a
// tricolour kit swatch (paintRowLabel, analysis/pass12/list_painter_notes.txt §1); the highlighted
// row gets the filled hilite pane. Rows page through the 20-player squad via the sbar thumb.
FeDrawStats FeFlow::drawLeague(render::SpriteBatch& ui) const {
    FeDrawStats st = presentation::composite(league73_, ui, const_cast<ScreenArt&>(art_), text_.get(), &leagueVisible_);
    // Row hilite: filled pane over the highlighted pn slot (pn00..pn08 geometry x=35 y=134+19*i).
    const float y = 134.f + 19.f * float(leagueSel_ % 9);
    ui.draw(0, {32.f, y - 3.f, 230.f, 16.f}, {}, render::packRgba(255, 224, 96, 48));
    // Roster rows (open item: league fixtures are saved-game (.WCF) state the port does not
    // simulate; FCDB squad data is a static source and paints here). Page = pn cursor / 9.
    const int page = leagueSel_ / 9;
    for (int i = 0; i < 9; ++i)
        if (const formats::LayoutObject* row = league73_.object("pn" + std::format("{:02d}", i)))
            paintRosterRow(*row, ui, page * 9 + i, (leagueSel_ % 9) == i, st);
    return st;
}

// Paint one kt/menu row's caption + kit swatch from the active team's FCDB record. `rosterIdx` =
// squad slot (-1 = blank row). Caption "#jersey name" (FCDBP STR1), swatch = home kit colours
// through presentation::kitColor (fifa98edit hslCols palette table).
void FeFlow::paintRosterRow(const formats::LayoutObject& row, render::SpriteBatch& ui, int rosterIdx,
                            bool hilited, FeDrawStats& st) const {
    if (!fcdb_) return; // no FCDB data on this tree: rows stay shape-only (pre-slice behaviour)
    const formats::FcdbTeam* team = cfgTeam();
    if (!team) return;
    std::string cap;
    const formats::FcdbPlayer* pl = nullptr;
    const int pid = std::size_t(rosterIdx) < team->roster.size() ? team->roster[std::size_t(rosterIdx)] : 0;
    if (pid != 0) { // squad slot (0 = empty; FCDB player ids are ascending, names parallel players)
        const auto it = std::ranges::find(fcdb_->players, std::uint16_t(pid), &formats::FcdbPlayer::id);
        if (it != fcdb_->players.end()) {
            pl = &*it;
            const std::size_t idx = std::size_t(std::distance(fcdb_->players.begin(), it));
            cap = std::format("#{}", pl->shirtNumber);
            if (idx < fcdbPlayerNames_.size() && !fcdbPlayerNames_[idx].empty())
                cap += " " + fcdbPlayerNames_[idx];
        }
    }
    presentation::paintRowLabel(ui, row, cap, *text_, 0, hilited, pl ? &team->home : nullptr, st);
}

// ---- League configuration family (MENU_PARITY.md §4 rank 7) ----------------------------------
// Scene edges (analysis/pass12/edges_table.txt SCENEREG block of the feflow73 cluster): Enter on a
// league row opens FEFLOW72 (formation budget), and the gofd/gobk nav icons chain through
// 74 -> 75 -> 79 -> 80; FEFLOW76 is the aggression slider popup registered beside feflow75. Keyboard
// parity per analysis/pass12/keyloop_notes.txt: up/down = row cursor / s001 value, left/right =
// gofd/gobk screen chain, Enter activates (alok applies on 76), Esc backs out to FEFLOW73.
const formats::FcdbTeam* FeFlow::cfgTeam() const {
    if (!fcdb_) return nullptr;
    // FEFLOW73 opens directly from the main menu (no pool screen ran): poolIds_ is empty there.
    // The original paints the first national team into pn rows in that state, so fall back to the
    // FCDB head team instead of blank rows.
    if (poolIds_.empty()) return fcdb_->teams.empty() ? nullptr : &fcdb_->teams.front();
    const std::size_t i = std::size_t(cfgTeamPool_) % poolIds_.size();
    const auto it = std::ranges::find(fcdb_->teams, poolIds_[i], &formats::FcdbTeam::id);
    return it == fcdb_->teams.end() ? nullptr : &*it;
}

bool FeFlow::openLeagueCfg(int screenId) {
    if (!haveLeagueCfg_) return false;
    const formats::ScreenLayout* sc = screenId == 72 ? (lg72_.object("back") ? &lg72_ : nullptr)
                          : screenId == 74 ? (lg74_.object("titl") ? &lg74_ : nullptr)
                          : screenId == 75 ? (lg75_.object("s001") ? &lg75_ : nullptr)
                          : screenId == 76 ? (lg76_.object("alok") ? &lg76_ : nullptr)
                          : screenId == 78 ? (lg78_.object("alok") ? &lg78_ : nullptr)
                          : screenId == 79 ? (lg79_.object("s001") ? &lg79_ : nullptr)
                          : screenId == 80 ? (lg80_.object("pn00") ? &lg80_ : nullptr) : nullptr;
    if (!sc) return false; // layout absent from this tree: the scene stays closed
    leagueView_ = true;    // FEFLOW73 context (gobk/Esc back target, edges_table.txt feflow73 'gobk')
    leagueCfg_ = screenId;
    cfgName_ = "FEFLOW" + std::to_string(screenId);
    cfgRow_ = 0;
    return true;
}

void FeFlow::cycleLeagueScreen(int dir) {
    // gofd/gobk chain (keyloop_notes.txt: every grid tick carries 'gofd'/'gobk'); the slider
    // popups 76/78 are registered beside feflow75 in SCENEREG, so they join when present.
    static constexpr int kOrder[] = {72, 74, 75, 76, 78, 79, 80};
    int idx = 0;
    for (int i = 0; i < 7; ++i)
        if (kOrder[i] == leagueCfg_) idx = i;
    // Skip screens whose layout is absent on this tree.
    for (int step = 0; step < 7; ++step) {
        idx = (idx + dir + 7) % 7;
        const int cand = kOrder[idx];
        if ((cand == 72 && lg72_.object("bnnr")) || (cand == 74 && lg74_.object("titl")) ||
            (cand == 75 && lg75_.object("s001")) || (cand == 76 && lg76_.object("alok")) ||
            (cand == 78 && lg78_.object("alok")) || (cand == 79 && lg79_.object("s001")) ||
            (cand == 80 && lg80_.object("pn00"))) break;
    }
    leagueCfg_ = kOrder[idx];
    cfgName_ = "FEFLOW" + std::to_string(leagueCfg_);
    cfgRow_ = 0;
}

FeDrawStats FeFlow::drawLeagueCfg(render::SpriteBatch& ui) const {
    using namespace presentation;
    const formats::ScreenLayout* sc = nullptr;
    const std::vector<std::string>* vis = nullptr;
    switch (leagueCfg_) {
        case 72: sc = &lg72_; vis = &lg72Visible_; break;
        case 74: sc = &lg74_; vis = &lg74Visible_; break;
        case 75: sc = &lg75_; vis = &lg75Visible_; break;
        case 76: sc = &lg76_; vis = &lg76Visible_; break;
        case 78: sc = &lg78_; vis = &lg78Visible_; break;
        case 79: sc = &lg79_; vis = &lg79Visible_; break;
        case 80: sc = &lg80_; vis = &lg80Visible_; break;
        default: return {};
    }    FeUiState uist; // nav-icon hover/press state (widgetHit geometry, composite shape-state chain)
    ++animTicks_;
    uist.animMs = animTicks_ * 16u;
    for (const auto& o : sc->objects)
        if (o.shown() && widgetHit(o, float(ptrX_), float(ptrY_))) { uist.hover = o.name; break; }
    if (ptrDown_) uist.pressed = uist.hover;
    FeDrawStats st = composite(*sc, ui, const_cast<ScreenArt&>(art_), text_.get(), vis, &uist);

    // FEFLOW72 continue buttons b010..b012: the engine paints shape + FC caption over the listbox
    // widget (objecttype 4 with TEXTID is dynamic content -> skipped by composite). Caption = FC
    // bank index 233 (measured textid=233,4 on all three buttons).
    if (leagueCfg_ == 72) {
        const char* kBtn[3] = {"b010", "b011", "b012"};
        const char* kShape[3] = {"bt10", "bt20", "bt30"};
        for (int i = 0; i < 3; ++i) {
            const formats::LayoutObject* o = sc->object(kBtn[i]);
            if (!o) continue;
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\feflow72\\", "feflow72", kShape[i], -1);
            if (sh.tex) {
                ui.draw(sh.tex, {float(o->x), float(o->y), float(o->width > 0 ? o->width : sh.w),
                                 float(o->height > 0 ? o->height : sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
                ++st.shapes;
            } else ++st.missing;
            if (text_ && text_->ready()) { // caption centred in the box (alignment=3 semantics)
                const std::string_view s = text_->stringAt(233);
                if (!s.empty()) {
                    const formats::FfnFont* f = text_->face(4);
                    render::TextureId ftex = text_->faceTexture(4);
                    if (!f || !ftex) { f = text_->font(); ftex = text_->texture(); }
                    if (f && ftex) {
                        const float px = f->lineHeight ? float(f->lineHeight) : 16.f;
                        const float w = textWidthPx(*f, s, px);
                        for (const auto& q : layoutText(*f, s, float(o->x) + (float(o->width) - w) * 0.5f,
                                                        float(o->y) + (float(o->height) - px) * 0.5f, px,
                                                        render::packRgba(235, 235, 235, 255)))
                            ui.draw(ftex, q.dst, q.uv, q.rgba);
                        ++st.texts;
                    }
                }
            }
        }
    }

    // kt/menu row overlays (paintRowLabel: caption + tricolour kit swatch + hilite pane).
    const int rowsPerPage = leagueCfg_ == 80 ? 12 : 5;
    if (leagueCfg_ == 74 || leagueCfg_ == 79 || leagueCfg_ == 80) {
        const char* col = leagueCfg_ == 80 ? "pn" : "pr"; // FEFLOW80: left column = squad list
        const int rows = leagueCfg_ == 80 ? 12 : 5;
        for (int i = 0; i < rows; ++i) {
            const formats::LayoutObject* row = sc->object(std::string(col) + std::format("{:02d}", i));
            if (!row) continue;
            paintRosterRow(*row, ui, (cfgRow_ / rowsPerPage) * rowsPerPage + i,
                           (cfgRow_ / rowsPerPage) * rowsPerPage + i == cfgRow_, st);
        }
    }
    // FEFLOW74 kit picker: rows 0/1 = home/away tricolour swatches of the team (FCDB kit fields);
    // custom saved kits are .WCF state the port does not simulate (open item, documented).
    if (leagueCfg_ == 74 && fcdb_) {
        const formats::FcdbTeam* team = cfgTeam();
        for (int i = 0; i < 9; ++i) {
            const formats::LayoutObject* row = lg74_.object("kt" + std::format("{:02d}", i));
            if (!row) continue;
            presentation::paintRowLabel(ui, *row, {}, *text_, 0, cfgRow_ == -1 && i == 0,
                                        team && i < 2 ? (i == 0 ? &team->home : &team->away) : nullptr, st);
        }
    }
    // Context help pane (LAY `help=` keys -> FC captions; helppaneid pane widget, e.g. canc=155,
    // alok=812 on the popups): same model as the hub screen.
    // The popups' default focus is the confirm control (Enter = 'alok', keyloop semantics): its help=
    // caption paints until the pointer hovers another widget.
    drawHelpPane(*sc, ui, st, (uist.hover.empty() || uist.hover == "back") && (leagueCfg_ == 76 || leagueCfg_ == 78)
                     ? "alok" /* default focus = confirm; the popup's 1x1 'back' box sits at (0,0) */
                     : uist.hover);
    // Slider thumbs: sbar/sbNN vertical page indicators + s001 KnobBar value positions.
    const auto thumb = [&](const formats::ScreenLayout& s, const char* name, int lo, int hi, bool vertPage) {
        const formats::LayoutObject* t = s.object(name);
        if (!t) return;
        if (vertPage) {
            const int pages = std::max(1, rosterSize() / rowsPerPage);
            paintSliderThumb(ui, const_cast<ScreenArt&>(art_), *t, cfgRow_ / rowsPerPage, 0, pages - 1);
        } else {
            const int v = leagueCfg_ == 75 ? stratVal_ : leagueCfg_ == 76 ? aggrVal_ : biasVal_;
            paintSliderThumb(ui, const_cast<ScreenArt&>(art_), *t, v, lo, hi);
        }
    };
    if (leagueCfg_ == 74) thumb(lg74_, "sbar", 0, 0, true);
    if (leagueCfg_ == 79) { thumb(lg79_, "sbar", 0, 0, true); thumb(lg79_, "s001", 0, 8, false); }
    if (leagueCfg_ == 75) thumb(lg75_, "s001", 0, 4, false);
    if (leagueCfg_ == 76) thumb(lg76_, "s001", 0, 15, false);
    if (leagueCfg_ == 78) thumb(lg78_, "s001", 0, 8, false);
    if (leagueCfg_ == 80) { thumb(lg80_, "sb00", 0, 0, true); thumb(lg80_, "sb01", 0, 0, true); }

    drawCursor(ui, st);
    return st;
}

// Squad rows on the active grid team (-1 when FCDB is absent): kt/menu row domain for the cursor.
int FeFlow::rosterSize() const {
    const formats::FcdbTeam* t = cfgTeam();
    if (!t) return 0;
    int n = 0;
    for (const std::uint16_t id : t->roster)
        if (id != 0) ++n; // 0 = empty squad slot
    return n;
}
// FEFLOW09/50 composite: art whitelist + the dynamic layer the original paints from live state —
// the two team shields (ltfg/rtfg, same FNNN record-index flag shapes as the pick grid) and the team names in
// the tltx/trtx slots. Row option captions (ctx{n}/grv{n}) are engine-driven strings the port has
// no static source for: they stay blank (open item), the row hilite is drawn over alp{n}.
FeDrawStats FeFlow::drawSetup(render::SpriteBatch& ui) const {
    const formats::ScreenLayout* sc = setupPractise_ && haveSetup_ ? &setup50_ : haveSetup_ ? &setup09_ : nullptr;
    if (!sc) return {};
    FeDrawStats st = presentation::composite(*sc, ui, const_cast<ScreenArt&>(art_), text_.get(), &setupVisible_);
    const auto drawFlag = [&](const char* objName, std::size_t poolIdx) {
        if (poolIds_.empty() || poolIdx >= poolIds_.size() || poolIdx >= poolFlagFile_.size()) return;
        const formats::LayoutObject* o = sc->object(objName);
        if (!o || poolFlagFile_[poolIdx].empty()) return;
        const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", poolFlagFile_[poolIdx], "f000", -1);
        if (sh.tex) { ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
        else ++st.missing;
    };
    const std::size_t selIdx = poolIds_.empty() ? 0 : std::size_t(teams_.sel) % poolIds_.size();
    drawFlag("ltfg", selIdx);
    drawFlag("rtfg", poolIds_.empty() ? 0 : (selIdx + 1) % poolIds_.size()); // CPU side: next pool entry
    const formats::FfnFont* f = text_ ? text_->face(0) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(0) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    const auto drawName = [&](const char* objName, std::size_t poolIdx, float dx) {
        if (!text_ || !text_->ready() || !f || !ftex || poolIds_.empty() || poolIdx >= poolIds_.size()) return;
        const formats::LayoutObject* o = sc->object(objName);
        if (!o) return;
        std::string label = poolNames_[poolIdx];
        if (label.empty()) label = "team " + std::to_string(poolIds_[poolIdx]);
        for (const auto& q : presentation::layoutText(*f, label, float(o->x) + dx, float(o->y), 14.f, render::packRgba(235, 235, 235, 255)))
            ui.draw(ftex, q.dst, q.uv, q.rgba);
        ++st.texts;
    };
    drawName("tltx", selIdx, -30.f);   // name slots sit over the shields (measured x 166/424)
    drawName("trtx", poolIds_.empty() ? 0 : (selIdx + 1) % poolIds_.size(), -30.f);
    if (const formats::LayoutObject* alp = sc->object("alp" + std::to_string(setupRow_))) { // row hilite
        const float x = float(alp->x), y = float(alp->y), w = float(alp->width > 0 ? alp->width : 217), h = float(alp->height > 0 ? alp->height : 55);
        const std::uint32_t c = render::packRgba(255, 224, 96, 255);
        ui.draw(0, {x, y, w, 2.f}, {}, c), ui.draw(0, {x, y + h - 2.f, w, 2.f}, {}, c);
        ui.draw(0, {x, y, 2.f, h}, {}, c), ui.draw(0, {x + w - 2.f, y, 2.f, h}, {}, c);
    }
    drawCursor(ui, st);
    return st;
}

// ---- FEFLOW07 single-player match setup + FEFLOW08 stadium select (pass 13 §B) -----------------
namespace {
constexpr int kStadTiles = 10; // mp00..mp09 map markers (FEFLOW08.LAY)
// mpNN -> LAY help indices (dumped from the members: mp00=4621 .. mp09=4630, mp05=4688). Those
// indices are OOR in every shipped FC*.BIN bank (all 2031 strings) - they address the engine's
// internal runtime table. Port-side stadium names = the verified FCENG stadium strings sorted
// alphabetically to mirror the sequential help-index block [H]: La Beaujoire(965), La Mosson(966),
// Parc Lescure(1315), Parc des Princes(1316), Stade Félix Bollaert(1750), Stade Geoffroy
// Guichard(1751), Stade Vélodrome(1752), Stade de France(1753), Stade de Gerland(1754), Toulouse(1897).
constexpr std::array<int, kStadTiles> kStadNameIdx{965, 966, 1315, 1316, 1750, 1751, 1752, 1753, 1754, 1897};
// FEFLOW07 bar-row captions: shtg/pssg/rnng/dfce/ovrl carry soup textids 429-433 (FC strings
// "Confirms selection..."/"Congratulations..." = other contexts); the real captions live at these
// FCENG indices [C]: Shooting(1691) Passing(1330) Speed(1741) Defence(573) Overall(1278).
constexpr std::array<int, 5> kSetupRowCaptions{1691, 1330, 1741, 573, 1278};
} // namespace

bool FeFlow::openSetup07() {
    if (!haveSetup07_) { lastError_ = "FEFLOW07.LAY is absent from this tree"; return false; }
    setup07View_ = true;
    hubView_ = false; // the wedge chain leaves the hub until Esc/launch (same as FEFLOW15 entry)
    const int n = std::max<int>(1, int(poolIds_.size())); // sides start at adjacent pool entries [H]
    s07Left_ = std::clamp(s07Left_, 0, n - 1);
    s07Right_ = std::clamp(s07Right_, 0, n - 1);
    if (s07Left_ == s07Right_) s07Right_ = (s07Left_ + 1) % n;
    return true;
}

bool FeFlow::openStad08() {
    if (!haveStad08_) { lastError_ = "FEFLOW08.LAY is absent from this tree"; return false; }
    stad08View_ = true;
    s08Sel_ = std::clamp(s08Sel_, 0, kStadTiles - 1);
    return true;
}

// FCDB record for a cup-pool index (team-id lookup in the full DBI team table).
const formats::FcdbTeam* FeFlow::poolTeamFor(int poolIdx) const {
    if (!fcdb_ || poolIds_.empty()) return nullptr;
    const std::size_t i = std::size_t(poolIdx) % poolIds_.size();
    const auto it = std::ranges::find(fcdb_->teams, poolIds_[i], &formats::FcdbTeam::id);
    return it == fcdb_->teams.end() ? nullptr : &*it;
}

// Five skill rows (Shooting/Passing/Running/Defence/Overall) as bar fractions on the widget's
// maxbarsize=10 domain: Shooting = avg(shotPower[12], shotAccuracy[15]); Passing = passing[8];
// Running = avg(speed[14], acceleration[1]); Defence = tackle[16]; Overall = mean of all 17
// squad nibbles; nibble scale 0..15 -> *2/3 rounded (game value = nibble*4+39).
std::array<int, 5> FeFlow::teamBars(const formats::FcdbTeam* t) const {
    std::array<int, 5> out{2, 2, 2, 2, 2}; // deterministic fixture when FCDB is absent [H]
    if (!fcdb_ || !t) return out;
    auto avg = [&](std::initializer_list<int> idxs) {
        int n2 = 0, d2 = 0;
        for (const std::uint16_t pid : t->roster) {
            if (pid == 0) continue;
            const auto it = std::ranges::find(fcdb_->players, std::uint16_t(pid), &formats::FcdbPlayer::id);
            if (it == fcdb_->players.end()) continue;
            for (const int i : idxs) n2 += it->attributes[std::size_t(i)];
            d2 += int(idxs.size());
        }
        if (!d2) return -1;
        return std::clamp(int(double(n2) / double(d2) * 2.0 / 3.0 + 0.5), 0, 10); // nibble -> bar [0,10]
    };
    const int v0 = avg({12, 15}), v1 = avg({8}), v2 = avg({14, 1}), v3 = avg({16});
    int allNum = 0, allDen = 0;
    for (const std::uint16_t pid : t->roster) {
        if (pid == 0) continue;
        const auto it = std::ranges::find(fcdb_->players, std::uint16_t(pid), &formats::FcdbPlayer::id);
        if (it == fcdb_->players.end()) continue;
        for (const std::uint8_t a : it->attributes) allNum += a;
        allDen += 17;
    }
    const int v4 = allDen ? std::clamp(int(double(allNum) / double(allDen) * 2.0 / 3.0 + 0.5), 0, 10) : -1;
    if (v0 >= 0) out[0] = v0;
    if (v1 >= 0) out[1] = v1;
    if (v2 >= 0) out[2] = v2;
    if (v3 >= 0) out[3] = v3;
    if (v4 >= 0) out[4] = v4;
    return out;
}

// FEFLOW07 composite: whitelist art (back collage, titl logo, aflag anim, nav icons, ball arrows,
// shields, balls, help pane) + the runtime layer the engine paints from live state: per-side team
// names (ltnm/rtnm textid=0), FNNN shields replacing f008/f009 defaults, FC-bank row captions and
// squad-skill bars (lbNN/rbNN sbars{lr}.fsh#bar0 filled to fraction). Two-axis grammar: UP/DOWN =
// side cursor (left/right team), LEFT/RIGHT (fotl/fotr arrows ltlb..rtrb) cycles that side's pool.
FeDrawStats FeFlow::drawSetup07(render::SpriteBatch& ui) const {
    FeDrawStats st = presentation::composite(setup07_, ui, const_cast<ScreenArt&>(art_), text_.get(), &setup07Visible_);
    // Side cursor: yellow outline over the name/shield/ball region of the focused team.
    const float x = s07Row_ == 0 ? 64.f : 320.f; // measured ltnm@82 rtnm@340 region boxes
    const std::uint32_t c = render::packRgba(255, 224, 96, 255);
    ui.draw(0, {x, 172.f, 216.f, 2.f}, {}, c), ui.draw(0, {x, 268.f, 216.f, 2.f}, {}, c);
    ui.draw(0, {x, 172.f, 2.f, 98.f}, {}, c), ui.draw(0, {x + 214.f, 172.f, 2.f, 98.f}, {}, c);
    const auto drawSide = [&](const char* nmName, const char* shName, int poolIdx) {
        if (const formats::LayoutObject* o = setup07_.object(shName)) { // FNNN shield over the f008/f009 default
            if (!poolIds_.empty() && std::size_t(poolIdx) < poolFlagFile_.size() && !poolFlagFile_[std::size_t(poolIdx)].empty()) {
                const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", poolFlagFile_[std::size_t(poolIdx)], "f000", -1);
                if (sh.tex) ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
            }
        }
        const formats::LayoutObject* o = setup07_.object(nmName); // team name caption (runtime-populated slot)
        if (!o || !text_ || !text_->ready()) return;
        const formats::FfnFont* f = text_->face(4);
        render::TextureId ftex = text_->faceTexture(4);
        if (!f || !ftex) { f = text_->font(); ftex = text_->texture(); }
        if (!f || !ftex) return;
        std::string label;
        if (!poolIds_.empty() && std::size_t(poolIdx) < poolNames_.size()) label = poolNames_[std::size_t(poolIdx)];
        if (label.empty()) label = "team " + std::to_string(poolIdx);
        for (const auto& q : presentation::layoutText(*f, label, float(o->x), float(o->y), 14.f, render::packRgba(235, 235, 235, 255)))
            ui.draw(ftex, q.dst, q.uv, q.rgba);
        ++st.texts;
    };
    drawSide("ltnm", "shdl", s07Left_);
    drawSide("rtnm", "shdr", s07Right_);
    // Row captions (FC-bank strings) centred at the shtg..ovrl geometry x=300 y=347+15i.
    if (text_ && text_->ready()) {
        const formats::FfnFont* f = text_->face(4);
        render::TextureId ftex = text_->faceTexture(4);
        if (!f || !ftex) { f = text_->font(); ftex = text_->texture(); }
        if (f && ftex) {
            for (int i = 0; i < 5; ++i) {
                const std::string_view s = text_->stringAt(std::size_t(kSetupRowCaptions[std::size_t(i)]));
                if (s.empty()) continue;
                for (const auto& q : presentation::layoutText(*f, s, 300.f, 347.f + 15.f * float(i), 12.f, render::packRgba(235, 235, 235, 255)))
                    ui.draw(ftex, q.dst, q.uv, q.rgba);
                ++st.texts;
            }
        }
    }
    // Volume bars: composite painted the full-length bar0 quads (whitelisted lbNN/rbNN); clip to
    // the FCDB squad-skill fraction [H: engine fills from live player data, port uses static FCDB].
    const auto paintBars = [&](const bool left, int poolIdx) {
        const std::array<int, 5> bars = teamBars(poolTeamFor(poolIdx));
        for (int i = 0; i < 5; ++i) {
            const formats::LayoutObject* o = setup07_.object(std::format("{}{:02d}", left ? "lb" : "rb", i + 1));
            if (!o) continue;
            const float x = float(o->x), y = float(o->y);
            const float w = float(o->width > 0 ? o->width : 154), h = float(o->height > 0 ? o->height : 16);
            ui.draw(0, {x, y, w, h}, {}, render::packRgba(20, 20, 40, 160)); // track under the fill
            const float frac = std::clamp(float(bars[std::size_t(i)]) / 10.f, 0.08f, 1.f);
            if (const std::string* tag = o->prop("shapeid")) {
                const std::string stem = [&] {
                    std::string f2 = o->fname;
                    const auto dot = f2.find_last_of('.');
                    if (dot != std::string::npos) f2 = f2.substr(0, dot);
                    std::transform(f2.begin(), f2.end(), f2.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                    return f2;
                }();
                const std::string vpath = o->prop("vpath") ? *o->prop("vpath") : std::string("..\\global\\");
                const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape(vpath, stem, *tag, -1);
                if (sh.tex) { ui.draw(sh.tex, {x, y, w * frac, h}, {0.f, 0.f, frac, 1.f}, 0xFFFFFFFFu); continue; }
            }
            ui.draw(0, {x, y, w * frac, h}, {}, render::packRgba(96, 224, 128, 220)); // flat fill when art absent
        }
    };
    paintBars(true, s07Left_);
    paintBars(false, s07Right_);
    drawCursor(ui, st);
    return st;
}

// FEFLOW08 composite: whitelist art (back map collage, titl logo, aflag, nav icons, flgp pole,
// nslt/nsrt arrows, sint/wint/hint fake-interface buttons, help pane) + the runtime layer: mpNN
// tile markers (releasedshapeid=null in LAY = engine-drawn), stadium name caption on stme and in
// the help pane. Cursor navigation cycles the 10 map tiles (nslt/nsrt two-axis arrows).
FeDrawStats FeFlow::drawStad08(render::SpriteBatch& ui) const {
    FeDrawStats st = presentation::composite(stad08_, ui, const_cast<ScreenArt&>(art_), text_.get(), &stad08Visible_);
    // Tile markers: the LAY mpNN boxes carry no shape key; engine draws a ball icon per tile. Port
    // uses the global ball sprite, highlighted ring on the cursor tile (flat quad fallback [H]).
    for (int i = 0; i < kStadTiles; ++i) {
        const formats::LayoutObject* o = stad08_.object(std::format("mp{:02d}", i));
        if (!o) continue;
        const float x = float(o->x), y = float(o->y), w = float(o->width > 0 ? o->width : 43), h = float(o->height > 0 ? o->height : 43);
        const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", "ball", "b000", -1);
        if (sh.tex) ui.draw(sh.tex, {x + w * 0.25f, y + h * 0.25f, w * 0.5f, h * 0.5f}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
        else ui.draw(0, {x + w * 0.35f, y + h * 0.35f, w * 0.3f, h * 0.3f}, {}, render::packRgba(240, 216, 96, 255));
        ++st.shapes;
        if (i == s08Sel_) { // cursor ring around the focused tile
            const std::uint32_t c = render::packRgba(255, 224, 96, 255);
            ui.draw(0, {x - 2.f, y - 2.f, w + 4.f, 2.f}, {}, c), ui.draw(0, {x - 2.f, y + h, w + 4.f, 2.f}, {}, c);
            ui.draw(0, {x - 2.f, y - 2.f, 2.f, h + 4.f}, {}, c), ui.draw(0, {x + w, y - 2.f, 2.f, h + 4.f}, {}, c);
        }
    }
    const std::string_view name = text_ ? text_->stringAt(std::size_t(kStadNameIdx[std::size_t(s08Sel_)])) : std::string_view{};
    if (!name.empty() && text_ && text_->ready()) {
        const formats::FfnFont* f = text_->face(4);
        render::TextureId ftex = text_->faceTexture(4);
        if (!f || !ftex) { f = text_->font(); ftex = text_->texture(); }
        if (f && ftex) {
            for (const auto& q : presentation::layoutText(*f, name, 320.f, 191.f, 14.f, render::packRgba(245, 235, 200, 255)))
                ui.draw(ftex, q.dst, q.uv, q.rgba);
            ++st.texts; // stme slot
            if (const formats::LayoutObject* hp = stad08_.object("help")) {
                for (const auto& q : presentation::layoutText(*f, name, float(hp->x) + 12.f, float(hp->y) + 4.f, 13.f, render::packRgba(235, 235, 235, 255)))
                    ui.draw(ftex, q.dst, q.uv, q.rgba);
                ++st.texts; // help-pane caption (helppaneid semantics)
            }
        }
    }
    drawCursor(ui, st);
    return st;
}
// ---- FEFLOW43 management grid + feflow41 LOAD / feflow42 SAVE lists (MENU_PARITY.md rows 29/30) -----
bool FeFlow::openMgmtGrid(int poolIdx) {
    if (!haveMgmt43_) { lastError_ = "FEFLOW43.LAY is absent from this tree"; return false; }
    mgmtView_ = true;
    hubView_ = false; // standalone board: leaves every other view (openSetup07 pattern)
    const int n = std::max<int>(1, int(poolIds_.size()));
    mgmtTeam_ = std::clamp(poolIdx, 0, n - 1);
    mgmtRow_ = 0;
    return true;
}

bool FeFlow::openLoadGame() {
    if (!haveLoad41_) { lastError_ = "FEFLOW41.LAY is absent from this tree"; return false; }
    lsView_ = 41; hubView_ = false; lsRow_ = 0; lsConfirm_ = 0;
    return true;
}

bool FeFlow::openSaveGame() {
    if (!haveSave42_) { lastError_ = "FEFLOW42.LAY is absent from this tree"; return false; }
    lsView_ = 42; hubView_ = false; lsRow_ = 0; lsConfirm_ = 0;
    return true;
}

// Sorted SLOT%.2d.SAV indices in the port's slot directory (app/saveload.cpp defaultSlotDir).
std::vector<int> FeFlow::slotFiles() const {
    std::vector<int> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(defaultSlotDir(), ec)) {
        if (!e.is_regular_file(ec) || ec) continue;
        const std::string n = e.path().filename().string(); // "SLOT07.SAV" (saveload.cpp naming)
        if (n.size() == 10 && n.starts_with("SLOT") && n.ends_with(".SAV")) { // SLOT%02d.SAV = 4+2+4
            auto digits = std::string_view(n).substr(4, 2);
            if (std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; }))
                out.push_back(std::stoi(std::string(digits)));
        }
    }
    std::ranges::sort(out);
    return out;
}

// Deterministic gate fixture (documented [H] in SAVESLOT_WC98.md terms): two scenario states with
// the fixed tournament seeds, written only when the directory holds no slots.
void FeFlow::ensureFixtureSlots() {
    if (!slotFiles().empty()) return; // directory carries slots already: leave user data untouched
    std::error_code ec;
    fs::create_directories(defaultSlotDir(), ec);
    const auto mk = [](std::uint32_t seed, std::uint32_t champ) {
        TournamentState st;
        st.seed = seed;
        st.champion = champ; // group-phase-only fixture: 16 played fixtures -> ko tree stays open
        for (int i = 0; i < 32; ++i) st.groups[std::size_t(i)] = std::uint16_t(4 + 2 * i); // deterministic pool ids
        const int nGroup = 16;
        st.groupResults.reserve(std::size_t(nGroup));
        for (int m = 0; m < nGroup; ++m) {
            tournament::MatchResult r;
            r.teamA = st.groups[std::size_t(m % 32)];
            r.teamB = st.groups[std::size_t((m + 1) % 32)];
            r.goalsA = std::uint8_t(m % 3);
            r.goalsB = std::uint8_t((m / 3) % 4);
            st.groupResults.push_back(r);
        }
        return st;
    };
    try {
        saveTournament(defaultSlotDir() / formats::slotFileName(0), mk(0x98D19A8, tournament::kNoTeam));
        saveTournament(defaultSlotDir() / formats::slotFileName(1), mk(0x5EED21, 4)); // slot 1: champion decided [H] fixture marker
    } catch (const std::exception&) { // read-only tree / unwritable dir: the gate prints its own failure
    }
}

// FCDB record for a grid row of the active team (poolTeamFor + fallback to head team, cfgTeam model).
const formats::FcdbPlayer* FeFlow::gridPlayer(int rosterIdx) const {
    if (!fcdb_) return nullptr;
    const formats::FcdbTeam* t = poolTeamFor(mgmtTeam_);
    if (!t && !fcdb_->teams.empty()) t = &fcdb_->teams.front(); // empty pool: head team (cfgTeam fallback)
    if (!t) return nullptr;
    const int pid = std::size_t(rosterIdx) < t->roster.size() ? t->roster[std::size_t(rosterIdx)] : 0;
    if (pid == 0) return nullptr; // empty squad slot
    const auto it = std::ranges::find(fcdb_->players, std::uint16_t(pid), &formats::FcdbPlayer::id);
    return it == fcdb_->players.end() ? nullptr : &*it;
}

int FeFlow::gridRosterSize() const {
    if (!fcdb_) return 0;
    const formats::FcdbTeam* t = poolTeamFor(mgmtTeam_);
    if (!t && !fcdb_->teams.empty()) t = &fcdb_->teams.front();
    if (!t) return 0;
    int n = 0;
    for (const std::uint16_t id : t->roster)
        if (id != 0) ++n; // 0 = empty squad slot
    return n;
}

void FeFlow::lstbPage(int dir) {
    const int rows = 10, n = std::max<int>(1, int(slotFiles().size()));
    const int page = std::clamp(lsRow_ / rows + dir, 0, (n - 1) / rows); // sbar pan semantics [H]
    lsRow_ = page * rows;
}

// feflow41 Enter: load the focused slot through the existing verify path (app/saveload.cpp).
void FeFlow::loadSelectedSlot() {
    const std::vector<int> files = slotFiles();
    if (lsRow_ >= int(files.size())) return; // empty row: no-op
    const int idx = files[std::size_t(lsRow_)];
    try {
        const TournamentState st = loadTournament(defaultSlotDir() / formats::slotFileName(idx));
        const auto f = summaryFields(st);
        std::printf("fe: slot %02d loaded (mode %u group-matches %u ko-nodes %u seed 0x%X)\n",
                    idx, unsigned(f[0]), unsigned(f[1]), unsigned(f[2]), unsigned(f[4])); // gate evidence line (no <print> on gcc-13)
    } catch (const std::exception& e) {
        lastError_ = std::format("slot {:02d}: {}", idx, e.what());
        std::fprintf(stderr, "fe: slot %02d load FAILED: %s\n", idx, e.what());
    }
}

// feflow42 Enter: write the current scenario state to the focused slot (existing save path),
// then show the tick confirm icon; cros (idle) -> tick. Rows over existing slots overwrite in place [H].
void FeFlow::saveSelectedSlot() {
    const std::vector<int> files = slotFiles();
    // Row domain: existing slots + one append row so a fresh directory can always grow.
    const int idx = lsRow_ < int(files.size()) ? files[std::size_t(lsRow_)]
                                               : (files.empty() ? 0 : files.back() + 1);
    TournamentState st;
    st.seed = 0x98D19A8; // scenario state (same deterministic fixture as the gate lane) [H]
    for (int i = 0; i < 32; ++i) st.groups[std::size_t(i)] = std::uint16_t(4 + 2 * i);
    try {
        saveTournament(defaultSlotDir() / formats::slotFileName(idx), st);
        lsConfirm_ = 1; // 'tick' icon state (tic1 chain) after the write
        std::printf("fe: saved slot %02d\n", idx); // gate evidence line (printf lane)
    } catch (const std::exception& e) {
        lastError_ = std::format("slot {:02d}: {}", idx, e.what());
        std::fprintf(stderr, "fe: slot %02d save FAILED: %s\n", idx, e.what());
    }
}

// FEFLOW43 composite + runtime layer. The engine fills the v/a/o/py animseq surfaces per page;
// the port paints the same content as overlays over the widget slots (rows are vNNN/pos pairs):
// name caption "#shirt name" (FCDBP names), FC-bank position label (roles list order in
// fifa98edit: GK SW LB CB RB LM CM RM LF CF RF RCB LCB RCM LCM; labels verified in FCENG.BIN —
// GK=810 SW=1801 RB=1531 LB=979 RM=1534 LM=982 RF=1533 LF=981 MF=1070 FW=751, CB/RCB/LCB share
// the sweeper caption [H]), and the aNNN icon columns from FE/PACK/FEFLOW73 (redc/yell/but1..3):
// selection = starting-slot marker, bio/fatigue = state tiles, card columns driven by the FCDB
// aggression nibble (attributes[0], 12+ -> red card) [H]. sbar thumb pans the four-row window.
FeDrawStats FeFlow::drawMgmtGrid(render::SpriteBatch& ui) const {
    FeDrawStats st = presentation::composite(mgmt43_, ui, const_cast<ScreenArt&>(art_), text_.get(), &mgmt43Visible_);
    const int n = gridRosterSize();
    constexpr int kWin = 4; // v000..v003 row slots on screen (window page)
    const int pages = std::max(1, (n + kWin - 1) / kWin);
    const int page = std::clamp(mgmtRow_ / kWin, 0, pages - 1);
    // Row cursor strip: the oNNN overlay geometry (x=168 w=381 h=22 at y=150+20r) in menu yellow.
    if (n > 0) {
        const int slot = mgmtRow_ % kWin;
        const std::uint32_t c = render::packRgba(255, 224, 96, 255);
        ui.draw(0, {168.f, 150.f + 20.f * slot, 381.f, 2.f}, {}, c);
        ui.draw(0, {168.f, 170.f + 20.f * slot, 381.f, 2.f}, {}, c);
    }
    for (int r = 0; r < kWin; ++r) {
        const int idx = page * kWin + r;
        const formats::FcdbPlayer* pl = gridPlayer(idx);
        if (const formats::LayoutObject* w = mgmt43_.object(std::format("v00{}", r))) { // name row slot
            std::string cap;
            if (pl) {
                cap = std::format("#{}", pl->shirtNumber);
                const auto it = std::ranges::find(fcdb_->players, pl->id, &formats::FcdbPlayer::id);
                const std::size_t pidx = std::size_t(std::distance(fcdb_->players.begin(), it));
                if (it != fcdb_->players.end() && pidx < fcdbPlayerNames_.size() && !fcdbPlayerNames_[pidx].empty())
                    cap += " " + fcdbPlayerNames_[pidx];
            }
            presentation::paintRowLabel(ui, *w, cap, *text_, 0, idx == mgmtRow_ && n > 0, nullptr, st);
        }
        if (const formats::LayoutObject* w = mgmt43_.object(std::format("v00{}", 4 + r))) { // FC pos label slot
            static constexpr int kPosLabel[15] = {810, 1801, 979, 1801, 1531, 982, 1070, 1534,
                                                  981, 751, 1533, 1801, 1801, 1070, 1070}; // [C] see header note
            std::string_view lbl;
            if (pl && text_ && text_->ready()) {
                const int role = std::clamp(int(pl->role), 0, 14);
                lbl = text_->stringAt(std::size_t(kPosLabel[std::size_t(role)]));
            }
            presentation::paintRowLabel(ui, *w, lbl, *text_, 4, false, nullptr, st);
        }
        if (!pl) continue;
        // aNNN icon columns (widget "a" + column(0..4) + row): tag per column [H], drawn at the slot.
        const int aggr = pl->attributes[0]; // nibble 0..15, scale 39+4*n; 12+ ~ aggressive [H]
        static constexpr std::array<const char*, 5> kTags{"but1", "but2", "but3", "", ""};
        for (int col = 0; col < 5; ++col) {
            const formats::LayoutObject* w = mgmt43_.object(std::format("a{}{}", col, r)); // a0N + row digit
            if (!w) continue;
            std::string_view tag = kTags[std::size_t(col)];
            if (col >= 3) { // match-card / total-card columns: discipline tiles from FCDB aggression [H]
                tag = aggr >= 12 ? "redc" : aggr >= 9 ? "yell" : "";
                if (col == 4 && aggr < 9) continue; // totals mirror the match column, no tile under booking
            } else if (col == 0 && pl->startingSlot == 0) {
                tag = ""; // not in the starting eleven: selection slot stays blank [H]
            }
            if (tag.empty()) continue;
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\feflow73\\", "feflow73", std::string(tag), -1);
            if (!sh.tex) continue;
            ui.draw(sh.tex, {float(w->x), float(w->y), 20.f, 20.f}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
            ++st.shapes;
        }
    }
    if (const formats::LayoutObject* w = mgmt43_.object("tmnm")) // team name caption on the tmnm slot
        presentation::paintRowLabel(ui, *w, poolName(mgmtTeam_), *text_, 4, false, nullptr, st);
    if (const formats::LayoutObject* t = mgmt43_.object("sbar")) // sbar pan thumb over the window pages
        presentation::paintSliderThumb(ui, *t, page, 0, pages - 1);
    drawHelpPane(mgmt43_, ui, st, "save"); // focused function button caption (help=1390) [H] default focus
    return st;
}

// feflow41/42 composite + lstb rows: directory scan of SLOT%.2d.SAV over the port slot dir
// (app/saveload.cpp), captions "SLOTnn - <summary>" through paintListRows (page=10 lineHeight=19
// fontID=5 widget keys); sbar thumb = page window. feflow42 paints 'cros' (crs1 idle) as an
// overlay over the composite's `tick` slot when nothing is confirmed, tick (tic1) when written.
FeDrawStats FeFlow::drawLoadSave(render::SpriteBatch& ui) const {
    const bool load = lsView_ == 41;
    const formats::ScreenLayout& sc = load ? load41_ : save42_;
    FeDrawStats st = presentation::composite(sc, ui, const_cast<ScreenArt&>(art_), text_.get(), load ? &load41Visible_ : &save42Visible_);
    const std::vector<int> files = slotFiles();
    if (const formats::LayoutObject* box = sc.object("lstb")) {
        const int rows = 10;
        const int page = lsRow_ / rows;
        std::vector<std::string> caps;
        for (int i = page * rows; i < (page + 1) * rows && i < int(files.size()); ++i) {
            // Caption per row: slot name + summary record facts read through the decode path so the
            // list shows real save-file state, not placeholder strings.
            std::string cap = std::format("SLOT{:02d}", files[std::size_t(i)]);
            try {
                const TournamentState stx = loadTournament(defaultSlotDir() / formats::slotFileName(files[std::size_t(i)]));
                const auto f = summaryFields(stx);
                cap += std::format("  m{} g{} k{}", f[0], f[1], f[2]); // mode, group matches, ko nodes decided
            } catch (const std::exception&) { // unreadable slot file: bare name row (engine lists it too)
            }
            caps.push_back(std::move(cap));
        }
        presentation::paintListRows(ui, *box, std::span(caps), caps.empty() ? -1 : lsRow_ - page * rows, *text_, 5, st);
    }
    if (const formats::LayoutObject* t = sc.object("sbar")) {
        const int pages = std::max(1, (int(files.size()) + 9) / 10);
        presentation::paintSliderThumb(ui, *t, lsRow_ / 10, 0, pages - 1);
    }
    if (!load && !lsConfirm_) { // feflow42 idle: 'cros' crs1 icon over the tick slot (widget x=417 y=370)
        if (const formats::LayoutObject* w = sc.object("cros")) {
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", "nvicons", "crs1", -1);
            if (sh.tex) { ui.draw(sh.tex, {float(w->x), float(w->y), 55.f, 35.f}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
        }
    }
    return st;
}

// ---- Between-round tournament presentation -------------------------------------------------
// Registry RE (pass 8, analysis/pass8/tourney_registry.txt): FUN_004479d0 registers the feflowNN
// scene cluster. Confirmed between-round screens: feflow35 = group standings table (gpnm + rows),
// feflow25 = quarterfinal board (tmNN participants, qtmN SF slots), feflow26 = semifinal board
// (+ wcup/wcps trophy slot), feflow27 = full bracket + chmp champion slot + 3rd-place match.
// Overlays draw the app's deterministic tournament model at the LAY's own slot positions,
// mirroring the drawTeamsGrid row-overlay pattern.
struct FeFlow::TourneyModel {
    std::array<std::uint16_t, 32> groups{};                     // group g = slots g*4..g*4+3
    std::array<std::vector<tournament::StandingsRow>, 8> tables; // sorted standings per group
    tournament::KnockoutBracket bracket{};                    // played via makeTree + seeded stream (below)
    std::array<std::uint32_t, 16> seeds{}, winners{}, losers{}; // knockout node results
    int userGroup = 0;                                          // group shown on the feflow35 board
};
FeFlow::~FeFlow() = default; // unique_ptr<TourneyModel> completes here

namespace {
constexpr std::uint32_t kPresScheduleSeed = 0x98D19A8u; // same fixed seeds as tournament_demo
constexpr std::uint32_t kPresResultSeed = 0xC0FFEE98u;  // so overlays are gate-deterministic
tournament::StandingsRow presSeedRow(std::uint16_t id, int rank) {
    tournament::StandingsRow r;
    r.teamId = id;
    r.seedRank = rank;
    return r;
}
} // namespace

int FeFlow::tourneyPresentCount() { return ensureTourney() ? kTourneyRounds : 0; }

// Builds the model from the ZWCUP pool (fixed seeds: no live match code, no RNG drift). Lazy so
// main-menu frames never touch it; returns false when assets/pool are absent (non-throwing).
bool FeFlow::ensureTourney() {
    if (haveTourney_) return true;
    if (tourney_) return false; // attempted once, tree lacks the layouts
    tourney_ = std::make_unique<TourneyModel>();
    static constexpr const char* kSpecs[kTourneyRounds] = {"FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW35.LAY",
                                                           "FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW25.LAY",
                                                           "FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW26.LAY",
                                           "FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW27.LAY"};
    if (poolIds_.size() < 32) { tourney_.reset(); return false; } // needs the full 32-team pool
    try {
        for (int i = 0; i < kTourneyRounds; ++i) {
            fs::path dir;
            const std::string text = presentation::readLayout((tree_ / kSpecs[i]).string(), dir);
            presScreens_[std::size_t(i)] = formats::ScreenLayout::build("feflow",
                formats::LayoutDocument::parse(text), nullptr, nullptr);
        }
    } catch (const std::exception&) { tourney_.reset(); return false; }
    auto& m = *tourney_;
    for (int i = 0; i < 32; ++i) m.groups[std::size_t(i)] = poolIds_[std::size_t(i)];
    const auto sched = tournament::generateSchedule(m.groups, kPresScheduleSeed);
    std::array<std::vector<tournament::MatchResult>, 8> gm{}; // played group fixtures per group
    std::array<tournament::Fixture, 16> koFix{};              // index == bracket node
    for (const auto& f : sched) {
        if (f.group >= 0)
            gm[std::size_t(f.group)].push_back({m.groups[f.home.a], m.groups[f.away.a], 0, 0});
        else
            koFix[std::size_t(f.bracketNode)] = f;
    }
    std::mt19937 rng(kPresResultSeed);
    auto play = [&rng] {
        std::uniform_int_distribution<int> goals(0, 3);
        return std::tuple{goals(rng), goals(rng), (rng() & 1u) != 0}; // a, b, penalty-home-wins
    };
    int cursor[8] = {0};
    for (const auto& f : sched)
        if (f.group >= 0) {
            const auto [a, b, pkHome] = play();
            (void)pkHome; // draws allowed in the group stage: goals only feed the table
            tournament::MatchResult& r = gm[std::size_t(f.group)][std::size_t(cursor[f.group]++)];
            r.goalsA = std::uint8_t(a);
            r.goalsB = std::uint8_t(b);
        }
    std::array<tournament::StandingsRow, 16> place{}; // [g*2 + (b-1)] = group placing b
    for (int g = 0; g < 8; ++g) {
        std::vector<tournament::StandingsRow> seeds4;
        for (int pos = 0; pos < 4; ++pos)
            seeds4.push_back(presSeedRow(m.groups[std::size_t(g * 4 + pos)], pos));
        m.tables[std::size_t(g)] = tournament::computeStandings(seeds4, gm[std::size_t(g)]);
        place[std::size_t(g * 2)] = m.tables[std::size_t(g)][0];
        place[std::size_t(g * 2 + 1)] = m.tables[std::size_t(g)][1]; // top-2 qualify (§4)
    }
    auto placeTeam = [&](const tournament::SlotRef& r) {
        return place[std::size_t(r.a * 2 + r.b - 1)].teamId;
    };
    for (int i = 0; i < 8; ++i) { // R16 fixtures feed bracket seeds 0..15
        const tournament::Fixture& f = koFix[std::size_t(i)];
        m.seeds[std::size_t(2 * i)] = placeTeam(f.home);
        m.seeds[std::size_t(2 * i + 1)] = placeTeam(f.away);
    }
    m.bracket = tournament::KnockoutBracket::makeTree(
        std::vector<std::uint32_t>(m.seeds.begin(), m.seeds.end()), true);
    for (int n = 0; n < 16; ++n) { // topological R16, QF, SF, third-place, final: play the stream
        const tournament::BracketNode& node = m.bracket.node(std::size_t(n));
        if (node.teamA == tournament::kNoTeam && node.teamB == tournament::kNoTeam) continue; // TP pre-SF guard
        auto resolve = [&](const tournament::SlotRef& r) {
            return n == int(m.bracket.thirdPlaceNode()) ? m.losers[std::size_t(r.a)]
                                                        : m.winners[std::size_t(r.a)];
        };
        const std::uint32_t h = koFix[n].home.kind == tournament::SlotRef::kGroupPlace
                                    ? placeTeam(koFix[n].home) : resolve(koFix[n].home);
        const std::uint32_t a = koFix[n].away.kind == tournament::SlotRef::kGroupPlace
                                    ? placeTeam(koFix[n].away) : resolve(koFix[n].away);
        const auto [ga, gb, pkHome] = play();
        const bool homeWin = ga > gb || (ga == gb && pkHome);
        const std::uint32_t w = homeWin ? h : a, l = homeWin ? a : h;
        if (!m.bracket.setWinner(std::size_t(n), w)) continue;
        if (node.round == tournament::KnockoutRound::kSemiFinal)
            m.bracket.setLoser(std::size_t(n), l); // SF losers -> third-place playoff
        m.winners[std::size_t(n)] = w;
        m.losers[std::size_t(n)] = l;
    }
    if (pick_ >= 0)
        for (int g = 0; g < 8; ++g)
            for (int k = 0; k < 4; ++k)
                if (m.groups[std::size_t(g * 4 + k)] == std::uint16_t(pick_)) m.userGroup = g;
    haveTourney_ = true;
    return true;
}

const char* FeFlow::tourneyPresentName(int round) const {
    static constexpr const char* kNames[kTourneyRounds] = {"FEFLOW35", "FEFLOW25", "FEFLOW26", "FEFLOW27"};
    return (round >= 0 && round < kTourneyRounds && haveTourney_) ? kNames[std::size_t(round)] : "";
}

// Composite + dynamic overlay for the boundary after `round` (0 group, 1 R16, 2 QF, 3 final).
presentation::FeDrawStats FeFlow::drawTourneyPresent(render::SpriteBatch& ui, int round) {
    if (!ensureTourney() || round < 0 || round >= kTourneyRounds) return {};
    presentation::FeDrawStats st =
        presentation::composite(presScreens_[std::size_t(round)], ui, art_, text_.get());
    const presentation::FeDrawStats ov =
        round == 0 ? drawStandings(ui, presScreens_[0], tourney_->userGroup) : drawBracketBoard(ui, round);
    st.shapes += ov.shapes; st.texts += ov.texts; st.cursors += ov.cursors; st.missing += ov.missing;
    drawCursor(ui, st);
    return st;
}

std::string FeFlow::poolName(std::uint32_t id) const {
    const auto it = std::ranges::find(poolIds_, std::uint16_t(id));
    if (it == poolIds_.end()) return "team " + std::to_string(id);
    const std::string& n = poolNames_[std::size_t(std::distance(poolIds_.begin(), it))];
    return n.empty() ? "team " + std::to_string(id) : n;
}

// FEFLOW35: standings table for the user team's group. Row slots ssnN (name), ssp/ssw/ssd/ssl
// (P W D L), ssf/ssa (GF GA), sst (points); suffix 0..3 = row, measured grid analysis/pass8.
presentation::FeDrawStats FeFlow::drawStandings(render::SpriteBatch& ui, const formats::ScreenLayout& sc, int group) const {
    presentation::FeDrawStats st;
    if (!text_ || !text_->ready() || !text_->font()) return st;
    auto line = [&](std::string_view s, float x, float y, std::uint32_t rgba) {
        for (const auto& q : presentation::layoutText(*text_->font(), s, x, y, 12.f, rgba)) {
            ui.draw(text_->texture(), q.dst, q.uv, q.rgba);
            ++st.texts;
        }
    };
    const auto& table = tourney_->tables[std::size_t(group)];
    if (const formats::LayoutObject* gpnm = sc.object("gpnm"))
        line(std::string("GROUP ") + char('A' + group), float(gpnm->x), float(gpnm->y),
             render::packRgba(255, 235, 96, 255));
    for (int pos = 0; std::size_t(pos) < table.size(); ++pos) {
        const auto& row = table[std::size_t(pos)];
        const std::string pfx = std::to_string(pos);
        if (const formats::LayoutObject* o = sc.object("ssn" + pfx))
            line(poolName(row.teamId), float(o->x), float(o->y), render::packRgba(235, 235, 235, 255));
        const std::pair<const char*, int> nums[] = {{"ssp", row.p}, {"ssw", row.w}, {"ssd", row.d},
                                                    {"ssl", row.l}, {"ssf", row.gf}, {"ssa", row.ga},
                                                    {"sst", row.pts}};
        for (const auto& [key, v] : nums)
            if (const formats::LayoutObject* no = sc.object(key + pfx))
                line(std::to_string(v), float(no->x), float(no->y), render::packRgba(255, 255, 255, 255));
    }
    return st;
}

// Knockout boards (feflow25/26/27): team names + flags at the LAY's own slots. Slot inventory
// measured from analysis/pass8 extracts: feflow25 tmNN=QF entrants (nodes 8..11 pairs), qtm0..3=
// SF slots, tfNN/qfNN = flag strips; feflow26 tm00/01=node12 pair, tm02/03=node13, wcup/wcps =
// trophy slot (WCUPF.PAK frame sprite); feflow27 tmNN=seeds, qtmN=R16 winners, stmN=QF winners,
// ftm0/1=finalists, chmp=champion (gold), 3tm0/1=third-place playoff entrants.
presentation::FeDrawStats FeFlow::drawBracketBoard(render::SpriteBatch& ui, int round) const {
    presentation::FeDrawStats st;
    if (!text_ || !text_->ready() || !text_->font()) return st;
    const auto& sc = presScreens_[std::size_t(round)];
    const auto& m = *tourney_;
    auto line = [&](const formats::LayoutObject* o, std::string_view s, std::uint32_t rgba) {
        for (const auto& q : presentation::layoutText(*text_->font(), s, float(o->x), float(o->y), 14.f, rgba)) {
            ui.draw(text_->texture(), q.dst, q.uv, q.rgba);
            ++st.texts;
        }
    };
    auto team = [&](std::string_view slot, std::uint32_t id, bool gold = false) {
        if (id == tournament::kNoTeam) return;
        if (const formats::LayoutObject* o = sc.object(slot))
            line(o, poolName(id), render::packRgba(gold ? 255 : 235, gold ? 215 : 235, gold ? 64 : 235, 255));
    };
    auto flag = [&](std::string_view slot, std::uint32_t id) {
        if (id == tournament::kNoTeam) return;
        const formats::LayoutObject* o = sc.object(slot);
        if (!o) return;
        const std::string flagFile = flagFileForTeam(id); // FNNN = FCDB record index (measured)
        if (flagFile.empty()) { ++st.missing; return; }
        const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", flagFile, "f000", -1);
        if (sh.tex) {
            ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
            ++st.shapes;
        } else
            ++st.missing;
    };
    auto w = [&](int node) { return m.winners[std::size_t(node)]; }; // played result of a bracket node
    auto pairSlot = [&](const char* pfx, int i) { return pfx + std::to_string(i); };
    if (round == 1) { // FEFLOW25: quarterfinal board - R16 done, QF entrants known
        for (int i = 0; i < 8; ++i) { // tmNN = QF participants (node 8 + i/2 slots)
            const auto& nd = m.bracket.node(std::size_t(8 + i / 2));
            const std::uint32_t id = i % 2 == 0 ? nd.teamA : nd.teamB;
            team(pairSlot("tm", i), id);
            flag(pairSlot("tf", i), id);
        }
        for (int k = 0; k < 4; ++k) { // SF slots: QF winners, unplayed at this boundary -> skipped
            team(pairSlot("qtm", k), w(8 + k));
            flag(pairSlot("qf", k), w(8 + k));
        }
    } else if (round == 2) { // FEFLOW26: semifinal board
        for (int i = 0; i < 4; ++i) {
            const auto& nd = m.bracket.node(std::size_t(12 + i / 2));
            team(pairSlot("tm", i), i % 2 == 0 ? nd.teamA : nd.teamB);
            flag(pairSlot("tf", i), i % 2 == 0 ? nd.teamA : nd.teamB);
        }
        for (int k = 0; k < 2; ++k) {
            team(pairSlot("qtm", k), w(12 + k)); // final slot: SF winner, unplayed -> skipped
            flag(pairSlot("qf", k), w(12 + k));
        }
    } else { // FEFLOW27: full bracket after the final
        for (int i = 0; i < 16; ++i) {
            team(pairSlot("tm", i), m.seeds[std::size_t(i)]);
            flag(pairSlot("tf", i), m.seeds[std::size_t(i)]);
        }
        for (int k = 0; k < 8; ++k) { team(pairSlot("qtm", k), w(k)); flag(pairSlot("qf", k), w(k)); }
        for (int s = 0; s < 4; ++s) { team(pairSlot("stm", s), w(8 + s)); flag(pairSlot("sf", s), w(8 + s)); }
        const auto& fin = m.bracket.node(14);
        team("ftm0", fin.teamA);
        team("ftm1", fin.teamB);
        flag("ff00", fin.teamA);
        flag("ff01", fin.teamB);
        const auto& tp = m.bracket.node(15);
        team("3tm0", tp.teamA);
        team("3tm1", tp.teamB);
        if (const formats::LayoutObject* o = sc.object("chmp")) { // champion: gold label at its slot
            const std::uint32_t champ = w(14);
            if (champ != tournament::kNoTeam) line(o, poolName(champ), render::packRgba(255, 215, 64, 255));
        }
    }
    // Trophy slot: WCUPF.PAK carries the pre-rendered trophy frames (SHPI sprites); draw the first
    // present frame at the wcps slot. The runtime PCMD/3D sequence stays a known polish item.
    if (const formats::LayoutObject* wcps = sc.object("wcps")) {
        for (int idx = 0; idx < 8; ++idx) {
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", "wcupf", "", idx);
            if (sh.tex) {
                ui.draw(sh.tex, {float(wcps->x), float(wcps->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu);
                ++st.shapes;
                break;
            }
        }
    }
    return st;
}
// ---- World-cup family screens (FEFLOW18/23/24/35; MENU_NAV_WC98.md rank-2) -------------------------
// Keyloop RE in analysis/pass12/wcfamily_notes.txt. FEFLOW18 is the finals-draw board: 32 tmNN zone slots
// (two columns x=40/x=285, y=128..413) + gmNN group cells; the 'rand' button runs a staged random draw
// (FUN_004cf150 + per-slot visibility refresh, counter cap 99 -> auto-advance). The tm/gm cells are
// objecttype 0/4 shapeID-null dynamic widgets: engine-painted content, so the port overlays pool names
// and group letters at the LAY's own slots (drawTeamsGrid pattern) with a row-cursor pane. FEFLOW23/24
// are R16 bracket boards over TourneyModel; FEFLOW35 is the 8-group standings board, nglt/ngrt = mod-8
// group page cursor (decoded 1:1).
bool FeFlow::openWcCfg(int screenId) {
    if (!haveWc_) return false;
    const formats::ScreenLayout* sc = screenId == 18 ? (wc18_.object("tm00") ? &wc18_ : nullptr)
                          : screenId == 23 ? (wc23_.object("strk") ? &wc23_ : nullptr)
                          : screenId == 24 ? (wc24_.object("strk") ? &wc24_ : nullptr)
                          : screenId == 35 ? (wc35_.object("nglt") ? &wc35_ : nullptr) : nullptr;
    if (!sc) return false; // layout absent from this tree: the scene stays closed
    wcCfg_ = screenId;
    cfgName_ = "FEFLOW" + std::to_string(screenId);
    wcRow_ = 0;
    return true;
}

// Deterministic zones order for FEFLOW18: the ZWCUP pool in draw order, re-shuffled per 'rand' press
// (the engine's random draw is a live RNG; the port uses fixed seeds so gate captures stay stable [H]).
std::vector<int> FeFlow::wcZones() const {
    std::vector<int> idx(std::min<std::size_t>(32, poolIds_.size()));
    for (int i = 0; i < int(idx.size()); ++i) idx[std::size_t(i)] = i;
    std::mt19937 rng(0x98C0DE5u ^ (static_cast<std::uint32_t>(wcRandCount_) + 1u) * 0x9E3779B9u);
    std::ranges::shuffle(idx, rng);
    return idx;
}

presentation::FeDrawStats FeFlow::drawWcFamily(render::SpriteBatch& ui) const {
    using namespace presentation;
    const formats::ScreenLayout* sc = nullptr;
    const std::vector<std::string>* vis = nullptr;
    switch (wcCfg_) {
        case 18: sc = &wc18_; vis = &wc18Visible_; break;
        case 23: sc = &wc23_; vis = &wc23Visible_; break;
        case 24: sc = &wc24_; vis = &wc24Visible_; break;
        case 35: sc = &wc35_; vis = &wc35Visible_; break;
        default: return {};
    }
    FeUiState uist; // nav-icon hover/press state (widgetHit geometry, composite shape-state chain)
    ++animTicks_;
    uist.animMs = animTicks_ * 16u;
    for (const auto& o : sc->objects)
        if (o.shown() && widgetHit(o, float(ptrX_), float(ptrY_))) { uist.hover = o.name; break; }
    if (ptrDown_) uist.pressed = uist.hover;
    FeDrawStats st = composite(*sc, ui, const_cast<ScreenArt&>(art_), text_.get(), vis, &uist);

    auto lineAt = [&](const formats::LayoutObject* o, std::string_view s, float px, std::uint32_t rgba) {
        if (!o || s.empty() || !text_ || !text_->ready() || !text_->font()) return;
        for (const auto& q : layoutText(*text_->font(), s, float(o->x), float(o->y), px, rgba))
            ui.draw(text_->texture(), q.dst, q.uv, q.rgba);
        ++st.texts;
    };
    const std::uint32_t kWhite = render::packRgba(235, 235, 235, 255);
    const std::uint32_t kGold = render::packRgba(255, 224, 96, 255);
    // strk/stsc/rand buttons: LAY releasedshapeid=null placeholders; the engine shows the hilited/pushed
    // shape on press. The port paints the hilited art as the resting face (flat quad when absent [H]).
    const auto button = [&](const char* name, std::string_view tag) {
        const formats::LayoutObject* o = sc->object(name);
        if (!o) return;
        std::string stem = o->fname;
        if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) stem = stem.substr(0, dot);
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        const std::string vpath = o->prop("vpath") ? *o->prop("vpath") : std::string();
        const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape(vpath, stem, std::string(tag), -1);
        const float x = float(o->x), y = float(o->y);
        const float w = float(o->width > 0 ? o->width : sh.w), h = float(o->height > 0 ? o->height : sh.h);
        if (sh.tex) { ui.draw(sh.tex, {x, y, w, h}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
        else ui.draw(0, {x, y, w, h}, {}, render::packRgba(40, 52, 96, 200)); // flat button plate [H]
    };
    const auto pane = [&](const formats::LayoutObject* o) { // row-cursor hilite (league-family pane model)
        if (!o) return;
        ui.draw(0, {float(o->x), float(o->y), float(o->width > 0 ? o->width : 100),
                    float(o->height > 0 ? o->height : 19)}, {}, render::packRgba(255, 224, 96, 48));
    };

    if (wcCfg_ == 18) {
        const auto zones = wcZones();
        for (int i = 0; std::size_t(i) < zones.size(); ++i) {
            lineAt(sc->object("tm" + std::format("{:02d}", i)), poolName(poolIds_[std::size_t(zones[std::size_t(i)])]), 14.f, kWhite);
            lineAt(sc->object("gm" + std::format("{:02d}", i)), std::string(1, char('A' + i / 4)), 14.f, kGold);
        }
        pane(sc->object("tm" + std::format("{:02d}", wcRow_)));
        button("rand", "btup"); // 'rand' = the draw-refresh control (decoded press semantics)
    }
    if (wcCfg_ == 23 || wcCfg_ == 24) {
        if (!haveTourney_) const_cast<FeFlow*>(this)->ensureTourney(); // lazy model (bracket data source)
        if (haveTourney_) {
            const auto& m = *tourney_;
            for (int n = 0; n < 8; ++n) { // R16: pair tmN(x=140)/tm(N+8)(x=439), flags tfNN, winner qtmN
                const tournament::BracketNode& node = m.bracket.node(std::size_t(n));
                // Flags first, team names last: the stf* kit shapes are full shield cards (like the
                // pick-grid overlays) that paint over the tm/qtm slots; the engine keeps the name layer
                // above them. Slot plates are light silver, so ink is dark navy [H].
                const std::uint32_t kInk = render::packRgba(12, 18, 48, 255);
                const auto flag = [&](std::string_view slot, std::uint32_t id) {
                    const formats::LayoutObject* o = sc->object(slot);
                    if (!o || id == tournament::kNoTeam) return;
                    const std::string ff = flagFileForTeam(id);
                    if (ff.empty()) { ++st.missing; return; }
                    const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", ff, "f000", -1);
                    if (sh.tex) { ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu); ++st.shapes; }
                    else ++st.missing;
                };
                flag("tf" + std::format("{:02d}", n), node.teamA);
                flag("tf" + std::format("{:02d}", n + 8), node.teamB);
                const std::uint32_t w = m.winners[std::size_t(n)];
                flag("qf" + std::format("{:02d}", n), w);
                lineAt(sc->object("tm" + std::format("{:02d}", n)), poolName(node.teamA), 14.f, kInk);
                lineAt(sc->object("tm" + std::format("{:02d}", n + 8)), poolName(node.teamB), 14.f, kInk);
                lineAt(sc->object("qtm" + std::to_string(n)), poolName(w), 14.f,
                       render::packRgba(120, 16, 24, 255)); // winner names: dark red on light plate [H]
            }
        }
        button("strk", "hic1"); // 'strk' = start-competition press; 'stsc' = stats view (decoded stores)
        button("stsc", "hic2");
        if (const formats::LayoutObject* f = sc->object(wcRow_ == 0 ? "strk" : "stsc")) { // port focus ring [H]
            const float x = float(f->x), y = float(f->y), w = float(f->width > 0 ? f->width : 79), h = float(f->height > 0 ? f->height : 48);
            ui.draw(0, {x, y, w, 2.f}, {}, kGold), ui.draw(0, {x, y + h - 2.f, w, 2.f}, {}, kGold);
            ui.draw(0, {x, y, 2.f, h}, {}, kGold), ui.draw(0, {x + w - 2.f, y, 2.f, h}, {}, kGold);
        }
    }
    if (wcCfg_ == 35) {
        if (!haveTourney_) const_cast<FeFlow*>(this)->ensureTourney(); // standings model + nglt/ngrt domain
        if (haveTourney_) {
            const FeDrawStats ov = drawStandings(ui, *sc, wcGroup_); // group caption + ssnN/ss{p..a}NN rows
            st.shapes += ov.shapes; st.texts += ov.texts; st.missing += ov.missing;
        }
        pane(sc->object("ssn" + std::to_string(wcRow_))); // table row cursor (port [H])
        button("strk", "hic1"); // feflow35 'strk' is an ack-only press (decoded no-op), art stays visible
        button("stsc", "hic2");
    }
    drawHelpPane(*sc, ui, st, uist.hover); // context help captions (help= FC keys), same model as the hub
    drawCursor(ui, st);
    return st;
}


// ================================================================================================
// Hub screen (Phase 1) + options/save-load/credits views (Phases 2-4). Evidence: Feflow06.lay
// widget table (button objectnames objects 6..19, help= FC captions, helppaneid=help), the global-
// key hub table FUN_00445664 EDX payload -> feflow scenes (MENU_NAV_WC98.md §3), and the scene->
// scene edges in analysis/pass12/edges_table.txt. feflow15.lay key handler 0x44f22c: event 2001,
// widget tags 'quit'/'deft'/'back'/'ops2'; Feflow89.lay slot buttons b000..b005 (USER/SLOTnn.SAV).

std::string_view FeFlow::hubButtonName(int index) {
    return index >= 0 && index < int(kHubButtons.size()) ? std::string_view{kHubButtons[std::size_t(index)]} : std::string_view{};
}
int FeFlow::hubButtonIndex(std::string_view name) {
    const auto it = std::ranges::find_if(kHubButtons, [name](const char* c) { return formats::iequals(c, name); });
    return it == kHubButtons.end() ? -1 : int(std::distance(kHubButtons.begin(), it));
}

void FeFlow::resetCursor() {
    if (haveHub_) hubSel_ = hubButtonIndex("fe07");
    sel_ = 0;
}

void FeFlow::selectNav(int entry) {
    // Gate nav-axis mapping shared by hub and FEFLOW05 fallback: 0 fe07, 1 league list direct,
    // 2 fe11 (team grid), 3 fe32 (practise). Keeps fe_nav_* / fe_league gates valid on both menus.
    if (!onHub()) {
        sel_ = std::clamp(entry, 0, kFeEntryCount - 1);
        return;
    }
    switch (entry) {
        case 0: hubSel_ = hubButtonIndex("fe07"); break;
        case 1: if (haveLeague_) { leagueView_ = true; } break; // FEFLOW73 list opens directly
        case 2: hubSel_ = hubButtonIndex("fe11"); break;
        case 3: hubSel_ = hubButtonIndex("fe32"); break;
        default: break;
    }
}

// Enter/click on the hub button `index`: scene-graph edges (edges_table.txt). Returns the FeEntry
// launch index when the shell must start a match, -1 for in-place view changes and no-ops.
int FeFlow::hubActivate(int index) {
    const std::string_view n = hubButtonName(index);
    if (n == "fe07") {
        // Hub wedge 'fe07' payload 4 [C] FUN_00445664 0x4456B9-0x4456CC opens feflow07 (matchup);
        // its Enter chain runs the friendly config (feflow09) then the 'fe08' stadium-select edge.
        if (openSetup07()) return -1;
        if (haveSetup_) { setupScreen_ = true; setupPractise_ = false; return -1; } // feflow09 config first
        return int(FeEntry::Exhibition);
    }
    if (n == "fe11") {
        if (!poolIds_.empty()) { teamScreen_ = true; teams_.setCount(int(poolIds_.size())); return -1; }
        return int(FeEntry::Tournament); // no pool data: tournament driver launches directly
    }
    if (n == "fe32") {
        if (haveSetup_) { setupScreen_ = true; setupPractise_ = true; return -1; } // feflow50 config first
        return int(FeEntry::Practise);
    }
    if (n == "quit") { // the Windows exit flow answers the 'quit' wedge with a yes/no confirm box
        // (POPUP_LANE_SPEC.md "wire into existing confirm paths"): FEFLOW37 opens over the hub; Enter
        // on yes sets quitRequested_ (main.cpp loop check), canc/Esc returns to the hub. No popup art
        // on this tree: keep the pre-slice direct exit.
        if ((havePop37_ && openPopup("feflow37", true)) || (havePop10_ && openPopup("feflow10", true))) return -1;
        quitRequested_ = true;
        return -1;
    }
    if (n == "fe15") { openOptions(); return -1; }
    if (n == "fe41") { openSaveLoad(); return -1; }
    if (n == "cred") { openCredits(); return -1; }
    if (n == "fe33") { openHistory(); return -1; } // feflow90 world-cup history board [C] SITE 0x449dc5
    // fe90 classic, fe04 modem, fe02 network: no port simulation of those modes. fe89 team
    // management, repl highlights: same (no shipped scene driver in the port).
    return -1;
}

int FeFlow::pointerActivate() {
    using presentation::widgetHit;
    if (popKind_) { // popup buttons under the pointer: focus + activate (payload name parity)
        const formats::ScreenLayout& sc = popKind_ == 37 ? pop37_ : popKind_ == 10 ? pop10_ : popIg_;
        for (int slot = 0; slot < 2; ++slot) {
            const char* n = popupButtonName(slot);
            if (!n) continue;
            const formats::LayoutObject* o = sc.object(n);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { popSel_ = slot; activatePopup(); return -1; }
        }
        return -1; // clicks inside the popup panel never launch a match
    }
    if (setup07View_) { // ball arrows cycle the side's pool; nav icons close per their help edges [C]
        const formats::ScreenLayout& sc = setup07_;
        for (const char* n : {"ltlb", "ltrb"}) {
            const formats::LayoutObject* o = sc.object(n);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { s07Row_ = 0; adjustValue(n[2] == 'l' ? -1 : 1); return -1; }
        }
        for (const char* n : {"rtlb", "rtrb"}) {
            const formats::LayoutObject* o = sc.object(n);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { s07Row_ = 1; adjustValue(n[3] == 'l' ? -1 : 1); return -1; }
        }
        for (const char* n : {"fe06", "quit"}) { // back/exit icons: feflow07 closes to the hub
            const formats::LayoutObject* o = sc.object(n);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { setup07View_ = false; return -1; }
        }
        return -1; // clicks inside the matchup panel never launch a match from here
    }
    if (stad08View_) { // mpNN tile click moves the cursor; nslt/nsrt cycle; fe07 icon = back edge [C]
        for (int i = 0; i < kStadTiles; ++i) {
            const formats::LayoutObject* o = stad08_.object(std::format("mp{:02d}", i));
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { s08Sel_ = i; return -1; }
        }
        for (const char* n : {"nslt", "nsrt"}) {
            const formats::LayoutObject* o = stad08_.object(n);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { move(n[2] == 'l' ? -1 : 1); return -1; }
        }
        for (const char* n : {"fe07", "quit"}) {
            const formats::LayoutObject* o = stad08_.object(n);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { stad08View_ = false; return -1; }
        }
        return -1; // sint/wint/hint fake-interface buttons: cycle the value under the tile cursor [H]
    }
    if (histScreen_) { // year buttons + nav icons (LAY widgets, help= panes); feflow91 fe07 -> back
        const formats::ScreenLayout& sc = histScreen_ == 90 ? hist90_ : hist91_;
        for (const char* n : {"yslb", "ysrb"}) {
            const formats::LayoutObject* o = sc.object(n);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { moveHistory(n[2] == 'l' ? -1 : 1); return -1; }
        }
        if (histScreen_ == 90) {
            for (const char* n : {"fe06", "fe91"}) {
                const formats::LayoutObject* o = hist90_.object(n);
                if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) {
                    if (n[3] == '6') histScreen_ = 0; else histScreen_ = 91; // fe06 exit / fe91 chain [C]
                    return -1;
                }
            }
        } else if (const formats::LayoutObject* o = hist91_.object("fe07"); o && widgetHit(*o, float(ptrX_), float(ptrY_))) {
            histScreen_ = 90; // LAY comment: "goes to feflow90"
            return -1;
        }
        return -1;
    }
    if (igMode_) {
        const formats::ScreenLayout& sc = igMode_ == 1 ? igA_ : igMode_ == 2 ? igG_ : igM_;
        for (auto [name, mode] : {std::pair{"go_g", 2}, std::pair{"go_a", 1}, std::pair{"go_c", 3}}) {
            const formats::LayoutObject* o = sc.object(name);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { openIgOptns(mode); return -1; } // tab switch [H]
        }
        for (const char* n : {"ok", "canc", "defa"}) { // commit/cancel/defaults close the popup [H] presentation-only
            const formats::LayoutObject* o = sc.object(n);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { igMode_ = 0; return -1; }
        }
        if (const formats::LayoutObject* box = sc.object("list"); box && widgetHit(*box, float(ptrX_), float(ptrY_))) {
            const int rh = std::max(8, box->intProp("ROWHEIGHT", 36));
            igRow_ = int(std::clamp((float(ptrY_) - float(box->y)) / float(rh), 0.f,
                                    float(std::max(1, int(igRows_.size())) - 1)));
            return -1;
        }
        return -1;
    }
    if (cheatsView_) { // feflow95 rows: a click focuses the row and flips its checkbox (the engine's
                       // hilite-flag write FUN_00479224 on the clicked widget)
        for (int i = 0; i < kCheats; ++i) {
            const formats::LayoutObject* o = cheats95_.object("btn" + std::to_string(i));
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { cheatRow_ = i; toggleCheat(i); return -1; }
        }
        return -1;
    }
    if (optsView_ && haveOpts_) { // tab buttons tbb1..4 (POINTS polygons in the LAY)
        for (int t = 0; t < 4; ++t) {
            const formats::LayoutObject* o = optsScreen_.object("tbb" + std::to_string(t + 1));
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { optTab_ = t; optRow_ = 0; return -1; }
        }
        return -1;
    }
    if (slView_ && haveSl_) { // slot buttons b000..b005: click selects + confirms the row
        for (int s = 0; s < kSaveSlots; ++s) {
            const formats::LayoutObject* o = slScreen_.object("b00" + std::to_string(s));
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { slSlot_ = s; enter(); return -1; }
        }
        return -1;
    }
    if (leagueCfg_) { // config grids/popups: nav icons, canc/alok, s001 click-to-value, clickenable rows
        const formats::ScreenLayout* sc = leagueCfg_ == 72 ? &lg72_ : leagueCfg_ == 74 ? &lg74_ :
                                           leagueCfg_ == 75 ? &lg75_ : leagueCfg_ == 76 ? &lg76_ :
                                           leagueCfg_ == 78 ? &lg78_ : leagueCfg_ == 79 ? &lg79_ : &lg80_;
        for (const char* n : {"gofd", "gobk"}) {
            const formats::LayoutObject* o = sc->object(n);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { cycleLeagueScreen(n[2] == 'd' ? 1 : -1); return -1; }
        }
        for (const char* n : {"alok", "canc", "back"}) { // confirm/cancel close to FEFLOW73 (keyloop 'gobk' cluster)
            const formats::LayoutObject* o = sc->object(n);
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { leagueCfg_ = 0; return -1; }
        }
        if (const formats::LayoutObject* s = sc->object("s001"); s && widgetHit(*s, float(ptrX_), float(ptrY_))) {
            const int hi = leagueCfg_ == 75 ? 4 : leagueCfg_ == 76 ? 15 : 8; // strategy / aggression / bias domains
            const float frac = std::clamp((float(ptrX_) - float(s->x)) / float(std::max(1, s->width)), 0.f, 1.f);
            if (leagueCfg_ == 75) stratVal_ = int(frac * hi + 0.5f);
            else if (leagueCfg_ == 76) aggrVal_ = int(frac * hi + 0.5f);
            else biasVal_ = int(frac * hi + 0.5f); // 78/79 share the attack-bias value
            return -1;
        }
        const char* col = leagueCfg_ == 80 ? "pn" : "pr"; // clickenable kt/menu rows (list_painter_notes.txt §2)
        const int rows = leagueCfg_ == 80 ? 12 : 5;
        for (int i = 0; i < rows; ++i) {
            const formats::LayoutObject* o = sc->object(std::string(col) + std::format("{:02d}", i));
            if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) { cfgRow_ = (cfgRow_ / rows) * rows + i; return -1; }
        }
        return -1; // config screens never launch a match by click
    }
    if (!onHub()) return -1; // team/setup/league/menu rows are code-drawn: no LAY hit boxes here
    for (int i = 0; i < int(kHubButtons.size()); ++i) {
        const formats::LayoutObject* o = hubScreen_.object(kHubButtons[std::size_t(i)]);
        if (o && widgetHit(*o, float(ptrX_), float(ptrY_))) {
            hubSel_ = i;
            return hubActivate(i);
        }
    }
    return -1;
}

bool FeFlow::openOptions()  { if (!haveOpts_) return false; optsView_ = true; optRow_ = 0; return true; }
bool FeFlow::openSaveLoad() { if (!haveSl_) return false; slView_ = true; slSlot_ = 0; return true; }
bool FeFlow::openCredits()  { if (!haveCred_) return false; credView_ = true; credFrames_ = 0; return true; }
void FeFlow::cycleTab(int dir) {
    if (!optsView_) return;
    optTab_ = (optTab_ + dir) % 4; // tabs tbb1..4 ('ops2' event family in the engine key handler)
    if (optTab_ < 0) optTab_ += 4;
    optRow_ = 0;
}

void FeFlow::hubUiState(presentation::FeUiState& st) const {
    // Mouse hover wins over the keyboard cursor; press paints PUSHEDSHAPE* on the hovered button.
    st.hover = std::string(hubButtonName(hubSel_));
    if (const formats::LayoutObject* o = pointerHitHub()) st.hover = o->name;
    st.mx = float(ptrX_); st.my = float(ptrY_);
    if (ptrDown_) st.pressed = st.hover;
    st.animMs = animTicks_ * 16u; // FRAMES=a,b,c,d ranges advance at 150 ms/frame (LAY_COMPOSITOR_NOTES)
    for (const char* d : kHubDisabled) st.disabled.emplace_back(d);
}
const formats::LayoutObject* FeFlow::pointerHitHub() const {
    using presentation::widgetHit; // POINTS= polygons (tbb wedges) / x-y-width-height box fallback
    for (const char* n : kHubButtons)
        if (const formats::LayoutObject* o = hubScreen_.object(n); o && widgetHit(*o, float(ptrX_), float(ptrY_))) return o;
    return nullptr;
}

FeDrawStats FeFlow::drawHub(render::SpriteBatch& ui) const {
    presentation::FeUiState uist;
    hubUiState(uist);
    ++animTicks_; // one frame-clock tick per presented frame (button FRAMES animations)
    FeDrawStats st = presentation::composite(hubScreen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &hubVisible_, &uist);
    drawHelpPane(hubScreen_, ui, st, uist.hover); // help= FC caption in the helppaneid pane (fontid 4)
    drawCursor(ui, st);
    return st;
}

// Help pane: the hovered button's `help=NNN,...` leading number indexes FeText::stringAt (FC*.BIN;
// measured captions fe07 help=1533 ... quit 4240). The "help" pane (objecttype 14) supplies geometry
// + fontid (4); text is centered within it (alignment=3 semantics of the engine list widgets).
void FeFlow::drawHelpPane(const formats::ScreenLayout& sc, render::SpriteBatch& ui,
                          FeDrawStats& st, std::string_view widgetName) const {
    if (!text_ || !text_->ready() || widgetName.empty()) return;
    const formats::LayoutObject* o = sc.object(widgetName);
    if (!o) return;
    int idx = 0;    const std::string* h = o->prop("HELP");
    if (!h || std::sscanf(h->c_str(), "%d", &idx) != 1 || idx <= 0) return;
    const std::string_view s = text_->stringAt(std::size_t(idx));
    if (s.empty()) return;
    const formats::LayoutObject* pane = sc.object(o->prop("HELPPANEID") ? *o->prop("HELPPANEID") : "help");
    if (!pane) pane = o; // fall back to the button's own box
    int fid = 4;
    const formats::FfnFont* f = text_->face(fid);
    render::TextureId ftex = text_->faceTexture(fid);
    if (!f || !ftex) { f = text_->font(); ftex = text_->texture(); }
    if (!f || !ftex) return;
    auto quads = presentation::layoutText(*f, s, 0.f, float(pane->y) + (pane->height - 16) / 2, 14.f, render::packRgba(255, 232, 96, 255));
    if (quads.empty()) return;
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& q : quads) minX = std::min(minX, q.dst.x), maxX = std::max(maxX, q.dst.x + q.dst.w);
    const float off = float(pane->x) + (float(pane->width) - (maxX - minX)) / 2.f - minX; // centered
    for (auto& q : quads) { q.dst.x += off; ui.draw(ftex, q.dst, q.uv, q.rgba); }
    ++st.texts;
}

// feflow15.lay list rows: the `rows=N+` key of lst{tab+1} is followed by N continuation lines
// "label,mode,count,v0..vN-1" (FC-bank indices; mode 2 = value list, mode 1 = slider/numeric row).
std::vector<FeFlow::OptRow> FeFlow::parseOptionRows(std::string_view layText, int tab) {
    std::vector<OptRow> rows;
    const std::string want = "objectname=lst" + std::to_string(tab + 1);
    std::size_t pos = layText.find(want);
    if (pos == std::string_view::npos) return rows;
    // Continuation lines run until the next [object / ;--- section marker.
    const std::size_t end = layText.find("[object", pos + want.size());
    std::string_view block = layText.substr(pos, (end == std::string_view::npos ? layText.size() : end) - pos);
    for (std::string_view line; !(block = block.substr(std::min<std::size_t>(block.find('\n') + 1, block.size()))).empty();) {}
    // Row tuples are the lines starting with ',': split on commas, strip '+' continuation markers.
    std::size_t p = 0;
    while (p < block.size()) {
        std::size_t nl = block.find('\n', p);
        if (nl == std::string_view::npos) nl = block.size();
        std::string_view ln = block.substr(p, nl - p);
        p = nl + 1;
        const std::size_t c0 = ln.find_first_not_of(" \t");
        if (c0 == std::string_view::npos || ln[c0] != ',') continue;
        OptRow r;
        std::vector<int> nums;
        std::size_t q = c0 + 1;
        while (q < ln.size()) {
            const std::size_t comma = ln.find(',', q);
            std::string_view tok = ln.substr(q, (comma == std::string_view::npos ? ln.size() : comma) - q);
            // trim + strip the trailing '+' continuation marker
            while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) tok.remove_prefix(1);
            while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t' || tok.back() == '+')) tok.remove_suffix(1);
            if (!tok.empty()) { int v = 0; if (std::from_chars(tok.data(), tok.data() + tok.size(), v).ptr == tok.data() + tok.size()) nums.push_back(v); }
            if (comma == std::string_view::npos) break;
            q = comma + 1;
        }
        if (nums.size() >= 3) { r.label = nums[0]; r.mode = nums[1]; r.values.assign(nums.begin() + 3, nums.end()); rows.push_back(r); }
        else if (nums.size() == 2) { r.label = nums[0]; r.mode = nums[1]; rows.push_back(r); } // "label,mode" slider row
    }
    return rows;
}

FeDrawStats FeFlow::drawOptions(render::SpriteBatch& ui) const {
    static constexpr std::array<const char*, 4> kTabs{"tbb1", "tbb2", "tbb3", "tbb4"};
    presentation::FeUiState uist;
    uist.animMs = animTicks_ * 16u;
    ++animTicks_;
    static const std::vector<std::string> kOptsVisible{"back", "bnnr", "flag", "titl", "tab1", "tab2", "tab3", "tab4",
                                                       "tbb1", "tbb2", "tbb3", "tbb4", "help", "quit", "save", "deft", "fwrd", "ttxt"};
    FeDrawStats st = presentation::composite(optsScreen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &kOptsVisible, &uist);
    drawHelpPane(optsScreen_, ui, st, kTabs[std::size_t(optTab_)]); // tab captions via help= keys
    const formats::LayoutObject* lst = optsScreen_.object("lst" + std::to_string(optTab_ + 1));
    if (!lst) { drawCursor(ui, st); return st; }
    const int rowH = lst->intProp("ROWHEIGHT", 21);
    const int fid = lst->intProp("TEXTFONT", 4);
    const formats::FfnFont* f = text_ ? text_->face(fid) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(fid) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    const auto& rows = optRows_[std::size_t(optTab_)];
    const auto& vals = optVals_[std::size_t(optTab_)];
    for (int i = 0; i < int(rows.size()); ++i) {
        const float y = float(lst->y) + float(i) * rowH;
        if (i == optRow_) ui.draw(0, {float(lst->x) - 4.f, y - 2.f, float(lst->width), float(rowH)}, {}, render::packRgba(255, 224, 96, 48));
        if (!f || !ftex) continue;
        const OptRow& r = rows[std::size_t(i)];
        const std::string_view cap = text_->stringAt(std::size_t(r.label));
        if (cap.empty()) continue; // FC-bank row captions are the scene-code truth here; gaps stay blank
        for (const auto& q : presentation::layoutText(*f, cap, float(lst->x) + 8.f, y, 13.f, render::packRgba(235, 235, 235, 255)))
            ui.draw(ftex, q.dst, q.uv, q.rgba);
        ++st.texts;
        if (!r.values.empty()) { // current value caption at the right edge (engine list-value column)
            const int vi = std::clamp(vals[std::size_t(i)], 0, int(r.values.size()) - 1);
            const std::string_view val = text_->stringAt(std::size_t(r.values[std::size_t(vi)]));
            if (!val.empty()) {
                for (const auto& q : presentation::layoutText(*f, val, float(lst->x + lst->width) - 150.f, y, 13.f, render::packRgba(255, 232, 96, 255)))
                    ui.draw(ftex, q.dst, q.uv, q.rgba);
                ++st.texts;
            }
        } else if (r.label == 1186 && settings_) { // "Music Volume" slider row -> the port's [audio] music volume
            const std::string v = std::format("{}%", settings_->audio.music);
            for (const auto& q : presentation::layoutText(*f, v, float(lst->x + lst->width) - 150.f, y, 13.f, render::packRgba(255, 232, 96, 255)))
                ui.draw(ftex, q.dst, q.uv, q.rgba);
            ++st.texts;
        }
    }
    drawCursor(ui, st);
    return st;
}

FeDrawStats FeFlow::drawSaveLoad(render::SpriteBatch& ui) const {
    static const std::vector<std::string> kSlVisible{"back", "bnnr", "flag", "titl", "vit0", "vit1",
                                                      "lb00", "lb01", "lb02", "lb03",
                                                      "b000", "b001", "b002", "b003", "b004", "b005"};
    FeDrawStats st = presentation::composite(slScreen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &kSlVisible);
    const formats::FfnFont* f = text_ ? text_->face(4) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(4) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    const auto dir = defaultSlotDir(); // USER/SLOTnn.SAV domain (saveload.hpp, SAVESLOT_WC98.md)
    for (int s = 0; s < kSaveSlots; ++s) {
        const formats::LayoutObject* o = slScreen_.object("b00" + std::to_string(s));
        if (!o) continue;
        if (s == slSlot_) ui.draw(0, {float(o->x) - 8.f, float(o->y) - 2.f, float(o->width) + 16.f, float(o->height)}, {}, render::packRgba(255, 224, 96, 48));
        if (!f || !ftex) continue;
        const auto file = dir / formats::slotFileName(s);
        std::string label = "Slot ";
        label += char('0' + s);
        label += exists(file) ? "  (saved game)" : "  (empty)"; // WCT1 slot state from the port's own saves
        for (const auto& q : presentation::layoutText(*f, label, float(o->x) + 8.f, float(o->y) + 5.f, 13.f, render::packRgba(235, 235, 235, 255)))
            ui.draw(ftex, q.dst, q.uv, q.rgba);
        ++st.texts;
    }
    drawCursor(ui, st);
    return st;
}

FeDrawStats FeFlow::drawCredits(render::SpriteBatch& ui) const {
    static const std::vector<std::string> kCredVisible{"back", "flag", "titl", "help"};
    FeDrawStats st = presentation::composite(credScreen_, ui, const_cast<ScreenArt&>(art_), text_.get(), &kCredVisible);
    // FC-bank credit role captions (measured indices in COMMON/FCENG.BIN: 508 "Core Team Artists:",
    // 509 "Core Team Development Director:", 510/511 programmers). The runtime roll names the people
    // from engine data the port has no static source for; the role captions are shipped strings.
    const formats::FfnFont* f = text_ ? text_->face(4) : nullptr;
    render::TextureId ftex = text_ ? text_->faceTexture(4) : 0;
    if (!f || !ftex) { f = text_ ? text_->font() : nullptr; ftex = text_ ? text_->texture() : 0; }
    static constexpr std::array<int, 4> kRoles{508, 509, 510, 511};
    if (f && ftex) {
        for (std::size_t i = 0; i < kRoles.size(); ++i) {
            const std::string_view s = text_->stringAt(std::size_t(kRoles[i]));
            if (s.empty()) continue;
            for (const auto& q : presentation::layoutText(*f, s, 180.f, 240.f + 22.f * float(i), 13.f, render::packRgba(235, 235, 235, 255)))
                ui.draw(ftex, q.dst, q.uv, q.rgba);
            ++st.texts;
        }
    }
    drawCursor(ui, st);
    return st;
}

// ---- Cheats screen (feflow95.lay; MENU_PARITY.md §8) ----------------------------------------

bool FeFlow::openCheats(int onMask) {
    if (!haveCheats_) { lastError_ = "FEFLOW95.LAY is absent from this tree"; return false; }
    cheatsView_ = true;
    cheatRow_ = 0;
    for (int i = 0; i < kCheats; ++i) cheatsOn_[std::size_t(i)] = (onMask & (1 << i)) != 0; // gate/dev pre-action [H]
    return true;
}

void FeFlow::moveCheat(int dir) {
    if (!cheatsView_ || dir == 0) return;
    cheatRow_ = (cheatRow_ + dir + kCheats) % kCheats; // rows btn0..btn7 at y=183+25i
}

void FeFlow::toggleCheat(int row) {
    if (!cheatsView_ || row < 0 || row >= kCheats) return;
    cheatsOn_[std::size_t(row)] = !cheatsOn_[std::size_t(row)]; // engine: FUN_00479224 hilite-flag write per row
}

int FeFlow::paintCaption(const formats::LayoutObject& o, std::string_view s, render::SpriteBatch& ui) const {
    if (!text_ || !text_->ready() || s.empty()) return 0;
    const int fid = o.intProp("FONTID", 4); // feflow95 rows/titl/help all carry fontID=4
    const formats::FfnFont* f = text_->face(fid);
    render::TextureId ftex = text_->faceTexture(fid);
    if (!f || !ftex) { f = text_->font(); ftex = text_->texture(); }
    if (!f || !ftex) return 0;
    const float sizePx = f->lineHeight ? float(f->lineHeight) : 16.f;
    // Caption colour: the widget's FE global-palette index in NORMALCOLOUR (widget fields +0x68/+0x6C,
    // parse @0x46e10b). The txt0..txt7 slots carry index 0 = a dark swatch on this pane; the runtime
    // caption colour is installed by scene code and not statically recoverable, so dark palette colours
    // fall back to a light one here [H] (legibility rule documented in MENU_PARITY.md §8).
    const int cidx = o.intProp("NORMALCOLOUR", -1);
    std::uint32_t rgba = render::packRgba(255u, 236u, 190u, 255u);
    if (const auto& pal = formats::globalPalette(); cidx >= 0 && std::size_t(cidx) < pal.size()) {
        const auto& c = pal[std::size_t(cidx)];
        if (int(c[0]) + int(c[1]) + int(c[2]) > 240) rgba = render::packRgba(c[0], c[1], c[2], 0xFF);
    }
    auto quads = presentation::layoutText(*f, s, float(o.x), float(o.y) + (float(o.height) - sizePx) * 0.5f, sizePx, rgba);
    if (quads.empty()) return 0;
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& q : quads) minX = std::min(minX, q.dst.x), maxX = std::max(maxX, q.dst.x + q.dst.w);
    const float off = float(o.x) + (float(o.width) - (maxX - minX)) * 0.5f - minX; // centred inside the box
    int n = 0;
    for (auto& q : quads) { q.dst.x += off; ui.draw(ftex, q.dst, q.uv, q.rgba); ++n; }
    return n;
}

FeDrawStats FeFlow::drawCheats(render::SpriteBatch& ui) const {
    // Scene-owned art (whitelist): blur backdrop, cheatart pane, the eight cheatbox `1box` frames,
    // 'yes' (nvicons tic1) and the help pane. titl/txt0..txt7 stay out of composite: their LAY textid
    // defaults are other contexts' bank strings ("and icons"), captions being installed at runtime.
    FeDrawStats st = presentation::composite(cheats95_, ui, const_cast<ScreenArt&>(art_), text_.get(), &cheatsVisible_);
    if (!text_ || !text_->ready()) return st;
    // Pane title: the scene installs "Cheats" (exe VA 0x56400c) on 'titl' at x=144 y=145 w=200 h=19.
    if (const formats::LayoutObject* t = cheats95_.object("titl")) st.texts += paintCaption(*t, kCheatTitle, ui);
    for (int i = 0; i < kCheats; ++i) {
        const formats::LayoutObject* b = cheats95_.object("btn" + std::to_string(i));
        if (!b) continue;
        // Row cursor pane: the same filled highlight as the FEFLOW73 list painter ([H] port-side focus;
        // the engine drives per-widget focus through FUN_00479224, not a static LAY key).
        if (i == cheatRow_)
            ui.draw(0, {float(b->x) - 3.f, float(b->y) - 2.f, float(b->width) + 6.f, float(b->height) + 4.f}, {},
                    render::packRgba(255u, 232u, 96u, 70u));
        presentation::paintCheckbox(ui, *b, cheatsOn_[std::size_t(i)], const_cast<ScreenArt&>(art_), st);
        if (const formats::LayoutObject* t = cheats95_.object("txt" + std::to_string(i)))
            st.texts += paintCaption(*t, kCheatLabels[std::size_t(i)], ui); // x=144 y=179+25i w=100 h=20
    }
    // Help pane (objecttype 14 'help', x=240 y=444 w=240 h=40): the scene shows the focused widget's
    // caption there, so the row cursor feeds it; textbackcolor/textbackshadow keys are art-level.
    if (const formats::LayoutObject* pane = cheats95_.object("help")) {
        ui.draw(0, {float(pane->x), float(pane->y), float(pane->width), float(pane->height)}, {},
                render::packRgba(12u, 10u, 6u, 150u)); // textbackshadow=50 dark plate [H]
        st.texts += paintCaption(*pane, kCheatLabels[std::size_t(cheatRow_)], ui);
    }
    drawCursor(ui, st);
    return st;
}

// ---- World-cup history boards feflow90/91 (hub wedge fe33; analysis/pass13/CLASSICS_FE33.md) -----
// FCDB fullName for a team id (classic-era records 861..876 included); the dataset's literal string
// paints when the record is absent (Czechoslovakia: no FCDB entry on this tree).
std::string FeFlow::wcTeamName(std::uint16_t id, const char* fallback) const {
    if (fcdb_ && !fcdbTeamFullNames_.empty()) {
        const auto it = std::ranges::find(fcdb_->teams, id, &formats::FcdbTeam::id);
        if (it != fcdb_->teams.end()) {
            const auto idx = std::size_t(std::distance(fcdb_->teams.begin(), it));
            if (idx < fcdbTeamFullNames_.size() && !fcdbTeamFullNames_[idx].empty()) return fcdbTeamFullNames_[idx];
        }
    }
    return fallback;
}

// Shield/flag art file for a team id: "FNNN" = FCDB record INDEX (same rule as the pool flags);
// "" when no record. FE/GLOBAL ships F000..F046, so the classic entries 861..876 (idx 41+) resolve.
std::string FeFlow::wcFlagFile(std::uint16_t id) const {
    if (!fcdb_) return {};
    const auto it = std::ranges::find(fcdb_->teams, id, &formats::FcdbTeam::id);
    if (it == fcdb_->teams.end()) return {};
    char ff[8];
    std::snprintf(ff, sizeof ff, "F%03u", unsigned(std::distance(fcdb_->teams.begin(), it)));
    return ff;
}

bool FeFlow::openHistory(int yearIdx) {
    if (!haveHistory_) { lastError_ = "Feflow90.lay is absent from this tree"; return false; }
    histScreen_ = 90;
    // Init cursor clamp [C] FUN_004688e8: DAT_0057d1cc bounded by the list length DAT_0057c970 = 9.
    histYear_ = std::clamp(yearIdx, 0, kWcYears - 1);
    return true;
}

void FeFlow::moveHistory(int dir) {
    if (histScreen_) histYear_ = std::clamp(histYear_ + dir, 0, kWcYears - 1); // 'ysla'/'yslc' -1, 'ysrb' +1 [C]
}

FeDrawStats FeFlow::drawHistory(render::SpriteBatch& ui) const {
    using namespace presentation;
    if (histScreen_ == 91) {
        // Continuation board: FUN_004857a8 stored the selected index -> this scene paints its pair.
        FeDrawStats st = composite(hist91_, ui, const_cast<ScreenArt&>(art_), text_.get(), &hist91Visible_);
        if (!text_ || !text_->ready()) return st;
        const WcFinal& f = kWcFinals[std::size_t(std::clamp(histYear_, 0, kWcYears - 1))];
        for (auto [w, nm] : {std::pair{"tltx", wcTeamName(f.leftId, f.leftName)},
                             std::pair{"trtx", wcTeamName(f.rightId, f.rightName)}}) {
            if (const formats::LayoutObject* o = hist91_.object(w)) st.texts += paintCaption(*o, nm, ui);
        }
        for (auto [w, id] : {std::pair{"ltfg", f.leftId}, std::pair{"rtfg", f.rightId}}) {
            const formats::LayoutObject* o = hist91_.object(w);
            if (!o) continue;
            const std::string ff = wcFlagFile(id);
            if (ff.empty()) { ++st.missing; continue; } // no FCDB record: keep the LAY default shield art
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", ff, "f000", -1);
            if (sh.tex) ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu), ++st.shapes;
            else ++st.missing;
        }
        drawHelpPane(hist91_, ui, st, "fe08"); // confirm-control caption (helppaneid pane) [H] default focus
        drawCursor(ui, st);
        return st;
    }
    FeDrawStats st = composite(hist90_, ui, const_cast<ScreenArt&>(art_), text_.get(), &hist90Visible_);
    if (!text_ || !text_->ready()) return st;
    const WcFinal& f = kWcFinals[std::size_t(std::clamp(histYear_, 0, kWcYears - 1))];
    // Field values: the repaint FUN_00468954 fills ltnm/rtnm/ltsc/rtsc/year via widget text set
    // FUN_004788bc ("%d" format string, VA 0x563f4c); the port draws the dataset fields direct.
    if (const formats::LayoutObject* o = hist90_.object("year")) st.texts += paintCaption(*o, std::to_string(f.year), ui);
    if (const formats::LayoutObject* o = hist90_.object("ltnm")) st.texts += paintCaption(*o, wcTeamName(f.leftId, f.leftName), ui);
    if (const formats::LayoutObject* o = hist90_.object("rtnm")) st.texts += paintCaption(*o, wcTeamName(f.rightId, f.rightName), ui);
    if (const formats::LayoutObject* o = hist90_.object("ltsc")) st.texts += paintCaption(*o, std::to_string(f.scoreL), ui);
    if (const formats::LayoutObject* o = hist90_.object("rtsc")) st.texts += paintCaption(*o, std::to_string(f.scoreR), ui);
    for (auto [w, id] : {std::pair{"shdl", f.leftId}, std::pair{"shdr", f.rightId}}) {
        const formats::LayoutObject* o = hist90_.object(w);
        if (!o) continue;
        const std::string ff = wcFlagFile(id);
        if (ff.empty()) { ++st.missing; continue; } // no FCDB record: keep the LAY default shield art
        const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape("..\\global\\", ff, "f000", -1);
        if (sh.tex) ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu), ++st.shapes;
        else ++st.missing;
    }
    drawHelpPane(hist90_, ui, st, "yslb"); // year-button caption (help=5095) [H] default focus
    drawCursor(ui, st);
    return st;
}
// ---- Shared popup lane (FEFLOW10/37 + IG boxes; analysis/pass13/POPUP_LANE_SPEC.md) -----------------
// Widget grammar (/tmp/layd90 FEFLOW10/FEFLOW37 dumps): blur blackout (FE/GLOBAL/BLUR.FSH#blur, 640x480)
// + popp panel feflow86.fsh#back 368x230 @136,140 + nvicons button pair: cancel = crs1/crs5 (help=13),
// confirm = tic1/tic5 (help=12) — the composite hover path paints HILITEDSHAPE. txtNN slots ship textid=0;
// captions are installed at runtime by callers (feflow37 label setter FUN_004592d4 x3, §27), so the FC-bank
// strings paint as overlays. IGCONSEL/MESG: null.fsh region boxes legit placeholders (§A) + objecttype-45
// buttons painting INGAME.FSH chrome over their geometry (button.fsh stem alias, IGMENU pattern).

const char* FeFlow::popupButtonName(int slot) const {
    switch (popKind_) {
        case 10: return slot == 0 ? "canc" : "ok_1"; // ok_2 = the standalone-ok context, not painted [H]
        case 37: return slot == 0 ? "no" : "yes";
        case -42: return "okay";                     // IGCONSEL dismiss-only box (FUN_00442000 stub)
        default: return "";                          // mesg/Feflow86 demo lanes carry no gated cursor pair
    }
}

bool FeFlow::openPopup(std::string_view name, bool quitContext) {
    std::string n(name);
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    popSel_ = 0;        // engine default focus: the cancel side [H]
    popQuitCtx_ = quitContext;
    if (n == "feflow10" || n == "ok") {
        if (!havePop10_) { lastError_ = "FEFLOW10.LAY is absent from this tree"; return false; }
        popKind_ = 10; popName_ = "FEFLOW10";
        popVisible_ = {"blur", "popp", "canc", "ok_1", "help"}; // txt slots = runtime captions (overlay); ok_2 hidden
        return true;
    }
    if (n == "feflow37" || n == "yesno") {
        if (!havePop37_) { lastError_ = "FEFLOW37.LAY is absent from this tree"; return false; }
        popKind_ = 37; popName_ = "FEFLOW37";
        popVisible_ = {"blur", "popp", "no", "yes", "help"}; // same grammar, yes/no payload pair [C] §29
        return true;
    }
    static constexpr std::pair<const char*, int> kKnown[] = {{"igconsel", -42}, {"mesg", -43}, {"feflow86", -86}};
    for (auto [nm, kind] : kKnown) {
        if (n != nm) continue;
        // Lazy member load: same search order as the capture lane (loadCaptureScreen).
        static constexpr std::array<const char*, 2> archives = {"LAYOUTS.BIG", "IGLAY.BIG"};
        try {
            fs::path d;
            std::string text;
            bool found = false;
            for (const char* arc : archives) {
                try {
                    text = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS" / arc).string() + "@" + n + ".LAY", d);
                    found = true;
                    break;
                } catch (const std::exception&) { /* member absent in this archive: try the next */ }
            }
            if (!found) {
                const fs::path loose = wc98::findCaseInsensitive(tree_, "FE/INGAME/" + n + ".LAY");
                if (loose.empty()) throw std::runtime_error(n + ".LAY: no such layout member/file");
                text = presentation::readLayout(loose.string(), d);
            }
            popIg_ = formats::ScreenLayout::build(n, formats::LayoutDocument::parse(std::string_view(text)), nullptr, nullptr, "PC");
        } catch (const std::exception& e) { lastError_ = std::string(name) + ": " + e.what(); return false; }
        popKind_ = kind;
        popName_ = n == "feflow86" ? "FEFLOW86" : n; // gate evidence label (raw member name stays lowercase)
        popVisible_.clear(); // raw members: composite() paints every shown() object (capture-lane rule)
        return true;
    }
    lastError_ = "unknown popup screen '" + std::string(name) + "'";
    return false;
}

void FeFlow::movePopup(int dir) {
    if (!popKind_) return;
    popSel_ = (popSel_ + dir + 2) % 2; // the two-slot pair wraps ('ok'/'cancel', 'yes'/'no' payloads [C])
}

void FeFlow::activatePopup() {
    if (!popKind_) return;
    // Payload semantics [C] §29: `ok`/`yes` confirm, `cancel`/`no` dismiss. Both branches close to the
    // caller scene (the box is modal chrome, not a view chain); in the hub 'quit'-wedge context the
    // confirm side requests the app exit — FEFLOW37 is that quit-confirm box.
    if (popSel_ == 1 && popQuitCtx_) quitRequested_ = true;
    popKind_ = 0;
}

FeDrawStats FeFlow::drawPopup(render::SpriteBatch& ui) const {
    using namespace presentation;
    const formats::ScreenLayout* sc = popKind_ == 37 ? &pop37_ : popKind_ == 10 ? &pop10_ : &popIg_;
    auto* vis = popVisible_.empty() ? nullptr : const_cast<std::vector<std::string>*>(&popVisible_); // raw member lane: paint all shown()
    FeUiState uist;
    if (const char* bn = popupButtonName(popSel_); bn && *bn) uist.hover = bn; // keyboard cursor paints HILITEDSHAPE
    uist.animMs = animTicks_ * 16u;                                              // FRAMES=46,49/51,54 button anim ranges
    FeDrawStats st = composite(*sc, ui, const_cast<ScreenArt&>(art_), text_.get(), vis);
    if (popKind_ == 10 || popKind_ == 37) {
        // Runtime caption family: FEFLOW37 = the quit-confirm prompts ("Are you sure you want to Quit?",
        // FC index 220 — the caller strings 213..232 are this popup's whole installed set); FEFLOW10 is
        // the generic modal, painted with "Are you sure?" (index 232) [H].
        if (text_ && text_->ready()) {
            const std::string_view cap = text_->stringAt(popKind_ == 37 ? 220 : 232);
            if (const formats::LayoutObject* o = sc->object("txt0")) st.texts += paintCaption(*o, cap, ui);
            drawHelpPane(*sc, ui, st, popupButtonName(popSel_)); // focused button's help= index (12/13) [C]
        }
    } else if (popKind_ == -42) {
        // IGCONSEL 'okay' box: shape chain over the widget geometry; RELEASED=btnn, HILITED/PUSHED=btnd
        // (INGAME.FSH tags via the button.fsh stem alias — IGMENU row pattern, INGAME FSH scan §85-91).
        if (const formats::LayoutObject* o = sc->object("okay")) {
            const std::string vpath = o->prop("vpath") ? *o->prop("vpath") : std::string("..\\global\\");
            const ScreenArt::Shape sh = const_cast<ScreenArt&>(art_).shape(vpath, "INGAME.FSH", popSel_ == 1 ? "btnd" : "btnn", -1);
            if (sh.tex) ui.draw(sh.tex, {float(o->x), float(o->y), float(sh.w), float(sh.h)}, {0.f, 0.f, 1.f, 1.f}, 0xFFFFFFFFu), ++st.shapes;
            else ++st.missing;
            if (text_ && text_->ready()) st.texts += paintCaption(*o, text_->stringAt(1256), ui); // FC bank "OK" (index 1256)
        }
    }
    drawCursor(ui, st);
    return st;
}


// ---- In-game option popups igaoptns/iggoptns/igmoptns (analysis/pass13/IGOPTNS_ART.md) -----------
// Inline `rows=N, label,mode,count,v...` tuples (prop ROWS): FC-bank caption ids per row + values.
std::vector<FeFlow::OptRow> FeFlow::parseIgRows(std::string_view rowsProp) {
    std::vector<int> nums;
    std::size_t p = 0;
    while (p < rowsProp.size()) {
        const std::size_t c = rowsProp.find(',', p);
        std::string_view tok = rowsProp.substr(p, (c == std::string_view::npos ? rowsProp.size() : c) - p);
        const std::size_t n0 = tok.find_first_not_of(" \t");
        if (n0 != std::string_view::npos) {
            tok = tok.substr(n0);
            int v = 0;
            if (std::from_chars(tok.data(), tok.data() + tok.size(), v).ptr == tok.data() + tok.size()) nums.push_back(v);
        }
        if (c == std::string_view::npos) break;
        p = c + 1;
    }
    std::vector<OptRow> rows;
    if (nums.size() < 4) return rows;
    std::size_t i = 1; // nums[0] = tuple count
    for (int t = 0; t < nums[0] && i + 2 < nums.size(); ++t) {
        OptRow r;
        r.label = nums[i++];
        r.mode = nums[i++];
        int cnt = nums[i++];
        while (cnt-- > 0 && i < nums.size()) r.values.push_back(nums[i++]);
        rows.push_back(std::move(r));
    }
    return rows;
}

bool FeFlow::openIgOptns(int mode) {
    if (mode < 1 || mode > 3 || !(haveIg_ & (1 << (mode - 1)))) {
        lastError_ = "igoptns popup layout is absent from this tree";
        return false;
    }
    igMode_ = mode;
    const formats::ScreenLayout& sc = mode == 1 ? igA_ : mode == 2 ? igG_ : igM_;
    const formats::LayoutObject* list = sc.object("list");
    igRows_ = list && list->prop("ROWS") ? parseIgRows(*list->prop("ROWS")) : std::vector<OptRow>{};
    igVals_.assign(igRows_.size(), 0);
    igRow_ = 0;
    return true;
}

void FeFlow::moveIgOption(int dir) {
    if (!igMode_) return;
    const int n = std::max(1, int(igRows_.size()));
    igRow_ = (igRow_ + dir + n) % n; // up/down row cursor (list widget, ROWHEIGHT stride rows) [H]
}

FeDrawStats FeFlow::drawIgOptns(render::SpriteBatch& ui) const {
    using namespace presentation;
    const formats::ScreenLayout& sc = igMode_ == 1 ? igA_ : igMode_ == 2 ? igG_ : igM_;
    FeDrawStats st = composite(sc, ui, const_cast<ScreenArt&>(art_), text_.get(), &igVisible_);
    if (!text_ || !text_->ready()) return st;
    // List rows: label caption + current value caption from the FC bank (the tuples' ids), painted in
    // the list box at the ROWHEIGHT stride with the row cursor hilite.
    if (const formats::LayoutObject* box = sc.object("list")) {
        std::vector<std::string> caps;
        for (std::size_t i = 0; i < igRows_.size(); ++i) {
            const OptRow& r = igRows_[i];
            std::string cap;
            if (const std::string_view s = text_->stringAt(r.label); !s.empty()) cap = std::string(s);
            if (!r.values.empty() && i < igVals_.size()) {
                const int v = r.values[std::size_t(igVals_[i]) % r.values.size()]; // value caption suffix [H]
                if (const std::string_view s = text_->stringAt(v); !s.empty()) cap += "  " + std::string(s);
            }
            caps.push_back(std::move(cap));
        }
        const int fontId = box->intProp("TEXTFONT", 2); // list widget textFont=2 (measured)
        const int rowH = box->intProp("ROWHEIGHT", 36); // slots that fit the list box (engine clips + scrolls)
        const int vis = std::max(1, (box->height - 2) / rowH);
        const int top = std::min(std::max(0, igRow_ - vis + 1), std::max(0, int(caps.size()) - vis));
        std::vector<std::string> view;
        for (int i = top; i < std::min(int(caps.size()), top + vis); ++i) view.push_back(caps[std::size_t(i)]);
        paintListRows(ui, *box, std::span(view), igRow_ - top, *text_, fontId, st);
        if (const formats::LayoutObject* trk = sc.object("scrl"))
            paintSliderThumb(ui, const_cast<ScreenArt&>(art_), *trk, igRow_, 0, std::max(1, int(igRows_.size()) - 1));
    }
    // Tab buttons: objecttype-4 widgets with TEXTID are dynamic content -> composite skips the label;
    // paint the bank captions over them (textid=1174/1175/1144 measured on go_g/go_a/go_c). The audio
    // popup names its three tab buttons "2", "3", "04" (measured igaoptns.lay widget map).
    const char* kTab[3] = {"go_g", "go_a", "go_c"};
    if (igMode_ == 1) { kTab[0] = "2"; kTab[1] = "3"; kTab[2] = "04"; }
    for (int i = 0; i < 3; ++i) {
        const formats::LayoutObject* o = sc.object(kTab[i]);
        if (!o) continue;
        const std::string* t = o->prop("TEXTID");
        if (!t) continue;
        int idx = 0;
        std::from_chars(t->data(), t->data() + std::min<std::size_t>(t->size(), 8), idx);
        std::string s(text_->stringAt(idx)); // e.g. the game-tab caption (1174)
        if (s.empty()) continue;
        const formats::FfnFont* f = text_->face(4);
        render::TextureId ftex = text_->faceTexture(4);
        if (!f || !ftex) { f = text_->font(); ftex = text_->texture(); }
        if (!f || !ftex) continue;
        const float px = f->lineHeight ? float(f->lineHeight) : 16.f;
        // The engine's font painter clips captions inside the widget box (this media's tab ids are long
        // dev strings); trim to the button width so pills never bleed over neighbours.
        while (s.size() > 1 && textWidthPx(*f, s, px) > float(o->width) - 8.f) s.pop_back();
        const float w = textWidthPx(*f, s, px);
        for (const auto& q : layoutText(*f, s, float(o->x) + (float(o->width) - w) * 0.5f,
                                        float(o->y) + (float(o->height) - px) * 0.5f, px,
                                        render::packRgba(235, 235, 235, 255)))
            ui.draw(ftex, q.dst, q.uv, q.rgba);
        ++st.texts;
    }
    // ok/canc/defa: art PROVEN ABSENT (igicon.fsh#chck dead reference, IGOPTNS_ART.md) -> paint the
    // widgets' own help= FC captions as text over their boxes; no invented placeholder art.
    const char* kCtl[3] = {"ok", "canc", "defa"}; // control widgets (chck art dead on this media)
    const int kCap[3]   = {60, 62, 1176};         // their help= FC-bank caption ids (measured)
    for (int i = 0; i < 3; ++i) {
        const formats::LayoutObject* o = sc.object(kCtl[i]);
        if (!o) continue;
        ++st.missing; // the chck shape is a documented legit-missing art slot on this media [C]
        std::string s(text_->stringAt(kCap[i]));
        if (s.empty()) continue;
        // Same widget-box clip as the engine's font painter: this media's ids are long dev strings.
        const formats::FfnFont* f = text_->face(4);
        if (f && f->lineHeight) {
            const std::size_t maxChars = static_cast<std::size_t>(std::max(1.f, float(o->width) - 8.f) / (float(f->lineHeight) * 0.62f));
            if (s.size() > maxChars) s.resize(maxChars);
        }
        st.texts += paintCaption(*o, std::string_view(s), ui);
    }
    drawHelpPane(sc, ui, st, "ok"); // confirm-control caption (help=60) [H] default focus
    drawCursor(ui, st);
    return st;
}

} // namespace wc98::app
