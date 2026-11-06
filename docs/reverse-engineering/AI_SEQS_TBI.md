# AISEQS.TBI + .SCI script indexing — measured notes (2026-11-06)

Read-only audit of `WorldCup98_Extracted/COMMON/{AISEQS.TBI,*.SCI}` plus FIFAWC.EXE static scans;
companion docs: [SPEECH_INDEXING_NOTES.md](SPEECH_INDEXING_NOTES.md),
[COMMENTARY_WC98.md](COMMENTARY_WC98.md), INVENTORY rows `AISEQS.TBI` / `*.SCI`.

## AISEQS.TBI index (CONFIRMED, decoded)

Header (`u32 LE` x2): `[0]=count=534`, `[1]=0x29c0` = string-pool file offset.
Record array: stride **20** bytes at `0x8`, 534 records (region size 10680 = 534*20 exactly).

| off | field | measured |
|---|---|---|
| 0  | u16 id, u16 keyA | `id == record index` for all 534 (assert-verified) |
| 4  | u16 idx(=id), u16 keyB | second key column; e.g. rec2 = (2,0x2b), rec5 = (5,0x1c) |
| 8  | u32 nameOff | file offset into string pool (`0x29c0` = first entry `aaaaaa`) |
| 12 | u32 flags   | observed pattern `0x00080008`, tail rows `0x000a000a`; STATUS anim-clip note: byte +0xD drives clip load flags |
| 16 | u32 len/flags | e.g. `0x40`, last row `0x03020047` (low = name-ish length) |

String pool at `0x29c0`: 441 printable names, 6–8 char clip codes with digit suffix
(`aaaaaa, cqckea4, sspecz6, ctchfa1, bsppob1 …`) — animation **clip code** names (pitch/keeper/set-piece
action families), matching the exe anim-clip use recorded in STATUS (`id==index` -> clip 278 `bsppob1`).
Decoded dump: 534 records, keys = per-record event/anim key pair (semantics open).

## .SCI script files

- 87 files: GEN* situation scripts + CLAINC00..03/CLAINS00..03 (4 language variants; language
  suffixes match `AUDSPC.W{E,F,G,I,J,S}` speech banks, INVENTORY). Bytecode VM **unparsed**
  (parity with FIFA 99, REUSABILITY §12) — open item.
- exe side: a descriptor array at file offset `0x17f09c` (VA `0x580a9c`, stride 32:
  `{u32 nameptr -> .sci name, u32 id, 0xffffffff, u32 idx, …}`; 86 names `gengolNN.sci…`) carries the
  script file table. **No static reference to that array VA exists anywhere in the exe** (full-file
  dword scan) — access is computed at runtime via a global base pointer (DGROUP BSS), not an immediate.
- Resource loader: `FUN_0047a3e8(EDX=FOURCC)` opens resources by 4-char tag (`'titl'` observed at
  call sites 0x44f337/0x44f430 — language-change path, writes `[0x60cbcb] = language byte`, then
  `FUN_0047ab1c` opens `common\%s` from the registry-selected `FC<LANG>.BIN`). This is the same
  `common\%s` sprintf loader used for FC banks (SPEECH notes), not a .SCI-specific path.

## Consequences for the port

- TBI index format known; usable as anim id -> clip name map (already consumed by anim/loco work).
- `.SCI` bytecode = single largest remaining script RE item (STATUS open item 4); no port consumer
  exists today, so it gates only faithful AI *scripting* beyond the ported sim tables.
