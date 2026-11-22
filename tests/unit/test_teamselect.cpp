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
using wc98::app::TeamPickPool;

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

    // ---- pool pick state (fun_0044d9c8 tm%02d click grammar [C]) ----
    TeamPickPool p;
    p.resize(40);
    assert(p.stateOf(0) == 0 && p.p1Count() == 0 && p.selCount() == 0 && p.firstP1() < 0);
    assert(p.click(-1) == false && p.click(4000) == false); // bounds: no change
    assert(p.click(5) && p.stateOf(5) == 1 && p.p1Count() == 1 && p.selCount() == 1 && p.firstP1() == 5);
    assert(p.click(5) && p.stateOf(5) == 2 && p.p1Count() == 0 && p.selCount() == 1 && p.firstP1() < 0);
    assert(p.click(5) && p.stateOf(5) == 0 && p.selCount() == 0); // full cycle back
    for (int i = 0; i < 31; ++i) assert(p.click(i)); // 31 picks stay under the cap
    assert(p.selCount() == 31 && p.p1Count() == 31);
    assert(p.click(31) == false); // 32nd pick: fun applies then reverts (0x1f < bc4)
    assert(p.stateOf(31) == 0 && p.selCount() == 31);
    assert(p.click(7) && p.stateOf(7) == 2 && p.p1Count() == 30 && p.selCount() == 31); // P2 takeover
    p.resize(8); // pool rebuild zeroes the walk (fun_0044dda0 scan lane)
    assert(p.selCount() == 0 && p.p1Count() == 0 && p.stateOf(0) == 0);

    std::printf("test_teamselect: ok\n");
    return 0;
}
