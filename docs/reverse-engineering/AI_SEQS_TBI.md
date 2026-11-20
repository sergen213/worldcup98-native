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

Pool at `0x29c0` — refined (2026-11-07): stride **24** records, `(20696-0x29c0)/24 = 417`:
`[+0 fourcc A][+4 fourcc B]` (the printable clip-code names read as ASCII pairs: `aaaa`,
`cqck`/`ea4`, `sspe`, `ctch`, `cbst`, `ciel`, `sfsh`, `sfrh`, …), `[+8 u32 = seqIndex << 16 |
flags]` (entry i carries its own seq index in the high half: 0, 1<<16, 2<<16, 3<<16 …),
`[+12 zero]`, `[+16 u32 byte offset into the .TBA animation data block]` (e.g. `0x0080b9e4`).
The 534 record nameOff fields point at these tagged entries, i.e. seq -> clip-tag mapping;
the engine walks entries + 0x18 to fetch keyframe offsets (`FUN_004c3630` reads entry+0x18).

## .SCI script files

- 87 files: GEN* situation scripts + CLAINC00..03/CLAINS00..03 (4 language variants; language
  suffixes match `AUDSPC.W{E,F,G,I,J,S}` speech banks, INVENTORY). **No bytecode VM exists**
  (D-29, pass-17sci): payload rows are consumed as data tables by the tabA/tabB handler dispatch.
- exe side: the old "descriptor array at raw `0x17f09c` (VA `0x580a9c`, stride 32)" is the boot
  script registry read as a shifted window (retracted pass-110sci). Truth: record table VA
  `0x580A80` (raw `0x17F080`) 22 stride-`0x20` records `{flags, ctxId, count, poolVa@+0x1c}` + name
  pool VA `0x580520` 86 entries `{name[12], class u16@+0xe}`; registry slots `0x580D3C/40/44` =
  pool-end sentinel / record count / table VA (`TASK_HANDLERS_WC98.md` §3/§10, gate `test_sci`).
  The window VA itself has no static references (full-file dword scan) — access runs through the
  slots, not an immediate.
- Resource loader: `FUN_0047a3e8(EDX=FOURCC)` opens resources by 4-char tag (`'titl'` observed at
  call sites 0x44f337/0x44f430 — language-change path, writes `[0x60cbcb] = language byte`, then
  `FUN_0047ab1c` opens `common\%s` from the registry-selected `FC<LANG>.BIN`). This is the same
  `common\%s` sprintf loader used for FC banks (SPEECH notes), not a .SCI-specific path.

## Consequences for the port

- TBI index format known; usable as anim id -> clip name map (already consumed by anim/loco work).
- `.SCI` bytecode = single largest remaining script RE item (STATUS open item 4); no port consumer
  exists today, so it gates only faithful AI *scripting* beyond the ported sim tables.
