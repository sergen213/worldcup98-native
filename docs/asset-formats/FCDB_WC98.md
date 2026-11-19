# FCDB (team/player database) — WC98 schema

Sources: our probe results (`docs/asset-formats/REUSABILITY.md` §5),
fifa99-native's FCDB parser, and `analysis/fifa98edit/` (MIT, cited:
`https://github.com/ma-akad/fifa98edit` — see `FIFA98EDIT_INTEGRATION.md` for the audit)
which parses the family's `.DBI` layout at runtime.
**Status: fully decoded and gated** (2026-11-18) — `wc98-dbexplorer --validate` all ok,
`tests/unit/test_fcdb.cpp` green. Field-by-field maps live in:

* `DATABASE_FORMAT_NOTES.md` — container framing, name catalogs, DBM variants
* `PLAYER_ATTRIBUTE_REFERENCE.md` — 20-byte player record + attribute scale
* `TEAM_AND_FORMATION_REFERENCE.md` — 68-byte team record + formation engine
* `KIT_ASSET_REFERENCE.md` — kit textures / palette lane
* `TMMNG_FORMAT_NOTES.md` — user tactics file (absent from CD, engine-registered)
* `DATA_EXTRACTION_VALIDATION.md` — measured constants + gate transcript

## File family (from inventory + probes)

| File | Role |
|---|---|
| `COMMON/FCDB.DBI` | master DB (language-selected at runtime) |
| `COMMON/FCDB_<LANG>.DBI` | per-language league+team name tables (STR0; ENG/DUT/DUT… + JAP) |
| `COMMON/FCDBP<LANG>.DBI` | per-language player-name catalog (STR1) |
| `COMMON/FC<lang>.BIN` | `FCLG` UI string bank (id = index) |
| `COMMON/FCDB*.DBM`, `FCDB_*.DBM` | **big-endian mirror** (Mac 68k companion): framing BE, `FCDB.DBM` pool = 2094 players (superset); PC engine loads only `.dbi` — DBM documented, not decoded |

## Layout (self-describing)

`FCDB` magic + version 501, three table offsets (league/team/player). Variable-size league
records (id 11-bit packed + u16 LE member ids, **member id 0 = empty slot**), 68 B team
records (kit/bank/stadium/tactics/nation bits + 20 u16 LE roster slots at byte 24, 0 = empty),
20 B player records (17 nibble attributes × `n*4+39`, appearance bytes 6..7, shirt 10-bit +
**255 = 'no number' sentinel** (720 reserve players), role 0..10 = engine position-code space).

## Open items (now closed)

* ~~field meanings pending dbexplorer review~~ — decodes verified: kit colours via
  `kitColor`/hslCols palette lane (MENU_NAV §288), tactics `b21>>2` consumed by formation
  module, fitness nibble → stamina (`PlayerAttr+0x18`).
* Kit artwork: fifa98edit's `JERSnn.FSH` lane is **RTWC-only and absent** here; WC98 kits are
  `PK<teamid>{A,H}.FSH` + `GK/RK/COM/HHIG/PALJER` — `KIT_ASSET_REFERENCE.md`.
* Tactics DB: `USER\tmmng.db` engine-registered (registrar call at 0x46dfba), no CD content —
  `TMMNG_FORMAT_NOTES.md`.
* `starting` byte 3 (editor RTWC field): even 82..198, 54 distinct; recorded raw, no engine
  consumer found (slot matching uses the role byte).

## Verification protocol (DONE 2026-11-18)

- `wc98-dbexplorer` prints 24/56/1782 + readable EN names for all 56 teams — green.
- Squad content vs 1998 rosters spot-checked through team-select gates (`test_teamselect`).
- Attribute ranges (39..99 step 4) — `test_fcdb` original() gate, 1782/1782.
