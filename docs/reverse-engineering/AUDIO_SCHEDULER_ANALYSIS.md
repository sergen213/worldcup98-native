# AUDIO_SCHEDULER_ANALYSIS — exe scheduling architecture and the port mirror (pass-118)

Reference: `WINDOWS_AUDIO_TIMELINE.md` §1 (clock chain), `AUDIO_RUNTIME.md` §7/§9/pass-61/
pass-73/pass-86/pass-100/pass-113tick, `analysis/pass118timing/{pump.txt,timer.txt}`.

## 1. exe scheduler facts (what the scheduler must reproduce)

1. **Two clocks, one authority.** The multimedia timer (`inittimer(100)` armed `0x43cb8f`;
   counter `DAT_00636a14` INC'd per event at `0x4966b8`, 10 ms wall) is the cadence
   authority. Engine pumps run on the 20 ms engine frame via the tick host `FUN_004906f8`.
   Nothing audio is scheduled on render frames (exe uses timer + `QueryPerformanceCounter`
   for presentation pacing). [C]
2. **Stamp, not frame count.** Chant cadence: stamp gate `tick >= stamp` at `0x48DACC`
   (`mov eax,[DAT_00636A14]; cmp eax,[DAT_0057EB40]; jl 0x48DA62`), and on arrival the
   stamp is re-armed **from the current tick** — `add eax,0x64; mov [DAT_0057EB40],eax`
   at `0x48DADE/0x48DAE3`, so `stamp = tick + 100` (1.000 s), never chained off the old
   stamp (bytes committed: `analysis/pass118timing/0057pump.txt`). Every cadence
   decision compares against the shared timer counter; pump frequency therefore cannot
   change event counts. [C]
3. **Arrival-time scheduling.** Rotation (LCG draw `FUN_004c321c`, modulo fetch, parity
   re-queue bit0→qbottom / bit1→qpush) executes **inside the arrival branch**
   `0x48dbeb..0x48dc07`, not per pump call. [C]
4. **Immediate request lane.** SFX/speech requests enter queue `DAT_00632734` and drain
   in ≤ 1 pump frame (`0x48da6c..0x48dacb` qfind + `SNDplay` when count ≥ 0). [C]
5. **Drop, never defer.** Speech start handler rejects while a sentence runs
   (`FUN_004dfc0c == 3`); rejected records recycle to the free pool — the event phrase is
   silently dropped, no interrupt, no re-queue. [C]
6. **Priority = max `+0x18` scan.** `FUN_0048f664` compares priority fields across the
   active queues; the presentation lane rides the same cluster (`0x48f78c`). [C]
7. **Delayed commentary records.** Generic scheduler records `{+8 sceneId, +0xc delay
   (0x7f/0x4a), +0x10 t0, +0x14 priority = DAT_00636a14 + 3, +0x18/+0x1c end}`; timing
   runtime-generated (`t*100/60` match clock, RNG gates `%5==0`, `%200` jitter). [C for
   record fields; scene-id table input undecoded → [H] wiring]
8. **Throttle lanes.** SFX ambient: `tick % 100 >= interval byte`; crowd pulse: tick-word
   remainder selects slot; speech autovol pump `0x4dfca0` ramps `cur += delta` clamp at
   target, beat scheduler fires only `vol <= threshold` (retail 0 → silent). [C]
9. **No MIDI module lane.** Without a synth the CRWD stamp returns `-13`, PVB rejects
   (`-8`), entries recycle silently — the port's no-device lane equals retail behavior. [C]

## 2. Port scheduler (`app/match_audio.*`, `app/commentary.*`, `app/livematch.cpp`)

| exe mechanism | port mirror | fidelity |
|---|---|---|
| timer tick `DAT_00636a14` | `audioTicks` +2 per 20 ms engine step (deterministic for smoke lanes) | tick-equivalent; wall-clock drift on live matches accepted (exe timer is wall-clock, port steps are wall-paced) |
| stamp gate `0x48dacc` | `pumpChants(clockTicks)`: `tick >= stamp ⇒ stamp = tick + 100` | exact |
| rotation at arrival | rotation branch under the cadence gate | exact |
| request ≤ 1 pump frame | requests played at arrival (≤ 1 frame early) | immaterial at 1 s cadence |
| speech busy-reject drop | `triggerLive` + `rejected()` counter | exact |
| priority max scan | `Scheduler::priority` records | modeled [H], live lane immediate |
| delay records + RNG gates | `Commentary::Scheduler` | modeled, unwired [H] |
| autovol ramp | `SpeechGain` (fixed-point truncation pinned by `test_gameaudio`: `set(80)`→`ramp(1,3)` 53/27/1 clamp) | exact |
| pulse ramp 16.16 | `CrowdEnvelope` pulse + clamp | exact |
| tick-word envelope draw | `ExeTickAcc` seed-0 constants + `targetLevel()` (pass-113tick) | exact for seed-0 lane; runtime seed (RDTSC/EBX counter) [H] |
| music fade 0xc8 | `Mixer::fade(…, 200)` at 20 ms steps | exact |
| MIDI seam → PVB | no device → silent recycle lane | equals retail no-synth |

## 3. Deliberate deviations (documented, not bugs)
- Requests play at arrival instead of ≤ 1 pump frame later — inaudible at 1 s cadence.
- `audioTicks` is a mirror counter, not a wall clock: deterministic replay lanes need the
  exact tick grid; live smoke drift ≤ 1 step.
- Beat scheduler gate operand (always 0 at retail add sites) stays exe-side semantics.

## 4. Open [H]
- Generic-commentary scene-id table (boot input to the scheduler) — wiring blocked.
- Runtime seed input for the tick walk (RDTSC at `0x47b9f6` gated `[0x57f674]!=0`, EBX
  counter at `0x42b145`).
- CRWD phrase→MIDI mapper `0x4e43c0` per-phrase semantics (EDX = -1 selector).
