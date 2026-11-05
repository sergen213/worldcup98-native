# WINE_ORACLE_WC98 — running the original FIFAWC.EXE under Wine on Linux (pass 7)

Ground-truth runtime capture of the **original** Windows game (`FIFAWC.EXE`, Watcom PE32,
ImageBase `0x400000`) under Wine, for the native port's A/B reference. Pass-7 evidence lives in
`/mnt/1st4TB/WorldCup98/analysis/pass7/wine_*`; the tools are `wc98_dumps2tables.py`,
`wc98_slots.py`, `wc98_dumpcmds.txt` (all in that directory). Nothing was committed.

**Outcome (honest):** the game **boots and runs indefinitely** under Wine on a 640x480/16-bit X
display — DirectDraw init succeeds, the render loop blits live 640x480 RGB565 frames every frame,
`USER/options.sav` is written, `d3da.dll` (THRASH) and `eacsnd.dll` load at their authored bases.
The **FE/match visuals were NOT reached**: the presented frames are uniformly black, so no title,
menu, or kickoff screenshots exist, and the intended *idle-vs-kickoff* task-slot diff was not
obtained (§6). What **was** obtained is a live DGROUP oracle: §4 dumps, plus two structural facts
that only runtime can give — (a) the pass-6 dispatch tables are **never patched at runtime**
(byte-identical to the on-disk image), and (b) `DAT_00632a80` is a **runtime-filled THRASH HAL
pointer table** aimed at the registry-selected driver image.

## 1. Environment (versions, prefix)

| item | value | evidence |
|---|---|---|
| Wine | `wine-11.21`, `/usr/bin/wine`, `/usr/bin/winedbg` | `wine --version` |
| Prefix | `~/.wine-wc98` (created by `wineboot -i`; FIFAWC runs as **wow64 32-bit**) | winedbg report: “page fault … in wow64 32-bit code” (`/tmp/wc98_seh7.log`) |
| Game copy | `~/.wine-wc98/drive_c/games/wc98` = `cp -a /mnt/1st4TB/WorldCup98/WorldCup98_Extracted` (source tree never written) | this doc §2 recipe |
| Display that works | `Xvfb :7 -screen 0 640x480x16` (root depth “16 planes”) | `xdpyinfo` output; captures in `wine_shots/` |
| Module bases at runtime | `PE 400000-68f000 fifawc`, `PE 3920000-392f000 eacsnd`, `PE 60000000-6001e000 d3da` | `wine_modules_d3da.txt` (`winedbg --command "info proc"` then console `info share`) |

The fixed Watcom image base is honoured exactly, so every static VA in the Ghidra docs
(`0x400000 + RVA`, DGROUP `0x55a000-0x597600`, `.bss 0x598000+`) is valid verbatim in the live
process — that is what makes the dumps below directly comparable to `analysis/ghidra_FIFAWC.EXE_dump.txt`.

## 2. Reproduction recipe (exact commands)

```bash
export WINEPREFIX=$HOME/.wine-wc98 DISPLAY=:7 WINEDEBUG=-all   # :7 = Xvfb -screen 0 640x480x16
Xvfb :7 -screen 0 640x480x16 -nolisten tcp &                  # dims gate, see §3.2

# one-time prefix setup (already applied; keys are the ones FIFAWC reads)
wineboot -i
wine reg add 'HKLM\Software\EA Sports\World Cup 98' /v "CD Drive"            /t REG_SZ /d "C:\games\wc98" /f
wine reg add 'HKLM\Software\EA Sports\World Cup 98' /v "Thrash Driver"       /t REG_SZ /d "d3da"           /f
wine reg add 'HKLM\Software\EA Sports\World Cup 98' /v "3D Card"             /t REG_SZ /d "Direct3D HAL"   /f
wine reg add 'HKLM\Software\EA Sports\World Cup 98' /v "Hardware Acceleration"/t REG_SZ /d "1"             /f
wine reg add 'HKLM\Software\EA Sports\World Cup 98' /v "Triple Buffer"       /t REG_SZ /d "0"              /f
wine reg add 'HKLM\Software\EA Sports\World Cup 98' /v "Language"            /t REG_SZ /d "English"        /f
wine reg add 'HKCU\Software\Wine\Drivers' /v Graphics /d x11 /f
for v in DisableHidraw DisableInput; do wine reg add 'HKLM\System\CurrentControlSet\Services\winebus' \
  /v $v /t REG_DWORD /d 1 /f; done          # joystick neutralisation, copied from the Kabus 22 harness

# data-path mirrors (game dir lives in C:\games\wc98)
cd ~/.wine-wc98/drive_c && for d in AUDIO COMM COMMON DIRECTX5 FE FE2 FEPACK INGAME THRASH SETUP INSTALL AUTORUN; do \
  [ -e "$d" ] || ln -s games/wc98/$d $d; done

# run (windowed Xvfb, no WM needed) + capture frames
cd ~/.wine-wc98/drive_c/games/wc98 && setsid wine FIFAWC.EXE >/tmp/run.log 2>&1 &
import -display :7 -window root analysis/pass7/wine_shots/state_20s.png      # ImageMagick; ffmpeg x11grab also needs `-i`

# key injection without focus (WM-less server): BadMatch on X_SetInputFocus
xdotool search --class fifawc.exe          # -> window id, name "World Cup 98"
xdotool key   --window <id> Return space Escape Down Return

# live memory dump: winedbg scripted attach, hexdump commands verbatim in wc98_dumpcmds.txt
P=$((16#$(winedbg --command "info proc" | awk '/FIFAWC.EXE/{print $1}')))
( printf 'x/4x 0x57e92c\n';   sleep 1; printf 'x/256x 0x57fbc8\n'; sleep 1;   # mode byte, task table
  printf 'x/32x 0x57fc40\n';  sleep 1; printf 'x/16x 0x581ac0\n' ; sleep 1;   # queue tables
  printf 'x/32x 0x632a80\n';  sleep 1; printf 'x/256x 0x631b18\n';sleep 1;    # HAL slots, bracket recs
  printf 'x/16x 0x5725b8\n';  sleep 1; printf 'x/64x 0x572648\n' ; sleep 2;   # stream model (+0x90)
  printf 'detach\n' ) | winedbg $P > analysis/pass7/wine_raw_dumps_t1.txt 2>&1

# decode to annotated tables (Ghidra names + cross-run diff)
python3 analysis/pass7/wc98_dumps2tables.py analysis/pass7/wine_raw_dumps_t1.txt \
        analysis/ghidra_FIFAWC.EXE_dump.txt analysis/pass7/wine_tasktable.txt \
        analysis/pass7/wine_raw_dumps_t2.txt
python3 analysis/pass7/wc98_slots.py analysis/pass7/wine_raw_dumps_{t1,t2}.txt   # -> wine_tasktable_slots.md
```

Notes: `winedbg --file <cmds> <pid>` **does not** execute the file when attaching by pid (hangs at
the prompt, verified) — pipe commands to stdin with sleeps. Only the **first** attach of a process
works; after `detach`, re-attach fails with “Can't attach process 012c: error 5” → one dump per
process lifetime (`wine_raw_dumps_t3_keyed.txt` is empty for that reason).

## 3. Boot fixes applied (each documented, in order)

### 3.1 The engine's own window-dimension gate
On the host desktop (`:0`, KWin Xwayland, root **3840x2160**) the game creates a 3840x2160 window
and immediately aborts with its own message box:

```
makevideowindow - INVALID WINDOW DIMENSIONS
requested: 3840x2160 maximum: 2400x1600
FILE win\vidwin.c LINE 50
```

Capture (dialog text, verbatim): `analysis/pass7/wine_shots/probe1.png` (299x134 px, 10 299 B).
**Port-relevant constants:** max video window **2400x1600**; the check lives in `win\vidwin.c:50`
(source-path leak family already listed in `research-inbox/WC98_EXE_ANALYSIS.md` §3.9).

### 3.2 Display geometry + colour depth is the real gate
Wine's emulated virtual desktop (`HKCU\Software\Wine\Desktop` = y / 640 / 480) and
`X11 Driver UseXVidMode=N`, `UseXRandR=N` did **not** change the reported window size on `:0`
(`xdotool getwindowgeometry` stayed 3840x2160). A private rootful `Xwayland :7 -geometry 640x480`
(24 bpp) passed §3.1 but crashed (first-chance AV, §3.3). **What boots** is a plain
`Xvfb :7 -screen 0 640x480x16`: the game's authored 640x480 @ 16-bit mode (§ research notes:
“resolution 640x480 … 16-bit colour depth”) is satisfied by the X root itself.

### 3.3 GPU wined3d path (24 bpp) crashes in the Watcom free-list
On `Xwayland :7 -geometry 640x480` (GPU GL via KWin) the process dies with:

```
wine: Unhandled page fault on read access to 8164E24B at address 0049F7A9 (thread 0124)
```

Register/insn state captured by breaking **at** the site (`break *0x49f7a9`, evidence
`analysis/pass7/wine_bp49f7a9.txt`): `ESI=8164e248`, `EBX=01640040`, `ECX=0164e0d8`,
`EDX=01630fc0`, `EDI=016400b0`, `EAX=700`; faulting insn
`testb $0x40,0x3(%esi)` at `fifawc+0x9f7a9`, inside the block-free/coalesce routine
`FUN_0049f750` (the same routine pass 3 names as the allocator's free path,
`GHIDRA_PASS3_RENDERING.md:225`). The bad node = a valid heap pointer (`0x164e248`) with **bit 31
set** — an EA/Watcom allocator tag bit seen where the loop expects it cleared. Wine's auto-winedbg
then shows the “Program Error” dialog: `wine_shots/probe2.png` (5 190 B),
`wine_shots/probe_mapped.png` (11 172 B). **Not solved.** Software rendering (`-no3dx`) reaches
the same steady state as §3.4, so this AV is specific to the wined3d/GLX path at 24 bpp.

### 3.4 Steady state on Xvfb 640x480x16: renders live frames, presents black
`WINEDEBUG=+ddraw,+wined3d,+x11drv` (`analysis/pass7/wine_run_16bpp_trace.log`, 894 lines) shows a
healthy frame loop at t≈60 s:

```
trace:ddraw:surface_lock surface 00B53F18 ... DDSD_WIDTH : 640 / DDSD_HEIGHT : 480 / DDSD_PITCH : 1280
   DDSD_PIXELFORMAT : ( DDPF_RGB , RGB bits: 16, R 0x0000f800 G 0x000007e0 B 0x0000001f)
trace:ddraw:ddraw_surface1_Blt ... dst_rect (0,0)-(640,480), flags 0x1000000
trace:ddraw:ddraw_surface_update_frontbuffer surface 00B6FEC8, rect (0,0)-(640,480), swap_interval 0
```

So DirectDraw cooperative mode, the RGB565 primary, and per-frame presentation all work. The
presented pixels are black: every capture is `colors=21 mean=0.00139 max=1` (full-black 640x480),
identical at t = 15/20/30/40/45/60/100 s, and unchanged by:

* `-no3dx` software renderer (`wine_shots/sw_*.png`) — same black frames;
* audio backend PipeWire vs `Drivers/Sound=null` (`wine_shots/snd_*.png`, `live_*.png`) — same;
* key/mouse injection via `xdotool key/click --window <id>` (`keys_after.png`, `click_after.png`) — same;
* “CD Drive” pointed at the install dir + C-root data symlinks (§2) — no DRM prompt, no CD-label check fired.

The engine therefore reaches its render loop and writes config
(`USER/options.sav`, 96 B, magic `AFIFAFIF`, md5 `8efe8a0153543b7240b6bdb349c6a03`, copied to
`analysis/pass7/wine_options_sav_live.bin`) but the FE presentation state stays black. Root cause
not isolated — needs dynamic tracing of the FE/vidmov state machine (§6).

## 4. Live DGROUP dumps (the oracle payload)

Raw hexdumps: `wine_raw_dumps_t1.txt` (session A), `wine_raw_dumps_t2.txt` (session B, fresh
process), `wine_modules_d3da.txt` (module list + HAL slots, session C). Annotated tables:
`wine_tasktable.txt`, `wine_tasktable_slots.md`; cross-run diff: `wine_diff.txt`.

| target | live value / shape | reading |
|---|---|---|
| `DAT_0057e92c` (bracket seed mode) | `0x00000000`, and `DAT_0057e930/0x57e934` = 0 | mode **0 = shuffle** (`TOURNAMENT_WC98.md:30`, `GHIDRA_PASS3_RENDERING.md:225`); the fixed-bracket table pointers are unpopulated in this state |
| `DAT_0057fbc8` task/dispatch table, `[+0x0,+0xE0)` = 56 slots | all 56 nonzero; **35 distinct handlers**; slot values byte-identical to the on-disk image static init (VA→file: DGROUP `0x55a000→0x158600`, so `0x57fbc8 → 0x17e1c8`; compared with `WorldCup98_Extracted/FIFAWC.EXE`) | the table is **compile-time constant**; runtime never registers/patches handlers. Dispatch selects by per-object index (`obj[+0x38/8]`, `GHIDRA_PASS6_AI.md:23`), so “which slots activate per mode” is an *object-state* question, not a table-mutation one — this answers the open item at `GHIDRA_PASS6_AI.md:66` for these three tables |
| same, handler clusters (full list in `wine_tasktable_slots.md`) | 16 slots = stub `0x4a0d30` (`FUN_004a0d00+0x30`, i.e. the null-task handler); 11 slots in `FUN_00516e30` (motion/animation cluster); 8 + 7 + 3 in the `FUN_004c9e20` / `FUN_004ca7b0` / `FUN_004cb310` AI clusters; singletons at `0x515fc0`, `0x5160b0`, `0x4ca610`, `0x4ca690`, … | confirms pass-6’s dispatch owner (`FUN_004a0d80`) and gives the exact 56-entry length (docs assumed “table”, length unknown statically) |
| `DAT_0057fc40` secondary queue table, `[+0x0,+0x80)` | 32/32 nonzero; **inside** the `0x57fbc8` block’s tail; also identical to image | corroborates the “0x57fbc8 / 0x57fc40 / 0x581ac0-block” note (`GHIDRA_PASS6_AI.md:66`) |
| `DAT_00581ac0` block, `[+0x0,+0x40)` | 16/16 nonzero; identical to image | same conclusion as above |
| `DAT_00632a80` HAL slots, `[+0x0,+0x80)` | **18 of 32 nonzero**, all in `0x60001020-0x6002e20`; module list proves the image at that range is **`d3da`** (`PE 60000000-6001e000 d3da`) | live confirmation of registry-driven THRASH binding (`Thrash Driver=d3da`, `WC98_EXE_ANALYSIS.md` §5 “exe loads `!thrash\%s.dll`”) **and** the runtime-patched half of that block: 18 `_THRASH_*` entry pointers filled at init, remaining slots left null. Identical in sessions A and C → deterministic fill order (useful as the port's expected HAL layout) |
| `DAT_00631b18` bracket records, `[+0x0,+0x400)` | **all zero** | tournament/bracket text fields are not populated in this state; pass-3’s five-fields-per-slot stride (`0x300`) can only be oracle’d from a real tournament screen (§6) |
| `DAT_0x5725b8` stream model, `[+0x0,+0x40)` and `[+0x90,+0x100)` | 13/16 nonzero; the words decode to **300, 600, 1200, 1800, 2000, 2400, 3600, 4800, 7200, 9600, 14400** (decimal), then a pointer ladder into `.bss` (`0x55b0ac…0x55b2a0`), then float bit patterns and `0x42ef24…` code pointers | the classic EA speech **bit-rate ladder** (4800/9600/14400 bps family, cf. `AUDIO_RUNTIME.md`, `SPEECH_INDEXING_NOTES.md`) resident at runtime; codec-id slot at `+0x94` region is pointer-shaped here, not a small int → the “codec id +0x94” reading from pass 7 needs a live *audio-active* state to confirm (§6) |

**Determinism:** t1 vs t2 over every sampled range = **0 differing words** (`wine_diff.txt`), and
the HAL-slot content in session C equals A. So these DGROUP regions are byte-stable across process
runs — the dumps are reproducible evidence, not one-off noise.

## 5. Screenshot inventory (`analysis/pass7/wine_shots/`, all non-blank checks by pixel stats)

| file | size | what it proves |
|---|---|---|
| `probe1.png` | 299x134, 10 299 B | engine abort dialog: **max window 2400x1600**, `win\vidwin.c LINE 50` (§3.1) — the dims gate, verbatim |
| `probe2.png`, `probe_mapped.png` | 5 190 B, 11 172 B | Wine “Program Error” + winedbg console after the §3.3 AV on the 24 bpp GPU path |
| `state_20s.png`, `state_50s_keyed.png` | 640x480, 3 326 B each | steady-state frames **with keys injected** (`colors=21 mean=0.00139 max=1`) — black presentation is stable and input-inert (§3.4) |
| `tr_20s/40s/60s.png`, `cdr_01..03.png`, `live_01..08.png`, `snd_01..05.png`, `sw_15/30/45s.png`, `probe_xvfb16_40s.png`, `keys_after.png`, `click_after.png` | 3 326 B each (all identical black) | the black state is invariant across renderer (`d3da` vs `-no3dx`), audio (PipeWire/null), CD-path fixes, and time up to 100 s |
| `probe_heapforce_35s.png`, `probe_no3dx_20s.png`, `probe_16bpp*` | — | negative results for §3.3 fix attempts (`HKCU\Software\Wine\Heap /v FIFAWC.EXE = force` does not avoid the AV) |

No FE title, main menu, or kickoff frame exists: **not captured ⇒ not available as visual A/B.**
Use `options.sav` (§4 table, last row of §3.4) and the dumps in §4 for A/B instead.

## 6. Gaps (what is NOT reached, and the shortest known path to each)

1. **FE visuals / title / menu / kickoff** — engine sits in a live black render loop on
   Xvfb-16bpp. Not gated by: window dims, colour depth, audio backend, CD registry path, data-dir
   layout, joystick keys, `-no3dx`, heap-force, input events (all tested §3). Next step is dynamic:
   breakpoint-trace the FE task entry (`FUN_004a0d80` dispatch, `DAT_0057e92c` neighbourhood) and
   the `FE/`+`FEPACK/` open sequence with `WINEDEBUG=+file,+heap`, to find which load/state edge
   leaves the primary surface black. Not attempted within this ticket's timebox (2 h boot gate).
2. **Idle-vs-kickoff task-slot activation diff** — needs a match (§1); substitute evidence is the
   live==image identity for `0x57fbc8/0x57fc40/0x581ac0` (§4), which already removes those tables
   from the “needs runtime” list. Bracket-record (`DAT_00631b18`) and mode-byte transitions remain open.
3. **GPU wined3d (24 bpp) AV** in `FUN_0049f750` free-list walk with a bit-31-tagged node (§3.3):
   unexplored; blocks any accelerated-render reference capture under Wine at non-authored depths.
4. **Audio-active stream model fields** (`+0x94` codec id, `lECs` chunk magic) — the dumped region
   shows a rate ladder, not a live codec instance: audio never started in this state (§3.4).
5. **CD-label/serial check** — never prompted with `CD Drive="C:\games\wc98"`; the label-spoof path
   (`HKLM\Software\Wine\DosDevices\<drive> /v Label = WCUPPCCD`, ISO PVD volume id read from
   `EA-WorldCup98-Win95.iso`) was therefore not needed and remains untested.
6. **Attach limits** — one winedbg attach per process lifetime (§2 notes): multi-state dumps need
   one fresh launch per state, which is also why the “idle vs kickoff” plan needs two sessions.

## 7. State left on disk / cleanup

* Wine processes: `wineserver -k` executed at session end; no FIFAWC/wine process remains.
* Xvfb `:7`: killed after captures (`Xvfb :7 -screen 0 640x480x16 -nolisten tcp &` to restart).
* **Prefix kept for reuse:** `~/.wine-wc98` (game copy at `drive_c/games/wc98`, C-root data symlinks,
  all registry keys of §2; audio backend currently PipeWire-enabled — set
  `HKCU\Software\Wine\Drivers /v Sound = null` for silent runs).
* Source tree `WorldCup98_Extracted/` was **read-only** throughout (verified: only new file under the
  copy is `USER/options.sav`, created by the game itself in the prefix copy).
