# WC98 in-match HUD: assets, coordinates, port map

Scope: the presentation-layer 2D overlay (scoreboard strip, clock, radar panel, shirt-number
labels, controller ground markers) added to `MatchScene` for the exhibition slice. Layout logic is
the GPU-free layer in `presentation/src/hud.cpp`, gated headless by `tests/unit/test_hud.cpp`.
Probe evidence: `analysis/pass7/hud_probe.txt` (parses via the shipped `wc98::formats` API).

## Asset identification (measured)

| Piece | File | Member/tag | Measured facts |
|---|---|---|---|
| Match/scoreboard font | `FE/PACK/IGFONTS.BIG` | `eras14bi.ffn` | FFN v100, 209 glyphs, lineHeight 31, embedded FSH atlas; digits + A–Z present. Loose copy `FE/ERAS14BI.FFN` parses identically (probe §1/§1b; tree gate in `test_ffn`). |
| Font faces (context) | same BIG / loose FE | eras8i..36, 9bi/11bi/20bi | probe §1 lists all 10 with glyph counts + lineHeights. Face selection RESOLVED for the strip (Open item 2): HUD draw routine `FUN_00438670`@`0x4386de` does `MOV EAX,0` → `CALL 0x46a2a8`, which copies font record[index] (array at `DAT_0060b704`, stride 0xB8) into the text context template `DAT_00580148`; index 0 = name-table slot `0x57d2c0` = **eras10.ffn** → scoreboard strip text face is eras10, eras14bi assumption REFUTED for the strip. Shirt-label face not traced (Hypothesis). |
| Radar panel | `INGAME/MISC/MISC8.FSH` (+ MISC16 twin) | `rpit`, `rpi2` | 180x120 bitmaps, probe §2 (~l.272-273, 346-347). Selection rule RESOLVED (Open item 5): radar init `FUN_00414eec`@`0x41510b` branches on `DAT_005c65b0`; sole writer `FUN_00419ec8`@`0x419fb9` stores 0 → panel handle `DAT_005c61c4` = **rpit** (rpi2 branch dead). |
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
- **Clock string**: RESOLVED (pass-10, `analysis/pass10/hud/run.log`…`run7.log`). The in-match scoreboard clock is drawn by
  `FUN_00438670` (HUD strip routine, called from draw dispatcher `FUN_0040332c`): at the immediate site `0x43888c` it sprintfs
  `FUN_0049168e(buf,"%02d:%02d",v/60,v%60)` (`"%02d:%02d"` @ `0x55b540`, disasm `MOV EAX,[0x00641214]` → `CALL 0x438b48` at
   `0x43885a/0x438899`). Both fields are `%02d`: minutes width 2, **zero-padded** — the old "no leading zero" hypothesis is
  REFUTED (the `%d` + `:%02d` pieces at `0x561308`/`0x55a744` belong to menu strings). Clock *value*: getter `FUN_00438b48`
  scales the tick counter `DAT_00641214` by period length (`tick*2700/DAT_0064120c`), adds 2700 for side 2
  (`DAT_0064126c==1`), and clamps per mode switch `DAT_00641270` (case 0 ≤ 2700 s, case 1 → 5400, cases 2/3 ≤ 900).
  The four canonical `%02d:%02d` xref sites are NOT the scoreboard: `0x437c8c`→`FUN_00437c14` gamepad mini-HUD (mode switch
  `DAT_0060194c` in caller `FUN_0043ad60`; string pool adjacent to "Gamepad" label); `0x43dc2f`→`FUN_0043da30` clock-rotation
  animation draw (angle global `DAT_006024b0`, state machine caller `FUN_0043d2b4`); `0x43ffed`→`FUN_0043ffe8` practice/menu
  counters (`DAT_0064024c/0x50 % 100`, IGOPTN2/IGPRACT pool at `0x562104..0x562188`); `0x440285`→`FUN_004401a4` (function
  created at gap, caller `FUN_0043fbe0`) menu-button dispatch, not a clock draw. Clock *value* semantics remain ported facts:
  DAT_0065F1C8 = seconds in current period, DAT_0065F224 = period index (GHIDRA_PASS4/5 citations).

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
   Negative probe (2026-11-07): the HUD is NOT a layout file — all 30 `IGLAY.BIG` members extracted,
   zero hits for score/clock/time/`%02d` keys; scoreboard text is drawn procedurally from the exe.
2. Font face per HUD element: **RESOLVED for the scoreboard strip** (pass-10/12, `run5.log`/`run6.log`) — strip text is
   drawn with font record index 0 (`MOV EAX,0; CALL FUN_0046a2a8` @`0x4386de`, copies `DAT_0060b704[0]` (stride 0xB8) into the
   text context template `DAT_00580148`); name table at `0x57d2c0` slot 0 = **eras10.ffn** → eras14bi assumption REFUTED for
   the strip. Shirt-label face not traced (Hypothesis).
3. Radar dot kit recoloring (f99 ramp path absent from WC98 kits.cpp).
4. **RESOLVED** (see "Clock string" above): consumer = `FUN_00438670` immediate site `0x43888c`, format
   `"%02d:%02d"`@`0x55b540`; minutes `%02d` width 2 zero-padded (value via `FUN_00438b48(DAT_00641214)`). The four canonical
   xref sites are gamepad HUD (`FUN_00437c14`), rotation-animation draw (`FUN_0043da30` ← `FUN_0043d2b4`), practice/menu
   counters (`FUN_0043ffe8`) and a menu-button dispatcher (`FUN_004401a4`, created at gap) — none is the scoreboard clock.
5. **RESOLVED** (pass-10, `run.log`/`run6.log`): radar init `FUN_00414eec`@`0x41510b`: `CMP [DAT_005c65b0],0` → flag==0
   branch loads tag string `rpit` (`0x55a998`) via `FUN_0041d5fc` into panel handle `DAT_005c61c4`; nonzero branch preloads
   `rpi2` (`0x55a990`). Sole writer of the flag is the camera-pack loader `FUN_00419ec8`@`0x419fb9`, which stores 0 →
   **rpit always selected**; port behavior confirmed correct.
