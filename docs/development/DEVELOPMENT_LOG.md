# Development Log

Newest entries first. One entry per meaningful milestone (project rule).

## 2026-11-01 — Milestone 1 complete: research merged; port dispatched

Four parallel probes completed; results merged into docs (hexdump
appendices excluded from repo — copyrighted game excerpts):

- **Asset inventory** (`docs/asset-formats/INVENTORY.md`): ISO = tree
  (0 missing/0 size mismatches, 6,868 files); format map: BIGF x1273,
  FSH/SHPI x411 (6,356 bitmaps) + GIMX art blocks x1203, RefPack x797,
  PCMD x14, GEOM/SKEL MDL x12, FNTF x10, FCDB x2 + STR0/STR1 x22 (24
  leagues / 56 teams / 1,782 players), LAY x29, BNKl x47, CRDl x2,
  PT streams x2,065 (.SMP/.PBK), SCHl x37, EULZ/Squish x49, MIDx x1,
  TRE x9, FCLG x9; loader filename patterns + VER.DAT stamps; SHA-256
  of game binaries. MOAI/MADk/CMAP absent (MO rig + TREM .DCT video
  replace them).
- **Executable RE** (`docs/reverse-engineering/EXECUTABLE.md`): Watcom
  2.18, unpacked, stripped; 173 imports; no CD check beyond volume info;
  THRASH 34-name ABI; D3D2 / glide2x / PowerVR PCX2 back-ends;
  EACSND via DirectSoundCreate; WSOCK32 datagram-only (IPX/UDP LAN) +
  TAPI modem play; registry layout confirmed; shipped DLLs contain
  Watcom debug blobs with C source paths (driver-internals roadmap);
  analysis tools in `tools/exe-analysis/` (rawpe2.py — Watcom PE import
  walker for VirtualSize=0 fields).
- **FIFA 99 reusability matrix** (`docs/asset-formats/REUSABILITY.md`):
  clean build + probes: BIG/RefPack 34/38+797/797, FSH 1184/1204, FFN
  10/10, PCMD 14/14, MO 404/413, FCDB names+players OK, LAY 29/29,
  audio codecs 47/47 BNK + 37/37 SCHl + CRD + MIDx. WC98 deltas: FCDB
  team record 68 B/17 words, FSH 0x2D/0x29 palettes, PT v2, UTK 10:1,
  G98*.FMT. Incompatibles: FE typed UI layer, speech index, SQF, MDL,
  TREM video, gamedata manifest (WC98 layout).
- **External dossier** (`docs/research/EXTERNAL.md`): 40 teams, WC +
  Classics rules, 640x480 locked, 85/63 fps caps, nGlide fix details
  (voodooa.dll — PCGW typo noted), disc check, no prior WC98
  reimplementation exists; WC98 vs RTWC98 facts separated.

Status list consolidated in `docs/STATUS.md` (confirmed / hypotheses /
unknowns / limitations). GitHub repo `sergen213/worldcup98-native`
public, CI green. Port of the fifa99-native formats+tools layer
(BigF/RefPack/FSH/FFN/PCMD/MO/audio/LAY/FCDB + wc98-* tools) with the
measured WC98 schema adaptations dispatched as a long-running task;
success gate = tools run against the WC98 tree with the exact probe
counts above.

## 2026-11-01 — Milestone 1 kickoff: environment, discovery, project skeleton

**Environment audit (confirmed):**
- Host: CachyOS/Arch, Linux 7.3, GNOME Wayland, AMD Radeon RX 7900 XTX
  (Navi 31), 32 cores, 30 GB RAM.
- Toolchain: GCC 16.2.1, Clang 23.1.4, CMake 4.4.5, Ninja 1.13.2, git
  2.56.0; Ghidra, objdump/readelf/nm/strings/xxd, glslangValidator,
  spirv-val, vulkaninfo present. `gh` CLI authenticated as `sergen213`.
- Disk: `/mnt/1st4TB` (1.1 TB free), `/mnt/2nd4TB` (2.8 TB free).

**Original game files located (user-supplied, this directory):**
- `EA-WorldCup98-Win95.iso` — 568,535,040 bytes, ISO 9660 CD001.
- `World-Cup-98_Win_EN-FR-DE-ES-IT_ISO-Version.zip` — same ISO, zipped.
- `WorldCup98_Extracted/` — installed-layout tree: `FIFAWC.EXE` (PE32
  i386, 1,762,304 B, imports GDI32/ADVAPI32/USER32/KERNEL32/WINMM/
  DDRAW/WSOCK32), `EACSND.DLL`/`EACSNDD.DLL`, `THRASH/` renderer
  back-ends (D3DA/D3DD, VOODOOA/VOODOOD, POWERVRA/POWERVRD), DIRECTX5
  setup, AUDIO (ANTH/CHNT/CRWD/MUSC/PBP/PHRS/PLRS/SFXS/TEAM), COMMON,
  FE/FE2/FEPACK, INGAME, VIDEO, USER, SETUP (InstallShield payload).

**Engine-family evidence (confirmed by signature scan):** `BIGF` x39,
`FCDB` x2, `BNK` x47, `MIDx` x1, `CRD` x2; 411 `.FSH`, 793 `.PAK`, 756
`.SMP`, 1232 `.PBK`, 87 `.SCI`, 29 `.LAY`, 12+12 `.DBI/.DBM`, 10 `.FFN`,
1 `.VIV`, 2 `.CRD`, 8 `.ASF`, 12 `.MDL`. This is the EA FIFA 97–99
family: same import set, same THRASH system, same format magics as the
previously reverse-engineered FIFA 99 tree.

**Existing work audit (confirmed):**
- `/mnt/2nd4TB/FIFA99/fifa99-native` — mature native FIFA 99
  reimplementation (C++26): BIGF/RefPack, FSH/FFN/FCDB, PCMD/MOAI,
  BNK/SCH/MicroTalk audio, LAY/VIS/INC UI, Vulkan renderer, SDL3
  platform, Qt6 launcher; 15 commits, extensive docs + Ghidra project.
  Primary code base to reuse; reusability being probed by running its
  tools directly against WC98 files.
- Audit dirs (`Kabus 22`, `etiyami`, `Pusu: Uyanış`, `IKV`,
  `CitroenC4Robot`, `SuperCan`, `BugsBunny`) are sibling native RE
  preservation projects (CMake/C++20, SDL3, GTK/Qt launchers, signed
  updater pattern, AppImage packaging) — methodology and packaging
  patterns reusable; no prior WC98 work found.
- No pre-existing World Cup 98 project exists; new project created here.

**Community/external research (confirmed sources):** PCGamingWiki
(D3D5/Glide/software renderers, 640x480-only, Glide caps at 63 FPS,
disc-check DRM, nGlide fix replaces `thrash/voodoo2a.dll` + registry
key `HKLM\...\EA Sports\World Cup 98`, `-no3dx` software-mode flag,
InstallShield `setup32.exe` workaround, saves in `<game>\USER`);
Wikipedia (engine "based on" FIFA: RTWC 98, 40 teams, World Cup mode +
15-match World Cup Classics with period kits/B&W-1930s presentation,
Tubthumping intro, UK commentary team); moddb community patches
(World Cup 98 Ultimate Patch — Win7-10 fix suite; FIFAWC.EXE 3rd-party
loader); fifa98edit + KUDr RefPack-Tool (`.dbi`/RefPack knowledge for
the family). WC98-specific vs RTWC98 facts kept separated per project
rule; carried-over assumptions recorded as hypotheses.

**C++ standard decision:** C++26 (finalized; fully usable on the GCC
16.2/Clang 23 dev toolchains, consistent with fifa99-native). CMake
preset `ci-cxx23` provided for older CI toolchains.

**Project created:** `world-cup98-native/` skeleton — CMake/Ninja C++26,
`core` interface library with bounds-checked little-endian
`ByteReader` (used by every planned format parser), first unit test
wired to CTest, MIT license, docs structure, dev log. No empty stub
modules (project rule).

**Research dispatched in parallel:** asset inventory (ISO + tree),
static RE of FIFAWC.EXE + EACSND/THRASH DLLs, fifa99-native tool
reusability matrix against WC98 data, external web dossier. Results to
be merged into `docs/research/`, `docs/reverse-engineering/`,
`docs/asset-formats/`.

**Next:** merge research results into docs, set up GitHub repo + CI,
then milestone 2 — format parsers (BIGF/RefPack, FSH, FCDB) with
WC98-verified tests, and the asset-inspector tool suite.

## Milestone 2 — fifa99-native format stack ported and WC98-locked (2026-11-01)

Ported the proven FIFA 99 toolchain (`/mnt/2nd4TB/FIFA99/fifa99-native`)
into this repo under the `wc98` namespace: core reader, BIG/RefPack,
FSH, FCDB, GEN4TBLS, FCLG, MO/ANIM, LAY, FFN, PCMD, PT/SCHl/BNK/UTK/EAXA/
MicroTalk audio, CRD, MIDx, plus the `wc98-extract`, `wc98-dbexplorer`,
`wc98-tbls`, `wc98-anim-inspect`, `wc98-lay-dump`, `wc98-art-inspect`
tools. Every WC98 deviation is now locked by CTest gates measured
against the original tree; the deltas table lives in
`docs/asset-formats/REUSABILITY.md` section 12. Headlines:

- 32 plain + 4 RefPack-wrapped `.BIG` archives; `ANIM.BIG` 48-byte
  prefix confirmed; 793 `.PAK` → 791 SHPI; header-only 11,628-byte PBK
  stubs in INSTALL need tolerant archive parsing.
- FCDB: 68 B team records; roster = 20 **little-endian** player-id
  slots (984/984 resolve; FIFA 99 big-endian resolves 31/984); squad =
  filled slots; byte-22 nibble = floor(squad/2); shirtStyle 6-bit;
  shirt numbers 1..255 with 255 = "no number" (720 players).
- FSH: 0x2D/0x29 palettes + global fallback (PAL.PAK resolver yields
  256 colours); full-tree scan gate.
- GEN4 G98: header `400\r\n`, KO int8, PK int16-LE, PL/FK int8, no
  AT/DF members.
- Audio: PT v2 `.SMP` 756/756 MicroTalk; `.PBK` = raw 1998 UTK 10:1
  payload members (final block = terminator — request (blocks-1)*256
  samples); 1998 PT v0 nests meta tags in 0x65 blocks (`.ASF`/`.DCT`)
  and 0xa2 blocks (`.BNK`); `ANTHEM.EAM` is a 76-byte 1998 MIDx
  dialect; layout 29/29 with 383 tolerated unknown-objecttype
  warnings + one measured numobjects quirk.
- `wc98-dbexplorer --json` fixed to escape Latin-1 EA name bytes as
  `\uXXXX` (JSON was invalid before).

Open research items for milestone 3 (recorded, gates structural):
1998 SFX `.BNK` member payload dialect (0x06/0x5a/0x91 tags + 0xa2
meta block), 1998 MIDx event dialect, `.SQF` EULZ, `.MDL` GEOM/SKEL,
`.DCT` EA-TREM video frames, typed UI layer, speech phrase indices,
TRE, ISO/VFS; plus the moddb Ultimate Patch download (Cloudflare —
see analysis/patches/PENDING.md).

## 2026-11-01 (late) — milestone 2 layer committed; audio gates pinned to measured reality
- Post-compaction reality audit: the working tree was ahead of git (formats/, tools/,
  tests/, core helpers untracked). This commit records the whole layer: 16 format
  decoders, 8 CLI tools, 10 CTest suites.
- AudioTest data gates re-pinned to full-tree measurements: BNK 246/246, PT v2 .SMP
  756/756 (MicroTalk tag 9, 66.0 M frames), SCHl 37/37 (8 .ASF + 29 .DCT audio),
  full-bank .PBK: 1,232 banks / 4,901 members, 249 (5.1%) decode through the FIFA 99
  framing. Layout is measured (160-bit blocks: 15-bit header + 144 payload bits,
  72 codes/block, final block terminates); the 1998 block decoder ALGORITHM is the
  open RE item (ghidra FUN_00432950). Test counter bug (.ASF/.DCT scan) fixed.
- ctest with WC98_TREE: 10/10 passed in 35 s (debug preset, -Wall -Wextra -Werror).
