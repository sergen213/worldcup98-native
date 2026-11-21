# AUDIO_TIMING_AUDIT — port pipeline audit (pass-118timing, 2026-11-21)

Scope: the full sound lifecycle in the Linux port, traced against the original
`FIFAWC.EXE` scheduling architecture (`WINDOWS_AUDIO_TIMELINE.md`, `AUDIO_RUNTIME.md`).
This audit was performed **before** any timing change; the fixes it motivated are in
`AUDIO_TIMING_RECONCILIATION.md` (register rows 1-10). Companion docs:
`AUDIO_EVENT_TRIGGER_MAP.md`, `AUDIO_SCHEDULER_ANALYSIS.md`, `AUDIO_LATENCY_ANALYSIS.md`,
`COMMENTARY_TIMING_ANALYSIS.md`, `CROWD_TIMING_ANALYSIS.md`, `AUDIO_TIMING_VALIDATION.md`.

## 1. Lifecycle stages (port, post-pass-118)

| Stage | Where | Timing source |
|---|---|---|
| 1. Gameplay situation occurs | `simulation/*` rules lanes produce entries in the world event list (`simulation/include/wc98/sim/events.hpp`) | sim frame (2 sim frames per engine tick) |
| 2. Event detection | `app/livematch.cpp` event-drain cursor inside the 20 ms engine-step loop | engine tick (ex was: every 2 render frames — audit finding F-2) |
| 3. Audio event generation | `MatchAudio::triggerLive` (speech/SFX), `CrowdEnvelope` + pulse (crowd), `EventSweep` (set-piece/phase seams) | one engine tick after the sim event (exe: event → trigger within one pump frame [C], pass-61 request queue) |
| 4. Scheduling | `MatchAudio::pumpChants(clockTicks)` stamp gate (exe `FUN_0048da50` mirror), `Commentary::Scheduler` delay/priority records (exe pass-12 record family), chant rotation under the cadence gate (exe 0x48dbeb..0x48dc07) | `audioTicks` mirror of `DAT_00636a14`, +2 per 20 ms engine step |
| 5. Sound selection | chant pool id→sample layout `{bank:1, slot:24}` (pass-62 §10); speech phrase id `[entry+0x0E]`, entry index `2*DAT_0057eb54[class] + (classByte>>6)` (pass-86/87) | selection happens at arrival, not request |
| 6. Queueing | chant 3-lane pool, speech 20-slot ring stride `0xc` cap 18 (`DAT_0064b068` mirror), in-flight records | exe cadence: 1.000 s stamp |
| 7. Mixer submission | `audio::Mixer::play/stream` (`audio/src/mixer.cpp`) → OpenAL source queue | immediately at arrival (≤ 1 pump frame early vs exe — immaterial) |
| 8. Audible onset | OpenAL Soft: one-shot `alSourcePlay` → first sample entering the mix ≈ 2 ms, sink tail ~35 ms measured (`AUDIO_LATENCY_ANALYSIS.md` §1) | mix queue, not scheduler |
| 8. Audible onset | OpenAL Soft: one-shot `alSourcePlay` → first sample entering the mix 0.6-10.9 ms (median 2.1), sink tail ~35 ms measured (`AUDIO_LATENCY_ANALYSIS.md` §1) | mix queue, not scheduler |
| 9. Completion / interruption | `Mixer::playing()` retire poll, `Mixer::fade` ramp (exe `FUN_0048ec64` 200-tick seam), busy-reject drop (exe: record recycled, never deferred, pass-86) | pump iteration 10 ms, fade step per 2 iterations = 20 ms tick |

## 2. Clock chain (the relationship that had to be reproduced)

| Clock | exe source | port mirror |
|---|---|---|
| Multimedia timer tick `DAT_00636a14` | `inittimer(100)` armed `0x43cb8f`, INC `0x4966b8`, 10 ms wall | `audioTicks` +2 per 20 ms engine step |
| Engine frame | tick host `FUN_004906f8` drives all pumps | the pause-guarded 20 ms step loop in `livematch.cpp` |
| Render frame | exe: none fed audio (timer/`QueryPerformanceCounter`) | **was** feeding all pumps — root cause F-1 |
| Sim frame | `Sim_frame` accumulator | unchanged |

## 3. Audit findings

- **F-1 (root cause).** All audio pumps were fed from **render frames**, and the chant
  cadence used a **frame count** (100 render frames) where the exe uses a **multimedia-
  timer stamp** (`DAT_0057eb40 <= DAT_00636a14 ⇒ +100`). fps-dependent: chant reload
  1.67 s @60 fps, 2.5 s @40 fps vs exe 1.000 s; commentary cadence ~1.5× late
  (`pump(2)` per 2 render frames ≈ 33 ms vs exe 20 ms). Fixed: tick-based cadence,
  `pump(1)` per tick. [C]
- **F-2.** Event/SFX drain ran every 2 render frames and **during pause** — stale events
  drained while the match was stopped. Fixed: drain inside the pause-guarded tick loop.
- **F-3.** `sfxPos` not reset on match restart → cursor past the new empty event list →
  **no SFX ever after restart**. Fixed: `sfxPos = 0` with `evPos = 0`.
- **F-4.** Rotation branch had been moved to every pump call (pass-118 mid-pass) —
  frame-rate-dependent LCG stream (56 draws/frame). Restored to the arrival branch.
- **F-5.** Music fade countdown assumed 60 fps (240) and halted on pause while the mixer
  ramp continued; corrected to 200 steps at 20 ms.
- **F-6.** Audio device ruled out as cause: OpenAL mix onset measured 0.6-10.9 ms after play, median 2.1 ms (`mix_onset_probe.out`; sink tail
  measured (~35 ms tail), `AUDIO_LATENCY_ANALYSIS.md` §1); stream chunk
  8192 frames @ 22050 Hz = 372 ms is a **refill window**, not onset latency
  (`AUDIO_LATENCY_ANALYSIS.md`). The scheduling layer was the fidelity factor.
- **F-7 [H].** exe generic-commentary periodic scheduler (pass-12 record family:
  sceneId/t0 inputs from the boot scene table, still undecoded) is modeled in
  `Commentary::Scheduler` but not wired to live match state; live-lane event lines are
  immediate and match the exe behavior (pass-86 busy-reject drop).

## 4. Verification hooks
Gates listed per category in `AUDIO_TIMING_VALIDATION.md`. Clock change covered by:
`test_gameaudio` cadence pin, selftest `audio-timing` (stamp-boundary arrivals,
pump-rate invariant), `fe_igmenu` restart lane, `--live-match-smoke`.
