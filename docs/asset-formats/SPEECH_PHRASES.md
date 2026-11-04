# Speech phrase banks — record-table API + queue runtime (M6 commentary substrate)

Parsers: `wc98::formats::PhraseBank` (`formats/include/wc98/formats/phrs.hpp`,
`formats/src/phrs.cpp`). Runtime: `wc98::audio::SpeechQueue`
(`audio/include/wc98/audio/speech_queue.hpp`, `audio/src/speech_queue.cpp`).
Measured byte-level facts live in
[docs/reverse-engineering/SPEECH_INDEXING_NOTES.md](../reverse-engineering/SPEECH_INDEXING_NOTES.md)
(§1 record census, §2 stripped mirrors, §4 CRWD strays); engine ABI evidence in
[docs/reverse-engineering/COMMENTARY_WC98.md](../reverse-engineering/COMMENTARY_WC98.md).
Tree gates: `tests/unit/test_phrs.cpp`, `tests/unit/test_speechqueue.cpp` (run with
`WC98_TREE=$WC98_TREE`).

## 1. PhraseBank record-table API

Every `.PBK` speech member = **20-byte phrase record || payload** (NOTES §1). Parsing a bank:

```cpp
const auto bytes = wc98::readFile(root / "AUDIO/PHRS/PHRS.PBK");
const PhraseBank phrs = PhraseBank::parse(bytes);            // strict
const PhraseBank type = PhraseBank::parse(typeBytes, true);  // TYPE.PBK needs the tolerant path
```

| accessor | meaning (gated by test_phrs on the original tree) |
|---|---|
| `phraseCount()` | top-level BIG members ("phrases"): PHRS/TYPE **1,290**; PLRS/P00162 2; COMOS 6; CROWD 16 |
| `recordCount()` | 20-byte records: PHRS **4,665** segments (nested per-phrase BIG archives); flat banks one record per member |
| `record(i)` | `{index, phrase, segment, size, classLetter, id, raw[20]}` — TOC order; fetch lazily by `(phrase, segment)` ordinal (all `.PBK` names are empty: indexing is positional) |
| `record.id` | u16le @raw+0x12 — global sound-segment handle; consumed per-frame by the original pump into `_DAT_006326bc` before the start thunk (COMMENTARY_WC98 "Pump"); PHRS ids are a **bijection onto {0..4,664}** |
| `record.packed()` | u16le @raw+0x04 = `(phraseOrdinal<<4) \| soundClass`; gated: `== 16*phrase + 4` on all 4,665 PHRS segments |
| `record.classLetter` | PT header tag 0x06 letter: `'c'` PHRS/PLRS/TEAM (MicroTalk), `'a'` CHNT (EA-XA loop banks), `'?'` non-PT payload (CRWD MIDx/raw) |
| `emptyPhrase(p)` | nested BIG archive with count=0, hdrSize=16 → **silent phrase** (268 in PHRS/TYPE; NOTES §2) |
| `memberBytes(p)` | raw pre-RefPack top-level member view (mirrors decode via `refpack::decompressIfNeeded`) |

`countMirrorPrefixMatches(stub, full)` implements the cheap NOTES §5.5 mirror check:
**TYPE.PBK[j] == PHRS.PBK[j][:len] on 1,290/1,290 members byte-exact** (test prints
`TYPE/PHRS mirror prefix equality: 1290/1290 members byte-exact`).

### Mirror leniency (`BigArchive::parse(data, tolerant = true)`)

TYPE.PBK is a header-only twin whose nested TOC offsets are self-relative to the **full**
sibling image in PHRS.PBK and run past each truncated member end; strict parse throws
(`BIG entry '' … exceeds archive`, `count implausible`). The optional flag (default false —
`.VIV`/`.PAK`/layout callers keep strict behavior) clamps such payloads to the member end,
keeps the name, sets `BigEntry::truncated`, and stops at a readable TOC-table prefix.
PhraseBank skips records whose 20-byte header is not kept in a stub; phrase counts stay exact.

### Decode dispatch (CRWD strays stop being dead)

`decodePlayTrack(data, ptOffset)` branches on the payload magic at `ptOffset` (NOTES §5.2):

* `"PT"` → full PT v2 header/codec decode — **byte-identical path for all 6,964 play-track
  members** (test_audio gate unchanged: 6,964 decoded = 6,851 MicroTalk + 113 EA-XA).
* `"MIDx"` → CRWD/COMOS.PBK crowd-MIDI streams → silent `Pcm{}` (no PCM; Midx::parse owns the stream).
* empty nested BIGF / no-magic raw block → silent `Pcm{}` (268 PHRS silences + CROWD.PBK
  crowd-bed loops — §4 Hypothesis, undecoded). Callers needing strictness gate on PT magic first.

## 2. SpeechQueue ↔ engine ABI mapping

Self-contained ring model of the EACSND speech layer; no audio device — presentation/app wires
`onStart` (segment fetch by `record.id`, mixer play) and `onComplete` (play-status probe).

| original (VA) | wc98 | notes |
|---|---|---|
| `SNDphraseadd(id, param2)` 0x4df968 | `SpeechQueue::addPhrase(id, overlap, param2)` | ring **20 slots × 12 B** fields mirrored in `Slot{state,id,overlap,param2}`; queued ≤ **0x12** else **-13**; returns slot index |
| `SNDphrasesqueued` 0x56ae14 cluster | `queued()` | non-free slot count |
| `SNDphrasestatus(slot)` 0x4dfa60 | `status(slot)` | -100→**8** free, play-status 1→**7** complete, 0→**3** playing, else **-1** (pending maps here) |
| `SNDsentencestart()` 0x4dfb40 | `sentenceStart()` | sets speaking flag (DAT_0064b15a = 1) |
| `SNDsentencestop()` 0x4dfb94 | `sentenceStop()` | clears flag + flushes all slots; stream-kill fan-out (FUN_004e2c60, 16 playback slots) is the mixer's job at M6 wiring |
| `SNDsentencestatus()` 0x4dfc0c | `sentenceStatus()` | **3** speaking / **2** idle |
| pump `FUN_0048f920` (per-frame from FUN_004906f8) | `pump(frames)` | live-slot scan breaks on the first non-complete item; new-item pick latches `record.id`, invokes `onStart` (= start thunk at item+0x1c); one pickup per frame; slots reported complete free next frame |
| autovol ramp (SNDspeechautovol, DAT_00588d6c) | not modelled | volume policy = mixer/app (open below) |

Port-side states: `-100` free / `0` playing / `1` complete mirror the engine raw codes;
`-50` pending is a port addition (engine queue items are separate arena records — item+0x24
live marker). `overlap` (+6 i16) = frames to defer the start thunk; negative inputs clamp to 0.

## 3. Open items carried from NOTES §5 / COMMENTARY_WC98

* **§5.1 sentence assembly**: fetch segments lazily by `(phraseIdx, segTocOrdinal)` and key
  runtime handles on `record.id` — done at the index layer; the phrase *selection* source
  (`FUN_0048d0dc` pick logic) is not decompiled yet (COMMENTARY_WC98 open item 2).
* **§5.3 FCLG cross-map**: what the u16@0x12 ids index stays Unknown; the FCLG probe was
  negative (UI string table, COMMENTARY_WC98 §FCLG) — sentence = ordered run of segment ids,
  producer TBD.
* **§5.4 record-field consumers**: id@0x12 resolved (pump); u16@0x04 / s32@0x08/0x0C consumer
  semantics remain open; queue links 0x08/0x0C are Hypothesis, not modelled in SpeechQueue.
* CROWD.PBK raw payload purpose (§4 Hypothesis) and autovol per-word ramp: wire at M6 integration.
