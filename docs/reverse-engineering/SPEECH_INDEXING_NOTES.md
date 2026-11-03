# Speech (.PBK) phrase-bank indexing — measured notes (2026-11-02)

Read-only audit of `WorldCup98_Extracted/AUDIO/**` (game tree untouched; ISO + FIFAWC.EXE
scanned as raw bytes only). Every number below was re-measured this pass with a lenient
BIGF/RefPack/PT python parser mirroring `formats/src/big.cpp`, `refpack.cpp`,
`schl::PtHeader::parse`. Companion docs: [AUDIO.md](../asset-formats/AUDIO.md),
[AUDIO_RUNTIME.md](AUDIO_RUNTIME.md).

Scope correction to prior notes: the 1,232 `.PBK` files are **not** all under
`{PHRS,CHNT,PLRS,TEAM,PBP}`. Actual census (full AUDIO walk):
`PHRS/2 (PHRS.PBK + TYPE.PBK)`, `PLRS/1020`, `CHNT/113`, `TEAM/95`, `CRWD/2
(CROWD.PBK, COMOS.PBK)`, **PBP has zero `.PBK`** (its 756 banks are standalone `.SMP`).
Top-level members: PHRS.PBK 1290 + TYPE.PBK 1290 + PLRS 2012 + TEAM 174 + CHNT 113 +
CRWD 22 = 4,901; PT-decodable members 6,964 (2,299 flat + 4,665 nested in PHRS.PBK),
exactly matching the `test_audio` gate (6964 / 6851 MT / 113 XA / other 1044).

## 1. The 20-byte per-member phrase record

Every speech member = `record[20] || payload`. Payload magic at offset 20 is usually
`PT` v2 (`50 54 00 02` + u32le headerSize; tag 0x88 == headerSize on **6,964/6,964**
members -> the codec bitstream starts immediately after the PT header), but see CRWD
below for `MIDx` payloads — dispatch must be by payload magic, not assumed PT.

Record field census over all 6,964 PT members + the 22 CRWD oddities:

| off | width | observed values | reading | confidence |
|---|---|---|---|---|
| 0x00 | u32 LE | `0` on 6,964/6,964 | reserved (never nonzero; likely runtime-patched link) | Confirmed constant / semantics Unknown |
| 0x04 | u16 LE | CHNT `0`, TEAM `1`, PLRS `3`, CRWD/CROWD `0`, CRWD/COMOS `0x0F`; PHRS = exactly `16*phraseIdx + 4` on **1,022/1,022** phrases (all segments of a phrase identical) | packed `(phraseOrdinal<<4) \| soundClass`: class enum {chant=0, team-announcer=1, player=3, commentary-phrase=4, crowd-midi=15?}; flat banks carry the class only (ordinal 0). The `phraseIdx%16` cycling of the low byte reads as a 16-way announcer round-robin | Layout **Confirmed-by-correlation**; enum/voice meaning **Hypothesis** |
| 0x06 | u16 LE | `-1` on all 4,665 PHRS segments; `0` elsewhere | sentinel (unset link) | Confirmed pattern / semantics Unknown |
| 0x08 | s32 LE | `-1` for every PHRS segment **and** the 16 CRWD/CROWD.PBK members; `0` in PLRS/TEAM/CHNT | runtime queue/overlap link slot (`SNDphraseadd - OVERLAP OF %i OUT OF RANGE.` exists at VA 0x56aed0) — initialized only for banks that chain | Confirmed pattern / semantics **Hypothesis** |
| 0x0C | s32 LE | same split as 0x08 (`-1` PHRS/CROWD, `0` flat) | second link slot (prev/next pair with 0x08?) | Confirmed pattern / Unknown |
| 0x10 | u16 LE | `-1` on all PHRS; `0` elsewhere | sentinel adjacent to 0x12 id | Confirmed pattern / Unknown |
| 0x12 | u16 LE | PHRS: a **bijection onto {0..4,664}** (min 0, max 4,664, 4,665 distinct over 4,665 segments); every phrase owns a contiguous id block in phrase order (**1,022/1,022**); the within-phrase permutation is NOT storage order, size rank, or sample-count rank (tested 0/1,022 each) | global sound-segment handle / word-slot index (FCLG `FC<LANG>.BIN` commentary tables are the plausible referent — unverified) | ID-bijection **Confirmed-by-correlation**; what indexes it **Unknown** |

Worked anchors (file + byte offset of the 20-byte record):
- `PHRS/PHRS.PBK` @ 80,440 (phrase 0 segment TOC-index 5): `00 00 00 00 | 04 00 FF FF
  | FF*8 | FF FF 01 00` -> u16@4 = 4 = 16*0+4; id @0x12 = 1.
- `PHRS/PHRS.PBK` @ 94,764 (phrase 1 seg 0): u16@4 = 0x0014 = 16*1+4, id = 8. Phrase 0
  ids in TOC order `[5,0,4,2,3,1]` — permuted within its block {0..5}.
- `PHRS/PHRS.PBK` @ 92,805,008 (phrase 1289 seg 0): u16@4 = 0x5094 = 16*1289+4,
  id = 4,664 (last).
- `PLRS/P00162.PBK` @ 2,956: `00*4 | 03 00 00 00 | 0*12` — class-only record; both
  members of a 2-member player bank carry identical records -> variant choice is by
  member ordinal.
- `TEAM/A_T0031.PBK` @ 4,980: class byte `01`, rest zero. `CHNT/C0003100.PBK` @ 76:
  all-zero record (EA-XA loop; tags carry 0x86/0x87 loop bounds — present on exactly
  the 113 CHNT members).
- Codec split by group: PHRS 4,665 + PLRS 2,012 + TEAM 174 = MicroTalk tag 9; CHNT 113
  = EA-XA tag 7. PT headerSize census {32:2679, 36:4172, 40:21, 44:89, 48:3}; the
  leading `tag 0x06 len 1` byte is `'c'`(99) for PHRS/PLRS/TEAM and `'a'`(97) for CHNT
  — a class letter inside the PT header mirroring record class.

## 2. Verdict on the 1,022 "stub" BIGF members: stripped mirror banks

All 1,022 are top-level members of **one file**, `PHRS/TYPE.PBK` (76,480 B). It is a
header-only twin of `PHRS/PHRS.PBK` (92,811,196 B):

- Identical outer TOC shape: count 1290, headerSize 11,628; every member name empty in
  both files (**zero** non-empty names exist in any of the 1,232 PBK TOCs — indexing is
  purely positional).
- **`TYPE.PBK[j] == PHRS.PBK[j][:len(TYPE.PBK[j])]` for all 1,290 j (byte-exact).**
  Each member keeps just the nested BIGF header of its phrase and drops the sample data.
- Inside a stub, entries are perfectly contiguous (`offset - hdrSize == cumulative
  size of preceding entries`, non-overlapping 1,022/1,022): e.g. TYPE.PBK member 0
  (file offset 11,628, 72 B) declares count=6, segments [11680, 14216, 13608, 13968,
  15268, 14216] spanning 83,028 B — exactly the byte size of PHRS.PBK member 0.
- So the offsets are **self-relative and correct for the full sibling image in
  PHRS.PBK**; they are not cross-references into TYPE.PBK itself. `BigArchive::parse`
  throws `BIG entry '' [72+11680] exceeds archive (72)` because the mirror was truncated
  by design, and `count 10 implausible` when `count > size/12` on a tiny header.
- 268 of the 1,290 phrases are empty nested archives (`BIGF`, count=0, hdrSize=16,
  16-byte members in both files) — they parse "fine" and are neither PT nor stubs.

The engine opens the trio together — FIFAWC.EXE DGROUP strings (VA = raw - 0x158600 +
0x55a000): `AUDIO\PHRS\%s` @0x566b9c, `PHRS.INF` @0x566bdc, `SPCPATCH` @0x566be8,
`SPCEVENT` @0x566bf4, `PHRS.HDR` @0x566c00, `TYPE.PBK` @0x566c0c, `STRASYNCH`
@0x566c18, `PHRS.PBK` @0x566c24. TYPE = the phrase->segment layout table loadable
without dragging 92 MB of audio; PHRS.HDR is a standalone TOC snapshot (see §3).
Related templates: `AUDIO\TEAM\A_T%04d.PBK` @0x566bac, `AUDIO\PLRS\P%05d.PBK`
@0x566bc4, chant `C0%04d` + `CHNCHANT`/`CHNPATCH` @0x566a88-a8,
`AUDIO\CRWD\%s` + `CROWD.PBK`/`COMOS.PBK` @0x566ab4-acc, second `PHRS.INF` next to
`Language` + `%s\%s` @0x561d3c (per-language bank dir). Bank-handle API strings:
`cmn\shdr.c` / `SNDbankheadersize` / `SNDpatheadersize` @0x56b050+; phrase API
`cmn\sspeech.c` + `SNDspeechinit/SNDphraseadd/SNDphrasestatus/SNDphrasesqueued/
SNDsentencestart/stop/status` @0x56ae14-0x56afac.

**BIGF `declaredSize` quirk (measured 1,232/1,232 outer banks and 1,022/1,022 nested
archives):** in `.PBK` archives this header field is NOT the archive size — it equals
the **last TOC entry's size** (`PHRS.PBK`: declared 6,216 = final phrase byte size;
TYPE: 28; a phrase stub above: 14,216). PHRS.HDR carries 6,216 for the same reason —
it is byte-identical to `PHRS.PBK[:11,628]` and its offsets are contiguous from 11,628
with sizes-sum + header == file size exactly. Do not trust `declaredSize`; use TOC
extents.

## 3. Sidecar inventory — prior "586 INF / 240 HDR / 159 TYPE" is refuted

Full-tree scan of `WorldCup98_Extracted` **and** the ISO (`isoinfo -f`, 7,052 entries)
agree: AUDIO contains exactly one `.INF` and one `.HDR`, and **no `.TYPE` file exists
anywhere on either medium**. The 360 other `.INF`s in the tree are `AUTORUN.INF` +
SETUP/DIRECTX driver files. The per-voice sidecar story in asset-formats/AUDIO.md is
wrong; per-bank metadata lives in the record itself and in the filename templates
(`P%05d`, `A_T%04d`).

- `AUDIO/PHRS/PHRS.INF` (28 B): u32 LE @0 = 0; u32 @4 = **1,290** (= phrase count ✓);
  u32 @8 = **83,696** (Unknown — rejected candidates: sum of nested header sizes
  60,564, TYPE data region 64,852, segment count 4,665, sum-of-declared 17.4 M);
  ASCII `V8-7-4\0` @12 (phrase-builder version tag; occurs in this file only — not
  anywhere else in the tree or in FIFAWC.EXE); zeros to EOF.
- `AUDIO/PHRS/PHRS.HDR` (11,628 B) = exact PHRS.PBK TOC prefix (§2).
- `AUDIO/PHRS/TYPE.PBK` = stripped mirror bank (§2).

## 4. CRWD strays — the "22 other" members explained

- `CRWD/COMOS.PBK`: 6 members, record class byte **0x0F**, payload at offset 20 is a
  **MIDx** MIDI stream (member 0: `4d 49 44 78 00 c0 ...`).
- `CRWD/CROWD.PBK`: 16 members, record = zeros with `-1` at 0x08; bytes from offset 20
  carry no magic (raw codec block, likely the SNDCRD crowd-bed loops — **Hypothesis**,
  not decoded here).
- These 22 members are precisely the old audit's "empties/odd payloads" bucket.

## 5. Confidence summary + next steps for engine-side phrase playback

Confirmed-by-correlation: record offsets 0x00/0x04/0x12 arithmetic (phrase packing,
id bijection, sentinel split), TYPE.PBK mirror identity (1290/1290 byte-exact prefixes),
HDR prefix identity, declaredSize quirk (1232+1022 archives), class-letter tag 0x06
('c'/'a'), codec and loop-tag splits. Hypothesis only: class-enum meanings
(chant/team/player/commentary/crowd-midi), voice = phrase%16 announcer rotation,
0x08/0x0C as queue links, CROWD.PBK raw-payload purpose. Unknown: record 0x06/0x10
usage, the within-phrase id permutation source, PHRS.INF u32@8 = 83,696.

1. Implement the phrase index model: parse `PHRS.HDR` (or the PHRS.PBK TOC) +
   `TYPE.PBK` stubs to get phrase -> (segment count, per-segment offsets/sizes, ids)
   without touching PHRS.PBK audio; fetch segments lazily by
   `(phraseIdx, segTocOrdinal)` and key runtime handles on the u16@0x12 id.
   `BigArchive::parse` must grow a tolerant mode for truncated mirrors (offsets past
   member end) or TYPE entries should be read via a dedicated stub-aware path.
2. In `decodePlayTrack` dispatch, branch on payload magic at offset 20 (`PT`, `MIDx`,
   none) so the two CRWD banks stop being dead members; treat empty nested archives
   (count=0, hdrSize=16) as silent phrases instead of errors.
3. Cross-map the per-phrase id blocks against `COMMON/FC<LANG>.BIN` (`FCLG.V1.7`
   commentary tables, 42,840 B each) to pin what the u16@0x12 ids index — that unlocks
   sentence assembly (SNDsentencestart = an ordered run of segment ids).
4. **PARTIALLY DONE (pass 7, `docs/reverse-engineering/COMMENTARY_WC98.md`)**: the u16@0x12 id is
   consumed by the per-frame pump `FUN_0048f920` (`_DAT_006326bc = *(u16*)(record + 0x12)`, then the
   item start thunk at `**(item + 0x1c)`); full SNDphrase* API ABI + the 46-caller match-event trigger
   seam (`FUN_004893ec`) documented there. Record offsets 0x04/0x08 consumers still open.
5. Correct asset-formats/AUDIO.md + STATUS.md: sidecar counts (1 INF / 1 HDR / 0 TYPE,
   plus TYPE.PBK), drop "cross-member references" wording (stripped mirror file, not
   intra-archive cross-refs), PBP membership, and the declaredSize quirk; extend
   `test_audio` gates with a cheap TYPE/PHRS prefix-equality check so BIGF leniency
   regressions get caught.
