# FIFA 99 native → World Cup 98 reusability matrix

Probed 2026-11-01. Evidence-based: every verdict below is backed by a clean-build fifa99-native tool
run, or by a probe program compiled against the *actual* fifa99-native static libs
(`build-wc98probe/formats/libf99_formats.a` + `core/libf99_core.a`). No fifa99-native source file was
modified; all probes live in `/tmp/wc98probe-src`, all outputs in `/tmp/wc98probe-out`.

- Repo: `/mnt/2nd4TB/FIFA99/fifa99-native` (build dir `build-wc98probe`, Release, Ninja)
- WC98 data: `/mnt/1st4TB/WorldCup98/WorldCup98_Extracted` (+ ISO `EA-WorldCup98-Win95.iso`)
- FIFA 99 reference: `/mnt/2nd4TB/FIFA99/original/rip/FIFA_99/data`, `/mnt/2nd4TB/FIFA99/original/iso/extracted`

## 0. Executive summary

**The low-level data spine of fifa99-native is reusable as-is for WC98: BIG container, RefPack,
FSH (98.5 % of files), FFN fonts, PCMD models, MO/ANIM rig, GEN4TBLS AI tables, EA-XA v1 audio
(BNK + SCHl + CRD + MIDx), LAY text grammar, FCLG message catalog, PE/EXE reader.**
The in-game asset pipeline (stadium/player models + textures + animations + sound + music)
exports from WC98 data today with unmodified FIFA 99 tools.

Four things break, each with a precise, measured delta:

1. **FCDB data tables** — 1998 team record is 68 bytes (17 u32 words), FIFA 99's is 80 (20).
   `f99-dbexplorer` SIGABRTs: `FCDB team record size 68 != 80`. Name tables (STR0/STR1),
   league table, FCDB header, player records (20 B, identical bit layout) and FC`<lang>`.BIN
   (FCLG) are byte-compatible.
2. **FE UI** — WC98 LAY uses a different key vocabulary (15 of 80 WC98 keys shared with FIFA 99's
   112) and 7 unregistered `objecttype` values (32/37/42/43/44/45/47 = 122 of 398 object
   sections); WC98 shape references are name-based (`fname`/`vpath`/`sname`/`releasedshape`) vs
   FIFA 99 id-based (`normalshapeid`/`curFname`). `f99-ui-render --game WC98` → 0 screens.
3. **Speech** — WC98 has no `.STR`/`.OFF`/`SPEECH.INF`. Commentary/player/team/chant banks are
   `.SMP`/`.PBK` (`PHRS.INF` "V8-7-4" index + nested BIGF phrase banks) with a PT header
   version 2 (`PT\0\2`, extra u32 header size, `0x88` = header-relative data offset).
   `.PBK` members carry a **20 B phrase record + a `PT` v2 play track at offset 20**; routing by
   codec tag decodes **6,964/6,964** bank members (MicroTalk tag 9: 6,851; EA-XA tag 7: 113 —
   measured 2026-11-01). FIFA 99's **UTK (MicroTalk 10:1) decoder decodes the WC98 payloads**
   (`utk::Decoder::decodeSpeechPayload`, continuous bitstream + one FIFA-99-style header block);
   gain calibration remains open. Chant `.PBK`
   members (PT tag `0x83`=7 = EA-XA) decode cleanly through the existing EA-XA path.
4. **Path layout** — WC98 has no `FIFA_99/DATA/` tree; `f99-gamedata detect/validate` reject it.
   Every module that resolves data through the FIFA 99 manifest/VFS needs a WC98 layout
   descriptor (path map in §9).

**Bottom line: reuse is a data-layer port, not a rewrite.** Audio codecs, image/font/model
readers, container/codec plumbing, the LAY/Femsg grammar, the rig and the AI tables are kept.
New work concentrates in: FCDB 1998 schema, WC98 UI vocabulary + FE art (SQF/EULZ sprites,
GEOM/SKEL `.MDL`), speech containers/index + PT-v2, EA-TREM video frames, and the
install/layout manifest.

## 1. Build (clean, tools only)

```
cd /mnt/2nd4TB/FIFA99/fifa99-native
cmake -G Ninja -B build-wc98probe -DCMAKE_BUILD_TYPE=Release          # exit 0
ninja -C build-wc98probe f99-inventory f99-extract f99-dbexplorer \
    f99-anim-inspect f99-model-export f99-audio-extract f99-gamedata \
    f99-exeinfo f99-ui-render                                          # all exit 0
```
Deps detected: SDL3 3.2.12, OpenAL, FFmpeg 7.1.2, libpng 1.6.53, zlib; GCC 16.2 (C++26).
Tool binaries: `build-wc98probe/tools/<name>/f99-<name>`.

Probe build pattern (links the fifa99 libs directly, testing the *modules*, not tool wrappers):

```
g++ -std=c++26 -O2 -I$N/core/include -I$N/formats/include probeN.cpp -o probeN \
    $N/build-wc98probe/formats/libf99_formats.a $N/build-wc98probe/core/libf99_core.a \
    -lpng $(pkg-config --libs libavcodec libavutil libswscale)
```

## 2. Inventory (magic-level recognition)

```
build-wc98probe/tools/inventory/f99-inventory WorldCup98_Extracted /tmp/wc98probe-out/wc98_inventory.json
# exit 0
```
`/tmp/wc98probe-out/wc98_inventory.json`: **6869 loose files / 560.8 MB**.
`filesByFormat`: `FSH 1202` (411 `.FSH` + 791 `.PAK`), `BIG 45`, `BNKl 47`, `SCHl 37`
(8 `.ASF` + 29 `.DCT`), `PCMD 14`, `DBI 12`, `FFN 10`, `FCLG 9`, `CRDl 2`, `MIDx 1`,
`text 617`, `MZ 2460` + `ELF 30` + `RIFF 75` + `TREE 9` + `WinHelp 4` (DIRECTX5/SETUP/AUTORUN/
INSTALL = non-game), **`unknown 2285`** (`.PBK 1228`, `.SMP 756`, `.SCI 87`, `.SQF 49`,
`.MDL 12`, `.DBM 12`, `.BIN 21`, `.LAY 29`→text, `options.sav` AFIF …).
BIG members (recursive): `BIG 2580`, `EA-010B 416`, `PCMD 123`, `FSH 32`, `FFN 16`, `text 132`,
`unknown 4691`. `refpackPayloads 797`, **`crcTrailerFiles 0`**, `errors 1023`.

Identification gaps to close for WC98: `.PBK` (BIGF at 0x30 → detected as BIG at offset 0 only in
~1/3 of cases; `errors 1023` are mostly `.PBK`/`.HDR` parsed as BIG at offset 0), `.SMP` (PT v2),
`.SQF` (EULZ), `.MDL` (GEOM/SKEL), `.DCT` (SCHl+mTCD), `.LAY/.VIS/.INC` (classified as text,
not layout grammar), `.MO/.VIV` anim/table members, `USER/options.sav` (AFIF).

## 3. Container + compression layer — **COMPATIBLE**

```
tools/extract/f99-extract  <file|dir> <out>
# full sweep over every WC98 .BIG/.MO/.VIV/.HDR: /tmp/wc98probe-out/bigall.log
# selected: INGAME/STADIUM/ADBOARD.BIG, INGAME/MISC/ANIM.BIG, COMMON/ANIM.MO,
#           COMMON/GEN4TBLS.VIV, FE/PACK/LAYOUTS/LAYOUTS.BIG, FE/PACK/FEFONTS.BIG
```
- **34/38 WC98 `.BIG` parse natively** ("BE" signature, 4-char member names, 564/705 named);
  **4 are whole-file RefPack wrapped** (`FE/PACK/FEFONTS.BIG`, `IGFONTS.BIG`, `LAYOUTS.BIG`,
  `FE/INGAME/IGLAY.BIG`; members `0000/0001…`) — `refpack::decompressIfNeeded` (the path
  `f99-extract` uses) unpacks all of them; raw `BigArchive::parse` throws. Rule: always
  `decompressIfNeeded` first (the fifa99 tools already do).
- **RefPack: 793/793 loose WC98 `.PAK` decompress to declared FSH sizes** (the `.PAK` =
  RefPack(FSH) form also seen in FIFA 99 `*.BIG` members); + 4 wrapped archives; + 416
  EA-010B RefPack members inside stadium archives. bigall sweep totals:
  **39 archives → 705 members, 793 RefPack payloads, 31 FSH files → 988 images, 10 FFN, 1 error**.
- **Nested archives**: `AUDIO/*/*.PBK` = 48-byte header + `BIGF` at 0x30; inner members are
  themselves BIGF (`PHRS.PBK` outer = 1290 phrase members; member payload = nested BIGF, e.g.
  phrase 0: `declared 14216 hdrSize 72 members 6`) — `BigArchive::parse` handles nesting;
  only the 48-byte prefix needs skipping.

## 4. FSH / FFN — **COMPATIBLE (FSH: 98.5 %, two palette deltas)**

```
tools/extract/f99-extract INGAME/STADIUM/PALSTAD.FSH INGAME/PLAYER/TEXTURES/COM_10.FSH \
    INGAME/MISC/B08_10.FSH FE/GLOBAL/BALL.FSH FE/BRDXF.FSH FE/ERAS10.FFN /tmp/wc98probe-out/fsh
# PNG out: /tmp/wc98probe-out/fsh/<file>.png/<tag>.png  e.g. 000_bl00.png; ERAS10.FFN.png 128x172
```
Probe over all 1204 WC98 `.FSH`+`.PAK`: **1184 parsed, 6356 bitmaps decoded**
(5817 8-bit-pal, 353 rgb565, 123 argb4444, 60 argb8888, 2 4-bit-pal, 1 rgb888;
306 RefPack-compressed pixel records; dirId `GIMX` 1184/1184).
**20 failures, 3 causes:**
- 15 × `FSH paletted bitmap without palette` — WC98 FE bitmaps (type `0x7B`) chain to palette
  record **`0x2D` (9 files) / `0x29` (10 files)**; the fifa99 palette reader implements
  `0x22/0x24/0x2A/0x2E` and `docs/formats/FSH.md` lists `0x2D/0x29` as "not present, not
  implemented". Attached-record census (chains of all 1168 WC98 FSH/PAK):
  `0x2A×4155, 0x24×142, 0x6F×145, 0x29×10, 0x2D×9, 0x2E×2, 0x22×5` → implement the 16-bit
  `0x2D/0x29` palette record. Files: `FE/FEFLOW02/80*.{FSH,PAK}`, `FE/GLOBAL/CHAT.FSH`,
  `FE/PACK/GLOBAL/CHAT.PAK`, `FE/PACK/FEFLOW*`, `FE/PACK/FEFLOW14/25/72/73/77/79/80/89.PAK`.
- 3 × `FSH palette index 255 >= 170` (`FE/GLOBAL/CONTR.FSH`, `FE/PACK/GLOBAL/CONTR.PAK`) and
  `20 >= 20` (`FE/PACK/FEFLOW12.PAK`) — WC98 bitmaps index past the attached record's declared
  count → WC98 uses the 256-colour **global FE palette** (FIFA 99 knows only the PM*.BIG
  255-entry special case; WC98 files carry **no `!pal` directory tag: 0 of 410** vs FIFA 99 1/417).
  Needs a global-palette fallback (candidate source: `FE/PACK/GLOBAL/PAK10042.BIG` members
  `PAL0001…PAL0039`, 418-byte 104-entry palette records).
- 2 × `not a SHPI file` — `INGAME/MISC/MISCART.PAK`, `INGAME/PLAYER/MODELS/MODELS2.PAK`
  (`.PAK` that are not FSH; extension heuristic, not a format delta).

**Size semantics**: FIFA 99 writes a `\0cRC` trailer — every FIFA 99 `.FSH` is
`declaredSize + 8 == fileSize` (416/416); **WC98: `declaredSize == fileSize` in 100 % of files
(0 trailer files)**. Trailer-trimming code is a no-op on WC98 (safe); size-asserts must not
require the trailer.

**FFN: 10/10 WC98 fonts parse** (version 100, flags 0/1 — FIFA 99 uses 0,1,9,11; 2205 glyphs),
including 16 font members inside RefPack-wrapped `.BIG` (FEFONTS/IGFONTS → PNG+JSON).

## 5. FCDB / DBI / message catalog — **header+names COMPATIBLE, data schema PARTIAL**

```
tools/dbexplorer/f99-dbexplorer COMMON --lang ENG --validate
```
Exact output:
```
leagues=24 teams=56 players=1782
01 2345 England …            (league + 56 team + 1782 player names print fine)
terminate called after throwing an instance of 'f99::FormatError'
  what():  FCDB team record size 68 != 80
```
| table | WC98 | FIFA 99 | verdict |
|---|---|---|---|
| FCDB header (`FCDB` + version u32=501 + 3 table pointers) | identical | identical | **COMPATIBLE** |
| STR0 (leagues/teams) + STR1 (players) name tables | 24 / 56 / 1782 | 53 / 294 / 5858 | **COMPATIBLE** (counts differ only) |
| League table (variable-size 5-byte records) | same format | same | **COMPATIBLE** |
| Player records @ pointer 3 | **20 B** (5 u32; w0 = `id:14\|nat:11\|overall:7` — WC98 `A00C0022` → id 34 / nat 48 / overall 80) | **20 B**, same layout | **COMPATIBLE** |
| **Team records** | **68 B** (17 u32); w0 = top-3-bit flags + id (WC98 `E000001A`); **budget bits 11..27 = 0**; roster u16 list base byte 24 | **80 B** (20 u32); `id:11 + budget:17` in w0; roster base byte 28 | **INCOMPATIBLE — 1998 schema; record-size-aware unpack + WC98 bit layout** |
| `FC<lang>.BIN` (FCLG version 1) | 9/9 parse, 2031 strings (ENG) | same format | **COMPATIBLE** (feeds `ui`/FemsgTable) |

All 12 WC98 `.DBI` recognized by magic; `FCDB.DBI`/`FCDB_ENG.DBI` parse through the name tables.
**WC98 has 0 `.TBL` files** — FIFA 99's `.TBL` tables do not exist in the 1998 layout.
`tools/f99-extract COMMON/FCDB.DBI` → `FAIL FCDB.DBI: FCDB team record size 68 != 80` (clean
error, no crash in the tool).

## 6. Animation / models / rig — **COMPATIBLE (1 minor anim delta)**

```
tools/models/f99-anim-inspect list COMMON/ANIM.MO            # 413 clips listed
tools/models/f99-anim-inspect list INGAME/MISC/ANIM.BIG      # 5 members
tools/models/f99-model-export INGAME/STADIUM/ADBOARD.BIG:adboard.pc \
      --fsh INGAME/STADIUM/ADBOARD.BIG:adboard0.fsh --out /tmp/wc98probe-out/gltf --preview
tools/models/f99-model-export INGAME/PLAYER/MODELS/MED_70.PC --fsh INGAME/PLAYER/TEXTURES/COM_70.FSH \
      --pose COMMON/ANIM.MO:sspecz6 --out /tmp/wc98probe-out/gltf2
tools/models/f99-model-export INGAME/PLAYER/MODELS/MED_70.PC --fsh INGAME/PLAYER/TEXTURES/COM_70.FSH \
      --pose /mnt/2nd4TB/FIFA99/original/iso/extracted/DATA/BE/ANIM.BIG:fronte --out /tmp/wc98probe-out/gltf2
```
- **14/14 WC98 `.PC` parse** (PCMD version `0x100`, exporter tag `STADEX` = FIFA 99); 131
  `.PC` members recognized inside archives (123 in inventory).
- `f99-model-export` on WC98 data: **adboard.pc → glTF, 152 tris, 19/19 textures bound**,
  preview PNG; **MED_70.PC (+COM_70.FSH) → 228 tris, 5/14 textures bound** (player-texture
  name→tag binding partially unresolved — a naming convention, not a decode failure).
- **Rig identical**: 413 clips in `ANIM.MO`, **404 parse (97.8 %)**; header signature
  `hdrSize 116, quats 14, triplets 5, flags 0xD1…0xDD` = FIFA 99 rig (33 skinned bones).
  Cross-game pose proof: WC98 `sspecz6` on WC98 `MED_70.PC` → **OK, 36 bones**; FIFA 99
  `fronte` on the WC98 model → **OK, 36 bones**.
- 9/413 clips: header variant `hdrSize 2817, keys N, ticks 116` →
  `ANIM: bad key block size 164` (1998 key-block layout, 2.2 %).
- FE `.MDL` (`BRDXF/SQRDXF/BRDTX/TROPHY`, 12 files) = `GEOM`+`SKEL`+`MDL0` chunks —
  **FIFA 99 has no `.MDL` at all** (0 files in the FIFA 99 tree, no module in
  `formats/src/`) → 1998-only format, decoder needed.

## 7. Audio — **codecs COMPATIBLE; containers/indexes PARTIAL**

```
tools/audio-extract/f99-audio-extract WorldCup98_Extracted /tmp/wc98probe-out/audio --threads 8
# [info] 257 entries, 0 failed, 0 file errors -> /tmp/wc98probe-out/audio/manifest.json
#   by dir: ANTH 81, SFXS 140, CRWD 28, MUSC 8;  codec: ea-xa-v1 196, pcm16le 58;  0 speech
```
- **`.BNK` (BNKl): 47/47 parse and decode** (PT version 2; FIFA 99 has 2 and 4),
  15,180,545 sample frames, rates 8000–22050 Hz (FIFA 99: 11025/22050) → EA-XA v1 path works.
- **`.ASF` music (SCHl v1 + `SCDl`): 8/8 decode** EA-XA v1 22050 Hz stereo (1.2–4.0 M frames:
  `BE_PAU1..5`, `FE_BOYY/FLUK/HARD/TUBU`).
- **`.DCT` (VIDEO): `scanSchl` + audio decode on 29/29** (EA-XA v1, 22050 Hz, ~0.44 M frames)
  → the audio half of WC98 video is fully readable.
- **`.CRD`: 2/2** (version 2, 24/61 cues; `AUDIO/CRWD/CROWD.CRD`) and **`.EAM` (MIDx): 1/1**
  (`ANTHEM.EAM`: 20 events, endTick 3200) — same model as FIFA 99.
- **`.SMP` / `.PBK` (speech, 401 + 1356 files)**: PT header **version 2** (`PT\0\2`, platform
  field `0x0200` on 756/756 sampled; FIFA 99 = `0x0000` v0 with no size field), `u16 hdrSize`
  32/36/44, tags `{0x06, 0x83=codec, 0x85 samples, 0x88=hdrSize, 0x8A}` (chants add `0x86/0x87`
  loop tags). Payload = **EA MicroTalk 10:1** (measured 1.34–1.57 bits/sample over 401 `.SMP` =
  the 16-bit→10:1 ratio). `utk::Decoder::decodeBlock` **decodes 125/125 sampled clips**
  (120 `.SMP` + 5 `.PBK` members) once a FIFA 99 block-flag byte (`0x01`) is prepended;
  sample counts match exactly (`A_SCH001.SMP`: 74865 samples) and levels are speech-like
  (mean ≈ 3800–5800), **but peak = 32767/32768 in 125/125 decodes → int16 saturation:
  the 1998 15-bit stream-header/gain layout needs calibration**.
  Chant `.PBK` members (PT tag `0x83`=**7=EA-XA**) decode cleanly through the existing EA-XA
  decoder (`CHNT/C0001800.PBK#0`: 37497 samples, peak 23755, mean 4315).
  Full-bank measurement (rewritten test gate, all 1,232 `.PBK`, measured 2026-11-01): every
  member = a **20 B phrase record + `PT` v2 play track at offset 20**;
  `decodePlayTrack(m, 20)` + nested-BIG recursion decodes **6,964/6,964 members (100%),
  0 failures** — codec census by tag `0x83`: codec 9 (MicroTalk) = 6,851, codec 7 (EA-XA) = 113;
  total 508,302,490 samples; 1,044 stub/other members (BIGF headers referencing data past the member). The earlier
  "249/4,901 gated on a 1998 block decoder at `FUN_00432950`" was an artifact of the test loop
  feeding raw member bytes into UTK framing while ignoring the PT header at offset 20: payload
  sizes mod 20 span {0,4,8,12,16} uniformly — NOT block-aligned. Correct model: ONE continuous
  UTK bitstream per member with a single FIFA-99-style header block prepended (=
  `utk::Decoder::decodeSpeechPayload`); decoded output verified speech-like (F0 ≈ 174–176 Hz,
  periodicity 0.71/0.38; codec-9 spectral flatness ≈ 0.32 vs EA-XA chant ≈ 0.64). Open: gain
  calibration (RMS ≈ 9,000–11,500 int16, occasional saturation), rate-tag default
  (`0x84` absent on 6,957/6,964; speech likely 8 kHz — unverified).
  `Bnk::decode` on `.SMP` → `MicroTalk in BNK not used by FIFA 99` (PT-v2/`.SMP` path not
  implemented in the BNK reader).
- Speech indexing: FIFA 99 = `.STR` + `.OFF` + `SPEECH.INF/.SID`. WC98 has **none**:
  `AUDIO/PHRS/{PHRS.PBK, PHRS.HDR, PHRS.INF}` (`INF` = 28 B: `V8-7-4` + 5 u32 counts; outer
  BIGF 1290 members → each phrase = nested BIGF of PT samples), per-voice
  `AUDIO/{PLRS,TEAM,CHNT,PBP}/*.PBK` flat banks (`PHRS.INF/HDR` are the only sidecars; `PHRS/TYPE.PBK` is a header-only phrase mirror) → **`audio/speech` (`.STR/.OFF/SPEECH.INF`) =
  INCOMPATIBLE; new WC98 index + container reader required** (payload codecs already work).
- `f99_audio` mixer (OpenAL) is data-agnostic → reusable (the 257 WAVs above are its contract).

| module | verdict |
|---|---|
| `audio/bnk` (BNKl + PT v0 + EA-XA v1) | **COMPATIBLE** codecs / **PARTIAL** parser (needs PT v2 branch: `0x0200`, hdrSize, `0x83` 7=EA-XA/9=MicroTalk, header-relative `0x88`) |
| `audio/schl` (v1 demux + EA-XA) | **COMPATIBLE** (8/8 ASF, 29/29 `.DCT` audio) |
| `audio/crd`, `audio/midx` | **COMPATIBLE** (2/2, 1/1) |
| `audio/utk` (MicroTalk 10:1) | **COMPATIBLE pending gain calibration** — full-bank `.PBK` speech decodes 6,964/6,964 via PT-tag routing + `decodeSpeechPayload` (measured 2026-11-01); open: output levels hot (saturation) |
| `audio/speech` index | **INCOMPATIBLE** — WC98 = `PHRS.INF` V8-7-4 + `.PBK` + `.INF/.HDR/.TYPE` |
| `audio/mixer` | **COMPATIBLE** |
| `tools/audio-extract` | **PARTIAL** — 257 WAV/0 fail; 0 speech entries |

## 8. UI / LAY / scripts — **grammar COMPATIBLE, typed screen layer NOT**

- `LayoutDocument::parse` on **29/29 loose WC98 `.LAY`**: 953 sections, 12522 key=value props,
  1 warning file; `[screen]` + `numobjects` present in 29/29 and `numobjects` == object-section
  count in **29/29** → **the LAY text grammar + encoding convention (box-drawing → U+F000) are
  fully reusable**. In archives: `FE/PACK/LAYOUTS/LAYOUTS.BIG` (RefPack-wrapped) = 99 `.LAY` +
  1 `.VIS` + 1 `.INC`; `FE/INGAME/IGLAY.BIG` = 29 `.LAY`; `FE/INGAME/IGSCHS.BIG` = 38 `.T`.
- Typed layer (`ui/` = `ScreenLayout`/`Screen`/composite/Resources):
  - objecttype sets — WC98 `3(174), 11(135), 32(21), 37(20), 42(15), 43(13), 45(8), 47(10),
    44(1), 1027(10)` vs FIFA 99 registered `0,2,3,4,6,9,11,12,14,15,16,17,19,20,21,22`:
    **7 WC98 types (122/398 object sections) have no FIFA 99 class**;
  - key vocabulary — WC98 80 keys, FIFA 99 112, **shared 15** (`fname,height,width,xposition,
    yposition,objectname,objecttype,sname,vpath,fontid,colour,disabledshapeid,hilitedshapeid,
    shapeid,barshapeid`); WC98-only 64 (`releasedshapeid×131, pushedshape×46, disabledshape×46,
    hilitedshape×46, boxid, gametextid, gtext, ibr*, is*…`), FIFA 99-only 96
    (`normalshapeid/depressedshapeid/hilitedshapeid ×876, cursorshapeid, zposition, fontname…`).
    WC98 references sprites by **name+path** (`fname`/`vpath`/`sname`, `..\global\` style),
    FIFA 99 by **shape id + file** (`curFname`/`curShapeID`);
  - `f99-ui-render --game <WC98> --out ui/` → `screens: 0 rendered, 0 failed; references: 0
    resolved, 0 unresolved` (the screen walk `DATA/FEART/GLOBAL`, `DATA/FE/LAYOUTS`, `BE`,
    `BEART` finds nothing in the WC98 tree; the same tool renders the FIFA 99 FE).
- `FemsgTable` (FCLG `FC<lang>.BIN`): 9/9 parse → **COMPATIBLE** (§5).
- BE/FE scripts: WC98 `.T` = 461 loose + 38 members (+`.SRT/.IDX` sidecars) against FIFA 99's
  `.T/.SRT/.IDX` triplets in `DATA/BE` → same file family; fifa99-native never executes scripts
  (`docs/GAMEDATA.md`) → **UNTESTED semantics; needs path map + per-script validation**.
  `COMMON/*.SCI` + `*.TBI` (AI scripts/tables) = unparsed in FIFA 99 too → parity.

## 9. Install / VFS / manifest / ISO — **PARTIAL (needs WC98 layout descriptor)**

```
tools/gamedata/f99-gamedata detect WorldCup98_Extracted    → rejects: no FIFA_99/DATA/ (FIFA99 manifest: 58 rows, 1270 MiB)
tools/gamedata/f99-gamedata detect EA-WorldCup98-Win95.iso → rejects: not a FIFA 99 ISO: no SETUP.INS at offset 0x13A810
tools/gamedata/f99-gamedata validate WorldCup98_Extracted  → 13212 files / 2321 MiB, 0 warnings, "not a FIFA_99 tree: no DATA/ directory"
```
- `IsoImage::probe` **works on the WC98 ISO** (2016 sectors, mode1/2352, 17 tracks, session LBA
  40000) → ISO plumbing reusable; the WC98 disc is multi-session/OS2-boot (FIFA 99 = ZIP'd rip +
  `SETUP.INS` at sector 128772).
- VFS plumbing (RefPack + BIG member reads) works (§3); **manifest + `GameLayoutDescriptor` +
  `Fifa99Paths` are FIFA 99-only** → a WC98 manifest/layout descriptor is the biggest plumbing
  task.
- Save/config: `USER/options.sav` = `AFIF` magic (same family FIFA 99 documents in
  `docs/formats/OPTIONS_SAV.md`) → layout needs a 1998 variant check (small).

**WC98 → FIFA 99 path map (proposed VFS overlay):**

| WC98 | FIFA 99 concept |
|---|---|
| (no prefix) | `FIFA_99/DATA` |
| `COMMON` | `DATA/CMN` (FCDB*, FC`<lang>`.BIN, GEN4TBLS.VIV, ANIM.MO) |
| `COMMON/{*.SCI,*.TBI,GEN4TBLS.T}` | `DATA/CMN/AI` (unparsed in both games) |
| `AUDIO/{ANTH,MUSC,SFXS,CRWD}` | `DATA/AUDIO/{ANTH,MUSC,SFXS,CRWD}` (same codecs: BNK/SCHl) |
| `AUDIO/{PLRS,TEAM,PHRS}` | `DATA/AUDIO/SPEECH` (1998 = `*.PBK`/`PHRS.*`, not `.STR/.OFF`) |
| `AUDIO/{CHNT,PBP}` | `DATA/AUDIO/CHANTS` (+ commentary schedule `.SMP` ↔ BE PBP) |
| `FE` + `FE/PACK/{FEFLOW*,GLOBAL,LAYOUTS,*.PAK}` | `DATA/FEART/*` + `DATA/FE/LAYOUTS` |
| `FE/INGAME` + `FE/PACK/INGAME` | `DATA/POPUPS` |
| `INGAME/MISC` | `DATA/BE` + `DATA/BEART/{BELOW,FLOW}` (`.FSH/.PAK/.BIG/.T/.SRT/.IDX/.LAY`) |
| `INGAME/STADIUM` | `DATA/STADIUM` |
| `INGAME/PLAYER/{MODELS,TEXTURES}` | `DATA/PLAYER/{MODELS,TEXTURES}` |
| `VIDEO/*.DCT` | `DATA/VIDEO/*.MAD` (different codec, same role) |
| — | `FE/FTX*_{1..15}.SQF` (EULZ sprite art) and `*.MDL` (GEOM/SKEL) have **no FIFA 99 equivalent** |
| `USER/options.sav` (AFIF) | `user/save.bin`/`default.sav` (1999 variant) |
| `.PAK` = RefPack(FSH), **0 CRC trailers**, 4 RefPack-wrapped `.BIG` | `.PAK` = SHPI, CRC trailer on every archive |
| `COMM`, `FE2`, `FEPACK`, `THRASH` | near-empty (VER.DAT/4 files) — ignorable |

## 10. Video — **INCOMPATIBLE (frames), PARTIAL (audio, container)**

- **0 `.MAD` files in WC98**; `f99-audio-extract` finds 0 videos; `Mad::parse` rejects `.DCT`
  (magic) — by design.
- WC98 `VIDEO/*.DCT` (29) = `SCHl` container + `mTCD`/`DCTm` chunks (EA-TREM, same codec family
  as `FEUPRTRE.SBF`); audio half decodes (§7) → **frame decode = new decoder**; container
  demux reusable.

## 11. The matrix

| fifa99-native module / tool | verdict | evidence / delta |
|---|---|---|
| `core` (Bytes/ByteReader/File/Log) | **COMPATIBLE** | used by every probe/tool above |
| `formats/big` | **COMPATIBLE** | 34/38 native + 4 RefPack-wrapped; nested BIGF/`.PBK`@0x30; 705 members |
| `formats/refpack` | **COMPATIBLE** | 797/797 payloads to declared size |
| `formats/fsh` | **PARTIAL** | 1184/1204, 6356 images; +0x2D/0x29 palettes, +global-palette fallback (1.5 % of files); 0 CRC trailers in WC98 (trimming is a safe no-op) |
| `formats/ffn` | **COMPATIBLE** | 10/10 loose + 16 members; version 100, flags 0/1 |
| `formats/fcdb` | **PARTIAL** | header/STR0/STR1/league/player byte-identical (20 B records); **team 68 vs 80 B** → 1998 schema; no `.TBL` in WC98 |
| `formats/layout` | **COMPATIBLE** (grammar) | 29/29 parse; 953 sections; invariants hold; archives via RefPack BIG |
| `formats/femsg` (FCLG) | **COMPATIBLE** | 9/9 `FC<lang>.BIN`, 2031 strings |
| `formats/moai` + rig | **COMPATIBLE** | 404/413 clips (97.8 %); rig 14q/5t identical; cross-game poses work; 9 clips hdr-2817 variant |
| `formats/pcmd` + `models/model_export` | **COMPATIBLE** | 14/14; glTF+textures+preview from WC98; player texture-name binding partial (5/14) |
| `formats/gen4tbls` | **PARTIAL** | KO 68 / PL 20332 / PK 136 / FK 20332 byte-layouts **identical**; `G98*` vs `G99*` member names; AT/DF absent; parser keys on `G99HD` |
| `formats/utk` | **COMPATIBLE** | full-bank WC98 speech: 6,964/6,964 `.PBK` PT members via `decodeSpeechPayload` + PT-tag routing (measured 2026-11-01); open: gain calibration (levels hot) |
| `audio/bnk` (+PT v0) | **COMPATIBLE** codecs / **PARTIAL** parser | 47/47 BNK decode; PT v2 branch needed (`0x0200`, hdrSize, tag `0x83` 7=EA-XA/9=UTK, relative `0x88`) |
| `audio/schl` | **COMPATIBLE** | 8/8 ASF + 29/29 `.DCT` audio, EA-XA v1 |
| `audio/crd` / `audio/midx` | **COMPATIBLE** | 2/2, 1/1 |
| `audio/speech` (`.STR/.OFF/SPEECH.INF`) | **INCOMPATIBLE** | WC98 = PBK/PHRS/SMP + `PHRS.INF` V8-7-4 + `.INF/.HDR/.TYPE` |
| `audio/mixer` (OpenAL) | **COMPATIBLE** | data-agnostic; 257 WAV |
| `formats/mad` + video frames | **INCOMPATIBLE** | 0 `.MAD`; WC98 = `.DCT` TREM `mTCD` (audio via SCHl works) |
| `.SQF` sprites (EULZ) | **INCOMPATIBLE** | 49/49 EULZ magic, RefPack 0/49; no EULZ code in fifa99-native |
| `.MDL` FE models (GEOM/SKEL) | **INCOMPATIBLE** | 12 files; FIFA 99 has no `.MDL` (0 files, no module) |
| `ui` (ScreenLayout typed model, composite, Resources, EXE screen graph) | **INCOMPATIBLE as-is** | 15/80 shared keys; 7 unknown objecttypes (122/398 sections); name/path shape refs; `f99-ui-render` 0 screens |
| BE/FE script engine (`.T/.SRT/.IDX`, `.SCI/.TBI`) | **UNTESTED (semantics)** | same file triplet present; fifa99 never executes scripts; parity = unparsed |
| `assets` (Vfs/IsoImage/install) | **PARTIAL** | ISO probe OK; VFS/RefPack/BIG reads OK; manifest/`GameLayoutDescriptor`/`Fifa99Paths` FIFA 99-only |
| `input` (SDL3) / `platform` / `renderer` (Vulkan) / `presentation` plumbing / `gameplay` / `app` / `launcher` | **UNTESTED — data-independent** | no WC98 data dependency exercised; reusable by construction (renderer consumes FSH→RGBA); `presentation` sprites need an `.SQF` adapter |
| `simulation` (formations/squad + ANIM feeds) | **PARTIAL** | formation tables byte-compatible (4/5 parse); squad/meta feeds blocked by FCDB team schema (§5) |
| `tools/f99-inventory` | **COMPATIBLE** (magic gaps §2) | 6869 files inventoried; PBK/SMP/SQF/MDL/DCT/AFIF/LAY unidentified |
| `tools/f99-extract` | **COMPATIBLE** | 705 members, 1184 FSH, 10 FFN, 4 wrapped archives; 1 non-fatal error |
| `tools/f99-dbexplorer` | **INCOMPATIBLE as-is** | SIGABRT `FCDB team record size 68 != 80` (names/leagues/players print first) |
| `tools/f99-audio-extract` | **PARTIAL** | 257 WAV, 0 fail (BNK+ASF); no WC98 speech path |
| `tools/f99-model-export` / `f99-anim-inspect` | **COMPATIBLE** | WC98 glTF+preview; 413-clip list; cross-game poses |
| `tools/f99-ui-render` | **INCOMPATIBLE as-is** | 0 screens on WC98 (paths + vocabulary) |
| `tools/f99-gamedata` | **PARTIAL** | ISO probe OK; detect/validate/manifest FIFA 99-only |
| `tools/f99-exeinfo` | **COMPATIBLE** | `FIFAWC.EXE`: PE32 i386, 5 sections, 7 DLLs / 173 imports (DDRAW/DSOUND/WINMM), timestamp 1998 |
| `tests/` | **FIFA 99 fixture-gated** | byte-exact fixtures; clone the compatible-codec assertions onto WC98 fixtures |

## 12. Top integration risks (ranked)

1. **FCDB team-record schema (68 B / 17 words, no budget field, roster base 24)** — hard-blocks
   `dbexplorer`, FE data layer, teams/leagues meta. Rest of FCDB compatible → schema-branch job.
   Entity counts 24/56/1782 vs 53/294/5858: WC98 is national-team-centric, so FE game modes
   differ too (not just bytes).
2. **FE UI**: WC98 LAY vocabulary (64 WC98-only keys, 7 unknown objecttypes) + FE art pipeline
   (`.SQF` EULZ sprites, `GEOM/SKEL .MDL` menu models, `FEFLOW*.PAK` FSH needing 0x2D/0x29 +
   global palette). `ui/` = port; `LayoutDocument`/`FemsgTable` = keep.
3. **Speech subsystem**: new container/index family (`.PBK`@0x30+BIGF, `PHRS.*` V8-7-4,
   `.INF/.HDR/.TYPE`, PT v2). Container + codec decode is DONE (6,964/6,964 members,
   measured 2026-11-01); open: phrase→event index semantics and output gain calibration
   (decoded levels hot, occasional saturation). UTK + EA-XA codecs reusable.
4. **Path layout/manifest**: no `DATA/`, 0 CRC trailers, `.PAK` = RefPack(FSH), 4 RefPack-wrapped
   LAY/FONT archives, `VIDEO=.DCT` → VFS plumbing works, descriptor is FIFA 99-only.
5. **Video frames**: EA-TREM (`mTCD/DCTm`) decoder for 29 `.DCT` (container+audio reusable).
6. **FE 3D (`GEOM/SKEL`) + `SQF/EULZ`** — new decoders for FE art (12 + 49 files).
7. **ANIM 1998 header variant** (9/413 clips, `hdrSize 2817`, key block 164) — small parser
   branch; rig/pipeline compatible.
8. **Inventory magic gaps** (PBK/SMP/SQF/MDL/DCT/AFIF/LAY/.INF/.HDR/.TYPE) — cheap
   `tools/common/identify.hpp` additions.
9. **Save/config** (`options.sav` AFIF 1998 variant) + **GEN4TBLS renames** (`G98*`, AT/DF absent)
   — small.
10. **Unparsed parity set**: `.SCI/.TBI/.T` AI/play scripts — FIFA 99 parses these itself, so
    simulation parity depends on reverse-engineering them from both games (largest unknown).

## 13. Artifacts

- Build: `/mnt/2nd4TB/FIFA99/fifa99-native/build-wc98probe` (tools built; repo untouched)
- Inventory: `/tmp/wc98probe-out/wc98_inventory.json`
- Archive sweep: `/tmp/wc98probe-out/bigall.log` + extracted tree `/tmp/wc98probe-out/bigall`
- Audio: `/tmp/wc98probe-out/audio/manifest.json` + 257 WAV
- FSH/FFN PNG: `/tmp/wc98probe-out/fsh`; glTF: `/tmp/wc98probe-out/gltf`, `gltf2`
- Probes (source `/tmp/wc98probe-src/probe{,2..7}.cpp`; logs `/tmp/wc98probe-out/probe*.log`):
  1 = broad per-format sweep, 2 = `.SMP`/`.PBK`/PT platform census, 3 = GEN4TBLS/UTK/anim
  signatures/LAY keys, 4 = forced EA-XA, 5 = PT v2 header census + chant EA-XA, 6 = LAY key
  vocabulary + FSH palette-type census, 7 = MicroTalk 10:1 framing test + `PHRS` nested anatomy.

---

Probe of fifa99-native code against WC98 data. Probe logs (/tmp/wc98probe-out) are
ephemeral; counts in this document are the durable record.

## 12. Measured WC98 deltas from the port run (2026-11)

The ported modules and their test gates lock in these measured facts (they supersede
section 3-8 probe estimates where they differ):

| Area | Matrix probe claim | Measured by port |
|---|---|---|
| .BIG archives | 36, "some 48 B prefixed" | 32 plain + 4 RefPack-wrapped (FEFONTS, IGFONTS, IGLAY, LAYOUTS); 48 B prefix confirmed on ANIM.BIG (members aaaaaa / fronte / ulsjga5) |
| .PAK | 793 = 786 SHPI + 7 .LIT | 793 = **791 SHPI**, 2 non-SHPI payloads |
| FCDB teams | 68 B / 17 words; 24/56/1782 | confirmed; roster = 20 **little-endian** u16 player-id slots (bytes 24..64, 0 = empty; 984/984 slots resolve — FIFA 99's big-endian resolves 31/984); squad size = filled slots, byte-22 nibble = floor(squad/2) (odd squads 11/13 measured; 40/56 teams slot-capped at 20); nationality = (b23&0xF)<<4 \| (b22&0xF0)>>4; shirtStyle is 6-bit (measured max 32) |
| FCDB players | 20 B bit-packed | confirmed byte-packed: id=((b1&0x3F)<<8)\|b0, nation=(b2<<2)\|(b1>>6), price LE@4..5, 17 skill nibbles @8..16 (value = n*4+39), shirt=((b17&0xF)<<4)\|(b16>>4), role=b17>>4; shirt numbers 1..255 (720 players carry the 255 "no number" sentinel); skills are raw 4-bit nibbles 0..15 |
| GEN4TBLS.VIV | G98* members | header "400\r\n" (5 B); KO = 68 B i8, PK = 136 B **i16 LE**, PL = FK = 20 332 B i8; no AT/DF members |
| PT v2 .SMP | tag 7 EA-XA, tag 9 MicroTalk | **756/756 codec 9 (MicroTalk)**; tag 0x88 is header-relative (value = headerSize) |
| .PBK banks | PT play-track members | **BIGF archives** (PHRS.PBK: 1290 members; nests second-level BIG containers — 536 nested archives / 2,044 nested-member entries measured 2026-11-01); every member = 20 B phrase record + `PT` v2 play track at offset **20** — decode via `decodePlayTrack(m, 20)`, which routes codec by tag `0x83` (9 = UTK MicroTalk via `decodeSpeechPayload`: one continuous bitstream with a single FIFA-99-style header block prepended; 7 = EA-XA). RefPack-wrapped members expand transparently. Full-bank result: **6,964/6,964 decode, 0 failures** (measured 2026-11-01); supersedes the earlier "raw UTK payloads with no PT wrapper" probe claim |
| PT v0 audio (.ASF / .DCT) | FIFA 99 tag stream | 1998 nests the standard meta tags (0x85 sample count, 0x82 channels, 0x83 codec = 7 EA-XA) inside a **0x65 block**; `PtHeader::parse` merges the nested tags |
| .BNK SFX banks | 47/47 decode to WAV | 47/47 structural parse; members are "PT\\0\\0" with a 1998 tag dialect (0x06, 0x5a loop meta, 0x91, 0x88 = absolute data offset) and a 0xa2 meta block nesting the standard tags (0x83 codec 7, 0x85 sample count); payload decoding is an open research item |
| .LAY screens | 29 parse, invariants | 29/29 parse; 383 "unknown objecttype" warnings for unmodelled UI types (measured) + exactly 1 numobjects=4-vs-6 section-count quirk (FEFLOW00.LAY) |
| ANIM | 404/413 clips | confirmed: 413 members → 404 clips + 9 1998 key-block variants; sspecz6 = player-rig clip (5 triplets / 14 quats), aaaaaa = show clip (16/16, 160 B header) |
| Global palette | PAL.PAK | FE/PACK/PAL.PAK = RefPack → SHPI carrying a type-0x01 256-colour record; the resolver unwraps RefPack first |
| FSH palette chain | 0x2D/0x29 + global fallback | the palette-record chain walker needs a bounds guard at record end (span underflow fixed) |
