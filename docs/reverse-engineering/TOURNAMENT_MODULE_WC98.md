# TOURNAMENT module — WC98 (`world-cup98-native`, pass 7b, 2026-11-05)

Implementation-ready spec for group-stage standings, knockout bracket, seeding/pots, schedule
generation, points/tiebreakers. Synthesizes the predecessor's pass-7 dumps (4 runs of
`ghidra-scripts/GhidraWc98Tourn{1..4}.py`, 7 evidence files) plus this session's runs
(`GhidraWc98Tourn5/6/7.py`) and host-side decodes of `FE/TEAMHIST.ENG/ZWCUP.RES`.

Evidence tags: **[Pn]** = predecessor dump (file cited); **[R#]** = this session's Ghidra run
(new files under `analysis/pass7/`); **[H#]** = this session's host-side probe (no Ghidra).
Claims not tagged with a citation are marked **(hyp)**.

## 1. Module map

### Registration / version manifest
- Getter `FUN_004e99e0` returns the pointer slot `DAT_00589708`; banner string
  `$FIFAVER$TOURNAMENTV11-7-9` lives at file raw `0x16a361` = VA `0x56BD61` (DGROUP map:
  raw `0x158600` → VA `0x55a000`). [P1 `tourn_xrefmap.txt` GETTER section] [H-banner dump]
- Registry block `DAT_0x5896c0..0x58977c`: code slots (`FUN_004e6c10/4e6c30/4e7190/4e71a0`) +
  (id, value) descriptor pairs; string pointers `0x589700-08 → 0x56bd20/3c/58`. Per
  GHIDRA_PASS4_SIMULATION.md this manifest is diagnostics-only — the sim loop does not dispatch
  through it. [H-registry dump] (module list of 20 banners in raw `0x162750..0x16a358` includes
  TOURNAMENT V11-7-9; PASS4 §module map)

### Key functions (size, callers)
| Function | Role | Evidence |
|---|---|---|
| `FUN_0048c0bc` @0x48c0bc size 2328, callers FE 0x451adc,0x4672a1,0x4674bd,0x468eb9 | participant/pot loader: reads `.HIS` index+grid, `.TRI` strings, fills bracket name records (stride 0x300 at `DAT_00631b18`) with pot order per mode byte | [P4 `tourn_loader.txt` L340–682 full decompile] |
| `FUN_0048c9ec` @0x48c9ec size 243, callers 0x451b00,0x4672c5,0x4674e1,0x468edd (paired with loader) | tournament-mode selector: writes zwcup struct bytes `DAT_00632415/419/41a/41b` from game mode `DAT_00640fb4`; calls the 3 `.RES` readers | [R1 `tourn_res_loader.txt` L44+] |
| `FUN_0048cae8` size ~250 | opens `fe\teamhist.eng\zwcup.res` / `zinter.res` (PTR array @0x48cae0, index 0/1), allocates buffer, validates size vs formula (§2.5) | [R1] |
| `FUN_0048cbd0` size 133 → `FUN_0048cc58` size 203 | team-id lookup in `.RES` header; pair-index arithmetic (§2.5) | [R1] + [H-formula verify] |
| `FUN_0048be5c` size 276, caller `FUN_0048ace4` | selection-sorts 17 allocated nodes by u16 key at node+0xE into a linked list (fixture ordering pass) | [P4 L274–337] |
| block @0x4859a0–0x485c90 (inside `FUN_00485850`, analysis gap; instructions marked `[nofunc]`) | FE `.LAY` pane-object token parser: compares tokens vs suffix-table strings @0x566608+ (`VPATH,FNAME,OPAQUE,MODE,FRAMES,POINTS,HELP,HILITEDPANE,…`) writing record fields `[ESI+0x3c/0x40/0x18/0x5e/0x54-0x5a/0x70/0x78-0x7a/0x86/0x88/0x84]` | [P4 L43–272 disasm] [H-string dump 0x566608..: suffix fragments] — **`POINTS` here is the standings-table column config of a scoreboard pane, NOT match points** (hyp based on neighbouring `HILITEDPANE/SHAPEID` tokens) |
| `FUN_00441168` size 271, caller 0x441682 | bracket name display: scans `DAT_00631b18` string, if len ≥ 100 splits at last space (`FUN_0047a3e8` field `+0x42>>0x18` = line cap), frames via `FUN_004c31d0/FUN_004788b4` | [P2 `tourn_decomp1.txt`] |
| `FUN_004916b0` size 1027, caller `FUN_00491b00` ← `FUN_004012ac` | **THRASH.DLL loader**: LoadLibrary + 35 `GetProcAddress(_THRASH_*)` into slots `DAT_00632a40..0x632ac0`; magic check `0x33444658` ("XFD3") on returned object | [R3 `tourn_grouptable.txt` chain + disasm] [H-exports 0x56781c–0x567adc: `_THRASH_init … _THRASH_writerect@20`] |
| `FUN_004edb00` / `FUN_004edd00` size ~500 each, caller `FUN_00491ac0` (FreeLibrary wrapper) | save/restore of per-display THRASH API context: 35 dwords at `DAT_0057edc8 + g*0x8C` (g = `DAT_0057edc0 & 0xf`, >7→0, so ≤8 slots) ↔ working set `0x632a40..ac0`. **Not** a standings table — superseded reading | [R2 `tourn_cmp.txt` L44–143] [R3 consumer scan L43+] |
| bracket object graph init/teardown `FUN_0048d378` (init, flag `DAT_0063241c`, list head `DAT_006324b4`) / `FUN_0048d3f4` (teardown); node ctor helper `FUN_0048d084` sets fields +6,+10 = 0xFFFFFFFF sentinels; `FUN_0048d114` builds N nodes self-linking +0xc/+0x10 | knockout bracket screen model = heap node list rooted at `DAT_006324b4`; slot cluster users `FUN_0048d53c..FUN_00492xxx` (see [P4 RECBLOCK] for per-slot function map) | [R2 accessors section] |
| `FUN_0045b0fc` size 657, 19+ callers in FE 0x45a7xx–0x45ae07 (fixture menu region) | writes count word at `DAT_00631b14` (= record base − 4) and touches slot `0x632aac`; per-fixture selection UI | [P4 RECBLOCK] [R2 decompile L362+] |

## 2. Data structures

### 2.1 Bracket name records (`DAT_00631b18`, stride 0x300)
Proven from the `FUN_0048c0bc` string-copy slices [P4 L579–665]: a concatenated `.TRI` record is
split at NUL into 5 fields (`local_b4[0..5]` boundary table, built in the loop `iVar10=4..0x18`,
[P4 L557–572]) and copied with caps:

| Record offset | Slot VA base | Cap | Content (field order as split) |
|---|---|---|---|
| +0x000 | `DAT_00631b18` | 255 B | full display name |
| +0x100 | `DAT_00631c18` | 127 B | short name |
| +0x180 | `DAT_00631c98` | 127 B | field 3 (group/label per prior doc) |
| +0x200 | `DAT_00631d18` | 127 B | field 4 |
| +0x280 | `DAT_00631d98` | 127 B | field 5 |

- Record index for slot k = `local_9c[k+3]` (the shuffled/rotated pot-order array, §2.3).
- Count word: u32 at `DAT_00631b14`. [P4 RECBLOCK: `FUN_0045b0fc 0x45b1ed->0x631b14`]
- Consumers: name renderer `FUN_00441168` (all five slots + framing) and loader writes only;
  no simulation code references these slots [P2/P4 xref sets] ⇒ the FE bracket screen is a pure
  consumer of loader output.

### 2.2 Mode bytes
- **`DAT_0057e92c` (draw mode)**: read-only sites 0x48c27f, 0x48c5fd inside `FUN_0048c0bc`;
  full-image reference scan shows **no writer** [P1 GLOB section; R2 "writer hunt" L7285–7288] ⇒
  always 0 at runtime; values 1/2 are unreachable legacy branches. Wine oracle: =0 at FE idle.
  Semantics from decompile slices [P4 L450–469, 515–553]:
  - `0` = shuffle: Fisher–Yates over the 3-entry order array `local_9c[3..5]` using RNG
    `FUN_004c321c % N`, plus per-block in-record shuffle when a name id matches.
  - `1` = deterministic cycling through counter `DAT_0057e934` mod block count; pot slot rotated
    (`local_9c[3]=1`).
  - `2` = same via counter `DAT_0057e930`; `local_9c[3]=0`.
  Counter high word = block quotient (CONCAT22(quotient, remainder) selects the k-th string in a
  block) [P4 L537–553].
- **`DAT_00640fb4` (game/competition mode)**: written by FE dispatcher `FUN_00445664`
  (param_2 = menu item): item 3→2, 4→0, 5→10, 6→0xC, item 2→high half of `FUN_004461b4` result
  [R2 L2670+]. FE flow handler table switch `FUN_00442b4c`: {0,0xB}→entry `&DAT_0x442600`,
  9→`0x442618`, 10→`0x44260C`, 0xC→`0x442630`, default→`0x442624` [R2]. Effects in the loader:
  `==9` triggers id remap through table @0x48bf6e (16 u32 entries; **all zero in the image** ⇒
  no-op, dead patch area) [P1 TABLE hex]; effects in `FUN_0048c9ec`: modes {3,4,5,6,8} set
  flag byte `DAT_0063241B=1`, mode 9 zeroes struct + sets `0xFF` sentinel [R1].

### 2.3 Pot / seeding sources (loader-internal)
- 3×u32 pot structure `local_9c[10]`: elements [0..2] = record offsets read from the `.HIS` grid
  header row, [6..8] = per-pot sizes (`(char*)local_74 - iVar4`), [3..5] = order array init
  {0,1,2} then permuted by mode (§2.2) — all slices in [P4 L434–469].
- 3 requested team ids arrive via `param`; their `.HIS` record indices resolved by paging scan
  over the 16-entry index pages (`local_d4[16]`, reload when `(uVar7 & 0xf)==0`) [P4 L408–433].
- Const arrays in AUTO referenced by the loader path: remap table @0x48bf6e = zero-filled 344 B
  [P1 hex]; linked-list template `DAT_0048a770` (17 dwords, values 0x70100030+ — pointers into
  uninitialized arena, not seed data) [H-dat dump]. **No static seed/pot table in AUTO**; seeding
  = `.HIS` index order + `ZWCUP.RES` id section (§2.5).

### 2.4 Group standings storage — current state of knowledge
- The candidate block (`DAT_0057edc8`, 35 dwords × ≤8, load/store pair) is the **THRASH video
  context**, not points [R2/R3 above] — prior suspicion closed as negative.
- No per-group P/W/D/L/GF/GA array has been located in FIFAWC.EXE .bss by any pass-7 xref sweep
  (record-block consumer census [P4 RECBLOCK, R3 scan]). Group-stage results in the FE are read
  from `.HIS` history cells and displayed via `wcstand1`/`lstand` screens; the binary's own live
  standings accumulation lives in the IG/sim engine region (GHIDRA_PASS4 scope), not in the FE.
- For the port: implement standings computation in-app (§4); inputs = fixtures + results,
  tiebreak order configurable via §3 vocabulary.

### 2.5 `ZWCUP.RES` / `ZINTER.RES` pair matrix (`FE\TEAMHIST.ENG\`, both exactly 2422 B) [H-res]
- Layout: `[u16 n=40][n × u16 ids][C(n,2)=780 triples of u8]`.
  Loader `FUN_0048cae8` validates `filesize ≥ 2 + 2n + (3·(n−1)·n)/2 + 2n+… ` — host check:
  2+80+2340 = 2422 exact ✓. [R1 decompile; H]
- **Pair index formula** from `FUN_0048cc58` (verified bijection over all C(40,2) pairs on the
  host): for a<b: `byteOff = 2 + 2n + 3·(((2n − a − 3)·a)/2 + b − 1)`; lookup swaps team args
  and byte0↔byte2 when order flips (directional access). [R1] [H-verify "bijection OK"]
- Id section = 33 FCDB ids in {26..88} interleaved with **7 placeholder values ≥256**
  (599,760,704,796,635,815,752) — qualifier/BYE slots, matching ".HIS rows 43..51 BYE" note in
  TEAMHIST.md. [H-res; prior doc TOURNAMENT_WC98.md §disk survey]
- Triple semantics (hyp): `(goalsA, venue/neutral flag, goalsB)` head-to-head history —
  histogram byte0∈{0,1,2,3,5}, byte1∈{0..3}, byte2∈{0..3}; 131 non-zero pairs; degrees 1..18 per
  team (qualifiers + finals history, not a single-tournament schedule). [H-res decode]
- ZINTER.RES: identical structure/ids, pair bytes differ from offset 0x52 — same format for the
  intercontinental mode. [H-cmp first diff char 83]

### 2.6 Knockout advancement writes
Bracket model = linked node heap at list head `DAT_006324b4` (init flag `DAT_0063241c`; fields
+0xc/+0x10 self links; team slots +6/+10 initialized 0xFFFFFFFF by `FUN_0048d084`) [R2].
Per-slot write census for the node/bracket block (`DAT_006324b4..0x6327xx`, from [P4 RECBLOCK] +
[P3 blockrefs]): accessor pairs `FUN_0048d0c0/d1f8/f6a4/f920/fcc0` (set node fields), list walkers
`FUN_0048da50/dcd8/df18/dfc4/e084`, screen refreshers `FUN_0048e300/e6d0/e78c/e84c`. Advancement =
node team-slot fill by the fixture chain in 0x48f6xx–0x492xxx (per-fixture menu writers of
`FUN_0045b0fc` family, FE region 0x45a7xx). Exact per-round propagation not decompiled — see §5.

### 2.7 Schedule generation order **[DECODED pass 12, `analysis/pass12tourn/SCHEDULE_GEN.md`]**
- Generator `FUN_0048c0bc` (mode = `DAT_0057e92c`): mode 0 = RNG LCG
  (`FUN_004c321c`) Fisher–Yates draw over the 3 seed slots; mode 1/2 = global
  counters `DAT_0057e934` / `DAT_0057e930` cycling `% per-block count`, mode 2
  carries the quotient into the pick's high u16. Per-round loop runs exactly 3
  blocks (exit via `FUN_004cfa70`). FE builder `FUN_0048ace4` registers 20 pool
  fixtures + per-pool 5-slot blocks from grid stride-7 tables
  (`DAT_0063172f..734 * DAT_0057e928`) gated by venue table `DAT_0057e69c`
  (stride 10/pool), then sorts **17** items by u16 key node+0xE
  (`FUN_0048be5c`, selection sort, tie-break = first unused).
- Fixture adder `FUN_0048afb8` writes record stride 0x20 @`DAT_006307dc`:
  type@+4, date ticks @+8/+0xc from bases `DAT_0057e614/618[type*8]` plus
  pool-count venue bands (`DAT_0057e7c8` x1 /x2 /+half), venue-pattern pair
  table `DAT_0057e6d8[pool*8 + tourn*0x28]`, flag byte record+0x18 |= 1, grid
  bookkeeping at `DAT_00631762` (byte, stride pool*6 + tourn*0x66) and
  `DAT_00631766` (u16 date, tourn*0x33 + pool*3).
- BracketRecord field copies prove the layout: full cap 0xFF @+0x000, shortname
  cap 0x7F @+0x100, label3/4/5 cap 0x7F @+0x180/+0x200/+0x280 (was hyp §4).

## 3. String inventory → flow states
FE screen/layout names (0x564dd0..0x564edc, [P1 `tourn_strings.txt` L283–319]):
`worldcup`, `wcqualif` (qualification list), `wcztsel` (team select), `wcshedq`/`wcshed1`
(schedule), `wcstand1` (standings table), `wcplayof` (knockout bracket), `wcleader`, `wccontr`,
`wcstad`; league variants `lstand`,`lresults`,`lleagsel`; generic flow `mainmenu`,`matchsel`.
These are the UI states for §4 screens.

Tournament keyword registry: slots `DAT_0x5943d8 + i*4` → strings @0x56d6a0.. (full table in
[P7b `tourn_block_dump.txt`] L55+ and [P1b `tourn_strings2.txt`]); **no static xrefs** to the
slots ⇒ dynamic-index keyword table. Vocabulary groups:
- types: TOURNAMENT, PLAYOFF, ELIMINATION, LEAGUE, FRIENDLY, PRACTICE, PKSHOOTOUT, PLAYOFF_FINAL, BEST, TRANSITION
- tiebreakers (enumerated order): TEAM_RANK1..5, MOST_GOALS(_HOME/_AWAY/_TOTAL), MOST_GOAL_DIFF,
  MOST_WINS, MOST_WINS_AWAY, MOST_GOALS_ENCOUNTERS, USER_TEAM, CO_EFFECIANT,
  EXTRA_TIME_PENALYKICKS, AGGREGATE_EXTRATIME_PENALTYKICKS, PENALTY_KICKS (slot order = enum index)
- structure: USER_SELECT, POOL_SIZE, QUALIFICATION_LIST, KNOCKOUT; venue patterns HOME_AWAY /
  HOME_HOME / HOME_AWAY_HOME / DOUBLE_* / QUAD_HOME_AWAY / NO_GROUP_PREFERENCE; schedule source
  FIXED vs GENERATED; rounds NO_ROUND, QUARTER_FINAL1..4, SEMI_FINAL1/2, THIRDROUND_PLAYOFF, FINAL
- tree sizes TREE4/8/16/32/64/128; STAGE1..20 (STAGES_ENUMERATED); GROUP1..39; TMSL1..18;
  calendar SUN..SAT, JAN..DEC, MORN/AFT/EVE/LATE (schedule dates).
Third-place match token = `THIRDROUND_PLAYOFF` @0x56d974 [P1b].

## 4. Implementation checklist (app slice) + struct sketch
Per-screen fields:
- Groups table (`wcstand1`): per group → 4 team slots {id, name(full/short)}, P/W/D/L/GF/GA/GD/Pts;
  sources: fixtures from §2.7 order, results from sim; names via `DAT_00631b18` record semantics.
- Bracket (`wcplayof`): slots = node list (§2.6) {teamA, teamB → sentinel −1}, winner propagation
  to parent slot; round labels QUARTER_FINAL1..4/SEMI_FINAL1/2/FINAL + THIRDROUND_PLAYOFF.
- Schedule (`wcshed1/q`): per fixture {round enum, venue pattern (HOME_AWAY…), date (day/month/
  time-slot enums), teams}.
- Team select (`wcztsel`): pool = ZWCUP id section (§2.5) minus placeholders; names from `.TRI`.

```cpp
// 1:1 with observed layout — bracket name record, stride 0x300 (VA DAT_00631b18 + k*0x300)
struct BracketRecord {           // §2.1 [P4]
    char full[256];              // +0x000 cap255
    uint8_t hdr[0x100-4];        // numeric header region (count word lives at base-4)
    char shortname[128];         // +0x100
    char label3[128];            // +0x180
    char label4[128];            // +0x200
    char label5[128];            // +0x280  (fields proven by pass-12 copy loops, §2.7)
};
struct ZwcupRes {                // §2.5 [R1][H]
    uint16_t pool_size;          // 40
    uint16_t ids[40];            // FCDB ids + ≥256 placeholders
    struct PairRec { uint8_t goals_a, venue_flag, goals_b; } pairs[780];
};                               // pair i<a<b at 3*((2n-a-3)*a/2 + b -1)
struct StandingsRow {            // app-side (binary block NOT located, §2.4)
    uint16_t team_id; int8_t p,w,d,l,gf,ga; int16_t gd,pts;
};                              // pts: win=3/draw=1 = hyp (§5) — config surface: enum §3
```

## 5. Open / unknown (top items for the app slice)
1. **Points values**: no `win=3` constant located in FE code; `POINTS` token is pane layout
   [P4 disasm + H-strings]. Candidate sites = IG/sim module (PASS4 regions). Mark win=3/draw=1 as
   **(hyp)** (FIFA convention 1998) until the comparator is found.
2. **Tiebreaker evaluation order**: enum vocabulary exists (§3 slot order) but no comparator code
   site proven; MOST_GOALS_ENCOUNTERS implies head-to-head mini-table — implement configurable
   chain, verify vs wine oracle screenshots of `wcstand1`.
3. **64-match schedule generation order** incl. third-place — **[CLOSED pass 12]**
   `FUN_0048c0bc` mode generator (RNG draw / counter cycles w/ carry), FE builder
   `FUN_0048ace4`, fixture adder `FUN_0048afb8` record stride 0x20, venue tables + grid
   bookkeeping: see §2.7 and `analysis/pass12tourn/SCHEDULE_GEN.md`. Knockout slot
   assignment (fixture chain 0x45a7xx UI writers) remains for per-round propagation (§5.5).
4. **ZWCUP triple semantics — [CLOSED pass 12, `analysis/pass12tourn/ZWCUP_TRIPLE.md`]**:
   (goals, venue-flag, goals) validated on Brazil–Scotland (2-1, '82/'98 identical line) and
   Italy–Chile (1-1); data = finals H2H history snapshot ~pre-'98 (all 1998 group fixtures are
   zero-recorded), ids map via FCDB.DBI + FCDB_ENG.DBI cross-index.
5. **Knockout advancement writes**: node graph identified (§2.6) but per-round winner propagation
   function chain (0x48f6xx–0x490xxx slot accessors) not individually decompiled; bracket record
   numeric fields between +0x1C..+0xFF unproven.
6. Mode 9 (`DAT_00640fb4==9`) exact FE entry (menu item 2 dynamic path via `FUN_004461b4`);
   ZINTER.RES consumer confirmed only by PTR array adjacency [R1].
