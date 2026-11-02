# Milestone 4 — FSH textures + LAY screens through the renderer

Goal: front-end and in-game art renders on the Vulkan path (M3 frame loop) from
real CD assets: `SHPI` `.FSH` → GIMX textures, `.LAY` screen layouts composited.

## Evidence (Ghidra pass 3 string/xref render, analysis/pass3/FIFAWC.EXE_pass3.txt)

| loader | address | pattern | role |
|--------|---------|---------|------|
| FE screens | `FUN_00401b40` | `y0.fsh`, `misc8/16.fsh` | front-end art sets |
| keeper anim | `FUN_00404ee4` | `GK%.2d_%c0.fsh` | goalkeeper frame strips |
| pk/rk seq | `FUN_00411080`, `FUN_004110ec` | `PK%.3d%c.fsh`, `RK%.2d_%c0.fsh`, `PK999?.FSH`, `paljer(.10).FSSH` | penalty/replay sprite sets + jersey palette swaps |
| manager FE | `FUN_00414168` | `_x0/G_x0/D_x0.FSH`, `COM_10/HHIG_10.FSH` | talking-head composites (layered FSH pairs) |
| pitch | (raw refs 0x5701f8+) | `turfs0p2.fsh`, `turfs999.fsh`, `flrlite.fsh` | turf/flag textures |

Composite pattern: base `_x0` + feature layers (`G_x0` glasses, `D_x0` …) stacked
in load order → same layering the LAY compositor needs.

## Scope (slices)

1. **FSH decode** — SHPI dir parse exists (`formats/shpi`); add GIMX→BGRA8 + palette
   (paljer swaps) → `wc98::formats::Fsh::frame(i)`; unit gate over all 307 `.FSH`.
2. **Texture upload** — staging-buffer uploader in renderer (`renderer/`), one-shot
   per frame group, VK_KHR pipeline; demo renders a FSH sprite sheet to screen.
3. **LAY parser** — `formats/src/lay.*`: header + element records (rects, sprite ids,
   text slots); goldens from 153 `.LAY` files; render compositor drawing sprites via (1).
4. **FE scene demo** — load one real LAY + its FSH set, composite at native res in
   the M3 loop; screenshot compare vs extracted expectations.

## Acceptance

- ctest: FSH 307/307 decode gate, LAY 153/153 parse gate (mirrors audio matrix style).
- Demo binary shows a real front-end screen frame; CI takes a headless (lavapipe) PNG.

## Queue (leftover Milestone 2)

MIDx event music; SQF decompress (EULZ); MDL/DCT decode gates. (The "PBK expander /
FUN_00432950 decompile" item was dropped 2026-11-01: `.PBK` speech decodes 6,964/6,964 via
PT-tag routing at member offset 20 — no 1998 expander exists or is needed; see
asset-formats/AUDIO.md and reverse-engineering/GHIDRA_PASS2_AUDIO.md correction banner.)
