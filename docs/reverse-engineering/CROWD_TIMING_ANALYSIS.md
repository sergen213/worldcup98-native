# CROWD_TIMING_ANALYSIS — stadium bed, pulses, reactions, chant cadence (pass-118)

Authority: `FIFAWC.EXE` bytes; lanes `AUDIO_RUNTIME.md` §6/61b/97a/pass-55/pass-100/
pass-113tick + `analysis/pass113tick/{clk3.txt,NOTES.md}`.

## 1. Continuous ambience (bed)
- One streaming bed per scene: `AUDIO\CRWD\CROWD.BNK/CRD` (every scene except 10) or
  `TRAFF.BNK/TRAFF.CRD` (scene 10/stadium) — same loader lane, scene dispatch
  `0x48e7a0..0x48e847`, `"AUDIO\\CRWD\\%s"` (pass-61b). [C]
- Curve machine `cmn\scrowd.c`: CROWD.CRD header byte 7 = 70 → blend weights be 358 /
  bc 154; fresh machine (level 0) indexes the curve at `(358*358)>>9 = 250`; the per-frame
  idle pump (`FUN_004e510c` walk + `FUN_004e5444` blend) breathes the index inside the
  blend band `[250, 404]` — never a fixed level. [C]
- **No reaction delay on the bed**: idle envelope moves every pump frame; onset of any
  change is ≤ 1 pump frame (20 ms). [C]

## 2. Target draw (idle envelope) — pass-113tick, exact mirror
- tick word = `cb8` slot of the `FUN_004c321c` lagged-Fibonacci walk (`cc8..cb8` over
  `DAT_00585CB8..0x585CC8`, increment slot `DAT_00585CCC`, adc carry chain; consumed as
  `cb8 & 0x7fff`). [C]
- multiplier = card header u16 at byte 8 (CROWD 30, TRAFF 2, second-lane `+0x14` = 40),
  guarded `cmp 0x10000`; draw = `clamp(scale + ((word15 - 0x4000)*scale >> 14), 0, 0x1ff)`
  at the call-site epilogue `add eax,edx` + clamp `0x1ff` (`DAT_005894c4`). [C]
- Seed input: RDTSC (`0x47b9f6`, gated `[0x57f674] != 0`) or EBX counter (`0x42b145`)
  [H]; port mirrors the **seed-0** constants (`app::ExeTickAcc`). [H]
- Gate: `test_gameaudio` block (h) pins words 17302/22024 and targets 31/40, card
  `params[0].a` = 30/2.

## 3. Pulses and reaction onset
- pulseA `0x48e488` / pulseB `0x48e4fc`: level gate `arg*pct[2]*127/10000 > 0`; slot
  chosen by **tick-word remainder** (`DIV` with BE-swap divisor); `SNDcrowdpulse(idx,
  slotrecord)` `0x4e4cac`. Onset ≤ 1 pump frame; slot choice randomized per tick. [C]
- Ramp: 16.16 envelope level toward the pulse target, step `0x38000` (= 3.5), clamped at
  the record ceiling (CROWD.CRD record 0: delta 350, ceiling 450). [C]
- Reaction sound seam `0x48e3b0`: pool entry → `SNDseqplayPVB 0x4e462c` plays the stamped
  **MIDI sequence id** (`SNDphrasemidi 0x4e43c0` stores id at record+6; PAN/VOL/BEND all
  -1 → center/127/64). Sequence id = stamped phrase id, not an envelope ceiling. [C]
- Without a MIDI module: stamp `-13` → PVB `-8` → entry recycled, silent — equals retail
  on machines without a synth. Port has no MIDI device: same lane. [C]

## 4. Chant cadence (crowd chant lane)
Stamp gate `DAT_0057eb40 <= DAT_00636a14 ⇒ +100` on the 10 ms timer clock = **1.000 s**
reload; rotation LCG at arrival, parity re-queue (bit0 → qbottom, bit1 → qpush). Pump
frequency cannot alter the count — cadence authority is the timer stamp, not frames. [C]
Port: `pumpChants(clockTicks)` exact mirror; per-arrival timing fields in the
`audio-timing` selftest step.

## 5. Half/full-time and transitions
- Crowd lane disabled on match stoppages via the stop-all sweep + enable gates
  (`DAT_006325b0/5b4/5b8`); handlers stay registry-installed (world rebuild keeps the
  chant pool). On resume the stamp is stale → one arrival fires immediately (≤ 1 step). [C]
- Scene-10 traffic lane (`TRAFF`): ships no pulse records (enable-byte gate, all-zero
  record area) — pulse refused; second lane `+0x14` = 40 draw only. [C]
- Goal celebration: crowd pulse + commentary + presentation lanes fire **independently**
  — no synchronized single "goal sound" exists; the exe layers them exactly as
  `AUDIO_EVENT_TRIGGER_MAP.md` row "Goal chain". [C]

## 6. Fidelity status
| Aspect | exe | port | Status |
|---|---|---|---|
| bed curve breathing | per pump frame | per engine tick | exact |
| pulse onset | ≤ 1 pump frame | ≤ 1 tick (20 ms) | exact |
| pulse randomization | tick-word remainder | same walk mirrored | exact (seed-0) |
| ramp step/ceiling | 0x38000 / 450 | same constants | `match-crowd-envelope` gate |
| reaction seam | MIDI PVB | silent lane (no device) | equals retail no-synth |
| chant cadence | 1.000 s stamp | stamp mirror | `audio-timing` step, per-record fields |
| runtime seed | RDTSC/counter | seed-0 constants | [H] accepted |
