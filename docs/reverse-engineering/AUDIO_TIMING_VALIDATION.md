# AUDIO_TIMING_VALIDATION — verification status for the timing pass (pass-118, 2026-11-21)

All numbers below are from runs executed on this host (host X11/KDE session, OpenAL Soft
loopback lanes, `SDL_VIDEODRIVER=dummy` for UI lanes). Wine audible-capture lane is dead
(black DirectDraw) — **expected values are the exe's own byte-level schedule (stamp
boundaries, pump-frame drain), not measured Windows audio**. That is the documented limit
of this framework; see `WINDOWS_AUDIO_TIMELINE.md` preamble.

## 1. Per-record timing fields (selftest `audio-timing`, this run)
JSON schema keys (`app/selftest.cpp::writeJson`): `step/subsystem/expectedEvent/
actualEvent/selectedAsset/playbackTimestampMs/measuredResult/tolerance/passed/diagnostic/
skipped`. The step's `measuredResult` carries the per-arrival record fields —
scenario (`1x`/`5x` pump phase), event id (`chant-load#n`), expected Windows onset
(stamp grid `stamp_i = 1 + i*100` timer ticks — the lane starts pumping at
tick 1; the exe re-arms the stamp from the **current tick** (`0x48DADE`: `add eax,0x64;
mov [DAT_0057EB40],eax`, re-arm site `0x48DAE3`), so the grid stays `1 + i*100` under
exact-cadence arrival), actual onset, diff, tolerance (±0), schedule delay
(`1 pump frame` — exe pump-body gate), output stage (first sample enters the OpenAL mix
0.6-10.9 ms after play, median 2.1 ms - `mix_onset_probe.out`; sink tail ~35 ms measured
via pass-118 `latency_probe.cpp`), per-record pass AND'd into the step verdict (full
`measuredResult`, incl. `chant-load#6` and the counts tail). Existence and verdict are now
enforced by the `selftest_json_schema` gate itself (asserts the `audio-timing` step exists,
`passed` — device-skip tolerated on headless — and carries `chant-load#` records; the report
is deleted before each run so a stale file cannot pass), not just by grep. Observed, all six records **diff=0**:

```
[1x] chant-load#1 exp=10ms    act=10ms    diff=0ms tol=0ms sched=1pumpframe plays=3 ... pass=true
[1x] chant-load#2 exp=1010ms  act=1010ms  diff=0ms tol=0ms sched=1pumpframe plays=3 ... pass=true
[1x] chant-load#3 exp=2010ms  act=2010ms  diff=0ms tol=0ms sched=1pumpframe plays=3 ... pass=true
[5x] chant-load#4 exp=3010ms  act=3010ms  diff=0ms tol=0ms sched=1pumpframe plays=3 ... pass=true
[5x] chant-load#5 exp=4010ms  act=4010ms  diff=0ms tol=0ms sched=1pumpframe plays=3 ... pass=true
[5x] chant-load#6 exp=5010ms  act=5010ms  diff=0ms tol=0ms sched=1pumpframe plays=3 ... pass=true
 counts 1x=3 5x=3 (identical, pump-rate-invariant); plays=3
```
Arrival == expected == stamp boundary exactly; no port-side delay term exists at the
cadence gate (the exe gate likewise fires on the first pump call at or past the stamp,
`0x48DACC` bytes — `analysis/pass118timing/0057pump.txt`). The exe re-arms the stamp
**from the current tick** (`add eax,0x64; mov [DAT_0057EB40],eax`, `0x48DADE/0x48DAE3`,
`stamp = tick + 100`); at smoke cadence `tick == stamp` at arrival, so the port mirror's
re-arm (`stamp_{i+1} = arrival + 100`) lands on the identical grid, `diff = 0` on every
record. `plays` is the chant request counter (`MatchAudio::chantPlays`, `game_audio.cpp:481`):
arrival #1 fills the three chant lanes (3 lane requests = plays 3), later arrivals dedup
against the active 3-slot pool (`DAT_006324d4` cap semantics), so the count stays 3
across all six records — the cadence records gate on the arrival tick, not on plays.

## 2. Gates executed (this pass)
| Lane | Result |
|---|---|
| `test_audio` (bank/samples) | Passed 11.06 s |
| `test_gameaudio` (cadence pin, envelope words 17302/22024, targets 31/40, SpeechGain 53/27/1 clamp, fog-lane pins) | Passed 0.15 s |
| selftest `audio-timing` | **PASS**, records above |
| selftest `match-audio-events`, `match-audio-lanes`, `match-crowd-envelope`, `audio-device-render` | PASS (35/37 steps; see §3) |
| `fe_igmenu` restart lane (scripted RestartAsk → kickoff whistle ≥ 2 post-restart) | passed in the full dummy-lane suite (§3); an earlier bare-mode flake probe mis-operated the lane — no port defect |
| full ctest suite (dummy lane) | see §3 |

### DCT drain granularity gate — hidden-failure fix (2026-11-21, commit `ec4f64b`)
Before: `video_play`'s `PASS_REGULAR_EXPRESSION` matched the stats line unconditionally, so
ctest ignored the exit code — `movie-audio-granularity BAD` (exit 4, `app/video.cpp`) printed
on every gated run yet the suite showed green. The BAD was real: the drain loop added the
full 2048-frame slice to `movieRenderedFrames` before the counter check and clamped to the
stream end only *after* it, so the final partial slice (1494277 = 729×2048 + 901) overshot
the true played counter by 763 frames — `d < 0` fired once per run (probe:
`gran BAD d=-763 rendered=1495040 played=1494277`). Root cause = check/clamp ordering, not
counter units: the trace confirms `framesPlayed()` tracks rendered exactly (offset advances
2048 per 2048-frame render call, buffer charges at unqueue). Fix: clamp before the check in
`app/video.cpp`; `video_play` gains `FAIL_REGULAR_EXPRESSION "granularity BAD|validation
message"`. Negative-tested by disabling the clamp (test #50 Failed on the BAD line), then
restored (test #50 Passed, exit 0, no BAD line).

## 3. Suite result
Full `ctest --test-dir build/release` under `SDL_VIDEODRIVER=dummy QT_QPA_PLATFORM=offscreen`:
**157/159 passed**. The two failures are `test_sim_scenarios` and `self_test_gameplay`
(steps `passing`, `animation`) — foreign uncommitted simulation WIP in the user's worktree
(`simulation/*`, `tests/unit/test_sim_*` modified), unrelated to audio; neither was claimed
and no audio file changed them. The same run additionally SEGFAULTed `replay_record_play`
against stale recorded fixtures (`/tmp/wc98port-out/replay`, same stale-fixture pitfall as
the `fe_replay57_enter` lane): deleted, gate re-recorded fresh, green on reruns. Root
cause **unresolved**: truncation repro (`truncate -s 40` over every `rply*.dat`, rerun)
does not reproduce the crash (gate exits 0, MATCH), so only the crash-on-stale-fixtures
observation stands — candidate replay-loader robustness issue, no repro found. Suite
after the retag follow-up: same **157/159**, same two foreign failures.

## 4. Category verification status
| Category (AUDIO_EVENT_TRIGGER_MAP.md) | Verification |
|---|---|
| chant cadence | `audio-timing` per-record fields (§1) |
| crowd bed/pulse/envelope | `match-crowd-envelope`, `test_gameaudio` block (h) |
| commentary live lane | `test_commentary` accept→reject→accept, `match-audio-events` |
| commentary periodic lane | open [H] — modeled, unwired (scene-id table undecoded) |
| SFX event seam | live smoke `events whistle/crowd` counts (pass-118) |
| pause/restart | `igPauseSmoke`, `fe_igmenu` restart lane (§2) |
| music fade | register row 8 — constants mirror, no runtime gate yet |
| beat scheduler / ambient throttle | exe-side lanes, not mirrored [H] |
| backend latency | mix onset 0.6-10.9 ms after play (median 2.1 ms, `mix_onset_probe.cpp`) + sink tail ~35 ms (`latency_probe.cpp`), `analysis/pass118timing/latency_probe.cpp` (`AUDIO_LATENCY_ANALYSIS.md` §1) |

## 5. CI lane
Pass-118 lanes all green on GitHub Actions: code commit `17d14cc`; selftest-record-field
+ docs commit `2a1f432` watched run 44873462238 (✓ build gcc-cxx23 ci-cxx23, ✓ AppImage);
retag/probe-wording follow-up watched on its own push run.

## 6. Remaining fidelity work (not timing)
- exe generic-commentary scene-id table decode → wire `Commentary::Scheduler`.
- CRWD phrase→MIDI mapper semantics [H]; MIDI device lane blocked (A5).
- Beat-scheduler gate operand [H].
