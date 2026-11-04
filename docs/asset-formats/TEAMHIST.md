# Team-history pair (.HIS + .TRI)

Disk: `$WC98_TREE/FE/TEAMHIST.ENG/{du,en,fr,ge,it,da,sp,sw}.{his,tri}` (one directory, 8 language
pairs; plus `ZINTER.RES`/`ZWCUP.RES`, not covered here). Loader: `FUN_00489f58` in FIFAWC.EXE —
evidence in `analysis/pass7/teamhist.txt` (GhidraWc98Tri.py run) and
`docs/reverse-engineering/TOURNAMENT_WC98.md`. Reference implementations: `formats/src/his.cpp`,
`formats/src/tri.cpp`; gated test `tests/unit/test_histtri.cpp`.

## .HIS layout (measured 2026-11-03, all 8 languages identical)

| offset | type | field | measured value |
|--------|------|-------|----------------|
| 0x00 | u32le | magic | `0x30028` (low half = declared index span 40 = 0x28) |
| 0x04 | u16le[] | participant id run, ascending with gaps | 33 ids `{26,29,31,...,88}`, identical in all 8 files; ends where a string-offset subtable begins (first value ≥ 0x100) |
| 0x1e | u16le | `nRows` (loader predicate input) | 49 — coincidentally the tail of the id run |
| 0x20 | u16le | `nIdx` (loader predicate input) | 52 — coincidentally the tail of the id run |
| 0x28 | u16le[nIdx] | index table (`His::index`, raw) | mixed: ids 57..88 continue, then offsets `{599,635,...}`, then alternating offset/zero pairs (u32 string-offsets `{2355,...,16363}`); NOT language-invariant past the id run |
| 0x70 | u32le[nIdx][nRows] | record grid base (`His::cell(row,col)`, row-major) | semantics UNKNOWN; region is **not** byte-identical across languages (localized strings embedded inline in records) |

Loader gate (exact predicate, `FUN_00489f58`): file size > 3 and
`8 + nIdx*2 + nIdx*nRows*4 <= fileSize`. Measured: every shipped `.HIS` is 161.7 KB (SW) ..
173.6 KB (FR); the minimum predicate bound is 10,304 bytes.

The authoritative participant list = `wc98::formats::participants(his)` — the invariant id run at
+0x04 (33 FCDB-space ids). The 52-row container's rows 43..51 are trailing BYE/qualifier slots per
the tournament doc. Localized display names come from the paired `.TRI` pool (+ FE/FCDB string
tables, see `FCDB_WC98.md`).

## .TRI layout (measured 2026-11-03, DU/EN probed; structure identical across languages)

| offset | type | field | measured value |
|--------|------|-------|----------------|
| 0x00 | u32le | record count | 43 (all 8 files) |
| 0x04 | u16le[count] | record-start table, offsets relative to base `B = 0x2c0` | not monotonic (last entry 0x108 sorts mid-range); values overlap because records share string tails |
| 0x5a..0xb0 | — | gap block: 22 x {u32 pair} incl. `{0x32, 0x30}` | opaque |
| 0xb0..0x2c0 | u32le[132] | secondary sequential offset table (opaque corpus offset table; low-16 meaningful per probe, full u32 ascending) | spans the pool window 0..~11967 (DU); not consumed by this parser |
| 0x2c0 (=B) | — | NUL-separated string pool; first token measured at 0x2da | country names (`Engeland`, `Uruguay`, ...) + all trivia strings, cp1252 bytes as stored |

Record assembly: `Tri::record(k)` reads consecutive NUL-terminated fields from `B + tab[k]`
(independent record starts; overlapping tails ⇒ shared strings). Verified DU k=0 =
`"Waar werden de eerste Wereldkampioenschappen gehouden?" / "Uruguay" / "Mexico"` (Q + answer
candidates); EN k=0 = `"Where was the first World Cup held?" / ...`.

## Open items (do NOT invent semantics)

- 52x49 `.HIS` grid cell meaning: raw row-major u32 access only (`His::cell`), no named fields.
- Per-record field count in `.TRI`: data-driven; the bracket loader copies **6** strings per team
  record (0x300-stride slots at `DAT_00631b18...`, truncation caps 255/127 B), so `record(k)` caps
  at 6 and stops at pool EOF. Exact per-record field boundaries = open.
- `.HIS` inter-page record framing (stream API pages in 16-entry blocks, `FUN_004cfaa0`) and the
  inline-string layout inside records = open; assigned to the tournament implementation slice.
