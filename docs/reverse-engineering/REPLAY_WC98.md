# Replay system (.rply / FEREPLAYBUFF) — measured notes (2026-11-06)

Read-only Ghidra pass (`ghidra-scripts/GhidraWc98Rply.py`, `GhidraWc98Rply2.py`;
outputs kept in `/tmp/rply*.txt`); no live `.rply` samples exist on the tree
(runtime-generated; wined3d AV never reaches match presentation, see
[WINE_ORACLE_WC98.md](WINE_ORACLE_WC98.md)), so byte-level file bodies stay **Hypothesis**.

## Confirmed — strings & slot scan

Data block (raw 0x1604a9.., VA = raw + 0x401a00):
`user\rplyindx.dat`, `user\rply%04d.dat`, registry values `replay` / `replayfile`
(HKLM\SOFTWARE\EA Sports\World Cup 98), buffer tag `FEREPLAYBUFF`.

- **Slot scan** `FUN_0043c4f0` (xref from FEREPLAYBUFF string VA 0x561ed4 block):
  iterates `local_1c < 100` slots over a table at `DAT_00601e6c`, **record stride
  0x19 = 25 B** (name bytes + slot index byte written at offset +4), formats
  `user\rply%04d.dat` per slot and existence-tests each path (`FUN_004393d4`).
  → the game manages up to **100 replay slots** in `USER\`.
- **Load/select** `FUN_0043c29c`: opens via `FUN_0049f5a0` (mem-stream wrapper,
  critical section at slot+0x3c), counts records with `FUN_00495bc0` =
  `DAT_0057f840 + 1`, formats the filename from the slot byte.
- String VAs for `rplyindx.dat` / `rply%04d.dat` have **no static xrefs** —
  filenames come from a runtime-computed table base (Watcom DGROUP pattern,
  same as `.SCI`/`.TBI` tables).

## Confirmed — snapshot record framing

`FUN_00495e60` (record writer into the FEREPLAYBUFF stream):
- writes an int record index (`n-1`, clamped to `DAT_0057f840+1`), then copies
  **0x8e = 142 dwords** from the live presentation state struct `DAT_0063698c`,
  then two variable-length arrays (`local_1c`, `local_18` counts, copied via
  `FUN_004964d0`) that follow the fixed block; record span =
  `index + 0x238 fixed payload`, stream offset advances to
  `local_1c + local_18 + 0x23c` — i.e. **record stride 0x23c bytes**:
  `[u32 frameIndex][0x238 fixed presentation state][arrA len=local_1c][arrB len=local_18]`.
- The payload is the FE/presentation snapshot (match clock, score, camera, ball) —
  replay = **snapshot stream**, not input deltas.

## Hypothesis

- `rplyindx.dat` = table of the same 25-B slot records scanned by `FUN_0043c4f0`
  (name + slot byte per entry). Unverified: no sample file to inspect.
- Per-frame cadence matches presentation ticks; count fields cap playback seek
  (`FUN_00496110` skip loop walks records backwards from the end).

## Port design decision (lazy path, not implemented)

The port's sim is seed-deterministic (M7 scenario gates), so native replay =
**{seed + teams + config + recorded input-event stream} re-simulated through the
live match driver**, verified by asserting identical final state — smaller and
stronger than decoding 0x238-byte opaque presentation snapshots whose consumers
the port does not have. Original-file interop stays blocked on live samples;
documented here, no invented byte claims beyond the framing above.
