# Ghidra Pass 3 — Rendering Init, Graphics Drivers, File Loading

Scope: `FIFAWC.EXE` (WC98) plus the three THRASH renderer DLLs (`D3DA.DLL`,
`VOODOOA.DLL`, `POWERVRA.DLL`), analyzed with Ghidra 12.1.2 in the `wc98`
Ghidra project kept locally (`ghidra/`, git-ignored). Companion docs:
[GHIDRA_PASS1.md](GHIDRA_PASS1.md) (PE layout, registry, THRASH overview),
[GHIDRA_PASS2_AUDIO.md](GHIDRA_PASS2_AUDIO.md), [EXECUTABLE.md](EXECUTABLE.md).

Raw decompile listings live outside the repo in
`analysis/pass3/{FIFAWC.EXE,D3DA,VOODOOA,POWERVRA}*_pass3.txt` (local analysis output, not in the repository).
This doc records addresses and structure only.

All `FUN_…` names are Ghidra defaults; `DAT_…` symbols use Ghidra's
convention (leading zeros trimmed). Watcom `@N` suffixes = stdcall byte-count
name decoration.

---

## 1. Graphics / rendering initialization

### 1.1 Boot chain

| Address | Size | Role |
|---|---|---|
| `0x43cb24` | — | Game boot entry: sets `_DAT_0057f64c = 0x47`, calls graphics init dispatcher |
| `0x401300` | 742 B | **Graphics init dispatcher** — selects and loads the renderer, initializes THRASH |
| `0x4010d4` | 123 B | Registry config reader: opens `HKLM\SOFTWARE\EA SPORTS\World Cup 98`, reads driver/config values (pass-1 xrefs at `0x401014` / `0x401109`) |
| `0x4012ac` | 82 B | THRASH driver init helper called by the dispatcher |
| `0x4012B5` | — | `LoadLibraryA` of `"!thrash\%s.dll"` (driver-name substitution) inside the dispatcher path |

The registry value selects one of three driver names — strings
`voodooa`, `powervra`, `d3da` at `0x15864c–0x158660`; the DLL is loaded from
the game-relative `!thrash\` subdirectory (on disk:
`WorldCup98_Extracted/THRASH/{VOODOOA,POWERVRA,D3DA}.DLL`).

### 1.2 THRASH binder and dispatch table

| Address | Size | Role |
|---|---|---|
| `0x4916b0` | 1027 B | **THRASH binder** — binds the loaded driver: builds `_THRASH_<name>@<n>` import names at `0x4916E3–0x491738`, resolves all 32 exports via `GetProcAddress` into the dispatch table |
| `0x632a40–0x632ac4` | — | **THRASH dispatch table** (32 slots written by the binder; slot at `DAT_00632ac0` = inithal presence check) |
| `0x56781c–0x567adc` | — | 32 export-name strings `_THRASH_*@N`, all xrefed from the binder |
| `0x165e1c–0x1660d0` | — | name-suffix table used by the binder's string builder |
| `0x567af4` | — | version-mismatch message: driver "WANTS 104. YOUR MODULE IS %d" (interface version handshake) |
| `0x491b00` | 78 B | Driver-name guard: if the name does not start with `'!'`, runs a non-THRASH init path (`FUN_00493056`); sets one-shot flags `DAT_0057edc0/c4`, then calls the binder |
| `0x4930b0` | 121 B | **inithal registration** — if dispatch slot `DAT_00632ac0 == 0` logs `"inithal : MUST SET THRASH DRIVER BEFORE CALLING INITHAL"` (string `0x567cc0`); then installs the exe-side HAL callbacks into `0x58a9ec–0x58a9fc`: `FUN_004ee730`, `LAB_004ee7c0`, `FUN_004eeaa0`, `LAB_004eeb10`, `FUN_004eeb50` |

### 1.3 THRASH API surface (identical in all three driver DLLs)

The exe expects exactly these 32 exports from every renderer module:

```
about@0            clearwindow@0      clip@16              drawline@8
drawlinemesh@12    drawlinestrip@8    drawpoint@4          drawpointstrip@8
drawpointmesh@12   drawquad@16        drawquadmesh@12      drawtri@12
drawtrifan@8       drawtrimesh@12     drawtristrip@8       flushwindow@0
idle@0             init@0             is@0                 lockwindow@0
pageflip@0         readrect@20        restore@0            selectdisplay@4
setstate@8         settexture@4       setvideomode@12      sync@4
talloc@20          treset@0           tupdate@12           unlockwindow@4
window@4           writerect@20
```

Each driver DLL builds its export table from the same name block:
D3DA export strings `0x6001c188–0x6001c31f` (builder at `0x6001c0bc`, module
name string `"d3da.dll"` at `0x6001c028`); VOODOOA exports from `0x60013188+`
(builder `0x600130bc`, `"voodooa.dll"` at `0x60013028`); POWERVRA exports from
`0x6001118c+` (builder `0x600110c0`, `"powervra.dll"` at `0x60011028`).

### 1.4 Glide path in the exe

| Address | Size | Role |
|---|---|---|
| `0x4ea6c0` | 1402 B | **Glide2x binder inside FIFAWC.EXE** — `LoadLibraryA("glide2x.dll")` (string `0x56bea4`, call at `0x4eae7b`), resolves `_grGlideInit@0` (name string `0x56beb0`) and ~60 `gr*` entry points into the exe-side glide table |
| `0x655334–0x655418` | — | Glide function-pointer table in the exe, populated by the binder |
| `0x4eb270` | 87 B | `DirectDrawCreate` wrapper (call at `0x4eb2ab`) |
| `0x4eecb0` | 82 B | Second `DirectDrawCreate` wrapper (call at `0x4eece9`) |

The exe's glide binder is byte-for-byte the same function size as
`FUN_60001020` in VOODOOA.DLL (§2.2) — the game embeds a copy of the
VOODOOA glide-binding logic (used for its own non-THRASH glide accesses, e.g.
video mode probing).

### 1.5 Software / `-no3dx` path

The string `NO3DX` exists at `0x17ab40`. No dedicated software renderer DLL
ships in the THRASH directory (only the three GPU drivers). The observable
selector is the registry driver value: when no valid driver name is present,
the dispatcher takes its default branch. Exact `-no3dx` handling (launcher vs
command-line parse) not located this pass — see Open questions.

---

## 2. Driver DLLs (all imagebase `0x60000000`)

All three implement the same 32-export THRASH interface and each contains a
local `DirectDrawCreate` import thunk.

### 2.1 D3DA.DLL — Direct3D/HAL renderer (301 functions)

| Address | Role |
|---|---|
| `0x6000d6c6` | Import thunk for `DirectDrawCreate` (ddraw.dll) |
| `0x60002340` | `_THRASH_is@0`: calls `DirectDrawCreate` at `0x60002404`, queries the Direct3D interface and the HAL device GUIDs (`DAT_60010830`, `DAT_60010850`); returns `0x50` when a HAL device is present |
| `0x6000f058` | Registry value name `"THRASH_DISPLAY"` (string), xrefed at `0x600016d7` — display index selection |
| `0x6000f068` | Registry value name `"THRASH_D3DDEVICETYPE"`, xrefed at `0x60001741` inside `_THRASH_selectdisplay@4`; value 1 triggers full mode enumeration (`FUN_600075d0`) |
| `0x600115b0` (stride `0x10`) + count `DAT_6000fe44` | Enumerated display-mode table; `_THRASH_selectdisplay@4` indexes it and applies via `_THRASH_setstate@8(400, …)` |
| `0xd848` region | String `"D3DINIT.C D3D_createD3D()"` — HAL device creation site, uses `_IID_IDirect3D2` / `_IID_IDirect3DHALDevice` (confirms Direct3D **2** interface level) |
| `0x6001c0bc` | Export-table builder; module string `"d3da.dll"` at `0x6001c028` |

Init (`_THRASH_init@0`, `0x60001850`, 1977 B) also reads an INI-style config
file: builds a path from `GetWindowsDirectoryA` + string `"d3da.cfg"`
(`DAT_6001c4a0`) — driver-side config lives in the Windows directory.

### 2.2 VOODOOA.DLL — 3Dfx Glide renderer (287 functions)

| Address | Role |
|---|---|
| `0x60001020` | **Glide2x binder** (1402 B): `LoadLibraryA("glide2x.dll")` (string `0x6000f004`, call at `0x6000103b`), `_grGlideInit@0` resolved at `0x60001059`; binds the full `gr*` surface into DLL-local table `DAT_60011414–0x600114ec`. Same size as the exe's own binder (§1.4) |
| `0x6000d8e8` | Import thunk for `DirectDrawCreate`; called from `FUN_60001bd0` at `0x60001c39` (window/mode plumbing, 87 B) |
| `0x600130bc` | Export-table builder; `"voodooa.dll"` string at `0x60013028` |

The 3Dfx SST configuration registry strings in the exe (`SST_*`, pass-1
region from `0x1586c0`) pair with this driver.

### 2.3 POWERVRA.DLL — SGL / Voodoo Rush renderer (259 functions)

Despite the "PowerVR" name, this driver binds **3Dfx SGL** (the SDK-GL used by
Voodoo Rush boards, whose PCI-side renderer was PowerVR-based):

| Address | Role |
|---|---|
| `0x60002670` | `_THRASH_init@0` (1025 B): `LoadLibraryA("SGL.DLL")` (string `0x6000d194`, call at `0x6000283b`) and binds 18 SGL entry points into `DAT_6000f188–0x6000f1cc`: `sgl_get_win_versions`, `sgl_use_address_mode`, `sgl_create_screen_device`, `sgl_delete_device`, `sgl_get_free_texture_mem(_info)`, `sgl_create_texture`, `sgl_set_texture`, `sgl_direct_set_texture`, … |
| `0x6000b970` | Import thunk for `DirectDrawCreate`; called from `_THRASH_init@0` at `0x60002a14`, followed by vtable calls: `+0x1c` (SetCooperativeLevel) at `0x60002a39`, `+0x20` (**EnumDisplayModes** with mode-collection callback `LAB_600020b0`) at `0x60002a4e` |
| `0x600110c0` | Export-table builder; `"powervra.dll"` string at `0x60011028`; exports from `0x6001118c+` |

SGL also appears as the lowercase string `"sgl.dll"` in the same DLL's
strings (`0xb9bc` file offset), reinforcing the SGL identification.

---

## 3. File loading and VFS

### 3.1 Init chain (file side)

| Address | Size | Role |
|---|---|---|
| `0x402dd0` | — | Master init; opens palette FSH at `0x402eb7` via the central FSH open routine |
| `0x439028` | — | FE (front end) init entry → calls `0x444914` |
| `0x43933c` / `0x4396e0` / `0x439810` | — | File open helpers (`CreateFileA` via thunk at `0x558c00`) used by the fallback chain below |
| `0x438e38` | 273 B | Second registry reader: reads value `"Language"` (string `0x561d48`) into `DAT_00601334`, and `"CD_Drive"` (string `0x561d54`) as an ASCII drive letter into `DAT_00601335` |

### 3.2 BIG archive loader

| Address | Size | Role |
|---|---|---|
| `0x404ee4` | 606 B | **BIG loader**: appends `".big"` to the base name, `CreateFileA`, reads the EA BIGF index; logs `"Big: %s\n"` (string `0x55d27c`) / `"… not found"`. Returns an in-memory archive handle used by all `.big` consumers |

Format cross-reference:
[`formats/include/wc98/formats/big.hpp`](../../formats/include/wc98/formats/big.hpp). WC98 sample (`INGAME/MISC/ANIM.BIG`)
confirms the FIFA 99 layout: magic `BIGF`, big-endian declared size =
filesize − 2, BE entry records `{u32 offset, u32 size, NUL-terminated name}`.

On-disk inventory of the extracted tree: **36 `.BIG` archives**, all under
`INGAME/MISC/` and `INGAME/STADIUM/` (ANIM.BIG, ADBOARD.BIG, CLASSIC.BIG, per
stadium day/night pairs `*_D/*_N`). No FE-resident `.big` files are in the
extracted tree.

### 3.3 FSH texture loading

| Address | Size | Role |
|---|---|---|
| `0x4098c0` | — | **Central FSH open routine** (`CreateFileA` at `0x409951`); callers include `0x402dd0`, `0x409b08`, `0x411080`, `0x414168`, `0x43a61c` |
| `0x43a61c` | 357 B | Demo FSH loader: opens `demo_d_08_fsh` / `demo_d_16_fsh` (strings `0x572b68`/`0x572b78`) — demo scene textures, 8- and 16-bpp variants |
| `0x44b20c`, `0x44b3f0`, `0x44b454` | — | FE global palette/scene loaders (`FE_GLOBAL_f_03d.fsh`, `fe_global_f_03i.fsh`, `fe_global_sc_02i.fsh`) using the standard idiom: build path via sprintf (`FUN_0049168e`) into buffer at `0x607dcc`, open via `FUN_0043933c` |
| `0x5703b4–0x570a8c` | — | Ingame asset-name table: `adboard.big`, `miscart.big`, `common.fsh`, `hihead/medhead/palstad/refkit/palera/skins/trophy.fsh`, per-stadium `stadium0/2.fsh`, `adboard0.fsh`; stadium FSH pointer array at `0x570758` (stride 0x14) |
| `0x467520` (via `FUN_004520b4`) | — | Scene-mode dispatcher selects background palettes `back16…back23.fsh` from scene characters (`+`, `,`, `-`, `.`, `/` stored at `DAT_006079e8 + 0x85e + idx*0x214`); mode byte `DAT_00640fb4` (observed: 0, 9, 0xa, 0xb, 0xc) |

### 3.4 VFS: pack redirector and fallback chain

| Address | Size | Role |
|---|---|---|
| `0x4798dc` | — | **Pack redirector**: takes a loose relative path, rewrites the leading `fe` to `%s`, sprintf's it against `"fe\pack\%s"` (string `0x564ee8`) and overwrites the extension with `.pak` — i.e. every loose FE asset reference is re-resolved into its packed archive counterpart |
| `0x479d14` | — | **Resource-open fallback chain**: tries the redirected `.pak` open first, then falls back to loose-file opens (`FUN_004396e0`, `FUN_00439810`) |

On disk: **793 `.PAK` files** in the extracted tree —
`FE/PACK/*.PAK` (per-stadium packs: BRDXF.PAK, LYONF.PAK, WCUPF.PAK, …) and
`INGAME/MISC/*.PAK` (MISCART.PAK, TURFS0Px.PAK, FLRLITE.PAK, …). The `.pak`
container layout itself was **not** decoded this pass — top candidate for a
pass-4 VFS task.

### 3.5 FE layout scanner

| Address | Size | Role |
|---|---|---|
| `0x47329c` | 1586 B | **FE layout/resource directory scanner**: `FindFirstFileA`/`FindNextFileA` over `fe\pack\layouts\LAYOUTS.BIG`, `IGLAY.BIG` and subdirectories; builds a priority-sorted linked list of resource directories consumed by the pack redirector. Fatal-if-missing path reports via `0x473ba0` |

Both archives exist in the extracted tree:
`FE/PACK/LAYOUTS/LAYOUTS.BIG` (52 773 B) and `IGLAY.BIG` (17 197 B).

### 3.6 FE asset string tables

- **Scene/sound `.sqf` table** at `0x565fd4–0x56633c`: entries
  `fe\ftxNN_M.sqf`, xrefs at `0x47b844–0x47b8a0`.
- **Stadium FE triplets** at `0x56634c–0x5665a8`: per-stadium
  `fe\pack\<stadium>.pak` + `fe\<name>.mdl` + `fe\<name>.fsh` for
  pdpf, stetf, stdenisf, lensf, lyonf, marsf, mtpelf, tlsef, brdxf, nantesf,
  wcupf, lpolybf; referenced from data tables starting at `0x57dec8`.
- **Team-history resource** `fe\teamhist.eng\zwcup.res` (string `0x566a38`,
  xref at `0x48cae0`).

### 3.7 Fonts, mouse, options

| Address | Size | Role |
|---|---|---|
| `0x469ec8` | 454 B | **Font loader**: table at `PTR_s_eras10_ffn_0057d2c0` (6 entries, stride 4: `eras10.ffn`…`eras24.ffn`); archive-vs-loose switch on flag `DAT_0057d34c` — loads from `fefonts.big`/`igfonts.big` when set, else loose files; opens at `0x46a069`. (FIFA 99 sibling used `.pak` here; WC98 uses `.big`.) |
| `0x46ac30` | — | Cursor loader: opens `.\fe\pack\maus.pak` (string `0x564ea0`, `CreateFileA` at `0x46ac81`) |
| `0x46b320` / `0x46b350` | — | **Options save/load**: magic `"FIFA"` (`0x46494641`) written twice (fields `DAT_0060cba8`, `DAT_0060cbac`); load validates the language field `DAT_0060cbcb` to 0..7 and falls back to defaults via `FUN_0046ae8c` on magic/range failure |

### 3.8 Team history and tournament bracket

| Address | Size | Role |
|---|---|---|
| `0x489f58` | — | **Team-history loader**: opens `fe\teamhist.<langdir>\<XX>.his` (language directory suffix table at stride 0x19, entries `du`, `en`, …; base string `…teamhist.eng\` at `0x5669f4/0x566a28`); validates header size fields and returns record pointer + size through the stream API (`FUN_004cf5e0` read, `FUN_004cfaa0`, `FUN_004cfb00`) |
| `0x48c0bc` | 2328 B | **Bracket/seed loader**: reads the `.res` stream; seed mode in `DAT_0057e92c` — 0 = random (`FUN_004c321c`), 1/2 = fixed bracket tables at `DAT_0057e930/0x57e934`; writes five text fields per slot (stride 0x300, fields at +0x0/+0x100/+0x180/+0x200/+0x280) into `DAT_00631b18/c18/c98/d18/d98`; allocations via `FUN_0049f5a0`, frees via `FUN_0049f750` |

### 3.9 Error paths

| Address | Role |
|---|---|
| `0x4313b0` | Fatal-error MessageBox: title `"FIFA World Cup 98"` (string `0x55b32c`), one-shot guard `DAT_005727e4`, then `ExitProcess` |
| `0x473ba0` | Missing-file fatal error (invoked by the layout scanner when FE archives are absent) |

---

## 4. Open questions / pass-4 candidates

1. **`.pak` container format** — 793 on-disk packs, written/read through
   `FUN_004798dc`/`FUN_00479d14`; internal index layout not yet decoded.
2. **`LAYOUTS.BIG` / `IGLAY.BIG` semantics** — the scanner's priority-sorted
   directory list drives resource lookup; exact priority ordering and record
   format inside these two archives need decoding.
3. **Software/`-no3dx` path** — string `NO3DX` at `0x17ab40`; no software
   renderer DLL ships. Mechanism (launcher flag → registry value removal →
   dispatcher default branch) not confirmed in the exe this pass.
4. **Mode byte `DAT_00640fb4`** — values 0/9/0xa/0xb/0xc gate demo, scene and
   bracket paths; full state map unknown.
5. **Bracket mode source (`DAT_0057e92c`)** — where the random/fixed seed
   selector is set (options file? command line?) not traced.
6. **Team-history language directory naming** — `fe\teamhist.<dir>` with
   per-file two-letter codes (`du`, `en`, …); mapping to registry
   `Language` value (1..7) inferred, not verified end-to-end.
