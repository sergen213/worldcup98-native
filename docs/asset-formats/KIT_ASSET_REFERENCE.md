# KIT_ASSET_REFERENCE — WC98 kit textures vs fifa98edit's RTWC kit lane

## fifa98edit kit lane is RTWC-only — ABSENT here

`PLAYER/TEXTURES/PLYRKITS/JERSxx.FSH` and `PLAYER/TEXTURES/COMMON/3DTEXHI.FSH` (the editor's
`showJersey` inputs) do not exist in `WorldCup98_Extracted` (`find . -iname '*jers*' -o
-iname '*3dte*'` → none). The editor's kit renderer cannot run on WC98.

## WC98 kit lane: `INGAME/PLAYER/TEXTURES/` (141 FSH, 15.9 MiB)

| Pattern | Content |
|---|---|
| `PK<teamid>A.FSH` / `PK<teamid>H.FSH` | full kit atlas per FCDB team id (026..876): tags `hifa hrsv hlsv hslv hclt hulg hllg hjbk hjfr hair` + model-mirror `m*`/`l*` entries (24 tags, type-0x7B GIMX records) |
| `PK999{A,B,C,D,E}.FSH` | officials / shared single-set atlases (referee sets) |
| `GK<nn>_{30,70,90}.FSH` | keeper kit sets GK00..GK05 × 3 LODs (no `_10`; LOD 10 falls back to `_30`) |
| `RK<nn>_{30,70,90}.FSH` | rookie/keeper alternates |
| `COM_{10,30,70,90}.FSH` | shared skin/head parts (`hgvo harm hhdc mhdc ...`) |
| `HHIG_*` / `HMED_*` / `HLOW_*` | hair + face strip atlases per LOD tier (LOW carries `hair` only; face tags `hifa`/`mdfa` are runtime-composited — no CD bitmap) |
| `PALJER(.10).FSH` | numeric jersey-palette swap entries (`0800 0801 1600 1601`) — the runtime recolor path (`pk/rk` handlers `FUN_00411080`/`FUN_004110ec`) |

Home-vs-away: `H` files are distinct pre-coloured atlases (md5 differs from `A`);
the loader prefers `PK###H`, falls back to `PK###A` when absent (e.g. `PK861H` legends).

## Data-driven mapping in the port

`presentation/src/kits.cpp`:

* `KitTextures::kitFile(teamId, away)` builds `PK%03dA/H` from the **FCDB team id** — no
  hardcoded team table.
* `lookOf(FcdbPlayer)` feeds hairStyle/hairColor/skinTone/faceStyle/beardStyle + shirt
  number straight from the 20-byte DB record into `compositeStrips`/`faceComposite`
  (`COM_<lod>` base + `HHIG_/HMED_` strips).
* Shirt colour indices from the team record's kit bitfields + the original palette table
  (`kitColor`/hslCols, MENU_NAV_WC98 §288) drive HUD/IGTEAM swatches.
* `hifa` composite: no atlas carries the tag; palette recolour at runtime (pass-16 kit-map
  probe, DEVELOPMENT_LOG 2026-11-01 §307). Exporter leaves flat where strips are absent.

Verified gates: `MILESTONE6` presentation lane + `fe_mgmt_pages` kit-swatch evidence lines;
`test_kits`/capture gates over PK026A/H + GK00_30 tag sets. No new kit designs were
introduced — all kit content originates from `PK/GK/RK/COM/HHIG` atlases + FCDB fields.
