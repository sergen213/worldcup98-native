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
| `LAYOUTS.BIG@FEFLOW11.LAY` | → 22 shapes + 13 text rows with 3 picks (`--fe-team --fe-team-select 3`; gate `fe_render_tournament`) | ZWCUP team-select pick grid — see section below |

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
page-first: row i = pool index page·8+i. Row flags are dynamic shapes from `FE/GLOBAL/FNNN.FSH`;
measured (SHPI directory probe 2026-11-06): every FNNN.FSH carries exactly one shape, tag `f000`.
The selected row's flag is also blitted over the `tmfg` slot. **RESOLVED (2026-11-07)** = file index NNN
= the team's record INDEX in FCDB.DBI, not its id: pool ids 86/87 sit at DBI records 30/31 and F030.FSH
f000 decodes to the Argentina shield, F031.FSH to Australia (render-demo dumps). Cup-pool ids (>46) have
no FNNN file by id — files span F000..F046 = the 56-record DBI's national-team records.

Port: `app/fe_flow.cpp drawTeamsGrid` composites the LAY (`presentation::composite`) then overlays
row flags + FCDB STR0 names at the LAY's own slot positions, scrollbar thumb window-proportional in
`sbar`. Gate: ctest `fe_render_tournament` (--fe-team; evidence line `FEFLOW11: shapes=.. texts=..`,
FAIL regex on zero counts) + `fe_screen_capture_team`. Nav model = continuous 8-row window
(`TeamListNav`, 174b3c3); pick state machine below.

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
   a null.fsh placeholder with no fill colour paints nothing). IG boards additionally call
   `app/livematch.cpp::strokeBoxes()` after composite: engine box widgets with null art and no
   fill draw a frame-only 1-px outline [pass-28audit].

Gate evidence: ctest `fe_flow_render` FAIL regex asserts `FEFLOW05: shapes=`/`texts=` non-zero;
capture /tmp/wc98port-out/menufix2_fe00.png (background collage + logos, two readable item
labels at the markers, no overlap).

## Team-select grid (pass-11, 2026-11-07)
- Pool pages from FCDB `teamdb` records (record fields: id, pool byte, flag file index -> FNNN loose
  `FE/GLOBAL/FNNN.FSH`, name offset). Pool 3 = Europe B (Argentina..Canada page 1); 56 teams total.
- FEFLOW11.LAY row slots: flags `stf0..stf7` (**single digit**, objecttype=3, cell 27x21 at x=49),
  name slots `tt00..tt07` (zero-padded) - mixed naming measured in the member text. Big current-team
  shield slot = `tmfg` (LAY default art f002); blit draws the selected team's FNNN over it at native
  bitmap size; row icons scale into the cell rect. Scrollbar thumb = flat quad in `sbar` track,
  page-proportional.
- **SpriteBatch UV rule**: textured draws take normalized uv `{0,0,1,1}` - passing `{}` (all-zero)
  samples texel(0,0) and paints the whole target rect with that one colour. All app-side dynamic
  overlays use the full-rect UV; kit strips keep explicit sub-rects from `Kits::stripRect`.

## Button-class interaction states + FRAMES animation (pass-12, 2026-11-07)

`composite(..., const FeUiState*)` adds pointer-driven widget state for the button classes
(objecttype 6 `ScrollBar`, 32 `Tbb`, plus 2/8/45): Press -> `PUSHEDSHAPEID/PUSHEDSHAPE`,
Hover/screen `HILITEPANE` -> `HILITEDSHAPEID/HILITEDSHAPE`, released-key chain as static default.
Evidence: Feflow06.lay hub buttons (`fe07` released `franim.fsh#0000`, side icons `quit` =
`nvicons.fsh#qui1` hilited `qui5`), widget key table 0x564458, runtime switch FUN_0044e178. **[C]**
- Hit box: `POINTS=N+,x,y,...` polygon (fe07 fan polygon) else the x/y/width/height rect.
- `FRAMES=a,b,c,d` = two frame ranges over the shape-name family of the widget's FSH: idle a..b,
  state c..d; ordinal maps onto the base tag digits (`qui1` + 6 -> `qui6`; `0000` + 3 -> `0003`).
  Measured qui1 frames `6,9,6,9`, franim `1,15,1,15`. Clock: 150 ms/frame. **[H]** (pair semantics
  from the two measured widgets; confirm against handler tick decode when MENU_NAV_WC98 lands)
- Pass-14 byte-lane update (2026-11-21), all addresses re-dumped this pass:
  - Data scope: `FRAMES=` observed on objecttype 6/32 widgets only within the corpus grepped =
    loose `WorldCup98_Extracted/FE/INGAME/*.LAY` + the 10 pass-11 packed dumps
    (`analysis/pass11/lays`); the full 156-member sweep was **not** re-run. Values always
    `(a,b,a,b)`: loose IGTEAM72/73/74/75/79/80/89 (`tmf3=11,15,11,15`, `gofd=6,10,6,10`,
    `gobk=1,5,1,5` over `igtman\icons.fsh` — the pass-12 "IGLOSER" label was a misattribution,
    the rows are IGTEAM73.LAY objects 58-60), CONTROL1 `1,4,1,4`, IGOPTN2 `0,0,0,0` (type 32);
    packed FEFLOW50/73/74/79/80/82 `16,19,16,19` / `11,14,11,14` / `1,4,1,4` / `56,59,56,59`,
    Feflow82/FEFLOW50 qui1 `6,9,6,9`.
  - Vocabulary (exe data, read at 0x564314..0x564510 delta 0x401a00): animseq keys
    `SEQUENCES/REWIND/BACKNFORTH/TICKSPERFRAME/LOADALLSEQS` @0x564314/20/28/34/44 +
    `FEPANE_ANIMTEXT`; list-class (id 4, FUN_0046de5c per LAYOUT.md) keys `MORPHER` @0x5644cc,
    `MORPHERXOFFSET/YOFFSET/MORPHERFIRSTSTRNO/MORPHERNUMSTRS`, `DELAYTICKS` @0x564510 (shared
    name with kt id 44) — these are **LAY key strings**, not warning messages (the earlier
    "Shape %s... does not exist in %s" attribution is retracted; the only imm32 ref to
    0x5644cc is `mov $0x5644cc,%edx` at 0x46dd1f inside the list-class parse chain).
  - Storage: tbb parse FUN_00485984 (0x485984 prologue `mov %eax,%esi`) key `FRAMES`
    (`mov $0x566628,%edx` at 0x485a5a) tokenizes 4 words (0x473a0c + 0x4c3f5e) and stores
    **zero-based** `dec %eax` values as u16 at widget `+0x54/+0x56/+0x58/+0x5a` (0x485a9b,
    0x485abc, 0x485add, 0x485af2). tbb key `FNAME` (`$0x566610` at 0x4859b7) →
    `mov %ax,0x3c(%esi)` at 0x4859ce.
  - Widget `+0x3C`: full-zone modrm scan found 26 `mov %ax,[reg+0x3C]` stores; 11 verified
    parse-time this+prior pass (each inside a key-compare chain: 0x4a5ca0 strcmp + 0x479b68 int
    reader + ret epilogue — e.g. 0x46dc0c keys 0x5643fc.., 0x476036 `$0x564a74`, 0x4763c2
    `$0x564a98`, 0x476e5a `$0x564abc`, 0x4859ce `$0x566610`), and 0x4db573 is a sim stack store
    (`mov %ax,0x2(%esp,%edi,1)`, 0x64101c array), not a widget write. No `incw/addw` form to
    `[reg+0x3C]` exists anywhere in the scanned 1.4 MB zone: no in-place advance of that slot.
  - 0x4870xx handler: reads `[esi+0x54]` (0x487101) and `[esi+0x52]` (0x487104) as u16 pair,
    iterates a stream cursor via 0x4875bc (reads one LE u16, `add $2,%eax` — a token cursor,
    terminated at 0xffff), string-byte walk at 0x487362..0x48739b. What class-registry widget
    `esi` names and what the loop produces is **[H]** — do not read it as proof of draw-time
    frame selection.
  - Clock facts re-dumped: 0x476269 `mov 0x636a14,%eax; cmp 0x48(%edx),%eax; jb ret;` then
    `mov 0x4c(%edx),%bl; add $4,%eax; inc %bl; mov %eax,0x48(%edx)` — next-event gate
    `[w+0x48] = stamp+4` (**40 ms** if `DAT_00636a14` is the 1/100-s multimedia stamp,
    pass-118audio fact) with byte counter `[w+0x4C]++`. 0x46e7e0 handler gates
    `cmpb $1,[w+0x60]`, fields `+0x90` (delay, -1 = off) and `+0x94` (start stamp); `now`
    comes from `call 0x43ae68` (unit [H]), and when `now-[w+0x94] > [w+0x90]` it walks
    `0x14(%ecx)` via 0x47a3fc with `'dely'` (0x64656c79) and resets `[w+0x94] = -1` — the
    DELAYTICKS watchdog (auto-repeat), not a frame clock.
  - Frame pump (class zone 0x4861xx-0x4867xx, aligned dumps): tick handler 0x486204
    `mov 0x636a14,%eax; cmp 0x68(%ecx),%eax; jl ret;` — gate `[w+0x68]`, period
    `[w+0x6E]>>16` loaded via `mov 0x6e(%ecx),%esi; sar $0x10,%esi`, next
    `[w+0x68] = stamp + period`; frame counter `[w+0x60]++`; consumes the FRAMES quad via
    overlapping `sar $0x10` dword reads — dword`0x52>>16`=u16@0x54, `0x54>>16`=u16@0x56,
    `0x56>>16`=u16@0x58, `0x58>>16`=u16@0x5A — counter compared against B=u16@0x56, wrap
    target A=u16@0x54 or C=u16@0x58 selected by `cmpb $2,0x5f(%ecx)`. Companion method
    0x486442 gates `[w+0x5c]` vs stamp, byte counter `[w+0x5a]++` to 20, then reloads a
    dword from `[w+0x40+([w+0x5e]>>24)*4]` into `[w+0x54]` and sets `[w+0x5c]=stamp+4`.
    Setter of the period u16 @`+0x6E` **[H]** (raw disp8 scans are misalignment-unreliable;
    the 0x470bec `+0x6e` hits are disp32 `+0x14e` artifacts). Class ids 0x21/0x22 are
    registered via `mov $id; call 0x472fd8` blocks at 0x486283/0x486752; the
    objecttype->class table is untouched. Exact quad semantics stay **[H]**; the port's
    150 ms fixed cycle is kept unchanged pending that mapping.
- Help pane: button `help=NNN,...` leading value = FC-bank string index painted into the
  `helppaneid=` widget (`objecttype=14`, fontid, centered alignment=3). **[C]** Feflow06.lay.

## IG summary-board bar/header fix + BOX-size fill rule (pass-43, 2026-11-18)
- **BOX-size fill rule**: IG-family LAY widgets anchor `width/height=12` and carry the real panel
  size in `BOXWIDTH/BOXHEIGHT`; the engine fills the BOX size, so `composite()` prefers it whenever
  declared (fe_screen.cpp). Audit scan (129 members, both LAYOUTS.BIG + IGLAY.BIG): 80 mismatching
  fill widgets across 15 members, all in IGLAY.BIG (`igstats` 21, `igflsum` 15, `igpract1` 8,
  `igscsum` 6, ... plus `mesg` box0) — LAYOUTS.BIG is clean, so FE captures are pixel-identical.
  `strokeBoxes` already used BOXWIDTH for type-43 frames; the fill lane now matches it.
- **Bottom bar (igflsum/igscsum/igstats/ighalf)**: LAY order paints the `typ6..typ9` caption
  widgets (textid 5086/5085/262/77 = remap 'Match Statistics'/'Bookings Summary'/'Score Summary'/
  'Back') BEFORE the white `bo25`/`box6..box9` RECT plates (boxwidth 140x19) that cover them —
  static-bank interpretation painted nothing. Fix: kind>=3 exclusion list covers typ6..typ9; the
  overlay repaints all four remap-resolved captions left-aligned at widget x (`paintLeft`,
  alignment=1 = widget x + TEXTXOFFSET) after composite plates. Blue chips on the plate left edge =
  `btn1/btn2` button.fsh#btnn composite shapes.
- **List headers**: `typ3/typ4/typ5` (igscsum textid 347/348/349 -> remap 1862 'Time' / 1351
  'Player' / 1828 'Team') painted left-aligned at widget x=200/261/380; the centered-on-column
  guess plus wrong third header ('Score' id 1603) replaced. Third list column = team name.
- **igstats page buttons**: `box6/box7` are exactly the bottom-bar left plates; the old centered
  kPageCap captions (soup ids 1771/329 'Statistics'/'Booking Summary') duplicated over the real
  bar labels — removed, pressed state stays `btnd`-over-`btnn` chip chrome on btn1/btn2.
- **Scroll arrows**: `up`/`down` stay composite-drawn on all boards via the `btnup -> INGAME.FSH`
  stem alias (pass-13 §D); the kind>=3 exclusion removed them from the whitelist, leaving bars
  without arrows.

## Team-select pick state machine + counter (pass-46fe11, 2026-11-22)

Decompile lane `analysis/pass45ctrl/subdecomp.txt` (slots feflow11 = init 0x44d9c8 / tick 0x44dda0
confirmed by the registry scan above; the click grammar lives in the +0x1c slot body):
- **tmNN click** (`fun_0044d9c8` [C]): row button `tm%02d` cycles the pool-record state byte
  (record array `DAT_00608af5`, stride 4, state at record byte +3): 0 → 1 (P1) → 2 (P2) → 0.
  Counter `DAT_00608bc0` (P1 total) is incremented/decremented on the transitions across 0;
  `DAT_00608bc4` (selection total) is apply-then-revert: a 0→1 that leaves `0x1f < bc4` plays
  the click sfx and rolls record + both counters back (max 31 selections accepted).
- **Open handler** (`fun_0044dda0` [C]): rescans the stride-4 +3 walk at scene open (boot-opened
  pool keeps its state across scene re-opens, `ram0x57cc48` one-shot gate), recomputes both
  counters, then calls the paint helper.
- **Counter paint** (`fun_0044dff8` [C]): when the selection total != 0 the pair `nopl`/`nopn`
  (type-20 text, fontID 1, `normalColour` 14) is drawn through `fun_004788bc` with the
  `DAT_005629e4` format `"%i"`; at 0 both captions stay hidden.
- **Continue gate**: the `fe18` edge (NEXT/CUP wedge) requires P1 counter ≥ 1; without a P1 pick
  it answers the click sfx and stays on screen. Enter on the grid mirrors a row click.

Port: `app::TeamPickPool` (`app/team_select.hpp`) mirrors the record walk incl. the apply-revert
cap; `FeFlow::clickTeamRow` + the `enter()` teamScreen branch drive it, `teamP1Count/teamFirstP1`
gate the continue edge (launch with the first state-1 row). `drawTeamsGrid` gates the `cntr` cell
and the state-gated red row name on state != 0, tints only an unselected cursor row (red text
paints over the tint otherwise), and `tmfg` shows the pool row-0 P1 shield. Capture lane
`--fe-team-select N` clicks rows 0..N-1 before the frame (evidence `FEFLOW11: shapes=22
texts=13 cursors=1`, counter digit + 3 cells + P1 name visible). Gate: `test_teamselect`
TeamPickPool asserts (cycle, bounds, cap revert at the 32nd pick, P2 takeover, resize zeroing).
