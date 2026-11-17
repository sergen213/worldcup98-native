// "[multitext]" section-blob parser; see wc98/formats/multitext.hpp.
#include "wc98/formats/multitext.hpp"
#include "wc98/reader.hpp" // FormatError

namespace wc98::formats {

namespace {
constexpr std::string_view kMarker = "[multitext]";
constexpr std::string_view kSection = "[section]";
} // namespace

Multitext Multitext::parse(std::string_view blob) {
    if (!blob.starts_with(kMarker))
        throw FormatError("multitext: missing [multitext] header");
    std::size_t p = kMarker.size();
    auto skipWs = [&] {
        while (p < blob.size() && (blob[p] == '\n' || blob[p] == '\r' || blob[p] == ' ')) ++p;
    };
    auto readLine = [&](std::string_view key, std::uint32_t& out) {
        skipWs();
        if (blob.substr(p).starts_with(key))
            p += key.size();
        else
            throw FormatError(std::format("multitext: expected {} at offset {}", key, p));
        std::size_t ds = p;
        while (ds < blob.size() && blob[ds] >= '0' && blob[ds] <= '9') ++ds;
        if (ds == p) throw FormatError(std::format("multitext: {} has no number", key));
        out = 0;
        for (std::size_t k = p; k < ds; ++k) out = out * 10u + std::uint32_t(blob[k] - '0');
        p = ds;
    };
    Multitext m;
    readLine("numsections=", m.numsections);
    readLine("bufsize=", m.bufsize);
    if (m.numsections == 0 || m.bufsize == 0)
        throw FormatError(std::format("multitext: numsections={} bufsize={}", m.numsections, m.bufsize));
    std::vector<std::size_t> marks;
    for (std::size_t q = blob.find(kSection); q != std::string_view::npos;
         q = blob.find(kSection, q + kSection.size()))
        marks.push_back(q);
    if (marks.size() != m.numsections)
        throw FormatError(std::format("multitext: {} markers, numsections={}", marks.size(), m.numsections));
    for (std::size_t k = 0; k < marks.size(); ++k) {
        const std::size_t body = marks[k] + kSection.size();
        const std::size_t end = k + 1 < marks.size() ? marks[k + 1] : blob.size();
        if (body >= end)
            throw FormatError(std::format("multitext: empty section {}", k));
        m.sections.push_back(blob.substr(body, end - body));
    }
    return m;
}

} // namespace wc98::formats
