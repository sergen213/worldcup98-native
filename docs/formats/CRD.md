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
| 0x128 | 16 u32 table refs, relative to each ref's own slot address `0x128+i*4`. `SNDcrowdload` fixups them to absolute pointers (loop `*(int*)(card + slotAddr + 0x128) = card + ref + slotAddr`, `FUN_004e4a80`). Measured (pass-54): slots 0..3 refs `0x40/0x43c/0x838/0xc34` resolve to `0x168/0x568/0x968/0xd68` = **tables 0/2/4/6**; slots 8..11 refs `0x220/0x61c/0xa18/0xe14` resolve to `0x368/0x768/0xb68/0xf68` = tables 1/3/5/7; slots 4..7 and 12..15 ref = 0 (unbound). The ref value compensates the slot address, so every bound slot points at a table base, not a mid-curve offset. |
| 0x168 | 8 × 512-byte non-decreasing byte curves (CROWD maxima 127/104/127/104/127/127/127/127; zero-floor pair t4/t6 = entries 0..4 → 0 then rise to 26 at index 127, max 127 at the tail; 21-floor pair t0/t2 (21 at index 0, 32 at 101, 35 at 127, max 127 at the tail); 28-floor pair t1/t3; 26-floor pair t5/t7) |

## Runtime control chain (pass-52 decode)

1. Percent row idx2 (scene reaction row, `DAT_00632778[2]`) → level:
   `FUN_004906a4`: `level = pct * 0x7f / 100`, clamped `>= 1` (80 → 101).
2. `FUN_004e4d8c` = `SNDcrowdcontrol(level)` (error string `SNDcrowdcontrol - controllerlevel
   OF %i OUT OF RANGE.` @0x569c40, file `cmn\scrowd.c`): rejects `level < 0 || > 0x1ff`, else
   stores the 16-bit level into the controller struct (`DAT_005894cc`).
3. Pump handler `0x4e502c` (function pointer stored by `FUN_004e4a80` into the card struct at
   the raw site 0x4e4b37): gates on `DAT_006597b0`/`DAT_006597dc`, reads the current value
   (`call 0x52b004`), and on state byte `0x589585 == 1` scales `value * 1640 / byte[0x589587]`
   (`call 0x4e4fb0`, `idiv`) before posting it (`call 0x52b058`). The handler reads each bound
   slot's gain byte through the fixed-up pointer at `0x128+i*4`: `cVar1 = *(char)(level +
   refSlot)` — the card table curve indexed by the running level, value range 0..127. Which
   level source feeds the index above the percent path (blend `DAT_005894cc`, `>> 16`) is not
   fully decoded; the port indexes the level from step 1 directly.
4. Boot gate `FUN_0048e78c`: loads `AUDIO\CRWD\%s` twice (CROWD.BNK then CROWD.CRD — scene 10
   lane uses `CROWD.PBK` instead), calls `SNDcrowdload` + `SNDcrowdstart` (`FUN_004e4a80`,
   `FUN_004e3128`); percent callback `FUN_0048e550` re-levels through steps 1–2 while started.
   The card's `bankSlots` (0..3 enabled) is the `.BNK` slot list opened as simultaneous bed
   voices: slots 0/1 decode to 0.75 s short swells, slots 2/3 to the 3.03 s murmur loop
   (pass-51 `bankprobe`).

## Port wiring (`app/game_audio.cpp`, pass-52/54)

`MatchAudio` builds one `CrowdLayer` per enabled card slot, each bound to its table through the
measured pointer arithmetic (`(0x128 + i*4 + ref - 0x168) / 512` → table index). Bed voices play
together on the mixer: `startCrowd` plays every layer looped at
`0.6 * smixLaneGain(crowd) * curve[level] / 127`, and `relevelCrowd` re-fades each running voice
when percent idx2 changes (FUN_004906a4 level formula). Slots 2/3 (murmur bed, zero-floor curve
t4/t6) fall silent with the lane gain at options row 0; slots 0/1 (swells, 21-floor curve t0/t2)
keep their floor gain on the lane. The traffic hum scales through its own TRAFF.CRD curve
(slot 0 ref `0x40` → table 0, flat 117). The fallback curve for unbound layers/voices is table 4
normalized at index 127 (`crowdCurveRatio`).

## Open

- Layer-record field semantics (`position`, `layer` index → which BNK slot / curve).
- Pump divisor/state bytes `0x589585/0x589587` (set by the card install; the 1640 constant
  implies a fixed-point gain scale, divisor value not observed).
- Reaction-time modulation of the bound curves (event envelopes above the percent level) —
  the pump blend `DAT_005894cc` and its event inputs are undecoded; the port applies the
  percent-path level only.
- `CROWD.PBK` scene-10 container lane (`FUN_0048e6d0` loads `CRWD` phrases + slot table
  `DAT_006325dc = FUN_0049f5a0`) — container exists on the CD, playback mapping undecoded.
