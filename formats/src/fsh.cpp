// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/fsh.hpp"
#include "wc98/reader.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/refpack.hpp"
#include <algorithm>
#include <array>
#include <format>

namespace wc98::formats {

namespace {

struct Palette {
    std::uint8_t type = 0;
    std::vector<std::array<std::uint8_t, 4>> rgba;
};

// WC98 shared FE palette (matrix section 4): the fallback used when a bitmap's palette
// indices are not covered by the palette record attached to it. Empty = disabled.
std::vector<std::array<std::uint8_t, 4>>& globalPaletteStore() {
    static std::vector<std::array<std::uint8_t, 4>> g;
    return g;
}

std::size_t bitsPerPixel(std::uint8_t t) {
    switch (t) {
    case 0x7A: return 4;
    case 0x7B: return 8;
    case 0x6D: case 0x78: case 0x7E: return 16;
    case 0x7F: return 24;
    case 0x7D: return 32;
    default: return 0;
    }
}

bool isPaletteType(std::uint8_t t) {
    // 0x29 / 0x2D are the WC98 16-bit FE palette records (matrix section 4); FIFA 99
    // knows only 0x22 / 0x24 / 0x2A / 0x2E. WC98 also stores the shared FE palette
    // (FE/PACK/PAL.PAK) as a type-0x01 256-colour RGB record.
    return t == 0x22 || t == 0x24 || t == 0x2A || t == 0x2E || t == 0x29 || t == 0x2D || t == 0x01;
}

std::size_t paletteStride(std::uint8_t t) {
    if (t == 0x29 || t == 0x2D) return 2; // WC98 16-bit entries
    return (t == 0x22 || t == 0x24 || t == 0x01) ? 3 : 4;
}

std::uint8_t expand(unsigned v, unsigned bits) { return std::uint8_t(v * 255 / ((1u << bits) - 1)); }

// FE menu palettes declare 255 entries in a 768-byte record and their bitmaps use index 255; the
// 256th stored entry is read and decoded as fully transparent (hypothesis: colour key, see FSH.md).
Palette readPalette(std::uint8_t type, std::uint16_t count, std::span<const std::uint8_t> body) {
    const std::size_t stride = paletteStride(type);
    if (std::size_t(count) * stride > body.size())
        throw FormatError(std::format("FSH palette 0x{:02X}: {} entries exceed record", type, count));
    // WC98 (matrix section 4): bitmaps index past the declared count of the attached
    // record, so decode the body to capacity (capped at 256 colours) when it is larger.
    const std::size_t capacity = body.size() / stride;
    std::size_t stored = std::min<std::size_t>(256, std::max<std::size_t>(count, std::min<std::size_t>(capacity, 256)));
    // The FIFA 99 255-entry menu palette keeps its 256th stored entry transparent.
    const bool transparentPad = (type == 0x2A || type == 0x2E) && count == 255 && capacity >= 256;
    Palette p{type, {}};
    p.rgba.reserve(stored);
    for (std::size_t i = 0; i < stored; ++i) {
        const std::uint8_t* c = body.data() + i * stride;
        switch (type) {
        case 0x24:
        case 0x01: p.rgba.push_back({c[0], c[1], c[2], 255}); break; // stored R,G,B (verified with flag textures; 0x01 = WC98 PAL.PAK)
        case 0x22: { // VGA 6-bit R,G,B (order assumed like 0x24; only grey samples exist)
            auto s = [](std::uint8_t v) { return std::uint8_t((v & 63) << 2 | (v & 63) >> 4); };
            p.rgba.push_back({s(c[0]), s(c[1]), s(c[2]), 255});
            break;
        }
        case 0x29:
        case 0x2D: {
            // WC98 16-bit palette entry (matrix section 4). The entry layout matches the
            // container's 0x7E argb1555 pixel format; calibrated against FE/GLOBAL/CONTR.FSH
            // and FE/FEFLOW02/80*.FSH by the tool run in the port verification log.
            const unsigned v = c[0] | (std::uint32_t(c[1]) << 8);
            p.rgba.push_back({expand(v >> 10 & 31, 5), expand(v >> 5 & 31, 5), expand(v & 31, 5),
                              (v & 0x8000u) ? std::uint8_t(255) : std::uint8_t(0)});
            break;
        }
        default: p.rgba.push_back({c[2], c[1], c[0], c[3]}); break; // 0x2A/0x2E: B,G,R,A
        }
        if (transparentPad && i >= count) p.rgba.back()[3] = 0;
    }
    return p;
}

std::vector<std::uint8_t> toRgba(std::uint8_t type, std::uint16_t w, std::uint16_t h, std::span<const std::uint8_t> px,
                                 const Palette* pal) {
    std::size_t n = std::size_t(w) * h;
    std::vector<std::uint8_t> out(n * 4);
    auto put = [&](std::size_t i, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
        out[i * 4] = r; out[i * 4 + 1] = g; out[i * 4 + 2] = b; out[i * 4 + 3] = a;
    };
    auto idx = [&](std::size_t i, unsigned v) {
        if (!pal) throw FormatError("FSH paletted bitmap without palette");
        if (v >= pal->rgba.size()) throw FormatError(std::format("FSH palette index {} >= {}", v, pal->rgba.size()));
        const auto& c = pal->rgba[v];
        put(i, c[0], c[1], c[2], c[3]);
    };
    for (std::size_t i = 0; i < n; ++i) {
        switch (type) {
        case 0x7A: idx(i, (i & 1) ? px[i / 2] & 15 : px[i / 2] >> 4); break; // high nibble = left pixel
        case 0x7B: idx(i, px[i]); break;
        case 0x7D: put(i, px[i * 4 + 2], px[i * 4 + 1], px[i * 4], px[i * 4 + 3]); break;
        case 0x7F: put(i, px[i * 3 + 2], px[i * 3 + 1], px[i * 3], 255); break;
        default: {
            unsigned v = px[i * 2] | px[i * 2 + 1] << 8;
            if (type == 0x78) put(i, expand(v >> 11, 5), expand(v >> 5 & 63, 6), expand(v & 31, 5), 255);
            else if (type == 0x6D) put(i, expand(v >> 8 & 15, 4), expand(v >> 4 & 15, 4), expand(v & 15, 4), expand(v >> 12, 4));
            else put(i, expand(v >> 10 & 31, 5), expand(v >> 5 & 31, 5), expand(v & 31, 5), (v & 0x8000) ? 255 : 0); // 0x7E
        }
        }
    }
    return out;
}

struct Chain {
    FshBitmap bitmap;
    bool hasBitmap = false;
    std::uint8_t firstType = 0;
    std::vector<std::string> comments;
};

Chain walk(std::span<const std::uint8_t> data, std::size_t offset, std::size_t end) {
    if (end > data.size() || offset >= end) throw FormatError(std::format("FSH record at {} out of range", offset));
    Chain c;
    std::optional<Palette> pal;
    std::span<const std::uint8_t> pixels;
    std::vector<std::uint8_t> unpacked;
    std::size_t o = offset;
    for (int guard = 0;; ++guard) {
        if (guard > 64) throw FormatError("FSH record chain too long");
        Reader r(data, o);
        std::uint8_t type = r.u8();
        std::uint32_t next = r.u24le();
        // A chain may continue into another directory entry (e.g. MOUSE.FSH "ptr1" -> "!tr1" palette);
        // only the first record is bounded by the entry end, later ones by the data size.
        std::size_t limit = next ? o + next : (o == offset ? end : data.size());
        if (limit > data.size() || limit < o + 8) throw FormatError(std::format("FSH record at {} has bad length", o));
        if (o == offset) c.firstType = type;
        if (type == 0x6F || type == 0x70) { // text attachment: u32 length + text
            std::uint32_t len = r.u32le();
            if (o + 8 + len > limit) throw FormatError("FSH comment exceeds record");
            auto t = r.bytes(len);
            std::string s(t.begin(), t.end());
            if (auto z = s.find('\0'); z != std::string::npos) s.resize(z);
            c.comments.push_back(std::move(s));
        } else {
            if (limit < o + 16) throw FormatError(std::format("FSH record at {} shorter than header", o));
            std::uint16_t w = r.u16le(), h = r.u16le();
            std::uint16_t misc[4] = {r.u16le(), r.u16le(), r.u16le(), r.u16le()};
            std::span<const std::uint8_t> body = data.subspan(o + 16, limit - o - 16);
            std::uint8_t base = type & 0x7F;
            if (o == offset && bitsPerPixel(base)) {
                FshBitmap& b = c.bitmap;
                b.type = base;
                b.compressed = type & 0x80;
                b.width = w; b.height = h;
                b.centerX = std::int16_t(misc[0]); b.centerY = std::int16_t(misc[1]);
                b.posX = std::int16_t(misc[2]); b.posY = std::int16_t(misc[3]);
                std::size_t need = (std::size_t(w) * h * bitsPerPixel(base) + 7) / 8;
                if (b.compressed) {
                    unpacked = refpack::decompress(body);
                    if (unpacked.size() < need) throw FormatError("FSH compressed bitmap too small");
                    pixels = unpacked;
                } else {
                    if (body.size() < need) throw FormatError(std::format("FSH bitmap {}x{} type 0x{:02X} truncated", w, h, base));
                    pixels = body.first(need);
                    // Extra mip levels: count halvings that fit, capped by the header's nibble (misc[3] >> 12).
                    std::size_t used = need, mw = w, mh = h;
                    unsigned cap = misc[3] >> 12;
                    while (b.mipLevels < cap && (mw > 1 || mh > 1)) {
                        mw = std::max<std::size_t>(1, mw / 2); mh = std::max<std::size_t>(1, mh / 2);
                        std::size_t lv = (mw * mh * bitsPerPixel(base) + 7) / 8;
                        if (used + lv > body.size()) break;
                        used += lv;
                        ++b.mipLevels;
                    }
                }
                c.hasBitmap = true;
            } else if (isPaletteType(type)) {
                if (!pal) pal = readPalette(type, w, body);
            } else if (o == offset) {
                throw FormatError(std::format("FSH unknown record type 0x{:02X} at {}", type, o));
            }
            // other attachment types are skipped
        }
        if (!next) break;
        o += next;
    }
    if (c.hasBitmap) {
        bool indexed = c.bitmap.type == 0x7A || c.bitmap.type == 0x7B;
        if (indexed) {
            std::size_t n = std::size_t(c.bitmap.width) * c.bitmap.height;
            c.bitmap.indices.resize(n);
            for (std::size_t i = 0; i < n; ++i)
                c.bitmap.indices[i] = c.bitmap.type == 0x7B ? pixels[i] : std::uint8_t((i & 1) ? pixels[i / 2] & 15 : pixels[i / 2] >> 4);
            // WC98 (matrix section 4): the FE art has no "!pal" tag and its bitmaps index
            // past the attached palette record, so the shared FE palette is the fallback.
            const auto& gp = globalPaletteStore();
            std::uint8_t maxIndex = 0;
            for (auto v : c.bitmap.indices) maxIndex = std::max(maxIndex, v);
            if (!gp.empty() && (!pal || maxIndex >= pal->rgba.size()) && std::size_t(maxIndex) < gp.size()) {
                pal = Palette{0xFE, gp};
                c.bitmap.usedGlobalPalette = true;
            }
        }
        if (indexed && pal) c.bitmap.paletteType = pal->type, c.bitmap.palette = pal->rgba;
        if (indexed && !pal && c.bitmap.type == 0x7A) { // 4-bit without palette: intensity ramp (FNTF fonts)
            Palette ramp{0, {}};
            for (unsigned i = 0; i < 16; ++i) ramp.rgba.push_back({255, 255, 255, std::uint8_t(i * 17)});
            c.bitmap.rgba = toRgba(c.bitmap.type, c.bitmap.width, c.bitmap.height, pixels, &ramp);
        } else {
            c.bitmap.rgba = toRgba(c.bitmap.type, c.bitmap.width, c.bitmap.height, pixels, pal ? &*pal : nullptr);
        }
    }
    return c;
}

} // namespace

const char* fshTypeName(std::uint8_t t) {
    switch (t & 0x7F) {
    case 0x7A: return "4bit-pal";
    case 0x7B: return "8bit-pal";
    case 0x7D: return "argb8888";
    case 0x7F: return "rgb888";
    case 0x78: return "rgb565";
    case 0x6D: return "argb4444";
    case 0x7E: return "argb1555";
    default: return "";
    }
}

FshBitmap decodeFshBitmap(std::span<const std::uint8_t> data, std::size_t offset, std::size_t end) {
    auto c = walk(data, offset, end);
    if (!c.hasBitmap) throw FormatError(std::format("FSH record at {} is not a bitmap (type 0x{:02X})", offset, c.firstType));
    return std::move(c.bitmap);
}

bool FshFile::isFsh(std::span<const std::uint8_t> d) {
    return d.size() >= 16 && d[0] == 'S' && d[1] == 'H' && d[2] == 'P' && d[3] == 'I';
}

FshFile FshFile::parse(std::span<const std::uint8_t> data) {
    if (!isFsh(data)) throw FormatError("not a SHPI file");
    Reader r(data, 4);
    FshFile f;
    f.declaredSize = r.u32le();
    std::uint32_t count = r.u32le();
    f.dirId = r.fixedString(4);
    if (count > (data.size() - 16) / 8) throw FormatError(std::format("SHPI entry count {} implausible", count));
    std::vector<std::uint32_t> starts;
    for (std::uint32_t i = 0; i < count; ++i) {
        FshEntry e;
        auto tag = r.bytes(4);
        e.tag.assign(tag.begin(), tag.end());
        e.offset = r.u32le();
        if (e.offset < 16 + 8 * count || e.offset >= data.size())
            throw FormatError(std::format("SHPI entry '{}' offset {} out of range", e.tag, e.offset));
        starts.push_back(e.offset);
        f.entries.push_back(std::move(e));
    }
    std::ranges::sort(starts);
    for (auto& e : f.entries) {
        auto it = std::ranges::upper_bound(starts, e.offset);
        std::size_t end = it == starts.end() ? data.size() : *it;
        auto c = walk(data, e.offset, end);
        e.firstType = c.firstType;
        e.comments = std::move(c.comments);
        if (c.hasBitmap) e.bitmap = std::move(c.bitmap);
    }
    return f;
}

void setGlobalPalette(std::span<const std::array<std::uint8_t, 4>> palette) {
    globalPaletteStore().assign(palette.begin(), palette.end());
}

std::size_t globalPaletteSize() { return globalPaletteStore().size(); }

const std::vector<std::array<std::uint8_t, 4>>& globalPalette() { return globalPaletteStore(); }

std::vector<std::array<std::uint8_t, 4>> fshPaletteRecordsAt(std::span<const std::uint8_t> data,
                                                             std::size_t offset, std::size_t end) {
    // WC98 (matrix section 4): walk the 16-byte-headed record chain at `offset` and collect
    // every attached palette record (types accepted by isPaletteType).
    std::vector<std::array<std::uint8_t, 4>> out;
    if (offset + 0x10 > data.size()) return out;
    for (unsigned guard = 0; guard < 32; ++guard) {
        if (offset + 0x10 > data.size() || offset >= end) break;
        Reader r(data, offset);
        const std::uint32_t next = r.u32le();
        std::uint16_t type = 0, w = 0;
        try { type = r.u16le(); w = r.u16le(); r.u16le(); } catch (const FormatError&) { break; }
        if (isPaletteType(std::uint8_t(type))) {
            const std::uint32_t count = w ? w : 0x100u;
            std::size_t avail = data.size() - (offset + 0x10);
            if (next >= 0x10) avail = std::min(avail, std::size_t(next) - 0x10);
            try {
                const auto pal = readPalette(std::uint8_t(type), count, data.subspan(offset + 0x10, avail));
                out.insert(out.end(), pal.rgba.begin(), pal.rgba.end());
            } catch (const FormatError&) {}
        }
        if (next < 0x10) break;
        offset += next;
        if (offset + 0x10 > end || offset + 0x10 > data.size()) break;
    }
    return out;
}

std::vector<std::array<std::uint8_t, 4>> wc98GlobalPaletteFromPalArchive(std::span<const std::uint8_t> palRaw) {
    // WC98 (matrix section 4): the shared FE palette. The CD extraction ships it as
    // FE/PACK/PAL.PAK = a RefPack-wrapped SHPI archive whose entry carries a type-0x01
    // 256-colour RGB record; the ISO extraction ships palette-member BIG archives
    // (PAK#####.BIG, 12 members / 2048 B) and FIFA 99 uses raw 0x10/0x10/0x200 records.
    // All three shapes are supported; the largest usable palette wins.
    const auto unpacked = refpack::decompressIfNeeded(palRaw); // PAL.PAK is RefPack-wrapped
    std::span<const std::uint8_t> pal = unpacked;
    std::vector<std::array<std::uint8_t, 4>> best;
    auto take = [&](std::vector<std::array<std::uint8_t, 4>> p) {
        if (p.size() >= 2 && p.size() > best.size()) best = std::move(p);
    };
    if (FshFile::isFsh(pal) && pal.size() >= 0x1C) {
        // Walk entries with the archive's own entry model (FshFile::parse — the same model
        // that decodes the 6 356 game bitmaps), then read each palette record chain.
        try {
            const auto f = FshFile::parse(pal);
            for (const auto& e : f.entries)
                if (e.offset < pal.size()) take(fshPaletteRecordsAt(pal, e.offset, pal.size()));
        } catch (const FormatError&) {}
        if (!best.empty()) return best;
    }
    if (BigArchive::isBig(pal)) {
        try {
            const auto a = BigArchive::parse(pal);
            for (const auto& e : a.entries)
                take(wc98GlobalPaletteFromPalArchive(refpack::decompressIfNeeded(BigArchive::payload(pal, e))));
        } catch (const FormatError&) {}
        if (!best.empty()) return best;
    }
    // raw palette records (FIFA 99's PAL####.BIG members): 0x10 header + 0x10 meta + 0x200 RGBA;
    // pick the window whose alpha bytes are all zero (the FE palette has no transparency).
    for (std::size_t p = 0; p + 0x20 + 0x400 <= pal.size(); p += 4) {
        if (pal[p + 0x10 + 5] != 0) continue; // header alpha byte must be 0
        std::size_t zeros = 0;
        for (std::size_t i = 0; i < 0x100; ++i)
            if (pal[p + 0x20 + i * 4 + 3] == 0) ++zeros;
        if (zeros > best.size() || (zeros && best.size() < 2)) {
            std::vector<std::array<std::uint8_t, 4>> cand;
            for (std::size_t i = 0; i < 0x100; ++i)
                cand.push_back({pal[p + 0x20 + i * 4], pal[p + 0x20 + i * 4 + 1], pal[p + 0x20 + i * 4 + 2], 255});
            take(std::move(cand));
        }
    }
    return best;
}

} // namespace wc98::formats
