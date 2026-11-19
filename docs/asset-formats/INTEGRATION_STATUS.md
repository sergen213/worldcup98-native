# FCDB / recovered-data integration status (pass-47)

Living matrix of which game systems consume the recovered database and which still
carry invented data. Every lane below is measured against the current tree; the
follow-up goal (`local://paste-1.md`) requires every row to read the same DB that
the match plays with. Audit dates: 2026-11-18, HEAD 626755b.

## A. Wired: recovered data drives gameplay/UI

| System | Consumer lane | Recovered source | Provenance |
|---|---|---|---|
| Single game database | `simulation/data.hpp` `GameData`/`loadGameData` (FCDB.DBI + FCDB_<LANG>.DBI + FCDBP<LANG>.DBI, provenance paths recorded once); `MatchBoot.game`, `FeFlow.game_` | COMMON/FCDB.DBI + localized name tables | pass-47 |
| Match team sheet (lineup + squad) | `simulation/src/data.cpp` `makeTeamSheet` -> `MatchData.sheets[0..1]`; `app/livematch.cpp` boot builds match players from `sheets[t].squad` | `COMMON/FCDB.DBI` 68 B team record, 22 u16 roster slots bytes 24..67 | FCDB_WC98.md, pass-44forensic (1064/1064 ids resolve) |
| Player attributes in sim | `Team_buildMatchPlayerAttrs` 0x478B08 over `sheets[t].squad` nibbles; `test_sim_players` gates | FCDB player record bytes 8..16 (17 skill nibbles), n*4+39 scale | PLAYER_ATTRIBUTE_REFERENCE.md |
| Tactics / formation style | `team.cpp` `Team_dbByte6f` = team record byte 21 >> 2; `Team_buildTacticsBlock` feeds GEN4TBLS descriptors | FCDB b212 tactics index | TEAM_AND_FORMATION_REFERENCE.md, pass-46 (no embedded default tactics; source = FCDB record) |
| Formation tables | `MatchData.formations` GEN4TBLS.T via `loadFormationTables` | GEN4TBLS.T/VIV G98PL | test_gen4tbls |
| FE team-select pool | `fe_flow.cpp` `loadTeams`: ZWCUP.RES ids + `GameData::teamIndexForId` + `fullName` | FE/TEAMHIST.ENG/ZWCUP.RES, COMMON/FCDB_LANG.DBI | TEAMHIST.md |
| FE squad/league rows | `FeFlow.game_` + `playerName(index)` (FCDBP_LANG.DBI STR1 parallel table) | COMMON/FCDB.DBI, COMMON/FCDBP_LANG.DBI | DATA_EXTRACTION_VALIDATION.md |
| Match HUD strip + IG board captions | `MatchBoot.abbrv/names` from `GameData` (injected at `livematch.cpp:1292/1678`) | FCDB_<LANG>.DBI STR0 rows 1:1 with team order | pass-7 IG audit |
| IG lineup/substitution/kit rows | `livematch.cpp` `buildIgMgmt` rows from `boot.game` (DB record index -> STR1 name) | FCDBPENG.DBI + FCDB shirt byte 16..17 (8-bit shirt incl. 255 sentinel) | pass-44 squad/shirt histogram |
| Kit atlases | `presentation/src/kits.cpp`: PK###H/A by team id, A-fallback-H for legend teams 861..876, GK/RK/COM overlays | INGAME/PLAYER/TEXTURES on-disk set (56 H + 41 A measured) | KIT_ASSET_REFERENCE.md |
| Kit swatch colours (FE rows / kt picker) | `FcdbKit1998` home/away decoded fields painted by `paintRowLabel` | FCDB team bytes 4..19 | pass-47 round trip: decode/encode symmetric for all 56 teams |
| Appearance ids (hair/skin/beard/face) | `kits.cpp:106` look tuple -> match_scene rig decals (`lookByKey` shirt+skin) | FCDB 1998-only look fields | test_fcdb original() |
| Team/player counts gates | `test_fcdb` original() + roundtrip(); `wc98-dbexplorer --validate/--roundtrip` | FCDB.DBI 39980 B | 24/56/1782, 1064 roster slots; byte-exact re-encode |
| Runtime-vs-extraction identity | `tools/gamecheck` (GameData view) vs `wc98-dbexplorer --json` via `tests/tools/identity_check.py` | both lanes from same tree files | gates `game_data_valid`, `game_data_identity` |
| Language reload | `fe_screen.cpp:195` FCLG string banks per language | COMMON/FC<LANG>.BIN | pass-7 grid audit |

## B. Invented data still in the runtime (to be replaced/relocated)

| Item | Current lane | Original source status |
|---|---|---|
| World Cup finals history (1930-1998 table) | `fe_flow.hpp kWcFinals` hard-coded names/scores; drawn by FEFLOW81 history screen | No shipped asset located; candidates: exe string banks or FEFLOW81 handler constants. NOT decoded - open RE slice. |
| FE default formation descriptors | `kFeDesc[style]` byte blobs in `team.cpp` (0x592C4C/0x592CB5/0x592CBC family) | FIFA-99-era shape; WC98 table bytes not dumped from DGROUP yet (D-37 lane). |
| TMMNG saved lineups | absent: USER/tmmng.db does not exist on the CD (loader slot installs empty registry pool, pass-46) | Runtime-generated file; port starts from FCDB roster order + tactics b212 defaults. |
| Kit-name field byte 19 | preserved raw, semantics unknown (PLAYER_ATTRIBUTE_REFERENCE) | do not reinterpret. |
| Keeper kit selection table | hypothesis lane `match_scene.cpp:469` | original selection table untraced - open. |
| Referee kit | PK999B..E measured | officials grammar done; selection untraced. |
| FCDB.DBM big-endian mirror | not consumed (PC loads .dbi only) | documented FCDB_WC98.md line 33; not decoded. |
| Group stage assignments | tournament driver picks pool order; comments only in `schedule.cpp` | ZWCUP pair history consumed; group seeding source untraced - open. |

## C. Gate inventory (dummy lane)

- `test_fcdb`: synthetic FIFA99/WC98 + original() + originalRoundtrip() (39980 B byte-exact; 56 team + 1782 player bit symmetry).
- `db_json_valid`, `db_roundtrip` (wc98-dbexplorer --json parseability / --roundtrip byte-exactness), `dbexplorer_usage` (usage/error exit-code lane).
- `game_data_valid` (wc98-gamecheck: 56/1782/1064 counts, kit file existence per team, provenance paths), `game_data_identity` (runtime GameData view vs extraction lane — teams and players ids/names/rosters).
- `test_sim_players` attr arithmetic + fatigue ladder; `test_sim_team` formation/mentality.
- `fe_flow_render` / IG capture gates present in tests/CMakeLists.txt.
