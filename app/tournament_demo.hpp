#pragma once
// Tournament progression driver (docs/reverse-engineering/TOURNAMENT_MODULE_WC98.md §4-§5).
// Reads $WC98_TREE; exit codes: 0 ok, 1 failure, 77 tree/ZWCUP.RES missing (ctest skip).
// Shared by wc98-tournament-demo and the FE flow's Tournament entry point.
int runTournamentDemo(int userTeam = -1); // FE team-select pick (FCDB id; -1 = no pick, pre-pick behaviour)
