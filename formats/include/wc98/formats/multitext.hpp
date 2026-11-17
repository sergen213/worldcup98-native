#pragma once
// "[multitext]" section blobs carried by FE/TEAMHIST.ENG/*.HIS cells (see his.hpp; token
// grammar painted by presentation::paintMultitext, layout rules in docs/asset-formats/
// MULTITEXT.md). Grammar, measured on all 8x120 shipped cells:
//   "[multitext]\nnumsections=N\nbufsize=B\n\n[section]\n<body>"
// with one "[section]" line per section, body to the next marker or EOF.
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace wc98::formats {

struct Multitext {
    std::uint32_t numsections = 0; // declared count; must equal the split count
    std::uint32_t bufsize = 0;     // per-section text buffer size (measured 166..1315)
    std::vector<std::string_view> sections; // raw body spans into the input

    // Validates the header, the numsections/marker agreement, and that a body exists.
    // Throws FormatError. `blob` must outlive this Multitext.
    static Multitext parse(std::string_view blob);
};

} // namespace wc98::formats
