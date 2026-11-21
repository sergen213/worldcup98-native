# WINDOWS_AUDIO_TIMELINE — original FIFAWC.EXE audio scheduling reference

Evidence: `analysis/pass118timing/{pump.txt,timer.txt}` (raw pump body + timer arming
disassembly), pass-53/56/68/113tick lanes (`AUDIO_RUNTIME.md`), pass-12 speech scheduler
dossier (`analysis/pass12speech/SPEECH_PRELOAD_TABLE.md`), tick host `FUN_004906f8`.
Authority: the original `FIFAWC.EXE` bytes. The Wine capture lane is dead (black
DirectDraw surface, see `docs/reverse-engineering/README` notes) — timings below are byte
evidence, not audible measurements. [C] = proven from disassembly, [H] = inferred.

## 1. Clock chain (the relationship the port must reproduce)

| Clock | Source | Period | Role |
|---|---|---|---|
| Multimedia timer tick `DAT_00636a14` | inittimer(100) armed at VA `0x43cb8f` (`mov 0x64,EAX; call 0x496bb0`), INC per event at callback tail `0x4966b8` | 10 ms wall-clock | **cadence authority** for chant stamp, replay schedule, speech clock |
| Engine frame | tick host `FUN_004906f8` (sole caller 0x49070e) drives the pumps | 1/50 s (20 ms) | all per-frame pumps: commentary queue, chant pump, crowd envelope, scene tick |
| Sim frame | `Sim_frame` fixed-point accumulator | 2 sim frames per engine tick | physics/animation contact events |
| Audio clock `DAT_0058951c` | 20 ms audio frame counter (mirrored by the mixer stream bookkeeping) | 20 ms | mixer voice accounting (`game_audio.cpp` mirror) |

Consequence [C]: render fps is not in the audio path — every audio pump is called once per
engine frame and cadence gates compare the 10 ms timer stamp. A 60 fps or 30 fps machine
triggers identical audio timing.

## 2. Chant lane timeline (FUN_0048da50, pass-53 + pass-118 re-decode)

```
t = 0            boot: lane enabled (FUN_0048d378 site 0x49078f), pool 113 classified
                 stamp DAT_0057eb40 = 0
t = first frame  pump arrival (tick >= stamp): 3-lane top-up <= 3-slot pool cap
t = +1 frame     per-call qfind branch plays the request -> SNDplay (audible <=20 ms after queueing)
t = +1.000 s     arrival #2 (stamp += 100 timer ticks); drain handlers + rotation loop
t = +2.000 s     arrival #3 ... cadence is wall-clock: pump call rate cannot shift it
```
Clip gain = percent idx2 (options reaction row): `(vol * percent * 0x7f) / 10000`
(FUN_0048d678 → FUN_004906d0); row 0 ⇒ silent requests still counted.

## 3. Commentary lane timeline

- Live event lines: gameplay event → phrase request immediately (no fixed delay, pass-86);
  segments queue through `SpeechQueue` (FUN_0048f920 pump per engine frame), concurrent
  slots kSlots, sentence status drives the 16.16 autovol ramp (sites 0x48fcb7/0x491125).
- Generic scheduled lines (pass-12): there is NO static event→phrase table. Records carry
  `{sceneId +8, delay +0xc (0x7f/0x4a), t0 +0x10, priority +0x14 = DAT_00636a34 + 3,
  end +0x18/+0x1c}`; timing is runtime-generated against the **match clock**
  (`t * 100 / 60` ticks), gated by RNG `%5 == 0` and `%200` jitter at scheduling sites
  0x48d4f7..0x48e1xx; per-frame pump `FUN_004906f8` → `FUN_0048f920`.
- Per-event speech preload: enable FUN_0048d378 (gate DAT_0063241c) drains a qadd queue then
  `SNDbankcreate` (fourcc 'BNkl', handle DAT_00632420 = phrase id for SNDphraseadd);
  disable FUN_0048d3f4 flushes on scene exit.
- Interruption: slot model — a new phrase takes a free slot; no global cut of ongoing
  lines [C]; priority word feeds the scheduler sort only.

## 4. Crowd lane timeline

- Continuous bed: CROWD card layers loop through the envelope pump FUN_004e510c (idle
  lagged-Fibonacci walk tick word consumed per pump, multiplier = card scale word CROWD 30 /
  TRAFF 2 / second lane 40, clamp 0x1ff — pass-113tick).
- Reaction onset: `SNDcrowdpulse` (FUN_004e4cac) on the match event record — pulse pushes
  the arrival target; ramp frames drawn `base + jitter`, min 1 (0x4e527a sites); blend
  consumer FUN_005444 rewrites voice gains only when the curve index moves.
- Percent idx2 re-level fades the whole stadium lane at 50 x 20 ms ticks (FUN_004906a4 →
  FUN_00550530 0.75x). Goal/shot/foul reactions therefore begin <= 1 engine frame after the
  event and ramp over drawn frame counts — they are layered on the continuous bed, not
  replacing it.

## 5. Match-state transitions

- Pause (IGMENU): the engine tick host halts ⇒ every audio pump halts; the timer thread
  keeps ticking, so the chant stamp goes stale and the first pump after resume fires exactly
  one arrival [C behavior of the stamp compare].
- Restart chain 0x440706: world rebuilt, kickoff rerun; scene handlers stay installed —
  chant pool and envelope state survive (not replayed); event streams restart from zero,
  so trigger cursors must reset with the world.
- Scene exit (match end → FE): chant lane disabled (FUN_0048d3f4 site 0x4907ee) + speech
  preload flush; FE music re-enter ramps the current track back in (FUN_0048ec64 fade 0xc8
  ticks = 4.0 s on the 20 ms clock).

## 6. Linux port alignment (current state)

- `livematch.cpp`: pumps run inside the 20 ms engine-step loop; `audioTicks` mirrors
  `DAT_00636a14` (+2 per step, deterministic for smoke lanes).
- `pumpChants(clockTicks)`: stamp gate `tick >= stamp`, `stamp = tick + 100` on arrival.
- Backend measurement (host, OpenAL Soft, mixer 48 kHz): one-shot `alSourcePlay` -> first
  sample entering the mix 0.6-10.9 ms after play, median 2.1 ms (sink tail after the mix measured ~35 ms, see
  `AUDIO_LATENCY_ANALYSIS.md` §1); streaming loop chunk 8192 frames @ 22050 = 372 ms refill window
  (a window, not onset latency). Backend latency is not a fidelity factor; the scheduling
  layer is. See `AUDIO_TIMING_RECONCILIATION.md` for the register.
