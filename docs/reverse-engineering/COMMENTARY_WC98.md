# Commentary / speech engine — WC98 (pass 7, 2026-11-03)

Evidence: `analysis/pass7/sspeech.txt`, `analysis/pass7/triggers.txt` (PyGhidra runs
GhidraWc98Ssp.py / GhidraWc98Trig.py over the `cmn\sspeech.c` string cluster + PHRS file-open
xrefs). Answers item 4 of SPEECH_INDEXING_NOTES.md §5 (record-field consumers) and supplies the
trigger model for M6 commentary integration.

## EACSND speech API (exe-side thunks, all gated on `DAT_006597b0` init flag)
| API | VA | contract |
|---|---|---|
| SNDspeechinit(vol) | 0x4df820 | vol ∈ [0,0x80); registers pump callback `FUN_004dfc64` (DAT_006597e0) + state DAT_006597c4; err -14/-8 |
| SNDspeechautovol(v) | 0x4df8a4 | v ∈ (-1,0x80); scales `DAT_00588d6c = (cur - base)/v` per-phrase ramp |
| SNDphraseadd(id, param2) | 0x4df968 | ring of **20 slots × 12 B** at DAT_0064b070; slot+0 state (-100 = free), +4 u16 phrase id (CX), +6 i16 overlap, +8 param2; queued-count ≤ 0x12 else -13; returns slot index |
| SNDphrasestatus(slot) | 0x4dfa60 | -100→8(free); play-status 1→7 (complete), 0→3 (playing), else -1 |
| SNDsentencestart() | 0x4dfb40 | sets DAT_0064b15a = 1 ("speaking" flag) |
| SNDsentencestop() | 0x4dfb94 | clears flag, flushes two queues (FUN_004e2c60 ×2), resets autovol base |
| SNDsentencestatus() | 0x4dfc0c | 3 = speaking, 2 = idle |

## Game-side layer (`sspeech` client, DAT_006326b0 gate)
- **Init `FUN_0048fd54`** (callers: FE boot FUN_00409ef4/0x44b340 + 0x489350/0x490728): SNDspeechinit,
  opens the phrase stream (DAT_0063271c via FUN_00439810), reserves an arena of
  `((HDR.size@+8 + 0x14) >> 10) + 1` × 0x400 B and builds **8 voice records** (stride 0x2c, record link
  at +0x28 = arena cursor advanced by that block size).
- **Bank loader `FUN_00490250`**: opens `PHRS.HDR` → DAT_00632730 and `PHRS.PBK` → DAT_00632734
  (path via FUN_0048f9d0 = `AUDIO\PHRS\%s` + lang dir), then a **16-iteration table build**
  (FUN_004c4800 ×0x10) and two `FUN_0043933c` sidecar opens (`PHRS.INF`, u32@0 → DAT via FUN_00438dac).
- **Trigger gate `FUN_004893ec`** — **46 callers, addresses 0x452af0..~0x455xxx = the match engine
  event region** (kickoff/goal/foul per-caller-site): if speech banks ready and no sentence pending
  (DAT_0057e378), stop any running sentence, enqueue via FUN_004901e0 + FUN_00489460, then walk a
  **6-entry link table at DAT_0057e3e8** clearing played items. This is the event→phrase request seam.
- **Pump `FUN_0048f920`** (per-frame from FUN_004906f8): scans queue items (FUN_004c4890 = peek,
  FUN_004c4800 = advance; item+0x24 = live-slot marker); for each active item calls
  SNDphrasestatus — on 7 (complete) clears the marker, else breaks; when picking a new item sets
  `_DAT_006326bc = *(u16 *)(item->record + 0x12)` — **the phrase-bank record id field is consumed
  exactly here** (SPEECH_INDEXING_NOTES §5.4 resolved) — then invokes the per-item start thunk at
  `**(item + 0x1c)`.
- **Cleanup/shutdown `FUN_0048f6a4`, `FUN_0048fcc0`, `FUN_0048fe70`**: status-driven stop of stragglers,
  queue flush (item+0x24 = 0), release arena + streams.

## Model for the native port
Event → gate(≤1 sentence in flight) → phraseadd(id ring ≤20) → sentencestart; pump advances items by
record status, resolves u16@0x12 bank ids into segment runs (docs/asset-formats/AUDIO.md PHRS index),
starts playback via the voice records (8-voice pool). autovol = per-word volume ramp.

## Sentence assembly + queue flush (run `analysis/pass7/sspeech2.txt`, GhidraWc98Ssp2.py)
- **Event-flag builder `FUN_00489460`** (callers = 7 match-event handlers at 0x453a6c/0x454338/
  0x454d60/0x45548c/0x455a3c/0x455f30/0x458cf0): scans the event-record array at DAT_0057e388 (stride
  0x20) for slots whose pending flag == 1; per hit calls `FUN_0048d0dc` (pick phrase from bank) then
  `FUN_0048d1f8` (enqueue). The DAT_0057e378 gate + DAT_0057e3e8 links seen in the trigger wrapper are
  the same record structure's fields.
- **Queue flush `FUN_004901e0`** (FE exit, options screen, trigger wrapper): under critical section
  DAT_00632728 walks all queue items; items with a live cancel thunk at `**(item + 0x10)` get cancelled,
  then released via FUN_004d8aa0.
- **Stream kill `FUN_004e2c60`** (SNDsentencestop internals): walks the 16 playback slots
  (stride 0x18 at DAT_006597e8) comparing each slot's channel byte (+0x3b) against the current stream's
  channel (`&DAT_00659823 + idx*0x60`), stopping matches via FUN_0052ddbc. `FUN_004dfe18` resets the
  four sentence-state globals; `FUN_004d9250` = play-status passthrough (SNDphrasestatus backend).

## Open items
1. Site map (run `analysis/pass7/eventmap.txt`): of the 46 gate call sites, **45 sit inside
   undecomposed chunks** (`-noanalysis` DB — getFunctionContaining = None) in the match-director band
   0x452af0..~0x458cf0 (the 7 builder-caller functions above cover the same band); the only resolved
   function caller is **FUN_0045948c @ site 0x459715 = save/load screen** → commentary fires on load.
   Disassembly of that band + alignment with M6 sim event ids remains open.
2. **RESOLVED** (see Sentence assembly section): builder = event-flag scan + `FUN_0048d0dc` phrase pick
   + `FUN_0048d1f8` enqueue; flush = cancel-thunk walk at item+0x10. Remaining sub-item: FCLG id cross-map
   (NOTES §5.3) for what the picked bank ids index in the per-language commentary tables.

3. Item vtable at +0x1c: per-item start thunk target set (voice record vs queue item) — enumerate xrefs.

## FCLG cross-map probe (2026-11-03, negative result)
- `COMMON/FC<LANG>.BIN` = **UI/commentary string table**: header `FCLG\0V1.7\0=TXT NOT AVAIL=\0`, then
  6,389 NUL-separated strings (42,840 B; all 9 languages present). Not a binary event→phrase map.
- The PHRS record u16@0x12 takes only **32 distinct values** over the 1,290 members (bit-packed:
  high-byte classes + index fields), consistent with a bank/voice-slot selector key (runtime use = pump
  `_DAT_006326bc`), NOT an FCLG string index. NOTES §5.3 cross-map stays open; commentary text selection
  must live in the `FUN_0048d0dc` pick logic (phrase id source) — decompile it next.

## Speech gain / autovol chain (pass 7b, 2026-11-05)
Evidence: `analysis/pass7/gain.txt`..`gain5.txt` (scripts `ghidra-scripts/GhidraWc98Gain{,2,3,4,5}.py`,
pure-JPype runs on the saved wc98 project). Closes STATUS "gain calibration + autovol ramp open".

### Confirmed
- **Vol domain**: every EACSND volume entry point (`SNDvol` 0x52c71c backend FUN_0052c660,
  `SNDspeechinit`, `SNDautovol`) takes a byte vol ∈ [0, 0x80) and stores it as **16.16** (`vol << 16`)
  in the 16-slot voice table (stride 0x18 at DAT_006597e8; current = slot+0x20, ramp delta = slot+0x18).
- **Game-side percent → vol**: `FUN_004906a4` = `pct * 0x7f / 100`, clamp ≥ 1 (percent table
  DAT_00632778[idx]); two-stage per-voice scaling `FUN_004906d0` = `param2 * pct[idx] * 0x7f / 100 / 100`.
- **Speech master ramp** (`SNDspeechautovol(target, steps)` 0x4df8a4 + state cb @0x4dfca0 in the
  pump callback): live volume = DAT_00588d68 (16.16), target = DAT_0064b064; on call
  `delta = (target<<16 − cur) / steps` (IDIV trunc, steps clamped ≥ 1); each tick `cur += delta`, and
  when the ramp crosses/lands on the target it **clamps to target with delta zeroed** (both directions).
  Every tick applies `SNDvol(cur >> 16)` (arithmetic SAR) to the two speech output streams
  (DAT_00588d70/DAT_00588d74, call sites 0x4dfd06/0x4dfd19). Truncating delta leaves one tick short of
  target, which the overshoot-clamp closes on the next tick — reproduced in the port.
- **Per-voice ramp**: `SNDautovol` variant FUN_004e2b30 sets slot target (+0x1c) + per-tick delta; plain
  `SNDvol` (FUN_0052c660) sets current with **delta = 0** (immediate).
- **Channel duck/mix table**: applier loops at 0x4e3c7f..0x4e3cc2 and 0x4e40b6..0x4e40e1 (undecomposed
  band) recompute per active channel `vol = mixerTable[channel] * (voiceVol16 >> 16) / 0x7f` →
  `SNDvol(handle, vol)` each frame — a byte scale table at DAT_0064b9dc[] implementing bus ducking
  (music/CD scaled against scene priority while speech plays).
- **Hardware mix** FUN_00544bd0: output byte = product of the volume chain normalized by **0x1f417f
  (≈ 0x7f³)**; an alternative branch indexes a **byte curve/compressor table**
  (`tbl[byte(+0x26)] * factor / 0x7f`, then `out = tbl2[factor]`). Linear-domain products saturate at
  0x7f — explains the measured "levels hot, occasional saturation" (no headroom in the chain).
- **Scene ramp seam (game side)**: `SNDspeechautovol` sites 0x48fcb2-0x48fcb7 (preceded by FUN_004906a4
  percent conversion at 0x48fcb2) and 0x491125 = sentence start/stop ramps; the `steps` argument is
  runtime data in the undecomposed band.

### Hypothesis (marked as such in code)
- Percent-table DAT_00632778 scene defaults are built at init by FUN_004c46a0 from string tables →
  exact default percentages need runtime measurement; port default = full-open vol 0x7f.
- Port ramp tick count `kRampTicks = 5` (original steps argument unmeasured) — ponytail-marked in
  `app/commentary.hpp`.

### Native mapping
`SpeechGain` (app/commentary.hpp) mirrors the state cb 16.16 arithmetic; Commentary::pump runs
queue pump + gain tick + per-voice `setGain(class * master)` each frame, with sentence-start/stop
ramp edges reproducing the autovol seam sites. Gate: `test_commentary` (`gainModel()` exact-tick
sequence asserts + wiring asserts at fixed timeline positions).
