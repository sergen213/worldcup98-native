# COMMENTARY_TIMING_ANALYSIS — speech/commentary lane: triggers, delays, sequencing (pass-118)

Authority: `FIFAWC.EXE` bytes; lane decodes `AUDIO_RUNTIME.md` §7/§10/pass-12/pass-73/
pass-85..89, dossier `analysis/pass12speech/SPEECH_PRELOAD_TABLE.md`.

## 1. There is no static event→phrase table
Per-event objects carry `{event byte [obj+0x20] & 0x3f → 8-arm dispatcher 0x48f45c,
phrase id @+0x0E, queue-slot result written back @+6}`. Phrase selection is runtime:
dispatcher arm class byte `>> 6` → scene lookup `FUN_0048d070`, entry index
`2*DAT_0057eb54[class] + (classByte>>6)` (pass-87). [C]

## 2. Trigger conditions (live in-play lane, pass-86)
- Registered handler cluster at boot `FUN_00490528` (`CALL FUN_004e6b50`, site `0x49059b`):
  start `0x48f730`, record lane `0x48f830`, presentation `0x48f78c`, stop-all sweep `0x48f8bc`.
  Pointer-invoked — zero static call sites. [C]
- Start handler rejects while a sentence **speaks** (`SNDsentencestatus FUN_004dfc0c == 3`).
  Rejected records recycle to the free pool → phrase **dropped, never deferred, never
  interrupts** the running sentence. This is the cooldown: one sentence at a time. [C]
- Percent gate `FUN_004906a4(3)` = `pct*0x7f/100` min 1 — a count gate, not probability. [C]
- Presentation announcements (pass-85 flush lane) ride the separate `0x48f78c` lane —
  event commentary does not preempt them and vice versa. [C]

## 3. Timing / delay behavior
- **Live lane: immediate.** Event → phrase appended to the 20-slot ring (`DAT_0064b068`,
  cap 18) → `SNDflush FUN_004dfb40` starts the engine that same pump frame. No artificial
  delay exists in the exe live lane. [C]
- **Generic scheduler: delayed, runtime-generated.** Records `{+8 sceneId, +0xc delay
  0x7f/0x4a ticks, +0x10 t0, +0x14 priority DAT_00636a14+3, +0x18/+0x1c end}`; timing
  computed on the match clock `t*100/60`, gated by RNG (`FUN_004c321c % 5 == 0`, `% 200`
  jitter); pumped per frame `FUN_004906f8 → FUN_0048f920`. So periodic commentary is
  **not** fired per event — it is scheduled with per-record delays and jitter. [C for
  fields; scene-id table input undecoded → [H] wiring]
- Name insertion: player/team word banks `AUDIO\TEAM\A_T%04d.PBK`, `AUDIO\PLRS\P%05d.PBK`
  registered at boot (`FUN_0048fb10`), words fetched via the queue-selector chain
  (pass-87) — commentary lines interleave word fragments from these banks. [C]
- Gain: `SNDspeechautovol` ramp (`delta = (serverCur-cur)/frames`, clamp at target, applied
  through `SNDvol` to the voice-group pair, pass-73). Ramp pacing on the 20 ms pump tick. [C]
- Beat scheduler (delta==0 lane): fires only when `getvol <= threshold`; retail sites set
  threshold 0 → effectively silent. [C gate, semantics [H]]

## 4. Sequencing / interruption / replay / transitions
- Interrupt: **none mid-sentence**; stop-all sweep `FUN_0048d3f4` (site `0x4907ee`) on
  match stop disables the lane and flushes the preload queue; FE re-entry re-ramps music
  (`FUN_0048ec64` 4.0 s) but does not restart speech. [C]
- Replay/transition commentary enters through the presentation/registry lanes
  (`feflow35` schedule lanes, pass-103), not the live event lane. [C]
- Half/full-time events are world events → same live lane with the busy-reject rule.

## 5. Linux implementation
- `Commentary::triggerLive` mirrors busy-reject+drop exactly (`rejected()` counter);
  `Commentary::trigger` keeps the presentation seam.
- `SpeechGain` mirrors the 16.16 ramp with exact truncation (`test_gameaudio` pins
  `set(80)` → `ramp(1,3)`: applied 53, 27, 1, clamp).
- `Commentary::Scheduler` models the delayed records; **unwired** — live lanes are
  immediate, which matches the exe live lane; periodic lane fidelity stays [H].

## 6. Measured diffs and status
| Behavior | exe | port | Status |
|---|---|---|---|
| pump rate (pass-118 diff) | once per 20 ms engine frame | was `pump(2)` every 2 render frames (~33 ms @60 fps); now `pump(1)` per tick | fixed, live smoke onset |
| live reject semantics (pass-86 mirror) | reject-while-speaking, drop | `triggerLive` + `rejected()` | `test_commentary` live block |
| event drain gate (pass-118 diff) | ≤ 1 pump frame, halts on pause | was every 2 render frames incl. pause; now per-tick, pause-guarded | live smoke + `igPauseSmoke` |
| periodic lane | delayed records + jitter | modeled, unwired | open [H] (scene-id table) |
