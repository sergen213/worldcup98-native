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
0 failures**; 1,044 members not decodable as play tracks (1,022 tiny BIGF stub headers whose entry offsets point past the member — cross-member references the engine resolves at load — plus 22 empty/odd payloads); total 508,302,490 samples.
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
occasional saturation); (2) the absent `0x84` rate tag defaults to 22050 Hz while speech is
likely 8 kHz (UNVERIFIED); (3) the 1,044 stub/other members (cross-member BIGF references not yet modelled).

`.SPEECH`-style `.STR/.OFF/SPEECH.INF` indexes do not exist in WC98: phrase lookup
uses `AUDIO/PHRS/{PHRS.PBK (BIGF, 1290 members), PHRS.HDR, PHRS.INF}` where INF is
28 B `V8-7-4` + five u32 counts; per-voice `.INF/.HDR/.TYPE` (586/240/159) map
player phrases.

## Music / streams

`.ASF` music and video audio ride `SCHl` command streams (`schl.cpp`); in-game
commentary is PT chains + PBK phrase fetch, timed by the CRDl event stream. MIDx
event music (MIDI payload) remains a stub — no MIDI device emulation yet.
