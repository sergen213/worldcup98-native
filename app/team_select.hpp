#pragma once
// FE team-select paging model (app/fe_flow.cpp draws it; ZWCUP.RES pool = 40 entries -> 5 pages
// of 8 rows, FCDB STR0 names). Pure logic, no tree/render deps; gate = tests/unit/test_teamselect.cpp.
namespace wc98::app {

struct TeamListNav {
    static constexpr int kRows = 8; // page size (contract: 5 pages x 8 for the 40-team pool)
    int count = 0, sel = 0;

    void setCount(int n) {
        count = n < 0 ? 0 : n;
        if (sel >= count) sel = 0;
    }
    void move(int d) {
        if (!count) return;
        sel = ((sel + d) % count + count) % count; // cursor wraps through page boundaries
    }
    [[nodiscard]] int page() const { return count ? sel / kRows : 0; }
    [[nodiscard]] int pageCount() const { return count ? (count + kRows - 1) / kRows : 1; }
};

} // namespace wc98::app
