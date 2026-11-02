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
- Text objects: not drawn (need FFN fonts + FC*.BIN strings — separate slice); counted `noArt`.

## Measured composites (2026-11-02, RX 7900 XTX)
| screen | objects → shapes | notes |
|---|---|---|
| `LAYOUTS.BIG@FEFLOW05.LAY` | 26 → 11 (+14 text, 1 unresolved) | main menu: France98 + EA SPORTS logos, tricolour, selector arrows, blue banners — PNG visually authentic; background flow bitmap composites over it (blending order is a known polish item) |
| `IGLAY.BIG@CONTROL1.LAY` | 27 → 9 (+17 text, 1 unresolved) | controller setup: four 1998 gamepads + keyboards + pitch diagram; unresolved `redleft.fsh#mous` = cursor sprite (maus.pak family) |

## Open items
- Alpha/blend state per element type (hidden flags drive `shown()` only); text slice;
  mouse cursor art; IGLAY in-game HUD needs the match scene behind it (M5).
- Gate: ctest `render_demo_lay` runs FEFLOW05 with `--min-shapes 8`, skipped without a tree.
