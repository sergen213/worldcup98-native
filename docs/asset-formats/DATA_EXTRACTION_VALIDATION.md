# DATA_EXTRACTION_VALIDATION — extraction layer gates and measured numbers

Tool: `tools/dbexplorer` (`wc98-dbexplorer`). Tests: `tests/unit/test_fcdb.cpp`.

## `--validate` (COMMON/FCDB.DBI + ENG names, measured 2026-11-18)

```
ok   league names 24 == league records 24
ok   team names 56 == team records 56
ok   player names 1782 == player records 1782
ok   team ids unique
ok   player ids strictly ascending (binary-searched by the game)
ok   984 roster references resolve to players (0 unresolved)
ok   team roster-size field matches the roster list (0 mismatches)
ok   WC98 rosters fit the 20 u16 slots (0 outside)
ok   league team references resolve (0 unresolved)          # id 0 = empty member slot
ok   all 17 skill fields are 4-bit nibbles (0 out of range)
ok   jersey numbers within 0..99 + 255 sentinel (0 outside, 720 sentinels)
info World Cup 98 schema: 24 leagues, 56 teams, 1782 players (161 goalkeepers)
```

## Measured constants (byte-level probes, this session unless noted)

| Item | Value |
|---|---|
| `FCDB.DBI` size / header | 39 980 B; magic `FCDB`, version 501, table offsets @8/12/16 |
| Entities | 24 leagues, 56 teams (68 B), 1782 players (20 B) |
| Roster slots filled | 984 (40/56 teams capped at 20) |
| Player ids | strictly ascending, min 34 |
| Role space (b17>>4) | 0..10; 161 goalkeepers |
| Shirt field | 10 bits; in-use 0..22; 255 sentinel × 720 (reserve pool) |
| `starting` byte 3 | even, 82..198, 54 distinct values (editor RTWC semantics; engine slot matching consumes role byte) |
| League padding | exactly 1 id-0 member slot (league 217 row 6) |
| Attribute scale | nibble×4+39 → 39..99 step 4; fitness nibble seeds stamina (`PlayerAttr+0x18`) |
| `FCDB.DBM` (BE mirror) | 46 220 B, version 501 BE, 2094-player superset pool — documented, not decoded |
| UI banks | `FCENG/GER/FRE/ITA/SPA/DUT/SWE/DAN.BIN` all same string count; id 0 = `FCLG` magic |
| JSON export | `--json /tmp/wc98_db.json` = 842 233 B (leagues/teams/players, normalized + raw words + names) |

## Test coverage (`test_fcdb`)

* Synthetic FIFA-99 and WC98 records: bit-field decodes, schema auto-detect (68 vs 80 B),
  throws on schema mismatch; league member list incl. id-0 empty slot (synthetic gate added).
* Original-tree gate: 24/56/1782 counts, name-table sizes, roster resolution, kit style
  ranges, shirt sentinel == 720, attribute nibble ranges, all 9 UI banks parse.
* Suite state: 149/151 green; the two failures (`test_sim_scenarios`, `self_test_gameplay`)
  are the in-flight simulation kick-seam lane — unrelated to extraction, not chased.

## Write-experiment policy

No editor code was run against the installation; no DBI/DBM files were modified. All
inspection is read-only + the throwaway Python probes in `/tmp` (never repo-tracked).
Original tree remains the definitive reference; dumps stay outside source control.

## Cross-check vs fifa98edit

The editor's semantics (attribute names, kit bit layout, `roles[]`, tactics `b21>>2`) were
used as the naming reference; numeric decodes come from our parser + engine-side citations
(PlayerAttr clamp, slot-fill table 0x586B74). Direct output diff against editor JSON is
not applicable — the editor's RTWC record lengths (18 B players / different team offsets)
do not match WC98's 20 B / 68 B records (`FIFA98EDIT_INTEGRATION.md` matrix).
