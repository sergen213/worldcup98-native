# FCDB (team/player database) — WC98 schema

Sources: our probe results (`docs/asset-formats/REUSABILITY.md` §5),
fifa99-native's FCDB parser, and `analysis/fifa98edit/` (MIT, cited:
`https://github.com/ma-akad/fifa98edit`) which parses the family's
`.DBI` layout at runtime. Status: **structure confirmed by probe**,
**field semantics partially confirmed**, WC98-specific field meanings
pending `wc98-dbexplorer` output review.

## File family (from inventory + probes)

| File | Role |
|---|---|
| `COMMON/FCDB.DBI` | master DB (language-selected at runtime) |
| `COMMON/FCDB_<LANG>.DBI` (ENG/FR/GER/IT/SP + JAP variant) | per-language name STR0/STR1 tables (`FCDB_ENG`-style sections) |
| `COMMON/FCDBP<LANG>.DBM` | per-language player-name message catalog (STR0/STR1) |

JAP `.DBM` is byte-swapped (68k-artifact, inventory §9) — skip.

## Layout (self-describing, confirmed by fifa98edit + probes)

Sequence of sections, each preceded by count/length u32 fields:

1. `FCDB` magic header block.
2. Leagues section: `nLeagues` u32, record size, per-league: `id` u16,
   `size` u16, member-team id list (u16 each). **Probe: 24 leagues.**
3. Teams section: `nTeams` u32, `teamRecordLen` u32 (self-describing),
   then `nTeams` records. **Probe: 56 teams; WC98 record = 68 B
   (17 u32 words) vs FIFA 99's 80 B (20 words); FIFA 99's budget word
   absent in 1998 build; roster index base 24 (FIFA 99: 28).**
4. Players section: `nPlayers` u32, `playerRecordLen` u32, records.
   **Probe: 1,782 players; 20-byte record, bit-packed, layout identical
   to FIFA 99's decoded record.**

Attribute set (fifa98edit column model for the family; 1998 values in
range 39..99 step 4): aggression, acceleration, attack bias, agility,
ball control, awareness, fitness, creativity, passing, heading,
reaction, pass bias, shot power, shot bias, speed, shot accuracy,
tackle (+ jersey index, + computed average). Exact 1998 bit offsets =
fifa99-native record (verified identical for FIFA 99; WC98 record size
identical 20 B → fields CONFIRMED-aligned, semantics spot-checked via
`wc98-dbexplorer` readable dump: 56 team names + 3 player samples).

## Kits, tactics, related data (RTWC98 family reference — hypothesis for WC98)

- Kit artwork: `JERSnn.FSH` per-team strips (fifa98edit, RTWC98);
  WC98 kits live in `INGAME/PLAYER/TEXTURES` (SHPI/GIMX observed);
  per-team kit color indices: the 68 B team record's 17 words include
  kit color fields (meanings pending dump review).
- Tactics DB (`TMMNG.DB` in RTWC98): WC98 user-edit tactics file names
  string-located in the exe (SAVES.md open item).

## Verification protocol (done when true)

- `wc98-dbexplorer` prints 24/56/1782 + readable EN names for all 56
  teams (gate from port task).
- Spot-compare 1998 squads vs `docs/research/TEAMS_1998.md` (e.g.
  France 98: Zidane, Petit, Dugarby, Leboeuf...) — squad content is
  data, not distribution: names only, user-supplied file at runtime.
- Cross-check attribute ranges (39..99 step 4) on 100 sampled players.
