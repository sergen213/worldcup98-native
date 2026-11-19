# PLAYER_ATTRIBUTE_REFERENCE — 20-byte FCDB player record (WC98 1998 schema)

Decoder: `formats/src/fcdb.cpp` (`FcdbPlayer`). Raw 5 u32 words are preserved on every record
(`p.raw`) so re-encoding and unknown-bit comparison are possible.

## Record map (bytes 0..19)

| Byte(s) | Field | Encoding / evidence |
|---|---|---|
| 0..1 (low 14 bits of dword0) | player id | `((b1&0x3F)<<8)\|b0`; strictly ascending (engine binary-search key) |
| 1..2 (bits) | nationality | `(b2<<2)\|((b1&0xC0)>>6)` (10 bits) |
| 3 | `starting` (editor byte) | even values 82..198, 54 distinct. Editor's RTWC 'starting-position' byte. **Engine slot matching consumes the role byte, not this field** (`0x49FDC8` masks vs `PlayerAttr+0x11`); semantics here remain documentation-only for WC98 |
| 4..5 | transfer price | u16 LE |
| 6 | appearance | `hairStyle<<4 \| hairColor<<2 \| skinTone` |
| 7 | appearance | `faceStyle<<3 \| beardStyle` |
| 8..15 | 16 attribute nibbles | hi = attr[2i], lo = attr[2i+1] |
| 16 | attributes + shirt | low nibble = tackle (attr[16]); hi nibble = shirt high bits |
| 17 | shirt + role | shirt = `((b17&0xF)<<4)|(b16>>4)` (8 bits, 0..255; measured in-use 1..22, 255 = 'no number' sentinel ×720); role = b17>>4 |
| 18..19 | engine lane | runtime stamina derives from the **fitness nibble** (`PlayerAttr` record +0x18/+0x1B built from `kFitness`, `simulation/src/player.cpp`); DB bytes 18..19 not decoded — semantics unknown, preserved raw |

## The 17 attributes (order = record order, values = nibbles)

Names co-derived from fifa98edit (MIT). Every nibble `n` encodes the game value `n*4+39`
(1998 range 39..99, step 4; `wc98SkillValue`).

```
idx  name          nibble position
0    aggression    byte 8  hi
1    acceleration  byte 8  lo
2    attack bias   byte 9  hi
3    agility       byte 9  lo
4    ball control  byte 10 hi
5    awareness     byte 10 lo
6    fitness       byte 11 hi        (engine: stamina seed, PlayerAttr+0x18)
7    creativity    byte 11 lo
8    passing       byte 12 hi
9    heading       byte 12 lo
10   reaction      byte 13 hi
11   pass bias     byte 13 lo
12   shot power    byte 14 hi
13   shot bias     byte 14 lo
14   speed         byte 15 hi
15   shot accuracy byte 15 lo
16   tackle        byte 16 lo
```

Bias fields (attack/pass/shot bias) are AI tendencies consumed by the ported team-AI,
not physics parameters; engine skill wiring lives in `Team_buildMatchPlayerAttrs 0x478B08`
(simulation port `simulation/src/player.cpp`) with clamp `0x478520`.

## Shirt numbers / roles

* Shirt field 0..255 (8 bits); in use: **1..22** squad numbers, **255** = 'no number' sentinel
  (720 reserve-pool players — editor display code must special-case this).
* Role b17>>4 ∈ 0..10 (161 goalkeepers at role 0). This is the engine's position-code space
  used by the formation slot fill masks (`kSlotFill` table, exe 0x586B74).

## Appearance identifiers

`hairColor` (2b), `hairStyle` (4b), `skinTone` (2b), `faceStyle` (5b), `beardStyle` (3b).
Texture mapping: `presentation` `kits::lookOf` consumes the decoded fields directly and
composites faces from `COM_<lod>` + `HHIG_/HMED_` strips — verified in
`presentation/src/kits.cpp` (`faceComposite`, `imagesFor`). Numeric ids are WC98-native;
they are NOT interchangeable with fifa98edit RTWC appearance indexes.
