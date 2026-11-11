# Asset Recovery Status — per-format decode / implementation / consumption (pass-15, 2026-11-10)

Condensed status index. Format details live in `docs/asset-formats/*` and the per-topic RE docs;
counts of shipped files are verified in `docs/asset-formats/INVENTORY.md` and `docs/STATUS.md`.
Decoder = module under `formats/src/`; gate = ctest entry in the 114-test suite (`build/release`).

| Format (shipped) | Decoder | Gate(s) | Consumed by | Status |
|---|---|---|---|---|
| `BIGF` archives (1,273 incl. 1,232 `.PBK`) + RefPack (797) | `big.cpp`, `refpack.cpp` | `test_big_refpack` | FE data, audio banks, match assets (`app/*`, `presentation/src/data.cpp`) | **Decoded + gated** |
| FSH/SHPI bitmaps (411 files / 6,356 bitmaps) | `fsh.cpp` | `test_fsh` | renderer textures, FE screens (`presentation/src/fe_screen.cpp`) | **Decoded + gated** |
| FFN fonts (10) | `ffn.cpp` | `test_ffn` | HUD/menu text (`presentation/src/hud.cpp`) | **Decoded + gated**; 4-bit atlas alpha path verified (WC98-FFN pass) |
| `.MDL` GEOM/SKEL (12) + TRI helpers | `mdl.cpp`, `tri.cpp` | `test_mdl`, `test_histtri` | renderer models (`renderer/…`), kit geometry | **Decoded + gated** |
| ANIM.MO rig clips (413) + `AISEQS.TBI` (534 records) | `anim.cpp` | `test_anim` | player animation state machine (`simulation/src`) | **Decoded + gated**; clip-code ↔ ANIM.MO mapping documented (`AI_SEQS_TBI.md`) |
| LAY layouts (29, incl. MNMSETUP header) | `layout.cpp` | `test_layout`, `test_renderer_layout`, FE gates | FE screens/HUD composition | **Decoded + gated**; menu labels are code-drawn strings, not widgets (`FE_MENU_SCREENS_WC98.md`) |
| FCDB `.DBI/.DBM` (2+22; 24 leagues / 56 teams / 1,782 players) | `fcdb.cpp` | `test_fcdb`, `test_teamselect` | team/player select, kit tables | **Decoded + gated** |
| Speech `.PBK`/PHRS (MicroTalk utk 6,851 + EA-XA 113 = 6,964/6,964) | `phrs.cpp`, `utk.cpp` | `test_phrs`, `test_speechqueue`, `test_audio` | commentary/audio (`app/commentary.cpp`, `app/game_audio.cpp`) | **Decoded + gated**; per-event preload table decoded (pass-12) |
| BNK / CRD / SCHl / MIDx audio (47/2/37/…) | `bnk.cpp`, `crd.cpp`, `schl.cpp`, `midx.cpp` | `test_audio`, `test_gameaudio` | music + SFX streams | **Decoded + gated** (`AUDIO_RUNTIME.md`) |
| `.DCT` EA SRE video (29 in 37 SCHl) | `dct.cpp` (+`dct_tables.inc`) | `test_dct`, `video_play` | intro/hint videos (`app/video.cpp`) | **Decoded + playable** (pass-8, 2026-11-06; STATUS.md) |
| `.SQF` EULZ/Squish (49) | `sqf.cpp` | `test_sqf` (raw-payload FNV stream stability) | FE art | **Partial — blocked**: EULZ expander install mechanism absent from shipped binaries (`SQF_FOOTIX.md` §3, pass-9 negative); needs Win9x-era debugger lane |
| `.SCI` scene scripts | `sci.cpp` | `test_sci` | match-scope scene packs (boot opens none) | **Partial**: header/record model + loader chain decoded (pass-14sci, `TASK_HANDLERS_WC98.md` §8); **opcode VM unparsed** → register gap lane |
| `.RES`, PCMD (14), GEN4 TBLS | `res.cpp`, `pcmd.cpp`, `gen4tbls.cpp` | `test_res`, `test_pcmd`, `test_gen4tbls` | FE resources, sound cmd, tournament tables | **Decoded + gated** |
| `USER/OPTIONS.SAV` (`AFIFAFIF`) + save slots | `options.cpp`, `slot.cpp`, `saveframe.cpp` | `test_slot` | save/load (`app/saveload.cpp`) | **Decoded + gated** (`SAVESLOT_WC98.md`) |
| `.RPLY` replays | `rply.cpp` | `test_rply` | replay playback (`app/replay.cpp`) | **Decoded + gated** (`REPLAY_WC98.md`) |
| TEAMHIST / ZWCUP history (`.RES`/`ZINTER.RES`) | `his.cpp` | `test_histtri`, history FE gates (#66-68) | history screens, H2H display | **Decoded + gated**; triple semantics validated (pass-12 `{goals_a, venue_flag, goals_b}`) |
| TRE (9), PNG export | `gen4tbls.cpp` (TRE tables), `png.cpp` | table gates; capture lane output | smoke/capture tooling | **Decoded / utility** |
| PE structure (exe analysis aid) | `pe.cpp` | byte-lane scans (`REVERSE_ENGINEERING_NOTES.md` §1) | RE tooling only | Utility, not shipped-asset runtime path |

## Blocked qualitative lanes (objective §2 "document why")

1. **EULZ/SQF expander** — slot `0x593e94`: zero write sites in the exe; no setter in any DLL
   (`analysis/pass9/eulz/run13..run15`). Remaining route: DOSBox-X / Win9x-era debugger.
2. **`.SCI` interpreter semantics** — loader chain known; opcode loop not decoded. Route: Ghidra
   static lane on the VM dispatcher (register row D-46 area).
3. **Original in-match pixels** — Wine DirectDraw capture dead; no pixel-diff oracle for gameplay
   visuals (`WINE_ORACLE_WC98.md`, `REVERSE_ENGINEERING_NOTES.md` §3). Static byte/table + telemetry
   lanes are the substitute, and they bound what may be claimed as verified.
