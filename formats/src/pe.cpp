// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/pe.hpp"
#include "wc98/reader.hpp"
#include <format>
#include <functional>

namespace wc98::formats {
namespace {

constexpr std::uint32_t kExportDir = 0, kImportDir = 1, kResourceDir = 2, kRelocDir = 5;

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out.push_back(char(cp));
    else if (cp < 0x800) { out.push_back(char(0xC0 | (cp >> 6))); out.push_back(char(0x80 | (cp & 0x3F))); }
    else { out.push_back(char(0xE0 | (cp >> 12))); out.push_back(char(0x80 | ((cp >> 6) & 0x3F))); out.push_back(char(0x80 | (cp & 0x3F))); }
}

// NUL-terminated UTF-16LE string at the reader position (BMP only; surrogates copied raw).
std::string utf16z(Reader& r) {
    std::string out;
    for (std::uint16_t c; (c = r.u16le()) != 0;) appendUtf8(out, c);
    return out;
}

// Advance to the next 4-byte boundary; the final node of a resource may end unpadded.
void align4(Reader& r) {
    r.seek(std::min((r.pos() + 3) & ~std::size_t(3), r.size()));
}

} // namespace

bool PeImage::isPe(std::span<const std::uint8_t> d) {
    if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z') return false;
    std::uint32_t off = d[0x3C] | d[0x3D] << 8 | d[0x3E] << 16 | std::uint32_t(d[0x3F]) << 24;
    return off <= d.size() - 4 && d[off] == 'P' && d[off + 1] == 'E' && d[off + 2] == 0 && d[off + 3] == 0;
}

const PeSection* PeImage::sectionForRva(std::uint32_t rva) const {
    for (auto& s : sections) {
        std::uint32_t span = s.memorySize();
        if (rva >= s.virtualAddress && rva - s.virtualAddress < span) return &s;
    }
    return nullptr;
}

const PeSection* PeImage::sectionForOffset(std::uint32_t off) const {
    for (auto& s : sections)
        if (s.hasFileData() && off >= s.rawOffset && off - s.rawOffset < s.rawSize) return &s;
    return nullptr;
}

std::optional<std::uint32_t> PeImage::rvaToOffset(std::uint32_t rva) const {
    if (rva < sizeOfHeaders) return rva;
    const PeSection* s = sectionForRva(rva);
    if (!s || !s->hasFileData() || rva - s->virtualAddress >= s->rawSize) return std::nullopt; // in .bss-like tail
    return s->rawOffset + (rva - s->virtualAddress);
}

PeImage PeImage::parse(std::span<const std::uint8_t> data) {
    if (!isPe(data)) throw FormatError("not a PE image (missing MZ/PE signature)");
    PeImage img;
    Reader r(data, 0x3C);
    img.peOffset = r.u32le();
    r.seek(img.peOffset + 4);
    img.machine = r.u16le();
    img.numberOfSections = r.u16le();
    img.timeDateStamp = r.u32le();
    r.skip(8); // symbol table pointer + count (unused in images)
    std::uint16_t optSize = r.u16le();
    img.characteristics = r.u16le();
    const std::size_t optStart = r.pos();
    img.magic = r.u16le();
    if (img.magic != 0x10b && img.magic != 0x20b) throw FormatError(std::format("unknown optional header magic {:#x}", img.magic));
    img.linkerMajor = r.u8();
    img.linkerMinor = r.u8();
    img.sizeOfCode = r.u32le();
    r.skip(8); // initialized/uninitialized data sizes
    img.entryRva = r.u32le();
    r.skip(4); // BaseOfCode
    if (img.is64()) img.imageBase = std::uint64_t(r.u32le()) | std::uint64_t(r.u32le()) << 32;
    else { r.skip(4); img.imageBase = r.u32le(); } // BaseOfData, ImageBase
    img.sectionAlignment = r.u32le();
    img.fileAlignment = r.u32le();
    img.osMajor = r.u16le(); img.osMinor = r.u16le();
    r.skip(4); // image version
    img.subsystemMajor = r.u16le(); img.subsystemMinor = r.u16le();
    r.skip(4); // Win32VersionValue
    img.sizeOfImage = r.u32le();
    img.sizeOfHeaders = r.u32le();
    img.checksum = r.u32le();
    img.subsystem = r.u16le();
    img.dllCharacteristics = r.u16le();
    r.skip(img.is64() ? 32 : 16); // stack/heap reserve+commit
    r.skip(4);                    // LoaderFlags
    std::uint32_t nDirs = r.u32le();
    for (std::uint32_t i = 0; i < std::min<std::uint32_t>(nDirs, 16); ++i) img.dataDirs[i] = {r.u32le(), r.u32le()};
    if (r.pos() > optStart + optSize) throw FormatError("optional header larger than SizeOfOptionalHeader");

    r.seek(optStart + optSize);
    for (std::uint16_t i = 0; i < img.numberOfSections; ++i) {
        PeSection s;
        s.name = r.fixedString(8);
        s.virtualSize = r.u32le();
        s.virtualAddress = r.u32le();
        s.rawSize = r.u32le();
        s.rawOffset = r.u32le();
        r.skip(12); // relocation/line-number pointers and counts
        s.characteristics = r.u32le();
        if (s.hasFileData() && (s.rawOffset > data.size() || s.rawSize > data.size() - s.rawOffset))
            throw FormatError(std::format("section {} raw data {:#x}+{:#x} beyond file end", s.name, s.rawOffset, s.rawSize));
        img.sections.push_back(std::move(s));
    }

    auto at = [&](std::uint32_t rva) -> Reader {
        auto off = img.rvaToOffset(rva);
        if (!off) throw FormatError(std::format("RVA {:#x} has no file backing", rva));
        return Reader(data, *off);
    };
    auto stringAt = [&](std::uint32_t rva) { auto rr = at(rva); return rr.cString(); };
    auto guarded = [&](const char* what, auto&& fn) {
        try { fn(); } catch (const FormatError& e) { img.warnings.push_back(std::format("{}: {}", what, e.what())); }
    };

    if (auto [rva, size] = img.dataDirs[kImportDir]; rva) guarded("imports", [&] {
        for (std::uint32_t d = rva;; d += 20) {
            auto rr = at(d);
            std::uint32_t ilt = rr.u32le(); rr.skip(8);
            std::uint32_t nameRva = rr.u32le(), iat = rr.u32le();
            if (!ilt && !nameRva && !iat) break;
            if (img.imports.size() > 4096) throw FormatError("unterminated import descriptor table");
            PeImportDll dll{stringAt(nameRva), {}};
            std::uint32_t thunk = ilt ? ilt : iat, step = img.is64() ? 8 : 4;
            for (std::uint32_t k = 0;; ++k) {
                auto tr = at(thunk + k * step);
                std::uint64_t v = img.is64() ? std::uint64_t(tr.u32le()) | std::uint64_t(tr.u32le()) << 32 : tr.u32le();
                if (!v) break;
                if (k > 65536) throw FormatError("unterminated import thunk list");
                PeImportSymbol sym;
                sym.iatRva = iat + k * step;
                if (v >> (img.is64() ? 63 : 31)) sym.ordinal = std::uint16_t(v);
                else { auto nr = at(std::uint32_t(v)); sym.hint = nr.u16le(); sym.name = nr.cString(); }
                dll.symbols.push_back(std::move(sym));
            }
            img.imports.push_back(std::move(dll));
        }
    });

    if (auto [rva, size] = img.dataDirs[kExportDir]; rva) guarded("exports", [&] {
        auto rr = at(rva);
        rr.skip(12);
        std::uint32_t nameRva = rr.u32le(), base = rr.u32le(), nFuncs = rr.u32le(), nNames = rr.u32le();
        std::uint32_t funcs = rr.u32le(), names = rr.u32le(), ords = rr.u32le();
        if (nFuncs > 65536 || nNames > nFuncs) throw FormatError("implausible export counts");
        img.exportName = stringAt(nameRva);
        std::vector<std::string> nameOf(nFuncs);
        for (std::uint32_t i = 0; i < nNames; ++i) {
            std::uint32_t nr = at(names + i * 4).u32le();
            std::uint16_t idx = at(ords + i * 2).u16le();
            if (idx >= nFuncs) throw FormatError("export name ordinal out of range");
            nameOf[idx] = stringAt(nr);
        }
        for (std::uint32_t i = 0; i < nFuncs; ++i) {
            std::uint32_t frva = at(funcs + i * 4).u32le();
            if (!frva) continue;
            PeExport e{base + i, std::move(nameOf[i]), frva, {}};
            if (frva >= rva && frva - rva < size) e.forwarder = stringAt(frva);
            img.exports.push_back(std::move(e));
        }
    });

    if (auto [rva, size] = img.dataDirs[kResourceDir]; rva) guarded("resources", [&] {
        auto base = img.rvaToOffset(rva);
        if (!base) throw FormatError("resource directory has no file backing");
        auto label = [&](std::uint32_t v) -> std::string {
            if (!(v & 0x80000000u)) return std::to_string(v);
            Reader nr(data, *base + (v & 0x7FFFFFFFu));
            std::uint16_t len = nr.u16le();
            std::string s;
            for (std::uint16_t i = 0; i < len; ++i) appendUtf8(s, nr.u16le());
            return s;
        };
        std::function<void(std::uint32_t, int, std::array<std::string, 3>&)> walk =
            [&](std::uint32_t dirOff, int level, std::array<std::string, 3>& path) {
                if (level > 2) throw FormatError("resource tree deeper than type/name/lang");
                Reader dr(data, *base + dirOff);
                dr.skip(12);
                std::uint32_t n = std::uint32_t(dr.u16le()) + dr.u16le();
                for (std::uint32_t i = 0; i < n; ++i) {
                    std::uint32_t id = dr.u32le(), target = dr.u32le();
                    path[level] = label(id);
                    if (target & 0x80000000u) {
                        walk(target & 0x7FFFFFFFu, level + 1, path);
                    } else {
                        Reader er(data, *base + target);
                        PeResource res{path[0], level >= 1 ? path[1] : "", level >= 2 ? path[2] : "", er.u32le(), er.u32le(), er.u32le()};
                        img.resources.push_back(std::move(res));
                    }
                }
            };
        std::array<std::string, 3> path;
        walk(0, 0, path);
    });

    if (auto [rva, size] = img.dataDirs[kRelocDir]; rva) guarded("relocations", [&] {
        for (std::uint32_t p = 0; p + 8 <= size;) {
            auto rr = at(rva + p);
            rr.u32le(); // page RVA
            std::uint32_t blockSize = rr.u32le();
            if (blockSize < 8) break; // zero padding terminates the table
            if (blockSize > size - p) throw FormatError("relocation block exceeds directory");
            ++img.relocs.blocks;
            for (std::uint32_t k = 0; k < (blockSize - 8) / 2; ++k) {
                std::uint8_t type = std::uint8_t(rr.u16le() >> 12);
                if (type == 0) continue; // IMAGE_REL_BASED_ABSOLUTE padding
                ++img.relocs.entries;
                ++img.relocs.byType[type];
            }
            p += blockSize;
        }
    });

    for (const auto& res : img.resources) {
        if (res.type != "16") continue; // RT_VERSION
        guarded("version", [&] {
            auto off = img.rvaToOffset(res.dataRva);
            if (!off || res.size > data.size() - *off) throw FormatError("version resource out of file");
            auto blob = data.subspan(*off, res.size);
            // VS_VERSIONINFO node: wLength, wValueLength, wType(1=text), key, pad, value, pad, children.
            std::function<void(std::size_t, int, bool)> node = [&](std::size_t start, int depth, bool inStrings) {
                if (depth > 4) throw FormatError("version tree too deep");
                Reader nr(blob, start);
                std::uint16_t len = nr.u16le(), valLen = nr.u16le(), type = nr.u16le();
                if (len < 6 || start + len > blob.size()) throw FormatError("bad version node length");
                std::string key = utf16z(nr);
                align4(nr);
                if (depth == 0 && valLen >= 52) {
                    if (nr.u32le() != 0xFEEF04BDu) throw FormatError("VS_FIXEDFILEINFO signature mismatch");
                    nr.u32le();
                    std::uint32_t ms = nr.u32le(), ls = nr.u32le();
                    img.fileVersion = std::array<std::uint16_t, 4>{std::uint16_t(ms >> 16), std::uint16_t(ms), std::uint16_t(ls >> 16), std::uint16_t(ls)};
                    nr.seek(nr.pos() - 16 + valLen);
                } else if (inStrings && depth == 3) {
                    std::string value;
                    if (type == 1 && valLen) {
                        Reader vr(blob.first(start + len), nr.pos());
                        for (std::uint16_t i = 0; i < valLen && vr.remaining() >= 2; ++i) {
                            std::uint16_t c = vr.u16le();
                            if (!c) break;
                            appendUtf8(value, c);
                        }
                    }
                    img.versionStrings.emplace_back(std::move(key), std::move(value));
                    return;
                } else {
                    nr.skip(type == 1 ? std::size_t(valLen) * 2 : valLen);
                }
                for (align4(nr); nr.pos() + 6 <= start + len; align4(nr)) {
                    std::size_t child = nr.pos();
                    std::uint16_t clen = Reader(blob, child).u16le();
                    if (clen == 0) break;
                    node(child, depth + 1, inStrings || key == "StringFileInfo");
                    nr.seek(child + clen);
                }
            };
            node(0, 0, false);
        });
        break;
    }
    return img;
}

} // namespace wc98::formats
