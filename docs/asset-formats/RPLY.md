# RPLY — WC98 replay container (`user\rply%04d.dat` + `user\rplyindx.dat`)

Status: **CONFIRMED** via Ghidra pass 8 (2026-11-06). Evidence dumps:
`analysis/pass8/rply_decomp_lib.txt`, `rply_decomp_rec.txt`,
`rply_decomp_region.txt`, xref scripts `rply_xref{1,5,7}.py`.
Codec: `formats/include/wc98/formats/rply.hpp` + `formats/src/rply.cpp`
(gate `tests/unit/test_rply.cpp`).

## String/symbol evidence (CONFIRMED)

- `user\rplyindx.dat` string VA 0x1604A4, `user\rply%04d.dat` VA 0x1604B8;
  symbols `FEREPLAYBUFF` 0x1604D4, `replayfile` 0x1604E4.
- In-memory record chain globals: buffer base `DAT_0057f83c`, cursor
  `DAT_0063699c`, end `DAT_00636994`, count+1 `DAT_0057f840`
  (walked by `FUN_00496110`).

## .dat container layout (CONFIRMED)

Little-endian, Watcom style. Offsets from file start:

| Offset | Size | Field | Evidence |
| --- | --- | --- | --- |
| +0x00 | u32 | record count | walk loop `FUN_00496110` (count = `DAT_0057f840 - idx`) |
| +0x04 | 142 × u32 | state table (match/session snapshot dwords) | `FUN_00495d10` loop `iVar2 = 0x8e`; writer `FUN_00495e60` iterates the same 0x8e dwords from the header base |
| +0x23c (572) | ... | record area: sequence of size-prefixed records | `FUN_00495e60`: `unaff_EBX < local_1c + local_18 + 0x23cU` bounds the table→records split (`rply_decomp_lib.txt:170`) |

Record framing (CONFIRMED, `FUN_00496110`/`FUN_004963b0`): each record is a
**big-endian u16 total stride** (size includes the 2-byte prefix itself)
followed by `stride - 2` payload bytes. The walk advances by
`(uint)*pbVar1 * 0x100 + (uint)pbVar1[1]`, i.e. BE16 self-size, and wraps to
the base when it passes the end sentinel — a classic size-prefixed stream with
no magic, no CRC.

Record payload content is not enumerated statically: the exe streams raw
per-tick controller frames recorded during play (input-stream replay model;
netplay in this family = synchronized input streams). No seed/magic bytes
exist inside payloads; the header table carries match state.

## rplyindx.dat (CONFIRMED fixed table)

Exactly `100 × 25` bytes (`FUN_0043c4f0` browser scan: entry stride 0x19, slot
number = byte at entry+4 fed to the `rply%04d` sprintf, display name string
base at entry+5). Entry layout as implemented/parsed:

| Offset | Size | Field |
| --- | --- | --- |
| +0x00 | u32 | status/flags (browser field 0) |
| +0x04 | u8 | slot number (`*(entry+4)` → filename suffix) |
| +0x05 | ≤20 | NUL-terminated display name (filename) |

Unused slots are zero. The file is rewritten by the browser whenever a replay
is stored; it is not a stream, so its size never changes.

## Native dialect (this port — `app/replay.cpp`, `wc98-replay`)

Our writer mirrors every byte constant above and appends one documented
dialect: record payloads = `u32 LE tick index` + the raw
`sim::ControllerState` word fed to the simulation at that tick. Header table
dialect fields: `[0]` seed, `[1]` tick count, `[2]` dialect version 1,
`[3]/[4]` final world state hash (low/high). Playback re-drives `SimRunner`
from the recorded seed + controller stream; determinism gate = final
`stateHash` equality (`ctest replay_record_play`, seed-pinned scenario
0x2E7A, 400 ticks). Files land only under `user/` — never on original game data.

## Not (yet) mapped

- Exact per-record payload encoding inside the exe's own files: needs a real
  WC98-generated `.dat`; Wine replay generation is blocked (wined3d GPU AV),
  so we gate on writer round-trip + byte-level constants above instead.
- Header table field semantics beyond the count/record split (`FUN_00495e60`
  writes it from live match state; per-dword names left as opaque `table[]`).
