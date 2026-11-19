# CRD — EA crowd/ambience card (CROWD.CRD, TRAFF.CRD)

Evidence: `AUDIO/CRWD/CROWD.CRD` + `TRAFF.CRD` bytes (measured dump below), FIFAWC.EXE lanes
`FUN_0048e78c` (crowd boot), `FUN_004e4a80` (card install), `FUN_004e4d8c` (`SNDcrowdcontrol`),
percent mapper `FUN_004906a4`, pump handler `0x4e502c` (raw disassembly, pass-52).
Parser: `formats/include/wc98/formats/crd.hpp` + `formats/src/crd.cpp`.

## Container layout (CROWD.CRD, size 0x1168)

| Offset | Contents |
|---|---|
| 0x00 | fourcc `CRDl` (0x4352446C), byte4 version = 2 |
| 0x07 | byte7 = weight split source. `SNDcrowdload` computes `w = byte7 << 9 / 100` and `512 - w` (CROWD 0x46 → 358 / 154 on the 512 scale; TRAFF 0x04 → 20 / 492) |
| 0x08 / 0x14 | two 12-byte groups `{u16 a, u16 b, u32 c, u32 d}` (semantics undecoded) |
| 0x28 | 16 layer records × 16 bytes. Present records start `01 FF 00 00`; six u16 fields `{0xFF01, 0, lo, hi, position, layer}` with `lo..hi` in 1..511 (layer level window), `position` in 0..65535, `layer` 0..16 (CROWD dump: lo/hi windows 300..511, positions {0, 16384, 32768, 43690, ...}) |
| 0x128 | 16 u32 table refs, relative to 0x128, pointing into the table area mid-curve (8 nonzero in CROWD: tables 0/2/4/6 at offsets 64/60/56/52 and tables 1/3/5/7 at 32/28/24/20). `SNDcrowdload` fixups these to absolute pointers (loop `*(int*)(card+i*4+0x128) += ...`) |
| 0x168 | 8 × 512-byte non-decreasing byte curves (CROWD maxima 127/104/127/104/127/127/127/127; zero-floor pair t4/t6 = entries 0..4 → 0 then rise to 27 at index 127; 21-floor pair t0/t2; 28-floor pair t1/t3; 26-floor pair t5/t7) |

## Runtime control chain (pass-52 decode)

1. Percent row idx2 (scene reaction row, `DAT_00632778[2]`) → level:
   `FUN_004906a4`: `level = pct * 0x7f / 100`, clamped `>= 1` (80 → 101).
2. `FUN_004e4d8c` = `SNDcrowdcontrol(level)` (error string `SNDcrowdcontrol - controllerlevel
   OF %i OUT OF RANGE.` @0x569c40, file `cmn\scrowd.c`): rejects `level < 0 || > 0x1ff`, else
   stores the 16-bit level into the controller struct (`DAT_005894cc`).
3. Pump handler `0x4e502c` (function pointer stored by `FUN_004e4a80` into the card struct at
   the raw site 0x4e4b37): gates on `DAT_006597b0`/`DAT_006597dc`, reads the current value
   (`call 0x52b004`), and on state byte `0x589585 == 1` scales `value * 1640 / byte[0x589587]`
   (`call 0x4e4fb0`, `idiv`) before posting it (`call 0x52b058`). The table read offsets at
   0x128 are consumed by the curve pump; exact runtime index selection is undecoded.
4. Boot gate `FUN_0048e78c`: loads `AUDIO\CRWD\%s` twice (CROWD.BNK then CROWD.CRD — scene 10
   lane uses `CROWD.PBK` instead), calls `SNDcrowdload` + `SNDcrowdstart` (`FUN_004e4a80`,
   `FUN_004e3128`); percent callback `FUN_0048e550` re-levels through steps 1–2 while started.

## Port wiring (`app/game_audio.cpp`, pass-52)

`MatchAudio::crowdCurveRatio`: `pct → level (FUN_004906a4 formula) → crowdCurve_[level] /
crowdCurve_[127]` using the CROWD.CRD zero-floor curve table 4 (entry 0 = silent bed, matching
the options-row-0 fade gate). Multiplied into the SMIX lane gain of the crowd bed, traffic hum
and presentation voice in `startCrowd`/`startTraffic`/`relevelCrowd`.

## Open

- Layer-record field semantics (`position`, `layer` index → which BNK slot / curve).
- Pump divisor/state bytes `0x589585/0x589587` (set by the card install; the 1640 constant
  implies a fixed-point gain scale, divisor value not observed).
- The 21-floor t0/t2 pair (anticipation/reaction layers) is not wired in the port — no chant
  layer exists yet (see AUDIO_DISCREPANCIES A6).
- `CROWD.PBK` scene-10 container lane (`FUN_0048e6d0` loads `CRWD` phrases + slot table
  `DAT_006325dc = FUN_0049f5a0`) — container exists on the CD, playback mapping undecoded.
