# Ghidra Pass 1 — call-site map (addresses CONFIRMED)

Project `ghidra/wc98` (non-distributed) holds analyzed copies of
FIFAWC.EXE (4,730 functions; imagebase 0x400000), EACSND.DLL (170),
EACSNDD.DLL (173), THRASH/D3DA.DLL (302), VOODOOA.DLL (288),
POWERVRA.DLL (260). Raw dumps: `analysis/ghidra_*_dump.txt`
(non-distributed; contain copyrighted string bytes). This doc records
the derived call-site facts (addresses + names only).

## FIFAWC.EXE (0x400000 base)

| Area | Address(es) | Evidence |
|---|---|---|
| Renderer driver load | `Thrash Driver` reg read @0x401014/0x401109; `!thrash\%s.dll` LoadLibrary @0x4012B5 | driver selection + load |
| THRASH ABI binder | GetProcAddress name table built @0x4916E3–0x491738+ (34 `_THRASH_*@n` names) | exe-side driver ABI |
| Options save/load | `.\user\options.sav` refs @0x46B33F, 0x46B358 | options loader region ~0x46B300 — port FIFA99 `Options_load/save` semantics; verify copied byte ranges |
| Replay system | `user\rplyindx.dat` @0x577470; `user\rply%04d.dat` @0x43C353, 0x43C59E, 0x577474 | index + per-replay file read/write |
| LAN settings | `user\network.dat` refs @0x4488B1…0x448B65 (5 sites) | settings module |
| LAN session | `MACHINE` @0x433362, `GAMELIST` @0x434BF8, `FIFAPW` @0x5728C0 | session create/join + password |
| Modem dial | `user\dial.dat` @0x57CCEC | phonebook |
| Classics data | `fe\teamhist.eng\<loc>.his` loader @0x489FA5/0x489F8D; `.tri` @0x48C142 | per-language team-history/tournament loaders (8 locales) |
| Joystick | `joyGetDevCapsA/joyGetNumDevs/joyGetPosEx` (WINMM thunks) | polling joystick input |
| Volume labels | Music/Commentary/Game SFX/Menu SFX UI strings @0x5654AD+ | FE options screen model (matches FIFA99 ADVD volume quartet) |
| Disc check | `GetVolumeInformationA` thunk @0x558DC8 | only CD gate (EXECUTABLE.md §7) |

## THRASH drivers

| Driver | Site | Meaning |
|---|---|---|
| D3DA.DLL | registry names `THRASH_DISPLAY` @0x600016D7, `THRASH_D3DDEVICETYPE` @0x60001741; export-table builder @0x6001C0BC+ | D3D device enumeration/config; 302 funcs incl. full DDraw/D3D2 path |
| VOODOOA.DLL | `glide2x.dll` load @0x6000103B, `_grGlideInit` @0x60001059 | runtime glide2x binding (Voodoo1/Rush) |
| POWERVRA.DLL | export-table builder @0x600110C0+ | PowerVR PCX path |

All three expose the identical 34-name `_THRASH_*@n` export ABI
(about, clearwindow, clip, drawline/mesh/strip, drawpoint/mesh/strip,
drawquad, ...) — **the exe-side render primitive model our renderer
must reproduce semantically** (architecture `RENDERER.md`).

## EACSND / EACSNDD (audio)

- `DirectSoundCreate` import + sites: EACSNDD getcaps @0x40155x,
  start @0x401D3x; iSND* API anchors: startbuffer @0x40219x,
  directstop @0x4022Bx, directvol @0x402A1x (debug-string xrefs).
- EACSND (release) layout matches; 170 funcs. EACSNDD carries the
  Watcom debug blob (20KB+) with C paths (`D:\audio\lib\win\sdirect.c`)
  — function naming pass next (debug-blob → Ghidra symbol import
  script).

## Next pass targets (ranked)

1. Decompile options loader @0x46B300 area → confirm WC98 AFIF field
   set (SAVES.md gate).
2. Debug-blob symbol import for EACSNDD + THRASH DLLs (ELF Watcom
   RTCS blob → names).
3. `.his`/`.tri` loader decompile @0x489FA5/@0x48C142 → Classics data
   format.
4. Replay writer around @0x43C353 → input-frame record layout
   (determinism harness).
5. LAN: `MACHINE`/`GAMELIST` module @0x4333xx–0x434Bxx — protocol
   message structs for modern UDP replacement.
6. Render-submit path: locate exe callers of the 34-name binder
   (0x491xxx) → per-primitive draw sequences (pitch/HUD/players).
