# .LAY Screen Compositor (M4 slice 2, 2026-11-02)

Front-end screens = `.LAY` text layouts + FSH art. Grammar parsed by
`formats/src/layout.cpp` (`LayoutDocument`/`ScreenLayout`, gate: `test_layout`);
the compositor lives in `tools/render-demo/main.cpp` (`--lay <spec> [--tree <dir>]`).

## Screen inventory (measured this pass)
- `FE/PACK/LAYOUTS/LAYOUTS.BIG` — 101 members, RefPack-wrapped; FE flow screens
  `FEFLOWxx.LAY` (+ non-LAY members). `IGLAY.BIG` — in-game layouts
  (`CONTROL1.LAY`, `IGOPTN2.LAY`, per-venue `IGteam7x.lay`…). Both archives decode
  through the same reader as loose `.LAY` files (48-byte-prefix variant handled).


## Netplay UI presence (probe 2026-11-07, FCENG.BIN + LAYOUTS/IGLAY member scan)
- `FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW80.LAY` = create/edit custom team screen; carries netplay
  chat widgets (`;chat box, enter text area`, `;display text in short/tall box`, objects at
  lines 2159-2174) and TEXTID labels 1079 `Modem`, 1081 `Modem Connect Menu`, 1084
  `Modem Setup Menu`, 1085 `Modem Type`. `IGLAY.BIG@IGCHAT.LAY` = in-game chat screen.
- FCLG catalog (`COMMON/FCENG.BIN`, 2,031 strings) netplay labels: 588-591 `Dial*`,
  913 `Initiate Network Game`, 938 `Join Network Game`, 1079-1086 `Modem*`,
  1203-1206 `Network*`, 1234 `No network game detected.`.
- No shipped `.LAY` (all 101 LAYOUTS + 30 IGLAY members) references 913/938/588-591/
  1203-1206/1234 → the Initiate/Join network menus are NOT `.LAY` screens; their labels
  resolve at runtime in scene code (exe IPX service strings `getnetgames/setnetgame/
  readnetgame/newnetgame`, EXECUTABLE.md §9). Netplay stays out of scope for the port.

## Element → draw mapping (what the compositor implements)
- Shape identity: first non-null of `SHAPEID, SNAME, RELEASEDSHAPEID, RELEASEDSHAPE,
  NORMALSHAPEID, NSHAPEID, BARSHAPEID`; else `SHAPEINDEX`/`DEFAULTSHAPENO` entry index.
- Art resolution (`ScreenArt`): authored `VPATH\fname` relative to the layout dir first;
  then tree-wide by base name (`.FSH` loose preferred over packed `.PAK`, authored
  directory wins ties). `.PAK` = RefPack → SHPI FSH — matches the EXE pack redirector
  (`FUN_004798dc`, GHIDRA_PASS3: `fe\pack\%s`, forces `.pak`).
- Placement: `(xposition, yposition)` + shape's native bitmap size. The layout
  `WIDTH/HEIGHT` is the interaction box, not the draw rect (author-side evidence:
  nvicons `qui1` 51×55 in a 55×50 box; aflag `0001` 193×100 in a 322×106 box).
- Flat panels (`fname` null/RECT): filled rect from `FILLCOLOUR/COLOUR/BOXCOLOUR`
  ARGB key (hex, `H…`/`0x…` forms); `FrameRect` type draws a 1-px outline.
  Text objects (`textid` = FC*.BIN string index, comma form `id,fontID`; measured FEFLOW05): resolved
  via `FeText`, drawn at the object position with
  `TEXTX/YOFFSET`/`TEXTXY=x,y,a` offsets, `ALIGNMENT` 1=left 2=right >=3=center-in-box; colour keys
  (`NORMALCOLOUR`, list-form `TEXTCOLOUR` first index) index the global FE palette. Unresolvable
  TEXTID (id 0 = dynamic/dummy, e.g. measured `textid=0,5`) keeps counting `noArt`.
- FontID → face (CONFIRMED 2026-11-06): the exe ships a font pointer table at DAT_0057d2c0
  (raw file offset 0x17b8c0) of 11 entries — `eras10, eras12, eras20, eras36, eras11i, eras8i,
  eras11bi, eras14bi, eras9bi, NULL, eras20bi` — and its loader loop (code VA 0x46933a:
  `mov edi,[eax+057d2c0]` → FUN_0045ca68 opens the table via "%s.ffn"; body at 0x46936f iterates
  ids 0..10 printing "lp %2d"). The 6 FE faces live in `FE/PACK/FEFONTS.BIG` (eras10, eras11i,
  eras12, eras20, eras36, eras8i), the 4 BI faces in `FE/PACK/IGFONTS.BIG` (Eras9bi/Eras11bi/
  Eras14bi/Eras20bi); loose copies under `FE/*.FFN`. Header lineHeights measured: eras10=22,
  eras8i=18, eras11i=24, eras12=26, eras20=44, eras36=79 px. `FeText::kFontNames` ports the table;
  labels draw at their face's native line height (sizePx = lineHeight → scale 1). The font header
  block also carries a colour array at raw 0x17b8fc (13 ARGB dwords: white, yellow, red, blue, …) —
  per-font label colours NOT yet mapped to fontIDs; label colour stays palette-key driven.

## Measured composites (text slice 2026-11-05, RX 7900 XTX)
| screen | objects → shapes | notes |
|---|---|---|
| `LAYOUTS.BIG@FEFLOW05.LAY` | 26 → 11 shapes + 100 text quads, 1 unresolved (text landed 2026-11-05) | main menu: France98 + EA SPORTS logos, tricolour, selector arrows, blue banners, FC*.BIN labels — PNG visually authentic; background flow bitmap composites over it (blending order is a known polish item) |
| `IGLAY.BIG@CONTROL1.LAY` | 27 → 9 (+17 text via FeText in the app compositor; render-demo passes no text, counted `noArt`) | controller setup: four 1998 gamepads + keyboards + pitch diagram; `redleft.fsh#mous` = shipped gap (see Open items) |
| `LAYOUTS.BIG@FEFLOW11.LAY` | → 24 shapes + 8 text rows (`--fe-team`, gate `fe_render_tournament`) | ZWCUP team-select pick grid — see section below |

## Open items
- Alpha/blend state per element type RESOLVED 2026-11-07 (see section below): no per-element GPU
  blend mode exists — colour state is palette-index switching (`normalColour`/`hiliteColour`) and
  art-state keys (`HILITEDSHAPEID`); hidden flags drive `shown()` only. IGLAY in-game HUD needs
  the match scene behind it (M5).
- Mouse cursor art RESOLVED 2026-11-06 (`presentation::loadFeCursor` + app overlay after each FE composite; gates: `test_presentation` tree checks, `fe_render_tournament` FAIL regex `cursors=0`). The engine hard-codes `.\fe\pack\maus.pak` at FIFAWC.EXE file offset 1451894 (bytes: `$AVER$FEPCV2-0-3      \0\0\0.\fe\pack\maus.pak\0\0.\\user\\options.sav\0\0VPATH\0FNAME\0SHAPEID\0BOXWIDTH\0`). No `curs` string exists in FIFAWC.EXE; no `.LAY` references maus — the cursor is drawn by engine code with LAY-style object keys (VPATH/FNAME/SHAPEID adjacent strings). MAUS.PAK is RefPack-wrapped and decompresses byte-identical to FE/MAUS.FSH; SHPI has 2 entries, both tag `curs`: entry0 off=32 w=28 h=26 (the arrow), entry1 off=1880 w=21 h=20. Selection among the two `curs` entries is Hypothesis: draw entry 0; hotspot from FshBitmap centerX/centerY (measured 0,0).
- Shipped gap: CONTROL1.LAY object `ls4` (`fname=redleft.fsh, sname=mous`) references a shape that does not exist on the shipped disc — FE/PACK/FEFLOW09/REDLEFT.PAK carries keyb/joys/gpad/grav only. The original silently skips it; NOT a loader bug (asserted in `test_presentation`).
- Gate: ctest `render_demo_lay` runs FEFLOW05 with `--min-shapes 8`, skipped without a tree;
  ctest `fe_flow_render` additionally asserts text quads > 0 (menu labels visible).

## ZWCUP team-select = FEFLOW11.LAY (2026-11-06)

**Confirmed.** The original "select your team" screen is `FE/PACK/LAYOUTS/LAYOUTS.BIG@FEFLOW11.LAY`
(scene name `feflow11`). The FE scene registry function `FUN_004479d0` registers the cluster string
block at `0x56237c`; its feflow11 entry is written at `0x00447ac3` with handler-slot pointers
`[+0x1c]=0x44d9c8`, `[+0x10]=0x44dda0`, `[+0x18]=0x44dfa8` (assembly dump: `analysis/pass7/grid7.txt`;
registration block incl. neighbours feflow09/10/13 in `grid5.txt` §feflow11 cluster/scenetbl).
**Hypothesis:** slot semantics (init/draw/tick) follow the fifa99 scene-table layout; not verified per-slot.

Grid objects (LAY grammar, all PC-merged): `back` = feflow11.fsh map background; `flag` = aflag.fsh
`0001` big banner flag; `titl`; `nptx` note text; `tmfg` = F002.FSH shape `f000` — the big current-team
flag slot (static default f002, France); `sbar` = sclbar.fsh scrollbar track (x=17 y=172 h=245);
buttons (`nvicons`, NEXT/CUP-style) and 8 group cells (`cntr`) + rank bars (`sbarsr`).
Row slots: `stfNN` flag cells (27×21, x=49, y=172+32·i), `ttNN` name column (x≈294, w69 h18),
`tmNN` row text boxes (x=52, w223 h26) — exactly 8 rows = our page size.

**Team → cell mapping:** the ZWCUP.RES pool order (40 ids, `tournament::parseZwcupRes`) fills rows
page-first: row i = pool index page·8+i. Row flags are dynamic shapes from `FE/PACK/GLOBAL F<fcdbid>.FSH`;
measured (SHPI directory probe 2026-11-06): every FNNN.FSH carries exactly one shape, tag `f000`.
The selected row's flag is also blitted over the `tmfg` slot. **Confirmed** = LAY objects, flag files/tags,
render output; **Hypothesis** = runtime per-row fname assignment by FCDB id (consistent with tmfg default
f002 and gate pick 35 → F035.FSH present).

Port: `app/fe_flow.cpp drawTeamsGrid` composites the LAY (`presentation::composite`) then overlays
row flags + FCDB STR0 names at the LAY's own slot positions, scrollbar thumb page-proportional in
`sbar`. Gate: ctest `fe_render_tournament` (--fe-team; evidence line `FEFLOW11: shapes=24 texts=8`,
FAIL regex on zero counts). Nav model unchanged (`TeamListNav` pages of 8 — matches the LAY's 8 slots).

## Alpha/blend & colour state per element type — RESOLVED (2026-11-07, pass 8)

**There is no per-element GPU blend mode.** The FE UI pass composites all LAY elements as alpha-blended
sprites; the only "blend" inputs are (a) the flat-panel ARGB key's alpha byte (`FILLCOLOUR`/`COLOUR`
`0x80…` = translucent panel, already implemented), (b) FSH palette-index-0 transparency. No widget-class
key table in the 80-key vocabulary carries a blend/filter mode — full per-class key dumps:
`analysis/pass8/layalpha_vocab.txt` (DGROUP blocks from 0x5643cc; button class lists
`RELEASEDSHAPEID, PUSHEDSHAPEID, HILITEDSHAPEID, DISABLEDSHAPEID, TEXTCOLOUR, NORMALCOLOUR,
HILITECOLOUR, MORPHER*, DELAYTICKS`; text/list classes list `TEXTCOLOUR, NORMALCOLOUR, HILITECOLOUR,
DISABLECOLOUR, FONTID, TEXTBACKCOLOUR, TEXTBACKSHADOW`).

**Colour state = global-palette index switching per widget state.** The key-compare parse chain in the
widget class constructor region disassembles 1:1 (`analysis/pass8/layalpha_disasm.txt`): at `0x46e0d9`
the compare against `"NORMALCOLOUR"` (string 0x5644ac) calls the int reader and stores into widget
field **+0x68**; the next compare `"HILITECOLOUR"` (0x5644bc, `0x46e10b`) stores into **+0x6C** — both
dword global-FE-palette indices. List class additionally ships `DISABLECOLOUR` in its key table.
The listbox draw routine `FUN_00477f54` @0x477f54 (`analysis/pass8/layalpha_widget.txt`) selects the
draw path per row: plain rows take the normal text path; the current/selected row (field compare vs
+0x3e/+0x40) takes the hilite text path (`FUN_0046a0f0`); a row whose u16 in the table at `[obj+0x68]
++row*2` has bit 0x8000 is drawn via the disabled path; widget field +0x18 bit 0x80 = widget-level
"no hilite". Button draw handlers `FUN_0046e2a6`/`FUN_0046e2b5` @0x46e2a6 branch on state byte +0x79
(1 normal, 2/3/10 pressed/hilite animation cases in the switch at 0x46e310) and +0x18 bit 4 (pressed).

**Art-state keys** select a different shape per state: `RELEASED/PUSHED/HILITED/DISABLEDSHAPEID`
(button key table strings 0x564438..0x564468; parsed into widget fields — same compare-chain form).
The screen's initial highlight target is `[screen] HILITEPANE` (parsed at layout.cpp:224; runtime
state setter `FUN_0044e178`).

**Port:** `presentation::composite` now honours the initial state — for the object named by
`screen.hilitePane` it prefers `HILITEDSHAPEID` as the shape tag and draws its label with
`HILITECOLOUR` (falling back to `NORMALCOLOUR`, then `TEXTCOLOUR` first index); every other pane uses
`NORMALCOLOUR`. Runtime state changes (focus messages) remain app-layer work. Gate: ctest
`test_presentation` tree checks build a synthetic screen with `HILITEPANE=TXT` +
`hilitecolour=2 normalcolour=1` and assert the glyph-quad rgba equals `pal[2]` vs `pal[1]`
deterministically (NullRenderer, real FFN/FCENG/PAL.PAK on WC98_TREE).

## Tournament presentation scenes (pass 8, 2026-11-06) — CONFIRMED

**Scene registry.** `FUN_004479d0` (size 1918) registers the whole FE scene cluster: per entry it
loads a Watcom p-string pointer (`MOV EAX,<name>`; string block base `0x56237c`, pointers are at
`string-8`) and writes handler slots `[+0x1c]=init [+0x10]=tick [+0x18]=draw`. Full enumeration of
all 49 registered `feflowNN` scenes with slot VAs: `analysis/pass8/tourney_registry_scenemaps.txt`
(raw assembly + string block dump: `tourney_registry.txt`; name->xref blocks:
`tourney_registry_entries.txt`). Consistency anchor: feflow11 (team-select, section above) =
init 0x44d9c8 / tick 0x44dda0 / draw 0x44dfa8.

**Screen -> scene mapping (CONFIRMED via LAY object keys + handler decompiles):**

| scene | LAY objects -> screen | handlers init/tick/draw |
|---|---|---|
| feflow19 | groupings board (`gpnm`, group columns) - group draw before the group stage | 0x452688 / 0x452884 / 0x4528e4 |
| feflow35 | standings rows `ssnN` name + `ssp/ssd/ssf/ssl/sst?` P/W/D/L points, `gpnm` group title -> **group standings table after the group stage** | 0x45838c / 0x458714 / 0x45889c |
| feflow25 | `tmNN`+`tfNN` entrant names/flags, `qfNN`, `qtmN` SF slots -> **quarterfinal board (after R16)** | 0x4554ec / 0x45570c / 0x455768 |
| feflow26 | `tm00..tm03`, `qtmN` SF pairs, **`wcup`/`wcps` trophy slots (LAY object comment: ";world cup model")** -> semifinal board + trophy presentation (after QF) | 0x455ae0 / 0x455d28 / 0x455d80 |
| feflow27 | full bracket `qfNN/qtmN/sfNN/stmN/ftmN`, **`chmp` champion slot**, `3mat/3tm*` third-place match -> **full-bracket/champion screen (after final)** | 0x455fd0 / 0x456030 / 0x456084 |
| feflow23 | knockout-round entry board (`sbar`) - per-pair matchup preview before each KO round | 0x4543dc / 0x4545fc / 0x454658 |

Handler decompiles: `analysis/pass8/tourney_handlers_raw.txt`; LAY text of the four implemented
screens (object name inventory): `analysis/pass8/tourney_lays.txt`. The trophy is presented with
the `wcup`/`wcps` sprite slots on feflow26 (WCUPF.MDL geometry per MDL_GEOMETRY_NOTES.md); 3D
trophy-model animation inside the ceremony remains unimplemented (sprite frame slot composites).

**Port:** `app/fe_flow.{hpp,cpp}` builds a deterministic tournament model from the ZWCUP pool
(fixed seeds, `TourneyModel`: groups + standings via `tournament::computeStandings`, knockout via
`KnockoutBracket::makeTree` played with a seeded result stream) and composites each between-round
boundary: round 0 -> FEFLOW35 (standings overlay at the LAY's own ssn/ss* slots), 1 -> FEFLOW25,
2 -> FEFLOW26 (+ wcup frame), 3 -> FEFLOW27 (bracket + chmp) with team names/flags from the model
(drawStandings/drawBracketBoard, row-overlay pattern of drawTeamsGrid). Gate: ctest
`fe_render_present` (`--fe-present --tree`, stat lines `FE-PRESENT FEFLOWxx: shapes=.. texts=..`,
FAIL on zero counts; WC98_TREE-gated, SKIP 2 when assets absent).

## Main-menu scene mapping + visibility semantics (2026-11-06, FeMenuFix)

**FEFLOW05.LAY is NOT the main menu.** Its header source name is
`D:\FIFA98.PC\SOURCE\FE.OLD\LAYOUTS\MNMSETUP.LAY` and its object inventory is modem/netplay
setup: `mneb` ";machine name edit box" (objecttype=10), two more string edit boxes
(objecttype=10/14, dial/init strings), five list widgets (objecttype=4, textid=0,5 / 209,4 /
4396,4 = FC-bank captions), seven slider objects, `help`, `rset`. The 26-object soup the port
rendered was this layout's netplay content painted all at once.

**True main-menu scene.** The FE scene registry (registrar FUN_004479d0; cluster string block
0x56237c) keys scenes by logical name: a sorted 47-entry name table (lookup code FUN_0047aeb0,
case-insensitive compare loop at 0x47aeb0..; index finder FUN_0047af2c) maps "mainmenu"
(string VA 0x564dd0) to a runtime window object in a struct array at DAT_0060e308 (stride 0x38,
count 0x32; per-slot creation code at 0x47afd4: `shl idx*56 + 0x60e308 -> FUN_0042be70`). No
feflowNN LAY carries menu item widgets (kt/menu objecttypes only appear in feflow73/74/79/80/82,
league-mode screens) — the original main-menu **item labels are drawn by scene-handler code from
strings embedded in FIFAWC.EXE**: "main menu" at 0x564f89 followed by item strings 'friendly'
(0x564fb1), 'league play' (0x564fbd), 'cup competition' (0x564fcd), 'practise' (0x564fe1)... The
port's menu composites the FE-flow art objects of FEFLOW05 (back collage + titl France98/EA logos
+ flag anim, all authentic FE background art) and draws "Friendly" / "Cup Competition" as app-side
labels (exe strings above), mirroring that handler-code semantics.

**Visibility semantics implemented in `presentation::composite`:**
1. *Scene ownership*: new `visible` whitelist parameter (objectnames). Objects of scenes the port
   never shows (MNMSETUP's netplay widgets on the menu screen) count as hidden and are not
   painted; stats keep counting them (`hidden`). App: `FeFlow::menuVisible_ = {back, titl, flag}`.
2. *Widget-class dynamic content*: List(4)/Edit(14)/CheckBox(10)/Button(2) objects carrying a
   TEXTID key are engine-driven list/edit captions painted from live item state; the port has no
   runtime item state, so static composites skip their label draws (the FC-bank "texts=100" soup).
3. SHOW flags and null-art boxes keep the pre-existing rules (`shown()`, FILLCOLOUR-gated boxes:
   a null.fsh placeholder with no fill colour paints nothing).

Gate evidence: ctest `fe_flow_render` FAIL regex asserts `FEFLOW05: shapes=`/`texts=` non-zero;
capture /tmp/wc98port-out/menufix2_fe00.png (background collage + logos, two readable item
labels at the markers, no overlap).
