# USER/options.sav — WC98 native layout (pass 7, 2026-11-03)

Evidence: `analysis/pass7/options.txt` (PyGhidra run GhidraWc98Opt.py). Sibling FIFA 99 spec
(`OPTIONS_SAV.md`, AFIF blocks) does NOT apply: WC98 uses its own block + "FIFA" magic.

## Confirmed
- Path string `.\user\options.sav` @0x564180, referenced by save wrapper `FUN_0046b320` and loader
  `FUN_0046b350`.
- Magic: two LE dwords `'FIFA'` (0x46494641) at block offsets +0/+4 → globals `DAT_0060cba8`,
  `DAT_0060cbac`. Loader re-validates after read (`FUN_0046b350@0046b37d/b390/b3a4`) and range-checks
  language byte `DAT_0060cbcb ∈ [0,8)`; on any failure it calls the full-default path.
- Defaults: `FUN_0046ae8c` = reset (magic writes + `FUN_0046aee4`, `FUN_0046af38`, `FUN_0046afb4`,
  `FUN_00436710`, then an 8-iteration loop over `FUN_0046b020` touching `DAT_0060cbd8`).
- Read path: save wrapper opens via `FUN_0043b140` (generic file-open, 36 callers), checks size with
  `FUN_004336fc`, then `FUN_004398d0`; loader uses the generic block reader `FUN_00439870`
  (chunk loop `FUN_004c2ed0`/`FUN_00439684`) shared with stadium/user screens (`FUN_0047b08c`,
  `FUN_0047b570`, `FUN_0047b67c`, `FUN_00485450`).
- Field consumers of the block (base `DAT_0060cba8`), by cluster:
  | field | writers | readers / role (hypothesis) |
  |---|---|---|
  | +0x08/+0x0c (`cbb0`) | defaults `FUN_0046aee4`, screen `FUN_0046b3e0` | stadium preview `FUN_0044fa84/44fafc`, apply `FUN_0046ba18` = stadium code + day/night pair (cf. FIFA99 +21/+22) |
  | +0x14 (`cbbc`) | `FUN_0046af38` | game-mode state machine `FUN_004192f4`, renderer apply `FUN_00450bb0` = game-speed/detail class |
  | +0x18/+0x1c (`cbc0/cbc4`) | option-screen widgets `FUN_0046b074/46b1a4/46b230/46b2b0` | `FUN_00450bb0`, FE `FUN_0047a8f4` = detail sliders pair (field/stadium) |
  | +0x20 (`cbc8`) | — | 37 direct-address refs in the options screens `FUN_0044f*`/`FUN_00478b*` = active-tab/scroller state |
  | +0x24..+0x30 (`cbcc..cbd8`) | `FUN_0046afb4`, defaults loop | camera/game fns `FUN_00410a10`, `FUN_0044bfc0`, `FUN_0046b5c8/46b6b8` = half length, clock style, weather trio |
  | +0x5c (`cc04`) byte fields | loader clears bytes 1..3 on success | renderer init `FUN_00445664`, menu `FUN_0044818c` = flags (radar/numbers/indicators analogues) |
  | +0x60 (`cc08`) ×7 refs | — | `FUN_0046b990/46b9d4` volume faders = the four ADVD volumes block |
  | +0x64..+0x78 (`cc0c..cc20`) | — | camera screens `FUN_0046d2c0`, `FUN_0046d458` (+ raw gap fns 0x46d6xx) = camera-mode selections per view type |
- Save writes the whole live block: wrapper calls `FUN_0043b140` (open), size probe, then
  `FUN_004398d0` stream-copy — no serialization transform ⇒ file is a raw DGROUP image of
  `DAT_0060cba8..+~0x80`, exactly as the FIFA99 sibling does for its AFIF blocks.

## Status of byte-level semantics
The extracted tree's `USER/options.sav` (54 B) is an ASCII artifact of extraction, not a game
binary — no authentic WC98 sample exists on disk. Byte offsets ↔ option labels above are HYPOTHESIS
(cluster co-location + FIFA99 family analogues). Ground truth requires one Wine session: run
FIFAWC.EXE, change each FOPTION value, save, diff `user\options.sav` (planned as part of the M7
wine-oracle pass; harness pattern = sibling project's wine-reference tooling).

## Implementation plan (unblocked parts)
1. `formats/src/options.cpp`: parse/serialize "FIFA"×2 + raw ~0x84-byte block, language-range check,
   default image from the reset-cluster constants once captured — no stubs for the mapped fields;
   unmapped bytes pass through verbatim (round-trip safe).
2. Map to `wc98::config` keys on load; write back on exit (mirrors original save-on-exit behavior).
3. Gate: round-trip unit test over a synthetic block + structural asserts against the consumer map;
   real-file test skipped until the Wine sample lands (documented, not faked).
