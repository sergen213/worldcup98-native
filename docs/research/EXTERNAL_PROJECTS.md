# External sibling-project audit (2026-11-01)
All paths, sizes and script names below were re-verified live on 2026-11-02; git states are in §Git states.

Read-only static audit of seven local reverse-engineering / native-port projects (plus one
high-value adjacent find), performed by the ExternalAudit pass. Method: file inspection only;
git state derived from `.git/HEAD` + reflog reads — nothing was built or executed. This page
records which tooling is reusable for World Cup 98 and under what licensing caveats. None of
the seven repos contains FIFA / World Cup / EA references (grepped over `docs` + `tools`).

## Per-project table

| Path | Purpose | Reusable tooling for WC98 | License notes (from audit evidence) |
|---|---|---|---|
| `/mnt/1st4TB/Kabus 22` | Native Linux port of Turkish horror game (Acknex A6/A7 engine); C++20/WDL, GTK installer + launcher, signed updater, OpenGL/Vulkan, FSR 3.1.4 | `tools/wrs2.py` + `check_wrs2.py` (standalone GameStudio WRS2 archive list/extract; documents Okumura LZSS params: 4096-byte space-filled window, cursor 4078, LSB flags, absolute 12-bit refs); `tools/wine-reference/{wineref.py,compare.py}` Wine-oracle harness; texture-upscaler, AppImage packaging + CI workflows | GPL-compliant AppImage recipe with source-correspondence machinery (`packaging/NOTICE.txt`, `helper-innoextract/`, `runtime-uruntime/`) — lift with attribution and matched-source obligations |
| `/mnt/1st4TB/etiyami` | Native C++20/SDL3 reconstruction of Turkish endless-runner from original MSI/CAB media; Vulkan RT, FSR/frame-interp; v1.6.4 published | `extract_game.py`: parses Turkish **MSI + Data1.cab without executing installers** (7z-streamed `_StringPool`/`_StringData`, cp1254 decoding, MSI table column extraction) — directly transferable pattern for EA-era Windows installers; release/setup packaging scripts + checkers | FidelityFX SDK vendored under `third_party/fidelityfx-sdk`; per-component review needed before reuse |
| `/mnt/1st4TB/Pusu: Uyanış` | Early-stage native Linux engine of Turkish tactical shooter; importer, GTK launcher, OpenGL/Vulkan RT queries | **Most complete GPL source-correspondence/relink AppImage system audited**: `packaging/build_appimage.py`, `license_bundle.py`, `check_packaging.py`, `sources/public-runtime/` rebuild recipes, `licenses/*/`; `src/importer.cpp` architecture; `cmake/embed_spirv.cmake`; `checks/` self-check pattern | Explicitly GPL source-correspondence tooling — designed for copyleft compliance; matched-source companions required when lifting |
| `/mnt/2nd4TB/IKV` | Independent native Linux preservation of MMORPG IKV (C++20 client, Qt6 launcher w/ signed updates, community server); 0.10.x | Richest asset-tool set: `tools/asset_inventory.py`, `asset_adf.py` (**SKYF v1 container parser + arithmetic-decode chunk reader**), `pe_inventory.py`, `audio_inventory.py`; **Ghidra headless helpers** `tools/ghidra/{AssetReturnTypes,ExportEvidence,GraphicsEvidence,ApplyWinsockTypes}.java` + `tools/audio_ghidra/AudioEvidence.java` — the exact type-application/evidence-export pattern WC98 Ghidra passes use; release/signing tools (`release.py`, `sign_manifest.py`) | Vendored FidelityFX SDK + `ffx_fsr3_blobs.py`; `third_party/stb_vorbis/` carries a provenance file (MIT-style) |
| `/mnt/2nd4TB/CitroenC4Robot` | Native Linux racing-game reconstruction (Inno Setup installer, i386 binaries; Qt6 launcher + signed updates); frozen at documented 0.1.4; **no `.git`** — snapshot copy | `tools/import_assets.py` (519 lines): hardened **Inno Setup importer** using trusted native `innoextract`, no-follow path safety, transactional staging, SHA-256 + `asset-manifest.json`; inventory/observe/benchmark tools; `packaging/` pinned dependency-closure pipeline (ffmpeg/dbus/dependency_sources + lock files) | Signed-manifest updater pattern (`updater/src/main.cpp`) is architecture reference only; no license blockers recorded in audit |
| `/mnt/2nd4TB/SuperCan` | Native Linux RE of Turkish platformer on Acknex A8 engine; D3D9→SDL/Vulkan, script VM; **no `.git`** — snapshot copy | WRS3/WRS4 archive recovery done right: `tools/assets/recover.py` (+`check_recover.py`, `mesh.py`) decodes 24 archives → 3,125 members; documents engine-confirmed **space-filled LZSS dictionary** (zero-fill analysis was wrong for 189 textures) and 40-byte on-disk vs 48-byte index records; MDL7 mesh tooling; `tools/ghidra/*.java` vtable/ABI scripts. CAUTION: its `src/engine/wdf.hpp` parses the **Acknex** `acknex.wdf`, not EA's `.WDF`/BIG family — do not conflate | LZSS-dictionary-correctness lesson is methodology, not code; Ghidra Java scripts lift freely per audit (no license notes recorded) |
| `/mnt/1st4TB/BugsBunny` (`bbtime/`) | Native C++26 reimplementation of the Behaviour engine for two Looney Tunes PC games; OpenGL+Vulkan, Qt launcher, signed updates; active | **Highest transfer value**: `tools/re/xa2wav.py` + `docs/formats/AUDIO_XA.md` — **PC CD-XA audio decode to WAV**, same XA family as WC98 EA-XA payloads (codec tag 7 in our format notes: `/mnt/1st4TB/WorldCup98/world-cup98-native/docs/asset-formats/AUDIO.md`, "PT / BNKl / EA-XA / speech"); full Wine runtime-oracle kit `tools/wine/{run_original.sh,memsrv.py,trace.py,peek.py,compare.py,shot.sh,cam.py}`; headless PSX capture flow; binary verifiers (`verify_exe.py` etc.); identical C++26 + SDL3 + Qt6 + glslc stack | No license blockers recorded in audit; FSR shader embed tooling present (`embed_fsr_shaders.py`) — same AMD SDK caveat as above |
| *(extra find)* `/mnt/2nd4TB/FIFA99` (`fifa99-native/`) | **EA Sports football** — native C++26/Vulkan reimplementation of FIFA 99 (same Sportstalk-era engine lineage as WC98); own git repo on `main`, clean, latest commit "docs: project status" | Primary code base for our formats layer; full spec/tool inventory in §FIFA99 detail below. Parsers: `formats/src/{fcdb,big,fsh,midx,mad,crd,schl,pcmd,gen4tbls,layout,anim,ffn,utk,speech,png,pe,refpack}.cpp`; Ghidra project `/mnt/2nd4TB/FIFA99/ghidra/fifa99.gpr` + `fifa99.rep/` | Prior EA-football work; status ledger at `/mnt/2nd4TB/FIFA99/fifa99-native/docs/STATUS.md`. Reuse governed by that repo's own license terms (it is our sibling port, not third-party) |

## Git states (verified 2026-11-02 via `git log` / `status --porcelain`)

| Repo | Branch | HEAD subject | Working tree | History evidence |
|---|---|---|---|---|
| `/mnt/1st4TB/Kabus 22` kabus22-linux | main | `8067fd4 release: 0.4.11` (≈2026-10-28) | clean, 0 dirty paths | releases 0.4.8→0.4.9→0.4.10→0.4.11 interleaved with feature commits (`launcher: mod manager`, `updater: list installed versions in semantic order`); GitHub `sergen213/kabus22-linux`; RE docs dir `/mnt/1st4TB/Kabus 22/docs/reverse-engineering/{DATA_STRUCTURES,ENGINE_IDENTIFICATION,EXECUTABLE_ANALYSIS,FUNCTION_MAP,ORIGINAL_BEHAVIOR,RESOURCE_FORMATS,SCRIPT_SYSTEM}.md` |
| `/mnt/1st4TB/etiyami` ETI Yami native | main | `2e31fa9 docs: Record public v1.6.4 publication` (≈2026-10-25) | not re-checked beyond audit | routine AI-co-authored fix commits per reflog; prior release doc commit `docs: Record public v1.6.3 verification`; top level has no `docs/` dir — RE narrative is root-level `REVERSE_ENGINEERING.md` |
| `/mnt/1st4TB/Pusu: Uyanış` Pusu native Linux | main | `c17913b docs: link verified rendering and loading bugfix release` (2026-10-08) | not re-checked | **exactly 6 commits** (`git rev-list --count HEAD` = 6), all dated 2026-10-08 — youngest repo of the set; includes `feat(packaging): ship matched source companions and portable GTK dictionary`, `build(release): publish matched source provenance and recipient rebuild controls` |
| `/mnt/2nd4TB/IKV` İstanbul Kaçak Vakti | main | ≈2026-10-24 (`fix: compile enhanced OpenGL shadows on Windows`) per audit reflog read; not re-run live (tree too large to rescan) | dense release history 0.2.0→0.10.3+; version line `.superpowers/release-0.10.4/` | stray tool dirs `.claude-flow/`, `.swarm/` present — noise, ignore |
| `/mnt/2nd4TB/CitroenC4Robot` Citroën C4 Robot 0.1.4 | **no git** (`.git` absent) | n/a — state from README + `work/verification/original-ui-shadow-fidelity-001.json` ledger | plain working-copy snapshot on 2nd4TB, verified by direct read; not a symlink to elsewhere |
| `/mnt/2nd4TB/SuperCan` Süpercan port (Acknex A8) | **no git** (`.git` absent) | n/a — state from `docs/PROGRESS.md` (526 lines of revision-bound test ledgers) | snapshot copy; docs: `{ASSET_FORMATS,ENGINE_ARCHITECTURE,FUNCTION_MAP,GRAPHICS_ROADMAP,INSTALLER_ARCHITECTURE,KNOWN_ISSUES,PROGRESS,REVERSE_ENGINEERING,SOURCE_COMPARISON}.md` |
| `/mnt/1st4TB/BugsBunny/bbtime` Behaviour engine port 0.12.x | active (top-level `BugsBunny/` is NOT a repo — only `bbtime/`) | reflog tip ≈2026-10-26: `c6a0d56 LiT: △ look view … every sampled counter equal to the original`; release line through **0.12.3** (`release 0.12.3` in prior commit) | mission briefs `/mnt/1st4TB/BugsBunny/goallostintime.txt`, `goaltaztimebusters.txt`; format docs `docs/formats/{AUDIO_XA,BZE,MODELS,SAVE_LIT,SAVE_TB,TB_LEVELS,TEXTURES,TEXTURE_GAP}.md` |
| *(extra)* `/mnt/2nd4TB/FIFA99/fifa99-native` **is a git repo** (audit's "no .git at top level" is true only of the parent snapshot dir) | main | `230414d docs: project status`; prior commits: simulation exact port + gameplay engine verified against exe oracle; front-end runtime; visual-regression harness with original reference captures; packaging (desktop entries, icon, AppImage, portable, PKGBUILD, release workflow); Qt6 launcher; LAY/VIS/INC layout parser ("148 screens render") | clean tree, 0 dirty paths — most directly liftable repo of the set: same mission shape as WC98 (formats/core/simulation/gameplay/ui/app + CTest + `--game-dir`) |

## FIFA99 detail — specs and tool inventory (verified live)

- **Per-format spec docs** at `/mnt/2nd4TB/FIFA99/fifa99-native/docs/formats/` (19 files):
  ANIM BIG BNK CMAP CRD FCDB FFN FSH GEN4TBLS LAYOUT MAD MIDX OPTIONS_SAV PCMD REFPACK SAVEGAME SCHL SPEECH STADIUM (.md). Every parser in `formats/src/` has a matching spec. **REFPACK.md** and **OPTIONS_SAV.md** are the direct answers to WC98 open items: the `.PAK` `10FB` wrapper decoder, and the save format (`USER/OPTIONS.SAV`, magic `AFIFAFIF`).
- **RE docs** at `/mnt/2nd4TB/FIFA99/fifa99-native/docs/reverse/`: EXECUTABLE FUNCTION_MAP MATCH_ANIM_HUD MATCH_CAMERAS MATCH_KITS MATCH_STADIUM PATCH_1_1 SIMULATION SUBSYSTEMS THRASH (.md). Match-engine behavior evidence (cameras, kits, HUD) is what WC98's INGAME layer needs; `PATCH_1_1.md` covers EA patch handling.
- **Project docs**: `/mnt/2nd4TB/FIFA99/fifa99-native/docs/{ASSET_INVENTORY,ENVIRONMENT,FRONTEND,GAMEDATA,GAME_MODES,INSTALL,LEGAL,MATCH_AUDIO,MATCH_RENDERING,PACKAGING,RENDERER,RESEARCH,SIMULATION_STATUS,STATUS,VISUAL_REGRESSION}.md`.
- **Tools** at `/mnt/2nd4TB/FIFA99/fifa99-native/tools/`: `extract gamedata audio-extract inventory exeinfo dbexplorer simrun sim-tables models play ui-render commentary-sim visual-regress common` (CMake-built). The extract / inventory / audio-extract trio is the reusable asset-pipeline skeleton.
- **Originals**: `/mnt/2nd4TB/FIFA99/original/`; ISO/RIP zips + two official patches and manual PDF at `/mnt/2nd4TB/FIFA99/*.{zip,pdf}`; mission brief `/mnt/2nd4TB/FIFA99/goal.txt`.

## WC98 reuse recommendations (from the audit)

1. **FIFA99 formats/audio-extract layer is the concrete "worth continuing" item** — FCDB/BIG/
   MIDX/BNK/SPEECH parsers are prime direct-port candidates into `world-cup98-native`'s
   formats layer; already largely ported and WC98-locked (reusability matrix: `/mnt/1st4TB/WorldCup98/research-inbox/F99_REUSABILITY_MATRIX.md`, cited in our docs as REUSABILITY.md). Its SPEECH
   framing is what our `.PBK` members decode through once PT-tag routing is applied.
2. **bbtime `xa2wav.py` + AUDIO_XA.md**: independent reference implementation to cross-check
   EA-XA decoding against WC98 chant/BNK payloads; its Wine-oracle kit is the model for a
   behavior-verification harness against original FIFAWC.EXE under Wine.
3. **IKV Ghidra Java helpers** (type application + evidence export) match exactly what our
   Ghidra passes 1–3 do — adopt their script structure for reproducible headless runs.
4. **etiyami `extract_game.py` MSI/CAB parsing pattern**: transferable to EA-era Windows
   installers if the original CD's InstallShield payload needs re-extraction without running
   installers (WC98 originals currently live in an already-extracted tree).
5. **CitroenC4Robot Inno importer + Pusu AppImage source-correspondence packaging**: lift for
   release engineering once WC98 ships artifacts; Pusu's is the compliant path if any GPL
   runtime components enter the bundle.
   CitroenC4Robot carries a parallel AppImage recipe with matched-source companions at `/mnt/2nd4TB/CitroenC4Robot/packaging/appimage/{build.py,acquire.py,runtime-LICENSE.txt,runtime-SOURCE-README.txt}`, plus pinned dbus/ffmpeg builders (`packaging/{build_dbus.py,dbus.lock.json,build_ffmpeg.py,ffmpeg.lock.json}`).
6. **SuperCan LZSS lesson** (space-filled dictionary vs zero-fill): archive-decoder analyses
   in this project should verify dictionary-init semantics against engine behavior, not
   convenience — same failure mode could hide in RefPack edge cases. SuperCan makes it
   explicit in code (`tools/assets/recover.py`: `ring = bytearray(b" " * 4078 + bytes(18))`,
   comment: "Engine clears only the first 4078 slots to spaces; the final 18 persist") —
   matching Kabus 22's documented cursor-4078 parameters. Its header docstring also states
   the redistribution rule we follow: extracted originals stay local, never shipped.
7. **Acknex `wdf.hpp` naming collision**: `/mnt/2nd4TB/SuperCan/src/engine/wdf.hpp` parses
   Acknex `acknex.wdf` (`"WDF\0"`), NOT EA's `.WDF`/BIG family — do not reuse that name for
   WC98 container code.

## Caveats

- CitroenC4Robot and SuperCan have no git state; version claims come from their README/ledger
  files only. IKV and bbtime HEAD lines above come from the audit's reflog reads (not re-run
  live); every other row was verified by direct git commands on 2026-11-02.
- Audit recorded **no** FIFA/WC/EA references in any of the seven repos; reuse value is
  tooling and methodology, not game-specific knowledge — except FIFA99, which is the sibling
  EA port itself.
- The "no .git" claim for FIFA99 applies to the top-level snapshot dir only; `fifa99-native/`
  inside it is a clean repo (see §Git states).
