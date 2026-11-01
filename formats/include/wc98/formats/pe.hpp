// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Win32 Portable Executable (PE32/PE32+) reader for the original FIFA 99 executable and DLLs.
// Read-only research parser: headers, sections, imports, exports, resources, relocation
// summary and VS_VERSIONINFO strings. See docs/reverse/EXECUTABLE.md.
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace wc98::formats {

struct PeSection {
    std::string name; // 8-byte name, NUL-trimmed
    std::uint32_t virtualAddress = 0, virtualSize = 0, rawOffset = 0, rawSize = 0, characteristics = 0;
    // Watcom wlink stores VirtualSize 0 and puts the .bss size in SizeOfRawData with
    // PointerToRawData 0, so a zero raw offset means "no file data".
    [[nodiscard]] bool hasFileData() const { return rawSize && rawOffset; }
    [[nodiscard]] std::uint32_t memorySize() const { return virtualSize ? virtualSize : rawSize; }
};

struct PeImportSymbol {
    std::optional<std::uint16_t> ordinal; // set for import-by-ordinal
    std::string name;                     // empty for import-by-ordinal
    std::uint16_t hint = 0;
    std::uint32_t iatRva = 0;             // RVA of the IAT slot the loader patches
};

struct PeImportDll {
    std::string dll;
    std::vector<PeImportSymbol> symbols;
};

struct PeExport {
    std::uint32_t ordinal = 0; // biased (as used by GetProcAddress)
    std::string name;          // empty when exported by ordinal only
    std::uint32_t rva = 0;
    std::string forwarder;     // non-empty when the RVA points inside the export directory
};

struct PeResource {
    // Each level is either a numeric id or a UTF-16 name converted to ASCII-ish UTF-8.
    std::string type, name, lang;
    std::uint32_t dataRva = 0, size = 0, codepage = 0;
};

struct PeRelocSummary {
    std::uint32_t blocks = 0, entries = 0;
    std::map<std::uint8_t, std::uint32_t> byType; // IMAGE_REL_BASED_* -> count
};

struct PeImage {
    std::uint32_t peOffset = 0;
    std::uint16_t machine = 0, numberOfSections = 0, characteristics = 0, magic = 0, subsystem = 0, dllCharacteristics = 0;
    std::uint32_t timeDateStamp = 0;
    std::uint8_t linkerMajor = 0, linkerMinor = 0;
    std::uint16_t osMajor = 0, osMinor = 0, subsystemMajor = 0, subsystemMinor = 0;
    std::uint32_t sizeOfCode = 0, entryRva = 0, sectionAlignment = 0, fileAlignment = 0, sizeOfImage = 0,
                  sizeOfHeaders = 0, checksum = 0;
    std::uint64_t imageBase = 0;
    std::array<std::pair<std::uint32_t, std::uint32_t>, 16> dataDirs{}; // (rva, size)

    std::vector<PeSection> sections;
    std::vector<PeImportDll> imports;
    std::string exportName;
    std::vector<PeExport> exports;
    std::vector<PeResource> resources;
    PeRelocSummary relocs;
    std::vector<std::pair<std::string, std::string>> versionStrings; // StringFileInfo key/value
    std::optional<std::array<std::uint16_t, 4>> fileVersion;         // VS_FIXEDFILEINFO
    std::vector<std::string> warnings; // non-fatal oddities (e.g. directories in packed images)

    // Throws wc98::FormatError on malformed DOS/PE/section headers. Directory parsing that
    // fails on obfuscated (packed) images is recorded in `warnings` instead.
    static PeImage parse(std::span<const std::uint8_t> data);
    [[nodiscard]] static bool isPe(std::span<const std::uint8_t> data);

    [[nodiscard]] std::optional<std::uint32_t> rvaToOffset(std::uint32_t rva) const;
    [[nodiscard]] const PeSection* sectionForRva(std::uint32_t rva) const;
    [[nodiscard]] const PeSection* sectionForOffset(std::uint32_t off) const;
    [[nodiscard]] bool is64() const { return magic == 0x20b; }
};

} // namespace wc98::formats
