#pragma once
// In-game option popups igaoptns/iggoptns/igmoptns (the IGOPTN2 window-object scene).
//
// Grammar (analysis/pass21igmenu/NOTES.md): the IGMENU init handler 0x4401a4 case 0xe runs
// 0x43ab9c(4) (in-game mode word DAT_005773c8 = 4) then the scene switch 0x4461b4 with name
// "IGOPTN2" (0x56214c). The scene paints the three popup layouts (igaoptns/iggoptns/igmoptns,
// FE/PACK/LAYOUTS) through the generic window-object list widget: the ROWS tuple carries
// FC-bank caption ids for each row label and its value captions; tab buttons go_g/go_a/go_c
// (iggoptns/igmoptns) or "2"/"3"/"04" (igaoptns, audio popup names); ok/canc/defa controls have
// PROVEN-ABSENT art (igicon.fsh#chck dead reference, analysis/pass13/IGOPTNS_ART.md) so their
// help= FC captions paint as text. Shared between the FE shell (FeFlow) and the live-match
// pause chain (IgBoards kinds 21/22/23) so both paint the identical caption grammar.
#include <wc98/presentation/fe_screen.hpp>

#include <string>
#include <vector>

namespace wc98::presentation {

struct IgOptRow {
    int label = 0;                            // FC-bank caption id of the row label
    int mode = 0;                             // widget mode word (two-axis rows carry values)
    std::vector<int> values;                  // FC-bank caption ids of the value captions
};

// "count, label,mode,count,v... xN" tuple (prop ROWS of the `list` widget).
[[nodiscard]] std::vector<IgOptRow> parseIgRows(std::string_view rowsProp);

// Paint the popup content over a composited layout: list rows (label + current value caption in
// the ROWHEIGHT stride with the cursor hilite + slider thumb), tab captions, control captions.
// `curRow` = row cursor; `audioTabs` = true for igaoptns (tab widgets "2"/"3"/"04").
void paintIgOptns(render::SpriteBatch& ui, const formats::ScreenLayout& sc, ScreenArt& art,
                  FeText* text, const std::vector<IgOptRow>& rows, const std::vector<int>& vals,
                  int curRow, bool audioTabs, FeDrawStats& st);

// The IGOPTN2 board itself (FE/INGAME/IGOPTN2.LAY, scene registry row IGOPTN2): one window with
// four tab panes. tab 0..3 = Match/Game play/controller/audio -> list widgets lst1..lst4; the
// tbb1..4 hot-rects carry POINTS polygons + hilitedpane=matH/gamH/conH/audH (the active tab's
// header pane shows), so the caller composites chrome + the active header per tab. The painter
// adds the active list's rows (label + current value caption, FC remap ids), the opslider.fsh
// slider art per mode-2 row (unsl track 185x17, sllf left fill, btnn knob - OPSLIDER.PAK tags,
// analysis/pass21igmenu) and the active tab's help= caption (78/81/1299/82) in the help pane.
void paintIgOptns2(render::SpriteBatch& ui, const formats::ScreenLayout& sc, ScreenArt& art,
                   FeText* text, const std::vector<IgOptRow>& rows, const std::vector<int>& vals,
                   int curRow, int tab, FeDrawStats& st);

} // namespace wc98::presentation
