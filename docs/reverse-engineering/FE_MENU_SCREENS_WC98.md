# FE Menu Screens — FIFA: Road to World Cup 98 (Windows)

Evidence dossier for 1:1 reproduction of the Windows-version front-end menu screens.
Every claim cites a dump in `analysis/pass11/` (raw Ghidra decompiles/disassembly, LAY member text).
Binary: `WorldCup98_Extracted/FIFAWC.EXE` (Watcom LE; string references exist only as runtime fixups — static xrefs to labels are absent by design, confirmed negative in `menu6_label_sites.txt`).

Legend: **[C]** = CONFIRMED from cited decompile/disassembly. **[H]** = HYPOTHESIS.

## 1. Scene registry (authoritative)

`FUN_004479d0` registers **66** scenes; each entry writes the scene p-string pointer (cluster block, base ~`0x562334`) and handler slots:
`[+0x1c]=init  [+0x10]=tick  [+0x18]=draw  [+0x14]=event`. **[C]** `menu5_scenemap_windowobj.txt` (full linear scan of 0x4479d0, "SCENE site=… lines"), raw asm `analysis/pass8/tourney_registry.txt`.

Menu-screen handler triplets (all **[C]**, from the SCENE lines):

| scene | init | tick | draw | event |
|---|---|---|---|---|
| feflow09 | 0x44c330 | 0x44c6c0 | 0x44c940 | 0x44c958 |
| feflow11 | 0x44d9c8 | 0x44dda0 | 0x44dfa8 (thunk→`FUN_0044a210`) | — |
| feflow13 | 0x44e440 | 0x44e578 | 0x44e580 (all thunks→`FUN_0044a210`) | — |
| feflow16 | 0x4519c0 | 0x451aac | none | — |
| feflow50 | 0x459e60 | 0x459f7c | 0x459fe4 | — |
| feflow73 | 0x45b860 | 0x45c800 | 0x45c8dc | — |
| feflow74 | 0x45df84 | 0x45e8e8 | 0x45e99c | — |
| feflow79 | 0x45ff14 | 0x460af4 | 0x460cac | — |
| feflow80 | 0x461edc | 0x463098 | 0x463168 | 0x461ed0 |
| feflow82 | 0x465854 | 0x465b0c | 0x46584c | — |

Pseudocode of these handlers: `menu7_handlers.txt` (feflow09/11 sections decompiled; 13/16/50/73/74/79/80/82 marked MISSING = entry points are 8-bit thunks with no function prologue, Ghidra has no function object there — disassembly of the raw slot bodies required for full decode).

Shared draw thunk `FUN_0044a210` **[C]** `menu12_painter_helpers.txt`: resolves the window-object via `FUN_0047a3e8`, gates on a per-scene animation counter (`FUN_004c321c % 0x28`), then calls `FUN_00484424/00482bfc/00483fa0` (compositor present/hide ops). It draws no primitives itself — content comes from the LAY widget tree of the scene's window object.

## 2. Window-object subsystem (scene ↔ LAY bridge)

- Logical-name → window-object table: struct array `DAT_0060e308`, stride **0x38**, count **0x32**; slot creation at 0x47afd4 (`shl idx*56 + 0x60e308` → `FUN_0042be70`). Name lookup binary search `FUN_0047aeb0`, index finder `FUN_0047af2c`. **[C]** disasm of the dispatcher `menu15_record_parser_disasm.txt`; subsystem pseudocode `menu11_windowobj_subsystem.txt`.
- Lazy creation opens `"fepack\%s.all"` (`FUN_0047a12c`), iterate-all helper `FUN_0047a268` walks the 0x32 slots. **[C]** `menu11_windowobj_subsystem.txt`.
- Scene-name p-string cluster (VA ~0x562334…0x5625c8) strings verbatim in `menu1_strings_564d00.txt`: scene ids `mainmenu`, `friendly`, `worldcup`, `league`, `cselect`, `matchsel`, `mnselect`, `mnmsetup`, `options`, `ingame`, `igcntrol`, `igoption`, `pencontr`, `penteam`, `wcqualif`, `wcshedq`, `wcztsel`, `lleagsel`, …
- The same cluster block carries the LAY screen-key vocabulary: **`HILITEDSHAPEIDOFF` / `HILITEDSHAPEIDON` / `HELPANEID`** **[C]** (`menu1_strings_564d00.txt`) — per-item selection/hilite art is a *screen key* on the `[screen]` header of each .LAY (see §3 grammar), i.e. hilite shape ids are LAY-level, not handler constants.

## 3. LAY grammar (geometry source)

Member text dumps: `analysis/pass11/lays/*.LAY` (+ full inventory parse `screen_inventory.txt`, bank strings `FCENG.BIN.json`). Grammar per member: `[screen] numobjects=N`, then per `[object N]`: `objecttype`, `objectname`, `xposition`, `yposition`, `wh`, optional `textid=<bank>,<index>` (FC-bank string), `font`, `ncol/hcol/tcol` (normal/hilite/text color), `shape=`. Compositor semantics: `docs/reverse-engineering/LAY_COMPOSITOR_NOTES.md`.
Object types observed **[C]**: 2=shape blit, 3=animseq (flag), 4=button (text+shape), 5=title shape, 6=sliderbtn arrow (sbln/sbrn/bak1), 0=text box, 10=edit box, 11=slider bar (sldb), 14=help pane, 42/44=menu (kt/menu list items).

## 4. Main menu

**[C]** The entry menu is code-drawn: logical name `mainmenu` string VA **0x564dd0**; item labels are exe strings — "main menu" **0x564f89**, "friendly" **0x564fb1**, "league play" **0x564fbd**, "cup competition" **0x564fcd**, "practise" **0x564fe1**.
**[C] negative:** byte-pattern scan of the whole image for dwords = these label VAs finds **no code sites**; every hit is inside the registry `FUN_004479d0` (scene p-string writes) — `menu6_label_sites.txt` (HITS 11: all CLUSTER[0..48] + data ptr 0x57d7fc). The labels therefore reach the screen through the runtime window-object/string-bank path (§2), not via immediates; per-item draw x/y cannot be recovered from static code and must come from the `mainmenu` `.all`/LAY resource at runtime.
**[H]** Item→scene jumps map to cluster scene ids: friendly→`feflow09` (friendly setup), league play→league scenes feflow73+ (`lleagsel`, "league"), cup competition→`feflow13`/world-cup family, practise→`feflow50`. Basis: scene-name strings + registry; Enter-key dispatch is in the mainmenu tick handler which has no Ghidra function object (thunk), decode pending.
Selection art per item = `HILITEDSHAPEIDON/OFF` screen keys (§2) **[C vocabulary, H for the mainmenu instance]**.

## 5. Friendly/practise setup — feflow09 + feflow50

feflow09 (friendly match setup) handlers §1 **[C]** pseudocode `menu7_handlers.txt`.
LAY: **FEFLOW09.LAY** (`lays/FEFLOW09.LAY`) — 8 config rows per team pair. Per-row widgets with exact coordinates **[C]**:
- row art `ovr{n}` x=82 y=193 (row0), 250, 307 … (+57 per row); left/right option arrows `lrp{n}`/`rrp{n}` x=82/344 same y; `<-->` pair `alp{n}`/`arp{n}` x=82/289; animseq `anm{n}` x=82,193; centre arrow `arw{n}` x=271,y=210(+57); config caption text box `ctx{n}` x=290,y=200(+57), `textid` dynamic.
- team-name texts `tltx` x=166,y=144 / `trtx` x=424,y=144; flags `ltfg`/`rtfg` x=104/362,y=165; title `titl` 10,9.
Row templates in the handler use printf-style widget names: `"tm%02d"` (s_tm_02d @0x56294c), `"tc%02d"`, `"tt%02d"` @0x5629e8/0x5629f0, `"stf%01d"` @0x5629f8 **[C]** `menu12_painter_helpers.txt` (FUN_0044e178 loops 7 rows; FUN_0044e368 sets stf colors).
Arrows/hilite painter: `FUN_0044ccd4` draws anm/lrp/rrp/alp/arp per state (`DAT_00608768` state array), hides via `FUN_0046bfac/bfcc`, sets widget x/y copy from arp (**[C]** same dump).
Transitions **[C]** feflow11-init code path: scene messages compared as dwords (e.g. `"del"`-family 0x64656c79) + `FUN_004461b4` pushes cluster scene names; feflow09 tick 0x44c6c0 body decode pending (thunk-less region).
feflow50 = practise setup, same widget grammar (**[H]** geometry identical to FEFLOW50.LAY `lays/FEFLOW50.LAY`); handlers §1.

## 6. Team select — feflow11

LAY: **FEFLOW11.LAY** (`lays/`). Init `FUN_0044d9c8` **[C]** `menu7_handlers.txt:300-528`: guards on messages 0x3e9/0x7d1/0x7d2; builds per-team rows via `"tm%02d"` template loop (compare against selected team id at +8), calls FUN_0044e0cc/…178/…368 row painters (§5) and `FUN_0047a3e8` widget lookup; sets state globals DAT_0057ce30/4, DAT_00608bd0-8.
Draw = shared thunk `FUN_0044a210` (§1). Tick 0x44dda0 **[C]** in `menu7_handlers.txt:458`.

## 7. World-cup setup — feflow13

Handlers §1 all thunks→shared painter; LAY **FEFLOW13.LAY** (`lays/`). Content widgets per inventory (crest slots, `<-->` arrows, option captions with FC-bank `textid`s) **[C]** in `screen_inventory.txt`. Scene-id strings for the flow: `wcqualif`, `wcshedq`, `wcztsel`, `wcstad`, `wcstand1`, `wcplayof`, `wcleader`, `wccontr` **[C]** cluster names §2. Enter/Esc dispatch: tick 0x44e578 region has no Ghidra function (thunk) — decode pending, jump targets = cluster scene ids above **[H]**.

## 8. League-mode kt/menu scenes — feflow73/74/79/80/82

LAY members with the only shipped **objecttype=42/44 menu widgets** (pass-10 vocabulary):
- FEFLOW73.LAY numobjects=60, authored `C:\Wc98\pc\local\cd\fe\LAYOUTS\Feflow73.lay`; FEFLOW74 38; FEFLOW79 57; FEFLOW80 132; **Feflow82.lay numobjects=31** with menu items `h000`, `t000`…`t008` **[C]** `lays/Feflow82.lay`.
- Menu widget draw source: each item is a text box referencing FC-bank index pairs (`textid=bank,index`); the string bank is FCENG.BIN ("FCLG V1.7", 2031 strings, `FCENG.BIN.json`) — item captions resolved at runtime by bank index, coordinates from the LAY record **[C grammar]**; per-item list length/selection via handler state (init/tick §1: 0x45b860/0x45c800 etc.).
- Scene ids: `lleagsel`, `lteamsel`, `lshed`, `lresults`, `lstand`, `lleaders`, `lstadium` **[C]** cluster §2 → feflow73=league select, 74/79/80/82 = league team/leaderboard/shed/results family **[H mapping by name]**.

## 9. In-game options popups

LAYOUTS.BIG members `igaoptns.lay`, `iggoptns.lay`, `igmoptns.lay` (attacking/goalkeeper/match in-game options) **[C inventory]**; raw text not yet dumped. `IGLAY.BIG@CONTROL1.LAY`, `IGOPTN2.LAY`: extraction of IGLAY.BIG pending this pass — geometry capture follows the §3 grammar once dumped. Save/load: feflow16 init 0x4519c0 **[C]** asm in `menu8_asm_slots.txt` handles Esc(0x1b)/'e'(0x65), save-slot key checks (`FUN_0049772c` keys 2/3, `FUN_0046ae74`) and pushes scene p-strings 0x562bb8 / 0x562bc4 via `FUN_004461b4`; those VAs sit in the cluster block → target names decode as `mnselect`/`loading` family **[H]**.

## 10. Transition graph (entry → scene)

Dispatch mechanism **[C]**: handlers push a cluster p-string scene name and call `FUN_004461b4` (scene switch); messages compared as ASCII dwords in init/tick guards (`"del"`-family seen at 0x44d9c8). Confirmed edges: feflow16→{0x562bb8, 0x562bc4} scenes **[C asm]**; mainmenu items → §4 **[H]**. Full edge table pending the thunk-body disassembly pass (next slice input).

## Open items for the port consumer
1. Main-menu per-item draw x/y/font: resource-level (runtime string-bank widgets), not statically recoverable — capture at runtime with the window-object dump once implemented.
2. Thunk-less tick bodies (feflow13/50/73/74/79/80/82) need raw disassembly (lane proven: `menu15` DisassembleCommand pattern).
3. IGLAY.BIG member extraction pending.

Dumps: `analysis/pass11/menu{1..15}_*.txt`, `lays/*.LAY`, `screen_inventory.txt`, `FCENG.BIN.json`; scripts `ghidra-scripts/GhidraWc98Menu{1..15}.py`.
