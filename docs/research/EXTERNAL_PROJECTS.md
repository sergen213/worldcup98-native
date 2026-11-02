# External sibling-project audit (2026-11-01)

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
| `/mnt/1st4TB/BugsBunny` (`bbtime/`) | Native C++26 reimplementation of the Behaviour engine for two Looney Tunes PC games; OpenGL+Vulkan, Qt launcher, signed updates; active | **Highest transfer value**: `tools/re/xa2wav.py` + `docs/formats/AUDIO_XA.md` — **PC CD-XA audio decode to WAV**, same XA family as WC98 EA-XA payloads (reference cross-check for our `eaxa.cpp`); full Wine runtime-oracle kit `tools/wine/{run_original.sh,memsrv.py,trace.py,peek.py,compare.py,shot.sh,cam.py}`; headless PSX capture flow; binary verifiers (`verify_exe.py` etc.); identical C++26 + SDL3 + Qt6 + glslc stack | No license blockers recorded in audit; FSR shader embed tooling present (`embed_fsr_shaders.py`) — same AMD SDK caveat as above |
| *(extra find)* `/mnt/2nd4TB/FIFA99` (`fifa99-native/`) | **EA Sports football** — native C++26/Vulkan reimplementation of FIFA 99 (same Sportstalk-era engine lineage as WC98); no `.git` at top level (snapshot) | Already the primary code base: `formats/src/{fcdb,big,fsh,midx,mad,crd,schl,pcmd,gen4tbls,layout,anim,ffn,utk,speech,png,pe,refpack}.cpp` with per-format specs (`docs/formats/{FCDB,BIG,MIDX,BNK,SPEECH,SAVEGAME,STADIUM,CMAP,LAYOUT}.md`); `tools/{extract,gamedata,audio-extract,inventory,exeinfo,dbexplorer,simrun,visual-regress}/`; Ghidra project `/mnt/2nd4TB/FIFA99/ghidra/fifa99.gpr` | Prior EA-football work; status ledger at `fifa99-native/docs/STATUS.md`. Reuse governed by that repo's own license terms (it is our sibling port, not third-party) |

## WC98 reuse recommendations (from the audit)

1. **FIFA99 formats/audio-extract layer is the concrete "worth continuing" item** — FCDB/BIG/
   MIDX/BNK/SPEECH parsers are prime direct-port candidates into `world-cup98-native`'s
   formats layer; already largely ported and WC98-locked (see REUSABILITY.md). Its SPEECH
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
6. **SuperCan LZSS lesson** (space-filled dictionary vs zero-fill): archive-decoder analyses
   in this project should verify dictionary-init semantics against engine behavior, not
   convenience — same failure mode could hide in RefPack edge cases.

## Caveats

- CitroenC4Robot and SuperCan have no git state; version claims come from their README/ledger
  files only.
- Audit recorded **no** FIFA/WC/EA references in any of the seven repos; reuse value is
  tooling and methodology, not game-specific knowledge — except FIFA99, which is the sibling
  EA port itself.
