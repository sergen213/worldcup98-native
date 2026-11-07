// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/layout.hpp"
#include "wc98/reader.hpp"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <format>

namespace wc98::formats {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::string upper(std::string_view s) {
    std::string o(s);
    for (char& c : o) if (c >= 'a' && c <= 'z') c = char(c - 32);
    return o;
}

// Removes a ';' comment that is not inside double quotes.
std::string_view stripComment(std::string_view s) {
    bool quoted = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"') quoted = !quoted;
        else if (s[i] == ';' && !quoted) return s.substr(0, i);
    }
    return s;
}

bool applies(const LayoutProperty& p, std::string_view platform) { return p.platform.empty() || iequals(p.platform, platform); }

} // namespace

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return (x >= 'a' && x <= 'z' ? x - 32 : x) == (y >= 'a' && y <= 'z' ? y - 32 : y);
           });
}

LayoutDocument LayoutDocument::parse(std::span<const std::uint8_t> data) {
    std::size_t n = data.size();
    while (n > 0 && data[n - 1] == 0) --n; // tolerate NUL padding
    return parse(std::string_view(reinterpret_cast<const char*>(data.data()), n));
}

LayoutDocument LayoutDocument::parse(std::string_view text) {
    LayoutDocument doc;
    int lineNo = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string_view raw = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++lineNo;
        std::string_view line = trim(raw);
        if (lineNo == 1 && line.starts_with(';')) doc.header = std::string(trim(line.substr(1)));
        line = trim(stripComment(line));
        if (line.empty()) continue;
        LayoutProperty p;
        p.line = lineNo;
        if (line.front() == '{') {
            std::size_t close = line.find('}');
            if (close == std::string_view::npos) {
                doc.warnings.push_back(std::format("line {}: unterminated platform prefix", lineNo));
                continue;
            }
            p.platform = upper(trim(line.substr(1, close - 1)));
            line = trim(line.substr(close + 1));
            if (line.empty()) continue; // "{PSX};-- comment"
        }
        if (line.front() == '[') {
            std::size_t close = line.find(']');
            if (close == std::string_view::npos) {
                doc.warnings.push_back(std::format("line {}: unterminated section header", lineNo));
                continue;
            }
            doc.sections.push_back({std::string(trim(line.substr(1, close - 1))), p.platform, lineNo, {}});
            continue;
        }
        std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || trim(line.substr(0, eq)).empty()) {
            doc.warnings.push_back(std::format("line {}: no key=value: {}", lineNo, line.substr(0, 40)));
            continue;
        }
        p.key = std::string(trim(line.substr(0, eq)));
        std::string_view v = trim(line.substr(eq + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        p.value = std::string(v);
        (doc.sections.empty() ? doc.preamble : doc.sections.back().props).push_back(std::move(p));
    }
    return doc;
}

const LayoutSection* LayoutDocument::section(std::string_view name) const {
    for (const auto& s : sections) if (iequals(s.name, name)) return &s;
    return nullptr;
}

FemsgTable FemsgTable::parse(std::span<const std::uint8_t> data) {
    constexpr std::size_t kRec = 72;
    std::size_t end = data.size();
    if (end >= 8 && std::memcmp(data.data() + end - 8, "\0cRC", 4) == 0) end -= 8;
    if (end == 0 || end % kRec != 0) throw FormatError(std::format("FEMSG: size {} is not a multiple of {}", end, kRec));
    FemsgTable t;
    for (std::size_t o = 0; o < end; o += kRec) {
        Reader r(data, o);
        if (r.u8() != 1) throw FormatError(std::format("FEMSG: record @{} has kind != 1", o));
        std::string name = r.fixedString(67);
        if (name.empty()) throw FormatError(std::format("FEMSG: record @{} has an empty name", o));
        t.symbols.push_back({std::move(name), r.i32le()});
    }
    return t;
}

std::optional<std::int32_t> FemsgTable::value(std::string_view name) const {
    for (const auto& s : symbols) if (iequals(s.name, name)) return s.value;
    return std::nullopt;
}

const char* layoutObjectTypeName(int type) {
    switch (LayoutObjectType(type)) {
    case LayoutObjectType::Image: return "image";
    case LayoutObjectType::Text: return "text";
    case LayoutObjectType::Button: return "button";
    case LayoutObjectType::AnimSeq: return "animseq";
    case LayoutObjectType::List: return "list";
    case LayoutObjectType::Bar: return "bar";
    case LayoutObjectType::ScrollBar: return "scrollbar";
    case LayoutObjectType::Sbar: return "sbar";
    case LayoutObjectType::Animation: return "animation";
    case LayoutObjectType::AnimButton: return "animbutton";
    case LayoutObjectType::Slider: return "slider";
    case LayoutObjectType::CheckBox: return "checkbox";
    case LayoutObjectType::FilledRect: return "filledrect";
    case LayoutObjectType::FrameRect: return "framerect";
    case LayoutObjectType::Edit: return "edit";
    case LayoutObjectType::Formation: return "formation";
    case LayoutObjectType::Bracket: return "bracket";
    case LayoutObjectType::Video: return "video";
    case LayoutObjectType::KnobBar: return "knobbar";
    case LayoutObjectType::Type20: return "type20";
    case LayoutObjectType::Tbb: return "tbb";
    case LayoutObjectType::Flg: return "flg";
    case LayoutObjectType::Menu: return "menu";
    case LayoutObjectType::Box: return "box";
    case LayoutObjectType::Kt: return "kt";
    case LayoutObjectType::Btn: return "btn";
    case LayoutObjectType::SliderBtn: return "sliderbtn";
    case LayoutObjectType::Chat: return "chat";
    case LayoutObjectType::FadeText: return "fadetext";
    }
    return "";
}

std::optional<LayoutMessage> parseLayoutMessage(std::string_view value) {
    std::vector<std::string> f;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= value.size(); ++i)
        if (i == value.size() || value[i] == ',') {
            f.emplace_back(trim(value.substr(start, i - start)));
            start = i + 1;
        }
    while (!f.empty() && f.back().empty()) f.pop_back(); // trailing ", " before a stripped comment
    if (f.size() < 7) return std::nullopt;
    LayoutMessage m{f[0], f[1], f[2], f[3], f[4], f[5], f[6], f.size() > 7 ? f[7] : "", f.size() > 8 ? f[8] : "", ""};
    return m;
}

const std::string* LayoutObject::prop(std::string_view key) const {
    auto it = props.find(upper(key));
    return it == props.end() ? nullptr : &it->second;
}

int LayoutObject::intProp(std::string_view key, int fallback) const {
    const std::string* v = prop(key);
    if (!v) return fallback;
    std::string_view s = trim(*v);
    int out = fallback;
    if (s.starts_with("0x") || s.starts_with("0X")) {
        unsigned u = 0;
        if (std::from_chars(s.data() + 2, s.data() + s.size(), u, 16).ec == std::errc{}) return int(u);
        return fallback;
    }
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} ? out : fallback;
}

const LayoutObject* ScreenLayout::object(std::string_view name) const {
    for (const auto& o : objects) if (iequals(o.name, name)) return &o;
    return nullptr;
}

ScreenLayout ScreenLayout::build(std::string_view name, const LayoutDocument& lay, const LayoutDocument* inc,
                                 const LayoutDocument* vis, std::string_view platform) {
    ScreenLayout s;
    s.name = std::string(name);
    auto apply = [&](const LayoutSection& sec, std::map<std::string, std::string>& props, std::vector<LayoutMessage>& msgs) {
        for (const auto& p : sec.props) {
            if (!applies(p, platform)) continue;
            if (iequals(p.key, "Message")) {
                if (auto m = parseLayoutMessage(p.value)) {
                    m->platform = p.platform;
                    msgs.push_back(std::move(*m));
                } else {
                    s.warnings.push_back(std::format("[{}] line {}: Message with fewer than 7 fields", sec.name, p.line));
                }
            } else {
                props[upper(p.key)] = p.value;
            }
        }
    };
    const LayoutDocument* ext[2] = {inc, vis}; // load order of EXE 0x4ea520: .inc then .vis

    // Screen-level keys: LAY [screen], then [scrn] (or [screen]) in INC and VIS.
    auto sectionApplies = [&](const LayoutSection& sec) { return sec.platform.empty() || iequals(sec.platform, platform); };
    if (const LayoutSection* sc = lay.section("screen")) apply(*sc, s.screenProps, s.screenMessages);
    else s.warnings.push_back("LAY has no [screen] section");
    for (const LayoutDocument* d : ext) {
        if (!d) continue;
        const LayoutSection* sc = d->section("scrn");
        if (!sc) sc = d->section("screen");
        if (sc && sectionApplies(*sc)) apply(*sc, s.screenProps, s.screenMessages);
    }
    if (auto it = s.screenProps.find("ISPOPUP"); it != s.screenProps.end()) s.isPopup = std::atoi(it->second.c_str()) != 0;
    if (auto it = s.screenProps.find("HILITEPANE"); it != s.screenProps.end()) s.hilitePane = it->second;

    for (const auto& sec : lay.sections) {
        std::string_view n = sec.name;
        if (n.size() < 7 || !iequals(n.substr(0, 7), "object ")) continue;
        if (!sectionApplies(sec)) continue;
        LayoutObject o;
        std::string_view num = trim(n.substr(7));
        std::from_chars(num.data(), num.data() + num.size(), o.index);
        apply(sec, o.props, o.messages);
        if (const std::string* on = o.prop("OBJECTNAME")) o.name = *on;
        // EXE 0x4eacd8 returns the first section whose name matches the object id, so a repeated
        // section (MATCHSEL.INC [TRBN], MATCHSEL.VIS [MTTL]) contributes only its first occurrence.
        if (!o.name.empty())
            for (const LayoutDocument* d : ext)
                if (d)
                    for (const auto& es : d->sections)
                        if (iequals(es.name, o.name) && sectionApplies(es)) {
                            apply(es, o.props, o.messages);
                            break;
                        }
        // EXE 0x4ea8c8..0x4ea8fa: the class id is the integer value of the section's first key,
        // whatever its name ("objecttype=" in LayMagic output, "type =" in the hand-written TEST1).
        for (const auto& p : sec.props)
            if (applies(p, platform)) {
                std::string_view v = trim(p.value);
                int t = -1;
                if (std::from_chars(v.data(), v.data() + v.size(), t).ec == std::errc{}) o.type = t;
                break;
            }
        o.x = o.intProp("XPOSITION", 0);
        o.y = o.intProp("YPOSITION", 0);
        o.z = o.intProp("ZPOSITION", 0);
        o.width = o.intProp("WIDTH", 0);
        o.height = o.intProp("HEIGHT", 0);
        if (const std::string* v = o.prop("FNAME")) o.fname = *v;
        if (const std::string* v = o.prop("SNAME")) o.sname = *v;
        if (o.type < 0) s.warnings.push_back(std::format("[{}] first key is not an object type", sec.name));
        else if (!*layoutObjectTypeName(o.type)) s.warnings.push_back(std::format("[{}] unknown objecttype {}", sec.name, o.type));
        s.objects.push_back(std::move(o));
    }
    if (auto it = s.screenProps.find("NUMOBJECTS"); it != s.screenProps.end() && std::atoi(it->second.c_str()) != int(s.objects.size()))
        s.warnings.push_back(std::format("numobjects={} but {} [object] sections", it->second, s.objects.size()));
    return s;
}

} // namespace wc98::formats
