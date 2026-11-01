# Teams & 1998 World Cup structure — external reference

Purpose: cross-check target for FCDB `.DBI` contents (never a data
source — actual rosters/names come from the user's original files).
Sources: https://en.wikipedia.org/wiki/World_Cup_98_(video_game) and
https://en.wikipedia.org/wiki/1998_FIFA_World_Cup (accessed 2026-11-01).

WC98 ships **40 teams**: all 32 qualified for France 98, plus 8
non-qualified invited: **Australia, Canada, China PR, Greece,
Portugal, Republic of Ireland, Russia, Sweden** (Wikipedia). The World
Cup mode uses the real 1998 groups for the 32 qualifiers; groups can
also be randomized from all 40. Friendlies allow any of 40.

## Real 1998 first-round groups (for tournament-mode cross-check)

| Group | Teams |
|---|---|
| A | Brazil, Morocco, Scotland, Norway |
| B | Italy, Chile, Cameroon, Austria |
| C | France, Denmark, South Africa, Saudi Arabia |
| D | Spain, Nigeria, Paraguay, Bulgaria |
| E | Netherlands, Mexico, Belgium, South Korea |
| F | Germany, RF Yugoslavia, USA, Iran |
| G | England, Tunisia, Romania, Colombia |
| H | Argentina, Croatia, Jamaica, Japan |

Knockout: R16 → QF → SF → Final; group stage = 3 pts/win, no extra
time/shootout in group matches; knockout draws go to extra time
(golden-goal era rules) then shootouts (Wikipedia + 1998 WC rules).

## World Cup Classics — 15 classic matches (Wikipedia)

Unlocked by winning World Cup mode, then sequentially. Period kits,
hairstyles, names per era; Wolstenholme commentary (UK); presentation:
B&W for 1950/1954/1966 finals, sepia for 1930/1938; no substitutions
in pre-1970 matches (and the 1970 final); cards shown anachronistically
pre-1970; brown ball from 1970 on. Data sources in-tree:
`fe\teamhist.<lang>\*.his` + `.tri` (8 locales, SAVES.md).

## Checks this file enables

1. FCDB team list ⊇ {32 qualifiers + 8} (expect extra fictional/
   club league teams from the 24-league/56-team structure).
2. Real groups reproduced in tournament data tables (`.tri`/FCDB
   tournament records) — compare after parse.
3. Squad spot-checks (e.g. France 98: Zidane, Dugarby, Petit,
   Lebœuf; Brazil: Taffarel, Cafu, Aldair, Zé Elias...) — verify our
   extractor returns plausible 1998 rosters from the user's file.
