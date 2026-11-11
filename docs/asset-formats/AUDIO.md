# Audio formats (PT / BNKl / EA-XA / speech)

All WC98 audio flows through the `PT` tag dialect; containers differ, codecs do not.
Reference implementations: `formats/src/schl.cpp` (`PtHeader::parse`), `bnk.cpp`,
`eaxa.cpp`, `utk.cpp`, `refpack.cpp`. Speaker-side original: `EACSND.DLL`/`EACSNDD.DLL`.

## PT tag stream

Magic `50 54 00 00` (`PT\0\0`) + u16 platform (always 0), then tags
`(u8 tag, u8 len, big-endian value)`, terminated by `0xFF`; `0xFC/0xFD/0xFE` are
single-byte skips. WC98 uses the v0 dialect (same as FIFA 97 `.BNK` banks);
FIFA 99 `.SMP` files use the v2 dialect (`dataOffset` tag `0x97`, size tags
`0x94/0x95`, loops `0x92/0x93`).

| tag | len | field |
|-----|-----|-------|
| 0x82 | 2 | duration (BE ms) |
| 0x83 | 1 | codec: 7 = EA-XA ADPCM, 9 = MicroTalk/UTK speech (census-confirmed, measured 2026-11-01), 0x0C = PCM16 |
| 0x84 | 2 | sample rate (BE Hz; e.g. `0x2B11` = 11025) |
| 0x85 | 2 | num samples per channel (BE) |
| 0x88 | 4 | data offset — in `.BNK` banks an absolute file offset to raw payload |
| 0x8A | 4 | padding/reserved (observed 0) |
| 0x91 | 1 | loop flag |

Chains are shared: many sounds point into one tag stream; the engine reads forward
from a table pointer until `0xFF`.

## BNKl banks (`.BNK`, 47 files: ANTH×40, CRWD×3, SFXS×4)

```
u32be "BNKl" | u16le version (=2; table at 0x0C; v4 table at 0x14)
u16le slot count n | u32le payload-base (first data offset)
n × u32le slot pointers, relative to each slot's own table position
```

Each non-null slot resolves (`entry_pos + rel`) onto a meta block and/or a `PT`
chain; chains carry the tags above; payloads are concatenated raw codec streams
after the definition region — **no per-payload header**. Payload size = difference
between adjacent sorted `0x88` offsets (last runs to EOF).

Case study `AUDIO/SFXS/GAME01.BNK`: 86 slots, payload base `0x950`, meta pool at
`0x778/0x79C/0x7C4`, 23 unique payloads (`0x950…0x394FC`, sizes 8 B–95 KiB).
Codec tag 7 (EA-XA) with stereo interleave: payload bytes ≈ samples×2ch/4, verified
across banks. All 47 banks decode to PCM in `test_audio` (matrix §8: 47/47 EA-XA);
no WC98 bank uses MicroTalk — that path exists for `.SMP`-shaped banks only.

## EA-XA payload

Standard XA ADPCM: sync byte `(rate/loop bits, filter, shift)` + 28 nibbled samples
per 16-byte half-frame; delta table `[1,2,3,5,7,9,11,13,15,18,22,27,34,41,53,67]`
with the standard step-index update. `eaxa::decodeFrames(in, channels, frames, hist)`
is shared by BNK/SMP/PBK paths; stereo is 2 KB-interleaved channel blocks.

## Speech archives (`.PBK` in `BIGF`, per-voice + `PHRS/CHNT/PBP`)

Measured 2026-11-01, full audit over all 1,232 banks (`AUDIO/{PHRS,CHNT,PLRS,TEAM,PBP}`):
EVERY member = a 20-byte phrase record followed by a `PT` v2 play track at offset 20.
RefPack-compressed members expand transparently (`refpack::decompressIfNeeded`).
`PHRS.PBK` additionally nests second-level BIG containers (536 nested archives,
2,044 nested-member entries counted by audit tooling).

Audit with `decodePlayTrack(m, 20)` + nested recursion: **6,964 PT members, 6,964 decoded,
0 failures**; 1,044 members not decodable as play tracks: `PHRS/TYPE.PBK` is a header-only mirror of PHRS.PBK (1,022 stub members that are byte-identical prefixes of the same-indexed full phrase + 268 empty count=0 nested archives) plus 22 non-audio payloads (16 raw in CROWD.PBK, 6 `MIDx` event files in COMOS.PBK); total 508,302,490 samples. Phrase-record layout and the mirror proof: docs/reverse-engineering/SPEECH_INDEXING_NOTES.md.
Codec census by PT tag `0x83`: codec **9 (MicroTalk) = 6,851** members, codec **7 (EA-XA) = 113**.
Sample-rate tag `0x84` is ABSENT on 6,957/6,964 members (only 5× 16000 and 2× 14000 present).

Payload model: ONE continuous UTK bitstream per member with a single FIFA-99-style header
block prepended — exactly what `utk::Decoder::decodeSpeechPayload` (`formats/src/utk.cpp`)
already implements; `decodePlayTrack` routes by codec tag. The earlier claim that `.PBK`
speech is self-contained 20 B blocks (15-bit header + 72 codes) with only 249/4,901 members
decodable was WRONG: measured payload sizes mod 20 span {0,4,8,12,16} uniformly — not
block-aligned. The old gate was an artifact of the test loop feeding raw members into UTK
framing while ignoring the PT header at offset 20.

Authenticity (measured 2026-11-01): decoded `TEAM/A_T0026.PBK` members → WAV; numpy
autocorrelation over voiced 2 s windows gives F0 ≈ 174–176 Hz with periodicity 0.71 and
0.38 (speech-like; noise would be ~0). Spectral flatness of `decodePlayTrack` output:
codec-9 members SFB ≈ 0.32 vs EA-XA chant member `CHNT/C0001800.PBK#0` SFB ≈ 0.64
(→ 37,497 samples).

Open items: (1) gain calibration — decoded amplitudes run hot (RMS ≈ 9,000–11,500 int16,
occasional saturation); (2) RESOLVED 2026-11-06 — the absent `0x84` rate tag defaults to 22,050 Hz:
CONFIRMED at `EACSND.DLL` file offset `0xe01`, where `mov dword ptr [0x40aa10], 0x5622` initializes
the driver's global sample-rate variable (same function branches on 11025/44100 for the rescale
divisor; the sole `8000` immediate in `EACSNDD.DLL@0xd4ea` is a string-blob false positive). Speech
plays at 22,050 Hz — NOT 8 kHz. (3) the 1,044 stub/other members — TYPE.PBK mirror headers and non-PT payloads, semantics documented in SPEECH_INDEXING_NOTES.md.

`.SPEECH`-style `.STR/.OFF/SPEECH.INF` indexes do not exist in WC98: phrase lookup
uses `AUDIO/PHRS/{PHRS.PBK (BIGF, 1290 members), PHRS.HDR, PHRS.INF}` where INF is
28 B `V8-7-4` + u32 {0, 1290 = phrase count, 83,696 unknown}; `.HDR` is a byte-exact snapshot of PHRS.PBK's first 11,628 bytes (TOC mirror). AUDIO holds **no other** sidecars — the earlier “586 INF / 240 HDR / 159 TYPE” counts were wrong; `PHRS/TYPE.PBK` (an archive, not a sidecar) is the header-only phrase mirror.

## Music / streams

`.ASF` music and video audio ride `SCHl` command streams (`schl.cpp`); in-game
commentary is PT chains + PBK phrase fetch, timed by the CRDl event stream. MIDx
event music (MIDI payload) remains a stub — no MIDI device emulation yet.

Speech phrase record-table API + tolerant mirror rules: [SPEECH_PHRASES.md](SPEECH_PHRASES.md) (`formats/src/phrs.cpp`).

## Scene sound-level layer (pass-17s6)

The exe mixes scene volumes through a global percent table **`DAT_00632778[6]`** (.bss; runtime
values `{80,80,80,80,65,80}` measured live under Wine in pass-7, idx4 = speech 65): init producer
`FUN_00490728` writes all six percents = 100 (one-time flag `DAT_00632774`); setter **`FUN_0049067c`**
stores a percent and dispatches the channel notifier from table `DAT_004904b0[1..5]` =
{`FUN_0048d7a0` commentary (gate `DAT_006324b4`), `FUN_0048e430` music (gates `DAT_006325b0/5b8`,
stop flag 5b4), `FUN_0048fca0` stream (gate `DAT_006326b0`), `FUN_0048ec38` EA-XA speech queue
(gate `DAT_00632644`, apply `FUN_004e6aa0`), `FUN_0049091c` CD (gates/positions `DAT_00632790/7a8/7ac`)};
slot [0] is NULL. Gain rows: **`FUN_004906a4`** computes Q7 = `(pct*0x7f)/100` clamped up to 1, and
**`FUN_004906d0`** the signed scaled multiply `(val*pct*0x7f)/100`. Speech sentences pass through
the gate `ready && DAT_00632644 && q7 > 0` (`FUN_0048eb4c/eb98`, called by the .SCI scene loaders).
Port: `audio/src/scene_levels.cpp` (grammar) + wiring in `app/game_audio.cpp` (music + crowd-stream
Q7 gains); gate `test_scene_levels`. Dossier `analysis/pass17s6audio/{NOTES.md,DECOMPILE/}`.

**Refuted seeds:** the pass-16 "streamer/envelope" family is the **THRASH 2-D renderer**, not audio —
`DAT_00632a68` = `_THRASH_drawline@8` import slot (string @0x567854, installed by `FUN_004916b0` via
LoadLibraryA/GetProcAddress), `DAT_005e6020` = clip-rect bound float (31 hits, all x87 compares in
rasterizers), 0x42a140 "streamerloop" = drawline endpoint interpolation. Cited VAs `musicsetup 0x42b780`
and `volenvelope 0x439c70` have no function, zero refs, and zero exec byte-scan hits;
`FUN_00409cac` is a scene-boot init dispatcher (disp32 count 0), not an audio device installer.
