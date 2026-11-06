// FE team-select paging model (app/team_select.hpp): 40-team pool = 5 pages of 8 rows;
// cursor wraps through page boundaries; empty count stays inert. No tree/render deps.
#include "team_select.hpp"

#include <cassert>
#include <cstdio>

using wc98::app::TeamListNav;

int main() {
    TeamListNav t;
    assert(t.pageCount() == 1 && t.page() == 0); // empty list: inert
    t.move(3);
    assert(t.sel == 0);

    t.setCount(40); // the ZWCUP pool size (5 pages x 8)
    assert(t.pageCount() == 5);
    for (int i = 0; i < 40; ++i) {
        assert(t.page() == i / TeamListNav::kRows);
        t.move(+1); // walk the whole pool forward
    }
    assert(t.sel == 0 && t.page() == 0);

    t.setCount(40);
    t.move(-1); // wrap to last entry: page 4 row 7
    assert(t.sel == 39 && t.page() == 4);
    t.move(+1);
    assert(t.sel == 0 && t.page() == 0);

    t.setCount(8); // setCount shrinks the pool (placeholder ids dropped): page count follows
    assert(t.pageCount() == 1);
    t.setCount(33); // partial last page
    assert(t.pageCount() == 5 && t.page() == 0);

    std::printf("test_teamselect: ok\n");
    return 0;
}
