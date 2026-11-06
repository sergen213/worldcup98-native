# Tournament module (docs/reverse-engineering/TOURNAMENT_MODULE_WC98.md §4-§5): ZWCUP.RES pair
# matrix, group standings + tiebreak chain, knockout bracket graph, 1998 schedule plan. Pure logic:
# no renderer/platform/format dependencies; gate = tests/unit/test_tournament.cpp.
file(GLOB WC98_TOURNAMENT_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(wc98_tournament STATIC ${WC98_TOURNAMENT_SOURCES})
target_include_directories(wc98_tournament PUBLIC include)
add_library(wc98::tournament ALIAS wc98_tournament)
