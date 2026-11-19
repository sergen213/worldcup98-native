# fifa98edit integration audit (FIFA 98: Road to World Cup editor vs World Cup 98)

Reference implementation: <https://github.com/ma-akad/fifa98edit> (MIT). Clone audited at
`analysis/fifa98edit/` (parent dir, not repo-tracked; MIT + author copyright preserved in citations).
This document records what the editor does, how much of it holds for our target
(FIFA World Cup 98 PC, `WorldCup98_Extracted/`), and what the independent extraction layer
(`formats/src/fcdb.cpp`, `tools/dbexplorer/`) instead derives from the actual binaries.

## 1. What fifa98edit does

| Lane | Editor behaviour | Verified claim |
|---|---|---|
| Database I/O | Reads/writes `FCDB.DBI` + language `STR0/STR1` DBI sections (count/len u32 fields, record tables, offsets relative to table start) | Grammar holds verbatim for WC98 (version field 501 checked; same framing) |
| Player record | 17 named attributes, nibble-pair packed; `roles[]` list; jersey/`starting` bytes | Field names reused; WC98 record is 20 B (RTWC editor record differs) — layout below |
| Attribute scale | Editor computes averages from raw values | WC98 nibble n encodes game value `n*4+39` (range 39..99, step 4) — verified against engine `PlayerAttr_clamp 0x478520` ratings and editor sums |
| TMMNG.DB | `read_tmmng/write_tmmng`: per-team tactics block, 2 B per formation slot × 17 slots, corner/free-kick/PK taker fields | RTWC file only. WC98 does not ship a `tmmng.db` — see `TMMNG_FORMAT_NOTES.md` |
| Kits | Reads `PLAYER/TEXTURES/PLYRKITS/JERSxx.FSH` + `COMMON/3DTEXHI.FSH`, renders `showJersey` from shirt/shorts/socks style + palette entries | Files ABSENT in WC98 tree (probed: `find . -iname '*jers*'` → none). WC98 kit lane is `PK<teamid>{A,H}.FSH`; see `KIT_ASSET_REFERENCE.md` |
| JSON export | Dumps parsed records to JSON | Not reused; our `wc98-dbexplorer --json` exports normalized + raw + offsets |

## 2. Compatibility matrix (WC98 vs editor assumptions)

| Item | RTWC (editor) | WC98 measured | Verdict |
|---|---|---|---|
| `COMMON/FCDB.DBI` | section framing | identical framing: `FCDB` magic, version 501, three table offsets @8/12/16 | compatible grammar |
| League record | fixed | variable-size, packed u32: id 12b, field12 12b, flags 24/25, member count 6b @26 + u16 LE member ids; **member id 0 = empty slot** (league 217 row 6) | same shape, WC98 ids |
| Team record | 20 B slots | **68 B (17 u32 words)**, roster 20 u16 LE slots at byte 24, byte 22 = squad nibble + nation nibble, kit/bank/stadium/tactics bits | editor bit layout verified against the file (kit colours, tactics `b21>>2`) |
| Player record | 18 B | **20 B**: id 14b/nation 10b in bytes 0..2, `starting` byte 3, price bytes 4..5, appearance bytes 6..7, 17 nibbles bytes 8..16, shirt 10b + role 4b bytes 16..17 | editor names/scale reused; offsets are WC98-specific |
| Shirt numbers | — | values 0..22 numbered, **255 = 'no number' sentinel** (720/1782 reserve-pool players) | editor display logic would crash on 255 |
| Role byte | `roles[]` index | b17>>4 ∈ 0..10 (161 GKs at role 0); engine slot masks (`0x586B74`, 17×11 table) consume the **role** byte, not `starting` | confirmed by sim port `fillFromSquad/fillFromPitch` |
| `FCDB_<LANG>.DBI` STR0 | league+team names | identical; ENG/FR/GER/IT/DUT/SWE/SPA + DAN (Danish added) | compatible |
| `FCDBP<LANG>.DBI` STR1 | player names | identical, 1782 entries | compatible |
| `FC<lang>.BIN` | UI strings | `FCLG` magic, NUL-separated, string id = index; 9 banks all same count | compatible |
| DBM files | — | big-endian mirror: magic + version 501 BE, table offsets BE; `FCDB.DBM` = 24/56/**2094** players (Mac 68k artifact pool) | same framing, BE dword; different pool; not consumed by the PC engine — documented, not decoded |
| `USER/tmmng.db` | tactics file on disk | engine registers `tmmng.db` (+`xfer.db`,`custom.db`,`cusplyr.db`) via `user\%s`; CD ships none | open item — `TMMNG_FORMAT_NOTES.md` |

## 3. What we did NOT reuse

* No editor code shipped in the repo; it remains a semantic reference (attribute names,
  kit bit layout, tactics table shape). All parsing is our own C++23 (`formats/src/fcdb.cpp`).
* The editor was never run against the installation (no writes were attempted on game files).
* Editor JSON output was not copied; `wc98-dbexplorer` re-exports from the parsed records
  and the field-by-field gates (`test_fcdb`, `--validate`) are the truth source.

## 4. License note

fifa98edit is MIT © its author; the repository `THIRD_PARTY_NOTICES.md` carries the citation
and the file `docs/asset-formats/FCDB_WC98.md` references it per-field where semantics were
adopted. Proprietary game data stays outside source control (`.gitignore`-excluded dumps).
