# AUDIO_EVENT_TRIGGER_MAP — every sound category: exe trigger vs port (pass-118)

Authority: `FIFAWC.EXE` bytes. [C] proven from disassembly, [H] inferred. Lane citations
in `AUDIO_RUNTIME.md`; measurement notes in `WINDOWS_AUDIO_TIMELINE.md`.

## 1. Mechanism layers (exe)

| Layer | Evidence |
|---|---|
| SFX request queue | callback table `PTR_FUN_005896b4..005896c8` installed by `FUN_004e6b50` (boot `0x49057f`); gate `0x48f728→0x48ffb8` probes queue `DAT_00632734`; play slot `0x48f830` submits via `FUN_0048f3e0`; pump slot `0x48f8bc` advances + invokes entry cb; audible ≤ 1 pump frame. Scene ∉ {10,0xc} enables the lane (stub-disabled otherwise). pass-61 [C] |
| SFX throttle | `tick % 100 >= interval byte` (`FUN_0051d670`, `DAT_0058f3b0+3`); ambient spec table `DAT_00644d50` ≤ 20 entries scene-9-only; range gate `|DAT_00646924| < DAT_00644934` + window `tick - DAT_00644d5c`. pass-61 [C] |
| id→sample | `{bank:1, slot:24}` id layout, bank subsystem §10 (pass-62) [C] |
| Speech live lane | registered handlers `0x48f730` start / `0x48f830` record / `0x48f78c` presentation / `0x48f8bc` stop-all sweep (`FUN_00490528`, pointer-invoked, zero static callers); phrase `[entry+0x0E]`; busy-reject = record recycled → **event phrase dropped, never deferred, no interrupt**. pass-86 [C] |
| Crowd handlers | scene-handler registry (`CALL 0x4e7060`): start `0x48e5a0`, percent seam `0x48e550` (`arg*511/100`, `SNDcrowdcontrol` ≤0x1ff), pulseA `0x48e488`, pulseB `0x48e4fc`; tick `0x48e68c` = MIDI drain; reaction seam `0x48e3b0` → `SNDseqplayPVB 0x4e462c` (MIDI sequence, PAN/VOL/BEND all -1). pass-100 [C] |
| Chant lane | pump `FUN_0048da50` stamp gate `0x48dacc`: `DAT_0057eb40 <= DAT_00636a14 ⇒ +100` (1.000 s), rotation loop `0x48dbeb..0x48dc07` (LCG draw, parity re-queue). pass-118 [C] |
| Priority | `FUN_0048f664` = max priority field `+0x18` scan over queues `DAT_006326e4/632700`; reject-while-sentence-running (`FUN_004dfc0c == 3`). pass-73/86 [C] |
| Percent gate | `FUN_004906a4` = `pct*0x7f/100` min 1 — a **count**, not probability (pass-86 correction) [C] |

## 2. Category map

| Category | exe trigger condition | exe timing / delay | ordering & priority | Port implementation | Verification |
|---|---|---|---|---|---|
| Kickoff whistle | match-start event → speech/SFX lane via play slot (per-scene handler registry, pass-101 attribution) | within ≤ 1 pump frame of the event | speech lane rejects while sentence running | kickoff seam in `livematch`/`match_demo` event drain → `triggerLive` | `fe_igmenu` restart lane: post-restart kickoff whistle ≥ 2 |
| Kick / pass / shot SFX | ball-contact event emitted by rules lanes; game code calls play slot `0x48f830` | audible ≤ 1 pump frame; pos/pan lane by flags byte `rec+0x20 & 0x3f` | request queue FIFO; no interrupt | sim contact events drained per tick → SFX seam | live smoke `events whistle/crowd` counts; `match-audio-events` gate |
| Ambient / scene SFX | scene-9 spec table entries, insert-sorted by plays count | throttled `tick % 100 >= interval byte`; windowed by ambient range table | per-id level byte clamped at boot max `DAT_00644de4` | not mirrored (port plays scene bed only) [H] | n/a — exe-side lane |
| Goal chain | goal event → commentary class phrase (dispatcher `0x48f45c` 8-arm), crowd pulse seam, `feflow35` result lane (pass-102/103) | speech immediate if idle (else dropped); crowd pulse at pump frame; celebration lane per schedule | speech priority `+0x18` max comparison | goal seam: pulse + `triggerLive` + celebration presentation | `match-audio-events`, `match-crowd-envelope` gates |
| Foul / card whistles | referee event → play slot | ≤ 1 pump frame | FIFO | foul/card event seam | live smoke counts |
| Crowd pulse | pulseA/B handlers: tick-word remainder selects slot; `SNDcrowdpulse(idx, slotrecord)`; seam plays stamped MIDI sequence id | onset ≤ 1 pump frame; ramp 16.16 step `0x38000` toward ceiling 450 (card record, pass-55) | gated by percent idx2 `arg*pct[2]*127/10000 > 0` | `CrowdEnvelope` + pulse mirror | `match-crowd-envelope` gate; `test_gameaudio` block (h) words 17302/22024 |
| Chant load | stamp gate, cadence 1.000 s | arrival on stamp boundary, rotation at arrival | parity re-queue (bit0 qbottom, bit1 qpush) | `pumpChants(clockTicks)` stamp mirror | `audio-timing` selftest step (per-record fields) |
| Commentary live | event byte `rec+0x20 & 0x3f` → dispatcher arm; phrase `[entry+0x0E]` | immediate when sentence not running; else **drop** | reject-while-running; presentation lane separate | `Commentary::triggerLive` + `rejected()` counter | `test_commentary` live block; `match-audio-events` |
| Commentary periodic | scheduler records `{+8 sceneId, +0xc delay 0x7f/0x4a, +0x10 t0, +0x14 priority, +0x18/+0x1c end}`, RNG gates `%5==0`, `%200` jitter | delay field in ticks; clock `t*100/60` | priority `DAT_00636a14+3` | `Commentary::Scheduler` modeled, **not wired** [H] | open — scene id table undecoded |
| Beat scheduler | beat table `DAT_0064b068` walk while autovol delta==0 | fires only `getvol <= threshold` (retail threshold 0 → silent) | 20-slot cycle | not mirrored [H] | pass-87 gate facts recorded |
| Music transitions | FE scene-change `FUN_0048ec64(…, 0xc8)` | 200 ticks = 4.0 s gain ramp on 20 ms clock | single voice, duck lane | `Mixer::fade(id, target, ticks)` 20 ms steps | register row 8 |
| Board announcer | scene registry dispatch (pass-101) | immediate | presentation lane | presentation seam `trigger` | gate `match-audio-events` |
| Movie audio | `.DCT` DCT lane (pass-74, A9 recoverable) | audio-clock paced | — | video-play lane | `wc98-video-play` |
| Replay audio | `feflow35` prologue schedule lanes (pass-103), schedule `DAT_0057e944`, 220 ms/tick timer clock | scheduled entries | — | replay start sweep mirrors stop-all `FUN_004d1540` | restart lane `fe_igmenu` |
| Pause / restart | tick host halts pumps; stop-all sweep + speech preload flush (`FUN_0048d3f4` site `0x4907ee`) | stamp stale → one arrival on resume | — | pumps inside pause-guarded tick loop; `sfxPos`/`evPos` reset | `igPauseSmoke`, restart lane |

## 3. Not audio-triggered in the exe
Player footstep/foot-contact sounds **do not exist**: no per-animation SFX lane was found
in any pass (all audio flows through the request/speech/crowd lanes above). The port must
not invent one — 1:1 fidelity rule.
