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
  must live in the `FUN_0048d0dc` pick logic (phrase id source) — **resolved: see §Pick predicate below**.

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
- **Channel duck/mix table**: byte scale table `DAT_0064b9dc[]` (stride 4, window
  0x64b9dc..0x64bddc) implements bus ducking — key-on voice vol = velocity * tbl[chan].vol / 0x7f and
  every CC7 write re-applies `SNDvol(handle, vol)` to all active voices of that channel. The applier
  loops at 0x4e3c7f..0x4e3cc2 / 0x4e40b6..0x4e40e1 sit in the MIDI timer callback; model + full
  semantics in §pass 7d (band was undecomposed when this section was written).
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
  - **Disproved pass 7d**: `FUN_004c46a0` decompiles to queue init (`s_qinit`, cmn_queue.c line 173) — no string tables, no percent writes. Scene defaults are written at runtime by scene handlers through setter FUN_0049067c; exact percentages remain Unknown (runtime args).
- Port ramp tick count `kRampTicks = 5` (original steps argument unmeasured) — ponytail-marked in
  `app/commentary.hpp`.

### Native mapping
`SpeechGain` (app/commentary.hpp) mirrors the state cb 16.16 arithmetic; Commentary::pump runs
queue pump + gain tick + per-voice `setGain(class * master)` each frame, with sentence-start/stop
ramp edges reproducing the autovol seam sites. Gate: `test_commentary` (`gainModel()` exact-tick
sequence asserts + wiring asserts at fixed timeline positions).

## Pick predicate + activation chain (pass 7c, 2026-11-06)
Evidence: `analysis/pass7/pick.txt` (builder/qfind decompiles), `pick2.txt` (producers/enqueue
helpers), `pick3.txt` (raw disasm: builder, pick wrapper, activation tail), `pick4.txt` (predicate
callback + record helpers). Scripts `ghidra-scripts/GhidraWc98Pick{1,2,3,4}.py`.

### Confirmed
- **Pick = list lookup; no rotation.** `FUN_0048d0dc` is a 56-byte wrapper: if the module init flag
  (`DAT_0063241c`) == 0 return null, else forward to `FUN_004c4ce0` (**qfind**, cmn_queue.c string
  `s_qfind` @0x5695c8). No counter/modulo anywhere in the chain — the round-robin Hypothesis had no
  basis here.
- **Builder (disasm 0x489460):** 6-slot event ring at `DAT_0057e380` stride 0x10: +0 payload,
  +4 event code, +8 pending flag, +0xC class word. Per slot with pending == 1:
  `EAX = [slot+0xC]`, `EDX = slot index (0..5)`, `CALL FUN_0048d0dc`; non-null pick result →
  `CALL FUN_0048d1f8` (activate); the three fields of every scanned slot are cleared unconditionally.
- **Pick wrapper (disasm 0x48d0dc):** saves EDX (slot index) on the stack; target queue =
  `DAT_00632428 + ([slot+0xC] << 2)` — a handle array indexed by the event's class word; predicate
  callback pointer = **FUN_0048cf10** (`MOV EDX,0x48cf10`).
- **Predicate FUN_0048cf10 (disasm):** `MOV ECX,[EDX]; CMP ECX,[EAX+0xC]` → true iff the record's
  +0xC tag field equals the ring slot index. qfind walks the class queue from its head (`obj+8`,
  node-linked list) under a critical section and returns the first match, else null.
- **Activation `FUN_0048d1f8` (disasm):** record's embedded segment subqueue at +0x14: drain loop
  qpop (`FUN_004c4890`) → per-item start thunk `FUN_0048d0c0` → `FUN_004e3330`; then qfree the
  subqueue (`FUN_004c4720`) and **remove the record** from its class list (`FUN_004c4a70` qremove on
  `DAT_00632428 + [rec+8]*4`). Picked records are consumed on use.
- Record fields: +6/+0xA = u16 voice-stream handles (-1 = none; play-status probe `FUN_004d9250`,
  autovol ramp helper at 0x48cf24 = vol 0x10 via `FUN_004e2b30`); +8 = class index into the handle
  array; +0xC = ring-slot tag; +0x14 = segment subqueue.
- Null pick → builder skips enqueue ⇒ **exhausted pool = event silent** (no commentary fallback).

### Hypothesis / Unknown
- The record preload that fills the ready queues and sets each record's slot tag (+0xC) is not in
  the analyzed band; per-event phrase *content* mapping stays **Unknown** (FCLG probe negative above:
  no sentence cross-map table found).
- Port model (`app/commentary.cpp`): shared commentary pool consumed once in storage order
  (head-scan first-match == qfind over a single-tag list), exhausted pool -> silent. Deviation
  marked in code; per-event tag table = open item.

### Gate
`test_commentary`: consumption asserts — picks step 0→2→3→4→5 (empty phrase skipped), the 6th event
is silent (`sentencesStarted == 5`, pool exhausted); gain seam re-verified on a fresh instance.

## Duck/mix table + percent-table scene defaults (pass 7d, 2026-11-06)
Evidence: `analysis/pass7/duck.txt` (percent getters + FUN_004c46a0 decompiles), `duck2.txt`
(SNDmidiinit/SNDseqstop + table-window byte scan), `duck3.txt` (function created at LAB_004e3bd4
after forced disassembly: applier decompiled; percent setter callers). Scripts
`ghidra-scripts/GhidraWc98Duck{1,2,3}.py`.

### Confirmed
- **The channel table is the MIDI module** (not speech): `DAT_0064b9dc` stride 4 x 64 slots =
  16 sequence tracks x 16 channels. Per-channel bytes: +0 CC10 expression, +1 CC7 volume,
  +2 pitch-bend merge byte, +3 program. Defaults {0x40, 0x7f, 0x40, 0xff} set by the init loop of
  **SNDmidiinit FUN_004e41d4** and restored per sequence channel by the reset loop of
  **SNDseqstop FUN_004e48d0**.
- **Applier = MIDI timer callback `LAB_004e3bd4`** (registered at DAT_006597cc by SNDmidiinit; band
  0x4e3bd4..0x4e41d4 — both §gain applier loops are inside this function, created in-pass after
  forcing disassembly): iterates the 16 sequence tracks per tick (@DAT_0064bee8), consuming each
  MIDI event stream:
  - key-on (status 0x90) → voice vol = velocity * tbl[chan].vol / 0x7f (int div), voice via FUN_004e2d74;
  - **CC7 write** (~0x4e3d20, loop at 0x4e40b6): `tbl[chan][+1] = data`, then every active voice on
    that channel (voice table DAT_0064bddc..0x64be9c stride 8, handle @+4) gets **SNDvol FUN_0052c71c**
    — the per-event bus ducking;
  - CC10 → tbl[chan][+0] + FUN_0052f1f0 per voice; pitch bend (0xE0) → tbl[chan][+2] = (d0 + d1*0x80)>>7;
    program change (0xC0) → tbl[chan][+3]; sequence end (F0 2F) triggers the FUN_004e48d0 default restore.
- **Percent-table API**: setter `FUN_0049067c` = `DAT_00632778[idx] = param2`, then fires a per-index
  notify callback stored at `DAT_004904b0[idx*4]` (null = none). Getter FUN_004906a4 has **no upper
  clamp** (only the ≥1 lower clamp); FUN_004906d0 two-stage scaling unchanged (§gain).
- **Scene defaults are written by scene handlers, not an init builder**: FUN_0046b130 writes five
  consecutive percent entries; FUN_0046b074 / FUN_0046b230 / FUN_0046b2b0 (menu/scene transitions)
  write single entries then call SNDspeech* helpers (FUN_0048ed9c / FUN_0048ed24). The §gain
  init-builder hypothesis is **disproved** — `FUN_004c46a0` = queue init (`s_qinit`, cmn_queue.c
  line 173), no string tables, no percent writes.

### Hypothesis / Unknown
- Exact scene default percentages = runtime arguments at the setter call sites (Watcom callee-clean
  drops them in Ghidra display) — stays **Unknown**; port keeps full-open defaults as before.
- Port model (`ChannelMix`, app/commentary.hpp): byte table + `scaled()` key-on formula;
  Commentary::pump applies it per frame to live speech voices (SNDvol-per-event semantics).
  Deviation: the original scales MIDI music voices; this port has no MIDI backend — speech slots
  occupy the channel window and default vol 0x7f = identity (no behavior change on a default table).

### Gate
`test_commentary duckModel()`: exact byte defaults for all 64 slots, CC7 masked writes + per-channel
isolation, int-div scaling (100*64/127=50), full-duck silence, percentToVol clamp + scene-seam ramp.

### Scene defaults measured (pass 7e, live Wine probe, 2026-11-06)
Setter breakpoints armed at process **entry** via `winedbg FIFAWC.EXE` launch-under-debugger
(paced command stream; every stop self-dumps registers + entry stack). Evidence:
`analysis/pass7/percent1.txt`, raw `percent_hits_v{2,3,4,5,6,7}_raw.txt`, scripts
`pct_run{3,4,6,7}.sh`, decoder `pct_v8_decode.py`. Two identical boot cycles captured:

| write site | caller (scene handler) | idx -> pct | entry regs |
|---|---|---|---|
| 0x46b095 | FUN_0046b074 | 4 -> 65 (0x41) | EAX=4, ECX=1, EDX=0x41 |
| 0x46b148 | FUN_0046b130 | 0 -> 80 | EAX=0, ECX=EDX=0x50 |
| 0x46b154 | FUN_0046b130 | 2 -> 80 | EAX=2, ECX=EDX=0x50 |
| 0x46b160 | FUN_0046b130 | 1 -> 80 | EAX=1, ECX=EDX=0x50 |
| 0x46b16c | FUN_0046b130 | 5 -> 80 | EAX=5, ECX=EDX=0x50 |
| 0x46b0f8 | FUN_0046b074 | 3 -> 80 | EAX=3, ECX=1, EDX=0x50 |

- **Setter args are Watcom registers: EAX = table index, EDX = percent** (ECX = extra param;
  Ghidra's callee-clean display dropped them - now resolved). Write order per cycle:
  4, then {0,2,1,5}, then 3. idx6=1 is **not** written at these sites (writer not hit in any
  observed window), so the table = {80,80,80,80,65,80,1} as snapshotted live at T+45 s twice.
- Live getter hits: idx4 via FUN_0048ed9c@0x48ee63, idx3 via FUN_0048fd54@0x48fd85 and
  FUN_0048fc78@0x48fcb7 (notify walk; ECX carried cb pointer 0x48fca0 = DAT_004904b0[3]).
- **Notify callbacks per index** (DAT_004904b0, live dump): [null, FUN_0048d7a0, FUN_0048e430,
  FUN_0048fca0, FUN_0048ec38, FUN_0049091c] - idx0 has no cb.
- Pass-7d "Unknown" line above is now closed: per-scene percentages for the FE boot + feflow
  cycle are measured; match-start values remain unmeasured (FE black-state, WINE_ORACLE_WC98 §3).
  Attach-lane limitation: winedbg attach lands after all scene writes (T+2..4 s floor), and the
  AV SEH-loop thread floods unpaced debugger streams - launch-under-debugger + pacing is the
  working recipe. Port keeps full-open defaults; no code change required (measured values are
  the same 80/65/1 set the port models).
