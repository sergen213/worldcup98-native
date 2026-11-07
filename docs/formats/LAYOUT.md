# LAY / VIS / INC layout language (FE screens)

INI-like text documents defining front-end screens, parsed by the FE resource loader
FUN_0047329c (VA; reads `SCREENS` 0x5648c8, `NUMSCREENS` 0x5648d0, `SCREEN` 0x56490c,
`NUMOBJECTS` 0x564914, `OBJECTTYPE` 0x564920; string xref sites 0x473314..0x47376f).
Loose copies: `FE\*.LAY`; archive copies in LAYOUTS.BIG / IGLAY.BIG and the .fe/.ingame trees.

## Grammar (per parser, formats/src/layout.cpp)

- Header line `;LAYOUT filename: x.lay`; `[section]` lines with `key = value` pairs;
  `{PC}`/`{PSX}` platform prefixes gate a section or property.
- `.INC` / `.VIS` text files overlay per-object sections; merge order of EXE 0x4ea520:
  `.inc` then `.vis`.
- `Message = trigger, triggerParam, keyMask, target, send, param1, param2[, flag[, sfx]]`
  (EXE 0x488104); symbols resolve through the FEMSG.BIN 72-byte record table (`SM_*`, `OM_*`,
  `FEJ_*`, `FESFX_*`).

## Objecttype vocabulary

Every FE UI class is registered at startup: driver FUN_00447910 (VA) calls one thunk per
class; each thunk loads EBX = parse function, EDX = vftbl, EAX = objecttype immediate
(`b8 <id> 00 00 00`) and tail-jumps registerClass FUN_00472fd8. The EAX value is the
`objecttype` key of an `[object N]` section (identity mapping; ids Edit=14, Slider=9,
Formation=15 verified against their key strings).

Full registry ids: 0-12, 14-24, 30-37, 39, 42-47. Unregistered gaps: 13, 25-29, 38, 40, 41, 48+.

| id | name (port) | parse fn | registration thunk (MOV EAX site = thunk entry) | distinctive keys |
|----|-------------|----------|-----------------------------------|------------------|
| 0 | image | FUN_00478184 | 0x4780f0 (`31 c0` -> eax=0) | TEXTID, SHAPEID, %s%s.fsh |
| 2 | button | FUN_004758ac | 0x475820 (EAX=2) | BOXCOLOUR, RECT, SOLID, CONNECTSTO |
| 3 | animseq | FUN_0046cd38 | 0x46ccc0 (EAX=3) | SEQUENCES, REWIND, TICKSPERFRAME, BACKNFORTH, LOADALLSEQS, FEPANE_ANIMTEXT |
| 4 | list | FUN_0046de5c | 0x46dda0 (EAX=4) | DELAYTICKS, MORPHER*, TEXTID |
| 11 | sbar | FUN_00476384 | 0x476300 (EAX=11) | DRAWSHAPE, FNAME, INGAME |
| 19 | knobbar | FUN_00476e1c | 0x476d80 (EAX=19) | BARSHAPEID, KNOBSHAPEID, SLIDERLEFTID, SLIDERRIGHTID |
| 32 | tbb | FUN_00485984 | 0x485850 (EAX=32) | RELEASEDSHAPEID, HILITEDSHAPE, PUSHEDSHAPE, DISABLEDSHAPE, FRAMES, HELP, POINTS |
| 37 | flg | FUN_0043b860 | 0x43b7f0 (EAX=0x25; raw `b8 25 00 00 00`) | VPATH2, FNAME2, ISHOME, IGHILITE, IGMSG, ".\fe\ingame\igflags\" |
| 42 | menu | FUN_0043f268 | 0x43f1e0 (EAX=42) | NUMITEMS, NUMSUBITEMS |
| 43 | box | FUN_004426a0 | 0x44263c (EAX=43) | BOXWIDTH, BOXHEIGHT, HEADERHEIGHT, TRANSPARENT |
| 44 | kt | FUN_0048a1f0 | 0x48a170 (EAX=44) | CLICKENABLE, TRICOLOUR, DELAYTICKS, HIGHLIGHTCALLBACK |
| 45 | btn | FUN_00442c34 | 0x442bc0 (EAX=45) | RELEASEDSHAPEID, PUSHEDSHAPEID, TRANSPARENT |
| 47 | sliderbtn | FUN_00442e64 | 0x442df0 (EAX=47) | RELEASEDSHAPEID, PUSHEDSHAPEID (instances slol/slor) |

Out-of-scope registry ids with key sets measured: 16 list {STRINGS, HOTSPOTS, NOLINE},
23 textlist {STRINGS, DISABLECOLOUR}, 24 mappane {CONNECTSTO, DRAWSHAPE, INGAME},
30/31 grid lists {NUMITEMS, PAGE, PIXELWIDTH; NUMVISIBLECOLUMNS, ROWHEIGHT, SCROLLBARPANEID},
33/34 flashing boxes {FLASHTICK, NUMOFFLASHES, TEXTCOLOUR1..4; TOPBORDER..}, 35 border box
{TOPBORDER..BOTTOMBORDER}, 36 unmeasured, 46 bar+text combo {TEXTID, TEXTXY, BARDIRECTION}.

Instance naming in shipped layouts: sbar (11), tbb* (32), flg1/flg2 (37), menu (42),
box* (43), kt00..kt05 (44), btn*/back (45), slol/slor (47). Port enum/names in
formats/include/wc98/formats/layout.hpp; evidence dump: analysis/pass10/lay_types.txt.
