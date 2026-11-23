# Menu Parity Inventory — FIFA: Road to World Cup 98 (Windows) → native Linux port

Authoritative menu-screen inventory + parity checklist for the "100 % Windows menu parity" goal. Companion to `FE_MENU_SCREENS_WC98.md` (handler/grammar dossier), `LAY_COMPOSITOR_NOTES.md`, `WINE_ORACLE_WC98.md`; the verification matrix of the third UI pass is `UI_VALIDATION_WC98.md`.

Legend: **[C]** = CONFIRMED from cited evidence; **[H]** = HYPOTHESIS.
Port status values: **impl** = interactive implementation in the port (`app/fe_flow.cpp`), tested by a named gate in `tests/CMakeLists.txt`; **cap** = renderable/capturable only via the `--fe-screen NAME` lane (visual asset parity, no interaction); **miss** = not implemented.

Evidence sources:
- Scene registry scan: `/mnt/1st4TB/WorldCup98/analysis/pass11/menu5_scenemap_windowobj.txt` ("SCENE site=…" lines; 66 rows verified by `grep -c "^SCENE "`). Raw asm `analysis/pass8/tourney_registry.txt`. Handler-slot semantics (`+0x1c`=init `+0x10`=tick `+0x18`=draw `+0x14`=event) **[C]** `FE_MENU_SCREENS_WC98.md` §1.
- LAY member inventory (101 members of `FE/PACK/LAYOUTS/LAYOUTS.BIG`, FCENG.BIN bank captions): `/mnt/1st4TB/WorldCup98/analysis/pass11/screen_inventory.txt`; raw members `analysis/pass11/lays/*.LAY`.
- IGLAY dump: `wc98-extract FE/PACK/LAYOUTS/IGLAY.BIG` → 30 BIG members (`/tmp/pass12inv/iglay/IGLAY.BIG.d`, sha256-verified identical to the loose tree files, see §2c).
- Reference-source rule: Windows reference captures are **asset-level only** (LAY text + FSH art + `--fe-screen` / render-demo PNGs). The Wine/DirectDraw screenshot lane is a proven dead end on this host **[C]** `WINE_ORACLE_WC98.md`; no row below claims a Windows screenshot.

## 1. Scene registry — all 66 registrations (56 unique scenes)

All rows from the linear scan of `FUN_004479d0` **[C]**. Sites at 0x4481b5…0x4483d3 are second-pass registrations of already-listed scenes (scene-name rewrites in later blocks, e.g. tournament-flow remap) — slots identical to the primary row for that scene. `—` = slot not written at that site.

| # | site | p-string VA | scene | init +0x1c | tick +0x10 | draw +0x18 | event +0x14 | LAY member | purpose (evidence: widgets/captions/authored path) | port |
|---|------|-----------|-------|------------|------------|------------|-------------|------------|------------------------------------------------------------------|------|
| 1 | 004479de | 0x562334 | feflow00 | 0x4483f0 | 0x4483f4 | — | — | FEFLOW00.LAY | dev/test stub ("temp testing scrollbars/match select buttons"; bank sentinel `=TXT NOT AVAIL=`) | cap |
| 2 | 00447a13 | 0x562340 | feflow06 | 0x449cbc | 0x449fac | 0x44a0dc | — | Feflow06.lay | main-menu hub: friendly/world cup 98/practise/shootout/classic buttons (`fe07/fe11/fe32/fe33/fe90`), modem/netw, quit/options/team-mgmt/load/highlights/credits (`repl`,`cred`) | **impl** (main menu: FEFLOW05 composite + 4 exe-string entries) |
| 3 | 00447a32 | 0x56234c | feflow07 | 0x44a804 | 0x44ae94 | 0x44b114 | 0x44a770 | FEFLOW07.LAY | single-player match setup: team names, ball sprites, volume bars (`rb*/lb*`), qualification captions 432/433 | **impl** (`fe_setup07`: hub fe07 wedge + two-axis rows; gates `fe_setup07`) |
| 4 | 00447a58 | 0x562358 | feflow08 | 0x44b878 | 0x44bc1c | 0x44bc9c | 0x44b858 | FEFLOW08.LAY | match/stadium select: `stad` `sare` `nslt/nsrt` (next score), `sint/wint/hint`, ball picker `mpx10` | **impl** (`fe_stadium08`: France-map mpNN tile ring + nslt/hint captions; gate `fe_stadium08`) |
| 5 | 00447a7e | 0x562364 | feflow09 | 0x44c330 | 0x44c6c0 | 0x44c940 | 0x44c958 | FEFLOW09.LAY | friendly match config (8 rows × team pair, `ovr/lrp/rrp/alp/arp/arw/ctx`, "Final Match Qualifiers") | **impl** (`fe_flow_setup09`, `fe_nav_exhibition`) |
| 6 | 00447aa4 | 0x562370 | feflow10 | 0x44d278 | 0x44d34c | 0x44d958 | — | FEFLOW10.LAY | modal confirm popup (blur/popp, ok/cancel, txt0-4) | **impl** (popup lane: `--fe-popup feflow10`; gates `fe_popup_ok`, `fe_popup_dismiss_esc`) |
| 7 | 00447ac3 | 0x56237c | feflow11 | 0x44d9c8 | 0x44dda0 | 0x44dfa8 (thunk→`FUN_0044a210`) | — | FEFLOW11.LAY | World-Cup team pick grid: map, big flag `tmfg`, group cells `tc/tt/ta/stf*`, scroll `sbar`. Row state machine **[C]** `analysis/pass45ctrl/subdecomp.txt`: `fun_0044d9c8` tm%02d click cycles the pool-record byte (stride 4, base `DAT_00608af5`, +3) 0→1→2→0 with P1/total counters `DAT_00608bc0/bc4` and the apply-then-revert cap `0x1f < bc4`; `fun_0044dda0` open handler rescans the walk; `fun_0044dff8` paints the `nopl/nopn` "num of teams selected" counter (`fun_004788bc`, format `DAT_005629e4` "%i", normalColour 14) when total != 0. `fe18` continue edge gates on P1 counter ≥ 1 | **impl** (`fe_render_tournament`, `fe_screen_capture_team`, `--fe-team-select N` pick capture; model `app::TeamPickPool`) |
| 8 | 00447ae2 | 0x562388 | feflow13 | 0x44e440 | 0x44e578 | 0x44e580 (thunk→`FUN_0044a210`) | — | FEFLOW13.LAY | world cup controller select screen (LAY header comment; slot-row grammar shared with FEFLOW87 shootout board). **[LANDED 2026-11-17 pass-24wc13]** token tree `FUN_0044e440` decoded `analysis/pass24wc13/fe13_asm.txt`: 'fe06' leaf 0x44e466→0x44e501 = FUN_00459410 tuple (0x3ea,0x82e,0x10f3) + push feflow37 (World-Cup exit confirm), 'quit' leaf 0x44e4d6→0x44e4de tuple (0x3e9,0x82e,0x10f1) + push feflow37 (app-quit confirm), 'fe15' leaf 0x44e483→0x44e563 = options (FUN_00451964 push 0x562a24), 'fe08' ZF-leaf 0x44e45a→0x44e4d4 JBE→0x44e524 = forward: FUN_00446624()==2 stay guard, FUN_0045a24c()<0 → helper 0x448428 push feflow06 (hub), else FUN_00448418+FUN_0045a198 push feflow56 (tournament launch); Esc = event branch 0x44e499→0x44e4ba push NAME:feflow06 (`edges_table.txt:113`). Board: open-event device zero, 4 slot rows (grv/ovr ring), contr frame 1+slot per row, tmtl/tmtr 'Teammates:' captions, ltfg/rtfg flags, titl plate; open edge feflow20 'frwd' [C SITE 0x452dc7]. Gates: selftest 14b5 `fe-wc13` + capture `--fe-wc13/--fe-wc13-dev`. | **impl** |
| 9 | 00447b06 | 0x562394 | feflow15 | 0x44f22c | 0x44f804 | 0x44fa7c | — | feflow15.lay | options screen, 4 tabs (`tab1-4`,`tbb1-4`,`lst1-4` = Match/Game play/Audio…, `save`/`deft`/`back`/`fwrd`) | **impl** (pass-22fe15: FE15_INIT FourCC tag dispatch decoded `analysis/pass22fe15/dec2.txt` — 'save' 0x73617665 writers cluster FUN_0046b0fc/b5c8/b3e0/b83c/b074 + options.sav FUN_0046b320 + sfx FUN_004678b8 stay; 'deft' 0x64656674 = FUN_00459410 cursor reset; 'fwrd' 0x66777264 writers+pop; Enter='fwrd', Esc='back'; `lst1-4` `rows=N+` FC tuples parsed (dead-scan bug fixed: rows never rendered before, now 11 captions + value column); gate `fe_flow_options` + selftest 14b3; pass-22fe15b: ttxt header pane = per-tab title caption via TABSW `FUN_004516c0` TEXTID rewrite from DGROUP baked constants 78/81/1299/82) |
| 10 | 00447b25 | 0x5623a0 | feflow16 | 0x4519c0 | 0x451aac | — | — | FEFLOW16.LAY | lineups grid (lp/rp/ls/rs player rows, flags, `ftix`) — static board, no row cursor: tick FUN_00451aac paints once then dispatches on `DAT_00640fb4` (0xa kickoff-arm, 0xb exit); init slot = event window: Esc -> push feflow06, 'e' + save-slot `FUN_0049772c(2|3)` -> slot flags `0x59bc88/8c` + push feflow35 [C analysis/pass30ig16]. **[LANDED 2026-11-17]** full board: 22+22 rows at LAY anchors (left x=6 y=0..169, right x=630/640 y=293..463; side test `s.slot[2]=='r'` — index-1 read 'm' and killed the right column), team-flag overdraw on shdr/shdl (global\FNNN f000), FC-bank captions tmlt/tmrt; gate `fe_lineups16` (shapes>=1 texts=46) + selftest 14b2 open/Esc/kick | impl |
| 11 | 00447b3d | 0x5623ac | feflow18 | 0x452328 | 0x4524f8 | 0x452560 | — | FEFLOW18.LAY | league team/zone pick (32 `tm`+`gm` zone slots, `rand` randomize) | **impl** (`--fe-wc-cfg 18`, gate `fe_wc_cfg18`; pass-20uitick edges) |
| 12 | 00447b5c | 0x5623b8 | feflow19 | 0x452688 | 0x452884 | 0x4528e4 | — | FEFLOW19.LAY | tournament group assignment (groups A-H names + 4 team slots each, `rand`) | **impl** (`--fe-wc-cfg 19`, gate `fe_wc_cfg19`; staged `rand` 99) |
| 13 | 00447b7b | 0x5623c4 | feflow20 | 0x452b00 | 0x452f34 | 0x45314c | 0x452af0 | FEFLOW20.LAY | "today's games" schedule day (`tody`, date fields, `simu` simulate, next-game lineup/stadium/score) | **impl** (`--fe-wc-cfg 20`, gate `fe_wc_cfg20`; simu/frwd/stsc/strk edges) |
| 14 | 00447ba1 | 0x5623d0 | feflow21 | 0x453f58 | 0x454298 | 0x453f50 | — | FEFLOW21.LAY | group standings mini-board (tm×4 + qf slots, simulate/stats buttons) | **impl** (`--fe-wc-cfg 21`, gate `fe_wc_cfg21`; QF pair strings + SF flags) |
| 15 | 00447bc0 | 0x5623dc | feflow23 | 0x4543dc | 0x4545fc | 0x454658 | 0x4543d0 | FEFLOW23.LAY | knockout bracket R16 (tm×16, qtm×8, hilite bars/brackets, `save`) | **impl** (`--fe-wc-cfg 23`, gate `fe_wc_cfg23`) |
| 16 | 00447be6 | 0x5623e8 | feflow24 | 0x454e0c | 0x45502c | 0x454658 | 0x454e00 | FEFLOW24.LAY | knockout bracket variant (tm×16 + qf flags) | **impl** (`--fe-wc-cfg 24`, gate `fe_wc_cfg24`) |
| 17 | 00447c0c | 0x5623f4 | feflow25 | 0x4554ec | 0x45570c | 0x455768 | 0x4554e0 | FEFLOW25.LAY | quarterfinal board (tm×8 entrants, qtm×4 SF slots) | **impl** (`fe_render_present` family; `--fe-present`) |
| 18 | 00447c32 | 0x562400 | feflow26 | 0x455ae0 | 0x455d28 | 0x455d80 | 0x455ad0 | FEFLOW26.LAY | semifinal board (+ trophy slots `wcup`/`wcps`) | **impl** (`--fe-present`) |
| 19 | 00447c58 | 0x56240c | feflow27 | 0x455fd0 | 0x456030 | 0x456084 | 0x45626c | FEFLOW27.LAY | full bracket + champion slot `chmp` + 3rd-place match (`3tm/3mat`) | **impl** (`--fe-present`) |
| 20 | 00447c7e | 0x562418 | feflow28 | 0x456cd8 | 0x456fa8 | 0x457038 | — | FEFLOW28.LAY | team-history bookmark hub (shield, team name, general/match/world-cup/greats icons) | **impl** (`fe_records28`) |
| 21 | 00447c9d | 0x562424 | feflow29 | 0x45721c | 0x4574ac | 0x45750c | — | FEFLOW29.LAY | records "match results" bookmark page (ribbon, scroll bar, top/second… columns) | **impl** (`fe_records29`) |
| 22 | 00447cbc | 0x562430 | feflow30 | 0x457574 | 0x45767c | 0x4576c0 | — | FEFLOW30.LAY | records "world cup results" bookmark page (same grammar) | **impl** (`fe_records30`) |
| 23 | 00447cdb | 0x56243c | feflow31 | 0x4576f8 | 0x457830 | 0x4578fc | — | FEFLOW31.LAY | records "general" page (multitext pane `mtex`, team name) | **impl** (`fe_records31`) |
| 24 | 00447cfa | 0x562448 | feflow32 | 0x45791c | 0x457cb4 | 0x457e4c | 0x457910 | FEFLOW32.LAY | single-player match config (volume bars left, footballs, shooting/passing captions) | **impl** (pass-20cfg §13: baked name plates from scene PAK back collage, one centre caption column, lbNN/rbNN bar fills from `sbarsr.fsh` bar0; gates `fe_cfg32`/`fe_cfg32_nav`) |
| 25 | 00447d20 | 0x562454 | feflow33 | 0x457e68 | 0x457f8c | 0x457fa4 | 0x457e60 | FEFLOW33.LAY | two-player match config (both team names, ball shadows, volume bars) | **impl** (pass-20cfg §13: single centre caption set at x=300 — double-paint defect fixed; gates `fe_cfg33`/`fe_cfg33_side`) |
| 26 | 00447d46 | 0x562460 | feflow34 | 0x457fd8 | 0x45822c | 0x4582e8 | — | FEFLOW34.LAY | trivia bookmark page (pane `mttr`, prev/next buttons) | **impl** (`fe_records34`) |
| 27 | 00447d65 | 0x56246c | feflow35 | 0x45838c | 0x458714 | 0x45889c | — | Feflow35.lay (+ FEFLOW35.LAY board variant) | group standings table: `gpnm` group name, rows `ssn/ssp/ssw/ssd/ssl/ssf/ssa/sst`, stats buttons | **impl** (`fe_render_present`: "FE-PRESENT FEFLOW35"; `--fe-pick 35`) — uppercase board variant: cap |
| 28 | 00447d84 | 0x562478 | feflow37 | 0x45922c | 0x4592f0 | 0x459220 | — | FEFLOW37.LAY (authored `Feflow86.lay`) | yes/no confirm popup (blur, popp, no/yes, txt×3) | **impl** (popup lane + hub quit-wedge confirm; gates `fe_popup_yesno`, `fe_popup_dismiss_enter`) |
| 29 | 00447da3 | 0x562484 | feflow41 | 0x459420 | 0x4597ac | **0x0** | — | FEFLOW41.LAY | LOAD game list (blur, `lstb`+slider, "LOAD game" caption) | **impl** (LOAD list scene scans real `SLOT%02d.SAV` files; gate `fe_loadsave41`) |
| 30 | 00447dc2 | 0x562490 | feflow42 | 0x45979c | 0x4597ac | 0x459b98 | — | FEFLOW42.LAY | SAVE game list (tick/cross, `lstb`+slider) | **impl** (SAVE-confirm list; Enter writes the focused slot + tick flip; gate `fe_savemgr42`) |
| 31 | 00447de1 | 0x56249c | feflow45 | 0x459d94 | 0x459df8 | 0x459e14 | — | FEFLOW45.LAY | dev test screen ("test button") | cap |
| 32 | 00447e00 | 0x5624a8 | feflow50 | 0x459e60 | 0x459f7c | 0x459fe4 | — | FEFLOW50.LAY | practise match config (same row grammar as feflow09) | **impl** (`fe_flow_setup50`, `fe_nav_practise`) |
| 33 | 00447e1f | 0x5624b4 | feflow51 | 0x459ff0 | 0x45a0fc | 0x45a110 | — | FEFLOW51.LAY | stadium/match select with HALF LENGTH setting + ball set (`mpx10`) | **impl** (`fe_stad51`: hub fe32 wedge → FEFLOW50 confirm → France-map mpNN ball-pin ring + `hbal` cursor + nslt/nsrt arrows; pass-33fe51) |
| 34 | 00447e3e | 0x5624c0 | feflow52 | 0x45a120 | 0x45a128 | — | — | FEFLOW52.LAY | lineups grid variant (no flag pane) — internal | cap |
| 35 | 00447e56 | 0x5624cc | feflow56 | **0x0** | 0x45a130 | 0x45a134 | — | FEFLOW56.LAY (stub) | null stub scene (init=0; numobjects=1) | cap |
| 36 | 00447e75 | 0x5624d8 | feflow72 | 0x45a514 | 0x45ae84 | 0x45afc4 | — | FEFLOW72.LAY | league match setup: defenders/midfield/forwards +/- counters (`bx9`, `vx3`) | **impl** (gate `fe_league_cfg72`; L/R gofd/gobk chain + pNNN pitch grid/tNNN formation rows via `fe_mgmt_pages` hub Enter mapping) |
| 37 | 00447e94 | 0x5624e4 | feflow73 | 0x45b860 | 0x45c800 | 0x45c8dc | — | FEFLOW73.LAY | league team player list (`pn00..pn08`, status/total columns, scroll `sbar`) | **impl** (`fe_league_select`, `fe_nav_league`) |
| 38 | 00447eb3 | 0x5624f0 | feflow74 | 0x45df84 | 0x45e8e8 | 0x45e99c | — | FEFLOW74.LAY | league team-management grid (`ktx9` name keys, attributes +/- `attb`) | **impl** (gate `fe_league_cfg74` + chain; card/vit rows mirrored by IGTEAM74. Event handler 0x45df84 token tree fully decoded (pass-40league): leaves `tmf0..3/ab00/ab01/lb00/lb01/sbar/gobk/gofd`; Enter = `tmf2` leaf 0x45e845 pushes scene 0x76 popup, close pops back to the grid (port: `cfgPopBase_`, row cursor survives); tick 0x45e8e8 decoded: mouse drag clear + `[0x60aac0]=-1`, `attb` budget text widget via group slot `[0x57e84a+grp*4]>>16` + 9-word budget array copy `[0x631746]→[0x60a9fc]`, keyboard branch slider sync 0x4d31a0 ×3; `tmf0` budget gate `[0x5882b8]` engine-side, no port budget sim §4 rank 7) |
| 39 | 00447ed2 | 0x5624fc | feflow75 | 0x45ee34 | 0x45f480 | 0x45f56c | — | FEFLOW75.LAY | league lineup editor (player block `ptop/px17/pbot`, save/reset buttons) | **impl** (gate `fe_league_cfg75` + chain; Enter = event 8005 0x45f49f opens FEFLOW76 popup (pass-38League)) |
| 40 | 00447ef1 | 0x562508 | feflow76 | 0x45f6a4 | 0x45fa48 | 0x45fbe4 | 0x45f49f | FEFLOW76.LAY (authored `feflow75.lay`) | aggression-value confirm popup (`alok`/`canc`, "aggression text") | **impl** (pass-38League: Enter on feflow75 = event 8005 `mov eax,0x76; call 0x466cb8` opens the popup; Enter/Esc/`alok`/`canc` close back to feflow75; plate `shapeID=aggr` absent from every pack → retail paints no plate quad (capture-verified), caption `textid=1211` = "Player Aggressiveness"; gate `fe_league_popup76`; dossier `analysis/pass38League/`) |
| 41 | 00447f10 | 0x562514 | feflow79 | 0x45ff14 | 0x460af4 | 0x460cac | — | FEFLOW79.LAY | league squad/formation screen (player oval `oval`, bias slider, attributes) | **impl** (gate `fe_league_cfg79` + chain; `attb` runtime skill-column header via DAT_0057e84a table + ab-arrow wrap, pass-42lg79; bias-slider s001 mirrored by IGTEAM79 UP/DOWN ±10 grammar) |
| 42 | 00447f2f | 0x562520 | feflow80 | 0x461edc | 0x463098 | 0x463168 | 0x461ed0 | FEFLOW80.LAY | full custom-mode management grid (132 objects: player model `plyr`, name edit box, stat columns, memory-card captions) | **impl** (gate `fe_league_cfg80` + chain; composite grid + `s002` slider shape; `plyr` model/edit-box lanes engine-driven [H]) |
| 43 | 00447f55 | 0x56252c | feflow81 | 0x4647a8 | 0x464878 | 0x464790 | 0x464798 | FEFLOW81.LAY | results page (multitext pane `mult`, scroll bar, quit/back) | **impl** (`--fe-wc-cfg 81`, gate `fe_wc_cfg81`; `mult` rows from bracket results) |
| 44 | 00447f7b | 0x562538 | feflow82 | 0x465854 | 0x465b0c | 0x46584c | — | Feflow82.lay | player performance stats (rows `t000-t008` type=44, stat buttons `mbt0-2`, pair headers) | **impl** (`--fe-wc-cfg 82`, gate `fe_wc_cfg82`; t00N standings rows) |
| 45 | 00447f9a | 0x562544 | feflow83 | 0x467270 | 0x467274 | — | — | FEFLOW83.LAY | lineups + score grid (lp/rp/ls/rs, `llls/rrrs` scores with inc buttons) — dev/debug screen **[H]** | cap |
| 46 | 00447fb2 | 0x562550 | feflow84 | 0x467320 | 0x467438 | — | — | FEFLOW84.LAY | lineups + score grid variant (same debug pattern) **[H]** | cap |
| 47 | 00447fca | 0x56255c | feflow86 | 0x467770 | 0x4677b4 | 0x467760 | — | FEFLOW86.LAY | confirm popup (txt0-2, fe09 button, footix) | **impl** (gate `fe_popup_feflow86` pass-37igpop + IGFLOW86 in-game twin; simulate confirm opens with `quitContext=false` and no longer exits app; popup plate quad absent in retail packs) |
| 48 | 00447fe9 | 0x562568 | feflow87 | 0x467970 | 0x467ab0 | 0x467ab8 | — | FEFLOW87.LAY | controller-select grid (4 slots, controller pictures `anm*`, gravis button numbers, "Num Team Mates") | **impl** (selftest `fe-cfg87`) |
| 49 | 00448008 | 0x562574 | feflow88 | 0x467af0 | 0x467c60 | 0x467d98 | — | FEFLOW88.LAY | "all time greats" records page (two multitext panes `mtxl/mtxr`, titles 4311/4376) | **impl** (`fe_records88`) |
| 50 | 00448027 | 0x562580 | feflow89 | 0x467da0 | 0x4683a4 | 0x4685b4 | — | Feflow89.lay | team save/load menu (6 buttons `b000-b005` incl. "Load" 994, reset-all/reset-team) | **impl** (`--fe-saveload` gate `fe_flow_saveload`; composite art + overlay slot labels from `SLOT%02d.SAV` state, hub fe41 wedge -> rows, Enter = load when file exists) |
| 51 | 00448046 | 0x56258c | feflow90 | 0x468728 | 0x4688e8 | 0x468924 | — | Feflow90.lay | **World-Cup history board** (hub wedge `fe33`): 9-edition year cursor (`ysla/yslc/ysrb`), finalist pair `ltnm/rtnm` + scores `ltsc/rtsc`, FC captions 429..433; Enter `'fe91'` -> feflow91 | **impl** (pass 13: year cursor over 1930..1994, FCDB names/shields painted) |
| 52 | 00448065 | 0x562598 | feflow91 | 0x468b50 | 0x468c68 | 0x468d28 | — | Feflow91.lay | continuation board for the selected final (two shields + qualifier rows, `fe08` confirm / `fe06` exit) | **impl** (composite + FC captions; chain from feflow90 Enter) |
| 53 | 00448084 | 0x5625a4 | feflow93 | 0x468d74 | 0x468e8c | — | — | Feflow93.lay | lineups + score grid variant (classic family) **[H]** debug | cap |
| 54 | 0044809c | 0x5625b0 | feflow94 | 0x46999c | 0x469904 | 0x46986c | — | feflow94.lay | credits roll (`cr01..cr32` text rows) | **impl** (roll contents from CRED.BIN cell stream, pass-34; retail art pack absent from this CD) |
| 55 | 004480bb | 0x5625bc | feflow95 | 0x469cb8 | 0x469af8 | 0x469a60 | — | feflow95.lay | icon/key-assignment popup (`box1-8`/`btn0-7`, "…and icons") | **impl** (`--fe-cheats` gates `fe_cheats`/`fe_cheats_off`, §8: type=18 checkbox painter, hub payload 0x1E entry, code-embedded captions replace bank soup) |
| 1 | 004481b5 | 0x562334 | feflow00 | — | — | — | — | (as #1) | remap site | cap |
| 2 | 004481d8 | 0x562340 | feflow06 | — | — | — | — | (as #2) | remap site | impl |
| 27 | 00448230 | 0x56246c | feflow35 | — | — | — | — | (as #27) | remap site | impl |
| 16 | 00448293 | 0x5623e8 | feflow24 | — | — | — | — | (as #16) | remap site | cap |
| 19 | 004482af | 0x56240c | feflow27 | — | — | — | — | (as #19) | remap site | impl |
| 15 | 004482c8 | 0x5623dc | feflow23 | — | — | — | — | (as #15) | remap site | cap |
| 17 | 004482cf | 0x5623f4 | feflow25 | — | — | — | — | (as #17) | remap site | impl |
| 14 | 004482d6 | 0x5623d0 | feflow21 | — | — | — | — | (as #14) | remap site | cap |
| 18 | 004482dd | 0x562400 | feflow26 | — | — | — | — | (as #18) | remap site | impl |
| 51 | 0044834d | 0x56258c | feflow90 | — | — | — | — | (as #51) | remap site | impl |
| 56 | 004483d3 | 0x5625c8 | **feflow17** | — | — | — | — | FEFLOW17.LAY | dev options list (`opt0-7`, `dop0-3`; captions "Automatic Crosses", "Audio/Visual Options" 267) — internal debug menu **[C]** authored feflow17.lay | cap |

Counts **[C]** (verified against `menu5_scenemap_windowobj.txt`): 66 SCENE rows = 55 primary registrations (#1–#55) + 11 second-pass sites in the 0x4481b5-0x4483d3 block; remap rows reuse the primary row's `#`. Unique scene names: **56** — feflow17 is registered only at site 0x4483d3 (no slot record in this scan); all other remap sites re-register scenes that carry their own primary row.

Registry-internal totals: **impl** = 18 unique scenes interactive — feflow06 (main menu), 07, 08, 09, 10, 11, 15, 25, 26, 27, 35, 37, 41, 42, 50, 73, 90, 91; **miss** = 0; **cap** = 38 — everything else, renderable via the `--fe-screen` lane only (includes stub scenes feflow00/feflow56; feflow15 moved to impl pass-22fe15).

## 2a. LAYOUTS.BIG members (101) — screen inventory

Source: `analysis/pass11/screen_inventory.txt` (`== MEMBER` sections; authored-path comment = original editor path inside the member header). Purposes from authored paths, widget names and FCENG.BIN bank captions. Status applies to the port; every member renders through the `--fe-screen NAME` lane (gate pattern: `wc98-native --fe-screen FEFLOWxx --tree $WC98_TREE`; gates `fe_screen_capture_menu`/`fe_screen_capture_team`). Members with `numobjects=1` and a single null widget are stubs.

| member | obj | purpose **[C]** (authored path / widgets / caption sample) | port |
|---|---|---|---|
| FEFLOW00.LAY | 4+6 parsed | dev test screen: temp scrollbars/buttons, `mnme` edit box; sentinel caption id2=`=TXT NOT AVAIL=` proves bank-text path not wired at authoring time | cap (stub) |
| FEFLOW01.LAY | 1 | null stub | cap (stub) |
| FEFLOW04.LAY | 10 | credits-style screen authored `mnmcon.lay` — "Additional Animation/Art/CG Art/Programming:" captions on buttons, flag anim, Footix; not in registry → legacy netplay-era credits **[H]** | miss (unregistered) |
| FEFLOW05.LAY | 26 | **authored `MNMSETUP.LAY`** = modem/netplay setup: machine name, modem type/comm port/baud/init/dial strings, save/reset; soup captions from FC bank ids 201-231 (Agility…). Historical note: used as the port's main-menu composite art (central panel baked into `feflow05.fsh#back`) | **impl** (menu art) — native MNMSETUP screen itself miss |
| FEFLOW07.LAY | 45 | single-player match setup (volume bars, balls, qualification captions 429-433) | cap |
| FEFLOW08.LAY | 36 | stadium/match select + ball picker `mpx10`, HALF LENGTH context, next-game fields | cap |
| FEFLOW09.LAY | 57 | friendly config rows (ovr/lrp/rrp/alp/arp/arw/ctx ×4 slots, "Final Match Qualifiers" 699) | **impl** |
| FEFLOW10.LAY | 11 | confirm popup: blur+popp, ok×2/cancel, txt0-4 | **impl** (popup lane: composite + "Are you sure?" caption + help pane; two-button cursor, Enter/Esc close to caller) |
| FEFLOW11.LAY | 74 | ZWCUP team pick grid (map, `tmfg` shield, group cells tc/tt/ta/stf, sbar) | **impl** |
| FEFLOW12.LAY | 1 | null stub | cap (stub) |
| FEFLOW13.LAY | 57 | world-cup competition config (quit-the-game variant of the row grammar) | cap |
| FEFLOW14.LAY | 14 | **authored `mnmdial.lay`** = dialling directory: listbox+slider, save/delete/dial buttons; not in registry → netplay family | miss (unregistered) |
| FEFLOW16.LAY | 50 | lineups grid with flags (`ftix`) — internal | cap |
| FEFLOW17.LAY | 17 | dev options list, captions "Automatic Crosses" 277 / "Audio/Visual Options" 267 (registry #64) | cap |
| FEFLOW18.LAY | 80 | league team/zone pick: 32 tm + 32 gm zone slots, randomize | cap |
| FEFLOW19.LAY | 88 | tournament groups A-H assignment (g0-g7 name+team cells, "numbers must be diferent color") | cap |
| FEFLOW20.LAY | 44 | schedule day screen: today's games, date fields, lineup/time/stadium rows, simulate | cap |
| FEFLOW21.LAY | 27 | group standings mini-board (tm×4 + qf slots, simulate/stats buttons) | cap |
| FEFLOW22.LAY | 1 | null stub | cap (stub) |
| FEFLOW23.LAY | 73 | knockout bracket R16 with hilite bars/brackets + save | cap |
| FEFLOW24.LAY | 65 | knockout bracket variant | cap |
| FEFLOW25.LAY | 47 | quarterfinal board (bar×4, hilight×2) | **impl** (`--fe-present`) |
| FEFLOW26.LAY | 31 | semifinal board + trophy slots wcup/wcps | **impl** (`--fe-present`) |
| FEFLOW27.LAY | 72 | full bracket, champion `chmp`, 3rd-place (stm/ftm/chmp/3tm/3mat) | **impl** (`--fe-present`) |
| FEFLOW28.LAY | 21 | team-history hub: shield, team name, bookmark icons general/match/WC/greats | cap |
| FEFLOW29.LAY | 67 | records page "match results" (ribbon, sbck banner, top/second… columns ×5, scroll) | cap |
| FEFLOW30.LAY | 67 | records page "world cup results" | cap |
| FEFLOW31.LAY | 17 | records page "general" (`mtex` multitext pane, team name) | cap |
| FEFLOW32.LAY | 32 | single-player config (volume bars displayed on right for left player — dev comment) | cap |
| FEFLOW33.LAY | 45 | two-player config (ball shadows ×2, footballs ×2) | cap |
| FEFLOW34.LAY | 18 | trivia page (`mttr` pane, prev/next sbln/sbrn) | cap |
| FEFLOW35.LAY | 47 | knockout board variant (bar×4 hilight×2 family of feflow25 art) — distinct from lowercase `Feflow35.lay` standings | cap |
| FEFLOW36.LAY | 1 | null stub | cap (stub) |
| FEFLOW37.LAY | 8 | yes/no popup authored `Feflow86.lay` copy (blur, no/yes, txt×3) | **impl** (popup lane; quit-confirm caption index 220 + hub 'quit' wedge wiring) |
| FEFLOW38–40.LAY | 1 | null stubs | cap (stubs) |
| FEFLOW41.LAY | 6 | LOAD game list: blur+overlay, "LOAD game", quit button, `lstb`+slider; caption id6="…and icons" leak | **impl** (`--fe-load-game`: lstb rows over SLOT%.2d.SAV scan + sbar thumb; soup caption excluded) |
| FEFLOW42.LAY | 7 | SAVE game list ("SAVE game", tick/cross buttons, `lstb`) | **impl** (`--fe-save-manager`: Enter writes slot via saveTournament, cros→tick nvicons chain) |
| FEFLOW43.LAY | 101 | full team-management grid: player overlays/cards, SMALL STATUS, positions/Sub captions (979 "Left Back", 1783 "Sub"), reset-all/save — authored Feflow43.lay; **not in registry** → superseded by feflow73-80 family **[H]** | **impl** (`--fe-mgmt-grid`: composite iNNn chain + FCDB squad rows, FC-bank position labels, aNNN card icons from FE/PACK/FEFLOW73) |
| FEFLOW44.LAY | 1 | null stub | cap (stub) |
| FEFLOW45.LAY | 6 | dev "test button" screen | cap |
| FEFLOW46–49.LAY | 1 | null stubs | cap (stubs) |
| FEFLOW50.LAY | 57 | practise config rows (same grammar as feflow09) | **impl** |
| FEFLOW51.LAY | 35 | match select with HALF LENGTH + ball set `mpx10` (sibling of feflow08) | **impl** |
| FEFLOW52.LAY | 47 | lineups grid, single-flag pane — internal | cap |
| FEFLOW53–55.LAY | 1 | null stubs | cap (stubs) |
| FEFLOW56.LAY | 1 | null stub (registry #34: init=0x0) | cap (stub) |
| FEFLOW57.LAY | 6 | LOAD REPLAY list (blur+overlay, quit button `fe06`, `lstb`+slider `sbar`); caption widget-id 322 → FC remap 999 "Load Reel" | **impl** (hub wedge `repl` → replay browser pass-29: `lstb` rows over `rplyindx.dat` slot filenames, deterministic fixture in scratch dir, `fe06` crs1 close overlay; gates `fe_replay57`, `fe_replay57_enter`) |
| FEFLOW58–71.LAY | 1 | null stubs (14 members) | cap (stubs) |
| FEFLOW72.LAY | 56 | league match setup: defenders/midfield/forwards count buttons, form rows (`ptop..pbot`) | cap |
| FEFLOW73.LAY | 60 | league squad list: pn×9 names, status/total columns, player block, reset/back/fwd | **impl** |
| FEFLOW74.LAY | 38 | management grid: name keys ktx9, attributes +/-, scroll (authored path empty — header corrupted) | cap |
| FEFLOW75.LAY | 42 | lineup editor + score fields ("LATEST SCORE FROM <city>" 958, stadium "La Beaujoire" 965 leaks) | cap |
| FEFLOW76.LAY | 10 | aggression confirm popup authored `feflow75.lay` copy (ok/cancel, "aggression text") | **impl** (pass-38League: opens on feflow75 Enter (event 8005, 0x45f49f); Enter/Esc close to feflow75; gate `fe_league_popup76`; plate art `aggr` absent on tree, black backdrop authentic — `analysis/pass38League/NOTES.md`) |
| FEFLOW77.LAY | 1 | background-only stub | cap (stub) |
| FEFLOW78.LAY | 10 | attack-bias confirm popup (ok/cancel, "attack bias text") — sibling of feflow76; **not in registry** | miss (unregistered) |
| FEFLOW79.LAY | 57 | squad/formation: player oval, bias bar, attributes, pos/status/total columns, cards captions ("RED CARD" 1460) | cap |
| FEFLOW80.LAY | 132 | custom-mode master grid: player model `plyr`, name edit box (type=10), stat matrices al/ar/at/ad×13, modem/menu-SFX caption leaks 1084/1061 | cap |
| FEFLOW81.LAY | 12 | results page (`mult` multitext pane + scroll + quit/back) | cap |
| FEFLOW82→Feflow82.lay | 31 | player performance: type=44 stat rows ×9, pair header arrows, `mbt0-2` buttons | cap |
| FEFLOW83.LAY | 49 | lineups+score debug grid (scores with inc buttons) **[H]** dev | cap |
| FEFLOW84.LAY | 60 | lineups+score grid with back/fwd — dev/debug pattern **[H]** | cap |
| FEFLOW85.LAY | 6 | foot-test screen ("test button", `foot` full-screen shape); not in registry → FSH test **[H]** | miss (unregistered) |
| FEFLOW86.LAY | 7 | confirm popup (txt0-2, fe09, footix; authored D:\wc98_pc…Feflow86.lay) | **impl** (the shared `popp` backdrop panel painted by the popup lane; gate `fe_popup_feflow86`) |
| FEFLOW87.LAY | 57 | controller-select grid: 4 slot overlays + controller pictures + gravis button numbers + "Num Team Mates" (4594) | impl (`--fe-cfg87`) |
| FEFLOW88.LAY | 21 | "all time greats" page (`mtxl/mtxr` panes, titles 4311/4376) | cap |
| FEFLOW97.LAY | 1 | null stub authored `feflow01.lay` (duplicate of #2) — the number is an authoring artifact **[H]** | cap (stub) |
| Feflow02.lay | 14 | netplay lobby: machine list `glst`, create/join buttons, machine-name box; **not in registry** → netplay family | miss (unregistered) |
| Feflow03.lay | 14 | netplay chat/lobby: chat boxes, eject button, machine list — not registered | miss (unregistered) |
| Feflow06.lay | 22 | main-menu hub art: friendly/WC98/practise/shootout/classic big buttons (`fe07/fe11/fe32/fe33/fe90` type=32), modem/netw, quit/options/team-mgmt/load/highlights(`repl`)/credits(`cred`) | **impl** (menu entries; `repl` -> FEFLOW57 replay browser [C 2026-11-18 leaf 0x4491d5 cmp 'repl' + DAT_0057ca48=1 at 0x4491ea; name registered 0x574dec]; `cred` -> FEFLOW94 credits roll (CRED.BIN cell stream, pass-34); gate `fe_replay57`) |
| Feflow35.lay | 59 | group standings table: `gpnm`, ssn/ssp/ssw/ssd/ssl/ssf/ssa/sst rows ×4, stats buttons — the standings member used by `--fe-present` | **impl** |
| Feflow82.lay | 31 | see row "FEFLOW82→Feflow82.lay" (same member; registry name feflow82) | cap |
| Feflow89.lay | 26 | team save/load menu: buttons b000-b005 ("New Controller Pak inserted." 1210, "Load" 994), reset-all/reset-team/save-all | impl — slot list lane (`--fe-saveload`, gate `fe_flow_saveload`); composite art + overlay slot labels from `SLOT%02d.SAV` state; FC caption ids stay widget-id space (remap unresolved) |
| Feflow90.lay | 42 | **World-Cup history board** (hub wedge `fe33`): year cursor + arrows, finalist pair/scores fields, volume bars | **impl** (pass 13 interactive scene; classic-era FCDB ids 861..876 -> `F040..F045` flag art, e.g. F040/F041 decode byte-identical = original data duplicate for Ur/Arg 1930) |
| Feflow91.lay | 57 | continuation board: two shields + qualifier rows ("goes to feflow90" comment) | **impl** (composite + FC captions; `fe91` Enter chain from feflow90, `fe06`/Esc exit) |
| Feflow93.lay | 60 | lineups+score debug grid (classic family) **[H]** | cap |
| INGAME.LAY | 5 | in-game menu frame: `titl/bxot/tlft/blft` shapes + `menu` type=16 widget; pairs with FE/INGAME/IGMENU.LAY overlay | boundary (frame is scene-handler code-drawn: the LAY carries only invisible null.fsh placeholders, raw shapes=0 legit — no composite expected; the port draws its live HUD frame from INGAME.FSH chrome in `app/livematch.cpp`) |
| fe.lst | n/a | member name list file, NOT a .LAY (size 3953) — LAYOUTS.BIG manifest | n/a |
| feflow15.lay | 27 | options screen: tabs 1-4 (`tab*`/`tbb*`), option lists `lst1-4` type=17, save/default/back/fwd; captions "Game play options"/"audio options" comments | **impl** (pass-22fe15 tag wiring + `rows=N+` tuple parse; pass-31f15slider: slider art per row = `unsl` track 185x17 at `lstN.x + hotspots 198` + `sllf` fill fraction + `btnn` knob + `sbtn` — IGOPTN2 pass-21igscn grammar; `fname=opbutns.fsh` resolves via global object cache to `FE/PACK/GLOBAL/OPBUTNS.PAK` (btnn/btnh/btnd/dsbl/unsl/sllf/slrt/but0/but1, same 9-tag slider family as IGOPTN2 `opslider.fsh`/OPSLIDER.PAK); mode-2 rows knob at index/(n-1), mode-1 volume rows at vol/100, unmapped mode-1 rows (4175/1183) show neutral track only [C]; gate `fe_flow_options` shapes=31) |
| feflow94.lay | 33 | credits roll: cr01-cr32 text rows at fixed slots; roll contents = CRED.BIN cell stream (header {3,1,0x129f}; kind 1 caption = FC widget-id via remap table, kind 2 literal staff name `len` NUL-included byte-exact no padding, kind 3 blank; 18-px grid, 2 px per 4 frames) | **impl** (roll; pack art absent) |
| feflow95.lay | 29 | icon/key-assignment popup: box1-8 + btn0-7 checkboxes, "…and icons" title, yes button (authored path = feflow94.lay — header artifact) | **impl** (`--fe-cheats`, gates `fe_cheats`/`fe_cheats_off`, §8) |
| igaoptns.lay | 12 | in-game audio options tab: list type=17 + slider, ok/cancel/default buttons, help pane; captions ids 1174/1175/1144 (bank noise — dev strings on this media); widgets named `1`,`2`,`3`,`04` | **impl** captions lane (pass 13: rows/values + tab captions painted, chck art absent [C]) |
| iggoptns.lay | 12 | in-game gameplay options tab (same grammar, `go_g/go_a/go_c` buttons) | **impl** captions lane (as above) |
| igmoptns.lay | 12 | in-game match options tab (id1171 "Moves highlight within bookmark group") | **impl** captions lane; ROWS windowed to the list box (5 slots, scroll with row cursor) |

Counts verified: 101 members total = 9 interactive impl, ~30 stub/unused, remainder cap/miss as listed. Registry↔member mapping is name-for-name (`feflowNN` → `FEFLOWNN.LAY`, case variants per table). Members not registered in the scene registry (43, 57, 78, 85, Feflow02/03, FEFLOW04/14, credits-adjacent) are legacy netplay/team-management-era assets **[H]** where noted.

## 2b. IGLAY.BIG members (30) — in-game screen pack

Dump: `wc98-extract FE/PACK/LAYOUTS/IGLAY.BIG` → "1 files, 30 BIG members" (`/tmp/pass12inv/iglay/IGLAY.BIG.d`). Grammar = raw LAY text (`[object N] objecttype/objectname`, same as §3 of the companion doc). `textid=0` is dynamic (runtime-resolved from IG banks); captions are not statically resolvable in this pack.

| member | objs | purpose **[C]** (authored path + widgets) | port |
|---|---|---|---|
| CONTROL1.LAY | 27 | in-game controller scheme page: `ctx0-3` config texts, gravis button numbers, team name texts | **impl** (options-chain popup via `--fe-control N`: composite paints the grv*/ls*/*rs* bar art + npll/nplr nav pair; overlays FC#258 'Attacking' ctx caption, four scene-default percent bars from DAT_00632778 {80,80,80,80,65,80,1} with gold fill quads, live gamepad axis readout when a pad is open (fallback 'kbd'), tmtl/tmtr FC#4594 captions + team-mates counts; gate `fe_control_page`. §19: also the in-game pause-menu Controller Select row (code 4) — `IgBoards` kind 25 opens it over the live match via the 'L' lane hotkey, full-composite draw, Enter dismisses to the menu; gate `fe_igmenu` CONTROL1 evidence line + `igmenu_control1.png`) |
| IGCAMERA.LAY | 19 | camera options popup (box/typ/flg/nmb triples, menu/back/forw) | **impl** (live pause 'C' opens it: FC#353 CAMERA title + 5 key-assignment rows (FC 1468/1011/1545/1546/1104) with binding strings, flg/nmb pair cursor, forw/menu nav buttons; gate `fe_camera_pages`) |
| IGCAMSEL.LAY | 13 | camera-select list popup (menu widget) | **impl** (forw nav from IGCAMERA opens the grid: FC#379 'Camera Select' + 7 camera names, row cursor quad, Enter applies a `MatchCamera` mode ("igcam: camera sideline"); gate `fe_camera_pages`) |
| IGCHAT.LAY | 16 | chat overlay: input line + `chat/cha2/cha3` display boxes (netplay) | miss |
| IGCONSEL.LAY | 2 | confirm-OK box (`okay`) | **impl** (`--fe-popup igconsel`: INGAME.FSH btnn/btnd chrome + "OK" caption over null.fsh region placeholders; gate `fe_igconsel`; FUN_00442000 stub = dismiss-only [C]) |
| IGFLOW86.LAY | 7 | in-game confirm popup (`fe09`, txt0-2; authored `IGflow86.lay`) — mirrors FEFLOW86 | **impl** (pass-37igpop: eligibility arbiter jump table 0x45de98 cases 1..4 -> sfx + scene switch `igflow86`; tick FUN_004677b4 installs txt0-2 from runtime ptrs [0x57d164..6c] (caller-set error strings, empty here), button gate [0x57d160]; key FUN_00467770 'fe09' click -> callback [0x57d170]/close; port `--fe-popup igflow86` frame strokes + INGAME.FSH chrome + textid=60 "OK"; gate `fe_popup_igflow86`) |
| IGFLSUM.LAY | 45 | full-match summary screen (typ/box stat grid) | **impl** (full-time chain page 2: Enter on IGSCSUM pages to the IGFLSUM composite; Time/Player/Score column rows from `goalLog` (minute, shirt#+pen/og tag, running score), title FC#776 "Full-Time"; gate `fe_igflsum`) |
| IGHALF.LAY | 57 | half-time summary screen (stat grid, list widgets) | **impl** (livematch opens it at the half whistle — period-end arm via `Match_requestPeriodEnd` + event 9 grammar [C]; stat-grid pages + FC#858 "HALF-TIME" title; Esc = resume to match; gate `fe_ighalf`) |
| IGHILIT1.LAY | 1 | single box overlay authored `igmenu.lay` — header artifact; highlight hilite pane | **impl** (shipped dummy hotkey-transition page — the LAY member's own comment says so; composite renders, shapes/texts 0 legit: null box1) |
| IGHILITE.LAY | 19 | highlights browser: rewd/paus/play/fast/slol/slor transport buttons, boxes | **impl** (IGMENU 'Highlights' row opens the browser over `rplyindx.dat` rows — FC#899 "INSTANT REPLAY" title on box3, slot filename + `SLOT NNNN` readouts, btlt/btrt 10-row slot pages, slol/slor cycle; Enter = app/replay.cpp re-drive of the selected file (state-hash parity `match=1`); gate `fe_highlite_browser`) |
| IGHILSAV.LAY | 22 | highlights save list (`lst1`, up/down) | **impl** (browser 'save' button opens it: lst1 rows from the rply index, Norway/Denmark header pair = typ names + FNNN shields + nmb score digits painted after the flags (z-order 5344a02), up/down arrows via btnup->ingame alias; box3 header caption `titl` textid 303 = FC-remap 'Save Replay' painted by composite (soup misread corrected pass-28audit); Enter writes `rplyNNNN.dat` + index entry through the existing recordToUserDir path; gate `fe_hilsav_list`) |
| IGMENU.LAY | 14 | in-game pause menu: box1-5/typ/flg/nmb chrome + `menu` type=42 widget, name text — the popup the port lacks (Esc quits match directly) | **impl** (livematch Esc opens the decoded row table (§19): 18-px `{textid,code}` rows centred into box2, FC-remap captions via `widgetStringAt`, HILITED pane quad, frame strokes; armed table = byte-exact friendly-1p 0x57c7d8 10 rows (Resume Match/Instant Replay/Camera Select/Team Management/Controller Select/Options/Match Statistics/Restart Match/Quit Match/Quit to Windows — no cup-only "Quit World Cup"; pass-27c: the port fixture is the exhibition bootstrap, mode class {0,9}); Enter dispatch by code, code 4 opens the CONTROL1 board; mask-disabled rows skipped; lane hotkeys 'L'/'M'/'O'/'B' mirror 2p-table rows; gates `fe_igmenu`, `fe_igstats`, `fe_mgmt_pages`) |
| IGMSG.LAY | 17 | message popup authored `igqueshn.lay`; ok button, lines (`bo25`) | **impl** (pass-37igpop: IGMENU-family frame (box1/2/3 + header overlay widgets, textid=0 slots empty) + `ok` INGAME.FSH btnn/btnd chain + btt1 textid=60 "OK" via remap lane; tick FUN_00440790 titl/line/lin2 from [0x57c922]/[0x57c926]/[0x57c92a] (caller-set netplay strings), key FUN_00440830 'ok' click -> callback [0x57c930] installed by FUN_00440784; port `--fe-popup igmsg`; gate `fe_popup_igmsg`) |
| IGOPTN2.LAY | 21 | in-game options: tabs 1-4 + tab-button areas, option lists `lst1-4` type=17, back/fwrd; `textID=5`, help keys (78/81) — the ig\*optns trio lives here **[C]** captions "Match"/"Game play"/tab comments | **impl** (livematch Options row / 'O' opens the retail board: composite `bpic`/`subt`/`tab2-4` strips + active header pane `matH/gamH/conH/audH` + `help` pane + `back`/`fwrd`; four tab panes `lst1-4` = Match/Game play/controller/audio with FC-bank caption rows from the `ROWS` tuples (`analysis/pass21igmenu`); UP/DOWN row cursor + LEFT/RIGHT value cycle; 2/3/4 tab hotkeys; help caption FC#1948; gate `fe_igoptn2_board` §14) |
| IGPRAC1.LAY | 36 | practise-mode setup/stats popup | **impl** (Esc from the live PRACT1 board opens it: on/off toggle rows + btn nav, keeper toggles; gate `fe_practise_pages`) |
| IGPRACT1.LAY | 37 | practise stats page (back/forw) | **impl** (pause 'P' opens it over the live frame: FC#1249 'Number of Players' stepper (< > INGAME.FSH bt4n/bt5n), numPlayers readout, User/CPU Keeper rows FC 1920/374 with On/Off 1267/1263; RIGHT tabs to IGPRACT2, LEFT wraps; gate `fe_practise_pages`) |
| IGPRACT2.LAY | 22 | practise stats page variant | **impl** (page-2 option grid composite + FC captions, back/forw nav pair; gate `fe_practise_pages`) |
| IGQUESHN.LAY | 20 | yes/no question popup (`okay`+`canc`, `btt1/btt2` textid 12/13 "Yes"/"No", `titl/line/lin2`) | **impl** (livematch pause 'quit'/'restart' rows -> IGQUESHN confirm: shared box1 header overlay (`FUN_0043f3e8` names/shields/plate/digits), `titl` row caption + `line` question from the dispatch chain literals (remap 274/270, §28), `btt1/btt2` repaint uncovers the plate-clipped caption; gate `fe_igmenu`; shared key FUN_00440830 + Esc response-callback contract §33) |
| IGSCSUM.LAY | 36 | score-summary list (lst1-3, up/down) | **impl** (full-time board opened at match end: header + goal rows from `goalLog`, score/shields from match state; Enter pages to IGFLSUM; gate `fe_igscsum`) |
| IGSTATS.LAY | 67 | in-game stats screen (stat grid ×33 texts, team columns) | **impl** (IGMENU 'Stats' row opens the composite over the live frame: two-page L/R axis — Possession/Corners Won/Caught Offside/Fouls Committed/CARDS + GOALS/Shots off Target from `TeamStats` + `possessionFrames`; FC captions 1382/516/392/758/354/781/1708; Esc = dismiss to menu; gate `fe_igstats`) |
| IGTEAM72.LAY | 49 | in-game league match setup mirror of FEFLOW72 (`t100-112` igm titles, form rows) | **impl** (IGMENU 'Management' row opens the §E page cycle: pNNN pitch grid + tNNN formation tiles from FCDB lineup shirt numbers/positions over the live frame; header pair names + bias %; gate `fe_mgmt_pages`) |
| IGTEAM73.LAY | 91 | in-game squad list mirror of FEFLOW73 (lin×10 rows, sbar, t44×15) | **impl** (kt00..08 kit row + lin player rows from the FCDB squad; ab00/ab01 sub pair: Enter swaps starter<->bench (`igmgmt: swap` evidence line), UP/DOWN pair cursor; gate `fe_mgmt_pages`) |
| IGTEAM74.LAY | 46 | management grid mirror (yel/red cards ×5 each) | **impl** (pr/red/yel card rows + vit0 stamina from the sim booking state, FC captions via fcBankText; gobk/gofd page cycle; gate `fe_mgmt_pages`) |
| IGTEAM75.LAY | 33 | lineup editor mirror (`ptop` player block s001/tt00) | **impl** (ptop player block + s001 slider with UP/DOWN value grammar; scene-ownership whitelisted composite; gate `fe_mgmt_pages`) |
| IGTEAM79.LAY | 64 | squad/formation mirror ("attack bias slide bar" comment) | **impl** (s001 attack-bias slider: UP/DOWN ±10 keyloop grammar, live teams[0] bias in the header pair; gate `fe_mgmt_pages`) |
| IGTEAM80.LAY | 57 | custom-mode grid mirror (yel/red, players block, help= keys) | **impl** (players block + card rows composite + FC help strings; cycle position 6; gate `fe_mgmt_pages`) |
| IGTEAM89.LAY | 17 | save/load menu mirror of Feflow89 (b000-b005 buttons, gofd/gobk) | **impl** ('B' (tmf3 icon) opens the management hub: b000..b005 rows = FC captions Formation/Substitutions/Positioning/Attack Bias/Man Marking/Kick Takers + vit stamina bars; Enter enters mapped page {72,73,79,75,80,74}; Esc -> IGMENU; gate `fe_mgmt_pages`) |
| MANMARKS.LAY | 59 | player-marker management screen (yel/red rows + players block) | **impl** (`IgBoards` kind 15: pNNN squad grid rows from FCDB lineup/subs with kit swatches + row-cursor pan window (sbar thumb), mm00..mm02 defender <-> attacker pair captions, FC 1025 subtitle; igteam72 'M' opens it, Esc -> IGMENU; gate `fe_manmarks_grid`) |
| MESG.LAY | 10 | message box yes/no authored `C:\fifa98.pc\source\FE.OLD\LAYOUTS\mesg.lay` — FIFA'98-era popup kept for in-game messages | cap (gate `fe_mesg`; FUN_004425e0 'yes ' payload grammar §32) |
| ingame.lst | n/a | member name list (not a .LAY) | n/a |

## 2c. Loose FE/INGAME tree (29 .LAY + extras)

`WorldCup98_Extracted/FE/INGAME/*.LAY` = 29 files, names identical to the IGLAY.BIG members above (case-insensitive). Byte-for-byte comparison of every dump member vs loose tree (`cmp -s`, dumps `/tmp/iglay_dump/IGLAY.BIG.d` + `/tmp/pass12inv/iglay/IGLAY.BIG.d`, digest notes `/tmp/ingame_digest.txt`): all 29 `.LAY` members identical, plus `ingame.lst`↔`INGAME.LST` identical; **diff=0** — the packed archive and loose directory are one asset set; §2b covers both. Loose extras: `INGAME.FSH` (in-game art atlas), `IGFLAGS/` (flag art dir). No parity row needed beyond §2b.

## 3. Parity checklist (goal §6 columns)

Columns: **ref** = reference captured (asset-level evidence, see rule above — never Windows screenshots); **vis** = visual impl; **nav** = navigation wired; **int** = interactions tested by a gate; **aud** = audio+animations parity; **cmp** = side-by-side comparison passed.

| screen | ref | vis | nav | int | aud | cmp | evidence (gate = `tests/CMakeLists.txt`) |
|---|---|---|---|---|---|---|---|
| Main menu (4 entries) | [C] Feflow06/FEFLOW05 assets + exe strings 0x564f89/fb1/fcd/fe1 | ✓ composite+labels | ✓ arrows+Enter | ✓ `fe_nav_exhibition`, `fe_nav_practise`, `fe_nav_tournament`, `fe_nav_league` | ✗ menu SFX/music not wired into FE | ✗ no formal comparison gate | gates above; `fe_flow_render` |
| FEFLOW09 friendly config | [C] LAY+§5 dossier | ✓ | ✓ Enter from entry | part (smoke frames only) | ✗ | ✗ | `fe_flow_setup09`, `fe_nav_exhibition` |
| FEFLOW50 practise config | [C] LAY | ✓ | ✓ | part | ✗ | ✗ | `fe_flow_setup50`, `fe_nav_practise` |
| FEFLOW51 practise stadium board | [C] LAY + FE51 handler slots 0x459ff0/45a0fc/45a110 (`analysis/pass33fe51`) | ✓ composite: France-map back collage + mpNN ball pins (`hbal` cursor hilite) + nslt/nsrt smbutl arrows + sint/wint/hint value icons + help-pane stadium name | ✓ hub fe32 wedge → FEFLOW50 confirm → FEFLOW51 (onHub fix); Enter = 'fe16' practise launch (FUN_004520b4), Esc = fe50 pop | ✓ `fe_stad51` pin-ring cycle gate | ✗ | ✗ | `fe_stad51` |
| FEFLOW73 league squad list | [C] LAY+§1 slots | ✓ pn cursor x=35 y=134+19i | ✓ Esc/Enter | ✓ "entry 1 returned 0" assertion | ✗ | ✗ | `fe_league_select`, `fe_nav_league` |
| FEFLOW11 team grid | [C] LAY + pick state machine (pass-46fe11 §ZWCUP) | ✓ flags/kit/shield fidelity + cntr cell/state-gated red names + nopl counter | ✓ cursor window + tmfg pool row 0 + Enter = row-click/continue gate | ✓ `test_teamselect` TeamPickPool asserts (cycle/cap-revert) + shapes/texts/cursors>0 shell assert + `--fe-team-select 3` capture (counter 3) | ✗ | ✗ | `fe_render_tournament`, `fe_screen_capture_team`, `test_teamselect` |
| Tournament presentation (Feflow35/25/26/27) | [C] LAY + pass8 slot inventory | ✓ 35/25/26/27 overlays | ✓ tournament flow | ✓ "FE-PRESENT FEFLOW35: shapes=[1-9]" | ✗ | ✗ | `fe_render_present`, `fe_nav_tournament --fe-pick 35` |
| Font/text layer (all screens) | [C] exe font table DAT_0057d2c0 | ✓ faces=10 | n/a | ✓ PASS_REGULAR_EXPRESSION "faces=10 lang=" + ger locale | n/a | ✗ | `fe_text_fonts`, `fe_text_lang_ger` |
| Arbitrary-member capture lane (all 156 layouts = LAYOUTS.BIG + IGLAY.BIG members + loose FE/INGAME .LAY) | [C] member dumps pass11/lays, sweep extract | ✓ compositor (+button states, FRAMES anim) | ✗ static frame only | ✓ `fe_screen_sweep` (every layout renders a real PNG; raw stubs legitimately texts=0 — gate exemption `feScreenName`) + shapes>0 FAIL regexes (FEFLOW05/FEFLOW11) | n/a | ✗ | `fe_screen_sweep`, `fe_screen_capture_menu`, `fe_screen_capture_team` |
| Tournament save/load slots | [C] SAVESLOT_WC98.md | ✓ WCT1 codec | ✓ tournament-load nav | ✓ fixture gates | n/a | ✗ | `fe_nav_tournament_save`, `fe_nav_tournament_load` (WC98_SAVE_SLOT=7) |
| Intro movies (.DCT INT_EASP/INT_FIFA) | [C] VIDEO_DCT_NOTES.md + all-29 fourcc histogram (pass-74: SCDl×1017 in INT_FIFA); pass-75 SNDstreaminit gate `2 < _DAT_00632a0c` | ✓ | ✓ Esc skip | ✓ video_play lane | movie audio wired (pass-75: SCHl stream through Mixer, gate-mode drain rendered=1494277 peak=31605; wall-clock pacing deviation) | ✗ | app/video.cpp gates; `--no-intro` |
| Options (feflow15) | [C] LAY + init 0x44f22c FourCC tag dispatch (FE15_INIT `save`/`deft`/`fwrd`/`back`/`ops2`, `analysis/pass22fe15/dec2.txt`) | ✓ FEFLOW15 composite, tab pages tbb1..4, row cursor over lst{tab} `rows=N+` FC tuples (11 captions + value column paint — dead-scan parse bug fixed; pass-31f15slider: per-row slider pill `unsl`/`sllf`/`btnn` via global OPBUTNS.PAK, IGOPTN2 grammar) | ✓ hub fe15 wedge -> options; up/down rows, L/R cycles values (music/menu-sfx volume wired to settings.ini + live mixer gain); Enter='fwrd' commit+pop, Esc='back', mouse save/deft/fwrd buttons; ttxt header pane = tab title caption per TABSW rewrite (pass-22fe15b) | ✓ `fe_flow_options` PASS "FEFLOW15: shapes=[1-9]" + selftest 14b3 tag grammar | ✗ | §4 rank 1 (in-game IGOPTN2 board landed §14) |
| In-game pause menu (IGMENU.LAY + IGQUESHN.LAY + CONTROL1.LAY) | [C] tick=0x43FC40, init 0x43f040 row tables, tags 'gmnc'/'okay', dispatch 0x43f548 jump table `*0x440148(,%ecx,4)`; art FE/INGAME/INGAME.FSH bt*n/bt*d family | ✓ composite + frame stroke quads + header overlay (typ1/typ2 names, flg shields, box4/5 white plate, nmb1/nmb2 `%d`) + decoded byte-exact friendly-1p `{textid,code}` row table (0x57c7d8, 10 rows, exhibition mode class {0,9} = port MatchBoot fixture; pass-27c re-arm, was cup 0x57c832) with FC-remap captions, 18-px rows, HILITED pane quad; CONTROL1 in-game board kind 25 | ✓ Esc opens / dismiss-resume per board src, arrows skip mask-disabled rows (replay `FUN_00494fc0` empty, restart phase mismatch), Enter dispatch by CODE, pointer lane: hover = cursor on press edge, row press activates, IGQUESHN `okay`/`canc` press confirms/resumes; sim frozen while open | ✓ `fe_igmenu` PASS "IGMENU: shapes=29 texts=169" + "CONTROL1: shapes=10 texts=37" + "igmenu: mouse activate row=6 code=6" + _igmenu/_control1/_igquesn/_igmenu_mouse.png shots | ✗ | §4 rank 8 closed by §19 + §20 + pass-26c (FILE-data row tables, captions, pane, code-keyed dispatch, pointer lane; 2p/PK/draw tables reachable via lane hotkeys L/M/O/B) |
| Save/load scene UI (Feflow89 entry) | [C] LAYs + slot buttons b000..b005 (USER/SLOTnn.SAV) | ✓ FEFLOW89 composite slot list | ✓ hub fe41 wedge -> rows; Enter = WCT1 load when a file exists; Esc back | ✓ `fe_flow_saveload` PASS "FEFLOW89: shapes=[1-9]" | ✗ | §4 rank 6 (write path stays tournament-save fixture) |
| Hub screen (Feflow06.lay, 13 wedges) + credits (feflow94 roll via cred wedge) | [C] §3 global-key table; buttons objecttype 6/32, help pane type=14 `help=NNN` FC captions | ✓ composite: hover/push/disabled shape states + FRAMES anim + help pane text; cursor cycles all 13 wedges (keyboard + mouse widgetHit, pointer un-projected through the `fit4x3` UI rect — pass-44pointer, `RENDERER.md` coordinate space) | ✓ fe07->friendly, fe11->team grid, fe32->practise, **fe33->feflow90 history board (+`fe91` chain to feflow91)**, quit->exit, fe15/fe41/cred -> views (cred = FEFLOW94 CRED.BIN roll, pass-34), **repl -> FEFLOW57 replay browser (pass-29, [C] leaf 0x4491d5 cmp 'repl' + DAT_0057ca48=1)**; fe90/fe04/fe02 disabled no-ops (documented) | ✓ `fe_hub` PASS "FEFLOW06: shapes=[1-9]" + `fe_credits` PASS "FEFLOW94: roll rows=[1-9]" + `fe_history_select`/`fe_nav_history` PASS "FEFLOW90/91" | ✗ | §4 rank 10 partially landed (shootout/modem/network = no simulation in port; classic history presentation now open; repl highlights open) |
Honest summary: **impl** = 13 registry scenes (incl. feflow90/91 history boards + shared popup lane feflow10/37) + hub composite (Feflow06, 13 wedges live) + options/saveload/credits views + in-game pause (IGMENU/IGQUESHN) + IGCONSEL box; igoptns trio = captions lane (art proven absent §7); **cap** = all 156 layouts via the capture lane, every one asserted to render a real frame by `fe_screen_sweep` (visual only, no gate asserts pixel parity with Windows — cmp column is empty by design); in-game pack interactive = pause menu + option popups captions + IG confirm boxes.

## 4. Missing features, ranked (user-reachable in the original main flow first)

Each entry: what's missing → RE blocker. Blockers reference handler slots §1; "thunk decode pending" = slot body is an 8-byte jump thunk with no Ghidra function object, raw disassembly required (`menu7_handlers.txt` MISSING set).

1. **Options screen family** — feflow15 (4 tabs: match/gameplay/audio/visual) + in-game IGOPTN2/iga-igg-igmoptns + FEFLOW80 custom grid. **[LANDED 2026-11-07: hub fe15 wedge opens the FEFLOW15 composite, tab pages tbb1..4, row cursor + L/R value cycle (audio volumes wired to settings.ini/mixer); LANDED 2026-11-17 pass-21igscn: in-game IGOPTN2 board = retail scene, gate `fe_igoptn2_board` §14; LANDED 2026-11-17 pass-22fe15: FE15_INIT FourCC tag dispatch decoded (`analysis/pass22fe15/dec2.txt`) — 'save' 0x73617665 = settings writer cluster FUN_0046b0fc/b5c8/b3e0/b83c/b074 + options.sav FUN_0046b320 + click sfx FUN_004678b8 staying on screen (FUN_004461b4 redraw), 'deft' 0x64656674 = FUN_00459410→FUN_004593d4 cursor-state reset (NOT settings defaults: defaults writer FUN_0046ae8c runs on scene OPEN event 1001, saved values read via FUN_0046b060), 'fwrd' 0x66777264 = writers + scene pop; mouse wiring + Enter='fwrd' + Esc='back'; `lst1-4` `rows=N+` tuple parse fixed (dead-scan loop emptied the block — 11 FC captions + value column never painted before), selftest 14b3.]** Blockers: type=17 option-list widget model (row/value pair painter), dynamic `textid` → FC-bank resolution for row captions; tick 0x44f804 decoded **[C 2026-11-17]** `analysis/pass20uitick` (options pump = widget hide/update branches, no key-token compares — keys live in init FUN_0044f22c, ported); in-game board handler cluster decoded **[C 2026-11-17]** `analysis/pass21igscn/ighandlers.txt` + `pass21igmenu/ig2000.txt` (ROWS tuples = FC-bank caption/value id pairs, list painter FUN_0043c9a0 remap lane gated by `test_fcwidget`). LANDED 2026-11-17 pass-22fe15d: Language-row arrow switch = event 1017 `0x3f9` (FUN_0046b074 writer + FUN_0047ab1c `common\%s` bank reload) ported as `FeFlow::setLanguage`, gate `fe-options` langSwitch + capture `--fe-options-lang` §16. LANDED 2026-11-18 pass-31f15slider: per-row slider pill art (`unsl` 185x17 track at lstN.x+hotspot 198, `sllf` fill fraction, `btnn` knob, value text over pill) — `opbutns.fsh` resolves via global object cache to FE/PACK/GLOBAL/OPBUTNS.PAK, IGOPTN2 pass-21igscn grammar; gate `fe_flow_options` shapes=31, capture `--fe-options-tab 3` audio tab §23 (`analysis/pass31f15slider/NOTES.md`).
2. **Controller/2-player select** — feflow07/32/33 (match setup volume bars/balls) + feflow87/91 (controller grids, `anm*` animseq pictures, gravis numbers). **[LANDED partial 2026-11-08: feflow07 single-player match setup — hub fe07 wedge opens FEFLOW07 composite, ltnm/rtnm names + shdl/shdr shields over the lbNN/rbNN skill bars (FCENG Shooting/Passing/Speed/Defence/Overall), two-axis rows: UP/DOWN side cursor with yellow HILITEDSHAPEIDON box, LEFT/RIGHT fotl/fotr team cycle from the FCDB pool; Enter = 'fe09' forward edge. Gate `fe_setup07`.]** LANDED 2026-11-17 pass-20cfg: FEFLOW32/33 pool-entry boards visual parity (§13). LANDED 2026-11-17 pass-23fe87: FEFLOW87 shootout controller board — 4 slot overlays (`ovrN`+`alpN`/`arpN` panes), `anmN` controller icons through the composite animseq/type=3 player with CONTR.FSH frames 1..8 (`ticksperframe=0`, fresh board paints frame 1 — open event 0x68 zeroes device-slot group DAT_00608748..0x608784 [C `analysis/pass23fe87/fe87_dec1`]), `arwN` pair arrows, `ltfg`/`rtfg` team shields, `titl` ENGL87.PAK title, `tmtl`/`tmtr` "Teammates:" FC labels; Enter = 'fe87' wedge from feflow33, Esc = pop, quit/fe06/fe15/fe07/fe08 rail edges. Gates selftest `fe-cfg87` + capture `--fe-cfg87`. Remaining blockers: feflow91 controller grid gravis numbers (`grvN` runtime text slots), controller-state source model.
3. **Match/stadium select + schedule flow** — feflow08/51 (stadium picker, ball set `mpx10`, HALF LENGTH), feflow20/21 (today's games, simulate). **[LANDED partial 2026-11-08: feflow08 stadium select opens from the friendly-config confirm ('fe08' tag @0x44C3D3) — France-map mpNN ball-marker tiles (`hbal` hilite), nslt/nsrt arrows cycle the tile ring, stme runtime stadium name + hint pane captions (FCENG 1692.. bank indices), sint/wint/hint decoration row; Esc = back. Gate `fe_stadium08` renders via the feflow07→feflow09→FEFLOW08 chain. **feflow51 [LANDED 2026-11-18 pass-33fe51: practise-side sibling — hub fe32 wedge → FEFLOW50 confirm opens FEFLOW51; FE51 slots 0x459ff0/45a0fc/45a110, ring cursor `DAT_0057cb54` ±1 wrap (initial 2), 'fe16' forward → match-start driver FUN_004520b4; gate `fe_stad51`; `analysis/pass33fe51`]**]** feflow20/21 init+tick decoded **[C 2026-11-17]** `analysis/pass20uitick` (`simu` schedule grammar FUN_00484d1c/FUN_00485090 + "%s_%i" widget-name dispatch FUN_00467944; `stg1`..`stg3` stage captions DAT_0057d79c; scene-edge push FUN_004461b4 gated on DAT_0062fad0+0x5060). **[LANDED 2026-11-17: feflow20/21 interactive via the `--fe-wc-cfg` lane — tgt/tns/tsd rows + QF "a v b" pair strings + SF shield flags painted from the TourneyModel; `simu`→feflow86 confirm, `frwd`→feflow13, `stsc`→feflow82, `strk`→feflow81 edges wired with focus ring. Gates `fe_wc_cfg20`, `fe_wc_cfg21`.]** Remaining blockers for 51 interactive: schedule data binding to the tournament module (`TOURNAMENT_MODULE_WC98.md`); list widget type=9 painter (feflow51 tick decoded 2026-11-18 pass-33fe51 — `DAT_0057cb54` ring + shared stad08 serve block, board interactive via `fe_stad51`).
4. **World-cup competition family** — feflow13 config, 16 lineups, 18 zones, 19 groups, 23/24 brackets, FEFLOW35 board (25-27+35 impl). **[C 2026-11-17]** feflow18/19 init + feflow13/20/21/23/24 init/tick decoded `analysis/pass20uitick`: key-token grammar ("fe06".."fe8a"/"rand"/"simu"/"simv"/"stnk"/"strj"/"strl"/"stsc"/"strt"/"sint"), `rand` staged randomize = counter DAT_00608bd4/8 cap 99 → FUN_004cf040 redraw + hide at 99 (port wcRandCount_ matches); feflow13 tick 0x44e578 = thunk → FUN_0044c6c0 (feflow09 pump) **[C]**. **[LANDED 2026-11-17: interactive family via the `--fe-wc-cfg` lane + `wcStack_` scene-switch state machine in `app/fe_flow.cpp` — NOTES §4 token→scene edge tables wired (18→fe19/fe11/fe15/fe41/fe42, 19→fe20/fe18 + staged `rand` 99, 20/21/23/24 →frwd→20/13, strk→81, stsc→82, simu→86 confirm; Esc pops the parent chain); composites paint zone/group/schedule/bracket/results/stats content from the TourneyModel (`g{g}t{s}`, tgt/tns/tsd, tm0N pair strings, qtm+shield flags, `mult` rows, t00N stat rows). Gates `fe_wc_cfg{18,19,20,21,23,24,35,81,82}`.]** Remaining blockers: feflow18 tick 0x4524f8, feflow19 tick 0x452884, feflow16 (0x4519c0/0x451aac) decode pending (animation/painter bodies; navigation fully decoded); navigation edges are code-side (shipped LAYs carry **no `Message=` keys**, no FEMSG.BIN exists — confirmed negative pass11); FC-bank captions resolved [C] so art/text parity is asset-complete. **[LANDED 2026-11-17 pass-24wc13: FEFLOW13 world-cup controller board interactive — init token tree `FUN_0044e440` decoded (`analysis/pass24wc13/fe13_asm.txt`): `'fe06'`/`'quit'` send FUN_00459410 tuples (0x3ea,0x82e,0x10f3 WC-exit / (0x3e9,0x82e,0x10f1 app-quit) then push NAME:feflow37 confirm, `'fe15'` options jump, `'fe08'` forward = FUN_00446624()==2 stay-guard else FUN_00448418(0)+FUN_0045a198 + push NAME:feflow56 tournament launch; Esc = event branch 0x3ea push NAME:feflow06 (hub, no stack pop); board = 4 slot rows `ovrN`+`alpN`/`arpN`+`anmN` contr pictures + `arwN` arrows, plate art feflow13.fsh, device-slot group reset on open 0x68. Gates: selftest 14b5 `fe-wc13` + capture `--fe-wc13/--fe-wc13-dev` (§18).]**
5. **Records/trivia/history bookmark family** — feflow28-31, 34, 88 (+ type=35 multitext panes). **[LANDED 2026-11-17: full bookmark family interactive — hub ribbon icons jump pages; FEFLOW28 hub (shield + `tmnm` pool name), 29/30 head-to-head tables from `.RES` — grammar settled by the "sbar" branch of `FUN_0045721c` (records_dec10:149-192 + dec7:42-59): rows ride a pool-anchored running index (DAT_0060a06e, = offset+1 when offset==own) that skips the own slot (DAT_0060a070) ONLY when the window crosses it (fetch advance `idx+1`; on `idx==own`: bound+1, idx+2); row r valid while `DAT_0060a064 (= poolSize-1) >= bound + r`, else the clear branch NULs the sprintf'd string slots — the row paints blank, not zeros. The pre-pass "pair-indexed over pool-minus-own" map shifted the offset>own window by one row; corrected 2026-11-17, gate = selftest step `records-window` pinning own=0 / offset<own / offset==own / offset>own / crossing tail / clear-branch boundary. Row record slots `+0x34 = b0+b1+b2` (Games) / `+0x38 = b0` / `+0x3c = b1` / `+0x40 = b2` via `FUN_0049168e`; triple = `{won, drawn, lost}` for the row team (pass-12 venue-flag reading superseded — see `analysis/pass12tourn/ZWCUP_TRIPLE.md` SUPERSEDED note), 31 general + 34 trivia + 88 greats multitext panes from `FE/TEAMHIST.<lang>/.HIS` cells (`FUN_00489f58` list builder, 16/page `FUN_00488CF0`); `hd*` headers are runtime-localized FC-bank captions (LAY textids stale: 1318="Paris", 444-447="Continues"…), overlaid from bank 1272/799/1954/614/1009 = Opponent/Games/Won/Draws/Lost. Gates `fe_records28/29/30/31/34/88` + `fe_records_nav` (team-cursor ring) + `fe_records_window` (sbar row-window offset) + selftest `records-window`. Dossier `analysis/pass20records/` (decompiles copied 2026-11-17).]** The `.TXT` resolver blocker was a misread: trivia text lives in the `.HIS` multitext cells themselves (no tag indirection); bank sentinel stays FEFLOW00-only.
6. **Save/load scene UI** — feflow41/42 (+57 replay-load, Feflow89 team-save menu). **[LANDED partial 2026-11-07: hub fe41 wedge -> FEFLOW89 slot list, WCT1 load on select; write path stays the tournament-save fixture. LANDED full 2026-11-08: feflow41 LOAD + feflow42 SAVE-confirm scenes — `lstb` rows scan real `SLOT%02d.SAV` files from the XDG slot dir, captions decode path (`SLOT00 m1 g16 k16`), sbbg/sbar page slider; Enter on 42 writes via `saveTournament` + cros→tick flip (nvicons tic1); gates `fe_loadsave41`, `fe_savemgr42`; fepack lazy loader decoded pass-13 (`FEPACK_LOADER.md`).]** **[LANDED 2026-11-18 pass-29: FEFLOW57 replay browser — hub wedge `repl` [C] leaf 0x4491d5 (`cmp $0x7265706c` → DAT_0057ca48=1 at 0x4491ea; scene name registered in pointer table VA 0x574dec; no code pushes the name — runtime fixups) opens the browser (dropped from the disabled-wedge list); `lstb` rows = `rplyindx.dat` slot filenames whose `.dat` exists, caption id 322 → remap FC#999 "Load Reel", `fe06` crs1 close overlay, Enter re-drives the focused file via `replayFile` (sealed hash == re-drive hash, `match=1`); deterministic fixture seeded by a MatchBoot+SimRunner pad stream under scratch `XDG_DATA_HOME`; gates `fe_replay57`, `fe_replay57_enter`.]**
7. **League management family** — feflow72/74/75/76/79/80 (+ FEFLOW43 superseded grid, 78 popup). **[LANDED 2026-11-07b..08: league widget painter (list type=9, row-label 42/44, slider 11) + interactive gates for feflow72/74/75/76/79/80 (§7); pn00..pn08 league-list row captions painted; FEFLOW43 standalone management grid landed 2026-11-08 — gate `fe_mgmt_grid43`, evidence `FEFLOW43: shapes=21 texts=91` (FCDB squad rows on vNNN slots, FC-bank position labels GK/SW/RB/LB/RM/LM/RF/LF/MF/FW, FORM/SUB/STRAT/CUST + AGG/POSIT/KICK/ATT.BIAS headers, four-row pan window with sbar thumb, L/R team cycle, Enter = saveTournament; §10).]** The attributes +/- state machine stays a code-side task; no format blockers remain. **[DECODED 2026-11-18 pass-40league:]** feflow74 event token tree + tick fully decoded (`analysis/pass40league/`); Enter = `tmf2` opens the FEFLOW76 popup over the grid and closes back to it (gate chain selftest `fe-league-nav`). The `attb` budget figure + `tmf0` budget gate (`[0x5882b8]`, error caption 0x45e728) remain engine-side — the port has no squad-budget simulation to feed widget `[0x57e84a+grp*4]>>16`.
8. **In-game FE popups** — IGMENU pause menu, IGQUESHN/MESG/IGCONSEL confirm boxes, IGHALF/IGSCSUM/IGFLSUM/IGSTATS stats screens, IGHILITE/IGHILSAV highlights browser, IGTEAM72-89 mirrors. **[LANDED partial 2026-11-07: livematch Esc opens the IGMENU composite (INGAME.FSH bt*n/bt*d art), {Resume,Quit} rows, Enter quit -> IGQUESHN confirm, sim frozen while open; runtime item captions undecoded. Confirm-box family landed same day via the shared popup lane (`--fe-popup`): FEFLOW10/37 FE modals + IGCONSEL/MESG/IGQUESHN grammar (§A null.fsh placeholders legit, INGAME.FSH btnn/btnd chrome via button->ingame alias); gates `fe_popup_ok/_yesno/_feflow86`, `fe_igconsel`, `fe_mesg`. Stats-screen family landed same day: IGSTATS opens from the IGMENU 'Stats' row over the live frame (two-page L/R stat grid, FC-bank captions + `TeamStats`/`possessionFrames` values), IGHALF opens at the half whistle (period-end arm grammar [C]), full time chains IGSCSUM -> Enter -> IGFLSUM (goal rows from `goalLog`); Esc dismisses to caller per the ESC=0x65 resume contract; gates `fe_igstats`, `fe_ighalf`, `fe_igscsum`, `fe_igflsum`. Highlights family landed 2026-11-07: IGMENU 'Highlights' row opens the IGHILITE browser over the `user/rplyindx.dat` rows (FC#899 title, slot readouts, btlt/btrt pages, slol/slor cycle; Enter = app/replay.cpp re-drive with state-hash parity), 'save' button -> IGHILSAV list + `recordToUserDir` write path; gates `fe_highlite_browser`, `fe_hilsav_list`. IGTEAM72..80/89 management mirrors + hub landed 2026-11-08 (commit e760202, gate `fe_mgmt_pages`; MANMARKS grid d1bd142 gate `fe_manmarks_grid`). Camera popups IGCAMERA/IGCAMSEL + practise pages IGPRAC1/PRACT1/PRACT2 landed 2026-11-08 (IgBoards kinds 16–20, pause 'C'/'P' chains; §10; gates `fe_camera_pages`, `fe_practise_pages`).]** **[CUTOVER 2026-11-18 pass-26c/27c: pause-menu row grammar ported 1:1 — per-mode runtime row tables decoded byte-exact from the data segment (delta VA=raw+0x3FEA00; earlier BSS-only negative was a wrong 0x400000 delta): u16 count + stride-4 {textid,code} rows; init selects by mode class `DAT_00640fb4` (store sites 0x43f06e..0x43f2b1), port fixture = friendly 1p table 0x57c7d8 (pass-27c re-arm, replaces the cup table 0x57c832); captions via widget-id remap; row height 18 with runtime centering into IGMENU box2; cursor skip-mask per tick 0x43fc40 fill loop (code 1 disabled when replay list `FUN_00494fc0()` <= 0 — old cite 0x495bc0 corrected; code 9 masked only outside normal match phase); Enter dispatch keyed by code through the 23-entry jump table VA 0x440148 (`handlers.txt`). Board kind 25 CONTROL1 in-game controller-select via code 4, routed to `drawIgOptns`; mouse row activation lane pass-27igmouse. Gate `fe_igmenu` (IGMENU + CONTROL1 + IGQUESHN + mouse-activate evidence); dossier `analysis/pass26igmenu/` — no format RE blocker remains, only code.]
9. **Netplay/modem family** — Feflow02/03 lobbies, FEFLOW04 (mnmcon)/FEFLOW05 (MNMSETUP)/FEFLOW14 (mnmdial), IGCHAT, FEFLOW00 net-test stubs. Blockers: whole netplay subsystem (modem + IPX code) undecoded; edit-box type=10 text input in compositor; out of LAN scope — rank last for parity goal if LAN is descoped [H].
  10. **Presentation-only leftovers** — credits feflow94, icon/key popup feflow95 (type=18 checkbox widget), classics Feflow90/91 + shootout path (Feflow06 `fe33` button), highlights (`repl`) + credits buttons on the menu hub. **[LANDED partial 2026-11-07: hub screen with all 13 wedges live; feflow95 cheats screen (rank-8 slice); fe33 wedge -> feflow90 World-Cup history board + feflow91 chain (pass 13); cred wedge -> FEFLOW94 credits roll with CRED.BIN cell-stream contents (pass-34; FEFLOW19 mapping was stale — registry slot 54 = feflow94, handler 0x469904/0x46986c; retail FEFLOW94 art pack absent from this CD). Remaining no-ops: fe90 shootout, fe04 modem, fe02 network — the port has no simulation of those modes.]** Blockers: tick decode pending.
11. **Internal/debug-only** — feflow00/17/45/83/84/93, FEFLOW52/16 lineups, FEFLOW85 foot test, 26 stub members: no parity requirement; keep capture-lane coverage only.

## 5. Cross-checked counts (against source files)

- SCENE registry rows: **66** (`grep -c "^SCENE "` on `menu5_scenemap_windowobj.txt`); unique scene names **56**; 55 primary rows + 11 second-pass sites (block 0x4481b5-0x4483d3).
- LAYOUTS.BIG members: **101** (file header line 1; `grep -c "== MEMBER"` = 101) — includes `fe.lst` manifest, 32 single-widget stub members (`numobjects=1 [objects parsed: 1]`, grep count), 3 ig\*optns popups.
- IGLAY.BIG members: **30** = 29 .LAY + `ingame.lst`; loose FE/INGAME tree: **29** `.LAY` files (+ LST/FSH/IGFLAGS); every member byte-identical to its loose-tree counterpart (`cmp -s`: 29+LST identical, diff=0).
- Port interactive scenes (post-`65fbeda`): hub Feflow06, friendly/practise setup 09/50, team grid 11, league list 73, options feflow15, save-load Feflow89, credits roll (FEFLOW94 + CRED.BIN cell stream, pass-34), presentation boards 25/26/27/35, shared popup lane (FEFLOW10 ok/cancel + FEFLOW37 yes/no: blur+popp composite, nvicons button pair, FC captions + help pane, two-slot cursor, Enter/Esc close to caller; hub 'quit' wedge opens the confirm), in-game IGMENU+IGQUESHN overlay (`app/fe_flow.cpp` + `app/livematch.cpp`; gates §3), camera popups IGCAMERA/IGCAMSEL (IgBoards kinds 16/17, pause 'C' chain; gate `fe_camera_pages`), practise pages PRACT1/PRACT2/IGPRAC1 (kinds 18-20, pause 'P' page cycle + keeper toggles; gate `fe_practise_pages`), CONTROL1 controller page (options-chain popup lane, FC#258 caption + DAT_00632778 percent bars + live pad axis readout; gate `fe_control_page`). Capture lane covers every LAYOUTS.BIG member (`--fe-screen`).

## 6. Verification sweep + functional-parity evidence (run 2026-11-07, post commit `65fbeda`)

Method (goal §6): capture every screen via the proven `--fe-screen`/gate lanes, region-scan each PNG against its LAY-declared widget rectangles (`parity_cmp.py`: non-uniform pixels inside every rect = art drew; `fname=RECT fillcolour=` widgets legitimately render uniform color), compare against PAK/FSH asset sheets. Windows screenshots remain impossible on this host (Wine DirectDraw black [C]; QEMU present but Win9x guest needs licensed OS media) — reference basis stays asset-level, per goal §7 documentation rule.

- Full LAYOUTS.BIG sweep (complete 101-member dump, `wc98-extract`): **100 members rendered (all incl. lowercase stubs + ig\*optns trio), 89 region-clean**. The 11 flagged are each explained [C]: FEFLOW00 dev-stub anchors; help panes in FEFLOW10/37/76/78/86/feflow95 (dynamic FC captions on hover only); INGAME raw HUD anchor widgets; FEFLOW76/78 `s001` league list rows (rank-7 painter slice, in progress); igoptns trio `ok/canc/defa` (IGICON.FSH gap, row below). Raw evidence: `/tmp/sweep-out/report.txt`.
- Gate captures visual QA (viewed): hub (Feflow06 France '98 art + wedge rail + help-pane FC caption), options FEFLOW15 (`texts=42`, tabs tbb1-4, yellow help pane "Confirms selection and allows progression" = FC-bank stringAt), save/load Feflow89 ("Slot 0 (empty)"..`Slot 5 (empty)` bars + nav arrows), credits FEFLOW94 roll (scene back collage + remapped caption headers "Executive Producer:" + literal staff-name rows in 18-px lanes, pass-34), in-game pause over live match: IGMENU/HUD strip + IGQUESHN authentic prompt "Are you sure you wish to exit with points remaining?", popup lane captures (`/tmp/wc98port-out/pop10_fe00.png` red-italic "Are you sure?" + ok_1/canc nvicons pair + help pane stringAt(12); `pop37_fe00.png` "Are you sure you want to Quit?" (FC index 220) + yes/no pair + stringAt(13); `fes86_fe00.png` panel; `igcon_fe00.png` INGAME.FSH "OK" chrome over legit null boxes; `fesmesg_fe00.png` raw 85-line caption board) — all region scans PASS.
- Full suite at landing: **ctest 95/95 green** (incl. `fe_hub`, `fe_flow_options`, `fe_flow_saveload`, `fe_credits`, `fe_igmenu`, `test_fescenes` wedge-mapping pins, popup lane `fe_popup_ok/_yesno/_dismiss_enter/_dismiss_esc/_feflow86`, `fe_igconsel`, `fe_mesg`; pre-existing FE nav gates unmodified).
- Functional lanes [C]: mouse = `widgetHit` POINTS-polygon/box hit-testing wired into hub/options/save-load pointer activation (SDL LMB); keyboard = arrows/Enter/Esc per tag model (`quit`, `feNN`, 'back'/'deft'); controller = original polls legacy WINMM joystick (`joyGetNumDevs/joyGetPosEx`, Gravis/Sidewinder name tables [C] exe import scan) — port maps SDL gamepad buttons to the same arrow/Enter/Esc codes; audio = NO menu click sounds exist in the original [C AUDIO_RUNTIME], FE music .ASF loop + ambient bank already wired, options volume rows drive settings.ini + live mixer gain.
- Resolution/timing: 640×480 virtual canvas, aspect-correct letterbox scaling (gates capture at native 640×480); animation advances on a millisecond clock (`animMs`), independent of render resolution/vsync; frame pacing per LAY FRAMES= ranges.
- Captured IG option popups (`--fe-screen igaoptns/iggoptns/igmoptns`: shapes=5, texts=46/33/37): region scan flags `ok/canc/defa` + help pane rects **black** (mean 0) — the buttons reference cross-file art `vpath=..\ingame\ fname=igicon.fsh sname=chck`; ScreenArt resolves FEFLOW*.fsh/FE PAKs (+INGAME.FSH via vpath-stem fix), IGICON.FSH not in the set → shape missing. Fix = add `..\ingame\igicon.fsh` to the art resolution list (small, queued with rank-8 slice).
  - Correction (proven 2026-11-07): **no `igicon.fsh` exists anywhere in the shipped tree** (`find` = zero hits; FE/INGAME loose files only INGAME.FSH; no IGICON member under FE/PACK incl. per-scene FEFLOWxx PAK dirs). The popup buttons' art ships via the window-object lazy-loader path `"fepack\%s.all"` (`FUN_0047a12c`, rank-6 blocker) — resolution needs that loader, not a static file list. `chk1..5.PAK` exists under FE/PACK/FEFLOW15 (options checkboxes, rank-10 type=18 art) [C].

## 7. Round 2026-11-07b — league family + world-cup family landed (commits `f0f8109..d323433`, gate fix)

- **League family [LANDED]**: FEFLOW72/74/75/76/78/79/80 config grids/popups + FEFLOW73 list, painted via
  list(type=9)/row(42/44)/slider(11) painters, kit swatches through `presentation::kitColor` (fifa98edit hslCols
  verbatim), soup suppressed by scene-ownership whitelists (§11 doc MENU_NAV_WC98.md). Gates `fe_league_select`,
  `fe_league_cfg{72,74,75,76,78,79,80}` all green; captures viewed: rows + swatches legible. FEFLOW72 draws the
  "Art Director" FC-bank#233 quirk — faithful to the original's string bank [C].
- **World-cup family [LANDED]**: FEFLOW18 zones board (32 tmNN pool names, group letters A–H, staged `rand` re-draw),
  FEFLOW23/24 R16 bracket boards (kit-shield cards, dark-ink names on silver plates, focus ring strk/stsc),
  FEFLOW35 standings page (`nglt/ngrt` mod-8 group cursor, P/W/D/L/GF/GA/pts rows). Tag semantics decoded in
  `analysis/pass12/wcfamily_notes.txt` (Ghidra pass 12: FUN_004cf150/FUN_00452568 rand, FUN_00464928 strk,
  FUN_00451964 group nav). Gates `fe_wc_cfg{18,23,24,35}` green; captures viewed.
- **FEFLOW73 gate root cause [FIXED]**: `--fe-league` ran `selectNav(LeaguePlay)` (hub case 1 opens the list)
  *then* `enter()`, which on the hub re-activated the default wedge → Friendly setup (`setupScreen_`) preempted
  `drawCurrent`'s league branch; stats printed "FEFLOW73" while feflow09 drew. Fix: gate calls `enter()` only on
  the FEFLOW05 fallback menu. Also `cfgTeam()` falls back to the FCDB head team when `poolIds_` is empty (league
  list opens with no pool screen ran), so pn rows paint "#jersey name" captions + tricolour kit swatches:
  capture now shows `#8 Simeone … #15 Zanetti`, texts=85, region-clean.
- Sweep this round: 12/12 interactive family screens render (`/tmp/sweep-out2/report.txt`); full suite **ctest 80/80**.

## 8. Round 2026-11-07c — objecttype-18 checkbox painter + feflow95 Cheats screen [LANDED]

Scene `feflow95.lay` (LAYOUTS.BIG member, 29 objects) = the hidden Cheats/debug board. Registry row **[C]**
`analysis/pass12/registry_all.txt:56` (`+0x10=0x469af8`, draw `+0x18=0x469a60`, event `+0x1c=0x469cb8`; also
`analysis/pass11/menu5_scenemap_windowobj.txt:72`). Hub global-key payload **0x1E** → feflow95 **[C]** `MENU_NAV_WC98.md` §3 row
(evidence `0x4456A3–0x4456A8`, `analysis/pass12/edges_table.txt:31`). Payload codes are menu-context IDs with no statically recoverable
keyboard mapping **[H]** (`MENU_NAV_WC98.md` §3 note), so the port opens the scene from a documented dev key: **F9** in interactive FE, Esc closes.

### 8.1 objecttype 18 = checkbox / toggle row (painter parity)
- Rows `btn0..btn7`: `objecttype=18`, x=118 y=183+25i w=15 h=14, `vpath=..\igtman\ fname=Xblak.fsh`, `hilitedshapeidon=blkx`,
  `hilitedshapeidoff=null` **[C]** LAY dump `/tmp/layd90/LAYOUTS.BIG.d/feflow95.lay:117-234`.
- Frame = a separate objecttype 2 widget per row, `box1..box8` (x=117 y=182+25i w=17 h=16, `cheatbox.fsh#1box`), painted by the compositor;
  the toggle glyph is an overlay (`presentation::paintCheckbox`, driven per row from `FeFlow::drawCheats`) — same architecture as
  paintRowLabel/paintSliderThumb, composite() stays generic.
- State semantics **[C]**: feflow95's draw body calls the widget-hilite setter FUN_00479224 at sites 0x469c6b / 0x469ca6 / 0x469cb1
 (`analysis/pass12/edge2.txt`, section from line 11629). That setter selects the HILITEDSHAPEIDON/OFF branch of the button-class shape
 chain: ON paints `blkx`, OFF paints nothing (`hilitedshapeidoff=null`), the frame box staying visible.
- State storage **[C]**: scene global DAT_0057d2a0, read per row at 0x469a19 (`MOV ESI,[EDX+0x57d2a0]`) and 0x469c54
  (`MOV EAX,[EBP+0x57d2a0]`) — `analysis/pass12/nav2_raw.txt:14379,14612` — plus the event handler read at 0x469ce7
  (`MOV AL,[0x0057d2a0]`, `analysis/pass12/nav1_raw.txt:24235`). The address is DGROUP/.bss (neighbour of the exe font table DAT_0057d2c0)
  so it starts cleared; per-row writes are not statically enumerated, hence the port keeps its own bool array **[H]**.
- Grammar note: `formats::LayoutObjectType` names class id 18 "Video" (the FIFA 99 grammar reserves CheckBox = 10); feflow95's key set plus
  the FUN_00479224 hilite writes show this family is a toggle. Enum untouched; documented here only.

### 8.2 Captions are exe strings, not FC-bank indices
`titl` (x=144 y=145 w=200 h=19 fontID=4 normalColour=6) carries `textid=6`, txt0..txt7 carry `textid=0`; those defaults resolve to other
contexts' bank strings ("and icons" — exactly the soup the raw `--fe-screen feflow95` lane captures). The scene installs real captions
through CALL->0x4788b4 sites 0x469b2d..0x469bb7 of its draw body **[C]** (`analysis/pass12/edge2.txt:11631-11639`), and the strings are exe
data, byte-verified in `WorldCup98_Extracted/FIFAWC.EXE` (raw = VA − 0x401a00): "Cheats" @VA 0x56400c / raw 0x16260c; rows at
0x564014/0x564020/0x56402c/0x564038/0x56404c/0x564058/0x564064/0x564074 = "Take a Dive", "Hot Potato", "Big Heads", "Skeleton Players",
"Crazy Ball", "Silly Moves", "Flaming Ball", "Alien Model"; the unavailable-row caption "-" @VA 0x564090 / raw 0x162690 is installed by
FUN_004788bc. The table is pinned verbatim in `tests/unit/test_fescenes.cpp`. Caption colour: the LAY slots use palette index 0 (a dark
swatch on this pane); the runtime caption colour is set by scene code and not statically recoverable, so dark palette colours fall back to a
light one in `FeFlow::paintCaption` **[H]**.

### 8.3 Port lane + gate
- Interactive view (`app/fe_flow.cpp`): F9 opens / Esc closes; arrows move the row cursor (btn0..btn7), Enter flips the focused row, click =
  focus + toggle (`openCheats/moveCheat/toggleCheat/pointerActivate`). `--fe-cheats N` opens it with row i ON for bit i.
- Gates `fe_cheats` / `fe_cheats_off` (`tests/CMakeLists.txt`): evidence line `FEFLOW95: shapes=15 texts=107 cursors=1 faces=10 lang=eng`;
  capture `/tmp/wc98port-out/cheats_fe00.png` shows the blur backdrop + cheatart pane, 8 legible rows with the X glyph on rows 0/2/4/6
  (mask 85), the 'yes' check button and the help pane carrying the focused row's caption **[C]**.
- Art resolved through the stem index: `FE/PACK/GLOBAL/CHEATART.PAK` (`pict`), `CHEATBOX.PAK` (`1box`), `FE/IGTMAN/XBLAK.FSH` +
  `FE/PACK/IGTMAN/XBLAK.PAK` (`blkx`), `FE/GLOBAL/BLUR.FSH`, nvicons (`tic1`/`tic5`) **[C]**.
- Not implemented: the 'yes' confirm target (objecttype 6, releasedshape=`tic1` hilited=`tic5`) — its payload edge is not statically
  recoverable; clicking it is a documented no-op in the port.

## 9. Round 2026-11-07d — World-Cup history boards feflow90/91 (hub wedge fe33) + igoptns captions lane [LANDED]

- **feflow90** (`FeFlow::openHistory/drawHistory`, hub `fe33` Enter; [C] CLASSICS_FE33.md: SITE 0x449dc5, key loop FUN_00468728):
  interactive year cursor over the 15 finals rows of the pass-13 dataset (original list length DAT_0057c970=9; 'ysla'/'yslc' = -1,
  'ysrb' = +1 bounded). Finalist pair names painted from FCDB fullName (classic-era ids 861..876 + modern pool ids), scores as text,
  shields/flags composited from `FE/GLOBAL/F0NN.FSH` by record index over the tmfg emblem. Measured: F040 and F041 decode to identical PNGs
  = original-data duplicate flag art for Uruguay/Argentina 1930; rendered faithfully (no invented art). Enter = 'fe91' chain
  (FUN_004857a8 stores the selected index -> DAT_0062fab8, consumed by feflow91) opens **feflow91**; Esc/'fe06' exits both to the hub.
- Gates `fe_history_select` (--fe-history 0), `fe_history_select_1994` (row 14 = last dataset row; also proves clamp at 15 > 9 original rows),
  `fe_nav_history` (--fe-nav-history 5: bounded cursor + Enter chain, evidence line FEFLOW91). Captures viewed: `/tmp/wc98port-out/hist90_fe00.png`,
  `histnav_fe00.png` (Brazil/Sweden 1958 shields render from their own F0NN files; stylized original textures, not literal FIFA flags).
- **igoptns captions lane** (`openIgOptns/drawIgOptns`, `--fe-igoptns a|g|m`): the trio ships inside LAYOUTS.BIG (measured; IGLAY.BIG has no such
  members). ROWS tuples parsed with the FEFLOW15 grammar; list windowed to its box (ROWHEIGHT=36, 5 slots + scroll); tab captions = widget TEXTID
  bank ids (1174/1175/1144) trimmed into the button boxes — on this media those ids are FC-bank dev strings ("Moves highlight…", "Music Licensing:…")
  and the original release shows exactly the same text [C] IGOPTNS_ART.md. Audio popup widget names measured `1`,`2`,`3`,`04` (no go_* set).
  ok/canc/defa `igicon.fsh#chck` art PROVEN ABSENT -> documented legit-missing slots, painted as text captions only; no placeholder art invented.
- Gates `fe_igoptns_{a,g,m}` PASS "IG[AGM]OPTNS: shapes=[1-9][0-9]* texts=[1-9]"; captures `/tmp/wc98port-out/ig{a,g,m}_fe00.png` region-clean
  (rows legible, pills clipped, help pane strip below the tab row). Suite: full ctest green incl. these gates.

## 10. Round 2026-11-08 — Pass-13 completion: grids, LOAD/SAVE lists, camera/practise/control pages [LANDED]

- **FEFLOW43 management grid** (standalone board; gate `fe_mgmt_grid43`, evidence `FEFLOW43: shapes=21 texts=91`): composite iNNn button chain + back collage via `FE/PACK/FEFLOW43`; FCDB squad rows (`#shirt name`) on vNNN slots, FC-bank position labels under the columns (GK 810 / SW 1801 / RB 1531 / LB 979 / RM 1534 / LM 982 / RF 1533 / LF 981 / MF 1070 / FW 751), header captions FORM/SUB/STRAT/CUST + AGG/POSIT/KICK/ATT.BIAS (FC 1379–1382) and CANCEL/SAVE/RESET TEAM/RESET ALL, aNNN card icons resolved from the `FE/PACK/FEFLOW73` PAK (`but1..but3` selection icons only — the `redc`/`yell` card columns are match state and stay blank, original flow unrecoverable); UP/DOWN pans the four-row window with sbar thumb, L/R cycles team, Enter = `saveTournament`.
- **feflow41 LOAD / feflow42 SAVE-confirm** (registry #29/#30 → impl): `lstb` rows scan real `SLOT%02d.SAV` files from the XDG slot dir (`defaultSlotDir()`), captions through the decode path (`SLOT00 m1 g16 k16`), sbbg/sbar page slider + thumb; Enter on 42 writes the focused slot via `saveTournament` and flips cros(idle, crs1) → tick (nvicons tic1). Gates `fe_loadsave41`, `fe_savemgr42`; deterministic fixtures seeded under scratch `XDG_DATA_HOME` (never the real slot dir).
- **IGCAMERA/IGCAMSEL** (`IgBoards` kinds 16/17, pause 'C' chain): FC#353 CAMERA title + 5 key-assignment rows (FC 1468/1011/1545/1546/1104) with binding strings, flg/nmb pair cursor, forw/menu nav; IGCAMSEL = FC#379 'Camera Select' + 7 camera names row grid, Enter applies a `MatchCamera` mode (evidence "igcam: camera sideline"). Gate `fe_camera_pages`.
- **IGPRAC1/IGPRACT1/IGPRACT2** (kinds 18–20, pause 'P' page cycle over the live frame): FC#1249 'Number of Players' stepper via INGAME.FSH bt4n/bt5n + numPlayers readout, User/CPU Keeper rows FC 1920/374 with On/Off 1267/1263; RIGHT tabs PRACT1→PRACT2, LEFT wraps, Enter flips keeper, Esc opens the IGPRAC1 toggle board. Gate `fe_practise_pages`.
- **CONTROL1** (FE popup lane from the FEFLOW15 options chain): composite grv*/ls*/*rs* bar art + npll/nplr nav pair, FC#258 'Attacking' ctx caption, four Gravis percent bars seeded from the exe scene-default table DAT_00632778 {80,80,80,80,65,80,1} with gold fill quads, live gamepad LEFTX axis readout when a pad is open (SDL3 `SDL_GetGamepadFromID`; fallback 'kbd'), tmtl/tmtr FC#4594 captions. Gate `fe_control_page`.
- **INGAME.LAY frame = documented boundary** (§2b row 172): the LAY carries only invisible null.fsh placeholders (raw shapes=0 legit); the port draws its live HUD frame from INGAME.FSH chrome in `app/livematch.cpp` — no composite expected.
- Suite: full `ctest -j4` on build/release = **111/111 passed** (baseline 108 + gates `fe_mgmt_grid43`, `fe_loadsave41`, `fe_savemgr42`, `fe_camera_pages`, `fe_practise_pages`, `fe_control_page`). Captures viewed legible: mgmt43/load41/save42/camera/practise/control `_fe00.png`.

## 11. Round 2026-11-17 — pass-20uitick: FE init/tick grammar decode (docs-only) [LANDED]

- Ghidra decode of every "decode pending" handler in `FE_MENU_SCREENS_WC98.md` §5/§7/§10 + rank-1/3/4 blockers: feflow09 tick 0x44c6c0, feflow73/80/82 tick 0x45c800/0x463098/0x465b0c, feflow13 init 0x44e440, feflow15 tick 0x44f804, feflow18 init 0x452328, feflow19 init 0x452688, feflow20 init/tick 0x452b00/0x452f34, feflow21 init/tick 0x453f58/0x454298, feflow23 init/tick 0x4543dc/0x4545fc, feflow24 init/tick 0x454e0c/0x45502c. Dossier `analysis/pass20uitick/uitick_dec{1,2}.txt` + `NOTES.md`; script `GhidraWc98UiTick1.py`.
- Key recovered facts: league-list visible-row count = min(9, DAT_006316ac+DAT_0057e7d0+DAT_006316b4) with pn00..pn08 show / pln0..plsn4 hide template loops (FUN_0048c350 + FUN_00418e34); feflow82 roster-slot scan DAT_00607c3a (stride 2, i<0x50) == DAT_0060b5b8 → DAT_0057d120; `rand` 99-step counter → FUN_004cf040; `stg1`..`stg3` captions DAT_0057d79c; feflow20 tick scene-edge push FUN_004461b4 gated DAT_0062fad0+0x5060.
- Port diff: feflow09/15/18/23/24 grammars match the implemented state machine (`app/fe_flow.cpp`) — no code change; feflow13/19/20/21 interactive lane landed same day (§12).
- Docs-only pass; suite baseline 123/123 (`8499894`) unchanged.

## 12. Round 2026-11-17 — pass-20uitick-b: wc-family interactive lane [LANDED]

- **Scene-switch state machine** (`app/fe_flow.cpp` `wcStack_` + `openWcCfg`/`drawWcFamily`): the NOTES §4 token→scene edge tables from `analysis/pass20uitick` wired 1:1 — feflow18 init edges (fe19/fe11/fe15/fe41/fe42/fe89, quit→feflow37), feflow19 (fe20/fe18 + staged `rand` counter cap 99 → auto-advance feflow20), feflow20 (frwd→feflow13, stsc→82, strk→81, simu→86 confirm, fe19→19), feflow21 (frwd→feflow20), feflow23/24 (frwd→20, strk/stsc row-cursor→81/82); `Esc` pops the decoded parent chain via `wcStack_` (quit-edge analogue), `fe06`/`fe15`/`fe41` clear the chain into hub/options/save-load.
- **Content overlays** painted from `TourneyModel` through the existing composite: feflow19 `g{g}t{s}` group rows + group-name labels; feflow20 `tgtN/tnsN/tsdN` schedule rows + `tgp1` headline; feflow21 `tm0N` "a v b" pair strings + `qtmN` SF names with shield flags over `qf0N` (flags-first, names-last layering per 23/24); feflow81 `mult` result rows ("a vs b -> winner"); feflow82 `t00N` standings rows (P/W/D/L/PTS).
- **Gates**: `fe_wc_cfg` foreach extended 4→9 screens (18 19 20 21 23 24 35 81 82); all pass with `FEFLOW{N}: shapes=.. texts=..`. Captures viewed legible: `wcfg19/20/21/81/82_fe00.png`.
- **Scripted nav gate** (`app/selftest.cpp` step `fe-wc-nav`): public-API chain asserts 17 decoded edges — 18 Enter = staged `rand` redraw stays on the board; 19 Enter fires the fe20 edge in ONE press (the staged-draw counter is pointer-lane only); Esc pops the chain (parent pop, empty stack closes); 21 ring-0 frwd→20; 20 ring-1 simu opens the FEFLOW86 prompt and its confirm closes without an app exit; a return edge (20→19→20) pops the stack so the chain stays bounded. `fe-nav` unchanged.
- **Interaction fixes from the nav gate**: Enter dispatches by focus ring (`kBtn` rows {frwd,simu,stsc,strk}; 23/24 = {strk,stsc}); `back()` moves the `wcCfg_` branch AHEAD of the `onHub()` guard — Esc on a family board pops the chain (was: the hub guard swallowed every Esc); `openWcCfg` pops on return edges (target == stack top), cyclic 18<->19<->20 stays bounded; FEFLOW86 opens with `quitContext=false` — its "simulate remaining fixtures" confirm no longer exits the app (the presentation `TourneyModel` is a fully resolved bracket, so the boards repaint on dismiss).
- **FEFLOW20 legibility**: `tsdN` "W:" text switched to white on the LAY's navy fill plates (navy-on-navy ink was invisible); `tody` plate labeled "Today's Games" [H]; the date/time/stadium runtime slots (dtnm/dtno/tgt) stay plate-only — the port has no fixture schedule data [H].
- Suite: 128 tests, **126/128** with the working tree's uncommitted simulation debug instrumentation (live `RU`/`D` probe prints in `simulation/src/ball.cpp` + `tests/unit/test_sim_scenarios.cpp`) present; the two failures (`test_sim_scenarios`, `self_test_gameplay` passing/animation steps) are that instrumentation's lane, not this pass. Clean-tree verification of this pass = **128/128** in its own worktree (full from-scratch build + `ctest -j8` on HEAD + UI diff).

## 13. Round 2026-11-17 — pass-20cfg: FEFLOW32/33 pool-entry visual parity [LANDED]

- **Baked name plate [C]**: the grey gradient bar behind the team name on FEFLOW07/32/33 is NOT a widget and not code-drawn — it is baked into the scene collage art (`FE/PACK/FEFLOW32/FEFLOW32.PAK` → `feflow32.fsh#back`, 640×480, plate at (97,200) 164×16; FEFLOW07 verified by extraction: `FE/PACK/FEFLOW07/FEFLOW07.PAK` → `feflow07.fsh#back`, same white→dark gradient strip at (82,179) 164×16 for `ltnm` and (340,179) 164×16 for `rtnm`; FEFLOW32 has no right-side plate at (340,200) — single-side design). `ltnm`/`rtnm` widgets carry `fname=null.fsh shapeID=null` (FIFAWC art has no plate shape: `F009.FSH` = single shape `f000` 125×120 shield) and `textid=0`: `FeText::label` returns empty for `idx<=0`, so composite paints nothing for them — the `cfg32Visible_`/`cfg33Visible_` whitelist exclusion is hygiene, not a visual fix (the visible band is the baked plate itself, present from the first capture); the name is an overlay centred on the plate.
- **Name centre grammar [C]**: `ltnm textXOffset=82, alignment=3` = centre of the 164-wide plate (97+82=179). Port paints via `layoutTextCentered` at `o->x + width/2`, `o->y + height/2` in `drawSetup07`/`drawCfg32` `drawSide` (face 4, 14 px, rgb 235).
- **Row captions**: single centre-column set per screen, FC-bank indices 1691/1330/1741/573/1278 (Shooting/Passing/Speed/Defence/Overall). FEFLOW33 (two bars): one set centred at x=300 between the groups (FEFLOW07 grammar); the defect fixed here was the unconditional x=284 group painting a second set 16px left of the x=300 set (glyph smear), now gated `if (!twoAxis)`. FEFLOW32 (single group at x=394): captions left of the group at x=284.
- **Bar fill art**: `lbNN/rbNN` widgets carry `shapeid` (`sbarsr.fsh` bar0); fill = shape UV-clipped to `frac` (same as FEFLOW07 `paintBars`), dark track quad under it; flat green fill only when art is absent.
- **Whitelists**: `cfg32Visible_`/`cfg33Visible_` exclude `ltnm`/`rtnm` (overlay lane owns them); soup widgets (`objectname=null textid=429..433`) stay composite-hidden (null names are not whitelist entries).
- Gates `fe_cfg32`, `fe_cfg33`, `fe_cfg32_nav`, `fe_cfg33_side` pass; captures `/tmp/wc98port-out/cfg32c_fe00.png`, `cfg33e_fe00.png`, `s07c_fe00.png` viewed: names centred on baked plates, shields `f009.fsh#f000` + pool flag files, bar0-art fills, single caption set, yellow side ring. Clean-worktree verification (`fd80e1a` + this UI diff, full build + `ctest -j8`): **142/142 passed**.

## 14. Round 2026-11-17 — pass-21igscn: IGOPTN2 in-game options board (rank-1 cutover) [LANDED]

- **Retail scene decode [C]** (`analysis/pass21igscn/ighandlers.txt`, `analysis/pass21igmenu/ig2000.txt`, scripts `GhidraWc98IgScn{1..6}.py` + `GhidraWc98Ig2000.py`): the in-game options scene is the IGLAY.BIG `IGOPTN2` layout (17 widgets), not the LAYOUTS.BIG `igaoptns/iggoptns/igmoptns` popups the port used. Handler cluster + list painter FUN_0043c9a0: ROWS label ids are s16 at (rows+0x40)+idx*2; ids >=0 ride the widget-id remap table DAT_005774a4 into the FC bank (FUN_004c31d0), ids <0 take the dynamic-string lane FUN_0047a8b4. The trio's own ids 2000/2001/2002/2026..2033 are 0xffffffff remap sentinels — separate caption lane [H], gated byte-exact by `test_fcwidget` (full 4096-dword table dump == `fcwidget_tables.inc`, key rows Main Menu=1022, Match Select=1039, Team Management=1833, Options=1273, OK=1256, Home or Away=891).
- **Board port** (`IgBoards` kind 21 = `open("igoptn2")`, `presentation::paintIgOptns2` in `ig_optns.cpp`): composite whitelist `bpic`/`subt`/`tab2-4` strips + active header pane (`matH`/`gamH`/`conH`/`audH` by tab) + `help` pane + `back`/`fwrd`; `lst1..lst4` = the four tab panes (Match/Game play/controller/audio) parsed from the widgets' `ROWS` FC-id tuples, per-tab row+value state (`igRowsTab_`/`igValsTab_`); value captions cycle over the tuple's id list; help pane = the active `tbb`'s `help=` caption widget ids 78/81/1299/82 → remap FC 1037/798/486/267 "Match Options"/"Gameplay Options"/"Controller Options"/"Audio/Visual Options"; Language row initial value = the FC bank language (row tuple 92,2,8 → {93 Dutch, 94 English, 95 French, 96 German, 97 Italian, 4398 Dansk, 99 Spanish, 100 Swedish}). Legacy trio popups = kinds 22/23/24 (FE-shell chain lane).
- **livematch cutover**: pause-menu Options row (`igSel==4`) and 'O' open the board; keys 1-4 = `tbb1..4` tab hot-rects; UP/DOWN row cursor, LEFT/RIGHT value cycle; Esc -> IGMENU, Esc resumes. Gate script 10 drives the chain (`--ig-optns-smoke 356`).
- **Gate** `fe_igoptn2_board` PASS "IGOPTN2: shapes=27 texts=185" + per-tab/per-cycle evidence lines ("IGOPTN2: tab=N rows=K val=V", "IGOPTN2: cycle tab=T row=R val=V") + per-tab PNGs `_igoptn2_t{0..3}.png`, `_igoptn2_cyc.png`; captures viewed legible: `subt` "Options" (widget id 5 → remap FC 1273), help pane "Match Options"/"Audio/Visual Options", Match pane 11 rows (Half Length 2 mins / Language English / Auto Replay On / Clock Continuous / Time Display / Score Display / Visual Indicators / Radar / Player Numbers / Pitch Condition Hot) with slider art at the value index + back/fwrd icons; audio pane 12 rows (Field Detail…Menu SFX On, Screen Size caption-only, volume sliders). FAIL patterns pin empty panes and a no-op value cycle.
- Suite: 144 tests; the 84 `fe_*` gates all pass. The two failures (`test_sim_scenarios` spin-net-surface, `self_test_gameplay` passing/animation) are the working tree's uncommitted simulation-lane instrumentation (§12 note), not this pass; clean-tree CI verification of this commit runs the full suite.
- **Composite textid lane cutover [C]** (`FeText::label` in `fe_screen.cpp`): LAY `textid` is a widget id resolved through the remap table `DAT_005774a4` (`FUN_0043c9a0` → `FUN_004c31d0`), NOT a raw FC bank index — the earlier raw-FC lane rendered `subt` as "Cancel Changes?" (raw FC#5) and the help pane as "1 v 4 = A" (raw FC#78); all composite static labels now ride `widgetStringAt` (sentinel 0xffffffff → empty; ids past table bound 5101 read zero-filled DGROUP → empty). `ttxt` static initial `textid=429` → remap FC 1691 "Shooting" (LAY initial; **resolved pass-22fe15b**: TABSW `FUN_004516c0` rewrites ttxt TEXTID per TAB through `FUN_00478834` from the DGROUP baked constants at `0x57cd24` = 78/81/1299/82 = tbb1..4 help ids — header pane = tab title caption "Match Options"/"Gameplay Options"/"Controller Options"/"Audio/Visual Options", never the design id; ported `applyOptionsHeader`, gate selftest 14b3 `hdr0/hdr1/hdr3`).

## 15. Round 2026-11-17 — pass-22fe15c: FEFLOW15 options-default cluster decode + Language row initial value [LANDED]

- **Default-path record bytes [C]** (`analysis/pass22fe15/optsdec.txt`, `optsdec2.txt`, script `GhidraWc98OptD1.py`): scene-OPEN default writer `FUN_0046ae8c` (DGROUP record base `0x60cba8`, magic "FIFA" at +0x00/+0x04) calls `FUN_0046aee4` (block bytes `{+0x10..+0x13 = 0,0,1,50; +0x16..+0x1b = 0,0,0,1,1,0}`), `FUN_0046af38` (game-speed byte +0x17 = 10 when `FUN_0046a984` mode == 0; player-detail slider bytes `0x50,0,0x5a` = 80/0/90), `FUN_0046afb4` (half/clock/weather group: +0x21 = 1, +0x22 = 1, +0x24/+0x25 = 0, clock dword bytes `{mode?,1,1,5}`), then `FUN_0046b020` loop (`while EDX < 8`). The row↔byte mapping itself lives in the scene-init row loop (runtime DGROUP base) and stays statically unrecovered [H]; the port's displayed defaults ride value-list index 0 and match these bytes (Half Length "2 mins", Clock "Continuous", Pitch Condition "Hot", Camera "Tele").
- **Language row initial value [C]**: lst1 tuple `,92,2,8, 93,94,95,96,97,4398,99,100` = Dutch,English,French,German,Italian,Dansk,Spanish,Swedish; the exe opens the row at the current-bank index `DAT_0060cbcb = FUN_00439330` (bank set = `COMMON/FC*.BIN`, 8 codes in the value-list order, JAP outside the row [H]). Port: `openOptions` initialises the label-92 row to the active bank's index (`{dut,eng,fre,ger,ita,dan,spa,swe}` → eng ⇒ 1 = "English"); new public accessor `FeFlow::optionValueIdx(tab,row)`.
- **Gate**: selftest step `fe-options` asserts `optionValueIdx(0,1) == 1` + value caption "English" (`lang=1`); `--fe-options` capture `/tmp/wc98port-out/optlang_fe00.png` viewed: 11 Match-Options rows legible, Language = English. Full selftest 22/24 (the 2 FAILs = in-flight simulation seam lane, §12 note).

## 16. Round 2026-11-17 — pass-22fe15d: Language-row arrow switch (event 1017 bank reload) [LANDED]

- **Event 1017 `0x3f9` branch [C]** (`analysis/pass22fe15/dec2.txt` FE15 event dispatch): writer `FUN_0046b074` writes the language byte (record dword `0x60cbc8` top byte = `0x60cbcb`); on `ram0x0060cbc8 >> 0x18 != _DAT_00608cf4` the exe hides the scene window object (vtable slot `0x24`), CONCATs the new bank id into the byte, `FUN_0047ab1c` reloads `common\%s` (localized `FC<lang>.BIN` string bank + FEMSG), `FUN_0047ac14` refreshes the name tables, vtable slot `0x20` redraws, `DAT_0057cd75 = 0xff` invalidates the cached header id. Event 3000 (`<0xbb9` branch) and the `'fwrd'` tag repeat the same writer+compare+reload before the scene pop; `'fwrd'` runs `FUN_0046b0fc` only when the byte changed (language-dependent writer).
- **Port**: `FeFlow::setLanguage(code)` rebuilds the `FeText` bank layer + reloads the localized FCDB tables (`loadTeams` now reads `COMMON/FCDB_<LANG>.DBI` with English fallback, mirroring the exe name-table reload); `adjustValue` on label-92 rows (FEFLOW15 lst1 + IGOPTN2 board tuple 92) maps the cycled index through `{dut,eng,fre,ger,ita,dan,spa,swe}` and applies the switch immediately (the arrow posts 1017, not deferred 'save'). `setLanguage` re-arms every label-92 row's value index to the new bank id so the value caption resolves through the new bank's ids 93..100. New capture lane `--fe-options-lang CODE`.
- **Gate**: selftest step `fe-options` extends with `capEng0`/`langFre`/`capFr`/`valFr`/`langEng` (row cursor -> label-92 row, adjust -> `language()=="fre"` + row captions change out of English, adjust back -> `eng` + "Half Length" restored) — PASS (`langSwitch=1 langBack=1`). Capture `analysis/pass22fe15d/fe15fre_fe00.png` (`--fe-options-lang fre`) viewed: header "Options du match", rows `Durée périodes / Langue / Vidèzo / Montre / Chronomètre / Affichage score / Indicateurs visuels / Radar / Numéro joueurs / État du terrain / Caméra`, Language value "Français", values `2 min./Avec/Continue/Idéal/TV` — full-screen French repaint. `fe_flow_options` + `test_fcwidget` ctest green.

## 17. Round 2026-11-17 — pass-23fe87: FEFLOW87 penalty-shootout controller board [LANDED]

- **Scene**: registry row 48 (`feflow87`, slots init 0x467970 / tick 0x467ab0 / draw 0x467ab8). Handler
  cluster + event dispatch decoded in `analysis/pass23fe87/fe87_dec1..4.txt` (Ghidra scripts
  `GhidraWc98Fe87*`): the tag compares on the window-object +8 (`'fe06'`/`'quit'`/`'fe08'`)
  drive cursor reset + scene push chain; 0x66+`'fe15'` = options jump (`FUN_00451964`).
  **[C pass-45 pump-painter] the installed tick is a thunk — `0x467ab0 JMP FUN_0044c6c0`
  (`0x44e578` likewise) — the device-slot pump: it installs `FUN_0044c958` (key-repeat poll,
  `DAT_0060194c` compare → `FUN_00432d38`) at `[sceneWindow+0x14]`, zeroes the 16-dword group
  `DAT_00608748..0x608784` (4 rows × {slot, …}) every tick, sets `DAT_0060a2e4`, clears the text
  latch `DAT_00600db8/dc8`, then runs the row painter `FUN_0044ccd4` (register-callback
  `EDX=FUN_0044c9fc` = `DAT_00608768[row]=value` + repaint; `FUN_0044d128`/`FUN_0044ca1c` =
  clear-row + repaint) and the `ovrN`/`arwN` hide pass. Painter grammar per row `i` (slot word
  `DAT_00608768+i*4`): slot==0 → `anmN` anim loop flag on (`+0x46=1`), `arwN` pair-arrow SHOWN,
  `grvN` value text HIDDEN, panes HIDDEN; slot≠0 → hide `anmN`/`arwN`, show the `lrp`/`rrp`
  repeat-arrow art (frame word copied from the row handle), show `grvN` text (`FUN_0044ca48`).
  There is **no row cursor, no highlight ring, no device rail** — row state changes only through
  the click callbacks, so the fresh-open board is 4 keyboard rows + pair arrows, all slots 0 —
  which is what the port composite paints. The pass-23/24 "device-slot cursor ring + LEFT/RIGHT
  cycle" model stays retracted; evidence `analysis/pass45ctrl/` (`tick_disasm.txt`, `pump.txt`,
  `decomp.txt`, `subdecomp*.txt`).
- **Board model**: four slot rows = `ovrN` overlay + `alpN`/`arpN` colour panes + `anmN` controller
  picture (`fname=contr.fsh`, `vpath=..\global\` → the CONTR.FSH keyboard frame (frame 1): the
  pump's default state all-slots-0 paints exactly this row art with `arwN` shown [C 45]) + `arwN`
  pair-arrow +
  `ctxN`/`grvN` runtime text slots (FC-remap ids, excluded from static paint). `ltfg`/`rtfg` shield
  slots from the pool entry art (`f00N.fsh#f000`), `tltx`/`trtx` team captions (textid=0 → label
  empty), `titl` title = ENGL87.PAK#engl via ScreenArt lang retry ("Penalty Shootout / Controller
  Select" collage verified in capture), `tmtl`/`tmtr` = FC-remap 4594 "Teammates:" labels centred on
  their boxes; `npll` (textid=0) + `nplr` (textid=699 static slot default "Number of Players")
  excluded as runtime text slots — drawing 699 double-paints over the tmtr box (x=340 vs x=485
  spill), the handler replaces slot text through the `FUN_004c31d0` draw lane at open [H].
- **animseq**: composite animseq player (objecttype=3 `FRAMES`/`TICKSPERFRAME` ranges) landed this
  pass (`presentation/src/fe_screen.cpp`); the pump's default state keeps the composite at its
  baked frame (all four rows show the keyboard picture); the exe side animates a row only after
  the click callback sets `DAT_00608768[row]≠0` [C 45].
- **Edges**: `enter()` on feflow33 = `'fe87'` wedge → `openCfg87()`; Esc pops to feflow33; rail
  widgets quit/fe06/fe15/fe07/fe08 per the event compares [C fe87_dec1].
- **Gates**: selftest step `fe-cfg87` (name/draw shapes, static frame: navigation cannot alter the
  board, stay-on-board, Esc pop to 33, hub fallback, FC-caption remap pair "Teammates:") PASS;
  capture lanes `--fe-cfg87` / `--fe-cfg87-dev` (bound-row lane since §36 pass-uidev)
  (`cfg87static_fe00.png` viewed: 4 keyboard rows + arrows, no cursor ring, no joystick tile,
  shields, clean caption row). `fe-cfg87` caption probe via `FeFlow::cfg87Caption` (FC-remap lane
  of `FeText::label`).
- **Suite**: ctest 143/145 green this lane (`test_sim_scenarios`, `self_test_gameplay` = the
  in-flight uncommitted simulation kick-seam lane, unchanged by this pass); selftest 23/25 steps
  (same two gameplay steps). Registry: row 48 + FEFLOW87.LAY table → impl; §4 item 2 updated.

## 18. Round 2026-11-17 — pass-24wc13: FEFLOW13 world-cup controller board [LANDED]

- **Scene [C]**: registry row 8 (`feflow13`, slots init `0x44e440` / tick `0x44e578` (`JMP FUN_0044c6c0`, the device-slot pump [C pass-45]) / draw thunk→`FUN_0044a210` / event `-`). LAY header comment `world cup controller select screen`; widget list byte-identical to FEFLOW87 (member text diff-verified), plate art `feflow13.fsh` from the scene PAK. Same pump-painted default state as FEFLOW87 (§17 pass-45 block).
- **Init token tree `FUN_0044e440` [C]** (`analysis/pass24wc13/fe13_asm.txt`, full-body disassembly 0x44e440..0x44e574; Watcom token-compare binary tree, 0x6665xxxx constants = search bounds not tokens): equality leaves `'fe06'` 0x44e466→0x44e501, `'quit'` 0x44e4d6→0x44e4de, `'fe15'` 0x44e483→0x44e563, `'fe08'` ZF-leaf 0x44e472→0x44e524, `'fe07'` redraw-only. Each exit arm runs `FUN_00459410` (cursor reset) with a popup tuple + `FUN_004461b4` scene push:
  - `'quit'` tuple `(0x3e9, 0x82e, 0x10f1)` sites 0x44e4de-0x44e4ef, push `NAME:feflow37` site 0x44e4f9 → app-quit confirm (port `openPopup("feflow37", quitContext=true)`).
  - `'fe06'` tuple `(0x3ea, 0x82e, 0x10f3)` sites 0x44e501-0x44e512, push `NAME:feflow37` site 0x44e51c → World-Cup exit variant: confirm closes the family to the hub, no app exit (`popWcExit_`).
  - `'fe15'` → options jump `FUN_00451964` push 0x562a24 site 0x44e56d; init tail also writes 0x66+`'fe15'` (same options edge).
  - `'fe08'` forward: `FUN_00446624()==2` stay-guard (2-controller play keeps the board), else `FUN_0045a24c()` (device-count check): `<0` → `FUN_00448428` no edge; `>=0` → `FUN_00448418(0)` + `FUN_0045a198` + push `NAME:feflow56` (tournament launch transition, null-LAY scene).
  - Esc path = event branch `0x3ea` push `NAME:feflow06` site 0x44e4c4 (`analysis/pass12/edges_table.txt:113`) → straight to hub, **not** a `wcStack_` pop as on family boards.
- **Board port** (`app/fe_flow.cpp` `openWc13`/`drawWc13` + enter/mouse handlers): pump-default composite — 4 rows `ovrN` overlay + `alpN`/`arpN` colour panes + `anmN` CONTR.FSH controller picture (baked frame 1 = keyboard) + `arwN` arrows + `ctxN`/`grvN` runtime text slots excluded; `tmtl`/`tmtr` FC-remap 4594 "Teammates:" captions; `ltfg`/`rtfg` pool shields; `titl` plate. No cursor ring, no device rail — the pass-24 invented lanes are cut; the exe row-state lane (`DAT_00608768[row]` click callbacks) stays unwired [H] (wired by §36 pass-uidev). Open edge: feflow20 `'frwd'` [C SITE 0x452dc7] → `openWc13` (already wired via `wcStack_`).
  capture lanes `--fe-wc13` / `--fe-wc13-dev` (bound-row lane since §36 pass-uidev) (`wc13static_fe00.png` region-clean).
- Registry row 8 → **impl**; §4 item 4 updated; dossier `analysis/pass24wc13/NOTES.md` (+ `fe13_asm.txt`).

## 19. Round 2026-11-18 — pass-26igmenu: IGMENU decoded row table + CONTROL1 in-game board [LANDED]

- **Row grammar [C]** (tick `FUN_0043fc40` + init `FUN_0043f040`, `analysis/pass25igmenu/igmenu_dec1..4`
  objdump slices): mode-class select `DAT_00640fb4` (+2p `DAT_00641274&0x10`) installs one of eight
  runtime row tables (VA 0x57c7d8..0x57c904). **pass-26c correction**: the slots are
  initialized FILE data — data-segment delta `VA = raw + 0x3FEA00`; the earlier zero-scan
  read the wrong delta (0x400000). Byte-exact dumps `analysis/pass26igmenu/NOTES.md`:
  friendly class {0,9} 1p 0x57c7d8 (10 rows, raw 0x17add8) / 2p 0x57c802 (8 rows
  Continue-set); cup qualifiers (modes [2,8], range check at mgmt dispatch 0x43f669) 1p
  **0x57c832 (11 rows, raw 0x17ae32)** / 2p 0x57c860 (9 rows); tied-draw 0x57c824 (3 rows:
  Extra-Time/22, Penalty Shootout/21, End Match/20 — friendly-class-only path 0x43f27c);
  PK 0x57c886/898/be; mode-0xc 0x57c8e0/902.
  Row stride 4: `{u16 textid, u16 code}`; window `[win+0x3c]`=count, `[win+0x40]`=tid array,
  `[win+0x44]`=disable mask. Disable rules: code 1 (Instant Replay) when `FUN_00494fc0()` highlight
  list is empty (cite corrected: 0x494fc0, not 0x495bc0); code 9 (Restart) when
  `FUN_0043a540 != FUN_00432af4` (phase mismatch). Caption ids resolve through the FC remap
  `DAT_005774a4` (`FeText::widgetStringAt`).
- **Geometry [C]**: centering into box2 (116,131,408x231): win.x=(408-286)/2+116=177,
  win.y=(231-count*18)/2+131 (`[win+0x12]=count*0x12`); LAY `menu` x=176 y=138 is stale template.
  Boxes box1/2/3 objecttype 43, no fillcolour -> frame-only stroke quads ported; score plate
  box4/box5 RECT + nmb1/nmb2 `%d` pair (`DAT_0064024c/250`, `%d`@0x562148) paint in the menu
  header (follow-up below), not the HUD strip.
- **Dispatch [C]** handler 0x43f548 jump table `*0x440148(,%ecx,4)`: code->action map verified per
  table (resume 0x439138, stats 0x4409d0, tourney-end chain 0x441140, options 0x440dc0, mgmt hub
  0x441800, replay-entry chains 0x43f888/8c4/901, confirm msgs 0x56214c/0x562154/0x562160,
  controller-select scene, PK/replay sub-chains). Enter dispatch keyed by CODE, not index.
- **Port** (`app/livematch.cpp`): `kIgMenuRows {tid,code}` armed from the byte-exact cup-qualifier
  1p table 0x57c832 (Resume Match 5083/0, Instant Replay 249/1, Camera Select 4423/2, Team
  Management 1407/3, Controller Select 258/4, Options 5/5, Match Statistics 261/6, Restart Match
  268/9, Quit Match 264/10, Quit World Cup 267/11, Quit to Windows 4337/12) replaces
  the bt0n..bt5n INGAME.FSH strip (20x20 button icons — wrong art, wrong geometry); 18-px centred
  rows + HILITED pane quad; UP/DOWN skip mask-disabled rows; Enter dispatch by code; lane hotkeys
  'L' (CONTROL1 board), 'M' (igteam72 mgmt), 'O' (igoptn2), 'B' (hub) mirror the 2p-table rows the
  1p smoke lane cannot reach. CONTROL1 becomes `IgBoards` kind 25 (`kBoardName` 26 entries, draw
  `bk>=21 -> drawIgOptns` full-composite branch; Enter dismisses, 21..24 stay no-dismiss).
- **Gates**: `fe_igmenu` smoke 260 — Esc open, Down x5 (replay masked) -> Quit Match, Enter ->
  IGQUESHN, Esc cancel, Up x5 -> Continue, 'L' -> CONTROL1; PASS regex list
  `IGMENU: shapes/texts` + `CONTROL1: shapes/texts` + `IGQUESHN: texts`; captures
  `igmenu_{igmenu,control1,igquesn}.png` viewed (8 legible centred captions, pane, frame strokes,
  CONTROL1 device strips + "Teammates:"). Smoke 260 keeps >=1 s of unpaused sim after resume for
  the clock guard (200-frame lane paused too long). boardScript 4/5 cursor rows retargeted
  (Down x1 -> Instant Replay), 6/7 'M' mgmt hotkey, 10 'O' options hotkey.
- **Header overlay follow-up** (same day): init `FUN_0043f3e8` strings the box1 header — typ1/typ2
  team names, FNNN shields on flg1/flg2, nmb1/nmb2 score digits — now painted in the pause-menu
  draw (board-header idiom: names centred x+50 pw130, digits box4/box5 centre x+13 pw24). LAY order
  flg→box4/5 means the white RECT plate repaints over the shield art after composite, digits last.
  `kTitle[kind_]` lookup guarded `kind_ < 5`: CONTROL1 (kind 25) carries a titl textid=0 slot with
  no caption table entry (was an out-of-bounds read). Evidence: `IGMENU: shapes=17 texts=131`,
  `_igmenu.png` shows names + shields + white plates + digits over the row table.
- **pass-26c follow-up (same day)**: data-delta correction re-armed the menu to the byte-exact
  11-row cup table (see §19 row-grammar correction) — captions "Quit World Cup" (FC#267 →
  remapped "Quit World Cup") and "Quit to Windows" (4337) render in place; `igPause`/`igPauseSmoke`
  comments and hover/click coords retargeted (winY 147.5, row-6 centre 264.5). Evidence:
  `IGMENU: shapes=29 texts=183` (11 captions), mouse lane `row=6 code=6`, `CONTROL1: shapes=10
  texts=37`, `IGQUESHN: shapes=18 texts=71`; `_igmenu.png` shows all 11 rows + pane + header
  overlay. Registry IGMENU row-grammar table replaced the speculative {tid,code} list with the
  FILE-data tables; `FE_MENU_SCREENS_WC98.md` updated with the data-delta fact.
- **pass-27c follow-up (same day)**: re-arm correction — the port's live fixture is the
  single-player EXHIBITION bootstrap (`MatchBoot`), i.e. retail mode class {0,9}, so the init
  select at 0x43f2a2 installs the **friendly 1p table 0x57c7d8** (10 rows, codes
  {0,1,2,3,4,5,6,9,10,12}), NOT the cup-qualifier 0x57c832 table armed by pass-26c. The cup
  table's 267 "Quit World Cup" row is mode-class [2,8] only (its dispatch 0x43f548 case exits
  the cup run; port lane keeps code 11 as a documented no-op). Geometry moves with the count:
  winY = 131+(231-18*10)/2 = 156.5, row-6 centre 273.5 — mouse hover/press coordinates and the
  smoke script retargeted. Evidence: `IGMENU: shapes=29 texts=169` (10 captions), mouse lane
  `row=6 code=6`, `_igmenu_mouse.png` shows the pane on Match Statistics and no Quit World Cup
  row. Gate regex `IGQUESHN: texts=[1-9][0-9]*` (was `[1-9]`) to survive the 71-text repaint.
- **Suite**: 143/145 this lane (`test_sim_scenarios`, `self_test_gameplay` = in-flight simulation
  kick-seam lane, untouched). Registry rows IGMENU.LAY/CONTROL1.LAY updated; §4 rank-8 residual
  (runtime captions) closed for the 1p table; dossier `analysis/pass26igmenu/NOTES.md`.

## 20. Round 2026-11-18 — pass-27igmouse: IGMENU/IGQUESHN pointer lane wired [LANDED]

- **Retail semantics [H]**: the pause menu and confirm box take mouse input through the same
  window-object hit machinery as the FE screens — hover moves the cursor, a press activates.
  Original scene handlers do not branch on a mouse flag; the pointer arrives as widget events
  (`'gmnc'` row code / `'okay'`+`'canc'` tags = objecttype 40/39 button widgets in IGMENU.LAY).
- **Port** (`app/livematch.cpp`): SDL LMB + motion wired into the live-match loop. Pointer maps
  to the virtual 640x480 through `window.pixelSize()` (FE-lane idiom); hover on a menu row sets
  `igSel` on a press *edge* only (retail press semantics, not continuous hover); a press inside a
  row activates it through the shared `menuActivate` (code-keyed dispatch), inside `okay`/`canc`
  confirms/resumes the quit path. `prevLmb` edge tracking is frame-local (processed-event view).
  `maus.bmp` cursor sprite drawn at the pointer via `presentation::loadFeCursor` when the art is
  present. Menu rows outside the window rect and confirm presses in blank area are no-ops.
- **Gate determinism**: the window-created synthetic motion at startup parks the pointer at
  (320,455) for every smoke lane (frame 2), so no gate cursor drifts from a phantom hover;
  igPauseSmoke hovers Match Stats (250,219.5) -> press -> IGSTATS -> Esc -> L opens CONTROL1 ->
  Esc -> cursor Up to row 0 -> Down x5 -> Enter IGQUESHN -> press `okay` exits (exit code 0).
- **Gate**: `fe_igmenu` PASS regex adds `igmenu: mouse activate row=2 code=6`; capture
  `_igmenu_mouse.png` pixel-checked (pane quad row 2 = (134,138,66) vs row 0 unpaneled, cursor
  sprite pixel (225,234,230) near pointer). `ctest -R fe_` 85/85 green with the lane.
- **Suite**: 143/145 baseline unchanged (`test_sim_scenarios`, `self_test_gameplay` = in-flight
  simulation kick-seam lane, untouched). Dossier `analysis/pass27igmouse/NOTES.md`.

## 21. Round 2026-11-18 — pass-28audit: IG board parity audit + stroke/caption fixes [LANDED]

- **Audit lane** (`analysis/pass28audit/`): `ig_scan.py` diffs every IG board capture region
  (LAY-declared widget rects, ink-rect note: `pixelwidth` feeds the width for alignment-3
  labels) against the lane's no-board baseline (`preShotAt` → `<prefix>_pre.png`);
  region-identical + declared content = gap. 27 pairs.
- **First run flagged** `IGHILSAV:titl`, `IGPRACT1:ty12/ty13`, null-art box widgets
  (`IGQUESHN:box2`, `IGCAMSEL:box3`, `IGMGMT:box2/3`, `IGHILSAV:box2/3`, `IGHILITE:box6`,
  `IGOPTN2/4:box5`, `IGOPTNS:box6`, `IGPRACT2:box3`, `IGPRAC1:box4`) — all fixed.
- **Caption misreads corrected** (pass-21igscn lesson re-applied): the FC **remap** ids, not raw
  bank indices — `IGHILSAV titl` textid 303 = 'Save Replay', `IGPRACT1 ty12/ty13` = 761/432
  'Attack'/'Defence' (verified with the remap probe); both exclusions dropped, composite paints
  the labels. `IGPRACT2 typ8/typ9` (702/703 = 'User Keeper'/'CPU Keeper') was a scanner false
  positive: captions already paint centred at x=320 across `pixelwidth=390`; scanner rect now
  honours `pixelwidth` — 476/559 changed px confirmed in `prac_igpract2.png`.
- **Box outline idiom**: engine box widgets with `fname=null.fsh`/`shapeid=null` and no
  `fillcolour` draw a frame-only 1-px outline; port added `strokeBoxes()` (tint 150/150/150,
  alpha 200) after every `composite()` in the IG lanes + pause overlay — IGMENU's manual stroke
  trio replaced by the helper.
- **Result**: `ig_scan TOTAL flagged widgets: 0` (`/tmp/wc98port-a2`, 46 captures). Suite
  143/145 (2 FAIL = in-flight simulation kick-seam lane, untouched). Dossier
  `analysis/pass28audit/NOTES.md`.

## 22. Round 2026-11-18 — pass-29repl57: FEFLOW57 replay browser + cite verification [LANDED]

- **Hub wedge `repl`**: verified leaf region 0x4491cf–0x4491f8 via objdump binary slice
  (`--adjust-vma=0x400000`): `cmp $0x7265706c,'repl'` at 0x4491d5 → match path sets
  `DAT_0057ca48 = 1` (0x4491ea), loads string 0x5626a0 = `"eras12.ffn"`, calls 0x4455b4.
  The earlier documented "scene push `feflow57`, SITE 0x449df0" was **refuted**: no code site
  pushes the name — the four `feflow57` string occurrences (raw 0x160ca0 etc.) carry zero
  imm-pointer refs (runtime fixups), and the pointer-table entry at VA 0x574dec
  `{0x560ca0, 0x8095, 0x2303, 0x560cb4}` registers the name for scene lookup only.
  Browser content stays [C] per docs/asset-formats/RPLY.md (`FUN_0043c4f0`).
- **Scene content (port)**: `openReplayLoad()` paints FEFLOW57 composite (blur `bgnd`+
  `blur`, `fe06` crs1 close overlay, `lstb` rows, `sbar` thumb); rows = `rplyindx.dat`
  slot filenames whose `.dat` exists (tree `user/` when present, else XDG slot dir);
  caption widget `0000` id 322 → remap `DAT_005774a4[322]` = FC#999 "Load Reel" — the
  earlier "id322 leak" [H] note is resolved. Enter = `replayFile()` re-drive (sealed
  `stateHash` == re-drive hash, `match=1`); deterministic fixture seeded by
  MatchBoot+SimRunner(0x2E7A) scripted pad stream under scratch `XDG_DATA_HOME`.
- **Bug-fix note**: `replRows()` initially kept `std::span` into a `readFile()` temporary
  (dangling after statement end) — fixed to own `std::vector`; comment corrected the stale
  "2500-byte index file" to the verified 2600 (`RPLY.md`).
- **Gates**: `fe_replay57` (`--fe-replay-load 1`, evidence "FEFLOW57: shapes=3 texts=33",
  `_replay57.png`), `fe_replay57_enter` (re-drive parity). Hub wedge dropped from
  `kHubDisabled`; `MENU_PARITY.md` inventory + checklist + §4 rank-6 cite wording updated
  to the verified evidence.
- **Suite**: full ctest 145/147 (2 FAIL = `test_sim_scenarios`, `self_test_gameplay` =
  in-flight simulation kick-seam lane, untouched). Dossier `analysis/pass29repl/NOTES.md`.

## 23. Round 2026-11-18 — pass-31f15slider: FEFLOW15 option-row slider pills [LANDED]

- **Grammar**: `lst1..lst4` (type=17) rows paint the shared opslider family —
  `fname=opbutns.fsh`, `vpath=FE/PACK/GLOBAL` resolves via the global object cache
  to `FE/PACK/GLOBAL/OPBUTNS.PAK` (`btnn 12x12 btnh btnd dsbl unsl 185x17 sllf 92x17
  slrt 185x17 but0 but1`), same 9-tag family IGOPTN2 measures (pass-21igscn). Track
  `unsl` at `lstN.x + hotSpots 198` (absolute x 290), fill `sllf` UV-clipped to
  frac, knob `btnn` vertically centred, right-end tag absent ⇒ no-op draw.
  Engine paint order: track/fill/knob first, caption+value text on top — value
  renders over the pill (retail).
- **Port**: `FeFlow::drawOptions` row loop restructured (art always drawn; caption
  skip moved after art). mode-2 enum rows frac = index/(n-1); mode-1 volume rows
  (Music/Commentary/Game SFX/Menu SFX) frac = settings vol/100 via `audioSliderRow`;
  unmapped mode-1 rows (1183, 4175) neutral track only, no knob [C].
- **Gate/captures**: `fe_flow_options` shapes=31 (was 20); `--fe-options-tab 3`
  audio tab shapes=35 — pills under all 12 rows, fill+knob per value
  (`/tmp/wc98port-out/optssl_fe00.png`, `optsl3_fe00.png`). IGOPTN2 board lane
  untouched (`fe_igoptn2_board` PASS).
- **Suite**: ctest 145/147 (2 FAIL = simulation kick-seam lane, untouched).
  Dossier `analysis/pass31f15slider/NOTES.md` (+ dumps `sl1-3.txt`, `list17b.txt`).

## 24. Round 2026-11-18 — pass-32igmouse: keyboard-lane hover-pin fix [LANDED]

- **Regression** `fe_igstats`: keyboard lane Esc->Down->Enter opened nothing; cursor
  snapped back to row 0. The pointer lane re-pins `igSel` to the hovered row **every
  frame** while the menu is open (§20 wording corrected: the assignment is continuous
  hover, not press-edge — press-edge only gates activation; retail `FUN_0046c014`
  hover-move semantics are faithful). The pass-27 park coordinate (320,455) was
  remapped by the motion/poll scale lane into (223,175) — inside row 0 of the menu
  band (winX 177..463, winY 131+(231-18·count)/2) — so phantom hover erased keyboard
  moves before Enter. Window pixel size also differs between injection and poll frames
  (swapchain 1792x1344 → 3840x2160), so a single park push is not scale-stable.
- **Fix** (`app/livematch.cpp`): park coordinate moved to (600,50) — lands outside the
  band at every observed scale (x ≥ 463 or y < 174.5); and keyboard-only smoke lanes
  (`smokeFrames && !igPauseSmoke`) re-push the park motion every frame, so an OS
  synthetic motion arriving after frame 2 can't re-enter row 0 (push at loop-top lands
  last in the SDL queue, i.e. the frame's final pointer state is the park).
  `igPauseSmoke` mouse lane keeps its scripted hover/click sequence.
- **Rule**: smoke lanes mixing keyboard cursor moves with the hover lane must keep the
  pointer outside the interactive band **per frame**, at a scale-stable coordinate.
- **Suite**: ctest 145/147 (2 FAIL = simulation kick-seam lane, untouched);
  `fe_igstats` green, captures `igmenu2_igstats.png` (board 1:1) + `igmenu_control1.png`
  verified. Dossier `analysis/pass32igmouse/NOTES.md`.

## 25. Round 2026-11-18 — pass-33fe51: FEFLOW51 practise stadium board [LANDED]
- **Decode**: FE51 handler slots `init 0x459ff0 / tick 0x45a0fc / draw 0x45a110` forced as functions
  (`ghidra-scripts/GhidraWc98Fe51*.py`, dumps `analysis/pass33fe51/fe51_dec{5,6}.txt`). Event handler
  token leaves (bounds-vs-leaves rule applied): `fe06`/`quit` → FUN_00459410+FUN_004461b4 pop chain,
  `fe15` → FUN_0044b838+FUN_00451964 (options), `fe50` → back to FEFLOW50, `fe89` → FUN_004685bc
  (team mgmt), default → generic click FUN_0044b878. Tick = FUN_0044bc1c (shared stad08 serve block;
  new-scene flag `DAT_0057cc10` resets ring cursor `DAT_0057cb54 = 2`, mirrored to `DAT_0060cbc5`)
  + FUN_004593a4 value pump. Ring: `+1` wrap `9→0`; decrement wraps through 10 → 0..9 over the ten
  mpNN pins. `fe16` forward runs via generic dispatch into the match-start driver FUN_004520b4
  (mode-class `DAT_00640fb4` lanes, first-init flags `DAT_0057ce04..07`).
- **Port**: `stad51_` layout + `drawStad51` composite (France-map collage + mpNN pins with `hbal`
  cursor hilite + nslt/nsrt arrows + sint/wint/hint value icons + help-pane stadium name + animated
  flag lane), ring cursor `s51Sel_` `%10`, Esc = fe50 pop (restores hub only when no config owns it),
  Enter = 'fe16' practise launch.
- **Fix (hub wedge stuck)**: `onHub()` did not exclude `setupScreen_` — after `hubActivate("fe32")`
  opened the FEFLOW50 config, every next Enter re-entered `hubActivate` (always -1), so FEFLOW51 and
  the practise launch were unreachable interactively. `app/fe_flow.hpp:324` now tests `!setupScreen_`
  (fe07 friendly chain was unaffected: `openSetup07()` already clears `hubView_`).
- **Gate**: `fe_stad51` (hub fe32 wedge → FEFLOW50 confirm → FEFLOW51, pin ring ×3) PASS
  `FEFLOW51: shapes=[1-9][0-9]* texts=[1-9]`; capture `stad51_fe00.png` shows map + 10 ball pins +
  yellow `hbal` cursor frame + arrow pair + value icons + "Parc des Princes" help caption.
- **Suite**: all 88 `fe_*` gates green (incl. new `fe_stad51`); simulation lane untouched.
  Dossier `analysis/pass33fe51/NOTES.md`.

## 26. Round 2026-11-18 — pass-34fe94: credits wedge = FEFLOW94 (not 19) + CRED.BIN roll [LANDED]

- **Scene mapping [C]**: the hub `cred` wedge opens **feflow94**, registry line 54
  (`0x44809c`, name VA `0x5625b0`, handler slots init `0x46999c` / tick `0x469904` /
  draw `0x46986c`). The port's "FEFLOW19" mapping was a registry-index misread — feflow19
  is the tournament group board (pass-20uitick tick 0x452884 family); the slot-54 bytes are
  byte-exact `66 C7 44 24 5A 00 00` = index 94, so the scene name was never 19.
- **Art-pack absence [C]**: no `FE/PACK/FEFLOW94` directory, no `feflow94.lay` member in
  LAYOUTS.BIG, no `FEFLOW94.PAK` anywhere in the CD. FEFLOW94 renders with composite
  shapes=0 (art absent); the `fe_credits` gate therefore asserts the **roll contents**
  line, not shapes.
- **Roll contents [C]**: BigArchives root `CRED.BIN` cell stream — header `{u32 3, u32 1,
  u32 0x129f}` + packed cells: kind 1 caption = FC widget-id through the remap table
  (`DAT_005774a4`), kind 2 literal staff name (`len` NUL-included, byte-exact no padding),
  kind 3 blank row; 18-px grid, +2 px/tick draw math (scene draw slot `FUN_0046986c`, wrap
  `0x12`, end beat `DAT_0057d280 = 0x1b` armed by tick slot `FUN_00469904`). All four
  shipped variants (eng/de/it/en2) parse 305/305 rows.
- **Port**: header-only parser `formats/include/wc98/formats/credits.hpp` + `drawCred_` roll
  in `fe_flow.cpp` on the feflow18 composite screen (retail loops the roll; the port runs a
  single pass with Esc/Enter exit then hub return — `kCredRowFrames=36`,
  `kCredBlankPauseFrames=243`). Captions centred on the cr-lane (x=257 w=160 → 337), white,
  eras12 face.
- **Gate**: `fe_credits` asserts `FEFLOW94: roll rows=[1-9]` (was the stale `FEFLOW19:
  shapes=[1-9]`); capture `_cred.png` shows "Assistant Producer: Jeff Smith … /
  Executive Producer: Bruce McMillan …" lanes. FE subset `ctest -R "fe_|fcwidget"` 89/89
  green. Dossier `analysis/pass34fe94/NOTES.md` + `fe94_dec1/2.txt`; grammar doc
  `FE_MENU_SCREENS_WC98.md` §11.
## 27. Round 2026-11-18 - pass-35igtitl: IGMENU pause-menu title caption [LANDED]
- **Grammar [C]** init `FUN_0043f040` tail `0x43f170/0x43f314/0x43f341`: scene event state
  `DAT_00601934` -> `{1 Half-Time, 2 Full-Time, 3/4 Extra-Time, else End Match}`; state 0 /
  mode-class 0xa/0xc -> table-pointer check (`DAT_006024d0` vs PK-alt `0x57c886` ->
  Scenario Select, practice `0x57c902` -> Post Game Menu, else **Pause Menu** for the live
  tables). The id goes through `FUN_0x4c25d0` = language **string pointer table**
  `DAT_00585cb0` (dword array `[base+id*4]`, runtime-built — not the FC remap
  `DAT_005774a4`), then `FUN_00477cb4(win, EDX='titl', EBX=str)` installs the caption on
  widget `titl` (LAY fontid 1, alignment 3, pixelwidth 350). Gate `DAT_00601930 != 0`
  skips the install; byte-lane tail (`DAT_0057c920/921`, exe string `0x562144`) overrides
  widget `name` dynamically — resolved pass-39, see §29.
- **Bank ids [C]** verified against FCENG.BIN: 868 `Half-Time`, 776 `Full-Time`, 669
  `Extra-Time`, 632 `End Match`, 1601 `Scenario Select`, 1384 `Post Game Menu`, 1333
  `Pause Menu` (FC index == DAT_00585cb0 entry index).
- **Port**: `igTitleId(sim::MatchState)` in the pause-menu block (status `0x10` /
  `endWhistle==2` -> Full-Time; `endWhistle==1` -> Half-Time, `periodIndex>=2` ->
  Extra-Time; live play -> Pause Menu), painted white fontId 1 centred x=320 y=titl+8.
- **Gate**: `fe_igmenu` `IGMENU: shapes=29 texts=141` (+10 texts vs pre-titl); capture
  `t26_igmenu.png` white glyph run x 272..369 centred, y 32..40. IG-family subset
  `ctest -R "fe_ig*|fe_highlite|fe_hilsav|fe_mgmt|fe_camera|fe_practise|fe_manmarks|fe_control|fcwidget"`
  14/14 PASS. Dossier `analysis/pass35igtitl/NOTES.md`.

## 28. Round 2026-11-18 — pass-36igquesn: IGQUESHN confirm-box captions + Yes/No repaint [LANDED]

- **Retail grammar [C]** (`analysis/pass25igmenu` objdump slices, re-read this pass): the confirm
  painter `FUN_0043fb90` runs `FUN_0043f3e8` at entry (0x43fb96) — the box1 header overlay (typ1/typ2
  names, flg1/flg2 shields, box4/5 white RECT, nmb1/nmb2 `%d` digits) is painted on IGQUESHN exactly
  like on IGMENU; the port hoisted it into one shared `paintPauseHeader` lambda used by both lanes.
  `titl`/`line`/`lin2` captions come from the dispatch-chain confirm slots: chains 0x43f754
  (restart, code 9) / 0x43f7dd (quit-match, code 10) call `FUN_0043fb50` (slots 0x57c924/0x57c926 =
  `titl`/`line` widget ids) and install a painter whose 0x43fba1 fallback branch (when
  `DAT_0057c934 == 0`) draws from the slot words + literals. `titl` id = the activated row's
  `{tid,code}` high word (row caption), `line` id = chain literal 0x112 (=274 restart question) /
  0x10e (=270 quit question); `lin2` id 0 = blank. Ids resolve through the FC remap
  (`widgetStringAt`, pass-21igscn rule), NOT raw bank indices.
- **Port** (`app/livematch.cpp`): IGQUESHN draw branch paints header overlay + `titl` row caption
  (white, fontid 1) centred at y+8 + `line` question (cream, fontid 0). `btt1/btt2` (objecttype 14,
  textid 12/13 = remap "Yes"/"No") repaint after composite with `drawLabel`'s exact placement math
  (TEXTX/YOFFSET, ALIGNMENT=0 left anchor at x=179/353, palette `normalcolour=1` ink): the composite
  draws the white `bo25` RECT plate (w=12 lane) over the first glyph, so the overlay uncovers it —
  pixel-exact re-draw verified, no ghost double-paint.
- **Gate**: `fe_igmenu` IGQUESHN evidence regex tightened to
  `IGQUESHN: shapes=[1-9][0-9]* texts=[1-9][0-9]*` (was a partial-mismatching `texts=[1-9]`
  alternative — ctest PASS regex list only needs one match); evidence `IGQUESHN: shapes=18
  texts=71`, capture `_igquesn.png` shows "Quit Match" title, "Are you sure you want to Quit
  Match?" line, full dark-ink "Yes"/"No" over the white plates. IG-family subset
  `ctest -R "fe_igmenu|fe_igoptn2|fe_igstats|fe_mgmt_pages"` 4/4 PASS.
- **Suite**: 146/148 (`test_sim_scenarios`, `self_test_gameplay` = in-flight simulation kick-seam
  lane, untouched). Dossier `analysis/pass36igquesn/NOTES.md`.

## 29. Round 2026-11-18 — pass-39igname: IGMENU `name` byte-lane blank + HUD minute chip [LANDED]
- **`name` override [C]**: init tail `0x43f1c0` / installer `0x43ffca` branch on controller byte
  `DAT_0057c920` — zero for live-pause boards → `FUN_004788b4(win, 'name', str VA 0x562144)`
  installs a blank `" "`, covering the static textid-231 caption ("Are you sure you wish to
  exit with points remaining?") that the composite painted at (319,390) in port captures.
  Retail live-pause `name` slot = blank. Port = composite whitelist `kIgMenuVisible`
  (IGMENU objects minus `name`) in `app/livematch.cpp`; IGQUESHN has no `name` object.
- **Minute chip [C]**: `FUN_00438670@0x438923` — 30×24 strip fill `$0x88000000` at
  absolute width+20 (`0x438958 LEA ESI,[EBP+0x14]`, y=10) + centred `sprintf("%02d",
  DAT_00641260)` (format verified at 0x55b54c). Gate: option bytes `0x60cc05/0x60cc06`
  bit 2, mode ∉ {0xa,0xc}, slot ≠ 9999 (sentinel 0x49d66c; countdown only armed for
  match-type-9 sessions, DEC loops 0x4b4ca7/0x4b530f). Port: `HudGeometry::minChipMinutes`
  + `layoutScoreboard` chip block + knob `hud_min_chip`; default −1 hidden (exhibition never
  arms it), chip x `sbX+w+gap+4` = exe absolute 181 (the +4 = four 1 px glyph gaps the port's
  measured widths exclude). HUD_WC98.md §"Minute chip" updated ported.
- **Gates**: `test_hud` chip assertions (off → no quad; 5 → strip 30×24 @x=181 alpha 0x88 +
  2 centred digits); `fe_igmenu` PASS. Capture `_igmenu.png` crop (300,385)-(480,412) = 0
  bright pixels: junk caption erased, header/rows intact. Dossier `analysis/pass39igname/NOTES.md`.

## 30. Round 2026-11-18 — pass-40league: FEFLOW74 event tree + tick decoded, `tmf2` popup edge [LANDED]

- **Event handler 0x45df84 token tree decoded** (`analysis/pass40league/lg_dec.txt`, Ghidra
  image reads): equality leaves `lb01 0x45e0c8`, `gobk 0x45e0d3`, `ab00 0x45e0de`,
  `tmf1 0x45e46b`, `sbar 0x45e472`, `tmf0 0x45e483`, `tmf2 0x45e4e0`, `tmf3 0x45e4eb`,
  `gofd 0x45e545`, `lb00 0x45e556`, `ab01 0x45e5b5` (bounds = binary-search nodes, not keys —
  pass-20records rule applied). `tmf2` leaf 0x45e845 pushes scene 0x76 + ev 8005 = the
  aggression popup over the grid. `tmf0` keyboard branch: budget gate `[0x5882b8] > 0` → widget
  0x56c/slot 0x82e update + caption 0x56388c; spent → error caption path 0x45e728 (`CALL
  0x48a7b4`, slots 0x588260/0x58828c). `ab01` rings the league-group counter `[0x57d040]++`
  wrap 0..0xc; `ab00` repaints sliders + help caption 0x563898; `lb00` clears the drag cursor
  (`[0x60aac0]=-1`); `sbar` writes the team-row FC slot word `[row*0x20+0x57d018].word →
  [idx*2+0x631746]`.
- **Tick 0x45e8e8 decoded** (`lg_dec.txt` + keyboard/mouse branches `lg_dec2.txt`): mouse class
  `CALL 0x446624==2` clears drag state, `[0x60aac0]=-1` per tick; `attb` budget text widget id =
  group slot `[0x57e84a + grp*4] >> 16` → `CALL 0x47882c`; 9-word budget array copy
  `[0x631746 + i*2] → [0x60a9fc + i*4]`; keyboard branch syncs the three slider globals
  (0x5882a8/0x588250/0x58827c) via `CALL 0x4d31a0` before the common prologue. Engine-side
  budget figure stays unported (no squad-budget simulation in the presentation model).
- **Ported**: Enter on FEFLOW74 opens FEFLOW76 (`tmf2`); popup Enter/Esc pop back to the grid —
  new `cfgPopBase_` remembers the opener, popup open no longer resets `cfgRow_`.
  `openLeagueCfg` now clears `hubView_` (config scene supersedes the hub — `onHub()` had been
  swallowing every Esc while a grid/popup was open); Esc from the FEFLOW73 list restores
  `hubView_` (menu-return semantics unchanged).
- **Gates**: selftest step `fe-league-nav` (open 74 → row cursor → Enter popup → Enter back →
  Enter popup again → Esc back, cursor kept → Esc grid → FEFLOW73 list) PASS;
  `fe_league_cfg*`/`fe_league_popup76` green. selftest `passing`/`animation` FAILs remain the
  in-flight `simulation/` kick-seam lane, untouched.

## 31. Round 2026-11-18 — pass-41scrl: IG option-popup `scrl` slider-widget art lane [LANDED]

- **Grammar**: each `ig*optns` popup (IGAOPTNS/IGGOPTNS/IGMOPTNS.LAY) declares a
  `scrl` widget: `objecttype=11` (slider-bar class, same as the FEFLOW15 `sldN`
  rows), `sname=sldr`, `shapeID=0x50d9` (53865, slider-family shape id; the
  `0x50b9` go-button family is documented in pass-12 `widget_slice_spec.md` §4),
  `fname=igsrolbr.fsh`, `vpath=..\IGOPTION\`, rect (64,111,11×216). `sldr` is the
  shared widget-class family id, not a bitmap tag — the composite `sname`-as-tag
  lane misses it (legit-missing slot), so the groove region painted black.
- **Art**: `FE/IGOPTION/IGSROLBR.FSH` ships the parts — `sldt` 3×10 top cap
  (blue-grey), `sldm` 3×50 groove column (black body, grey 181 right edge),
  `sldb` 3×10 bottom cap; no thumb shape. The FE reference family
  `FE/PACK/FEFLOW11/SCLBAR.PAK` has `sldm` 21×22, `sldt` 48×19, `sldb` 32×16 and
  `thmb` 20×32 — thumb art exists only in the FE slider family.
- **Port**: `presentation::paintScrollSlot` (`fe_screen.{hpp,cpp}`) paints groove
  `sldm` stretched to the widget rect, centred `sldt`/`sldb` caps, and a thumb =
  `thmb` art where the family has it, else the flat page quad
  `(210,230,255,220)`, height `h/8`, at the value-cursor ratio (paintSliderThumb
  convention). Wired into `IgBoards::drawIgOptns` (live popups) and
  `FeFlow::drawIgOptns` (capture gate).
- **Gates**: `fe_igoptns_{a,g,m}` shapes 5→7 (groove + 2 caps; thumb quad counts
  as `rects`); pixel sweep `/tmp/wc98port-out/igopt{a,g,m}_fe00.png`: groove
  columns x=64..74 blend black→grey gradient, thumb blend (181,198,220) visible
  above it. Suite 151 tests: 149 pass; the 2 FAILs (`test_sim_scenarios`,
  `self_test_gameplay` passing/animation) remain the in-flight `simulation/`
  kick-seam lane (§12 note). Dossier `analysis/pass41scrl/NOTES.md`.

## 32. Round 2026-11-18 — pass-42lg79: FEFLOW79 attb runtime skill-column header [LANDED]

- **Grammar [C]**: `attb` (box (315,110,50,18), LAY template textID 1379 → remap bank
  1376 `Pos`) is a runtime label: scene handler FUN_0045ff14 tag branch writes the selected
  skill-column abbreviation into the window object (`obj+0x48`) — init store
  `0x46003a MOV [0x57d040],0xc`, label setter `0x46005b CALL 0x47882c` with
  `EBX = [EBX*4 + 0x57e84a] >> 16` (widget-id lane, pass-21igscn remap rule). Table
  DAT_0057e84a (13 entries) → {Spd,Spw,Sac,Acl,Tac,Hac,Bct,Agl,Rct,Fit,Awr,Pac,Cre}
  (bank idx 1736,1744,1575,172,1818,861,315,202,1475,738,288,1308,520). `ab00`/`ab01`
  arrows: ±1 with wrap at 12/0. The static template label rendered clipped `Po` (width 50).
- **Also**: `oval`/`ptop`/`pbot` (objecttype 19, zero coords, runtime-placed from the live-sim
  player table DAT_006307f0) excluded from `lg79Visible_` — the static paint was the stray
  top-left blob.
- **Ported**: attb dropped from the whitelist; overlay block (vit0 twin) paints
  `widgetStringAt(kAttrIds[lg79AttrCol_])` centred fontID4 white; `lg79AttrCol_` init 12;
  `adjustAttrColumn` ±1 wrap wired to `ab00`/`ab01` mouse hits. Header renders
  `Name | ← Cre → | Status | Cards` (`/tmp/wc98port-out/lcfg79_fe00.png`, texts 85→83).
- **Gates**: `fe_league_cfg*` family 9/9 green; new selftest step `fe-lg79-attr`
  (default 12, wrap up→0, wrap back→12, decrement→11). Dossier `analysis/pass42lg79/`.

## 33. Round 2026-11-18 — pass-43igfix: IG bottom bar + summary headers + BOX-size fill [LANDED]
- Sweep result (pass-43): the IG boards' bottom bar (Match Statistics / Bookings Summary /
  Score Summary / Back) and the Time/Player/Team list headers painted nothing on igflsum/
  igscsum/igstats/ighalf. Cause 1: LAY order paints `typ6..typ9` caption widgets before the
  white `bo25`/`box6..9` RECT plates covering them; cause 2: composite filled `width=12`
  anchors instead of `BOXWIDTH` panel sizes (audit: 80 mismatching fill widgets, 15 IGLAY.BIG
  members, LAYOUTS.BIG clean).
- Grammar [C]: engine fills BOXWIDTH/BOXHEIGHT when declared — `composite()` now prefers it;
  bar captions re-painted by the overlay after the plates via `paintLeft` (alignment=1, widget
  x + TEXTXOFFSET, palette ink from TEXTCOLOUR/NORMALCOLOUR); headers typ3/typ4/typ5 left at
  widget x (remap 1862 'Time' / 1351 'Player' / 1828 'Team' — the old centred 'Score'/1603
  header was wrong); third list column = team name.
- igstats kPageCap centered 'Statistics'/'Booking Summary' (soup ids 1771/329) removed — the
  real bar labels are typ6/typ7; page state = btn1/btn2 `btnd` chip chrome only.
- up/down list arrows restored on summary boards: removed from the kind>=3 whitelist exclusion
  (btnup->INGAME.FSH alias, pass-13 §D).
- Gates: IGSTATS shapes=23 texts=180, IGSCSUM texts=91, IGFLSUM texts=87; captures
  analysis/pass43igfix/captures/ig_{igstats,igscsum,igflsum}.png; suite green (sim lane
  excluded, in-flight work).

## 34. Round 2026-11-18 — pass-44pointer: pointer un-projection through the UI content rect [LANDED]
- Interaction defect (goal §7 resolution/scaling): the mouse was mapped by scaling the **whole
  window** into the 640x480 canvas (`app/main.cpp` FE hub + `app/livematch.cpp` live pointer and
  the gate-side `motion`/`click` pushers). The UI pipeline always draws into `render::fit4x3(window)`,
  so on any non-4:3 window the letterbox offset was folded into the canvas — every widget hit box
  sat `fit4x3().x` pixels inboard of the art it covered (1280x720: 160 px, i.e. a fifth of the
  screen). Hub wedges, view-screen buttons, the pause-menu rows and the confirm-box buttons all
  responded at the wrong place, and the drift grew with the aspect mismatch.
- Fix: one coordinate-space contract in `presentation/` (`docs/architecture/RENDERER.md` "Window
  coordinate space"): `uiPointerFromWindow` (window points -> UI, offset-subtract + rect scale) and
  `uiWindowFromPointer` (UI -> window points, for the scripted mouse lanes). Both use
  `Window::pointSize()` (SDL mouse events are window *points*; `pixelSize()` is swapchain space and
  carries the HiDPI scale factor), so mapping is correct independent of DPI scaling.
- Gate: `test_presentation` pointer block (16:9 offset, bar clamp, 4:3 identity, forward-map
  round trip). Capture lane `--fe-pointer X,Y` injects window-space coordinates through the same
  un-projection: `/tmp/p3/inter/hub_pointer_fe00.png` vs `hub_plain_fe00.png` differs in exactly
  two clusters — cursor sprite at the injected point, and the hovered wedge band (x 448..512,
  y 240..320 = virtual ~(493,240)) showing the HILITED wedge art.
- Mouse-driven IG lanes re-verified through the forward map: `fe_hub`, `fe_igmenu` (hover row 6 +
  click-activate IGSTATS), `fe_igoptns_m`, `fe_igstats`, `fe_ighalf` PASS. Suite 149/151 (the two
  in-flight simulation-lane tests stay excluded).

## 35. Round 2026-11-22 — pass-45ctrl: FE87/FE13 tick = device-slot pump; port paints the default state [LANDED]

- **Verdict [C]**: the pass-23/24 "device-slot cursor ring" model was an invention, but the
  earlier same-day "static boards, pump has no callers" retraction was **also wrong** — the
  installed ticks are JMP thunks: `0x467ab0` (FE87) and `0x44e578` (FE13) both `JMP FUN_0044c6c0`,
  the device-slot pump (a byte-scan of E8 call sites finds no direct callers precisely because
  every call goes through these thunks). Pump contract [C] (`analysis/pass45ctrl/`
  `tick_disasm.txt`/`pump.txt`/`decomp.txt`/`subdecomp*.txt`): install repeat-poll callback
  `FUN_0044c958` at `[sceneWindow+0x14]`; zero the 16-dword group `DAT_00608748..0x608784` every
  tick; set `DAT_0060a2e4`, clear text latch `DAT_00600db8/dc8`; run row painter `FUN_0044ccd4`
  (click callback `EDX=FUN_0044c9fc`: `DAT_00608768[row]=value` + repaint; `FUN_0044d128` +
  `FUN_0044ca1c`: clear-row + repaint). Painter grammar per row (`slot=DAT_00608768+i*4`):
  slot==0 → `anmN` anim loop flag on, `arwN` pair-arrow SHOWN, `grvN` text HIDDEN, panes HIDDEN;
  slot≠0 → hide `anmN`/`arwN`, show `lrp`/`rrp` repeat-arrow art, show `grvN` (`FUN_0044ca48`).
  No cursor ring, no row highlight, no device rail anywhere.
- **Port**: paints the pump's **all-slots-0 default state** — 4 keyboard rows + pair arrows +
  captions + shields; `ctxN`/`grvN`/`nplr` runtime text excluded. Cut (kept): `wc13Dev_`/
  `cfg87Dev_`, `cycle*Dev`, `on*Dev`, ring highlights, LEFT/RIGHT dev keys, `onWc13Adjust`
  device branch; `--fe-*-dev N` flags stay parsed but are no-ops. The exe row-state lane
  (widget-click callbacks setting `DAT_00608768[row]≠0` → `lrp`/`rrp` art + `grvN` text) stays
  unwired [H] — nothing in the port drives it and no byte exists for a port-side cursor.
  (Superseded same day: §36 pass-uidev wired the lane — pad button events drive the row state.)
- **Gates**: selftest `fe-cfg87`/`fe-wc13` PASS (`statik=1` — navigation never alters the board,
  which matches the exe: only click callbacks change row state); captures
  `cfg87static_fe00.png`/`wc13static_fe00.png` viewed — 4 keyboard rows, arrows, shields,
  captions; no ring, no joystick tile. Registry rows 8/48 stay impl.

## 36. Round 2026-11-22 — pass-uidev: device-row lane wired for both controller boards [LANDED]

- **Port** (`app/fe_flow.cpp` `devRowsPoll`/`devRowsAdjust`/`devRowsPaint`, mirrored per-board
  in `drawWc13`/`drawCfg87`): the §35 "unwired [H]" row-state lane is now driven.
  `devRowsPoll` walks `SDL_GetGamepads` each frame (handles kept open for the process, like
  the CONTROL1 lane's `s_pad`) and binds row i on any button-down of pad i — the exe's
  dispatcher `FUN_00436bd0` filters device types 0/2 (keyboard/mouse) at entry, so only pad
  events bind; bindings are sticky for the board visit instead of the exe's 30-tick queue
  re-arm `[H]`, and the pad→row/side/id mapping (pad j → row j, even j left (state 1, `alpN`
  pane) / odd j right (state 2, `arpN`), id = j → `contr.fsh` frame id+1) is port-side policy
  `[H]`. `devRowsAdjust` applies the painter grammar to the composite whitelist: unbound rows
  keep `arwN`, drop `alpN`/`arpN`/`anmN`; bound rows drop `arwN` + the non-bound-side pane.
  `devRowsPaint` draws the bound `contr.fsh` frame at the `anmN` origin (manual, after
  composite — composite's baked `ticksperframe=0` frame is never painted).
- **Capture lane**: `--fe-wc13-dev N` / `--fe-cfg87-dev N` now force N bound rows
  (`setDevRowsCapture`) instead of being ignored; `WC98_DUMP_VIS=1` dumps the pre-adjust
  whitelist. Captures `/tmp/uidev/wc13dev_fe00.png` (rows 0/1 bound: arrows hidden, panes +
  `contr.fsh` art on the bound side; rows 2/3 keep arrows) and `cfg87dev_fe00.png` (same) —
  `ltfg`/`rtfg` team shields are LAY statics on both boards, not part of the lane.
- **Gates**: selftest `fe-cfg87`/`fe-wc13` gained `lane` assertions (`lane=1`): bound rows
  drop `arw0`/`arw1` + cross panes `arp0`/`alp1`, unbound rows keep `arw2`/`arw3` and drop
  `alp2`/`arp2`/`alp3`/`arp3`, `anm*` never composite; `devRowState` encodes row value +
  device id. `statik` still holds headless (no pad → all rows stay state 0). Registry rows
  8/48 stay impl. §17/§18 "dev flag ignored" wording superseded by this section.

**grvN/ctxN text-lane follow-up (2026-11-22, pass-uidev):** the §35 "show `grvN`" leg is
closed — `FUN_0044ca48` uses the `grv%01d`/`ctx%01d` bytes (file 0x160C54/0x160C8C) as
**widget-name templates** (`FUN_0049168e` builds the lookup name, `FUN_0047a3e8` resolves
the object, `FUN_0046bfac`/`FUN_0046bfcc` only show/hide it), and the LAY records carry
`textid=0` with zero-size `ctxN` (`analysis/pass11/lays/FEFLOW13.LAY`) — no painter path
ever writes string content into them, so the original renders them empty even on bound
rows [C]. The `alpN`/`arpN` panes are `null.fsh`/`shapeID=null` placeholders (legit, §A).
Port behaviour matches; no defect.

**anmN controller-image follow-up (2026-11-22, pass-uidev2):** unbound rows were rendering
arrow-only; both painters show `anmN` on unbound rows — `FUN_0044ccd4` slot==0 runs
`FUN_0046d28c` (animation flag ON → `contr.fsh` frames 1..8 cycle via the
`FUN_0044c920`/`FUN_0044c9fc` pump), bound rows run `FUN_0046d27c` (flag OFF, frozen frame).
Port now keeps `anmN` in the composite for state-0/3 rows (static frame 1 at
`ticksperframe=0`; the frame-cycling pump pacing is [H]) and only bound rows use the manual
frozen-frame paint. Verified: `/tmp/uanm/{wc13,cfg87}_fe00.png` rows 2/3 show the
controller image + arrows; selftest `fe-cfg87`/`fe-wc13` still pass. The
`FUN_00446624 = DAT_0057c965>>0x18` board-mode word (cmp==2 → `FUN_0044cb04`, cmp==1 branch
at 0x443fbf) selects the two painter grammars; its producer is not statically decoded [H],
port keeps the §35 grammar for arrow/pane placement.
