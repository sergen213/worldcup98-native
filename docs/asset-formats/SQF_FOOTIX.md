# SQF / FOOTIX — FE overlay archives (EULZ container)

Measured 2026-11-06 on all 49 `FE/*.SQF` files (`WorldCup98_Extracted/FE`) and the
FIFAWC.EXE Watcom PE loader. Implementation: `formats/src/sqf.cpp` (container only);
gate: `tests/unit/test_sqf.cpp`.

## 1. Container layout (all little-endian) — verified on 49/49 files

| Off | Size | Field | Census |
|-----|------|-------|--------|
| 0x00 | 4 | magic `EULZ` (`45 55 4C 5A`) | all 49 |
| 0x04 | 6 | kind signature `*RTFD\0` (byte[7] == `'F'`, checked by loader) | all 49 |
| 0x08 | u32 | FE-set constant (semantics Open) | **68** on every file; not the file count (49 files, 48 exe path strings) |
| 0x0C | u32 | resource-type count of this archive (loader: must be non-zero) | min 16, max 280, sum 5493 over the set |
| 0x10 | 16 | UUID-ish id; creation date as **u32be** at 0x18 (e.g. `0x0139ed38`) | — |
| 0x28 | 16 | second UUID-ish id; date u32be at 0x38 | — |
| 0x44 | rest | compressed payload stream | size = filesize − 0x44 (e.g. FTX10_1: 35683 of 35751) |

`u32@0x0C` is **not** a byte length (FTX13_2 file 4568 B, field 40; FTX10_1 field 90).
Parser (`parseSqf`) validates magic/kind/non-zero type count/payload presence.

## 2. Runtime loader contract (FIFAWC.EXE)

- FE path table at raw `0x1645df`: 48 strings `fe\ftxN_M.sqf`.
- Consumer `caseD_0` @ `0x47b9d0` (switch @ `0x47b9ad`) opens each file via `FUN_00439810`;
  tag block VA `0x5662e8` = `'EULZ\0\0\0\0'` + `"FOOTIX_F"`, and `0x5662fc` = `"FTXLFT"`.
- Validation loop (in the loaded **decompressed** image) requires per block: u32 tag
  `'EULZ'`, byte[7] == `'F'` (`FOOTIX_F`), non-zero u32@+8 and +0xc; chain fields at
  +8/+0xc are linked-list pointers.
- Expansion is performed by a **function pointer** stored in DGROUP slot `DAT_00593e94`,
  called just after the magic check (call sites raw `0xc2277` = `cmp dword [slot],0 / jz +4 /
  test esi,esi`, and `0xc2295` = `ff 15 94 3e 59 00`).

## 3. EULZ expander status — NOT recoverable from this binary (Open item)

- The slot `[0x593e94]` is **NULL at runtime**: launch-under-winedbg probe (pass 7e/9 lane,
  `analysis/pass9/sqf_wine_run3.sh`) armed a read of the slot at every load (`bp1` @
  `fifawc+0xc2e75`, 15 hits) and observed value `0x00000000` each time; the indirect call is
  skipped (JZ taken → cleanup/ret path, `bp2` @ return site never fired).
- Static scan of the whole image: the immediate `0x00593E94` appears exactly twice (the two
  sites above); no `mov [ebp+slot]`-style write exists. The expander is installed by a
  runtime patcher that computes the address from the DGROUP base → not statically locatable,
  and **absent** at analysis time (4,730-function Ghidra pass). Consequence: under Wine the
  FE SQF loads fail entirely; no decompressed sample exists on disk.
- Payload grammar probing (throwaway `/tmp/sqf_probe*.py`): stream is byte-aligned
  literal + escape-token LZ data. Escape = 3 bytes `7F C P`, control `C >= 0x80`
  (`0x8F` dominant: 42% of escapes in FTX13_2; also `0x86/0x87`). Param nibbles skew to
  `{3,4}` (len base) and runs of `7F 8F FF` occur consecutively (solid-pixel RLE-LZ
  matches). Not RefPack (`isCompressed()` false), not zlib/deflate, not bit-packed LZSS;
  length/dist split unconfirmed. Marked **Open**: image decode (palette + rows) cannot be
  built without one decompressed FOOTIX sample.

## 4. Gate semantics

`test_sqf` = synthetic header checks + tree census: 49/49 parse, `archiveCount == 68`,
type-count range [16..280] summing to 5493, payload length = filesize − 0x44, and FNV-1a/64
over the **raw** payload stream pinned for FTX10_1 `0x5c3363fc09ad0cf2`,
FTX7_2 `0xbca2732859187e04`, FTX13_2 `0x6af7f39411287da7` (stream stability, not decoded output).

## Open items

1. EULZ expansion algorithm: needs a captured decompressed block (install the `[0x593e94]`
   hook target on a working Win98-era environment) or an EA tool spec. Then FOOTIX image
   decode model + palette extraction follow.
2. `FTXLFT` (VA 0x5662fc): second magic/name field, no raw occurrence in any SQF → assumed
   to live inside expanded blocks only; unverified.
3. Meaning of the two id/date records at 0x10/0x028 (Mac resource-fork style ids) — open.
