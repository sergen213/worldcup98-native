# World Cup 98 — Verified Asset Inventory

- **Generated:** 2026-11-01 (UTC) by automated filesystem + ISO scan (no game file modified)
- **ISO image:** `EA-WorldCup98-Win95.iso` — 568,535,040 bytes; ISO9660, volume label `WCUPPCCD`, preparer `OPTICAL MEDIA QUICKTOPIX 2.20`, created/modified **1998-04-13 23:20:47**
- **Extracted installed layout:** `WorldCup98_Extracted/`
- **Extracted tree:** 6,869 files / 534.8 MiB in 185 directories (176 of them contain files)
- **ISO payload:** 6,868 files / 534.8 MiB in 184 directories; **100% size match** against the extracted tree on all 6,868 common paths (see §3)
- **Game-data share:** 3,624 files / 378.1 MiB · **installer/redistributable share:** 3,245 files / 156.7 MiB (`SETUP/`, `DIRECTX5/`)
- **Original file mtimes:** 1997-05-13 → 1998-04-13 (CD master window); extracted copies carry extraction timestamps
- **Method:** `7z l -slt` on the ISO, full recursive tree walk, first-2048-byte magic capture per file, `sha256sum`, `strings` on the executables. Raw artifacts: `research-inbox/raw/{iso_slt.txt, iso_files.json, tree_scan.jsonl, comparison.json, hexdumps.txt}`

## 1. Overview

World Cup 98 PC ships as a 1998-04-13 master CD (`WCUPPCCD`). The playable payload is one PE32 binary (`FIFAWC.EXE`, 1.76 MB) plus two audio-driver DLLs (`EACSND.DLL`, `EACSNDD.DLL`) and six renderer plugin DLLs in `THRASH/` (GDI/Direct3D, PowerVR, Voodoo — each with an `A`/`D` accelerated + debug pair). Everything else is data in EA's FIFA-97/98/99-family formats: `SHPI` archives (`FSH`, `PAK`) wrapping `GIMX` art blocks, `BIGF` archives (`BIG`, `MO`, `VIV`, `PBK`, `HDR`) wrapping `PCMD` models and `GIMX` art, `BNKl`/`CRDl`/`PT` audio banks and command streams, `SCHl` streams for music (`ASF`) and video (`DCT`), `EULZ`-compressed `SQF` front-end text, `FCDB`/`STR0` language databases, `FNTF` fonts, `TREE` autorun bundles, and ASCII `LAY`/`LST` UI layouts.

| Top-level dir | Role | Files | Bytes | Dominant formats |
|---|---|---|---|---|
| `AUDIO` | speech, music, crowd, SFX | 2051 | 140.8 MiB | `PBK` ×1232, `SMP` ×756, `BNK` ×47, `ASF` ×8, `CRD` ×2 |
| `VIDEO` | full-motion video (stadium flyovers, sequences) | 30 | 153.1 MiB | `DCT` ×29 (`SCHl`) |
| `FE` + `FE2` + `FEPACK` | front-end screens, art, fonts, layouts, per-screen packs | 1165 | 49.7 MiB | `PAK` ×764, `FSH` ×244, `SQF` ×49, `LAY` ×29, `FFN` ×10 |
| `INGAME` | match engine art/models/stadiums | 223 | 25.8 MiB | `FSH` ×168, `BIG` ×32, `PAK` ×9, `PC` ×14 |
| `COMMON` | AI scripts, animations, databases, commentary | 130 | 1.9 MiB | `SCI` ×87, `BIN` ×9, `DBI` ×12, `DBM` ×12, `W` ×7 |
| `AUTORUN` | launcher + multilingual autorun bundles | 9 | 3.9 MiB | `TRE` ×9 (`TREE`) |
| `THRASH` | renderer back-end plugins | 7 | 964.9 KiB | `DLL` ×6 |
| `(root)` | engine, audio DLLs, autorun entry, SKU stamps | 9 | 1.9 MiB | `EXE` ×2, `DLL` ×2, `TRE`, `INF`, `SKU` ×2 |
| `COMM` | version stamp only | 1 | 15 B | `VER.DAT` |
| `USER` | runtime save data (not on ISO) | 1 | 96 B | `OPTIONS.SAV` |
| `SETUP` | Win95 installer payload (cruft, non-game) | 208 | 20.9 MiB | `DLL`/`EXE`/`DL_`/`TXT`/`INS` |
| `DIRECTX5` | DirectX 5.0 redistributable (16 language driver sets) | 3037 | 135.8 MiB | `DRV` ×873, `VXD` ×696, `INF` ×356, `ACV`/`CSP`/`UC`/`SBK`/`MPD` |

The two redistributable trees account for 52% of the file count (3,245 of 6,869) and 29% of bytes; `SETUP/` is Win95 installer machinery (`.VXD`, `.DRV`, `.SYS`, `.DL_`, `.EX_`, `EREG`, InstallShield `.INS`) and `DIRECTX5/` is the stock DirectX 5 redist with 16 localized driver sets (`DRIVERS/BRZ`, `CHS`, `CZE`, `NEC`, `USA`, …). Both are categorized as installer payload in §14, not game data.

## 2. Format signature map (EA FIFA-97/98/99 family present in WC98)

Counts are files whose first 128 bytes contain the magic (a match at offset 0 is noted per class as the format's true magic offset — e.g. `BIGF` sits at 0x30 inside every `.PBK`, `PT` at 0x60). Per-extension breakdown and two example paths per class. Full 128-byte hexdumps of every unidentified class are in §15.

| Magic (observed) | Files | Extensions (count) | Reading | Example paths |
|---|---|---|---|---|
| `BIGF` | 1273 | .pbk×1232, .big×36, .pak×2, .hdr×1, .mo×1 | EA BIG archiver (TOC + member data) — `.BIG` stadiums, `.MO` animations, `.VIV` script tables, `.PBK` speech banks, `.HDR` index | `AUDIO/CHNT/C0001800.PBK`, `AUDIO/CHNT/C0001801.PBK` |
| `SHPI` | 411 | .fsh×411 | EA SHPI archive (art container): magic + LE u32 size + LE u32 count + TOC | `FE/BRDXF.FSH`, `FE/LENSF.FSH` |
| `GIMX` | 1203 | .pak×791, .fsh×411, .big×1 | EA GIMX art block inside SHPI (textures/titles; pixel data follows sub-TOC) | `FE/BRDXF.FSH`, `FE/LENSF.FSH` |
| `PCMD` | 14 | .pc×14 | EA PCMD 3D model (Softimage-derived: STADEX/SOFIMAGE + MATR + NODE chunks) | `INGAME/PLAYER/MODELS/HIG_30.PC`, `INGAME/PLAYER/MODELS/HIG_70.PC` |
| `GEOM/SKEL` | 12 | .mdl×12 | EA front-end model geometry (GEOM) / skeleton (SKEL) | `FE/BRDXF.MDL`, `FE/LENSF.MDL` |
| `FNTF` | 10 | .ffn×10 | EA bitmap font (`.FFN`) | `FE/ERAS10.FFN`, `FE/ERAS11BI.FFN` |
| `FCDB` | 2 | .dbi×1, .dbm×1 | EA FIFA Club DataBase master (index `.DBI` + data `.DBM`) | `COMMON/FCDB.DBI`, `COMMON/FCDB.DBM` |
| `STR0/STR1` | 22 | .dbi×11, .dbm×11 | FCDB per-language string tables (STR0 = game data, STR1 = presentation/name strings) | `COMMON/FCDBPENG.DBI`, `COMMON/FCDBPENG.DBM` |
| `BNKl` | 47 | .bnk×47 | EA sample bank (EACSND) | `AUDIO/ANTH/ANTH0026.BNK`, `AUDIO/ANTH/ANTH0029.BNK` |
| `CRDl` | 2 | .crd×2 | EA crowd mix script (zones → samples/levels) | `AUDIO/CRWD/CROWD.CRD`, `AUDIO/CRWD/TRAFF.CRD` |
| `SCHl` | 37 | .dct×29, .asf×8 | EA streamed container: `.ASF` music (SCCl/SCDl) + `.DCT` video (mTCD, 320×240) | `AUDIO/MUSC/BE_PAU1.ASF`, `AUDIO/MUSC/BE_PAU2.ASF` |
| `MIDx` | 1 | .eam×1 | EA MIDI container (anthem) | `AUDIO/ANTH/ANTHEM.EAM` |
| `PT` | 2065 | .pbk×1228, .smp×756, .bnk×44, .dct×29, .asf×8 | EA audio play-track command stream: standalone `.SMP` (`PT\0 02`), one per entry inside every `.PBK`, sample headers in 44 of 47 `.BNK`, and the head sub-stream of every `SCHl` (`.ASF`/`.DCT`) | `AUDIO/PBP/SCHEDULE/A_SCH000.SMP`, `AUDIO/PLRS/P00365.PBK`, `AUDIO/ANTH/ANTH0026.BNK`, `AUDIO/MUSC/BE_PAU1.ASF` |
| `EULZ*RTF` | 49 | .sqf×49 | EA Squish/LZ compressed stream (front-end text `.SQF`) | `FE/FTX10_1.SQF`, `FE/FTX11_1.SQF` |
| `TREE` | 9 | .tre×9 | EA TREE TOC container (`.TRE`, autorun/language bundles) | `AUTORUN.TRE`, `AUTORUN/DANISH.TRE` |
| `0x10FB wrapper` | 797 | .pak×793, .big×4 | RefPack-family compressed wrapper in front of a SHPI/BIGF payload (`.PAK`) | `FE/PACK/BRDXF.PAK`, `FE/PACK/FEFONTS.BIG` |
| `FCLG` | 9 | .bin×9 | EA commentary language tables (`FC<LANG>.BIN`, `FCLG.V1.7`) | `COMMON/FCDAN.BIN`, `COMMON/FCDUT.BIN` |
| `RIFF 'CSP '` | 60 | .csp×60 | E-mu EMU10K1 CSP DSP package (DirectX redist payload, not game data) | `DIRECTX5/DIRECTX/DRIVERS/BRZ/WFM0200A.CSP`, `DIRECTX5/DIRECTX/DRIVERS/BRZ/WFM0201A.CSP` |
| `RIFF sfbk` | 15 | .sbk×15 | SoundFont bank (DirectX redist payload, not game data) | `DIRECTX5/DIRECTX/DRIVERS/BRZ/SYNTHGM.SBK`, `DIRECTX5/DIRECTX/DRIVERS/CHS/SYNTHGM.SBK` |

**Formats of the FIFA family that are NOT present:** MOAI animation magic, MADk/MADN video magic, CMAP colour-map magic, and AISCRIPT `.STI` string-index files (the WC98 script volume `COMMON/GEN4TBLS.VIV` carries `G98*.FMT` tables instead). The `0x10FB` RefPack-style prefix occurs **only** as the `.PAK` wrapper (793 files) plus 4 `.BIG` members' archives — never as a standalone stream. Animation is a `BIGF` archive of 6-char clips (`COMMON/ANIM.MO`), not MOAI; cinematics are `SCHl`/`DCT`, not MADk.

## 3. ISO vs extracted tree

| Check | Result |
|---|---|
| ISO file count (`7z l -slt`, `Folder = -`) | 6,868 |
| Extracted file count | 6,869 |
| Files in ISO missing from extracted tree | **0** (none) |
| Files in extracted tree not in ISO | 1 — `USER/OPTIONS.SAV` (96 B), a runtime options save created after install |
| Size mismatches on the 6,868 common paths | **0** — every common path matches the ISO `Size` field exactly |
| ISO dirs / extracted dirs | 184 / 185 — the extra extracted dir is `USER/` (runtime-created) |
| ISO total payload bytes | 560,792,059 vs 560,792,155 extracted (Δ = 96 B, entirely `USER/OPTIONS.SAV`) |

ISO metadata: volume `WCUPPCCD`, single-volume set, created = modified = 1998-04-13 23:20:47, mode flags absent (ISO9660). **Verdict: the extracted tree is a complete, byte-size-verified copy of the ISO plus one post-install file — no missing game asset.** Note `USER/OPTIONS.SAV` starts with `AFIFAFIF` (EA save header) and is the only writable game state in the tree; a native reimplementation needs a compatible reader/writer for it.

## 4. Extension inventory (whole tree)

| Extension | Files | Bytes | Directories |
|---|---|---|---|
| `.pbk` | 1232 | 99.2 MiB | AUDIO×1232 |
| `.drv` | 873 | 50.6 MiB | DIRECTX5×873 |
| `.pak` | 793 | 36.1 MiB | FE×784, INGAME×9 |
| `.dll` | 767 | 58.6 MiB | DIRECTX5×732, SETUP×27, THRASH×6, (root)×2 |
| `.smp` | 756 | 12.4 MiB | AUDIO×756 |
| `.vxd` | 699 | 18.9 MiB | DIRECTX5×696, SETUP×3 |
| `.fsh` | 411 | 25.2 MiB | FE×243, INGAME×168 |
| `.inf` | 360 | 3.0 MiB | DIRECTX5×356, SETUP×2, (root)×1, AUDIO×1 |
| `.ini` | 145 | 5.9 MiB | DIRECTX5×136, SETUP×9 |
| `.sci` | 87 | 272.6 KiB | COMMON×87 |
| `.acv` | 60 | 538.4 KiB | DIRECTX5×60 |
| `.csp` | 60 | 395.8 KiB | DIRECTX5×60 |
| `.sqf` | 49 | 1.9 MiB | FE×49 |
| `.bnk` | 47 | 9.7 MiB | AUDIO×47 |
| `.txt` | 42 | 175.6 KiB | SETUP×25, DIRECTX5×17 |
| `.big` | 36 | 8.8 MiB | INGAME×32, FE×4 |
| `.exe` | 33 | 5.2 MiB | SETUP×26, DIRECTX5×5, (root)×2 |
| `.bin` | 31 | 563.5 KiB | DIRECTX5×15, COMMON×9, FE×6, SETUP×1 |
| `.uc` | 30 | 1.3 MiB | DIRECTX5×30 |
| `.lay` | 29 | 208.3 KiB | FE×29 |
| `.dct` | 29 | 153.1 MiB | VIDEO×29 |
| `.dl_` | 26 | 921.0 KiB | SETUP×26 |
| `.ddb` | 16 | 241.2 KiB | DIRECTX5×16 |
| `.sbk` | 15 | 510.2 KiB | DIRECTX5×15 |
| `.mpd` | 15 | 176.5 KiB | DIRECTX5×15 |
| `.ex_` | 15 | 2.8 MiB | SETUP×15 |
| `.dat` | 14 | 78.3 KiB | SETUP×4, DIRECTX5×3, AUDIO×1, COMM×1 |
| `.pc` | 14 | 715.0 KiB | INGAME×14 |
| `.dbi` | 12 | 88.0 KiB | COMMON×12 |
| `.dbm` | 12 | 94.0 KiB | COMMON×12 |
| `.mdl` | 12 | 354.5 KiB | FE×12 |
| `.ffn` | 10 | 322.0 KiB | FE×10 |
| `.bmp` | 10 | 2.4 MiB | SETUP×10 |
| `.tre` | 9 | 4.0 MiB | AUTORUN×8, (root)×1 |
| `.ico` | 8 | 17.5 KiB | SETUP×7, (root)×1 |
| `.asf` | 8 | 19.5 MiB | AUDIO×8 |
| `.his` | 8 | 1.3 MiB | FE×8 |
| `.tri` | 8 | 454.1 KiB | FE×8 |
| `.ins` | 8 | 452.2 KiB | SETUP×8 |
| `.lib` | 8 | 3.0 MiB | SETUP×8 |
| `.rs_` | 8 | 45.0 KiB | SETUP×8 |
| `.pkg` | 7 | 1.8 MiB | SETUP×7 |
| `.hlp` | 4 | 1.1 MiB | SETUP×3, DIRECTX5×1 |
| `.sys` | 3 | 17.6 KiB | SETUP×3 |
| `.vx_` | 3 | 46.7 KiB | SETUP×3 |
| `.sku` | 2 | 8 B | (root)×2 |
| `.crd` | 2 | 5.7 KiB | AUDIO×2 |
| `.118` | 2 | 47.4 KiB | DIRECTX5×2 |
| `.new` | 2 | 47.4 KiB | DIRECTX5×2 |
| `.lst` | 2 | 5.4 KiB | FE×2 |
| `.mak` | 2 | 157.8 KiB | FE×2 |
| `.bat` | 2 | 4.5 KiB | FE×2 |
| `.res` | 2 | 4.7 KiB | FE×2 |
| `.ovl` | 2 | 794.9 KiB | SETUP×2 |
| `.cfg` | 2 | 61 B | SETUP×2 |
| `.sav` | 2 | 107 B | SETUP×1, USER×1 |
| `.eam` | 1 | 76 B | AUDIO×1 |
| `(none)` | 1 | 167 B | AUDIO×1 |
| `.hdr` | 1 | 11.4 KiB | AUDIO×1 |
| `.tbi` | 1 | 20.2 KiB | COMMON×1 |
| `.mo` | 1 | 1021.7 KiB | COMMON×1 |
| `.w` | 1 | 552 B | COMMON×1 |
| `.we` | 1 | 5.8 KiB | COMMON×1 |
| `.wf` | 1 | 5.8 KiB | COMMON×1 |
| `.wg` | 1 | 5.8 KiB | COMMON×1 |
| `.wi` | 1 | 5.8 KiB | COMMON×1 |
| `.wj` | 1 | 5.8 KiB | COMMON×1 |
| `.ws` | 1 | 5.8 KiB | COMMON×1 |
| `.viv` | 1 | 40.0 KiB | COMMON×1 |
| `.cpl` | 1 | 224.5 KiB | DIRECTX5×1 |
| `.nec` | 1 | 18.2 KiB | DIRECTX5×1 |
| `.x86` | 1 | 18.2 KiB | DIRECTX5×1 |
| `.scc` | 1 | 194 B | FE×1 |
| `.cnt` | 1 | 1.9 KiB | SETUP×1 |
| `.sy_` | 1 | 14.5 KiB | SETUP×1 |
| `.tx_` | 1 | 4.4 KiB | SETUP×1 |
| `.dr_` | 1 | 131.4 KiB | SETUP×1 |
| `.w95` | 1 | 4.2 KiB | SETUP×1 |
| `.rsc` | 1 | 43.0 KiB | SETUP×1 |
| `.m2_` | 1 | 159.3 KiB | SETUP×1 |
| `.doc` | 1 | 11.7 KiB | SETUP×1 |

## 5. Per-directory inventory (game data)

### 5.1 Game-data directories

| Directory | Files | Bytes | Extensions (count) |
|---|---|---|---|
| (root) | 9 | 1.9 MiB | .exe×2, .dll×2, .sku×2, .inf×1, .tre×1, .ico×1 |
| AUDIO | 1 | 15 B | .dat×1 |
| AUDIO/ANTH | 41 | 7.2 MiB | .bnk×40, .eam×1 |
| AUDIO/CHNT | 113 | 3.4 MiB | .pbk×113 |
| AUDIO/CRWD | 8 | 1.4 MiB | .bnk×3, .pbk×2, .crd×2, (none)×1 |
| AUDIO/MUSC | 8 | 19.5 MiB | .asf×8 |
| AUDIO/PBP/SCHEDULE | 436 | 7.0 MiB | .smp×436 |
| AUDIO/PBP/STANDING | 320 | 5.4 MiB | .smp×320 |
| AUDIO/PHRS | 4 | 88.6 MiB | .pbk×2, .hdr×1, .inf×1 |
| AUDIO/PLRS | 1020 | 6.6 MiB | .pbk×1020 |
| AUDIO/SFXS | 4 | 1.1 MiB | .bnk×4 |
| AUDIO/TEAM | 95 | 608.2 KiB | .pbk×95 |
| AUTORUN | 8 | 3.9 MiB | .tre×8 |
| COMM | 1 | 15 B | .dat×1 |
| COMMON | 130 | 1.9 MiB | .sci×87, .dbi×12, .dbm×12, .bin×9, .tbi×1, .mo×1, .w×1, .we×1 |
| FE | 99 | 3.5 MiB | .sqf×49, .fsh×15, .mdl×12, .ffn×10, .bin×6, .mak×2, .bat×2, .lst×1 |
| FE/FEFLOW00 | 1 | 1.3 KiB | .fsh×1 |
| FE/FEFLOW01 | 1 | 16 B | .fsh×1 |
| FE/FEFLOW02 | 4 | 766.3 KiB | .fsh×4 |
| FE/FEFLOW80 | 13 | 1.8 MiB | .fsh×13 |
| FE/GLOBAL | 106 | 4.1 MiB | .fsh×106 |
| FE/IGCNTROL | 8 | 360.0 KiB | .fsh×8 |
| FE/IGOPTION | 5 | 164.2 KiB | .fsh×5 |
| FE/IGTMAN | 33 | 677.8 KiB | .fsh×33 |
| FE/INGAME | 31 | 265.0 KiB | .lay×29, .fsh×1, .lst×1 |
| FE/INGAME/IGFLAGS | 56 | 195.4 KiB | .fsh×56 |
| FE/PACK | 17 | 430.3 KiB | .pak×15, .big×2 |
| FE/PACK/FEFLOW00 | 1 | 958 B | .pak×1 |
| FE/PACK/FEFLOW01 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW02 | 9 | 412.2 KiB | .pak×9 |
| FE/PACK/FEFLOW03 | 10 | 728.1 KiB | .pak×10 |
| FE/PACK/FEFLOW04 | 9 | 470.0 KiB | .pak×9 |
| FE/PACK/FEFLOW05 | 9 | 458.4 KiB | .pak×9 |
| FE/PACK/FEFLOW06 | 14 | 771.7 KiB | .pak×14 |
| FE/PACK/FEFLOW07 | 9 | 343.3 KiB | .pak×9 |
| FE/PACK/FEFLOW08 | 11 | 368.2 KiB | .pak×11 |
| FE/PACK/FEFLOW09 | 20 | 826.1 KiB | .pak×20 |
| FE/PACK/FEFLOW10 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW11 | 10 | 324.6 KiB | .pak×10 |
| FE/PACK/FEFLOW12 | 1 | 239.5 KiB | .pak×1 |
| FE/PACK/FEFLOW13 | 9 | 382.0 KiB | .pak×9 |
| FE/PACK/FEFLOW14 | 9 | 426.0 KiB | .pak×9 |
| FE/PACK/FEFLOW15 | 35 | 980.2 KiB | .pak×35 |
| FE/PACK/FEFLOW16 | 12 | 2.9 MiB | .pak×12 |
| FE/PACK/FEFLOW17 | 1 | 7.7 KiB | .pak×1 |
| FE/PACK/FEFLOW18 | 10 | 345.7 KiB | .pak×10 |
| FE/PACK/FEFLOW19 | 10 | 398.2 KiB | .pak×10 |
| FE/PACK/FEFLOW20 | 14 | 1.6 MiB | .pak×14 |
| FE/PACK/FEFLOW21 | 9 | 332.4 KiB | .pak×9 |
| FE/PACK/FEFLOW22 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW23 | 9 | 396.2 KiB | .pak×9 |
| FE/PACK/FEFLOW24 | 8 | 109.9 KiB | .pak×8 |
| FE/PACK/FEFLOW25 | 9 | 458.6 KiB | .pak×9 |
| FE/PACK/FEFLOW26 | 9 | 430.0 KiB | .pak×9 |
| FE/PACK/FEFLOW27 | 9 | 315.4 KiB | .pak×9 |
| FE/PACK/FEFLOW28 | 10 | 352.0 KiB | .pak×10 |
| FE/PACK/FEFLOW29 | 11 | 406.0 KiB | .pak×11 |
| FE/PACK/FEFLOW30 | 8 | 101.7 KiB | .pak×8 |
| FE/PACK/FEFLOW31 | 8 | 92.1 KiB | .pak×8 |
| FE/PACK/FEFLOW32 | 9 | 383.6 KiB | .pak×9 |
| FE/PACK/FEFLOW33 | 9 | 341.2 KiB | .pak×9 |
| FE/PACK/FEFLOW34 | 8 | 88.8 KiB | .pak×8 |
| FE/PACK/FEFLOW35 | 9 | 475.6 KiB | .pak×9 |
| FE/PACK/FEFLOW37 | 1 | 51.7 KiB | .pak×1 |
| FE/PACK/FEFLOW38 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW39 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW40 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW41 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW42 | 1 | 38.0 KiB | .pak×1 |
| FE/PACK/FEFLOW43 | 2 | 190.4 KiB | .pak×2 |
| FE/PACK/FEFLOW44 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW45 | 1 | 223.3 KiB | .pak×1 |
| FE/PACK/FEFLOW46 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW47 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW48 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW49 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW50 | 9 | 365.9 KiB | .pak×9 |
| FE/PACK/FEFLOW51 | 1 | 267.8 KiB | .pak×1 |
| FE/PACK/FEFLOW52 | 1 | 263.4 KiB | .pak×1 |
| FE/PACK/FEFLOW53 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW54 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW55 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW56 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW57 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW58 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW59 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW60 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW61 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW62 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW63 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW64 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW65 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW66 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW67 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW68 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW69 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW70 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW71 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW72 | 12 | 441.5 KiB | .pak×12 |
| FE/PACK/FEFLOW73 | 11 | 254.5 KiB | .pak×11 |
| FE/PACK/FEFLOW74 | 11 | 188.0 KiB | .pak×11 |
| FE/PACK/FEFLOW75 | 10 | 277.4 KiB | .pak×10 |
| FE/PACK/FEFLOW76 | 2 | 164.6 KiB | .pak×2 |
| FE/PACK/FEFLOW77 | 3 | 235.0 KiB | .pak×3 |
| FE/PACK/FEFLOW78 | 1 | 20 B | .pak×1 |
| FE/PACK/FEFLOW79 | 13 | 465.5 KiB | .pak×13 |
| FE/PACK/FEFLOW80 | 13 | 749.4 KiB | .pak×13 |
| FE/PACK/FEFLOW81 | 15 | 2.1 MiB | .pak×15 |
| FE/PACK/FEFLOW82 | 16 | 1.9 MiB | .pak×16 |
| FE/PACK/FEFLOW83 | 1 | 279.5 KiB | .pak×1 |
| FE/PACK/FEFLOW84 | 14 | 4.4 MiB | .pak×14 |
| FE/PACK/FEFLOW85 | 2 | 118.8 KiB | .pak×2 |
| FE/PACK/FEFLOW86 | 1 | 32.6 KiB | .pak×1 |
| FE/PACK/FEFLOW87 | 9 | 376.6 KiB | .pak×9 |
| FE/PACK/FEFLOW88 | 9 | 364.5 KiB | .pak×9 |
| FE/PACK/FEFLOW89 | 11 | 557.1 KiB | .pak×11 |
| FE/PACK/FEFLOW90 | 9 | 349.4 KiB | .pak×9 |
| FE/PACK/FEFLOW91 | 9 | 330.2 KiB | .pak×9 |
| FE/PACK/FEFLOW93 | 1 | 291.4 KiB | .pak×1 |
| FE/PACK/FEFLOW97 | 1 | 20 B | .pak×1 |
| FE/PACK/GLOBAL | 108 | 1.3 MiB | .pak×108 |
| FE/PACK/IGCNTROL | 8 | 66.6 KiB | .pak×8 |
| FE/PACK/IGOPTION | 5 | 27.0 KiB | .pak×5 |
| FE/PACK/IGTMAN | 33 | 381.2 KiB | .pak×33 |
| FE/PACK/INGAME/IGFLAGS | 56 | 69.7 KiB | .pak×56 |
| FE/PACK/LAYOUTS | 2 | 68.3 KiB | .big×2 |
| FE/PACK/TITLES | 10 | 2.8 MiB | .pak×10 |
| FE/TEAMHIST.ENG | 18 | 1.7 MiB | .his×8, .tri×8, .res×2 |
| FE2 | 1 | 2 B | .dat×1 |
| FEPACK | 1 | 2 B | .dat×1 |
| INGAME/MISC | 35 | 624.3 KiB | .fsh×26, .pak×8, .big×1 |
| INGAME/PLAYER/MODELS | 15 | 719.2 KiB | .pc×14, .pak×1 |
| INGAME/PLAYER/TEXTURES | 141 | 15.9 MiB | .fsh×141 |
| INGAME/STADIUM | 32 | 8.6 MiB | .big×31, .fsh×1 |
| THRASH | 7 | 964.9 KiB | .dll×6, .dat×1 |
| USER | 1 | 96 B | .sav×1 |
| VIDEO | 30 | 153.1 MiB | .dct×29, .dat×1 |

## 6. Front-end — `FE/`, `FE2/`, `FEPACK/`

- `FE/` holds 99 loose top-level files + 112 subdirectories.
- **`FE/FEFLOWxx/` (4 dirs: 00,01,02,80)** — 19 loose `FSH`/`PAK` screen-art files (e.g. `FE/FEFLOW02/FEFLOW02.FSH`, 784,740 B).
- **`FE/PACK/FEFLOWxx/` (90 dirs, 00–97 with 36, 92, 94–96 absent)** — the per-screen asset packs; each `FEFLOWxx.PAK` is a compressed `SHPI` bundle. 46 of these packs contain a single 20-byte stub `FEFLOWxx.PAK` (screen present in the registry, art merged elsewhere) — the smallest `PAK` in the tree.
- Screen registry: `FE/FE.LST` (`[screens] numScreens=96`, names `feflow00…feflow97`) and `FE/INGAME/INGAME.LST` (`numScreens=25`, `igmenu`, `igqueshn`, …).
- Build provenance leaks: `FE/GLOBAL.MAK` (115,864 B, header `# GLOBAL.MAK - 20/03/08 : D. Ja…`), `FE/FE.MAK` (`SOURCEDir = $(%ARTBUILD_SOURCE)`), `FE/MSSCCPRJ.SCC` (`SCC_Project_Name = "$/FIFA WC PC-CD/Front End Art"`) — the front end is a Visual SourceSafe-managed art build; `*.LAY` files embed their authoring path, e.g. `FE/INGAME/CONTROL1.LAY` → `D:\work\worldcup\FESCREEN\CONTROL\ingamnew\control1.lay` and `FE/INGAME/IGHILIT1.LAY` → `D:\fifawc.pc\source\ingame\femenu\igmenu.lay`.
- **Fonts:** 10 × `FNTF` — `ERAS8I`, `ERAS9BI`, `ERAS10`, `ERAS11I`, `ERAS11BI`, `ERAS12`, `ERAS14BI`, `ERAS20`, `ERAS20BI`, `ERAS36` (8 KB–122 KB, EA bitmap font, magic `FNTF` + LE u32 glyph-table size + `d\0` (100) rows marker).
- **Front-end 3D:** 12 × `MDL` (`GEOM`/`SKEL`) with matching `FSH` art pairs — stadium/scene 3D titles: `BRDXF`, `LENSF`, `LYONF`, `MARSF`, `NANTESF`, `STDENISF`, `STETF`, `TLSEF`, `WCUPF`, `MTPELF`, `PDPF`, `LPOLYBF` (= the 10 World Cup venues + pitch overlay/poly backdrop).
- **Compressed text:** 49 × `FTX*.SQF` (magic `EULZ*RTF` = EA Squish/LZ) — front-end string/credit text, 4,568–112,306 B.
- **Credits:** 6 × `*CRED.BIN` (11 KB each: `FRCRED`, `GECRED`, `ITCRED`, `SPCRED`, `UKCRED`, `USCRED`) — per-language credit text.
- **`FE/GLOBAL/` (106 files, 4,300,400 B)** shared front-end art + `FE/PACK/GLOBAL/` (108 files) packed copies.
- **`FE/INGAME/` (31) + `FE/INGAME/IGFLAGS/` (56) + `FE/IGCNTROL` (8) + `FE/IGOPTION` (5) + `FE/IGTMAN` (33)** — in-game UI screens: 29 `.LAY` layouts (`CONTROL1.LAY`, `IGCAMERA.LAY`, `IGCAMSEL.LAY`, `IGCHAT.LAY`, `IGHILIT1.LAY`, …) plus `IGFLAGS` flag art (56 files, per-nation).
- **`FE/TEAMHIST.ENG/` (18 files, 1,802,519 B)** — 8 × `.HIS` team-history tables and 8 × `.TRI` trivia tables, keyed by language (`DU`, `EN`, `FR`, `GE`, `IT`, `DA`, `SP`, `SW`), + 2 × `.RES` shared resources (`ZINTER.RES` = international history, `ZWCUP.RES` = World Cup trivia). `FIFAWC.EXE` strings confirm the exact list: `fe\teamhist.eng\{du,en,fr,ge,it,da,sp,sw}.his` / `.tri`.
- **`FE/PACK/TITLES/` (10 files, 2,890,706 B)** — title screens per language (`titlengl.fsh`, `titlduch.fsh` referenced by EXE strings).
- **`FE2/` and `FEPACK/`** contain exactly one file each: `VER.DAT` (2 bytes, `0d 0a`) — empty build-version stamps; these two dirs are vestigial (staging leftovers of the art build), not asset content.
- `FE/VER.DAT` = `$FIFAVER$V1-3-9` (see §7).

## 7. `VER.DAT` version stamps

| Path | Bytes | Content | Reading |
|---|---|---|---|
| `AUDIO/VER.DAT` | 15 | `$FIFAVER$V3-9-2` | EA build stamp |
| `COMM/VER.DAT` | 15 | `$FIFAVER$V1-1-0` | EA build stamp |
| `FE/VER.DAT` | 15 | `$FIFAVER$V1-3-9` | EA build stamp |
| `FE2/VER.DAT` | 2 | `(CRLF)` | empty stamp |
| `FEPACK/VER.DAT` | 2 | `(CRLF)` | empty stamp |
| `THRASH/VER.DAT` | 2 | `(CRLF)` | empty stamp |
| `VIDEO/VER.DAT` | 15 | `$FIFAVER$V3-5-0` | EA build stamp |

The per-module stamps differ (FE 1.3.9, COMM 1.1.0, VIDEO 3.5.0, AUDIO 3.9.2) → the CD was assembled from independently versioned art/audio/video builds; format readers should treat `VER.DAT` as build metadata, not asset data. `INSTALL.SKU` and `US.SKU` are both 4 bytes (`01 00 00 00`) = SKU/region flag (US release).

## 8. `INGAME/` — match engine

| Directory | Files | Bytes | Contents |
|---|---|---|---|
| `INGAME/MISC` | 35 | 624.3 KiB | 26 `FSH` textures (`B08_*`, `B16_*` ball/broadcast, `DEMO*_08/16`, `MISC8/16`, `PALERA`), 8 `PAK` (`TURFS0P0…P4`, `TURFS999`, `FLRLITE`, `MISCART`), 1 `BIG` (`ANIM.BIG`) |
| `INGAME/PLAYER/MODELS` | 15 | 719.2 KiB | 14 × `PCMD` models — `HIG_30/70/90` (high), `MED_30/70/90`, `LOW_30/70/90` (LOD by camera distance), `SHI_10/30/70/90` (shirts), `SHADOW` + `MODELS2.PAK` |
| `INGAME/PLAYER/TEXTURES` | 141 | 15.9 MiB | 141 × `FSH`/`GIMX` texture atlases: `COM_/HHIG_/HMED_/HLOW_*` per-kit, `PK%.3d%c` per-player, `GK%.2d_%c0`, `RK%.2d_%c0` (goalkeeper/rookie) |
| `INGAME/STADIUM` | 32 | 8.6 MiB | 31 × `BIG` (`PARI___D.BIG`, `PARI___A/B/C`, `STET___x`, `LENS___x`, `LYON___x`, `MARS___x`, `MONT___x`, `TOUL___x`, `BORD___x`, `NANT___x`, `ETIE___x`, `DENI___x` + `TROPHY.BIG`), 1 `FSH` |
| `INGAME/` | 0 | 0 B | container only — no loose files |

Verified structure of `INGAME/STADIUM/PARI___D.BIG` (284,948 B, `BIGF`, 5 members): `stad_n.pc`, `stad_s.pc`, `stad_e.pc`, `stad_w.pc` (four stand models) + `stadium0.fsh` (shared texture) — i.e. each stadium is one `BIGF` of 4 `PCMD` stands + art. `INGAME/STADIUM/TROPHY.BIG` = `trophy.pc` + `trophy.fsh`. `PCMD` header layout observed: `PCMD` + ver(1) + chunk tag `STADEX`/`SOFTIMAGE`, then `MATR` (materials, 4cc names `mhdc`, `marm`, `mrsv`, `mdfa`, `ccfr`), `NODE` blocks with names like `TorsoChn_2`, `cust…` — a Softimage-derived scene graph with 4-character material slots.

## 9. `COMMON/` — shared AI, animation, databases

| Group | Files | Bytes | Notes |
|---|---|---|---|
| `*.SCI` (AI Script) | 87 | 272.6 KiB | EA AI Script bytecode, 1.7–9 KB each. Prefix families: `GENGOL*` (24, goals/goalkeeper), `GENOFS*/GENOFC*` (offence/defence), `GENLOS*` (loss of possession), `GENWIN*/GENWIC*/GENWIS*` (wins/press), `GENSON/GENSOG/GENSOS/GENSOK/GENSSW/GENSKW` (set pieces, throw-ins, kick-outs), `GENANC/GENANS/GENINC/GENINS/GEN2NC/GEN2NS/GEN4SO/GENLOC/GEN2NC` (support, intercept, marking, 2v1/4v1 drill scripts), `CLAINC*/CLAINS*` (class/cline instructions, 8) |
| `GEN4TBLS.VIV` | 1 | 40.0 KiB | `BIGF` archive, 5 members: `G98HD`, `G98KO.FMT`, `G98PL.FMT`, `G98PK.FMT`, `G98FK.FMT` — the AISCRIPT table set (formations/kickoff/penalty/free-kick) for **G**98; `.STI` string index is absent |
| `ANIM.MO` | 1 | 1021.7 KiB | `BIGF`, 413 members of 6-char animation clips (`aaaaaa`, `cqckea4`, `sspecz6`, `cqctra1`, `ctchfa1`, `ctchra6`, `cbstaa`, …) — the whole match animation set in one archive; 1,046,204 B |
| `FCDB.DBI` / `FCDB.DBM` | 2 | 84.2 KiB | `FCDB` master database (index + data): 39,980 / 46,220 B — team & player master records |
| `FCDB_<LANG>.DBI/.DBM` | 20 | 39.2 KiB | 10 languages × (index, data) with `STR0` magic: DAN, DUT, ENG, FRE, GER, ITA, JAP, SPA, SWE (+ `FCDBPENG/FCDBPJAP` pair with `STR1` magic, 20,082/23,565 and 12,807/8,409 B = presentation/name strings) |
| `FC<LANG>.BIN` | 9 | 375.8 KiB | `FCLG.V1.7` (JAP `V1.6`) commentary-language tables, 42,840 B each (JAP 44,387) — commentary phrase/word tables |
| `AUDSFX.W`, `AUDSPC.WE/WF/WG/WI/WJ/WS` | 7 | 35.6 KiB | int32 index tables for the audio driver (552 B SFX, 5,988 B each of 6 speech variants) |
| `AISEQS.TBI` | 1 | 20.2 KiB | anim clip sequence table: header `{count=534, pool@0x29c0}`, stride-20 records `{u16 id==idx, u16 keyA; u16 idx, u16 keyB; u32 nameOff; u32 flags 0x80008/0xa000a; u32 len}`; pool names = ANIM.MO clip codes (AI_SEQS_TBI.md) |

## 10. `AUDIO/*` — sound subsystem (EACSND)

| Directory | Files | Bytes | Formats |
|---|---|---|---|
| `AUDIO` | 1 | 15 B | .dat×1 |
| `AUDIO/ANTH` | 41 | 7.2 MiB | .bnk×40, .eam×1 |
| `AUDIO/CHNT` | 113 | 3.4 MiB | .pbk×113 |
| `AUDIO/CRWD` | 8 | 1.4 MiB | .bnk×3, .pbk×2, .crd×2, (none)×1 |
| `AUDIO/MUSC` | 8 | 19.5 MiB | .asf×8 |
| `AUDIO/PBP/SCHEDULE` | 436 | 7.0 MiB | .smp×436 |
| `AUDIO/PBP/STANDING` | 320 | 5.4 MiB | .smp×320 |
| `AUDIO/PHRS` | 4 | 88.6 MiB | .pbk×2, .hdr×1, .inf×1 |
| `AUDIO/PLRS` | 1020 | 6.6 MiB | .pbk×1020 |
| `AUDIO/SFXS` | 4 | 1.1 MiB | .bnk×4 |
| `AUDIO/TEAM` | 95 | 608.2 KiB | .pbk×95 |

| Class | Files | Bytes | Format reading + evidence |
|---|---|---|---|
| `.PBK` (play-bank) | 1232 | 99.2 MiB | `BIGF` archive of `PT` play-track entries; header 0x30 of zeros, `BIGF` at offset 0x30 (TOC name length 0x1c/0x24, entries `<6cc>` + offset/size) then `PT\0 02 …` payloads. `AUDIO/PLRS/P04501.PBK` (6,896 B): 2 entries; `AUDIO/PHRS/PHRS.PBK` is 92,811,196 B (92 MB — the entire player-phrase speech corpus in one archive, indexed by `PHRS.HDR` + `PHRS.INF`) |
| `.SMP` (play track) | 756 | 12.4 MiB | Standalone `PT\0 02` command stream (EA audio driver opcodes visible: `82 01 ..`, `83 01 ..`, `8a 04 <len>`, terminator `ff`), 1,208–17,508 B; `AUDIO/PBP/SCHEDULE/A_SCH*.SMP` (436) = fixture announcements, `AUDIO/PBP/STANDING/A_STD*.SMP` (320) = table standings reads |
| `.BNK` (bank) | 47 | 9.7 MiB | `BNKl` sample banks: 40 in `ANTH` (anthems), 3 in `CRWD` (`CMIDI`, `CROWD`, `TRAFF`), 4 in `SFXS` (`GAME00/01`, `PLRS00/01`) |
| `.CRD` (crowd) | 2 | 5.7 KiB | `CRDl` crowd mix scripts: `CROWD.CRD` (4,456 B, 0x46=70 zones, per-zone sample/level/pan records) and `TRAFF.CRD` (1,384 B, 4 zones) — drives the crowd bed from `CROWD.BNK`/`TRAFF.BNK` |
| `.ASF` (music stream) | 8 | 19.5 MiB | `SCHl` streams (1.2–1.9 MB) with `SCCl` + `SCDl` chunks: `BE_PAU1/2/3/5`, `FE_TUBU`, … (menu music, streamed) |
| `.EAM` | 1 | 76 B | `ANTHEM.EAM` = `MIDx` MIDI program/patch stream (76 B): note-on/patch-change byte stream `c1 01 00 b0 07 7f 02 b1 07 7f 03 90 3c 7f ff …` + `f8 ff` clock; the opening anthem is MIDI, not sampled audio |
| `.HDR`/`.INF` | 2 | 11.4 KiB | `PHRS.HDR` (11,628 B, `BIGF`) + `PHRS.INF` (28 B) — index/catalogue for `PHRS.PBK`; `PHRS.INF` = counts + `V8-7-4` build tag string |
| `.BNK`+`.PBK`+`.CRD` in `CRWD` | 7 | 1,470,283 B | `AUDIO/CRWD/CROWD` (167 B, **no extension**) is plain ASCII text: `.. Volume in dri…` — a captured DOS `dir` listing of the crowd build directory, shipped by accident; treat as build detritus, not audio |

Speaker-side note: `EACSND.DLL` (42,496 B) + `EACSNDD.DLL` (84,264 B) implement the `PT`/`BNKl`/`CRDl`/`SCHl` playback; the `PT` opcode stream is the load-bearing format for a reimplementation (all speech/music/crowd goes through it).

## 11. `VIDEO/` — cinematics

- 29 × `.DCT` (all `SCHl`), 160,498,907 B (153 MiB) + `VER.DAT` (`$FIFAVER$V3-5-0`).
- Naming: `FL0_<VENUE>` (10 stadium flyovers, 4.83–4.91 MB), `FL1_<VENUE>` (10, 4.92–5.00 MB), `FL2_DENI/MARS` (2, 6.07/6.12 MB), `FL3_DENI` (9.68 MB), `INT_EASP` (0.64 MB EA Sports logo), `INT_FIFA` (16.45 MB FIFA intro), `SEQ_OPEN/CLOS/CLAS/QUAL` (6.19/6.05/4.85/6.08 MB opening, closing, classify, qualify sequences).
- Venues: `PARI, MARS, LYON, LEN(S), TOUL, NANT, BORD, DENI, ETIE, MONT` — the 10 French 1998 venues, matching the `FE/*F.MDL` + `INGAME/STADIUM` sets.
- Header decode (verified on 3 samples): `SCHl` + `0x28` (40 = header length) + `PT` sub-stream, then `mTCD` chunk: `40 01 f0 00` = 0x140/0x00f0 = **320 × 240** frame size, `20 07 00 38 01 00 02 00 20 00 …` = rate/flag fields, then DCT-coded frame data (visible 8×8 block patterns in `FL1_BORD.DCT`). Video is EA's own `SCHl`/DCT codec — **not** MADk/MAD, not Bink, not AVI (no RIFF/AVI magic in the tree).

## 12. `AUTORUN/` + `.TRE` bundles

| File | Bytes | Reading |
|---|---|---|
| `AUTORUN.TRE` | 9817 | `TREE` + `01 00 00 00` + `01 00 00 00` + `30 00 00 00` (entry count/offset TOC), then compressed text: readable fragments `// *c0 N E…Exemple Multi-Language Autorun script fo…r.Kit v1.6` = a MacroMedia/InstallShield-style **autorun script kit v1.6** payload |
| `AUTORUN/ENGLISH.TRE` | 717854 | language bundle (15 TOC entries); 7 language bundles are byte-identical in size (355,238 B) except ENGLISH (717,854) and GENERAL (1,287,766) |
| `AUTORUN/{DANISH,DUTCH,FRENCH,GERMAN,SPANISH,SWEDISH}.TRE` | 355238 | same container, per-language launcher text/dialog resources |
| `AUTORUN/GENERAL.TRE` | 1287766 | shared launcher resources (largest bundle) |
| `AUTORUN.INF` | 127 | `[autorun] open=…` — Windows shell entry |
| `AUTORUN.EXE` | 129024 | PE32 launcher (see §13) |

`.TRE` = `TREE`-magic TOC container (LE u32 fields: `version, count, …`) with a compressed payload; 9 files, 4,146,865 B. It is the autorun/installer front-end payload, not in-game data — a native port can ignore it.

## 13. Executable checksums (game payload)

| File | Bytes | mtime (CD master, UTC) | SHA-256 |
|---|---|---|---|
| `FIFAWC.EXE` | 1,762,304 | 1998-04-13 | 581345811a5ef3d868af80e8cafa9afa2fd05d951f47cbe5a4544f6c5c63ec2f |
| `EACSND.DLL` | 42,496 | 1998-03-27 | fbd998b4cc14b49cd3210b8cc0158e58e54ada7f6f247df084e551064dde3641 |
| `EACSNDD.DLL` | 84,264 | 1998-03-27 | a015c14a15a85ac8be1d0d542b9dd02386e840eae55f8fd08fef37956339e6b3 |
| `AUTORUN.EXE` | 129,024 | 1998-04-14 | ce2684bd56ed8e9224b0098ab4491c1aa46485b480c5d15ef241242c77a65cc6 |

`file(1)`: `FIFAWC.EXE` = PE32 GUI, i386, 5 sections; `EACSND.DLL` / `EACSNDD.DLL` = PE32 DLL, i386, 6 sections, image version 3.10; `AUTORUN.EXE` = PE32 GUI, i386, 5 sections. `FIFAWC.EXE` asset-path strings recovered (loader map, direct evidence for a reimplementation):

```text
audio\team\A_T0026.pbk        fe\teamhist.eng\{du,en,fr,ge,it,da,sp,sw}.his / .tri
ingame\misc\anim.big          ingame\stadium | ingame\misc | ingame\player\models | ingame\player\textures
common\%s                     ingame\player\textures\%s        ..\feflow86\   fe\feflow86\feflow86.fsh
.\fe\ingame\ingame.fsh        .\fe\ingame\igflags\   fe\titles\{titlengl,titlduch}.fsh
b08_y0.fsh   (@misc16.fsh   misc8.fsh   paljer10.FSH   paljer.FSH   GK%.2d_%c0.fsh   PK%.3d%c.fsh
RK%.2d_%c0.fsh   PK999a..e.FSH   PK041h.FSH   PK032h.FSH   turfs0p0..p4.fsh   turfs999.fsh   flrlite.fsh   palera.fsh
jnummX0.ffn   jnumhX0.ffn   LOW_x0.PC   MED_x0.PC   HIG_x0.PC   HIG_90/70/30.PC   SHI_10.PC
COM_x0.FSH   HHIG_x0.FSH   HMED_x0.FSH   COM_10.FSH   HHIG_10.FSH   {pari,stet,stde,lens,lyon,mars,mont,toul,bord,nant}___x.big
```

Also present in `FIFAWC.EXE` strings: format magics `FCDB`, `EULZ`, `CRDl`, `BNKl` (the loader knows these containers by name).

**Renderer/installer binary hashes (measured 2026-11-01).** The installer tree ships a **different D3DA.DLL build** than the game: `SETUP/D3DA.DLL` (92,514 B) ≠ `THRASH/D3DA.DLL` (90,942 B) — two builds; identity checks MUST be path-scoped.

| File | Bytes | SHA-256 |
|---|---|---|
| `THRASH/D3DA.DLL` | 90,942 | 7a60a1d4b4f20b3834da9f7fe398042f75b9921733c49d2d043d54429ae08974 |
| `SETUP/D3DA.DLL` | 92,514 | 988cae4d164aa7872f433a1afca10791a76162d2a2272feda623fc92ddb02bb3 |
| `THRASH/POWERVRA.DLL` | 76,518 | 636636fed2ad284f6bd2f57fcf2b31858525b386ec3185df68b2ea9d49b8962e |
| `THRASH/POWERVRD.DLL` | 201,091 | 5f9ba64b40d426e81f88915af7095219369cb79c23f8309b14073367c4e80d09 |
| `SETUP/3DSETUP.EXE` | 271,360 | 0740d9adb7a67d806ed53ddd25ce961ba6ddf6a1a6ed3c55d272e7a097a27889 |

Patch-research artifact note: `analysis/patches/zeus_compat.html` (4,432 B) is a **failed download** — an anti-bot interstitial page captured instead of the nGlide/zeus compatibility content; treat it as non-evidence (`fifawc98_patch.zip` contents remain unverified).

## 14. Installer payload (non-game, categorized separately)

| Tree | Role | Files | Bytes | Notes |
|---|---|---|---|---|
| `SETUP/` | Win95 installer + hardware drivers | 208 | 20.9 MiB | `3DFX/` (Glide Voodoo/VRush DOS·WIN95·WINNT·NTSYS drivers + `IS95`/`ISNT` InstallShield), `ATI`, `POWERVR`, `ENGLISH/FRENCH/GERMAN/SPANISH/DUTCH/SWEDISH/DANISH` installer resources, `EREG/EREG.BIN` (128 B, `SYIT…` registration blob), `*.DL_/*.EX_/*.RS_/*.VX_/*.SY_/*.TX_/*.DR_/*.M2_` cab-compressed payloads, `.INS` InstallShield scripts, `*.PKG`, `.HLP`, `.DOC`, `.CNT`. **No game assets.** |
| `DIRECTX5/` | stock DirectX 5.0 redistributable | 3037 | 135.8 MiB | `DIRECTX/DRIVERS/<16 locales>/{BRZ,CHS,CHT,CZE,DUT,FRN,GER,ITN,JPN,KOR,NEC,POL,RUS,SPA,SWE,USA}`: 873 `.DRV`, 696 `.VXD`, 356 `.INF`, 136 `.INI`, 16 `.DDB` (all `[version] platform = …` INI text), 60 `.ACV` (4 ACM codec binaries × 15 locales), 60 `.CSP` (`RIFF 'CSP '` EMU10K1 DSP packages, 4 × 15 locales), 15 `.MPD` + 15 `.SBK` (1 each × 15 locales; `SYNTHGM.SBK` = `RIFF 'sfbk'` SoundFont, 34,832 B, `EMU8000`/`1MGM`/`General MIDI`), 30 `.UC` = only **2 unique** blobs (`REND2D.UC`, `REND3D.UC` — **ELF32 LE**, e_type=2, e_machine=0x323d = Rendition Viper GPU microcode) replicated into 15 locale dirs, 8 `.HLP`, 1 `.CPL`, 2 `.118/.NEW`, 1 `.nec`, 1 `.x86`. **All of it is Microsoft/Microsoft-3rd-party driver redist, not World Cup 98 content.** |
| `THRASH/` | game renderer back-ends (game data, kept) | 7 | 964.9 KiB | `D3DA/D3DD.DLL` (Direct3D accel/debug), `VOODOOA/VOODOOD.DLL`, `POWERVRA/POWERVRD.DLL` + `VER.DAT` (2 B). `THRASH` = EA's renderer abstraction layer; the native port replaces these with GL/Vulkan. |

## 15. Raw hexdump appendix — omitted from this repository

The 128-byte raw hexdump samples of original game files remain in the
non-distributed research notes (`research-inbox/WC98_ASSET_INVENTORY.md`,
section 15) because they are excerpts of copyrighted game data. Format
observations derived from them are summarized in section 16.

## 16. Format decode notes (observed, not assumed)

- **`SHPI`** (`FSH` files, and the payload of `PAK` files): layout = `SHPI` + LE u32 total size + LE u32 member count + TOC of (4cc tag + LE u32 offset/size). 411 `FSH` files all start `SHPI`; 410 contain a `GIMX` member. In `INGAME/PLAYER/TEXTURES/COM_30.FSH` (26,016 B) the TOC is `GIMX` @0x0c then 10 art sub-entries: `hgvo` 0x60, `marm` 0xc80, `mgvc` 0x14a0, `mgvo` 0x1ac0, `hhdo` 0x20e0, `hhdc` 0x2d00, `harm` 0x3920, `mhdo` 0x4d40, `mhdc` 0x5360, `hgvc` 0x5980 — per-part kit textures (`h`/`m` = hi/med LOD, 2-letter body part, `o`/`c`/`v` variants). 621 distinct 4cc sub-names observed across the corpus (`hclt`, `hrsv`, `hulg`, `hllg`, `hslv`, `hlsv`, `hjbk`, `hjfr`, `mjbk`, `mjfr`, `mslv`, `mllg`, `mulg`, …).
- **`.PAK` wrapper**: all 793 files begin `10 FB` + two LE u16 fields + `E4`. Bytes 6..9 are literally `SHPI` in **791/793** files and `BIGF` in 2/793; in 758/793 the LE u16 at offset 2..3 equals the bytes at offset 10..11 (the SHPI size field visible in the payload), and byte 5 is `0xE4` in 738/793 (`0xE0` 33, `0xE3` 16, `0xE5` 3, `0xE2` 2). So `.PAK` = a 6-byte RefPack-family compression header in front of a `SHPI` archive; the exact RefPack size encoding still needs a decoder (open item, §17).
- **`BIGF`** (`.BIG`, `.MO`, `.VIV`, `.PBK`, `.HDR`, 2 `.PAK`): `BIGF` + LE u32 total size + LE u32 member count + LE u32 TOC size + LE u32 name length, then N × (name + LE u32 offset + LE u32 size). Verified: `COMMON/ANIM.MO` = 413 members, name length 0x1c; `COMMON/GEN4TBLS.VIV` = 5 members, name length 8; `INGAME/STADIUM/PARI___D.BIG` = 5 members; `AUDIO/PLRS/P04501.PBK` = 2 members (name length 0x24); `TROPHY.BIG` = 2 members.
- **`PT\0 02`** audio track: fixed 32-byte (0x20) / 36-byte (0x24) header, big-endian fields, then EA audio opcodes (`82 01 <n>`, `83 01 <n>`, `8a 04 <u32>`, `ff` end). Used by `.SMP`, and as the first sub-stream of every `SCHl` (`.ASF`/`.DCT`) and inside `.PBK`.
- **`SCHl`**: magic + LE u32 header length (0x28) + `PT` stream, then `.ASF` carries `SCCl` (LE u32) + `SCDl` chunk pair, `.DCT` carries `mTCD` (video: 0x140×0x0f0 = 320×240 + rate/flags).
- **`EULZ*RTF`** (`.SQF`): magic 8 B + LE u32 (68 = 0x44 uncompressed table/offset) + LE u32 (0x28 = 40 payload offset in the samples) + payload; EA Squish/LZ family, 49 files.
- **`FNTF`** (`.FFN`): magic + LE u32 glyph-data size (0x2c22 = 11,298 / 0x4b50 = 19,280) + `64 00` (100 = first glyph) + height/rows + glyph metrics records (4cc/`21 00` style ids, widths, 8-byte bitmap strides).
- **`TREE`** (`.TRE`): magic + LE u32 version(1) + count + TOC of (4cc tag + LE u32 size) pairs, payload compressed (readable text fragments appear mid-file).
- **`FCDB` / `STR0` / `STR1`**: `STR0` = LE u32 count + LE u32 offsets, a classic string-table index (e.g. `COMMON/FCDB_JAP.DBI` 1,712 B: count 0x1f5 = 501, 20 index groups, then `none`, `Wor…` strings). `FCDB.DBI/.DBM` use the `FCDB` magic (master index/data). `.DBI` = index, `.DBM` = data/members; `.DBM` in the JAP pair stores offsets big-endian (byte-swap of `.DBI`) — a 68k→x86 endian artifact.
- **`FCLG.V1.7`** (`.BIN` in `COMMON`): ASCII version tag then binary commentary tables; `COMMON/FCDAN.BIN` begins `FCLG.V1.7.=TXT N`.
- **`GEOM`/`SKEL`** (`.MDL`): LE u32 counts then per-vertex colour/`0000`-padded records (`b2 b2 b2 00` observed) — front-end 3D titles; 1 of 12 is `SKEL` (`LPOLYBF.MDL`).
- **`.HIS`/`.TRI`** (parsers `formats/src/{his,tri}.cpp`, gate `test_histtri`; full spec `docs/asset-formats/TEAMHIST.md`): team-history/trivia pairs, one per language inside `FE/TEAMHIST.ENG/` (8 pairs du/en/fr/ge/it/da/sp/sw + ZINTER/ZWCUP `.RES`). `.HIS`: header u32 @0 = **0x30028**, LE u16 `nRows=49` @0x1e, LE u16 `nIdx=52` @0x20; loader validates `8 + nIdx*2 + nIdx*nRows*4 ≤ filesize`. Language-invariant participant id run (LE u16, 33 ids {26..88}) at **+0x04**, terminated by first value ≥ 0x100; the +0x28 table and grid region [0x70,10304) are localized payload (inline strings), NOT an index. `.TRI` = LE u32 count (43) + LE u16 offsets relative to base **0x2c0**, secondary sequential index at 0xb0..0x2c0, NUL-separated string pool from ~0x2da (Q/A trivia records share strings; per-record field boundary open).
- **`.TBI`/`.W*`**: fixed-stride LE u32 tables (`.TBI` 16 B/record, `.W*` 4 B/entry) — AI/audio lookup tables.
- **`AFIFAFIF`** (`USER/OPTIONS.SAV`, 96 B) — EA save header; WC98 runtime uses the same magic written as two LE u32 `0x46494641` ('FIFA' = AFIF byte-mirror) at DAT_0060cba8/cb0c (pass 7, `analysis/pass7/options.txt`). Full block layout + field consumers: `docs/reverse-engineering/OPTIONS_SAV_WC98.md`. The only writable asset.

## 17. Open items for the reverse-engineering pass

1. `.PAK` 6-byte `10 FB` header → RefPack variant: needs a bit-exact decompressor (see §16 for the verified field correlations).
2. `GIMX` art block: pixel format(s) (8-bit indexed + 16-bit observed in `COM_30.FSH` at 0x60: `7b 10 08 00 40 00 20 00 … 40 00 40 00`) and palette source.
3. `PT` opcode table: 100% coverage needed for faithful audio; opcodes observed so far: `63 fd`, `82`, `83`, `8a`, `f1`, `ff`.
4. `SCHl`/`mTCD` video codec (DCT variant) and audio pairing for `.DCT` cinematics.
5. `.SCI` AISCRIPT bytecode + `GEN4TBLS.FMT` table formats (no `.STI` present, so strings likely inline in the `.FMT` tables).
6. `.LAY` layout object schema (INI-like `[screen] numobjects` + `[object N]`, 29 files) — the UI layer is text and can be re-hosted cheaply.

