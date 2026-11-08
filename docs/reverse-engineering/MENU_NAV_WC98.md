# MENU_NAV_WC98 — FIFA: Road to World Cup 98 (FIFAWC.EXE) Menu Navigation Dossier

Generated 2026-11-07 from pass-12 evidence. Tags: **[C]** = decoded from code, **[H]** = inference/hypothesis.
Every claim cites a VA; raw file offsets use the PE map in `analysis/pass12/rawmap.txt` (AUTO VA 0x401000→raw 0x400; DGROUP VA 0x55A000→raw 0x158600).

Evidence streams (all preserved under `analysis/pass12/`):

| File | Content |
|---|---|
| `registry_all.txt` (`nav_reg3.py`, from `nav10_raw.txt`) | Full scene registry decode, 74 SCENEREG rows |
| `edges_table.txt` (`nav_edges.py`, from `nav1_raw.txt`) | All 435 `FUN_004461b4` call sites with resolved targets + per-scene summary |
| `nav_semantics.txt` (`nav_sem.py`, from `nav2/nav8/nav10`) | Per-scene event/key/cursor/tag analysis |
| `nav3_raw.txt`, `nav7_raw.txt`, `nav9_raw.txt`, `nav11_raw.txt` | Global key hub, 2001 producers, IG registration helpers, hub tail |

---

## 1. Scene registry model [C]

There are **three** registration blocks, all emitting the same pattern
`MOV EAX,name → CALL 0x446598 (name lookup) → MOV dword ptr [EAX+slot],handler`:

- **SCENEMAP** `FUN_004479d0`, sites 0x4479e3..0x4480c0 — 55 rows, scene names `feflowNN`
  (string cluster VA 0x562334..0x5625BC = raw 0x160934). Decoded by `nav_reg3.py`.
- **REGHELPERS** 0x448CEC / 0x44915C / 0x44932C / 0x449BB8 — register `feflow02`, `feflow03`,
  `feflow04`, `feflow05` (names at VA 0x56260C/0x562630/0x56266C/0x562694). These are the
  early menu screens; e.g. feflow04 = `+0x1c=0x449190` (its key handler, §4), tick `+0x10=0x4492C4`.
- **IGBLOCK** 0x43FB00..0x442700 — 15 in-game popup scenes: IGMENU, IGQUESHN, IGMSG, IGPRACT2,
  IGSCSUM, IGFLSUM, IGSTATS, IGCAMERA, IGPRACT1, IGCONSEL, IGOPTN2, igteam89, CONTROL1, …
  (e.g. IGMENU row site 0x43FBE5: tick=0x43FC40, draw=0x43FF8C, init=0x4401A4).

Slot layout per scene struct (stride 0x38 at DAT_0060E308, pass-11):
`+0x10` tick, `+0x14` event handler, `+0x18` draw, `+0x1c` init [C].

Note: `feflow05`, `feflow14`, `feflow17` are **registered/switched at runtime** (feflow05 via
REGHELPERS row 0x449BBD; feflow14/feflow17 only as switch targets — §4/§5), not SCENEMAP rows.
No LAY member named FEFLOW14/FEFLOW17 has a registry handler of its own [C].

## 2. Event model [C]

Events are posted to the active scene's `+0x14` slot (and to tick/init bodies that dispatch on
them). Handler entry registers: **EDX = event ID**, **EBX = event record**;
`[EBX] = payload dword`, `[EBX + 8] = widget tag fourcc`.

Event IDs observed as `CMP EDX/EAX, imm` (decimal/hex; evidence sites in `nav_semantics.txt`):

| ID | Hex | Meaning | Evidence |
|---|---|---|---|
| 1001 | 0x3E9 | widget activate/click | feflow19 init 0x452695, feflow09 0x44C33D |
| 1002 | 0x3EA | widget hover/next | feflow19 0x45268D |
| 1004 | 0x3EC | frame/timer tick family | feflow73 0x45B87F |
| 1017 | 0x3F9 | frame counter family | feflow73 0x45B874 |
| 2001 | 0x7D1 | **key press** (payload `[EBX]` = key code, `[EBX+8]` = widget tag) | KEYH 0x449195, feflow73 0x45B969 |
| 2002 | 0x7D2 | key release | feflow09 0x44C429 |
| 8001–8005 | 0x1F41–0x1F45 | netplay/timer events (multiplayer context) | feflow73 0x45BA0B, 0x45B86D/6D64E, 0x45B8D6/E7 |

Widget tags are 4-char little-endian fourccs compared against `[EBX+8]` / EAX:
layout anchors `'feNN'` (e.g. `'fe06'` 0x66653030 @0x452B3A, `'fe19'`, `'fe41'`, `'fe89'`),
list-row tags (`feflow73`: alphabetical ranges checked at 0x45B99B–0x45B9BC: `'lb00'` ≤ tag < `'cb03'`,
`'ab00'`; the LAY rows are `pn00..pn08`), numeric button tags `"  4".."  6"` (0x34202020–0x36202020,
feflow04 @0x4491A8), `'btnC'` 0x62757443 (@0x44919D), `'quit'` 0x71756974 (feflow09 @0x44C35E,
feflow04 @0x449202), `'medl'`, `'gmnc'` (IGMENU @0x4401CA), `'plre'`.

Key presses for the *global* key set are routed to hub **FUN_00445664** with
`EAX = scene-local handler address, EDX = [EBX] payload`; see §3.

## 3. Global-key hub FUN_004461b4-bridged dispatch (FUN_00445664) [C]

Disasm `nav3_raw.txt` + `nav11_raw.txt` (HUBTAIL). Each branch: guard checks,
`CALL ECX` (per-scene callback), optional `CALL 0x436A8C/0x4337BC/0x434AA8`, then
scene-switch `MOV EAX,name; CALL 0x4461B4`, then stores a **mode value** in DAT_00640FB4.

| payload EDX | target scene (name VA) | mode→DAT_00640FB4 | evidence |
|---|---|---|---|
| 2 | feflow06 (0x562270) | −1 | 0x4456D7–0x4456E8 |
| 3 | feflow11 (0x56227C) | 2 | 0x4456F3–0x445704 |
| 4 | feflow07 (0x562288) | 0 | 0x4456B9–0x4456CC |
| 5 | feflow32 (0x562294) | 10 (0xA) | 0x445713–0x445729 |
| 6 | feflow33 (0x5622A0) | 12 (0xC) | 0x445734–0x44574A |
| 7 | feflow90 (0x5622AC) | 9 | 0x445755–0x445779 (reads DAT_0060CC04, calls 0x46892C) |
| 8 | feflow04 (0x5622B8) | −1 | 0x445785–0x4457A3 (guarded by `CALL 0x4337BC`) |
| 9 | feflow02 (0x5622C4) | −1 | 0x4457AE–0x4457D9 (guards 0x4337BC + 0x434AA8) |
| 0x1E | feflow95 (0x5622D0) | — | 0x4456A3–0x4456A8 |

**[H] interpretation:** payload values are menu-context codes, not raw scan codes:
4 = "quit to feflow07", 2/8 = "return/back (feflow06/feflow04)", 3 = "confirm→feflow11" etc.
The producer region 0x4455F9–0x44565F (`nav7_raw.txt`) posts event `0x32` (50) via `CALL 0x4A2030`
before invoking scene switches, and hub codes 2..9 are the codes tested there.
- **Wedge tag compares [C]** (whole-file fourcc scan, 2026-11-07; fourcc immediates are little-endian dwords — byte-scan for `'feNN'` must search the *reversed* ASCII): feflow06's own handler body compares widget tags `'fe32'` @0x449ce5, `'fe33'` @0x449d4e, `'fe90'` @0x449c4d/0x449d92, `'fe11'` @0x449e29, `'fe07'` @0x449e30, `'repl'` @0x449c01, `'cred'` @0x449c11/0x449cf7. The hub producer region sites map keyboard payload codes onto the same wedge tags: 0x445a2a ('fe11'), 0x445a65 ('fe07'), 0x445aa1 ('fe32'), 0x445b4b ('fe33'), 0x445b6f ('fe90'). Same grammar in other scenes: 'fe07' @0x44b94f (feflow07), 'fe11' @0x452348; shared widget-hit table area at 0x485e37–0x486xxx. Keyboard and mouse converge on identical per-scene tag compares — navigation is data-bound to LAY widget tags, not a generic string dispatcher.
- Raw map formula for `.text`: `raw = VA - 0x400c00` (i.e. 0x400 + VA - 0x401000); DGROUP: `raw = VA - 0x401a00`. Verified against Ghidra byte reads at 0x436180/0x44A71C (2026-11-07 sfx pass).

## 4. Scene-switch primitive FUN_004461b4 [C]

Signature: **EAX = p-string scene name** (imm, `[DAT+table*4]`, or `[DAT]` load),
EDX/ECX = context params. All 435 sites resolved in `edges_table.txt`
(410 NAME/TABLE resolutions; remainder honestly `unresolved`, incl. runtime .bss loads).

Per-scene outgoing edges (from tick/init/draw/event bodies; full list in `edges_table.txt`):

```
feflow07+init -> feflow06 08 15 37 41 89      feflow08+init -> feflow06 07 09 15 37 41 89
feflow09+init -> feflow06 15 37 56 89         feflow11+draw/init -> feflow04 06 15 18 28 37 41 42 86 89
feflow19+init -> feflow06 15 18 20 37 41 42 56 89
feflow23/25/26+init -> feflow06 15 20 37 41 42 81 82 89
feflow35+init -> feflow06 15 20 27 37 41 42 56 81 82 89   feflow27+draw -> feflow86
IGMENU+init -> CONTROL1 IGOPTN2 igteam89      hub §3 + feflow04 handler (§5) = other edges.
```

Name string pool for dynamic switches: VA 0x5625C8..0x562A A0 (raw 0x160BC8+) contains
`feflow95, feflow17, feflow06, feflow14, feflow05, feflow37 …` — referenced by the hub tail and
feflow04 handler immediates (no static pointer table; direct `MOV EAX,imm`).

## 5. Per-screen navigation models

All bodies below cited from `nav_semantics.txt`; registry rows from `registry_all.txt`.

### feflow04 — main menu front ("mainmenu"/intro) [C]
Registry REGHELPERS site 0x449161: tick=0x4492C4, draw=0x449188 (JMP 0x44A71C), **init=0x449190**.
`FUN_0044A71C` = thunk `{PUSH EDX; EAX=0xB; XOR EDX,EDX; CALL 0x4833f8}` writing slot
DAT_0057dee4[0xb*12] (bytes raw 0x49B1C = `52 b8 0b000000 31d2 e8…`; nav13_raw.txt A71C). [C]
Init body = the menu key handler (`nav11.txt` F04KEY):
- `CMP EDX,0x7D1` → if key press: `CALL hub(EDX=[EBX])` (§3) — global keys take over. [C] 0x449195/0x4491C9–D0
- `'btnC'` (0x62757443) widget event; tag fourcc range `"  4".."  6"` (@0x4491A8–0x4491B6):
  - tag == `"  4"` (JBE @0x4491B6 → 0x449230): `CALL 0x436468; CALL 0x44A71C; switch feflow14`
    (0x56263C) via stub fn 0x44923A, site 0x44923F. [C]
  - tag == `"  5"` (@0x4491BC): setup `CALL 0x436468; CALL 0x46F310; FUN_00444874(EBX=0x4361D0 pump cb)`,
    then `FUN_00436180(EAX=0x433150/0x433134 tmflag-animation callback, EDX=event code 0xC6/0xC0/0xC2)`
    (@0x449246–0x4492AC). [C] NOT sound: stores cb ptr → DAT_00600688 (+tick counters 0x600684=20,
    0x600690=50) and routes code through `FUN_0043c9a0` table DAT_005774a4 → selector dispatch
    `FUN_004447dc` (→ switch tmflag/mesg/feflow10). Pump `FUN_004361d0` counts down, calls audio
    ramp `FUN_0048ec64(EAX=0xc8)` + the registered cb. See `analysis/pass12/sfx_fe.txt`. [C]
  - tag == `"  6"` (JNC @0x4491E2): `'fe06'`-context → `CALL 0x44A71C; switch feflow05` (0x562648)
    via stub fn 0x4491F0, site 0x4491F5. [C]
  - `'quit'` tag (@0x449202): posts synthetic event `(EAX=0x3E9, EBX=0x82E, EDX=0x10F3)` via
    `CALL 0x459410`, then `CALL 0x44A71C; switch feflow37` (0x562654) @0x44921F–0x449229. [C]
- default → RET.

### feflow05 — REGHELPERS scene (intro/movie context) [C]
Row 0x449BBD: tick=0x449B14, draw=0x449A40, init=0x4494A0. Body 0x4494A0 fetches resources by
fourcc `'fe04'`, `'fe02'`, `'plre'`, `'cred'` via `FUN_0047A3E8(EDX=fourcc)` (@0x449BE8–0x449C15) —
presentation scene driven by LAY composite, no key loop; enters via hub code 2/8 & feflow04.

### feflow06 / feflow37 / feflow15 / feflow89 — video/movie context scenes [C]
Presenters (draw-only families); every menu screen switches *through* them on movie playback
(edges_table PER-SCENE: all interactive screens list `feflow06/37/41/89` targets).

### feflow07, feflow08 — league/cup config contexts [C]
Rows 0x447A37 / 0x447A5D with full 4-slot sets (event=0x44A770 / 0x44B858). Init bodies issue the
bulk of scene switches (edges_table PER-SCENE: 6–7 targets each): config-commit paths to feflow15
(team select), feflow37/41 (presentation), feflow89; back-edges feflow06. Event handlers process
key events; global keys route via hub FUN_00445664 (§3).

### feflow09 — friendly-match config [C]
Row 0x447A83. init=0x44C330 dispatches 1001 (@0x44C33D), 2001/2002 keypress/release
(@0x44C41B/0x44C429); widget tags `'quit'` (@0x44C35E), `'fe06'`, `'fe08'` (@0x44C3D3/C3C8).
Edges: → feflow06, 15, 37, 56, 89 (init-body switch sites in edges_table).

### feflow13 — world-cup config + presentation [C]
Row 0x447AE7: tick=0x44E578 draw=0x44E580 init=0x44E440 (no `+0x14`): events consumed by init.
Presentation composite per port notes; switches to presentation screens 25/26/27 via feflow35 family.

### feflow19 — "Core Team Development" / credits + netplay entry [C]
Rows: tick=0x452884 (dispatches 1001/1002 @0x452B15/0x452B0D; tags `'fe19','fe89','fe06','fe15'`
@0x452B24–0x452B3A), init=0x452688 (1001/1002 @0x452695/0x45268D, 2001 @0x4526C1; tags
`'fe41','fe15','fe06'`). Cursor `INC ESI/ECX` @0x452A19/0x452A2A. Edges → feflow06 15 18 20 37 41 42 56 89.

### feflow50 — practise-config [C]
Row 0x447E05: tick=0x459F7C init=0x459E60. Two-axis row/value model (port parity audit); keypress
dispatch in init body; edges per edges_table (→ feflow06/15/37/89 family).

### feflow52 — stub presenter [C]
Row 0x447E43: tick=0x45A128, init=0x45A120 (thunk); no key loop.

### feflow73 — League Play team list (`pn00..pn08`) [C]
Row 0x447E99: tick=0x45C800, draw=0x45C8DC, init=0x45B860. Init body (the scene's key loop):
- dispatch event IDs 1004/1017 (`CMP EAX,0x3EC/0x3F9` @0x45B87F/0x45B874), 2001 (@0x45B969),
  8001–8005 netplay events (@0x45BA0B, 0x45B86D, 0x45B8D6/E7).
- widget tag range checks on `[EBX+8]`: `'lb00'`/`'cb03'`/`'ab00'` bounds (@0x45B99B–0x45B9BC)
  → list rows; `'mdel'` fourcc @0x45B8F2.
- cursor globals **DAT_0060A694** (count/pos) and **DAT_0060A698** (`CMP …,-1` guards @0x45B8A3,
  `DEC ESI; MOV [DAT_0060A694],ESI` @0x45B9DD–E0), row redraw helper `CALL 0x45CFAC`.
- key press → `MOV EAX,0x45B820; CALL hub(EDX=[EBX])` @0x45BAB1–B8 (global keys, §3).
- cursor arithmetic: `INC EDX` @0x45B960.

### feflow74 — team grid / kit select [C]
Row 0x447EB8: tick=0x45E8E8 init=0x45DF84; FCDB fidelity per port parity notes; key loop in init body.

### feflow79 — tournament bracket/config [C]
Row 0x447F15: tick=0x460AF4, draw=0x460CAC, init=0x45FF14 (no `+0x14`).

### feflow80 — squad/lineup screen [C]
Row 0x447F34: full 4-slot set incl. **event=0x461ED0**; tick=0x463098 init=0x461EDC; largest key loop.

### feflow14 / feflow17 — dynamic switch targets [C]
No registry rows. Reached only via `FUN_004461b4` sites: feflow14 from feflow04 tag `"  4"`
(site 0x44923F), feflow17 from SCENEMAP-tail pump fn 0x44818C (site 0x4483D8). Both are LAY-backed
screens (`LAYOUTS.BIG` members) whose navigation is the composite of their loader's scene handler.

### IG popups (in-game) [C]
Rows in IGBLOCK; switch helpers `FUN_00439FC8/0x43A3F4/0x4401A4` (`edges_table.txt` sites
0x43A01A→IGMENU, 0x43A079→IGSTATS, 0x43A0D7–0x43A275→IGQUESHN/IGHILITE/IGSCSUM/IGFLSUM/CONTROL1/
igteam89/IGOPTN2, 0x440260→IGOPTN2).
- **igaoptns / iggoptns / igmoptns** (LAY popups) render under IGOPTN2 (row site 0x441FE1 region):
  option rows with textid help panes; key loop in tick=0x441FFC/init=0x442000.
- **igexit** → exit-confirm posts event + switch to feflow07 via hub code 4 (§3). [H for label]
- **igsavelo / igload** → save/load list popups under IGSTATS family (tick=0x441278, init=0x441744);
  list cursor `INC EAX` @0x440337 pattern shared with IGMENU.
- **IGMENU** tick=0x43FC40: tag `'gmnc'` (@0x4401CA), cursor `INC EDX` @0x43FCD0; init body =
  the popup dispatch (edges → CONTROL1, igteam89, IGOPTN2).

## 6. Navigation grammar (15 interactive screens) [C]

Composing pattern per screen:

1. **Entry**: `FUN_004461b4(name)` from a producer scene's tick/init body (edges_table),
   with the mode global DAT_00640FB4 set by hub branches (§3).
2. **Widget dispatch**: every key/mouse event arrives at slot +0x14 (or init body) as
   `EDX=eventID ∈ {1001,1002,1003,1004,1017,2001,2002,8001..8005}`; the record EBX carries
   `[EBX]=payload key code`, `[EBX+8]=widget fourcc`. Handlers do `CMP EAX,[EBX+8]` against
   fourcc constants (range checks for list rows: alphabetical ranges, e.g. feflow73 0x45B99B).
3. **Cursor movement**: per-scene `.bss` cursor globals incremented/decremented with bounds
   compares (`INC/DEC` + `CMP reg,[cnt]`), redraw helper per scene (e.g. feflow73 `CALL 0x45CFAC`).
4. **Help pane / labels**: text drawn via `FUN_00479350(string VA)`; string args resolved in
   `nav_semantics.txt` STR lines; FC-bank `(offset,index)` pairs are consumed by LAY list widgets
   (port side: `fe_flow.cpp`), not by the scene handler itself. [C/H boundary]
5. **Commit**: on activate (1001 or hub code 3) the body calls scene-switch with target name;
   presentation screens feflow06/37/41/89 sit between menus and match start.
6. **Back/quit**: `'quit'` widget tag → synthetic event post `FUN_00459410(EAX=0x3E9,EBX=0x82E,EDX=0x10F3)`
   then switch (feflow04 @0x449209–0x449229); hub codes 2/8 = return edges.

## 7. Unresolved [H]/open

- Hub payload codes {2,3,4,5,6,7,8,9,0x1E} semantic meaning is **[H]** (menu-context key IDs);
  raw scan-code mapping table not recovered — the FE keyboard driver posts them upstream of 0x4455F9
  region; GetAsyncKeyState site 0x4D0529 / GetKeyState 0x4CE1E4 are in-game lanes only.
- Cursor globals live in `.bss` (VA ≥ 0x598000): no file backing, initial values runtime-only.
- DAT_00640FB4 mode-value consumers not traced per-scene (out of slice).
- EULZ/animseq art presentation: out of scope (pass-9 negative result).
- `feflow17` producer condition inside fn 0x44818C (SCENEMAP tail pump) not fully decoded;
  site evidence only.

## 8. Key-code mapping table (observed constants, with evidence) [C]

| Const | Dec | Role | Evidence site |
|---|---|---|---|
| 0x3E9/0x3EA/0x3EB | 1001/1002/1003 | widget activate / hover / press-family | feflow19 0x452695, 0x45268D; feflow09 0x44C33D |
| 0x3EC / 0x3F9 | 1004 / 1017 | frame/timer family | feflow73 0x45B87F, 0x45B874 |
| 0x7D1 / 0x7D2 | 2001/2002 | key press / release | KEYH 0x449195; feflow09 0x44C41B/0x44C429 |
| 0x1F41–0x1F45 | 8001–8005 | netplay/timer events | feflow73 0x45BA0B, 0x45B86D, 0x45B8D6, 0x45B8E7 |
| 0x32 (post) | 50 | scene-load event post via `CALL 0x4A2030` | producer region 0x445634 |
| fourcc `'btnC'`,`'quit'`,`'feNN'`,`'gmnc'`, `"  N"` | — | widget tags (§2) | as listed §2 |

## 9. Widget-hit wedge dispatcher (0x485d00 block) [C]

Raw disasm of the shared FE widget-hit handler at VA 0x485e28–0x486110 (`raw = VA-0x400c00`): a compare chain over the clicked widget's fourcc — `cmp $'fe33',EDX @0x485e35`, `'fe11' @0x485e4c`, `'fe90' @0x485e63`, `'fe32' @0x485e75`, `'fe07' @0x485e88` — each paired with `cmp DAT_0057e364,reg` (runtime current-widget tag global). On match the branch calls FUN_004b3ce0 / builds a target via FUN_00479f30 under mode switches (`cmp $0x2,%dl`, `%dh in {3,5}`) = mouse click on a hub wedge performs the same scene switch as the keyboard path (§3). Confirms: mouse and keyboard converge on tag-bound per-wedge branches.

## 10. Per-scene fourcc key inventories (tick-body scan, raw objdump + reversed-ASCII immediates) [C]

Method: disassemble each scene tick window (`raw = VA-0x400c00`), collect `cmp $imm32,{edx,eax,...}` immediates that decode as printable fourcc (stored byte-swapped). Window bounds per §1 registry slots.

| scene | window | tags |
|---|---|---|
| feflow13 (WC config) | 0x44e440..0x44e578 | `fe06 fe08 fe15 quit` |
| feflow16 (lineups) | 0x4519c0..0x452328 (init+tick+draw, full window) | — (cursor-only: row-cursor `INC EDX` at 0x451b76/0x451e6c; no fourcc compares; keys arrive as global payload codes) |
| feflow18 (zones) | 0x452328..0x4524f8 | `fe06 fe11 fe15 fe19 fe41 fe42 fe89 quit rand` |
| feflow23/24 (brackets) | 0x4543dc.. / 0x454e0c.. | `fe06 fe15 fe41 fe42 fe89 frwd quit strk stsc` |
| feflow35 (standings) | 0x45838c..0x458714 | + `nglt ngrt` (group left/right nav) |
| feflow87/91 (controller grids) | 0x467970../0x468b50.. | anchors only (`fe06 fe08? fe15 quit`) — grid interaction via generic row cursor |

Semantics: `rand` = randomize zones; `strk` = start/launch tournament path; `stsc` = standings/stats view (FEFLOW20/35 grammar); `frwd` = forward anchor; `nglt/ngrt` = group page nav. Full sweep data files: `/mnt/1st4TB/WorldCup98/analysis/pass12/{wcfamily,rank2,keyloop}_tags.txt`.

## 11. League-config family fidelity findings (FEFLOW72/74/75/76/78/79/80, pass-12 QA) [C]

- Engine textID soup rule: widgets named `0000` (four share the name on feflow74 @x=60/245/445/475,y=110),
  label families `tt*`/`ad*` (feflow80 grid) and `mt9x` carry textID into FC-bank indices whose DEFAULTS
  are other contexts' captions ("Are you sure want to Quit?", "Mod SFX", controller labels). Composite
  whitelists exclude them; the scene handler paints runtime content per mode. Verified by capture QA:
  soup disappears, rows stay clean (gates fe_league_cfg*).
- feflow72 header slots `t10x`/`t11x`: engine-composed at init via sprintf `%s%d` from FCDB tactics/strategy
  (fourcc 't110'..'t112' FUN_004788b4 @0x45af41, strcat lane FUN_0049168e; formats "%s" @0x5637fc region).
  Static tid=0 = bank #0 ("FCLG") soup -> excluded from lg72Visible_.
- feflow72 buttons `b010..b012` captions: textid=233,4 measured in LAY; FCENG.BIN string #233 = "Art Director:"
  (bank credits-section index reuse — the Windows original shows exactly this on the formation-budget screen).
  Port paints shape bt10/bt20/bt30 + caption over the widget (composite skips objecttype-4 TEXTID content).
- feflow76 popup at rest: '0000' slot default = "New Memory card inserted." prompt text; X/✓ buttons + s001
  slider render as authored. Context captions remain engine-driven [H].
- kitColor(): FCDB 5-bit shirt colour indices index analysis/fifa98edit/__init__.py hslCols VERBATIM in table
  order (idx0=(242,165,237) pink ... idx19=(0,2,244) white, idx22=(0,0,40)). A rotated/fabricated table
  painted Argentina/Brazil swatches magenta; verbatim copy fixes it (presentation/src/fe_screen.cpp kitColor).
