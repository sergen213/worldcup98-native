# .LAY Screen Compositor (M4 slice 2, 2026-11-02)

Front-end screens = `.LAY` text layouts + FSH art. Grammar parsed by
`formats/src/layout.cpp` (`LayoutDocument`/`ScreenLayout`, gate: `test_layout`);
the compositor lives in `tools/render-demo/main.cpp` (`--lay <spec> [--tree <dir>]`).

## Screen inventory (measured this pass)
- `FE/PACK/LAYOUTS/LAYOUTS.BIG` — 101 members, RefPack-wrapped; FE flow screens
  `FEFLOWxx.LAY` (+ non-LAY members). `IGLAY.BIG` — in-game layouts
  (`CONTROL1.LAY`, `IGOPTN2.LAY`, per-venue `IGteam7x.lay`…). Both archives decode
  through the same reader as loose `.LAY` files (48-byte-prefix variant handled).

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
  via `FeText` (FFN face eras14bi.ffn + FCENG.BIN strings), drawn at the object position with
  `TEXTX/YOFFSET`/`TEXTXY=x,y,a` offsets, `ALIGNMENT` 1=left 2=right >=3=center-in-box; colour keys
  (`NORMALCOLOUR`, list-form `TEXTCOLOUR` first index) index the global FE palette. Unresolvable
  TEXTID (id 0 = dynamic/dummy, e.g. measured `textid=0,5`) keeps counting `noArt`.

## Measured composites (text slice 2026-11-05, RX 7900 XTX)
| screen | objects → shapes | notes |
|---|---|---|
| `LAYOUTS.BIG@FEFLOW05.LAY` | 26 → 11 shapes + 100 text quads, 1 unresolved (text landed 2026-11-05) | main menu: France98 + EA SPORTS logos, tricolour, selector arrows, blue banners, FC*.BIN labels — PNG visually authentic; background flow bitmap composites over it (blending order is a known polish item) |
| `IGLAY.BIG@CONTROL1.LAY` | 27 → 9 (+17 text via FeText in the app compositor; render-demo passes no text, counted `noArt`) | controller setup: four 1998 gamepads + keyboards + pitch diagram; unresolved `redleft.fsh#mous` = cursor sprite (maus.pak family) |
| `LAYOUTS.BIG@FEFLOW11.LAY` | → 24 shapes + 8 text rows (`--fe-team`, gate `fe_render_tournament`) | ZWCUP team-select pick grid — see section below |

## Open items
- Alpha/blend state per element type (hidden flags drive `shown()` only);
  mouse cursor art; IGLAY in-game HUD needs the match scene behind it (M5).
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
