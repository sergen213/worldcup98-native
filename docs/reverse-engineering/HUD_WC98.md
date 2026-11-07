# WC98 in-match HUD: assets, coordinates, port map

Scope: the presentation-layer 2D overlay (scoreboard strip, clock, radar panel, shirt-number
labels, controller ground markers) added to `MatchScene` for the exhibition slice. Layout logic is
the GPU-free layer in `presentation/src/hud.cpp`, gated headless by `tests/unit/test_hud.cpp`.
Probe evidence: `analysis/pass7/hud_probe.txt` (parses via the shipped `wc98::formats` API).

## Asset identification (measured)

| Piece | File | Member/tag | Measured facts |
|---|---|---|---|
| Match/scoreboard font | `FE/PACK/IGFONTS.BIG` | `eras14bi.ffn` | FFN v100, 209 glyphs, lineHeight 31, embedded FSH atlas; digits + A–Z present. Loose copy `FE/ERAS14BI.FFN` parses identically (probe §1/§1b; tree gate in `test_ffn`). |
| Font faces (context) | same BIG / loose FE | eras8i..36, 9bi/11bi/20bi | probe §1 lists all 10 with glyph counts + lineHeights; the exe font routine table (GHIDRA_PASS3_RENDERING.md, loader `0x469ec8`) indexes `.ffn` names. Which face each HUD element selects is **not traced** → the port uses Eras14bi for strip + shirt labels (Hypothesis). |
| Radar panel | `INGAME/MISC/MISC8.FSH` (+ MISC16 twin) | `rpit`, `rpi2` | 180x120 bitmaps, probe §2 (~l.272-273, 346-347). |
| Player dots | same | `rpl0`, `rpl1` | 8x8 (probe ~l.269-270). Team-keyed art; f99 recolors by kit ramp — WC98 kits.cpp has no rampColor, the port draws raw art (open item). |
| Selector rings | same | `rsp0..rsp4` | 8x8 (probe ~l.289-293). Side/view pick index is f99 family (`rsp(side*5+view)`), WC98 unverified (Hypothesis). |
| Ball marker | MISC8/MISC16 | `ball` 64x64 | No `rso*` tags exist in WC98 (probe: zero hits); the port samples the `ball` sprite at 8x8 on the panel (Hypothesis). |
| Controller markers | same | `sb00..sb04` | 32x32 diamonds (probe ~l.317-323, 386-390), one art slot per controller; tint table `exe::kControllerTint` is the retained f99 family constant (`presentation/src/exe_tables.inc`). |
| Team abbreviations | `COMMON/FCDB_ENG.DBI` (STR0) | row i of team table | 1:1 with `COMMON/FCDB.DBI` team order, same join as `tools/dbexplorer`. Strip uppercases. |

## Coordinates recovered from FIFAWC.EXE

- **Radar panel rect**: float block VA `0x55a9a8` = `{533.0, 67.0}` (panel top-left), tele size
  180x120 = the `rpit` bitmap; init code computes the per-pitch scale factors (sx = 180/pitch
  length, sy = 120/pitch width) and stores them at runtime globals (disassembly this session;
  draw site of the panel is the rpit consumer). These are the defaults in `HudGeometry`.
- **Clock string**: DGROUP contains `%d` + `:%02d` format pieces (`0x561308`, `0x55a744` region);
  the drawing routine that consumes them was not pinned (no direct absolute xrefs found in a
  linear scan — Win98 code references DGROUP via base registers). Clock *value* semantics are
  ported facts: DAT_0065F1C8 = seconds in the current period, DAT_0065F224 = period index
  (GHIDRA_PASS4/GHIDRA_PASS5 citations in `app/match_demo.cpp`). Format `MM:SS` no leading zero
  on minutes follows the string pieces (Hypothesis: consumer unverified).

## Hypotheses + calibration knobs (all `[gameplay] hud_*` passthrough keys)

The scoreboard strip rectangle, name/score/clock slot centres and shirt-label placement were **not**
recovered from FIFAWC.EXE this session; defaults are the FIFA 99 family layout (f99
HUD_drawScoreClock fillrect model, `exe::kPopupTextColour` retained table) scaled to the 640x480
virtual screen. Every number is a knob in `HudGeometry` (`presentation/include/wc98/presentation/hud.hpp`):

`hud_sb_x hud_sb_w hud_sb_y hud_sb_h hud_name_cx0 hud_name_cx1 hud_score_cx hud_clock_cy
hud_radar_x hud_radar_y hud_radar_w hud_radar_h hud_dot_half hud_shirt_head_px hud_shirt_size`

wired in `app/match_demo.cpp` → `MatchScene::setHudGeometry`. Shirt-label head anchor is the
constant 70 game units above the feet (`kShirtHeadUnits`, f99 sibling value; Hypothesis).

## Draw order (port)

World batch first (controller ground diamonds under players, depth-tested), then SpriteBatch:
radar panel → ball marker → player dots → selector rings → scoreboard panel rects → names → score
→ clock → shirt-number labels. The ui batch rides `Frame::ui` (slot 3); enabled when
`s.hud.scoreboard || s.hud.radar`.

## Open items

1. Scoreboard strip rect + text slot coordinates: exe recovery incomplete (see above) — knob-calibrated.
2. Font face per HUD element (eras14bi assumed for strip and shirt labels).
3. Radar dot kit recoloring (f99 ramp path absent from WC98 kits.cpp).
4. `%02d` clock consumer routine; minutes field width. Format-string leads (measured 2026-11-07):
   `"%02d:%02d"` at VAs `0x55b540`, `0x562104`, `0x56217c`, `0x562188`; each has exactly one dword
   xref in `.text` (raw `0x37c8c/0x3dc2f/0x3ffed/0x40285` = VA `0x437c8c/0x43dc2f/0x43ffed/0x440285`)
   — next Ghidra pass: decompile those four sites, match against the scoreboard draw routine.
5. `rpi2` vs `rpit` camera-mode selection rule (port always uses `rpit`).
