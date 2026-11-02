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

## Models & kits (2026-11-02, `wc98-model-export` on real CD data)

Player art renders end-to-end: `INGAME/PLAYER/MODELS/*.PC` (`PCMD`/STADEX, skinned,
28 nodes / 320 tris for `HIG_30`) posed by a real match clip from `COMMON/ANIM.MO`
(413 clips; `cqckea4` kick) through the ported `PlayerRig`, textured from harvested
FSH, exported to glTF + orthographic preview PNG.

Kit texture binding (measured over all 206 files in `INGAME/PLAYER/TEXTURES`):

| archive | tags | role |
|---------|------|------|
| `COM_{10,30,70,90}.FSH` | `harm hhdc hhdo` + `m*`(MED) `g*`(GK) skin/hair parts | base body set per detail group |
| `PK###A/H.FSH` (team 026…876, `PK999?`) | full kit: `hifa hrsv hlsv hslv hclt hulg hllg hjbk hjfr hair` … | per-team home (`A`) / away (`H`) jersey atlas |
| `RK##_##.FSH`, `GK##_##.FSH` | same tag set, keeper variants | keeper kits (`.2d_%c` loader pattern) |
| `HHIG_/HMED_*.FSH` | `fa00-08 hr00-12 fh00-05` | hair style catalogue |
| `PALJER(.10).FSH` | numeric `0800 0801 1600 1601` | jersey palette swap entries (pk/rk recolor path) |

Quirks: model materials reference **`hjbx`** while every kit archive stores **`hjbk`**
(boxer-shorts tag off-by-k); material `hifa` (face) has no bitmap anywhere on the CD —
the 1998 build recolors it via PALJER at runtime, our exporter leaves it flat. With
TEXTURES harvested as a directory (`--fsh DIR`, `--pal FE/PACK/PAL.PAK`): `HIG_30`
binds **10/13**, shirt variant `SHI_30` (524 tris) 10/13, `LOW_90` (64 tris, no face
part) **7/7**. Stadium stands: `PARI___D.BIG/stad_n.pc` 144 tris 15/21 textured;
`TROPHY.BIG/trophy.pc` 110 tris **1/1** textured from its own `trophy.fsh`.
