# Third-pass UI reconstruction validation (WC98) — 2026-11-18

Objective §8 evidence trail for the **third** user-interface pass. This file records *what was
verified*, by which gate, and what may honestly be claimed. Parity decisions and the screen
registry live in `MENU_PARITY.md`; the coordinate-space contract in `docs/architecture/RENDERER.md`;
release-facing summary in `CHANGELOG.md` (`[0.5.0]`).

## 1. Verification matrix

| Lane | Command | Result |
| --- | --- | --- |
| Full-layout capture sweep | `ctest -R fe_screen_sweep` (script `tests/scripts/fe_screen_sweep.sh`, enumerates `LAYOUTS.BIG` + `IGLAY.BIG` members via `wc98-extract` plus the 29 loose `FE/INGAME/*.LAY`) | **156 layouts, 0 failures** — every shipped menu layout renders a real frame through `--fe-screen` |
| Unit + interactive gates | `ctest -j 6` (release build, real GPU + extracted tree) | Clean-worktree `main`: **151/152** (152 tests incl. `db_json_valid`), sole stable failure `fe_replay57_enter` (replay round-trip hash divergence, in-flight kick-seam lane); `fe_hilsav_list` flakes on parallel timing, passes rerun. Working tree (user's uncommitted simulation lane): **150/152** — failures `test_sim_scenarios` + `self_test_gameplay`, not UI. Headless lane (`SDL_VIDEODRIVER=dummy` → `VK_EXT_headless_surface` offscreen render, `platform::dummyLane`): **150/152**, same failing set — gates + captures run on the real GPU without ever touching a display |
| Navigation smoke | `ctest -R "fe_nav_\|fe_render_tournament\|gamepad_smoke"` | **10/10 PASS** (practise / tournament / exhibition / league entry + load, offscreen tournament render, dummy video driver pad smoke) |
| Self-test | `ctest -R self_test_gameplay` → `/tmp/wc98port-out/selftest.json` | **28/28 steps PASS on the released commit** (same clean worktree as above). In the working tree, with the uncommitted simulation lane, it is **26/28** — the two failing steps are that lane (`pass: possessor pid=-1`, `kick seam`), so no UI step fails |
| Pointer contract | `ctest -R test_presentation` | PASS — 16:9 un-projection offset, letterbox-bar clamp, 4:3 identity, forward-map round trip |
| Mouse-driven UI gates | `ctest -R "fe_hub\|fe_igmenu\|fe_igoptns\|fe_igstats\|fe_ighalf"` | PASS, including `fe_igmenu` "igmenu: mouse activate row=6 code=6" (hover row 6, press activates IGSTATS through the forward map) |

## 2. Defect corrected this pass — pointer space (pass-44pointer)

The mouse was mapped by scaling the **whole window** into the virtual 640x480 canvas, while every
UI draw goes through `render::fit4x3(window)`. On non-4:3 windows the letterbox offset was folded
into the canvas (1280x720: every widget hit box 160 px inboard of the art), so hover/click landed
on the wrong hub wedge, pause-menu row or confirm-box button. Fixed by the single contract in
`presentation/` (`uiPointerFromWindow` / `uiWindowFromPointer`, both on `Window::pointSize()`, so
HiDPI scaling is covered too) — details in `MENU_PARITY.md` §34 and `RENDERER.md` "Window
coordinate space".

Pointer evidence, produced through the capture lane `--fe-pointer X,Y` (window-space coordinates
injected through the same mapping):

| Capture | Assertion |
| --- | --- |
| `hub_plain_fe00.png` vs `hub_pointer_fe00.png` | 956 differing pixels in exactly two clusters: the cursor sprite at the injected point, and the hovered wedge band (x 448..512, y 240..320 ⇒ virtual ≈ (493,240)) showing the `HILITED` wedge art. No unrelated region changed. |
| `exhibition_fe_exhibition_{kickoff,midplay,sprint}.png` | live-match presentation intact after the mapping change: scoreboard strip + clock, radar panel with player dots, shirt numbers, pitch lines |
| `league_fe_league_kickoff.png`, `league_fe_league_midplay.png` | league path reaches live play with the same mapping |

## 3. Live-match UI state wired to simulation (objective §6)

The in-match presentation reads match state, not presentation constants: scoreboard clock/team
names/scores and the minute chip from `sim::MatchState` (pass-26b/39igname), pause-menu row table
and `titl` caption from mode class + event state (pass-27c/35igtitl), IG boards (IGSTATS/IGHALF/
IGSCSUM/IGFLSUM) from live stats, bar captions from the LAY widget table (pass-43igfix). HUD values
are pinned by `test_hud`; board contents by the `fe_ig*` gates.

## 4. Boundaries — what cannot be claimed

- **No pixel-diff oracle.** Wine DirectDraw capture is dead (`WINE_ORACLE_WC98.md`), so no gate
  asserts pixel parity against Windows output; the `cmp` column in the registry stays empty by
  design. Byte/table-level RE plus telemetry is the substitute.
- **Legit render-zero screens**: `INGAME.LAY` frame is code-drawn (raw `shapes=0`); the retail
  `FEFLOW94` art pack is absent from the CD; netplay/modem/classics-only screens stay documented
  no-ops because the port has no such simulation.
- **Simulation lane** (`test_sim_*`, self-test `passing`/`animation` steps) is separate in-flight
  work; it is excluded from this pass's claims.

## 5. Release verification

See `CHANGELOG.md` `[0.5.0]` and the GitHub release body: the shipped AppImage is built by
`packaging/linux/deploy.sh` (pinned, digest-verified linuxdeploy/appimagetool/type2-runtime), with
the CI package job running the game `--smoke` lane and the offscreen launcher `--self-test` before
upload; release assets carry a `.sha256` sidecar and a `.version` marker.
