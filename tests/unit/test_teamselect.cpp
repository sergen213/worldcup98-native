// FE team-select pool model (app/team_select.hpp): the FEFLOW11 row order is the
// ZWCUP.RES qualification order partitioned by FUN_0044b618 (eight non-qualified
// ids moved to the tail in appearance order); the cursor walks the pool with
// wrap and the row window tracks it continuously (sbar slider semantics).
// No tree/render deps.
#include "team_select.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

using wc98::app::TeamListNav;
using wc98::app::poolOrder;

int main() {
    // ---- pool partition (FUN_0044b618 [C], analysis/pass11team/fe11-pool.txt) ----
    // ZWCUP.RES id order (40 entries).
    const std::vector<int> zwcup = {86, 87, 88, 57, 59, 60, 61, 62, 63, 64,
                                    65, 599, 67, 69, 71, 72, 41, 760, 31, 47,
                                    704, 49, 37, 52, 53, 54, 56, 26, 796, 29,
                                    32, 33, 34, 35, 36, 38, 39, 635, 815, 752};
    const auto order = poolOrder(zwcup);
    assert(order.size() == zwcup.size());
    const std::vector<int> expect = {86, 88, 57, 59, 60, 61, 63, 65,     // 0-7
                                     599, 67, 69, 71, 72, 760, 47, 704,  // 8-15
                                     49, 37, 52, 53, 54, 56, 26, 796,    // 16-23
                                     32, 34, 35, 36, 38, 635, 815, 752,  // 24-31
                                     87, 62, 64, 41, 31, 29, 33, 39};    // 32-39 tail
    for (std::size_t i = 0; i < expect.size(); ++i)
        assert(zwcup[std::size_t(order[i])] == expect[i]);
    // Stability: every id appears exactly once.
    for (int id : expect)
        assert(std::count(zwcup.begin(), zwcup.end(), id) == 1);
    // Already-tail ids keep relative appearance order (87, 62, 64, 41, 31, 29, 33, 39).
    {
        const std::vector<int> allSpecial = {87, 62, 64, 41, 31, 29, 33, 39};
        const auto o = poolOrder(allSpecial);
        for (std::size_t i = 0; i < allSpecial.size(); ++i) assert(o[i] == int(i));
    }

    // ---- cursor + continuous window ----
    TeamListNav t;
    assert(t.top() == 0 && t.rowsShown() == 0 && t.scrollRange() == 1);
    t.move(3);
    assert(t.sel == 0); // empty list: inert

    t.setCount(40); // the ZWCUP pool size
    assert(t.scrollRange() == 40 - TeamListNav::kRows);
    for (int i = 0; i < 40; ++i) {
        assert(t.sel == i);
        assert(t.top() == (i < TeamListNav::kRows ? 0 : i - TeamListNav::kRows + 1));
        assert(t.rowsShown() == TeamListNav::kRows);
        t.move(+1); // walk the whole pool forward
    }
    assert(t.sel == 0 && t.top() == 0); // wrapped

    t.move(-1); // wrap to the last entry: window ends on the pool end
    assert(t.sel == 39 && t.top() == 40 - TeamListNav::kRows);
    t.move(+1);
    assert(t.sel == 0 && t.top() == 0);

    t.setCount(8); // small pool: window pinned at 0, thumb inert
    assert(t.top() == 0 && t.rowsShown() == 8 && t.scrollRange() == 1);
    t.setCount(33); // partial tail window still 8 rows at the bottom
    t.sel = 0;
    for (int i = 0; i < 32; ++i) t.move(+1);
    assert(t.sel == 32 && t.top() == 33 - TeamListNav::kRows);
    assert(t.rowsShown() == TeamListNav::kRows); // pool 33, top 25 -> rows 25..32
    t.setCount(0);
    assert(t.top() == 0 && t.rowsShown() == 0);

    std::printf("test_teamselect: ok\n");
    return 0;
}
