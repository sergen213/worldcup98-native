# Team-history pair (.HIS + .TRI)

Disk: `$WC98_TREE/FE/TEAMHIST.ENG/{du,en,fr,ge,it,da,sp,sw}.{his,tri}` (one directory, 8 language
pairs; plus `ZINTER.RES`/`ZWCUP.RES`, not covered here). Loader: `FUN_00489f58` in FIFAWC.EXE —
evidence in `analysis/pass7/teamhist.txt` (GhidraWc98Tri.py run) and
`docs/reverse-engineering/TOURNAMENT_WC98.md`. Reference implementations: `formats/src/his.cpp`,
`formats/src/tri.cpp`; gated test `tests/unit/test_histtri.cpp`.

## .HIS layout (re-decoded 2026-11-17 pass-20ui, all 8 languages; supersedes the 2026-11-03
"52x49 grid" view, which was a header misread: the u16 fields at 0x1e/0x20 are ids[13..14] of
the id run — `...47,49,52...` — not counts; the cited loader predicate was derived from those
fabricated counts)

| offset | type | field | measured value |
|--------|------|-------|----------------|
| 0x00 | u16le | `nTeams` | 40 (all 8 files) |
| 0x02 | u16le | `nCols` | 3 (all 8 files) |
| 0x04 | u16le[nTeams] | team id run, ascending, FCDB id space | `{26,29,31,...,88}` national teams + club ids `{752,760,796,815}`; language-invariant |
| 0x04+2*nTeams | u32le[nTeams*nCols] | cell offsets, strictly ascending flat | first cell at 568 (DU), last cell runs to EOF |

Cell text = `[offsets[k], offsets[k+1] or EOF)`, cp1252. Per team: column 0 = greats (caps/goals
list), 1 = gen (association blurb), 2 = triv (trivia, multi-section). Every cell starts with
`[multitext]` (gated over all 8x120 cells in `test_histtri`); token grammar in
`docs/asset-formats/MULTITEXT.md` (parser `formats/src/multitext.cpp`). Ireland (id 31) = team
row 2: gen "The Football Association...", greats include Niall Quinn, trivia = 8 sections.

Loader gate (`FUN_00489f58`): file size > 4 and the offset table fits
(`4 + 2*nTeams + 4*nTeams*nCols <= fileSize`).

The authoritative team list = `His::teamIds` (40 FCDB-space ids, identical in all 8 files; the
same 40 as the `ZWCUP.RES`/`ZINTER.RES` head-to-head grids, parser `formats/src/res.cpp`,
grid semantics in `docs/reverse-engineering/TOURNAMENT_WC98.md`). Localized display names come
from FE/FCDB string tables (`FCDB_WC98.md`); the `.TRI` pool carries the trivia name/question
strings.

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

- Per-record field count in `.TRI`: data-driven; the bracket loader copies **6** strings per team
  record (0x300-stride slots at `DAT_00631b18...`, truncation caps 255/127 B), so `record(k)` caps
  at 6 and stops at pool EOF. Exact per-record field boundaries = open.
- Multitext layout semantics: tab-stop coordinate space (widget-relative vs absolute), `{centre}`
  anchor, and section->widget binding on feflow31/34/88 = confirmed at handler level
  (`analysis/pass20records/NOTES.md`), painted by `presentation::paintMultitext`.
