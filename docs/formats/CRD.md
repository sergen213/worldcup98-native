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
| 0x20 | 8 signed bytes = per-slot bank ids (`-1` disabled): CROWD `{0,1,2,3,-1,-1,-1,-1}`, TRAFF `{0,-1,...}`. `SNDcrowdstart` (`FUN_004e4ba0`) reads `card + 0x20 + i`; enabled slots reserve a voice through the `SNDplaysetdef` lane (`FUN_004e2d74`, validates against the `BKNL` bank registry `DAT_00659de8` stride 8), the voice id lands in the slot table `DAT_005894d0 + i*4`, disabled slots land `-1` |
| 0x28 | 16 layer records × 16 bytes. Present records start `01 FF 00 00`. Exe consumption (pass-71, `SNDcrowdpulse` `FUN_004e4cac` + idle machine `FUN_004e510c`): byte 0 = present flag (pulse gate `!= 0`); byte 1 = optional bank-slot id to reserve via `SNDplaysetdef` lane (`FUN_004e2d74`) — all CROWD records ship `0xFF` = none; `u16 @4` = pulse envelope delta and `u16 @6` = pulse ceiling (1..511 window — pass-55's measured 350/450 pair, read through `u32 >> 16`); `u32 @8` = idle envelope step in 16.16 (dump values {0, 16384, 32768, 43690} = 0.0/0.25/0.5/0.666 per tick — the field the layout earlier called `position`); `u32 @12` = idle ramp rate divisor (`rate * 0x21 >> 16` frames, measured 132/198/264 — the field earlier called `layer`'s neighbour), indexed by the layer index byte stored at `DAT_005894a7+3` |
| 0x128 | 16 u32 table refs, relative to each ref's own slot address `0x128+i*4`. `SNDcrowdload` fixups them to absolute pointers (loop `*(int*)(card + slotAddr + 0x128) = card + ref + slotAddr`, `FUN_004e4a80`). Measured (pass-54): slots 0..3 refs `0x40/0x43c/0x838/0xc34` resolve to `0x168/0x568/0x968/0xd68` = **tables 0/2/4/6**; slots 8..11 refs `0x220/0x61c/0xa18/0xe14` resolve to `0x368/0x768/0xb68/0xf68` = tables 1/3/5/7; slots 4..7 and 12..15 ref = 0 (unbound). The ref value compensates the slot address, so every bound slot points at a table base, not a mid-curve offset. |
| 0x168 | 8 × 512-byte non-decreasing byte curves (CROWD maxima 127/104/127/104/127/127/127/127; zero-floor pair t4/t6 = entries 0..4 → 0 then rise to 26 at index 127, max 127 at the tail; 21-floor pair t0/t2 (21 at index 0, 32 at 101, 35 at 127, max 127 at the tail); 28-floor pair t1/t3; 26-floor pair t5/t7) |

## Runtime control chain (pass-52 decode)

1. Percent row idx2 (scene reaction row, `DAT_00632778[2]`) → level:
   `FUN_004906a4`: `level = pct * 0x7f / 100`, clamped `>= 1` (80 → 101).
2. `FUN_004e4d8c` = `SNDcrowdcontrol(level)` (error string `SNDcrowdcontrol - controllerlevel
   OF %i OUT OF RANGE.` @0x569c40, file `cmn\scrowd.c`): rejects `level < 0 || > 0x1ff`, else
   stores the 16-bit level into the controller struct (`DAT_005894cc`).
3. Pump handler (function pointer `0x4e5018` — pass-70 body decode, the earlier
   `0x4e502c`/`1640/divisor` wording was a misattribution of the `cmn\sstream.c`
   stream state bytes `0x589585/0x589587`): gates on lane state `{2,4}`, runs the
   envelope + ramp, then always calls the idle machine `FUN_004e510c` + blend
   consumer `FUN_004e5444`. The blend consumer writes each bound slot's gain
   bytes through the fixed-up pointers (pass-71): murmur byte
   `= curveB[cc]` with `cc` the blended control word (`DAT_005894cc >> 16`),
   swell byte `= curveA[cc] * envelope_pos >> 17` (envelope 16.16 position
   `DAT_005894ac`), both via `0x128+i*4`/`0x148+i*4` table refs, value range
   0..127, written to the voice lane only on change. The port indexes the level
   from step 1 directly.
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

- ~~Layer-record field semantics~~ resolved pass-71 (see 0x28 row): pulse delta /
  ceiling / optional slot byte, idle step + rate fields.
- ~~Pump divisor/state bytes `0x589585/0x589587`~~ resolved (pass-70): those bytes
  belong to the `cmn\sstream.c` stream state, not the crowd card — the pump body
  `0x4e5018..0x4e510b` has no 1640/divisor scaling.
- ~~`CROWD.PBK` scene-10 container lane~~ wired pass-69: registry counts via
  `MatchAudio::catalogCrwd`; MIDI playback stays A5-blocked.
- Reaction-time modulation remains approximate on the port: the blend word `cc`
  = `(be * ramp + bc * c0hi) >> 9` (pass-71 exact formula; the pass-55 doc's
  `be*be` form is the post-snap identity, ramp converged = control = be) and the
  swell gain scales by the idle envelope position; the port folds blend +
  envelope into re-fade seams instead of per-tick byte updates.
