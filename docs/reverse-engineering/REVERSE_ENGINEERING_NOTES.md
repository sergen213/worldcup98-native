# Reverse-Engineering Notes — forensic method index (pass-15, 2026-11-10/11)

Index + method record for the second-pass forensic audit. Facts live in the per-topic docs; this
file records **how** each claim is provable and which lanes are dead, so every register row in
[GAMEPLAY_DISCREPANCIES.md](GAMEPLAY_DISCREPANCIES.md) can be re-checked independently.

## 1. Reference binary

- `WorldCup98_Extracted/FIFAWC.EXE` (read-only game tree; original Windows release). Watcom 2.18
  PE32, built 1998-04-13, not packed (`docs/reverse-engineering/EXECUTABLE.md`).
- Section deltas **measured this pass** (not assumed):
  - `.text` file offset = `VA − 0x400c00` — proven by the CD-probe anchor in `docs/STATUS.md`
    (file `0x38440` == VA `0x439040`).
  - data/DGROUP file offset = `VA − 0x401a00` — proven by byte-exact reproduction of the queue table
    at VA `0x58c752` (raw `0x18ad52`), see [GAMEPLAY_VALIDATION.md](GAMEPLAY_VALIDATION.md) §3.

## 2. Live lanes and what each can prove

| Lane | Command / recipe | Proves | Cannot prove |
|---|---|---|---|
| Static decompile (PyGhidra) | `.venv-ghidra/bin/python -m pyghidra --project-path ghidra --project-name wc98 --skip-analysis WorldCup98_Extracted/FIFAWC.EXE ghidra-scripts/<X>.py` (saved project = 4,720 analyzed functions; **exclusive** — one run at a time, `ghidra/wc98/wc98.lock`) | Function bodies, constants, tables, call graph, data-record layout | Runtime ordering, values chosen by dynamic state, code Ghidra cannot locate (Watcom blobs: disassemble-loop then `CreateFunctionCmd` at xref sites) |
| Byte lane (raw scans) | Python over the exe with the §1 deltas; unique-sigil/constant scans; record-stride dumps | Existence + exact value of every immediate/table, file-unique constants (e.g. `0xeab`, `0xc391` occur exactly once), absence proofs (`0x2f5c0`: zero occurrences) | Semantics of a constant without the consumer decompile |
| Launch-under-debugger (winedbg) | Xvfb `:7` 16 bpp, prefix `~/.wine-wc98`, ISO mounted (label `WCUPPCCD`), breakpoint set at process entry, paced command triples (~0.25 s); scripts kept in `analysis/pass7/`, `analysis/pass14sci/run1_fileopen.sh` | Which files the boot path opens, register state at Watcom call entries (EAX first arg), early-boot writes | Post-attach inspection: Yama `ptrace_scope=1` blocks gdb/frida attach; winedbg has no usable data watchpoint (`Z1` unsupported) |
| Port capture lane | `wc98-native --fe-smoke N --windowed … --out-prefix P`, `--live-match-smoke` → PNGs in `/tmp/wc98port-out/` | What the port presents (FE + match HUD), pixel-level regressions | Original-game pixels (see §3) |
| In-process telemetry | `wc98-native --self-test <json>` (`app/selftest.cpp`, fixed seeds, 14 steps incl. pass-15 input gate) | Deterministic port behaviour: movement, ball, AI, keeper, set pieces, stop-distance ladder replica | Original runtime values (needs §2 static/byte lanes for the reference side) |

## 3. Dead / blocked lanes (documented negatives — do not re-attempt blind)

- **Original in-match frame capture under Wine:** DirectDraw present path is dead in this
  environment; no captured original pixels exist. Consequence: pixel-diff validation of gameplay
  visuals (§9 of the objective) is impossible for in-match scenes; FE/menu screens are only
  recoverable as code-drawn label/scene registries (`FE_MENU_SCREENS_WC98.md`, `MENU_NAV_WC98.md`).
- **EULZ/SQF expander:** slot `0x593e94` has zero write sites in the exe, no setter in any shipped
  DLL; install mechanism absent from shipped binaries → decode needs a Win9x-era debugger
  (DOSBox-X). Evidence: `analysis/pass9/eulz/run13..run15`, `docs/asset-formats/SQF_FOOTIX.md` §3.
- **Referee positioning:** no gameplay referee tick in the static lanes (`cmn\ref.c` = `eacsnd`,
  `REFKIT` = renderer atlas) — negative pass-14ref; register row 12 stays qualitative.
- **.SCI opcode VM:** loader chain decoded (pass-14sci, `TASK_HANDLERS_WC98.md` §8 + memory notes);
  interpreter opcode semantics loop unparsed. Boot-scope `fileopen` probe shows FE boot opens only
  LAY/FSH/BIG/`modems.cfg`; `.SCI` consumption is match-scope.

## 4. Per-subsystem pointer index (each row = where the evidence lives)

| Subsystem | Notes docs | Register rows |
|---|---|---|
| Simulation core, tick clock, tables | `SIM_PORT_NOTES.md`, `SIM_TABLES_WC98.md`, `GHIDRA_PASS4_SIMULATION.md` | D-10..D-21 |
| Ball physics / kick solvers | `flight.cpp`/`ball.cpp` header cites, pass-15 byte-lane §3.1 of validation record | D-10..D-16 |
| Input (pad words, queue, context) | `input_tables.inc`, `MENU_NAV_WC98.md` | D-01..D-06 |
| Animation (clips, timing, contact frames) | `AI_SEQS_TBI.md`, `MDL_GEOMETRY_NOTES.md` | D-07..D-09, D-25..D-29 |
| AI / tactics / goalkeeper | `GHIDRA_PASS6_AI.md`, `HANDLER_DIFF_AUDIT.md` §4.5-4.8 | D-22..D-24, D-30..D-34 |
| Tournament / set pieces / rules | `TOURNAMENT_MODULE_WC98.md`, `GAMEPLAY_PARITY.md` rows 13/16/17/19 | D-35..D-37 |
| Camera / presentation | `presentation/src/camera.cpp` cites, `HUD_WC98.md`, `GHIDRA_PASS3_RENDERING.md` | D-38..D-40 |
| Commentary / audio triggers | `COMMENTARY_WC98.md`, `AUDIO_RUNTIME.md`, `SPEECH_INDEXING_NOTES.md` | D-41..D-46 |

## 5. Ghidra API pitfalls (this host)

- PyGhidra 3.2.0 in `/mnt/1st4TB/WorldCup98/.venv-ghidra/bin/python`; system python has no pyghidra
  (PEP 668). Never `import ghidra.program.flatapi` in scripts (silent death); pure JPype API only.
- `FunctionManagerDB.getFunction` is long-address-only → use `getFunctionAt(Address)`.
- External locations: `L.getSymbol().getName()`, `getExternalSpaceAddress()`; enumerate per library
  with `em.getExternalLocations(lib)`.
- Never run `py_compile` on `ghidra-scripts/*.py` (stale `__pycache__` shadowing).
