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
| 3 | 00447a32 | 0x56234c | feflow07 | 0x44a804 | 0x44ae94 | 0x44b114 | 0x44a770 | FEFLOW07.LAY | single-player match setup: team names, ball sprites, volume bars (`rb*/lb*`), qualification captions 432/433 | miss |
| 4 | 00447a58 | 0x562358 | feflow08 | 0x44b878 | 0x44bc1c | 0x44bc9c | 0x44b858 | FEFLOW08.LAY | match/stadium select: `stad` `sare` `nslt/nsrt` (next score), `sint/wint/hint`, ball picker `mpx10` | miss |
| 5 | 00447a7e | 0x562364 | feflow09 | 0x44c330 | 0x44c6c0 | 0x44c940 | 0x44c958 | FEFLOW09.LAY | friendly match config (8 rows × team pair, `ovr/lrp/rrp/alp/arp/arw/ctx`, "Final Match Qualifiers") | **impl** (`fe_flow_setup09`, `fe_nav_exhibition`) |
| 6 | 00447aa4 | 0x562370 | feflow10 | 0x44d278 | 0x44d34c | 0x44d958 | — | FEFLOW10.LAY | modal confirm popup (blur/popp, ok/cancel, txt0-4) | miss |
| 7 | 00447ac3 | 0x56237c | feflow11 | 0x44d9c8 | 0x44dda0 | 0x44dfa8 (thunk→`FUN_0044a210`) | — | FEFLOW11.LAY | World-Cup team pick grid: map, big flag `tmfg`, group cells `tc/tt/ta/stf*`, scroll `sbar` | **impl** (`fe_render_tournament`, `fe_screen_capture_team`) |
| 8 | 00447ae2 | 0x562388 | feflow13 | 0x44e440 | 0x44e578 | 0x44e580 (thunk→`FUN_0044a210`) | — | FEFLOW13.LAY | World-Cup competition config ("quit the game"/"quit world cup" buttons, same row grammar as feflow09) | cap |
| 9 | 00447b06 | 0x562394 | feflow15 | 0x44f22c | 0x44f804 | 0x44fa7c | — | feflow15.lay | options screen, 4 tabs (`tab1-4`,`tbb1-4`,`lst1-4` = Match/Game play/Audio…, `save`/`deft`/`back`/`fwrd`) | cap (interactive **miss**) |
| 10 | 00447b25 | 0x5623a0 | feflow16 | 0x4519c0 | 0x451aac | — | — | FEFLOW16.LAY | lineups grid (lp/rp/ls/rs player rows, flags, `ftix`) — internal lineup editor | cap |
| 11 | 00447b3d | 0x5623ac | feflow18 | 0x452328 | 0x4524f8 | 0x452560 | — | FEFLOW18.LAY | league team/zone pick (32 `tm`+`gm` zone slots, `rand` randomize) | cap |
| 12 | 00447b5c | 0x5623b8 | feflow19 | 0x452688 | 0x452884 | 0x4528e4 | — | FEFLOW19.LAY | tournament group assignment (groups A-H names + 4 team slots each, `rand`) | cap |
| 13 | 00447b7b | 0x5623c4 | feflow20 | 0x452b00 | 0x452f34 | 0x45314c | 0x452af0 | FEFLOW20.LAY | "today's games" schedule day (`tody`, date fields, `simu` simulate, next-game lineup/stadium/score) | cap |
| 14 | 00447ba1 | 0x5623d0 | feflow21 | 0x453f58 | 0x454298 | 0x453f50 | — | FEFLOW21.LAY | group standings mini-board (tm×4 + qf slots, simulate/stats buttons) | cap |
| 15 | 00447bc0 | 0x5623dc | feflow23 | 0x4543dc | 0x4545fc | 0x454658 | 0x4543d0 | FEFLOW23.LAY | knockout bracket R16 (tm×16, qtm×8, hilite bars/brackets, `save`) | cap |
| 16 | 00447be6 | 0x5623e8 | feflow24 | 0x454e0c | 0x45502c | 0x454658 | 0x454e00 | FEFLOW24.LAY | knockout bracket variant (tm×16 + qf flags) | cap |
| 17 | 00447c0c | 0x5623f4 | feflow25 | 0x4554ec | 0x45570c | 0x455768 | 0x4554e0 | FEFLOW25.LAY | quarterfinal board (tm×8 entrants, qtm×4 SF slots) | **impl** (`fe_render_present` family; `--fe-present`) |
| 18 | 00447c32 | 0x562400 | feflow26 | 0x455ae0 | 0x455d28 | 0x455d80 | 0x455ad0 | FEFLOW26.LAY | semifinal board (+ trophy slots `wcup`/`wcps`) | **impl** (`--fe-present`) |
| 19 | 00447c58 | 0x56240c | feflow27 | 0x455fd0 | 0x456030 | 0x456084 | 0x45626c | FEFLOW27.LAY | full bracket + champion slot `chmp` + 3rd-place match (`3tm/3mat`) | **impl** (`--fe-present`) |
| 20 | 00447c7e | 0x562418 | feflow28 | 0x456cd8 | 0x456fa8 | 0x457038 | — | FEFLOW28.LAY | team-history bookmark hub (shield, team name, general/match/world-cup/greats icons) | cap |
| 21 | 00447c9d | 0x562424 | feflow29 | 0x45721c | 0x4574ac | 0x45750c | — | FEFLOW29.LAY | records "match results" bookmark page (ribbon, scroll bar, top/second… columns) | cap |
| 22 | 00447cbc | 0x562430 | feflow30 | 0x457574 | 0x45767c | 0x4576c0 | — | FEFLOW30.LAY | records "world cup results" bookmark page (same grammar) | cap |
| 23 | 00447cdb | 0x56243c | feflow31 | 0x4576f8 | 0x457830 | 0x4578fc | — | FEFLOW31.LAY | records "general" page (multitext pane `mtex`, team name) | cap |
| 24 | 00447cfa | 0x562448 | feflow32 | 0x45791c | 0x457cb4 | 0x457e4c | 0x457910 | FEFLOW32.LAY | single-player match config (volume bars left, footballs, shooting/passing captions) | cap |
| 25 | 00447d20 | 0x562454 | feflow33 | 0x457e68 | 0x457f8c | 0x457fa4 | 0x457e60 | FEFLOW33.LAY | two-player match config (both team names, ball shadows, volume bars) | cap |
| 26 | 00447d46 | 0x562460 | feflow34 | 0x457fd8 | 0x45822c | 0x4582e8 | — | FEFLOW34.LAY | trivia bookmark page (pane `mttr`, prev/next buttons) | cap |
| 27 | 00447d65 | 0x56246c | feflow35 | 0x45838c | 0x458714 | 0x45889c | — | Feflow35.lay (+ FEFLOW35.LAY board variant) | group standings table: `gpnm` group name, rows `ssn/ssp/ssw/ssd/ssl/ssf/ssa/sst`, stats buttons | **impl** (`fe_render_present`: "FE-PRESENT FEFLOW35"; `--fe-pick 35`) — uppercase board variant: cap |
| 28 | 00447d84 | 0x562478 | feflow37 | 0x45922c | 0x4592f0 | 0x459220 | — | FEFLOW37.LAY (authored `Feflow86.lay`) | yes/no confirm popup (blur, popp, no/yes, txt×3) | miss |
| 29 | 00447da3 | 0x562484 | feflow41 | 0x459420 | 0x4597ac | **0x0** | — | FEFLOW41.LAY | LOAD game list (blur, `lstb`+slider, "LOAD game" caption) | miss (port has WCT1 save slots, no scene UI) |
| 30 | 00447dc2 | 0x562490 | feflow42 | 0x45979c | 0x4597ac | 0x459b98 | — | FEFLOW42.LAY | SAVE game list (tick/cross, `lstb`+slider) | miss |
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
| 43 | 00447f55 | 0x56252c | feflow81 | 0x4647a8 | 0x464878 | 0x464790 | 0x464798 | FEFLOW81.LAY | results page (multitext pane `mult`, scroll bar, quit/back) | cap |
| 44 | 00447f7b | 0x562538 | feflow82 | 0x465854 | 0x465b0c | 0x46584c | — | Feflow82.lay | player performance stats (rows `t000-t008` type=44, stat buttons `mbt0-2`, pair headers) | cap |
| 45 | 00447f9a | 0x562544 | feflow83 | 0x467270 | 0x467274 | — | — | FEFLOW83.LAY | lineups + score grid (lp/rp/ls/rs, `llls/rrrs` scores with inc buttons) — dev/debug screen **[H]** | cap |
| 46 | 00447fb2 | 0x562550 | feflow84 | 0x467320 | 0x467438 | — | — | FEFLOW84.LAY | lineups + score grid variant (same debug pattern) **[H]** | cap |
| 47 | 00447fca | 0x56255c | feflow86 | 0x467770 | 0x4677b4 | 0x467760 | — | FEFLOW86.LAY | confirm popup (txt0-2, fe09 button, footix) | cap |
| 48 | 00447fe9 | 0x562568 | feflow87 | 0x467970 | 0x467ab0 | 0x467ab8 | — | FEFLOW87.LAY | controller-select grid (4 slots, controller pictures `anm*`, gravis button numbers, "Num Team Mates") | cap |
| 49 | 00448008 | 0x562574 | feflow88 | 0x467af0 | 0x467c60 | 0x467d98 | — | FEFLOW88.LAY | "all time greats" records page (two multitext panes `mtxl/mtxr`, titles 4311/4376) | cap |
| 50 | 00448027 | 0x562580 | feflow89 | 0x467da0 | 0x4683a4 | 0x4685b4 | — | Feflow89.lay | team save/load menu (6 buttons `b000-b005` incl. "Load" 994, reset-all/reset-team) | cap |
| 51 | 00448046 | 0x56258c | feflow90 | 0x468728 | 0x4688e8 | 0x468924 | — | Feflow90.lay | classic (legend) match setup: year slider `year/yslb`, classic balls `bal1/bal2`, qualification captions | cap |
| 52 | 00448065 | 0x562598 | feflow91 | 0x468b50 | 0x468c68 | 0x468d28 | — | Feflow91.lay | controller-select grid for classics (same grammar as feflow87) | cap |
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
| 51 | 0044834d | 0x56258c | feflow90 | — | — | — | — | (as #51) | remap site | cap |
| 56 | 004483d3 | 0x5625c8 | **feflow17** | — | — | — | — | FEFLOW17.LAY | dev options list (`opt0-7`, `dop0-3`; captions "Automatic Crosses", "Audio/Visual Options" 267) — internal debug menu **[C]** authored feflow17.lay | cap |

Counts **[C]** (verified against `menu5_scenemap_windowobj.txt`): 66 SCENE rows = 55 primary registrations (#1–#55) + 11 second-pass sites in the 0x4481b5-0x4483d3 block; remap rows reuse the primary row's `#`. Unique scene names: **56** — feflow17 is registered only at site 0x4483d3 (no slot record in this scan); all other remap sites re-register scenes that carry their own primary row.

Registry-internal totals: **impl** = 9 unique scenes interactive — feflow06 (main menu), 09, 11, 25, 26, 27, 35, 50, 73; **miss** = 6 — feflow07, 08, 10, 37, 41, 42 (no renderable lane wired: popups/scene UIs); **cap** = 41 — everything else, renderable via the `--fe-screen` lane only (includes stub scenes feflow00/feflow56 and feflow15, whose interactive option-list logic is missing).

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
| FEFLOW10.LAY | 11 | confirm popup: blur+popp, ok×2/cancel, txt0-4 | miss (popup lane not implemented) |
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
| FEFLOW37.LAY | 8 | yes/no popup authored `Feflow86.lay` copy (blur, no/yes, txt×3) | miss (popup lane) |
| FEFLOW38–40.LAY | 1 | null stubs | cap (stubs) |
| FEFLOW41.LAY | 6 | LOAD game list: blur+overlay, "LOAD game", quit button, `lstb`+slider; caption id6="…and icons" leak | miss (no save UI scene) |
| FEFLOW42.LAY | 7 | SAVE game list ("SAVE game", tick/cross buttons, `lstb`) | miss |
| FEFLOW43.LAY | 101 | full team-management grid: player overlays/cards, SMALL STATUS, positions/Sub captions (979 "Left Back", 1783 "Sub"), reset-all/save — authored Feflow43.lay; **not in registry** → superseded by feflow73-80 family **[H]** | miss (unregistered) |
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
| FEFLOW86.LAY | 7 | confirm popup (txt0-2, fe09, footix; authored D:\wc98_pc…Feflow86.lay) | cap |
| FEFLOW87.LAY | 57 | controller-select grid: 4 slot overlays + controller pictures + gravis button numbers + "Num Team Mates" (4594) | cap |
| FEFLOW88.LAY | 21 | "all time greats" page (`mtxl/mtxr` panes, titles 4311/4376) | cap |
| FEFLOW97.LAY | 1 | null stub authored `feflow01.lay` (duplicate of #2) — the number is an authoring artifact **[H]** | cap (stub) |
| Feflow02.lay | 14 | netplay lobby: machine list `glst`, create/join buttons, machine-name box; **not in registry** → netplay family | miss (unregistered) |
| Feflow03.lay | 14 | netplay chat/lobby: chat boxes, eject button, machine list — not registered | miss (unregistered) |
| Feflow06.lay | 22 | main-menu hub art: friendly/WC98/practise/shootout/classic big buttons (`fe07/fe11/fe32/fe33/fe90` type=32), modem/netw, quit/options/team-mgmt/load/highlights(`repl`)/credits(`cred`) | **impl** (menu entries; highlights+credits buttons not wired) |
| Feflow35.lay | 59 | group standings table: `gpnm`, ssn/ssp/ssw/ssd/ssl/ssf/ssa/sst rows ×4, stats buttons — the standings member used by `--fe-present` | **impl** |
| Feflow82.lay | 31 | see row "FEFLOW82→Feflow82.lay" (same member; registry name feflow82) | cap |
| Feflow89.lay | 26 | team save/load menu: buttons b000-b005 ("New Controller Pak inserted." 1210, "Load" 994), reset-all/reset-team/save-all | cap (save-slot UI **miss** in port) |
| Feflow90.lay | 42 | classic match setup: year slider + yslb/ysrb arrows, classic balls `bal1/bal2`, volume bars | cap |
| Feflow91.lay | 57 | controller-select grid (classic family; comment "goes to feflow90") | cap |
| Feflow93.lay | 60 | lineups+score debug grid (classic family) **[H]** | cap |
| INGAME.LAY | 5 | in-game menu frame: `titl/bxot/tlft/blft` shapes + `menu` type=16 widget; pairs with FE/INGAME/IGMENU.LAY overlay | miss (in-game popup not implemented) |
| fe.lst | n/a | member name list file, NOT a .LAY (size 3953) — LAYOUTS.BIG manifest | n/a |
| feflow15.lay | 27 | options screen: tabs 1-4 (`tab*`/`tbb*`), option lists `lst1-4` type=17, save/default/back/fwd; captions "Game play options"/"audio options" comments | cap (interactive **miss**) |
| feflow94.lay | 33 | credits roll: cr01-cr32 text rows at fixed slots | cap |
| feflow95.lay | 29 | icon/key-assignment popup: box1-8 + btn0-7 checkboxes, "…and icons" title, yes button (authored path = feflow94.lay — header artifact) | cap |
| igaoptns.lay | 12 | in-game audio options tab: list `lst` type=17 + slider, ok/cancel/default buttons, help pane; captions ids 1174/1175/1144 (bank noise — dev strings) | cap (in-game **miss**) |
| iggoptns.lay | 12 | in-game gameplay options tab (same grammar, `go_g/go_a/go_c` buttons) | cap (in-game **miss**) |
| igmoptns.lay | 12 | in-game match options tab (id1171 "Moves highlight within bookmark group") | cap (in-game **miss**) |

Counts verified: 101 members total = 9 interactive impl, ~30 stub/unused, remainder cap/miss as listed. Registry↔member mapping is name-for-name (`feflowNN` → `FEFLOWNN.LAY`, case variants per table). Members not registered in the scene registry (43, 57, 78, 85, Feflow02/03, FEFLOW04/14, credits-adjacent) are legacy netplay/team-management-era assets **[H]** where noted.

## 2b. IGLAY.BIG members (30) — in-game screen pack

Dump: `wc98-extract FE/PACK/LAYOUTS/IGLAY.BIG` → "1 files, 30 BIG members" (`/tmp/pass12inv/iglay/IGLAY.BIG.d`). Grammar = raw LAY text (`[object N] objecttype/objectname`, same as §3 of the companion doc). `textid=0` is dynamic (runtime-resolved from IG banks); captions are not statically resolvable in this pack.

| member | objs | purpose **[C]** (authored path + widgets) | port |
|---|---|---|---|
| CONTROL1.LAY | 27 | in-game controller scheme page: `ctx0-3` config texts, gravis button numbers, team name texts | miss |
| IGCAMERA.LAY | 19 | camera options popup (box/typ/flg/nmb triples, menu/back/forw) | miss |
| IGCAMSEL.LAY | 13 | camera-select list popup (menu widget) | miss |
| IGCHAT.LAY | 16 | chat overlay: input line + `chat/cha2/cha3` display boxes (netplay) | miss |
| IGCONSEL.LAY | 2 | confirm-OK box (`okay`) | miss |
| IGFLOW86.LAY | 7 | in-game confirm popup (`fe09`, txt0-2; authored `IGflow86.lay`) — mirrors FEFLOW86 | miss |
| IGFLSUM.LAY | 45 | full-match summary screen (typ/box stat grid) | miss |
| IGHALF.LAY | 57 | half-time summary screen (stat grid, list widgets) | miss |
| IGHILIT1.LAY | 1 | single box overlay authored `igmenu.lay` — header artifact; highlight hilite pane | miss |
| IGHILITE.LAY | 19 | highlights browser: rewd/paus/play/fast/slol/slor transport buttons, boxes | miss |
| IGHILSAV.LAY | 22 | highlights save list (`lst1`, up/down) | miss |
| IGMENU.LAY | 14 | in-game pause menu: box1-5/typ/flg/nmb chrome + `menu` type=42 widget, name text — the popup the port lacks (Esc quits match directly) | miss (**key parity gap**) |
| IGMSG.LAY | 17 | message popup authored `igqueshn.lay`; ok button, lines (`bo25`) | miss |
| IGOPTN2.LAY | 21 | in-game options: tabs 1-4 + tab-button areas, option lists `lst1-4` type=17, back/fwrd; `textID=5`, help keys (78/81) — the ig\*optns trio lives here **[C]** captions "Match"/"Game play"/tab comments | miss |
| IGPRAC1.LAY | 36 | practise-mode setup/stats popup | miss |
| IGPRACT1.LAY | 37 | practise stats page (back/forw) | miss |
| IGPRACT2.LAY | 22 | practise stats page variant | miss |
| IGQUESHN.LAY | 20 | yes/no question popup (`okay`+`canc`, lines) | miss |
| IGSCSUM.LAY | 36 | score-summary list (lst1-3, up/down) | miss |
| IGSTATS.LAY | 67 | in-game stats screen (stat grid ×33 texts, team columns) | miss |
| IGTEAM72.LAY | 49 | in-game league match setup mirror of FEFLOW72 (`t100-112` igm titles, form rows) | miss |
| IGTEAM73.LAY | 91 | in-game squad list mirror of FEFLOW73 (lin×10 rows, sbar, t44×15) | miss |
| IGTEAM74.LAY | 46 | management grid mirror (yel/red cards ×5 each) | miss |
| IGTEAM75.LAY | 33 | lineup editor mirror (`ptop` player block s001/tt00) | miss |
| IGTEAM79.LAY | 64 | squad/formation mirror ("attack bias slide bar" comment) | miss |
| IGTEAM80.LAY | 57 | custom-mode grid mirror (yel/red, players block, help= keys) | miss |
| IGTEAM89.LAY | 17 | save/load menu mirror of Feflow89 (b000-b005 buttons, gofd/gobk) | miss |
| MANMARKS.LAY | 59 | player-marker management screen (yel/red rows + players block) | miss |
| MESG.LAY | 10 | message box yes/no authored `C:\fifa98.pc\source\FE.OLD\LAYOUTS\mesg.lay` — FIFA'98-era popup kept for in-game messages | miss |
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
| Hub screen (Feflow06.lay, 13 wedges) + credits (feflow19 via cred wedge) | [C] §3 global-key table; buttons objecttype 6/32, help pane type=14 `help=NNN` FC captions | ✓ composite: hover/push/disabled shape states + FRAMES anim + help pane text; cursor cycles all 13 wedges (keyboard + mouse widgetHit) | ✓ fe07->friendly, fe11->team grid, fe32->practise, quit->exit, fe15/fe41/cred -> views; fe33/fe90/fe04/fe02 disabled no-ops (documented) | ✓ `fe_hub` PASS "FEFLOW06: shapes=[1-9]" + `fe_credits` PASS "FEFLOW19: shapes=[1-9]" | ✗ | §4 rank 10 partially landed (shootout/classic/modem/network = no simulation in port; repl highlights open) |
Honest summary: **impl** = 9 registry scenes + hub composite (Feflow06, 13 wedges live) + options/saveload/credits views + in-game pause (IGMENU/IGQUESHN); **cap** = all 156 layouts via the capture lane, every one asserted to render a real frame by `fe_screen_sweep` (visual only, no gate asserts pixel parity with Windows — cmp column is empty by design); in-game pack interactive = 1/29 (pause menu).

## 4. Missing features, ranked (user-reachable in the original main flow first)

Each entry: what's missing → RE blocker. Blockers reference handler slots §1; "thunk decode pending" = slot body is an 8-byte jump thunk with no Ghidra function object, raw disassembly required (`menu7_handlers.txt` MISSING set).

1. **Options screen family** — feflow15 (4 tabs: match/gameplay/audio/visual) + in-game IGOPTN2/iga-igg-igmoptns + FEFLOW80 custom grid. **[LANDED 2026-11-07: hub fe15 wedge opens the FEFLOW15 composite, tab pages tbb1..4, row cursor + L/R value cycle (audio volumes wired to settings.ini/mixer); in-game trio still open.]** Blockers: type=17 option-list widget model (row/value pair painter), dynamic `textid` → FC-bank resolution for row captions, tick 0x44f804 thunk decode pending; in-game trio needs the ingame scene-handler cluster (`igoption`, §2b).
2. **Controller/2-player select** — feflow07/32/33 (match setup volume bars/balls) + feflow87/91 (controller grids, `anm*` animseq pictures, gravis numbers). Blockers: tick/event thunk decode pending; animseq (type=3) player not implemented for FE shapes; controller-state source model.
3. **Match/stadium select + schedule flow** — feflow08/51 (stadium picker, ball set `mpx10`, HALF LENGTH), feflow20/21 (today's games, simulate). Blockers: tick decode pending; schedule data binding to the tournament module (`TOURNAMENT_MODULE_WC98.md`); list widget type=9 painter.
4. **World-cup competition family** — feflow13 config, 16 lineups, 18 zones, 19 groups, 23/24 brackets, FEFLOW35 board (25-27+35 impl). Blockers: all these scene ticks are thunks pending decode; navigation edges are code-side (shipped LAYs carry **no `Message=` keys**, no FEMSG.BIN exists — confirmed negative pass11); FC-bank captions resolved [C] so art/text parity is asset-complete.
5. **Records/trivia/history bookmark family** — feflow28-31, 34, 88 (+ type=35 multitext panes). Blockers: `.TXT`/multitext resource resolver absent (bank sentinel `=TXT NOT AVAIL=` [C] FEFLOW00); ribbon/hilite screen-key painting (`HILITEDSHAPEIDON/OFF`, companion §2).
6. **Save/load scene UI** — feflow41/42 (+57 replay-load, Feflow89 team-save menu). **[LANDED partial 2026-11-07: hub fe41 wedge -> FEFLOW89 slot list, WCT1 load on select; write path stays the tournament-save fixture.]** Port ships WCT1 slot codec with no menu surface. Blockers: window-object lazy loader `"fepack\%s.all"` path (`FUN_0047a12c`, companion §2), list box type=9 + slider painter, save-slot record parser beyond tournament slots.
7. **League management family** — feflow72/74/75/76/79/80 (+ FEFLOW43 superseded grid, 78 popup). Blockers: kt/menu list widgets (objecttype 42/44) not in the compositor; per-row dynamic painter templates (`"tm%02d"` family exists [C] companion §5, row model for grids open); attributes +/- state machine.
8. **In-game FE popups** — IGMENU pause menu, IGQUESHN/MESG/IGCONSEL confirm boxes, IGHALF/IGSCSUM/IGFLSUM/IGSTATS stats screens, IGHILITE/IGHILSAV highlights browser, IGTEAM72-89 mirrors. **[LANDED partial 2026-11-07: livematch Esc opens the IGMENU composite (INGAME.FSH bt*n/bt*d art), {Resume,Quit} rows, Enter quit -> IGQUESHN confirm, sim frozen while open; runtime item captions undecoded.]** Blockers: ingame scene-handler cluster (`ingame`/`igcntrol` ids); assets parsed/renderable (IGLAY dump §2b) — no format RE blocker remains, only code.
9. **Netplay/modem family** — Feflow02/03 lobbies, FEFLOW04 (mnmcon)/FEFLOW05 (MNMSETUP)/FEFLOW14 (mnmdial), IGCHAT, FEFLOW00 net-test stubs. Blockers: whole netplay subsystem (modem + IPX code) undecoded; edit-box type=10 text input in compositor; out of LAN scope — rank last for parity goal if LAN is descoped [H].
10. **Presentation-only leftovers** — credits feflow94, icon/key popup feflow95 (type=18 checkbox widget), classics Feflow90/91 + shootout path (Feflow06 `fe33` button), highlights (`repl`) + credits buttons on the menu hub. **[LANDED partial 2026-11-07: hub screen with all 13 wedges live (hover/push/disabled states, help pane, FRAMES anim); cred wedge opens the feflow19 roll; fe33/fe90/fe04/fe02 paint DISABLEDSHAPE* and Enter is a documented no-op — the port has no shootout/classic/netplay simulation.]** Blockers: tick decode pending; type=18 painter; small — mostly one-pass work after rank 1-2 infrastructure.
11. **Internal/debug-only** — feflow00/17/45/83/84/93, FEFLOW52/16 lineups, FEFLOW85 foot test, 26 stub members: no parity requirement; keep capture-lane coverage only.

## 5. Cross-checked counts (against source files)

- SCENE registry rows: **66** (`grep -c "^SCENE "` on `menu5_scenemap_windowobj.txt`); unique scene names **56**; 55 primary rows + 11 second-pass sites (block 0x4481b5-0x4483d3).
- LAYOUTS.BIG members: **101** (file header line 1; `grep -c "== MEMBER"` = 101) — includes `fe.lst` manifest, 32 single-widget stub members (`numobjects=1 [objects parsed: 1]`, grep count), 3 ig\*optns popups.
- IGLAY.BIG members: **30** = 29 .LAY + `ingame.lst`; loose FE/INGAME tree: **29** `.LAY` files (+ LST/FSH/IGFLAGS); every member byte-identical to its loose-tree counterpart (`cmp -s`: 29+LST identical, diff=0).
- Port interactive scenes (post-`65fbeda`): hub Feflow06, friendly/practise setup 09/50, team grid 11, league list 73, options feflow15, save-load Feflow89, credits feflow19, presentation boards 25/26/27/35, in-game IGMENU+IGQUESHN overlay (`app/fe_flow.cpp` + `app/livematch.cpp`; gates §3). Capture lane covers every LAYOUTS.BIG member (`--fe-screen`).

## 6. Verification sweep + functional-parity evidence (run 2026-11-07, post commit `65fbeda`)

Method (goal §6): capture every screen via the proven `--fe-screen`/gate lanes, region-scan each PNG against its LAY-declared widget rectangles (`parity_cmp.py`: non-uniform pixels inside every rect = art drew; `fname=RECT fillcolour=` widgets legitimately render uniform color), compare against PAK/FSH asset sheets. Windows screenshots remain impossible on this host (Wine DirectDraw black [C]; QEMU present but Win9x guest needs licensed OS media) — reference basis stays asset-level, per goal §7 documentation rule.

- Full LAYOUTS.BIG sweep: **85 members rendered, 78 region-clean**; the 7 flagged are all explained (FEFLOW00 dev stub anchors; FEFLOW10/37/86 help panes = dynamic FC captions only on hover; INGAME raw HUD anchor widgets; FEFLOW76/78 `s001` league list rows = rank-7 painter gap, slice in progress). Raw evidence: `/tmp/sweep-out/report.txt`.
- Gate captures visual QA (viewed): hub (Feflow06 France '98 art + wedge rail + help-pane FC caption), options FEFLOW15 (`texts=42`, tabs tbb1-4, yellow help pane "Confirms selection and allows progression" = FC-bank stringAt), save/load Feflow89 ("Slot 0 (empty)"..`Slot 5 (empty)` bars + nav arrows), credits feflow19 (France map + stadium names + "Core Team …" caption rows), in-game pause over live match: IGMENU/HUD strip + IGQUESHN authentic prompt "Are you sure you wish to exit with points remaining?" — all region scans PASS.
- Full suite at landing: **ctest 69/69 green** (incl. `fe_hub`, `fe_flow_options`, `fe_flow_saveload`, `fe_credits`, `fe_igmenu`, `test_fescenes` wedge-mapping pins; pre-existing FE nav gates unmodified).
- Functional lanes [C]: mouse = `widgetHit` POINTS-polygon/box hit-testing wired into hub/options/save-load pointer activation (SDL LMB); keyboard = arrows/Enter/Esc per tag model (`quit`, `feNN`, 'back'/'deft'); controller = original polls legacy WINMM joystick (`joyGetNumDevs/joyGetPosEx`, Gravis/Sidewinder name tables [C] exe import scan) — port maps SDL gamepad buttons to the same arrow/Enter/Esc codes; audio = NO menu click sounds exist in the original [C AUDIO_RUNTIME], FE music .ASF loop + ambient bank already wired, options volume rows drive settings.ini + live mixer gain.
- Resolution/timing: 640×480 virtual canvas, aspect-correct letterbox scaling (gates capture at native 640×480); animation advances on a millisecond clock (`animMs`), independent of render resolution/vsync; frame pacing per LAY FRAMES= ranges.
- Captured IG option popups (`--fe-screen igaoptns/iggoptns/igmoptns`: shapes=5, texts=46/33/37): region scan flags `ok/canc/defa` + help pane rects **black** (mean 0) — the buttons reference cross-file art `vpath=..\ingame\ fname=igicon.fsh sname=chck`; ScreenArt resolves FEFLOW*.fsh/FE PAKs (+INGAME.FSH via vpath-stem fix), IGICON.FSH not in the set → shape missing. Fix = add `..\ingame\igicon.fsh` to the art resolution list (small, queued with rank-8 slice).
