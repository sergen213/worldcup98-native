# DATABASE_FORMAT_NOTES — WC98 DBI/DBM/BIN container framing

All offsets little-endian unless noted. Source of truth: `formats/src/fcdb.cpp` + measured
gates (`tests/unit/test_fcdb.cpp`, `wc98-dbexplorer --validate`).

## DBI framing (FCDB / STR0 / STR1)

```
0x00  magic ("FCDB"/"STR0"/"STR1")
0x04  u32 version == 501 (checked by the game, FUN_004a69ac)
0x08  u32 table offset A   (FCDB: league table; STR0: league+team name table; STR1: player names)
0x0C  u32 table offset B   (FCDB: team table; STR0: team-name table; STR1: same table as A)
0x10  u32 table offset C   (FCDB: player table; STR0: unused)
```

Table (at any offset, `formats/src/fcdb.cpp readTable`):
```
0x00 u32 count
0x04 u32 recordSize   (0 => u32 offset table follows; offsets are RELATIVE to table start)
      else fixed-size records at tableStart + 8 + i*recordSize
```

String tables: variable-size records, NUL-terminated cp1252. STR0 team entries are fixed
fields: `char[6] abbreviation`, `char[11] short name`, then cstring full name.

## FCDB tables (COMMON/FCDB.DBI, 39 980 B)

* League table — variable records, packed u32: id bits 0..11, field12 bits 12..23,
  flags bits 24/25, member count bits 26..31, then `count` u16 LE team ids.
  **id 0 = empty member slot** (single instance: league 217 'CAF' member row 6).
* Team table — 68 B records (17 u32 words), 56 teams. See `TEAM_AND_FORMATION_REFERENCE.md`.
* Player table — 20 B records, 1782 players. See `PLAYER_ATTRIBUTE_REFERENCE.md`.

## Name catalogs

| File | Magic | Entries |
|---|---|---|
| `COMMON/FCDB_<LANG>.DBI` | STR0 | 24 league names + 56 team names (ENG/GER/FRE/ITA/DUT/SWE/SPA/DAN; JAP variant) |
| `COMMON/FCDBP<LANG>.DBI` | STR1 | 1782 player names |
| `COMMON/FC<lang>.BIN` | `FCLG` | UI string bank, NUL-separated, id = index (id 0 is the magic itself) |

## DBM variants (big-endian mirror, NOT consumed by the PC engine)

`FCDB.DBM` (46 220 B), `FCDBP*.DBM`, `FCDB_<LANG>.DBM`: identical magic/version semantics but
every framing dword is byte-swapped (version reads 501 BE, table offsets BE). `FCDB.DBM`
counts: 24 leagues / 56 teams / **2094 players** — a superset pool (platform unverified);
record bytes do not byte-match the LE master pool. Note: Watcom bitfields allocate MSB-first —
measured `FCDB.DBM` team word0 `0x0340000e >> 21` = 26 equals the LE master team0 id
(`0xe000001a & 0x7ff` = 26), so a BE decoder must mirror bit positions within each word
(LE field at `[lo,n]` → BE at `[32-lo-n,n]`), not just swap dword reads. PC engine path
strings load only the `.dbi` set (`common\fcdb.dbi`, `common\fcdb_%s.dbi`,
`common\fcdbpeng.dbi`); DBM files are inventory-documented, deliberately not decoded.

## Validation protocol (currently green)

`wc98-dbexplorer <tree>/COMMON --validate`: name counts 24/56/1782, unique team ids,
strictly-ascending player ids (the game binary-searches them), 1064 roster slots resolve,
byte-22 nibble == floor(squad/2) (22 u16 LE slots at bytes 24..67), league members resolve
(single id-0 member in league 217 'CAF'), all 17 attributes are nibbles, 8-bit shirts
0..22 + 255 sentinel (720).
