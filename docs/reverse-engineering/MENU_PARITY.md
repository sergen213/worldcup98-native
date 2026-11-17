# Menu Parity Inventory — FIFA: Road to World Cup 98 (Windows) → native Linux port

Authoritative menu-screen inventory + parity checklist for the "100 % Windows menu parity" goal. Companion to `FE_MENU_SCREENS_WC98.md` (handler/grammar dossier), `LAY_COMPOSITOR_NOTES.md`, `WINE_ORACLE_WC98.md`.

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
| 7 | 00447ac3 | 0x56237c | feflow11 | 0x44d9c8 | 0x44dda0 | 0x44dfa8 (thunk→`FUN_0044a210`) | — | FEFLOW11.LAY | World-Cup team pick grid: map, big flag `tmfg`, group cells `tc/tt/ta/stf*`, scroll `sbar` | **impl** (`fe_render_tournament`, `fe_screen_capture_team`) |
| 8 | 00447ae2 | 0x562388 | feflow13 | 0x44e440 | 0x44e578 | 0x44e580 (thunk→`FUN_0044a210`) | — | FEFLOW13.LAY | World-Cup competition config ("quit the game"/"quit world cup" buttons, same row grammar as feflow09) | cap |
| 9 | 00447b06 | 0x562394 | feflow15 | 0x44f22c | 0x44f804 | 0x44fa7c | — | feflow15.lay | options screen, 4 tabs (`tab1-4`,`tbb1-4`,`lst1-4` = Match/Game play/Audio…, `save`/`deft`/`back`/`fwrd`) | cap (interactive **miss**) |
| 10 | 00447b25 | 0x5623a0 | feflow16 | 0x4519c0 | 0x451aac | — | — | FEFLOW16.LAY | lineups grid (lp/rp/ls/rs player rows, flags, `ftix`) — internal lineup editor | cap |
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
| 24 | 00447cfa | 0x562448 | feflow32 | 0x45791c | 0x457cb4 | 0x457e4c | 0x457910 | FEFLOW32.LAY | single-player match config (volume bars left, footballs, shooting/passing captions) | cap |
| 25 | 00447d20 | 0x562454 | feflow33 | 0x457e68 | 0x457f8c | 0x457fa4 | 0x457e60 | FEFLOW33.LAY | two-player match config (both team names, ball shadows, volume bars) | cap |
| 26 | 00447d46 | 0x562460 | feflow34 | 0x457fd8 | 0x45822c | 0x4582e8 | — | FEFLOW34.LAY | trivia bookmark page (pane `mttr`, prev/next buttons) | **impl** (`fe_records34`) |
| 27 | 00447d65 | 0x56246c | feflow35 | 0x45838c | 0x458714 | 0x45889c | — | Feflow35.lay (+ FEFLOW35.LAY board variant) | group standings table: `gpnm` group name, rows `ssn/ssp/ssw/ssd/ssl/ssf/ssa/sst`, stats buttons | **impl** (`fe_render_present`: "FE-PRESENT FEFLOW35"; `--fe-pick 35`) — uppercase board variant: cap |
| 28 | 00447d84 | 0x562478 | feflow37 | 0x45922c | 0x4592f0 | 0x459220 | — | FEFLOW37.LAY (authored `Feflow86.lay`) | yes/no confirm popup (blur, popp, no/yes, txt×3) | **impl** (popup lane + hub quit-wedge confirm; gates `fe_popup_yesno`, `fe_popup_dismiss_enter`) |
| 29 | 00447da3 | 0x562484 | feflow41 | 0x459420 | 0x4597ac | **0x0** | — | FEFLOW41.LAY | LOAD game list (blur, `lstb`+slider, "LOAD game" caption) | **impl** (LOAD list scene scans real `SLOT%02d.SAV` files; gate `fe_loadsave41`) |
| 30 | 00447dc2 | 0x562490 | feflow42 | 0x45979c | 0x4597ac | 0x459b98 | — | FEFLOW42.LAY | SAVE game list (tick/cross, `lstb`+slider) | **impl** (SAVE-confirm list; Enter writes the focused slot + tick flip; gate `fe_savemgr42`) |
| 31 | 00447de1 | 0x56249c | feflow45 | 0x459d94 | 0x459df8 | 0x459e14 | — | FEFLOW45.LAY | dev test screen ("test button") | cap |
| 32 | 00447e00 | 0x5624a8 | feflow50 | 0x459e60 | 0x459f7c | 0x459fe4 | — | FEFLOW50.LAY | practise match config (same row grammar as feflow09) | **impl** (`fe_flow_setup50`, `fe_nav_practise`) |
| 33 | 00447e1f | 0x5624b4 | feflow51 | 0x459ff0 | 0x45a0fc | 0x45a110 | — | FEFLOW51.LAY | stadium/match select with HALF LENGTH setting + ball set (`mpx10`) | cap |
| 34 | 00447e3e | 0x5624c0 | feflow52 | 0x45a120 | 0x45a128 | — | — | FEFLOW52.LAY | lineups grid variant (no flag pane) — internal | cap |
| 35 | 00447e56 | 0x5624cc | feflow56 | **0x0** | 0x45a130 | 0x45a134 | — | FEFLOW56.LAY (stub) | null stub scene (init=0; numobjects=1) | cap |
| 36 | 00447e75 | 0x5624d8 | feflow72 | 0x45a514 | 0x45ae84 | 0x45afc4 | — | FEFLOW72.LAY | league match setup: defenders/midfield/forwards +/- counters (`bx9`, `vx3`) | cap |
| 37 | 00447e94 | 0x5624e4 | feflow73 | 0x45b860 | 0x45c800 | 0x45c8dc | — | FEFLOW73.LAY | league team player list (`pn00..pn08`, status/total columns, scroll `sbar`) | **impl** (`fe_league_select`, `fe_nav_league`) |
| 38 | 00447eb3 | 0x5624f0 | feflow74 | 0x45df84 | 0x45e8e8 | 0x45e99c | — | FEFLOW74.LAY | league team-management grid (`ktx9` name keys, attributes +/- `attb`) | cap |
| 39 | 00447ed2 | 0x5624fc | feflow75 | 0x45ee34 | 0x45f480 | 0x45f56c | — | FEFLOW75.LAY | league lineup editor (player block `ptop/px17/pbot`, save/reset buttons) | cap |
| 40 | 00447ef1 | 0x562508 | feflow76 | 0x45f6a4 | 0x45fa48 | 0x45fbe4 | — | FEFLOW76.LAY (authored `feflow75.lay`) | aggression-value confirm popup (`alok`/`canc`, "aggression text") | cap |
| 41 | 00447f10 | 0x562514 | feflow79 | 0x45ff14 | 0x460af4 | 0x460cac | — | FEFLOW79.LAY | league squad/formation screen (player oval `oval`, bias slider, attributes) | cap |
| 42 | 00447f2f | 0x562520 | feflow80 | 0x461edc | 0x463098 | 0x463168 | 0x461ed0 | FEFLOW80.LAY | full custom-mode management grid (132 objects: player model `plyr`, name edit box, stat columns, memory-card captions) | cap |
| 43 | 00447f55 | 0x56252c | feflow81 | 0x4647a8 | 0x464878 | 0x464790 | 0x464798 | FEFLOW81.LAY | results page (multitext pane `mult`, scroll bar, quit/back) | **impl** (`--fe-wc-cfg 81`, gate `fe_wc_cfg81`; `mult` rows from bracket results) |
| 44 | 00447f7b | 0x562538 | feflow82 | 0x465854 | 0x465b0c | 0x46584c | — | Feflow82.lay | player performance stats (rows `t000-t008` type=44, stat buttons `mbt0-2`, pair headers) | **impl** (`--fe-wc-cfg 82`, gate `fe_wc_cfg82`; t00N standings rows) |
| 45 | 00447f9a | 0x562544 | feflow83 | 0x467270 | 0x467274 | — | — | FEFLOW83.LAY | lineups + score grid (lp/rp/ls/rs, `llls/rrrs` scores with inc buttons) — dev/debug screen **[H]** | cap |
| 46 | 00447fb2 | 0x562550 | feflow84 | 0x467320 | 0x467438 | — | — | FEFLOW84.LAY | lineups + score grid variant (same debug pattern) **[H]** | cap |
| 47 | 00447fca | 0x56255c | feflow86 | 0x467770 | 0x4677b4 | 0x467760 | — | FEFLOW86.LAY | confirm popup (txt0-2, fe09 button, footix) | cap |
| 48 | 00447fe9 | 0x562568 | feflow87 | 0x467970 | 0x467ab0 | 0x467ab8 | — | FEFLOW87.LAY | controller-select grid (4 slots, controller pictures `anm*`, gravis button numbers, "Num Team Mates") | cap |
| 49 | 00448008 | 0x562574 | feflow88 | 0x467af0 | 0x467c60 | 0x467d98 | — | FEFLOW88.LAY | "all time greats" records page (two multitext panes `mtxl/mtxr`, titles 4311/4376) | **impl** (`fe_records88`) |
| 50 | 00448027 | 0x562580 | feflow89 | 0x467da0 | 0x4683a4 | 0x4685b4 | — | Feflow89.lay | team save/load menu (6 buttons `b000-b005` incl. "Load" 994, reset-all/reset-team) | cap |
| 51 | 00448046 | 0x56258c | feflow90 | 0x468728 | 0x4688e8 | 0x468924 | — | Feflow90.lay | **World-Cup history board** (hub wedge `fe33`): 9-edition year cursor (`ysla/yslc/ysrb`), finalist pair `ltnm/rtnm` + scores `ltsc/rtsc`, FC captions 429..433; Enter `'fe91'` -> feflow91 | **impl** (pass 13: year cursor over 1930..1994, FCDB names/shields painted) |
| 52 | 00448065 | 0x562598 | feflow91 | 0x468b50 | 0x468c68 | 0x468d28 | — | Feflow91.lay | continuation board for the selected final (two shields + qualifier rows, `fe08` confirm / `fe06` exit) | **impl** (composite + FC captions; chain from feflow90 Enter) |
| 53 | 00448084 | 0x5625a4 | feflow93 | 0x468d74 | 0x468e8c | — | — | Feflow93.lay | lineups + score grid variant (classic family) **[H]** debug | cap |
| 54 | 0044809c | 0x5625b0 | feflow94 | 0x46999c | 0x469904 | 0x46986c | — | feflow94.lay | credits roll (`cr01..cr32` text rows) | cap |
| 55 | 004480bb | 0x5625bc | feflow95 | 0x469cb8 | 0x469af8 | 0x469a60 | — | feflow95.lay | icon/key-assignment popup (`box1-8`/`btn0-7`, "…and icons") | cap |
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

Registry-internal totals: **impl** = 17 unique scenes interactive — feflow06 (main menu), 07, 08, 09, 10, 11, 25, 26, 27, 35, 37, 41, 42, 50, 73, 90, 91; **miss** = 0; **cap** = 39 — everything else, renderable via the `--fe-screen` lane only (includes stub scenes feflow00/feflow56 and feflow15, whose interactive option-list logic is missing).

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
| FEFLOW51.LAY | 35 | match select with HALF LENGTH + ball set `mpx10` (sibling of feflow08) | cap |
| FEFLOW52.LAY | 47 | lineups grid, single-flag pane — internal | cap |
| FEFLOW53–55.LAY | 1 | null stubs | cap (stubs) |
| FEFLOW56.LAY | 1 | null stub (registry #34: init=0x0) | cap (stub) |
| FEFLOW57.LAY | 6 | LOAD REPLAY list (blur+overlay, quit button, `lstb`+slider; caption id322 leak) — **not in registry** → replay-load screen **[H]** | miss (unregistered) |
| FEFLOW58–71.LAY | 1 | null stubs (14 members) | cap (stubs) |
| FEFLOW72.LAY | 56 | league match setup: defenders/midfield/forwards count buttons, form rows (`ptop..pbot`) | cap |
| FEFLOW73.LAY | 60 | league squad list: pn×9 names, status/total columns, player block, reset/back/fwd | **impl** |
| FEFLOW74.LAY | 38 | management grid: name keys ktx9, attributes +/-, scroll (authored path empty — header corrupted) | cap |
| FEFLOW75.LAY | 42 | lineup editor + score fields ("LATEST SCORE FROM <city>" 958, stadium "La Beaujoire" 965 leaks) | cap |
| FEFLOW76.LAY | 10 | aggression confirm popup authored `feflow75.lay` copy (ok/cancel, "aggression text") | cap |
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
| FEFLOW87.LAY | 57 | controller-select grid: 4 slot overlays + controller pictures + gravis button numbers + "Num Team Mates" (4594) | cap |
| FEFLOW88.LAY | 21 | "all time greats" page (`mtxl/mtxr` panes, titles 4311/4376) | cap |
| FEFLOW97.LAY | 1 | null stub authored `feflow01.lay` (duplicate of #2) — the number is an authoring artifact **[H]** | cap (stub) |
| Feflow02.lay | 14 | netplay lobby: machine list `glst`, create/join buttons, machine-name box; **not in registry** → netplay family | miss (unregistered) |
| Feflow03.lay | 14 | netplay chat/lobby: chat boxes, eject button, machine list — not registered | miss (unregistered) |
| Feflow06.lay | 22 | main-menu hub art: friendly/WC98/practise/shootout/classic big buttons (`fe07/fe11/fe32/fe33/fe90` type=32), modem/netw, quit/options/team-mgmt/load/highlights(`repl`)/credits(`cred`) | **impl** (menu entries; highlights+credits buttons not wired) |
| Feflow35.lay | 59 | group standings table: `gpnm`, ssn/ssp/ssw/ssd/ssl/ssf/ssa/sst rows ×4, stats buttons — the standings member used by `--fe-present` | **impl** |
| Feflow82.lay | 31 | see row "FEFLOW82→Feflow82.lay" (same member; registry name feflow82) | cap |
| Feflow89.lay | 26 | team save/load menu: buttons b000-b005 ("New Controller Pak inserted." 1210, "Load" 994), reset-all/reset-team/save-all | cap (save-slot UI **miss** in port) |
| Feflow90.lay | 42 | **World-Cup history board** (hub wedge `fe33`): year cursor + arrows, finalist pair/scores fields, volume bars | **impl** (pass 13 interactive scene; classic-era FCDB ids 861..876 -> `F040..F045` flag art, e.g. F040/F041 decode byte-identical = original data duplicate for Ur/Arg 1930) |
| Feflow91.lay | 57 | continuation board: two shields + qualifier rows ("goes to feflow90" comment) | **impl** (composite + FC captions; `fe91` Enter chain from feflow90, `fe06`/Esc exit) |
| Feflow93.lay | 60 | lineups+score debug grid (classic family) **[H]** | cap |
| INGAME.LAY | 5 | in-game menu frame: `titl/bxot/tlft/blft` shapes + `menu` type=16 widget; pairs with FE/INGAME/IGMENU.LAY overlay | boundary (frame is scene-handler code-drawn: the LAY carries only invisible null.fsh placeholders, raw shapes=0 legit — no composite expected; the port draws its live HUD frame from INGAME.FSH chrome in `app/livematch.cpp`) |
| fe.lst | n/a | member name list file, NOT a .LAY (size 3953) — LAYOUTS.BIG manifest | n/a |
| feflow15.lay | 27 | options screen: tabs 1-4 (`tab*`/`tbb*`), option lists `lst1-4` type=17, save/default/back/fwd; captions "Game play options"/"audio options" comments | cap (interactive **miss**) |
| feflow94.lay | 33 | credits roll: cr01-cr32 text rows at fixed slots | cap |
| feflow95.lay | 29 | icon/key-assignment popup: box1-8 + btn0-7 checkboxes, "…and icons" title, yes button (authored path = feflow94.lay — header artifact) | cap |
| igaoptns.lay | 12 | in-game audio options tab: list type=17 + slider, ok/cancel/default buttons, help pane; captions ids 1174/1175/1144 (bank noise — dev strings on this media); widgets named `1`,`2`,`3`,`04` | **impl** captions lane (pass 13: rows/values + tab captions painted, chck art absent [C]) |
| iggoptns.lay | 12 | in-game gameplay options tab (same grammar, `go_g/go_a/go_c` buttons) | **impl** captions lane (as above) |
| igmoptns.lay | 12 | in-game match options tab (id1171 "Moves highlight within bookmark group") | **impl** captions lane; ROWS windowed to the list box (5 slots, scroll with row cursor) |

Counts verified: 101 members total = 9 interactive impl, ~30 stub/unused, remainder cap/miss as listed. Registry↔member mapping is name-for-name (`feflowNN` → `FEFLOWNN.LAY`, case variants per table). Members not registered in the scene registry (43, 57, 78, 85, Feflow02/03, FEFLOW04/14, credits-adjacent) are legacy netplay/team-management-era assets **[H]** where noted.

## 2b. IGLAY.BIG members (30) — in-game screen pack

Dump: `wc98-extract FE/PACK/LAYOUTS/IGLAY.BIG` → "1 files, 30 BIG members" (`/tmp/pass12inv/iglay/IGLAY.BIG.d`). Grammar = raw LAY text (`[object N] objecttype/objectname`, same as §3 of the companion doc). `textid=0` is dynamic (runtime-resolved from IG banks); captions are not statically resolvable in this pack.

| member | objs | purpose **[C]** (authored path + widgets) | port |
|---|---|---|---|
| CONTROL1.LAY | 27 | in-game controller scheme page: `ctx0-3` config texts, gravis button numbers, team name texts | **impl** (options-chain popup via `--fe-control N`: composite paints the grv*/ls*/*rs* bar art + npll/nplr nav pair; overlays FC#258 'Attacking' ctx caption, four scene-default percent bars from DAT_00632778 {80,80,80,80,65,80,1} with gold fill quads, live gamepad axis readout when a pad is open (fallback 'kbd'), tmtl/tmtr FC#4594 captions + team-mates counts; gate `fe_control_page`) |
| IGCAMERA.LAY | 19 | camera options popup (box/typ/flg/nmb triples, menu/back/forw) | **impl** (live pause 'C' opens it: FC#353 CAMERA title + 5 key-assignment rows (FC 1468/1011/1545/1546/1104) with binding strings, flg/nmb pair cursor, forw/menu nav buttons; gate `fe_camera_pages`) |
| IGCAMSEL.LAY | 13 | camera-select list popup (menu widget) | **impl** (forw nav from IGCAMERA opens the grid: FC#379 'Camera Select' + 7 camera names, row cursor quad, Enter applies a `MatchCamera` mode ("igcam: camera sideline"); gate `fe_camera_pages`) |
| IGCHAT.LAY | 16 | chat overlay: input line + `chat/cha2/cha3` display boxes (netplay) | miss |
| IGCONSEL.LAY | 2 | confirm-OK box (`okay`) | **impl** (`--fe-popup igconsel`: INGAME.FSH btnn/btnd chrome + "OK" caption over null.fsh region placeholders; gate `fe_igconsel`; FUN_00442000 stub = dismiss-only [C]) |
| IGFLOW86.LAY | 7 | in-game confirm popup (`fe09`, txt0-2; authored `IGflow86.lay`) — mirrors FEFLOW86 | miss |
| IGFLSUM.LAY | 45 | full-match summary screen (typ/box stat grid) | **impl** (full-time chain page 2: Enter on IGSCSUM pages to the IGFLSUM composite; Time/Player/Score column rows from `goalLog` (minute, shirt#+pen/og tag, running score), title FC#776 "Full-Time"; gate `fe_igflsum`) |
| IGHALF.LAY | 57 | half-time summary screen (stat grid, list widgets) | **impl** (livematch opens it at the half whistle — period-end arm via `Match_requestPeriodEnd` + event 9 grammar [C]; stat-grid pages + FC#858 "HALF-TIME" title; Esc = resume to match; gate `fe_ighalf`) |
| IGHILIT1.LAY | 1 | single box overlay authored `igmenu.lay` — header artifact; highlight hilite pane | **impl** (shipped dummy hotkey-transition page — the LAY member's own comment says so; composite renders, shapes/texts 0 legit: null box1) |
| IGHILITE.LAY | 19 | highlights browser: rewd/paus/play/fast/slol/slor transport buttons, boxes | **impl** (IGMENU 'Highlights' row opens the browser over `rplyindx.dat` rows — FC#899 "INSTANT REPLAY" title on box3, slot filename + `SLOT NNNN` readouts, btlt/btrt 10-row slot pages, slol/slor cycle; Enter = app/replay.cpp re-drive of the selected file (state-hash parity `match=1`); gate `fe_highlite_browser`) |
| IGHILSAV.LAY | 22 | highlights save list (`lst1`, up/down) | **impl** (browser 'save' button opens it: lst1 rows from the rply index, Norway/Denmark header pair = typ names + FNNN shields + nmb score digits painted after the flags (z-order 5344a02), up/down arrows via btnup->ingame alias; Enter writes `rplyNNNN.dat` + index entry through the existing recordToUserDir path; gate `fe_hilsav_list`) |
| IGMENU.LAY | 14 | in-game pause menu: box1-5/typ/flg/nmb chrome + `menu` type=42 widget, name text — the popup the port lacks (Esc quits match directly) | **impl** (livematch Esc opens the composite; {Resume,Stats,Highlights,Management,Quit} rows bt0-bt4 chrome, Enter on Stats -> IGSTATS board, Highlights -> IGHILITE browser, Management -> IGTEAM page cycle (§E), quit row -> IGQUESHN confirm; gates `fe_igmenu`, `fe_igstats`, `fe_mgmt_pages`) |
| IGMSG.LAY | 17 | message popup authored `igqueshn.lay`; ok button, lines (`bo25`) | miss (shares the igqueshn key FUN_00440830; renderable via capture lane) |
| IGOPTN2.LAY | 21 | in-game options: tabs 1-4 + tab-button areas, option lists `lst1-4` type=17, back/fwrd; `textID=5`, help keys (78/81) — the ig\*optns trio lives here **[C]** captions "Match"/"Game play"/tab comments | miss |
| IGPRAC1.LAY | 36 | practise-mode setup/stats popup | **impl** (Esc from the live PRACT1 board opens it: on/off toggle rows + btn nav, keeper toggles; gate `fe_practise_pages`) |
| IGPRACT1.LAY | 37 | practise stats page (back/forw) | **impl** (pause 'P' opens it over the live frame: FC#1249 'Number of Players' stepper (< > INGAME.FSH bt4n/bt5n), numPlayers readout, User/CPU Keeper rows FC 1920/374 with On/Off 1267/1263; RIGHT tabs to IGPRACT2, LEFT wraps; gate `fe_practise_pages`) |
| IGPRACT2.LAY | 22 | practise stats page variant | **impl** (page-2 option grid composite + FC captions, back/forw nav pair; gate `fe_practise_pages`) |
| IGQUESHN.LAY | 20 | yes/no question popup (`okay`+`canc`, lines) | **impl** (livematch pause 'quit' row -> IGQUESHN confirm; gate `fe_igmenu`; shared key FUN_00440830 + Esc response-callback contract §33) |
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
| FEFLOW73 league squad list | [C] LAY+§1 slots | ✓ pn cursor x=35 y=134+19i | ✓ Esc/Enter | ✓ "entry 1 returned 0" assertion | ✗ | ✗ | `fe_league_select`, `fe_nav_league` |
| FEFLOW11 team grid | [C] LAY | ✓ flags/kit/shield fidelity | ✓ | ✓ shapes/texts/cursors>0 shell assert | ✗ | ✗ | `fe_render_tournament`, `fe_screen_capture_team` |
| Tournament presentation (Feflow35/25/26/27) | [C] LAY + pass8 slot inventory | ✓ 35/25/26/27 overlays | ✓ tournament flow | ✓ "FE-PRESENT FEFLOW35: shapes=[1-9]" | ✗ | ✗ | `fe_render_present`, `fe_nav_tournament --fe-pick 35` |
| Font/text layer (all screens) | [C] exe font table DAT_0057d2c0 | ✓ faces=10 | n/a | ✓ PASS_REGULAR_EXPRESSION "faces=10 lang=" + ger locale | n/a | ✗ | `fe_text_fonts`, `fe_text_lang_ger` |
| Arbitrary-member capture lane (all 156 layouts = LAYOUTS.BIG + IGLAY.BIG members + loose FE/INGAME .LAY) | [C] member dumps pass11/lays, sweep extract | ✓ compositor (+button states, FRAMES anim) | ✗ static frame only | ✓ `fe_screen_sweep` (every layout renders a real PNG; raw stubs legitimately texts=0 — gate exemption `feScreenName`) + shapes>0 FAIL regexes (FEFLOW05/FEFLOW11) | n/a | ✗ | `fe_screen_sweep`, `fe_screen_capture_menu`, `fe_screen_capture_team` |
| Tournament save/load slots | [C] SAVESLOT_WC98.md | ✓ WCT1 codec | ✓ tournament-load nav | ✓ fixture gates | n/a | ✗ | `fe_nav_tournament_save`, `fe_nav_tournament_load` (WC98_SAVE_SLOT=7) |
| Intro movies (.DCT INT_EASP/INT_FIFA) | [C] VIDEO_DCT_NOTES.md | ✓ | ✓ Esc skip | ✓ video_play lane | ✓ audio track | ✗ | app/video.cpp gates; `--no-intro` |
| Options (feflow15) | [C] LAY + key handler 0x44f22c (`quit`/`deft`/`back`/`ops2`) | ✓ FEFLOW15 composite, tab pages tbb1..4, row cursor over lst{tab} rows | ✓ hub fe15 wedge -> options; up/down rows, L/R cycles values (music/menu-sfx volume wired to settings.ini + live mixer gain); Esc back | ✓ `fe_flow_options` PASS "FEFLOW15: shapes=[1-9]" | ✗ | §4 rank 1 (in-game IGOPTN2 trio stays open) |
| In-game pause menu (IGMENU.LAY + IGQUESHN.LAY) | [C] tick=0x43FC40, tags 'gmnc'/'okay'; art FE/INGAME/INGAME.FSH bt*n/bt*d family | ✓ composite + FSH button shapes (LAY fname=null.fsh: scene installs art at init; RELEASED->*n, HILITED/PRESSED->*d) | ✓ Esc opens / closes-resume, arrows over {Resume,Quit}, Enter on quit -> IGQUESHN confirm ('quit'-tag path); sim frozen while open | ✓ `fe_igmenu` PASS "IGMENU: shapes=[1-9]" + _igmenu/_igquesn.png shots | ✗ | §4 rank 8 (runtime item captions from FC indices undecoded -> port domain {Resume,Quit}) |
| Save/load scene UI (Feflow89 entry) | [C] LAYs + slot buttons b000..b005 (USER/SLOTnn.SAV) | ✓ FEFLOW89 composite slot list | ✓ hub fe41 wedge -> rows; Enter = WCT1 load when a file exists; Esc back | ✓ `fe_flow_saveload` PASS "FEFLOW89: shapes=[1-9]" | ✗ | §4 rank 6 (write path stays tournament-save fixture) |
| Hub screen (Feflow06.lay, 13 wedges) + credits (feflow19 via cred wedge) | [C] §3 global-key table; buttons objecttype 6/32, help pane type=14 `help=NNN` FC captions | ✓ composite: hover/push/disabled shape states + FRAMES anim + help pane text; cursor cycles all 13 wedges (keyboard + mouse widgetHit) | ✓ fe07->friendly, fe11->team grid, fe32->practise, **fe33->feflow90 history board (+`fe91` chain to feflow91)**, quit->exit, fe15/fe41/cred -> views; fe90/fe04/fe02 disabled no-ops (documented) | ✓ `fe_hub` PASS "FEFLOW06: shapes=[1-9]" + `fe_credits` PASS "FEFLOW19: shapes=[1-9]" + `fe_history_select`/`fe_nav_history` PASS "FEFLOW90/91" | ✗ | §4 rank 10 partially landed (shootout/modem/network = no simulation in port; classic history presentation now open; repl highlights open) |
Honest summary: **impl** = 13 registry scenes (incl. feflow90/91 history boards + shared popup lane feflow10/37) + hub composite (Feflow06, 13 wedges live) + options/saveload/credits views + in-game pause (IGMENU/IGQUESHN) + IGCONSEL box; igoptns trio = captions lane (art proven absent §7); **cap** = all 156 layouts via the capture lane, every one asserted to render a real frame by `fe_screen_sweep` (visual only, no gate asserts pixel parity with Windows — cmp column is empty by design); in-game pack interactive = pause menu + option popups captions + IG confirm boxes.

## 4. Missing features, ranked (user-reachable in the original main flow first)

Each entry: what's missing → RE blocker. Blockers reference handler slots §1; "thunk decode pending" = slot body is an 8-byte jump thunk with no Ghidra function object, raw disassembly required (`menu7_handlers.txt` MISSING set).

1. **Options screen family** — feflow15 (4 tabs: match/gameplay/audio/visual) + in-game IGOPTN2/iga-igg-igmoptns + FEFLOW80 custom grid. **[LANDED 2026-11-07: hub fe15 wedge opens the FEFLOW15 composite, tab pages tbb1..4, row cursor + L/R value cycle (audio volumes wired to settings.ini/mixer); in-game trio still open.]** Blockers: type=17 option-list widget model (row/value pair painter), dynamic `textid` → FC-bank resolution for row captions; tick 0x44f804 decoded **[C 2026-11-17]** `analysis/pass20uitick` (options pump = widget hide/update branches, no key-token compares — keys live in init FUN_0044f22c, ported); in-game trio needs the ingame scene-handler cluster (`igoption`, §2b).
2. **Controller/2-player select** — feflow07/32/33 (match setup volume bars/balls) + feflow87/91 (controller grids, `anm*` animseq pictures, gravis numbers). **[LANDED partial 2026-11-08: feflow07 single-player match setup — hub fe07 wedge opens FEFLOW07 composite, ltnm/rtnm names + shdl/shdr shields over the lbNN/rbNN skill bars (FCENG Shooting/Passing/Speed/Defence/Overall), two-axis rows: UP/DOWN side cursor with yellow HILITEDSHAPEIDON box, LEFT/RIGHT fotl/fotr team cycle from the FCDB pool; Enter = 'fe09' forward edge. Gate `fe_setup07`.]** Remaining blockers for 32/33 + feflow87/91: tick/event thunk decode pending; animseq (type=3) player not implemented for FE shapes; controller-state source model.
3. **Match/stadium select + schedule flow** — feflow08/51 (stadium picker, ball set `mpx10`, HALF LENGTH), feflow20/21 (today's games, simulate). **[LANDED partial 2026-11-08: feflow08 stadium select opens from the friendly-config confirm ('fe08' tag @0x44C3D3) — France-map mpNN ball-marker tiles (`hbal` hilite), nslt/nsrt arrows cycle the tile ring, stme runtime stadium name + hint pane captions (FCENG 1692.. bank indices), sint/wint/hint decoration row; Esc = back. Gate `fe_stadium08` renders via the feflow07→feflow09→FEFLOW08 chain.]** feflow20/21 init+tick decoded **[C 2026-11-17]** `analysis/pass20uitick` (`simu` schedule grammar FUN_00484d1c/FUN_00485090 + "%s_%i" widget-name dispatch FUN_00467944; `stg1`..`stg3` stage captions DAT_0057d79c; scene-edge push FUN_004461b4 gated on DAT_0062fad0+0x5060). **[LANDED 2026-11-17: feflow20/21 interactive via the `--fe-wc-cfg` lane — tgt/tns/tsd rows + QF "a v b" pair strings + SF shield flags painted from the TourneyModel; `simu`→feflow86 confirm, `frwd`→feflow13, `stsc`→feflow82, `strk`→feflow81 edges wired with focus ring. Gates `fe_wc_cfg20`, `fe_wc_cfg21`.]** Remaining blockers for 51 interactive: schedule data binding to the tournament module (`TOURNAMENT_MODULE_WC98.md`); list widget type=9 painter; feflow51 tick decode pending.
4. **World-cup competition family** — feflow13 config, 16 lineups, 18 zones, 19 groups, 23/24 brackets, FEFLOW35 board (25-27+35 impl). **[C 2026-11-17]** feflow18/19 init + feflow13/20/21/23/24 init/tick decoded `analysis/pass20uitick`: key-token grammar ("fe06".."fe8a"/"rand"/"simu"/"simv"/"stnk"/"strj"/"strl"/"stsc"/"strt"/"sint"), `rand` staged randomize = counter DAT_00608bd4/8 cap 99 → FUN_004cf040 redraw + hide at 99 (port wcRandCount_ matches); feflow13 tick 0x44e578 = thunk → FUN_0044c6c0 (feflow09 pump) **[C]**. **[LANDED 2026-11-17: interactive family via the `--fe-wc-cfg` lane + `wcStack_` scene-switch state machine in `app/fe_flow.cpp` — NOTES §4 token→scene edge tables wired (18→fe19/fe11/fe15/fe41/fe42, 19→fe20/fe18 + staged `rand` 99, 20/21/23/24 →frwd→20/13, strk→81, stsc→82, simu→86 confirm; Esc pops the parent chain); composites paint zone/group/schedule/bracket/results/stats content from the TourneyModel (`g{g}t{s}`, tgt/tns/tsd, tm0N pair strings, qtm+shield flags, `mult` rows, t00N stat rows). Gates `fe_wc_cfg{18,19,20,21,23,24,35,81,82}`.]** Remaining blockers: feflow18 tick 0x4524f8, feflow19 tick 0x452884, feflow16 (0x4519c0/0x451aac) decode pending (animation/painter bodies; navigation fully decoded); navigation edges are code-side (shipped LAYs carry **no `Message=` keys**, no FEMSG.BIN exists — confirmed negative pass11); FC-bank captions resolved [C] so art/text parity is asset-complete.
5. **Records/trivia/history bookmark family** — feflow28-31, 34, 88 (+ type=35 multitext panes). **[LANDED 2026-11-17: full bookmark family interactive — hub ribbon icons jump pages; FEFLOW28 hub (shield + `tmnm` pool name), 29/30 head-to-head tables from `.RES` — grammar settled by the "sbar" branch of `FUN_0045721c` (records_dec10:149-192 + dec7:42-59): rows ride a pool-anchored running index (DAT_0060a06e, = offset+1 when offset==own) that skips the own slot (DAT_0060a070) ONLY when the window crosses it (fetch advance `idx+1`; on `idx==own`: bound+1, idx+2); row r valid while `DAT_0060a064 (= poolSize-1) >= bound + r`, else the clear branch NULs the sprintf'd string slots — the row paints blank, not zeros. The pre-pass "pair-indexed over pool-minus-own" map shifted the offset>own window by one row; corrected 2026-11-17, gate = selftest step `records-window` pinning own=0 / offset<own / offset==own / offset>own / crossing tail / clear-branch boundary. Row record slots `+0x34 = b0+b1+b2` (Games) / `+0x38 = b0` / `+0x3c = b1` / `+0x40 = b2` via `FUN_0049168e`; triple = `{won, drawn, lost}` for the row team (pass-12 venue-flag reading superseded — see `analysis/pass12tourn/ZWCUP_TRIPLE.md` SUPERSEDED note), 31 general + 34 trivia + 88 greats multitext panes from `FE/TEAMHIST.<lang>/.HIS` cells (`FUN_00489f58` list builder, 16/page `FUN_00488CF0`); `hd*` headers are runtime-localized FC-bank captions (LAY textids stale: 1318="Paris", 444-447="Continues"…), overlaid from bank 1272/799/1954/614/1009 = Opponent/Games/Won/Draws/Lost. Gates `fe_records28/29/30/31/34/88` + `fe_records_nav` (team-cursor ring) + `fe_records_window` (sbar row-window offset) + selftest `records-window`. Dossier `analysis/pass20records/` (decompiles copied 2026-11-17).]** The `.TXT` resolver blocker was a misread: trivia text lives in the `.HIS` multitext cells themselves (no tag indirection); bank sentinel stays FEFLOW00-only.
6. **Save/load scene UI** — feflow41/42 (+57 replay-load, Feflow89 team-save menu). **[LANDED partial 2026-11-07: hub fe41 wedge -> FEFLOW89 slot list, WCT1 load on select; write path stays the tournament-save fixture. LANDED full 2026-11-08: feflow41 LOAD + feflow42 SAVE-confirm scenes — `lstb` rows scan real `SLOT%02d.SAV` files from the XDG slot dir, captions decode path (`SLOT00 m1 g16 k16`), sbbg/sbar page slider; Enter on 42 writes via `saveTournament` + cros→tick flip (nvicons tic1); gates `fe_loadsave41`, `fe_savemgr42`; fepack lazy loader decoded pass-13 (`FEPACK_LOADER.md`).]**
7. **League management family** — feflow72/74/75/76/79/80 (+ FEFLOW43 superseded grid, 78 popup). **[LANDED 2026-11-07b..08: league widget painter (list type=9, row-label 42/44, slider 11) + interactive gates for feflow72/74/75/76/79/80 (§7); pn00..pn08 league-list row captions painted; FEFLOW43 standalone management grid landed 2026-11-08 — gate `fe_mgmt_grid43`, evidence `FEFLOW43: shapes=21 texts=91` (FCDB squad rows on vNNN slots, FC-bank position labels GK/SW/RB/LB/RM/LM/RF/LF/MF/FW, FORM/SUB/STRAT/CUST + AGG/POSIT/KICK/ATT.BIAS headers, four-row pan window with sbar thumb, L/R team cycle, Enter = saveTournament; §10).]** The attributes +/- state machine stays a code-side task; no format blockers remain.
8. **In-game FE popups** — IGMENU pause menu, IGQUESHN/MESG/IGCONSEL confirm boxes, IGHALF/IGSCSUM/IGFLSUM/IGSTATS stats screens, IGHILITE/IGHILSAV highlights browser, IGTEAM72-89 mirrors. **[LANDED partial 2026-11-07: livematch Esc opens the IGMENU composite (INGAME.FSH bt*n/bt*d art), {Resume,Quit} rows, Enter quit -> IGQUESHN confirm, sim frozen while open; runtime item captions undecoded. Confirm-box family landed same day via the shared popup lane (`--fe-popup`): FEFLOW10/37 FE modals + IGCONSEL/MESG/IGQUESHN grammar (§A null.fsh placeholders legit, INGAME.FSH btnn/btnd chrome via button->ingame alias); gates `fe_popup_ok/_yesno/_feflow86`, `fe_igconsel`, `fe_mesg`. Stats-screen family landed same day: IGSTATS opens from the IGMENU 'Stats' row over the live frame (two-page L/R stat grid, FC-bank captions + `TeamStats`/`possessionFrames` values), IGHALF opens at the half whistle (period-end arm grammar [C]), full time chains IGSCSUM -> Enter -> IGFLSUM (goal rows from `goalLog`); Esc dismisses to caller per the ESC=0x65 resume contract; gates `fe_igstats`, `fe_ighalf`, `fe_igscsum`, `fe_igflsum`. Highlights family landed 2026-11-07: IGMENU 'Highlights' row opens the IGHILITE browser over the `user/rplyindx.dat` rows (FC#899 title, slot readouts, btlt/btrt pages, slol/slor cycle; Enter = app/replay.cpp re-drive with state-hash parity), 'save' button -> IGHILSAV list + `recordToUserDir` write path; gates `fe_highlite_browser`, `fe_hilsav_list`. IGTEAM72..80/89 management mirrors + hub landed 2026-11-08 (commit e760202, gate `fe_mgmt_pages`; MANMARKS grid d1bd142 gate `fe_manmarks_grid`). Camera popups IGCAMERA/IGCAMSEL + practise pages IGPRAC1/PRACT1/PRACT2 landed 2026-11-08 (IgBoards kinds 16–20, pause 'C'/'P' chains; §10; gates `fe_camera_pages`, `fe_practise_pages`).]** Blockers: ingame scene-handler cluster (`ingame`/`igcntrol` ids); assets parsed/renderable (IGLAY dump §2b) — no format RE blocker remains, only code.
9. **Netplay/modem family** — Feflow02/03 lobbies, FEFLOW04 (mnmcon)/FEFLOW05 (MNMSETUP)/FEFLOW14 (mnmdial), IGCHAT, FEFLOW00 net-test stubs. Blockers: whole netplay subsystem (modem + IPX code) undecoded; edit-box type=10 text input in compositor; out of LAN scope — rank last for parity goal if LAN is descoped [H].
  10. **Presentation-only leftovers** — credits feflow94, icon/key popup feflow95 (type=18 checkbox widget), classics Feflow90/91 + shootout path (Feflow06 `fe33` button), highlights (`repl`) + credits buttons on the menu hub. **[LANDED partial 2026-11-07: hub screen with all 13 wedges live; cred wedge opens feflow19; feflow95 cheats screen (rank-8 slice); fe33 wedge -> feflow90 World-Cup history board + feflow91 chain (pass 13). Remaining no-ops: fe90 shootout, fe04 modem, fe02 network — the port has no simulation of those modes; feflow94 roll = capture lane only.]** Blockers: tick decode pending.
11. **Internal/debug-only** — feflow00/17/45/83/84/93, FEFLOW52/16 lineups, FEFLOW85 foot test, 26 stub members: no parity requirement; keep capture-lane coverage only.

## 5. Cross-checked counts (against source files)

- SCENE registry rows: **66** (`grep -c "^SCENE "` on `menu5_scenemap_windowobj.txt`); unique scene names **56**; 55 primary rows + 11 second-pass sites (block 0x4481b5-0x4483d3).
- LAYOUTS.BIG members: **101** (file header line 1; `grep -c "== MEMBER"` = 101) — includes `fe.lst` manifest, 32 single-widget stub members (`numobjects=1 [objects parsed: 1]`, grep count), 3 ig\*optns popups.
- IGLAY.BIG members: **30** = 29 .LAY + `ingame.lst`; loose FE/INGAME tree: **29** `.LAY` files (+ LST/FSH/IGFLAGS); every member byte-identical to its loose-tree counterpart (`cmp -s`: 29+LST identical, diff=0).
- Port interactive scenes (post-`65fbeda`): hub Feflow06, friendly/practise setup 09/50, team grid 11, league list 73, options feflow15, save-load Feflow89, credits feflow19, presentation boards 25/26/27/35, shared popup lane (FEFLOW10 ok/cancel + FEFLOW37 yes/no: blur+popp composite, nvicons button pair, FC captions + help pane, two-slot cursor, Enter/Esc close to caller; hub 'quit' wedge opens the confirm), in-game IGMENU+IGQUESHN overlay (`app/fe_flow.cpp` + `app/livematch.cpp`; gates §3), camera popups IGCAMERA/IGCAMSEL (IgBoards kinds 16/17, pause 'C' chain; gate `fe_camera_pages`), practise pages PRACT1/PRACT2/IGPRAC1 (kinds 18-20, pause 'P' page cycle + keeper toggles; gate `fe_practise_pages`), CONTROL1 controller page (options-chain popup lane, FC#258 caption + DAT_00632778 percent bars + live pad axis readout; gate `fe_control_page`). Capture lane covers every LAYOUTS.BIG member (`--fe-screen`).

## 6. Verification sweep + functional-parity evidence (run 2026-11-07, post commit `65fbeda`)

Method (goal §6): capture every screen via the proven `--fe-screen`/gate lanes, region-scan each PNG against its LAY-declared widget rectangles (`parity_cmp.py`: non-uniform pixels inside every rect = art drew; `fname=RECT fillcolour=` widgets legitimately render uniform color), compare against PAK/FSH asset sheets. Windows screenshots remain impossible on this host (Wine DirectDraw black [C]; QEMU present but Win9x guest needs licensed OS media) — reference basis stays asset-level, per goal §7 documentation rule.

- Full LAYOUTS.BIG sweep (complete 101-member dump, `wc98-extract`): **100 members rendered (all incl. lowercase stubs + ig\*optns trio), 89 region-clean**. The 11 flagged are each explained [C]: FEFLOW00 dev-stub anchors; help panes in FEFLOW10/37/76/78/86/feflow95 (dynamic FC captions on hover only); INGAME raw HUD anchor widgets; FEFLOW76/78 `s001` league list rows (rank-7 painter slice, in progress); igoptns trio `ok/canc/defa` (IGICON.FSH gap, row below). Raw evidence: `/tmp/sweep-out/report.txt`.
- Gate captures visual QA (viewed): hub (Feflow06 France '98 art + wedge rail + help-pane FC caption), options FEFLOW15 (`texts=42`, tabs tbb1-4, yellow help pane "Confirms selection and allows progression" = FC-bank stringAt), save/load Feflow89 ("Slot 0 (empty)"..`Slot 5 (empty)` bars + nav arrows), credits feflow19 (France map + stadium names + "Core Team …" caption rows), in-game pause over live match: IGMENU/HUD strip + IGQUESHN authentic prompt "Are you sure you wish to exit with points remaining?", popup lane captures (`/tmp/wc98port-out/pop10_fe00.png` red-italic "Are you sure?" + ok_1/canc nvicons pair + help pane stringAt(12); `pop37_fe00.png` "Are you sure you want to Quit?" (FC index 220) + yes/no pair + stringAt(13); `fes86_fe00.png` panel; `igcon_fe00.png` INGAME.FSH "OK" chrome over legit null boxes; `fesmesg_fe00.png` raw 85-line caption board) — all region scans PASS.
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

- **FEFLOW43 management grid** (standalone board; gate `fe_mgmt_grid43`, evidence `FEFLOW43: shapes=21 texts=91`): composite iNNn button chain + back collage via `FE/PACK/FEFLOW43`; FCDB squad rows (`#shirt name`) on vNNN slots, FC-bank position labels under the columns (GK 810 / SW 1801 / RB 1531 / LB 979 / RM 1534 / LM 982 / RF 1533 / LF 981 / MF 1070 / FW 751), header captions FORM/SUB/STRAT/CUST + AGG/POSIT/KICK/ATT.BIAS (FC 1379–1382) and CANCEL/SAVE/RESET TEAM/RESET ALL, aNNN card icons resolved from the `FE/PACK/FEFLOW73` PAK; UP/DOWN pans the four-row window with sbar thumb, L/R cycles team, Enter = `saveTournament`.
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

- **Baked name plate [C]**: the grey gradient bar behind the team name on FEFLOW07/32/33 is NOT a widget and not code-drawn — it is baked into the scene collage art (`FE/PACK/FEFLOW32/FEFLOW32.PAK` → `feflow32.fsh#back`, 640×480, plate at (97,200) 164×16; FEFLOW07 verified by extraction: `FE/PACK/FEFLOW07/FEFLOW07.PAK` → `feflow07.fsh#back`, same white→dark gradient strip at (82,179) 164×16 for `ltnm` and (340,179) 164×16 for `rtnm`; FEFLOW32 has no right-side plate at (340,200) — single-side design). `ltnm`/`rtnm` widgets carry `fname=null.fsh shapeID=null` (FIFAWC art has no plate shape: `F009.FSH` = single shape `f000` 125×120 shield). Composite must therefore NOT paint `ltnm`/`rtnm` — `drawLabel` paints their `textid=0` FC-bank string at the widget box, duplicating the overlay name (the widgets declare no `FILLCOLOUR`, so composite paints no box quad; the visible band is the baked plate itself, present from the first capture); the name is an overlay centred on the plate.
- **Name centre grammar [C]**: `ltnm textXOffset=82, alignment=3` = centre of the 164-wide plate (97+82=179). Port paints via `layoutTextCentered` at `o->x + width/2`, `o->y + height/2` in `drawSetup07`/`drawCfg32` `drawSide` (face 4, 14 px, rgb 235).
- **Row captions**: single centre-column set per screen, FC-bank indices 1691/1330/1741/573/1278 (Shooting/Passing/Speed/Defence/Overall). FEFLOW33 (two bars): one set centred at x=300 between the groups (FEFLOW07 grammar); the defect fixed here was the unconditional x=284 group painting a second set 16px left of the x=300 set (glyph smear), now gated `if (!twoAxis)`. FEFLOW32 (single group at x=394): captions left of the group at x=284.
- **Bar fill art**: `lbNN/rbNN` widgets carry `shapeid` (`sbarsr.fsh` bar0); fill = shape UV-clipped to `frac` (same as FEFLOW07 `paintBars`), dark track quad under it; flat green fill only when art is absent.
- **Whitelists**: `cfg32Visible_`/`cfg33Visible_` exclude `ltnm`/`rtnm` (overlay lane owns them); soup widgets (`objectname=null textid=429..433`) stay composite-hidden (null names are not whitelist entries).
- Gates `fe_cfg32`, `fe_cfg33`, `fe_cfg32_nav`, `fe_cfg33_side` pass; captures `/tmp/wc98port-out/cfg32c_fe00.png`, `cfg33e_fe00.png`, `s07c_fe00.png` viewed: names centred on baked plates, shields `f009.fsh#f000` + pool flag files, bar0-art fills, single caption set, yellow side ring. Clean-worktree verification (`fd80e1a` + this UI diff, full build + `ctest -j8`): **142/142 passed**.
