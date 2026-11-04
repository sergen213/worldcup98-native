# Save slots & custom-player data — WC98 (pass 7, 2026-11-03)

Evidence: `analysis/pass7/saves.txt` (string xrefs), `analysis/pass7/saves2.txt` (decompiles of
FUN_0047b474/4ec/570/67c + FUN_0048537c/450; run scripts GhidraWc98Sav*.py).

## Confirmed — file set
- `.\user\options.sav` — global options block (see OPTIONS_SAV_WC98.md).
- `USER\SLOT%.2d.SAV` — numbered game-slot saves (tournament/carrier progress). Scan loop in
  `FUN_0047b67c`: slots iterate to a count taken from the directory state (`+1` upper bound) capped at
  **0x14 = 20**; per slot: open via generic reader `FUN_00439870` (same block/chunk API as
  options.studied in OPTIONS doc), and on parse failure mark the slot invalid.
- `user\Pdat%02d.sav` — custom-player data, ONE live block at **DAT_00607dcc** loaded/saved by
  `FUN_0048537c/450`; loader also refreshes six sibling blocks (`FUN_004964d0` ×6) before write and
  after read, then re-inits match state (DAT_006099c0 via FUN_00484a60).

## Slot summary record (slot browser UI cache)
- Array at **DAT_00616890**, stride **0x38** per slot: +0x00 valid flag; +0x04..+0x18 = six LE u32
  copied from the parsed slot stream fields record+0x08/0x0c/0x10/0x14/0x18 (header check
  `*(u32*)(rec+4) == 0x10000` = version/format id **65536**, then FUN_004c2400 decodes the display
  line). Six fields = strong candidates for (season/mode, played, won, drawn, lost, points) —
  HYPOTHESIS until a real slot file exists; layout is ground-truth.
- Save path `FUN_0047b474` writes the whole live block via stream copy `FUN_004398d0` (no transform):
  on-disk = DGROUP image, same pattern as options.sav.

## Generic block framing
All three file kinds share the reader chain FUN_004336d8(open probe) → FUN_00439870/FUN_004398d0
(block read/write) → chunk loop FUN_004c2ed0/0x439684 (documented in OPTIONS_SAV_WC98.md §Read path).
Slot stream records: field record+0x04 = format id 0x10000; record+0x08.. fields as above.

## Status / next steps
- No authentic SLOT/Pdat samples exist on the disc (USER/ ships only options.sav, itself an ASCII
  extraction artifact). Byte-level field semantics of the six summary u32s and the Pdat block layout
  require the Wine oracle session (WineOracle in progress): create a tournament save, change custom
  players, dump `SLOT00.SAV` + `Pdat00.sav`, diff per action. Harness = same method as options diff.
- Implementation hooks: `formats/src/slot.cpp` reader for the summary record + validity gate (lazy
  parse of full block not needed until save/load lands in app); launcher Save page can list slots via
  this struct once samples pin field order.
