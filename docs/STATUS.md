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
  793 loose `.PAK` = FSH textures, 791 unpack to SHPI), FSH 411 files /
  6,356 bitmaps, FFN 10, PCMD 14, GEOM/SKEL `.MDL` 12, MO rig 413 anim
  clips (`COMMON/ANIM.MO`), FCDB 2 + 22 STR0/STR1 `.DBI/.DBM` (24 leagues,
  56 teams, 1,782 players, 20-byte player records), LAY 29, FCLG 9,
  BNK 47, CRD 2, SCHl 37 (8 `.ASF` music incl. Tubthumping, 29 `.DCT`
  video), EULZ/Squish `.SQF` 49, TRE 9, `USER/OPTIONS.SAV` = `AFIFAFIF`.
- FIFA 99 native toolchain validated against WC98 (see REUSABILITY.md):
  core, BIG/RefPack, FSH (98.5%), FFN, PCMD, MO/ANIM (404/413),
  GEN4TBLS, FCDB names/players, FCLG, LAY grammar, all audio codecs
  (BNK 47/47, SCHl 37/37, CRD, MIDx), OpenAL mixer — COMPATIBLE.
- M4 rendering proven on real GPU offscreen (ctest gates green, PNGs visually verified
  2026-11-02): FEFLOW02 full-frame bitmap; `.LAY` composites of FE main menu (FEFLOW05) and
  CONTROL1 via `.FSH`/RefPack `.PAK` art resolution (LAYOUTS.BIG/IGLAY.BIG members); original
  trophy (`WCUPF.MDL`: 2 groups, 416 faces, 9 FSH textures) and stadium bowl (`BRDXF`) through
  the GEOM/SKEL parser — see reverse-engineering/{LAY_COMPOSITOR,MDL_GEOMETRY}_NOTES.md.
- Player art end-to-end via `wc98-model-export` (2026-11-02): skinned `PCMD` players
  (`HIG_30`: 28 nodes, 320 tris) posed by real `ANIM.MO` clips; kit atlases `PK###A/H.FSH`
  bind 10/13 materials (harvest mode); stands 15/21, trophy PCMD 1/1 textured.
  Kit/tag map + quirks (`hjbx`↔`hjbk`, palette-only face `hifa`) in MILESTONE4.md.
- WC98 deltas requiring adaptation (final, locked by port test gates —
  see REUSABILITY.md section 12): FCDB team record 68 B/17 words
  (FIFA 99: 80 B/20); roster = 20 little-endian u16 player-id slots
  (bytes 24..64; 984/984 slots resolve — FIFA 99's big-endian resolves
  31/984), squad size = filled slots, byte-22 nibble = floor(squad/2)
  (odd squads 11/13 measured; 40/56 teams slot-capped at 20); shirtStyle is 6-bit
  (measured max 32); FSH 0x2D/0x29 palette records + global fallback
  (PAL.PAK = RefPack→SHPI→type-0x01 palette, resolver yields 256
  colours); PT v2 .SMP streams are 756/756 MicroTalk (tag 9) with
  header-relative 0x88; .PBK = BIGF archives whose members each carry a
  20 B phrase record + `PT` v2 play track at offset 20 — all speech decodes
  via codec-tag routing, 6,964/6,964 members measured 2026-11-01 (gain
  calibration open; see asset-formats/AUDIO.md); 1998 PT v0 nests the
  standard meta tags (0x85 samples / 0x82 channels / 0x83 codec)
  inside 0x65 blocks (.ASF/.DCT audio) and 0xa2 meta blocks (.BNK
  SFX members, which also use a 1998 tag dialect 0x06/0x5a/0x91 —
  payload decoding open); 4 of 36 .BIG archives are RefPack-wrapped;
  ANIM.BIG carries the 48-byte prefix; INSTALL ships header-only
  11,628-byte PBK stubs (count field 1290) — archive parsing must be
  tolerant; G98*.FMT names (KO 68 B i8, PK 136 B i16-LE, PL/FK
  20,332 B i8, no AT/DF).

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
2. Speech/commentary indexing (decoder itself is COMPLETE: 6,964/6,964 `.PBK`
   members decode via PT-tag routing, measured 2026-11-01): PHRS.INF index
   semantics + event→phrase tables; decoded-speech gain calibration (levels
   hot, occasional saturation); rate default when tag `0x84` is absent
   (currently 22050 Hz — speech likely 8 kHz, unverified).
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

- M6 vertical slice playable headless (`wc98-match-demo`, gate `match_demo_frames`); commentary
  integrated in the exhibition loop via `app/commentary.{hpp,cpp}` (trigger seam FUN_004893ec model ->
  SpeechQueue -> PHRS segment fetch/decode -> OpenAL Mixer, null-mixer headless; gates
  `test_commentary`). Event->phrase pick = Hypothesis round-robin (pick logic FUN_0048d0dc undecomposed);
  FCLG cross-map probed negative (`analysis/pass7/fclg_probe.txt`); gain calibration + autovol ramp open.
  Front-end flow and HUD not wired yet.
- Raw hexdump appendices of game files are kept out of the repo
  (copyright); research-inbox/ is gitignored.
- Probe logs under /tmp are ephemeral; durable record = counts in docs.
- Community patches' internals (Ultimate Patch, 3rd-party loader)
  unpacked/analyzed = pending; treat as black boxes until then.
