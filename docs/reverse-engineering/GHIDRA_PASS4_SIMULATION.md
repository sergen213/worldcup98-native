# Ghidra Pass 4 — Simulation / Input / Audio-Service Anchors (2026-11-02)

Headless PyGhidra runs: scripts `ghidra-scripts/GhidraWc98Sim{,2,3,4}.py` +
`GhidraWc98Getters.py`/`GhidraWc98Callers.py`; decompiles in
`analysis/pass4/FIFAWC.EXE_sim{,2,3,4}.txt`. Every claim cites an address;
confidence per item as Confirmed (decompile + xref evidence) / Hypothesis / Unknown.

## Methodology facts learned (Confirmed)
- The Ghidra DGROUP block is **zero-filled** (`VirtualSize=0` PE header quirk, see
  AUDIO_RUNTIME.md): in-Ghidra memory scans for strings/pointers silently miss everything.
  Pointer-slot discovery must be done host-side on the raw file:
  `raw2va(r) = r - 0x158600 + 0x55a000` (DGROUP), `r - 0x400 + 0x401000` (AUTO).
- Watcom assert strings (`SNDspeechinit - VOL OF %i…` @0x56ae24+) are loaded via LEA/add
  immediates → **no static xrefs** to them; they anchor code only through the string pool.
- `$FIFAVER$<MODULE>V<x-y-z>` banners exist for FEPC, AUDIO, COMM, FE_EAC, FE_UK, MISC,
  RENDER, VERSION, VIDEO, LIBS_{EAC,SND,CMN}, CAMERA V12-2-10, MOTION V4-1-12, REPLAY V13-0-5,
  **IG V5-0-16**, AUDIOCMN V18-9-9, **AI-ver V13-6-14**, FCDB V10-5-0, FCLG V1-1-2,
  TOURNAMENT V11-7-9 (file offsets 0x162750–0x16a358).

## Module version manifest (Confirmed)
Each banner has a leaf getter returning its pointer: IG `FUN_004ab4a0`, AI `FUN_004c9710`,
CAMERA `FUN_004a0710`, TOURNAMENT `FUN_004e99e0`, FCDB `FUN_004e99c0`, FCLG `FUN_004e99d0`,
MOTION/RENDER pair via `FUN_0043cfa4`. They are called from a single report builder
`FUN_00490c48` (448 instrs): appends `"FIFA : Road to World Cup, PC CD I…"` + every getter
result through the string appender `FUN_00490bc8`, then `FUN_004c2bb0` (FILE/LINE diagnostic).
Caller chain: `FUN_00544ae8 → FUN_0043cfa4 → FUN_00490c48`. This is a **build/version report**
(registered for diagnostics), NOT the per-frame module dispatch — the sim loop does not live here.

## Input (Confirmed)
- `win\key.c` pump thread: `FUN_004c9c30` sets assert context (`DAT_00585c9c = "win\key.c"`,
  line `0x1db=475`), calls init `FUN_0052aac0` (which refuses when a surface is set —
  `"DO NOT CALL WITH SURFACE LO…"` @0x56d66c), then loops `{ FUN_004bf0b0(); FUN_004c9c10(); }`
  until `FUN_004c9c10() != 0`. Started from `FUN_00439028` (231 instrs).
- `FUN_004bf0b0` = key-state read primitive: 26+ callers across FE (0x41–0x4e region, incl.
  `FUN_004d1340` size 1101, `FUN_0049110c` size 826) — shared input poll for screens.
- Joystick: imports `joyGetDevCapsA/joyGetNumDevs/joyGetPosEx` (winmm @0x558530+), device-name
  strings (`Gravis Gamepad`, `Microsoft Sidewinder Gamepad`, `Creative Labs Blaster Gamepad`
  @0x559ae4+) → classic MMJOY polling, no DirectInput.

## Audio services inside the EXE (Confirmed)
Range 0x4df800–0x4e7100 = EA cmn sound runtime embedded in FIFAWC.EXE: `iSNDstreamservice`
parser (`…ERROR PARSIN…` @0x56ba50), MIDI sequencer API (`SNDseqplayPVB`, `SNDseqautovol`,
`SNDseqstop/seqadd` @0x56b3c0–0x56b604), SoundBlank bank download (`iSNDdownloadbank …
BANK VERSION…` @0x56b2a8). The two mega-functions `FUN_004dff20` (3,942 instrs) and
`FUN_004e11f0` (3,474) dominate it — stream/sequencer service bodies. Speech API assert
strings (`SNDspeechinit/SNDphraseadd/SNDsentencestart…` @0x56ae24–0x56afac) sit in the same
pool: the speech queue implementation is IN THE EXE (eacsnd.dll is a thin host).

## Pointer-table leads for next pass (Confirmed slots, consumer unknown)
- `0x570380`: absolute pointer to `"ingame\player\models"` — inside a path-template block near
  the turf raw refs (0x5701f8+, pass3); no Ghidra xref (computed access). Consumers of this
  template load in-game player models → locate by tracing writes/reads around DAT_00570364-0x5703c0.
- `0x57f7fc`: pointer to `"TT_CORNER_KICK_POSX_POSZ"` (set-piece placement table names, pool
  @0x56819c–0x5681f0) — tournament/set-piece config keyed by name; parser compares strings.

## Negative results (documented per goal rule 14)
- Module version getters do not lead to the sim loop (manifest only).
- No static xrefs to commentary/event-name string pools (`Offside`, `PRACTICE/FREEKICK …`):
  they are consumed via base+offset comparisons — needs dynamic capture or manual disasm of
  the comparing loops, not Ghidra reference walking.

## Next steps (prioritized)
1. Decompile the key-consumer cluster `FUN_00437b9c/437c14/437cec/446850` (callers of
   `FUN_004c9c10`) — these sit right after the startup region and gate on input: likely FE/IG
   frame loops.
2. Walk callers of renderer present (`THRASH binder 0x4916b0`, HAL callbacks DAT_0058a9ec-fc)
   → frame loop; its sibling calls = per-tick sim update (IG module).
3. Host-side: trace accessors of the `ingame\player\models` template block to find the in-game
   model loader, then correlate with INGAME .BIG member magics (M4 player models).
4. Wine dynamic capture (`wineref.py` idiom from Kabus) for commentary trigger timing if static
   walking stays blocked.
