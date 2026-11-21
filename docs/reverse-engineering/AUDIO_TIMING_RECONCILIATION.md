# AUDIO_TIMING_RECONCILIATION — original vs port, discrepancy register

Reference: `WINDOWS_AUDIO_TIMELINE.md` (clock chain) and `AUDIO_RUNTIME.md` (lane decodes).
Verification gates: `test_gameaudio` cadence pin (h-lane), `match-audio-lanes` selftest
gate, `audio-timing` selftest step (arrivals land on stamp boundaries, identical count at
1x and 5x pump/tick), `fe_igmenu` restart lane (scripted RestartAsk -> `igmenu: match
restarted` + post-restart kickoff whistle >= 2), `--live-match-smoke` lane.
Pass id: pass-118timing.

| # | Behavior | Original (FIFAWC.EXE) | Port before | Fix | Verification |
|---|---|---|---|---|---|
| 1 | Chant load cadence | Timer stamp: `DAT_0057eb40 <= DAT_00636a14 ⇒ +100`, 10 ms ticks => 1.000 s wall-clock, pump-body gate VA 0x48dacc | 100 **render frames** per pump call — fps-dependent (1.67 s at 60 fps, 2.5 s at 40 fps) | `pumpChants(clockTicks)` stamp gate; `audioTicks` mirror +2 per 20 ms engine step | `test_gameaudio`: arrivals fire exactly at 0/100/200/300 ticks, 12 requests = 4 x 3 lanes; gate blocks at tick 99 |
| 2 | Rotation branch location | Rotation loop lives in the **arrival branch** (0x48dbeb..0x48dc07): steps = qlength/2, LCG modulo fetch, parity re-queue (bit 0 -> qbottom, bit 1 -> qpush) | Mid-pass: rotation moved to every pump call (frame-rate-dependent RNG stream, 56 draws/frame) | Restored under the cadence gate; per-call branch = qfind service step only | `test_gameaudio` LCG sequence pin |
| 3 | Per-call service branch | One qfind on request queue + SNDplay when count >= 0 (VA 0x48da6c..0x48dacb): requests audible <= 1 pump frame | Voice-retire probe only | Comment-documented as port model; requests played at arrival (<= 1 frame early vs exe — immaterial at 1 s cadence) | — |
| 4 | Commentary pump rate | FUN_0048f920 called once per engine frame (20 ms) | `pump(2)` every 2 **render** frames (~33 ms at 60 fps), event drain also render-frame gated | Per-tick `pump(1)`; constants re-read as 20 ms ticks | live smoke: kickoff phrase onset inside first ticks |
| 5 | SFX/event dispatch seam | Event -> trigger within one engine frame; drain cursor advances per frame | every 2 render frames; cursor lag while paused | drain inside the tick loop (pause halts the lane) | live smoke `sim advanced ... events whistle/crowd` counts |
| 6 | Pause semantics | Tick host halts pumps; timer thread keeps ticking; stamp stale -> one arrival on resume | pumps kept running during IGMENU (stale events drained while paused) | pumps inside the pause-guarded tick loop | livematch pause smoke lane (`igPauseSmoke`) |
| 7 | Restart chain 0x440706 | World rebuilt, event stream restarts, scene handlers stay installed (chant pool survives) | `sfxPos` not reset -> **no SFX ever after match restart** (cursor past new empty event list) | `sfxPos = 0` with `evPos = 0` | live smoke restart line (`igmenu: match restarted`) |
| 8 | Music fade ramp | FUN_0048ec64 argument 0xc8 ticks on the 20 ms clock = 4.0 s ramp | countdown 240 assumed 60 fps; pause halted the countdown while the mixer ramp continued | `fadeFrames_ = 200` at 20 ms steps | — |
| 9 | Mixer backend latency | DirectSound device buffers (~100 ms class) | OpenAL Soft: one-shot play -> first sample enters the OpenAL mix 0.6-10.9 ms after play (median 2.1 ms, `mix_onset_probe.out`); head audible ~35 ms later (`analysis/pass118timing/mix_onset_probe.cpp` + `latency_probe.cpp`, mixer 48 kHz); stream chunk 372 ms is a refill window | none needed — measured and documented | probe result recorded in `WINDOWS_AUDIO_TIMELINE.md` §6 |
| 10 | Crowd pulse onset | SNDcrowdpulse pushes arrival target; ramp frames drawn at event pump; blend rewrite on index move only | pulse + ramp ticks already per-pump-call | tick host moved to engine steps (pulse latency <= 1 step) | `match-audio-lanes` gate |
| 11 | Stamp behaviour across pause | exe timer thread keeps ticking during pause ⇒ stamp stale at resume ⇒ **one arrival fires immediately** on resume (gate reads the live tick, `analysis/pass118timing/0057pump.txt`) | port mirror ticks freeze during pause; the stamp freezes with them ⇒ post-resume arrival is delayed up to one chant period (1.0 s) instead of firing at once | **unresolved** — candidate fix: advance the mirror from wall-clock during pause (1 timer tick per 10 ms); not implemented, deterministic smoke lanes pin tick counts | row 6 pause smoke measures the port path only; no exe-side measurement exists |

Root-cause summary: the port fed all pumps from **render frames** and used a
**frame-count** cadence where the exe used a **multimedia-timer stamp**. The audible
symptoms (chant load rate drifting with fps, commentary ~1.5x late cadence, silent SFX
after restart, stale events draining during pause) all traced to that clock mismatch, not
to the audio device (mix onset 0.6-10.9 ms, median 2.1 ms; head audible ~35 ms later - pass-118 probes).

Blocked / open [H]:
- Wine audible-capture lane is dead (black DirectDraw) — no millisecond A/V comparison
  possible; the register is built from byte evidence.
- exe generic-commentary scheduler wiring (sceneId/t0 inputs, pass-12 record family) is
  modeled in `Commentary::Scheduler` but not wired to live match state; live-lane event
  lines are immediate and match the exe. Wiring the periodic lane requires the scene id
  table consumed by the exe boot — still undecoded.
