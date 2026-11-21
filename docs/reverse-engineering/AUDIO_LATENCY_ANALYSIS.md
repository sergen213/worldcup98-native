# AUDIO_LATENCY_ANALYSIS — backend buffering and the event→audible chain (pass-118)

Question this pass answered: **is the Linux audio device responsible for the timing
drift?** Answer: no — OpenAL mix onset 0.6-10.9 ms after play (median 2.1 ms, committed artifacts; sink latency after
the mix never measured, §1); the drift was entirely in the scheduling layer
(`AUDIO_TIMING_AUDIT.md` F-1..F-5).

## 1. Linux backend characteristics (OpenAL Soft, `audio/src/mixer.cpp`)

| Item | Value | Evidence |
|---|---|---|
| Device | default output device (or loopback when tests request one) | `Mixer(loopbackRate)` |
| Sample rate | 48 kHz host default / 44.1 kHz loopback | probe, selftest lanes |
| Mixer pump thread | 10 ms sleep per iteration | `mixer.cpp:72` |
| Fade step pacing | one gain step per 2 pump iterations = 20 ms engine tick | `mixer.cpp:185` |
| Stream chunk | 8192 frames per queued buffer ≈ 372 ms @ 22050 Hz | `mixer.cpp:15` |
| One-shot onset | `alSourcePlay` → first sample entering the OpenAL mix ≈ **2 ms**; sink tail after the mix measured **~35 ms** head-to-monitor (`AL_SOFT_source_latency`, pass-118 probe) | committed artifact `analysis/pass118timing/latency_probe.cpp` + raw output `latency_probe.out` |
| Underruns | none observed in smoke lanes (stream refill window is 372 ms, far above the mix queue) | smoke lanes |

The 372 ms chunk is a **refill window, not onset latency**: once the buffer pair is
queued, the sample enters the OpenAL mix 0.6-10.9 ms after play (median 2.1 ms) (pass-118
`analysis/pass118timing/mix_onset_probe.cpp`: first `AL_SAMPLE_OFFSET>0` observed at
20-iteration spread 0.62-10.90 ms, median 2.12 ms (`mix_onset_probe.out`) - the onset lands inside the OpenAL mixer update period (~10 ms). Mix-to-audible was measured in pass 118 with
`analysis/pass118timing/latency_probe.cpp` (raw output `latency_probe.out`,
`AL_SOFT_source_latency` on the default "OpenAL Soft" device, 48 kHz): with a fresh
100 ms silent buffer the head reaches the monitor ~35 ms after `alSourcePlay`
(`min=34.6 ms`; `max=124.4 ms` includes the queue depth of the full buffer — tail
latency ≈ 25-35 ms class). From mix to audible speaker output the sink adds this
~35 ms buffer; the reference lane (DirectSound on Windows) carries a sink of the same
class (~100 ms era-typical), so the A/V comparison cancels it — the fidelity-relevant
stage is the scheduling grid. [H: same-class comparison]

## 2. Instrumentation points (event → audible)

```
sim event (stamp)      → drain seam (≤ 1 engine step, 20 ms)
audio schedule stamp   → arrival tick (cadence lane only; chant ≤ 1 step)
Mixer::play()          → alSourcei(AL_BUFFER) + alSourcePlay (same call; stream lane:
                         alSourceQueueBuffers then play)
mix queue buffers      → first sample enters the OpenAL mix 0.6-10.9 ms after play (median 2.1, mixer period)
                         head then reaches the monitor ~35 ms later — measured with
                         latency_probe.cpp, §1
```
No intermediate queue exists in the port: submission is synchronous with arrival.
exe comparison: request queue drain ≤ 1 pump frame (20 ms) — same structure.

## 3. Latency budget vs tolerance
- Cadence tolerance: ±0 on the stamp grid (`stamp = tick + 100`, `0057pump.txt`;
  `audio-timing` records, `AUDIO_TIMING_VALIDATION.md` §1) — the grid itself is 100 timer ticks = 1.000 s.
- Worst-case event→mix onset: 20 ms drain + <= ~11 ms mix (max observed, mixer period), plus the
  ~35 ms measured sink tail (§1); the reference lane's DirectSound sink is unmeasured
  [H], so audible comparison cancels only to sink-class order of magnitude (§1).
- Deliberate deviation: requests play at arrival (≤ 1 frame early vs the exe drain) —
  bounded by one 20 ms step, documented in `AUDIO_SCHEDULER_ANALYSIS.md` §3.

## 4. Original-side context
- The exe drives DirectSound buffers through its audio module; retail data never enables
  the DirectSound3 lane (`AUDIO_RUNTIME.md` pass-99), and the `GetSoundForceNotOwner-
  Notification` import (`PTR_DAT_005896d0`, A11) is unreferenced in shipped code paths.
- No original-side device-latency measurement is possible (Wine capture lane dead);
  the port's ≤ 22 ms budget is compared against the exe's ≤ 20 ms scheduling drain. [C]

## 5. No buffer-size changes
Deliberate: no buffer reductions were applied. Reducing below the measured 2 ms onset
would risk underruns without improving fidelity — the scheduling layer, not buffers,
carries all remaining deviation, and it is pinned by gates (`AUDIO_TIMING_VALIDATION.md`).
