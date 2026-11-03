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

## Open items
1. Map the 46 call sites to concrete sim events (needs M6 match event enum alignment; site addresses
   above are the anchors — decompile each caller once the simulation-side event ids exist).
2. `FUN_00489460` + `FUN_004901e0`: sentence assembly from bank ids (FCLG cross-map, NOTES §5.3).
3. Item vtable at +0x1c: per-item start thunk target set (voice record vs queue item) — enumerate xrefs.
