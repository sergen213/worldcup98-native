// Derived from fifa99-native (same author), ported for World Cup 98
// wc98-exeinfo: inventory of Win32 PE files (FIFA99.EXE, THRASH DLLs, EACSND.DLL).
//   wc98-exeinfo info <file>             headers, sections, version, resources, relocations
//   wc98-exeinfo imports <file>          imports per DLL with IAT VA
//   wc98-exeinfo exports <file>          exports with VA
//   wc98-exeinfo strings <file> [min]    printable ASCII strings with offset, section and VA
//   wc98-exeinfo compare <a> <b>         header/section/import diff and byte-diff runs
#include "wc98/file.hpp"
#include "wc98/formats/pe.hpp"
#include <algorithm>
#include <cstdio>
#include <exception>
#include <cstdio>
#include <format>
#include <set>
#include <string>

template <class... A> void println(std::format_string<A...> f, A&&... a) { auto s = std::format(f, std::forward<A>(a)...); std::fwrite(s.data(), 1, s.size(), stdout); std::putc('\n', stdout); }
template <class... A> void eprintln(std::format_string<A...> f, A&&... a) { auto s = std::format(f, std::forward<A>(a)...); std::fwrite(s.data(), 1, s.size(), stderr); std::putc('\n', stderr); }

using wc98::formats::PeImage;
using wc98::formats::PeSection;

namespace {

std::string symName(const wc98::formats::PeImportSymbol& s) {
    return s.ordinal ? std::format("#{}", *s.ordinal) : s.name;
}

std::string where(const PeImage& img, std::uint32_t off) {
    const PeSection* s = img.sectionForOffset(off);
    if (!s) return off < img.sizeOfHeaders ? "(headers)" : "(overlay)";
    return std::format("{:<8} VA {:08x}", s->name, img.imageBase + s->virtualAddress + (off - s->rawOffset));
}

void info(const PeImage& img) {
    println("machine {:#06x}  characteristics {:#06x}  timestamp {:#010x}  magic {:#x}", img.machine, img.characteristics, img.timeDateStamp, img.magic);
    println("linker {}.{}  os {}.{}  subsystem {} ({}.{})  image base {:#x}  entry VA {:#x}", img.linkerMajor, img.linkerMinor,
                 img.osMajor, img.osMinor, img.subsystem, img.subsystemMajor, img.subsystemMinor, img.imageBase, img.imageBase + img.entryRva);
    println("section align {:#x}  file align {:#x}  size of image {:#x}  headers {:#x}  checksum {:#x}", img.sectionAlignment,
                 img.fileAlignment, img.sizeOfImage, img.sizeOfHeaders, img.checksum);
    static const char* dirNames[16] = {"export", "import", "resource", "exception", "security", "basereloc", "debug", "arch",
                                       "globalptr", "tls", "loadconfig", "boundimport", "iat", "delayimport", "clr", "reserved"};
    for (int i = 0; i < 16; ++i)
        if (img.dataDirs[i].first) println("  dir {:<12} rva {:#010x} size {:#x}", dirNames[i], img.dataDirs[i].first, img.dataDirs[i].second);
    println("sections:");
    for (auto& s : img.sections)
        println("  {:<8} VA {:08x} vsize {:08x} raw {:08x}+{:08x} chars {:08x}", s.name, img.imageBase + s.virtualAddress, s.virtualSize,
                     s.rawOffset, s.rawSize, s.characteristics);
    std::size_t nsym = 0;
    for (auto& d : img.imports) nsym += d.symbols.size();
    println("imports: {} DLLs, {} symbols", img.imports.size(), nsym);
    for (auto& d : img.imports) println("  {} ({})", d.dll, d.symbols.size());
    if (!img.exportName.empty() || !img.exports.empty()) println("exports: {} ({} entries)", img.exportName, img.exports.size());
    println("relocations: {} blocks, {} entries", img.relocs.blocks, img.relocs.entries);
    for (auto [t, n] : img.relocs.byType) println("  type {}: {}", t, n);
    println("resources: {}", img.resources.size());
    for (auto& r : img.resources) println("  type {:<6} name {:<12} lang {:<6} rva {:#x} size {}", r.type, r.name, r.lang, r.dataRva, r.size);
    if (img.fileVersion) println("file version {}.{}.{}.{}", (*img.fileVersion)[0], (*img.fileVersion)[1], (*img.fileVersion)[2], (*img.fileVersion)[3]);
    for (auto& [k, v] : img.versionStrings) println("  {} = {}", k, v);
    for (auto& w : img.warnings) println("warning: {}", w);
}

void strings(std::span<const std::uint8_t> d, const PeImage& img, std::size_t minLen) {
    for (std::size_t i = 0; i < d.size();) {
        std::size_t j = i;
        while (j < d.size() && ((d[j] >= 0x20 && d[j] < 0x7F) || d[j] == '\t')) ++j;
        if (j - i >= minLen)
            println("{:08x} {} {}", i, where(img, std::uint32_t(i)), std::string_view(reinterpret_cast<const char*>(d.data() + i), j - i));
        i = j + 1;
    }
}

void compare(std::span<const std::uint8_t> a, const PeImage& A, std::span<const std::uint8_t> b, const PeImage& B) {
    auto field = [](const char* n, std::uint64_t x, std::uint64_t y) { if (x != y) println("header {}: {:#x} -> {:#x}", n, x, y); };
    field("timestamp", A.timeDateStamp, B.timeDateStamp);
    field("entry", A.entryRva, B.entryRva);
    field("imagebase", A.imageBase, B.imageBase);
    field("sizeofimage", A.sizeOfImage, B.sizeOfImage);
    field("checksum", A.checksum, B.checksum);
    field("linker", A.linkerMajor * 256 + A.linkerMinor, B.linkerMajor * 256 + B.linkerMinor);
    field("sections", A.sections.size(), B.sections.size());
    for (std::size_t i = 0; i < std::max(A.sections.size(), B.sections.size()); ++i) {
        auto s = [](const PeImage& p, std::size_t k) {
            if (k >= p.sections.size()) return std::string("-");
            auto& x = p.sections[k];
            return std::format("{} va {:x} vsz {:x} raw {:x}+{:x}", x.name, x.virtualAddress, x.virtualSize, x.rawOffset, x.rawSize);
        };
        if (s(A, i) != s(B, i)) println("section {}: {}  ->  {}", i, s(A, i), s(B, i));
    }
    auto syms = [](const PeImage& p) {
        std::set<std::string> out;
        for (auto& d : p.imports) for (auto& s : d.symbols) out.insert(d.dll + "!" + symName(s));
        return out;
    };
    auto sa = syms(A), sb = syms(B);
    for (auto& s : sa) if (!sb.contains(s)) println("import removed: {}", s);
    for (auto& s : sb) if (!sa.contains(s)) println("import added:   {}", s);
    if (a.size() != b.size()) println("file size {} -> {} (byte runs compared over common prefix)", a.size(), b.size());
    std::size_t n = std::min(a.size(), b.size()), runs = 0, bytes = 0;
    for (std::size_t i = 0; i < n;) {
        if (a[i] == b[i]) { ++i; continue; }
        std::size_t j = i;
        while (j < n && a[j] != b[j]) ++j;
        bytes += j - i;
        if (++runs <= 200) {
            std::string ha, hb;
            for (std::size_t k = i; k < std::min(j, i + 16); ++k) { ha += std::format("{:02x}", a[k]); hb += std::format("{:02x}", b[k]); }
            println("diff {:08x} len {:<6} {}  {} -> {}", i, j - i, where(A, std::uint32_t(i)), ha, hb);
        }
        i = j;
    }
    println("{} differing bytes in {} runs", bytes, runs);
}

} // namespace

int main(int argc, char** argv) try {
    if (argc < 3) {
        eprintln("usage: wc98-exeinfo info|imports|exports|strings|compare <file> [min|<file2>]");
        return 2;
    }
    std::string cmd = argv[1];
    auto data = wc98::readFile(argv[2]);
    auto img = PeImage::parse(data);
    if (cmd == "info") info(img);
    else if (cmd == "imports") {
        for (auto& d : img.imports)
            for (auto& s : d.symbols) println("{:08x} {}!{}", img.imageBase + s.iatRva, d.dll, symName(s));
    } else if (cmd == "exports") {
        for (auto& e : img.exports)
            println("{:5} {:08x} {}{}", e.ordinal, img.imageBase + e.rva, e.name.empty() ? "(ordinal)" : e.name, e.forwarder.empty() ? "" : " -> " + e.forwarder);
    } else if (cmd == "strings") strings(data, img, argc > 3 ? std::stoul(argv[3]) : 6);
    else if (cmd == "compare" && argc > 3) {
        auto other = wc98::readFile(argv[3]);
        compare(data, img, other, PeImage::parse(other));
    } else { eprintln("unknown command {}", cmd); return 2; }
    return 0;
} catch (const std::exception& e) {
    eprintln("error: {}", e.what());
    return 1;
}
