#pragma once

#include <algorithm>
#include <cstdint>
#include <array>
#include <vector>

namespace wc98::app {

// Team-select pool ordering and cursor (FEFLOW11 grid).
//
// The engine fills the FE11 pool from ZWCUP.RES (tournament qualification order,
// analysis/pass11team/fe11-pool.txt) and then FUN_0044b618 [C, 0x44b618] runs a
// stable partition over the 40-entry id array: the eight non-qualified teams
// {87 Australia, 62 Canada, 64 China PR, 41 Greece, 31 Rep. of Ireland,
// 29 Portugal, 33 Russia, 39 Sweden} are moved out of the list and re-appended
// in appearance order at the tail. Rows 0..31 are therefore the 32 qualified
// teams in World-Cup order (Argentina..Yugoslavia), rows 32..39 the eight extra
// teams — the original board groups them last; the unpartitioned ZWCUP order
// interleaves Australia/Canada/China/Greece into the qualified block.
inline constexpr std::array<int, 8> kSpecialTeamIds{87, 62, 64, 41, 29, 31, 39, 33};

// Indices of `ids` in engine display order: non-special ids in original order,
// then the special ids in original (ZWCUP) order (FUN_0044b618 [C]).
inline std::vector<int> poolOrder(const std::vector<int>& ids) {
    std::vector<int> head, tail;
    head.reserve(ids.size());
    tail.reserve(kSpecialTeamIds.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (std::find(kSpecialTeamIds.begin(), kSpecialTeamIds.end(), ids[i]) !=
            kSpecialTeamIds.end())
            tail.push_back(int(i));
        else
            head.push_back(int(i));
    }
    head.insert(head.end(), tail.begin(), tail.end());
    return head;
}

// Cursor + scroll window for the grid. The engine drives the row window through
// the sbar slider as a CONTINUOUS top-row offset (FUN_0044db47 [C] top = slider
// value + 1 into DAT_00608bd0), not fixed pages: the thumb tracks
// (top / (count - kRows)) and the window follows the cursor. Empty count stays
// inert (no movement, window 0).
class TeamListNav {
public:
    static constexpr int kRows = 8; // FEFLOW11.LAY stf0..stf7 row slots

    void setCount(int n) {
        count = n < 0 ? 0 : n;
        if (sel >= count) sel = 0;
        clamp();
    }
    // Cursor step with wrap (Up/Down through the pool boundaries).
    void move(int delta) {
        if (!count) return;
        sel = ((sel + delta) % count + count) % count;
        clamp();
    }
    [[nodiscard]] int top() const { return top_; }
    // Rows to paint: the window from top, clipped to the pool end.
    [[nodiscard]] int rowsShown() const {
        if (!count) return 0;
        const int r = count - top_;
        return r < kRows ? r : kRows;
    }
    // Thumb denominator: number of distinct window positions.
    [[nodiscard]] int scrollRange() const { return count > kRows ? count - kRows : 1; }
    int sel = 0;

private:
    void clamp() {
        if (count <= kRows) { top_ = 0; return; }
        if (sel < top_) top_ = sel;
        if (sel >= top_ + kRows) top_ = sel - kRows + 1;
    }
    int count = 0;
    int top_ = 0;
};

// Per-team pool selection state for the FEFLOW11 grid (fun_0044d9c8 click grammar,
// analysis/pass13team/fe11_handlers.txt [C]). The engine keeps the state in the
// stride-4 pool record byte +3 (DAT_00608af5 walk); clicking the row button tm%02d
// cycles: 0 ->1 (select for P1: P1++ total++), 1->2 (P2 takes the team from P1:
// P1-- total unchanged), 2 ->0 (deselect: total--). The click handler applies the
// change first, then reverts + plays the click sfx if the total selected exceeds
// 31 (0x1f, DAT_00608bc4) — the 32-team cap.
class TeamPickPool {
public:
    void resize(std::size_t n) { state.assign(n, 0); }
    void clear() { state.clear(); }
    [[nodiscard]] int stateOf(int i) const {
        return i >= 0 && std::size_t(i) < state.size() ? int(state[std::size_t(i)]) : 0;
    }
    // Mirror of the row-button click. False = click refused (pool bounds or the
    // >31 cap, which the engine reverts and acknowledges with the click sfx).
    bool click(int i) {
        if (i < 0 || std::size_t(i) >= state.size()) return false;
        const std::uint8_t old = state[std::size_t(i)];
        state[std::size_t(i)] = std::uint8_t(old == 0 ? 1 : old == 1 ? 2 : 0);
        if (const int total = selCount(); total > 0x1f) {
            state[std::size_t(i)] = old;
            return false;
        }
        return true;
    }
    [[nodiscard]] int p1Count() const { // DAT_00608bc0 [C]: count of state-1 entries
        int n = 0;
        for (const std::uint8_t b : state)
            if (b == 1) ++n;
        return n;
    }
    [[nodiscard]] int selCount() const { // DAT_00608bc4 [C]: count of state in {1,2}
        int n = 0;
        for (const std::uint8_t b : state)
            if (b) ++n;
        return n;
    }
    [[nodiscard]] int firstP1() const { // 'fe18' commit source: first state-1 row
        for (std::size_t i = 0; i < state.size(); ++i)
            if (state[i] == 1) return int(i);
        return -1;
    }

private:
    std::vector<std::uint8_t> state;
};

} // namespace wc98::app
