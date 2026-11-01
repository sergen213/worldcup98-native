# Project Status — Confirmed Findings, Hypotheses, Unknowns, Limitations

Conventions: **Confirmed** = evidence in this repo (offset/string/magic +
counts in `docs/asset-formats/INVENTORY.md`, `docs/reverse-engineering/EXECUTABLE.md`).
**Hypothesis** = strongly supported; **Unverified** = thin evidence;
**Unknown** = open RE items. Source URLs in `docs/research/EXTERNAL.md`.

## Confirmed — engine & platform

- Engine family: EA FIFA 97–99 codebase. FIFAWC.EXE = Watcom linker 2.18
  PE32 (AUTO/DGROUP segments), built 1998-04-13, **not packed**, no CD
  protection beyond `GetVolumeInformation` (unlike FIFA 99's Petite+CMS).
  `$FIFAVER$` module banners (RENDER/IG/AI/CAMERA/TOURNAMENT/FCDB...).
- Renderer: `!thrash\%s.dll` loader with 34-name `_THRASH_*` GetProcAddress
  ABI; shipped back-ends Direct3D 2 (`d3da/d3dd`), 3dfx Glide 2.x via
  runtime `glide2x.dll` (`voodoa/d`), PowerVR PCX1/2 (`powervra/d`);
  software renderer in the exe (`-no3dx`). Locked 640x480, 16-bit color.
- Registry: `HKLM\SOFTWARE\EA Sports\World Cup 98` (3D Card, Thrash
  Driver, Triple Buffer, Hardware Acceleration, Language, CD Drive).
- Audio: `eacsnd.dll` runtime-loaded, version-checked, 13
  `_iSNDdirect*@` exports, uses `DirectSoundCreate`; formats BNKl/EA-XA,
  SCHl, CRD, PT streams (.SMP/.PBK), MIDx; **no** waveOut/MIDI path.
- Networking: WSOCK32 all-ordinal datagram-only (sendto/recvfrom/select;
  no connect/listen) + "DOS IPX compatible"/GAMELIST/MACHINE strings;
  modem play fully implemented (TAPI, AT S95=47, modems.cfg).
- Shipped binaries carry Watcom debug blobs (ELF-magic stubs, C source
  paths `C:\3rash\d3d\*.c`, `D:\audio\lib\win\sdirect.c`) — driver
  internals recoverable from the DLLs themselves; the exe is stripped.

## Confirmed — data (counts verified by scanning the user's files)

- `BIGF` archives 1,273 (incl. 1,232 `.PBK`), RefPack 797 payloads (all
  793 loose `.PAK` = FSH textures), FSH 411 files / 6,356 bitmaps,
  FFN 10, PCMD 14, GEOM/SKEL `.MDL` 12, MO rig 413 anim clips
  (`COMMON/ANIM.MO`), FCDB 2 + 22 STR0/STR1 `.DBI/.DBM` (24 leagues,
  56 teams, 1,782 players, 20-byte player records), LAY 29, FCLG 9,
  BNK 47, CRD 2, SCHl 37 (8 `.ASF` music incl. Tubthumping, 29 `.DCT`
  video), EULZ/Squish `.SQF` 49, TRE 9, `USER/OPTIONS.SAV` = `AFIFAFIF`.
- FIFA 99 native toolchain validated against WC98 (see REUSABILITY.md):
  core, BIG/RefPack, FSH (98.5%), FFN, PCMD, MO/ANIM (404/413),
  GEN4TBLS, FCDB names/players, FCLG, LAY grammar, all audio codecs
  (BNK 47/47, SCHl 37/37, CRD, MIDx), OpenAL mixer — COMPATIBLE.
- WC98 deltas requiring adaptation: FCDB team record 68 B/17 words
  (FIFA 99: 80 B/20), FSH 0x2D/0x29 palette records + global fallback,
  PT v2 stream header, UTK 10:1 speech flag byte, G98*.FMT names.

## Confirmed — product (external, cited)

- 40 teams (32 qualifiers + 8); World Cup mode with real 1998 groups,
  no ET/shootout in groups, golden goal/shootout in friendlies; 15-match
  World Cup Classics (unlocked progressively; B&W/sepia presentation
  for 1930–66 matches; pre-1970 matches no substitutions; Wolstenholme
  commentary in Classics; UK build Motson/Waddle + Lynam/Lineker).
- Disc check required; Glide mode 63 FPS cap; nGlide fix = replace
  `thrash/voodooa.dll` + registry fix (PCGW's "voodoo2a" is a typo).
- No prior World Cup 98 reimplementation exists (GitHub searched, 2026-11).

## Strong hypotheses

- Simulation is deterministic (netplay lockstep in FIFA 98/99 readmes;
  same engine) → enables replay/regression harnesses.
- LAN play is IPX-derived broadcast (datagram-only imports + GAMELIST
  broadcast strings); DirectPlay absent.

## Unverified / Unknowns (next RE targets, ranked)

1. FE typed UI layer: LAY object-type vocabulary (15/80 keys shared,
   7 unknown types) + FE art pipeline (SQF/MDL/palettes).
2. Speech/commentary: PHRS.INF index + nested-BIGF phrase banks +
   event→phrase tables; UTK 15-bit gain calibration.
3. `.MDL` GEOM/SKEL (FE 3D titles), `.SQF` EULZ sprites, `.DCT` EA-TREM
   video frames.
4. `.SCI/.TBI` AI scripts (unparsed in FIFA 99 too), GEN4TBLS AT/DF.
5. Save/replay/presentation files: see
   `docs/reverse-engineering/SAVES.md` (options.sav layout hypothesis,
   `rply%04d.dat`/`FEREPLAYBUFF` replay system, `.his`/`.tri` Classics
   data, network.dat/dial.dat) — Ghidra xref pass + Wine-generated
   reference saves pending.
6. Official patch lineage: `fifawc98_patch.zip` + moddb Ultimate Patch
   contents (download & diff pending).

## Known limitations

- Nothing playable yet: current scope = research + asset/format layer.
- Raw hexdump appendices of game files are kept out of the repo
  (copyright); research-inbox/ is gitignored.
- Probe logs under /tmp are ephemeral; durable record = counts in docs.
- Community patches' internals (Ultimate Patch, 3rd-party loader)
  unpacked/analyzed = pending; treat as black boxes until then.
