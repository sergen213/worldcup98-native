# TEAM_AND_FORMATION_REFERENCE — 68-byte team record + formation mechanics

Decoder: `formats/src/fcdb.cpp` (WC98 branch). Raw 17 u32 words preserved (`t.raw`).

## Team record bit layout (bytes 0..23 decoded; bytes 24..64 roster)

| Byte | Field | Decode |
|---|---|---|
| 0..1 | team id | `((b1&0x07)<<8)\|b0` (11 bits; measured 26..876) |
| 1..3 | string-bank id | `((b1&0xF8)>>3)\|(b2<<5)\|((b3&0x0F)<<13)` (18 bits, FC bank selector) |
| 3..4 | stadium | `b3>>4` |
| 4..11 | home kit | shirt style `b5>>2`, colours from b4..b10 (5-bit colour indices + 2-bit style lanes; exact split in `fcdb.cpp FcdbKit1998`) |
| 12..19 | away kit | same grammar; socks style = `(b19>>4)&3` |
| 17 | strategy | `b17>>5` |
| 21 | tactics | `b21>>2` (6-bit formation id; current teams measured {0,1,3,5,6,7,9,10}) |
| 22..23 | nation + squad size | nationality `((b23&0xF)<<4)\|((b22&0xF0)>>4)`; low nibble b22 = floor(squad/2) (current squads 22 → 11, legends 11 → 5) |
| 24..67 | roster | 22 u16 LE player slots, 0 = empty; 1064 filled across 56 teams (current squads 22, legends 11; byte-22 nibble = floor(squad/2)) |

No club flag, no budget field (FIFA 99's 80 B record has both — schema auto-detect uses
the record size, `FcdbSchema`).

## Leagues

24 variable-size records (see `DATABASE_FORMAT_NOTES.md`). Measured: league 104 "World Cup"
(40 teams), 198 "Classics" (16 legend teams 861..876), then year groups 1930..1978 pairs;
member id 0 = empty slot. `field12`/`flag24/25` are container metadata (unused by UI lanes
decoded so far).

## Formation engine (runtime, not a database file)

* Formation slots 0..16 (`sim::kSlots`): 0 GK, 1 sweeper, 2..6 defence, 7..11 midfield,
  12..16 attack (`simulation/include/wc98/sim/team.hpp`).
* Slot-fill preference table, exe 0x586B74 (17 rows × 11 i32: slot, DB-role masks,
  on-pitch masks) — byte-exact port in `simulation/src/squad.cpp kSlotFill`; fill routines
  `0x49FDC8` (bench→free slots) / `0x49FF44` (on-pitch re-assign). Matching keys on the
  **role byte** (1<<role) written to `PlayerAttr+0x11` — not on record byte 3.
* Active formation record per team: team block +0x16 + i*0x6E, 3 records
  (mentality variants); `Team_initPlayers 0x451210` copies the FE tactics block.
* AISCRIPT formation tables: `GEN4TBLS.VIV` (COMMON) members `G98*.FMT`, column-oriented
  17-attribute delta tables — `formats/src/gen4tbls.hpp`, loaded by
  `Formation_loadTables 0x4CE374`; pitch coordinates come from these tables, not from any
  tactics DB file.

## Default line-ups

The CD ships no line-up file. Match boot consumes the FCDB team record (roster order +
role byte + tactics id) and the slot-fill masks above; the first filled roster slots are
handed to the match by the front end (`sim::TeamSheet`, `makeTeamSheet`). User-edited
line-ups persist to `USER\tmmng.db` — see `TMMNG_FORMAT_NOTES.md`.
