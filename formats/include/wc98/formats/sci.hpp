// Scene payload parser for WC98 COMMON/*.SCI files. Grammar proven from on-disk bytes in
// docs/reverse-engineering/TASK_HANDLERS_WC98.md §6 (pass-12sci); the runtime consumer of the
// deserialized quads is FUN_004aaf00 (DAT_0063697c). Loader-side field splitting stays [H].
#include <wc98/reader.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {

// One record-stream entry: 16 bytes = 8×i16 (offsets/speeds/angles; same units as the task-handler
// writes, TASK_HANDLERS_WC98.md §5). fields[0] is the record type (observed 0..0x19).
struct SciRecord {
    std::array<std::int16_t, 8> fields;
};

// Main tail table entry: stride 12 = {u32 DGROUP VA, u16[4]} (per-arena data bases / BSS scene
// block; observed words = {0, 0, count, index}).
struct SciTailEntry {
    std::uint32_t va = 0;
    std::array<std::uint16_t, 4> words{};
};

// Trailing packed list entry: stride 8 = {u32 DGROUP VA, u32 value} (BSS relocation base pairs).
struct SciTailPair {
    std::uint32_t va = 0;
    std::uint32_t val = 0;
};

// COMMON/*.SCI layout: header 0x00..0x2F, record stream @0x30 (stride 16) ending at the tail anchor
// (header `offset` field × 16), tail table at offset*16 + 0x28 through end of file.
struct SciFile {
    std::uint32_t count = 0;             // header u32 @0x00 (payload word/record count [H])
    std::uint16_t tagA = 0;              // @0x04, always 0x35
    std::uint16_t sizeField = 0;         // @0x06 payload size candidate (monotone with file size)
    std::uint16_t tagB = 0;              // @0x08 {2|6}
    std::uint16_t offset = 0;            // @0x0A tail anchor ×16 (records end, stream pad to +0x28)
    std::uint16_t tagC = 0;              // @0x0C, always 0x16
    std::uint16_t magic = 0;             // @0x0E, 0x201..0x204
    std::array<std::uint32_t, 6> relocs{}; // @0x10..+0x24 DGROUP relocation targets (identical set)

    std::vector<SciRecord> records;      // [0x30, offset*16), stride 16
    std::vector<SciTailEntry> tail;      // [offset*16 + 0x28, ...), stride 12
    std::vector<SciTailPair> trailer;    // trailing stride-8 {VA,val} packed list at end of file

    // Strict parse; throws FormatError on layout violations. `data` must outlive the spans.
    static SciFile parse(std::span<const std::uint8_t> data);
};

// One decoded record-stream op: 16-byte record = {type, seven operand slots}. Grammar proven
// tree-wide (pass-17sci): operands f1/f5 are always zero, f2 pitch delta i16 within ±0x384,
// f3 duration ticks u16 <= 0x214. Per-type notes: TASK_HANDLERS_WC98.md §9.
struct SciOp {
    std::uint16_t type = 0;              // fields[0]; observed set {1,2,4,6,9,A,B,C,D,10,13,15}
    std::array<std::int16_t, 7> ops{};   // f1..f7 operand slots (f2=dx, f3=dur, f4=angle, f7=id)
    std::size_t offset = 0;              // file offset of the record
};

// Decode the record stream into an op stream: one SciOp per 16-byte record. Throws FormatError if
// any record violates the tree-wide grammar (type outside the observed set, f1/f5 non-zero, f2 out
// of ±0x384). Semantics stay data-table level: there is no runtime opcode dispatcher in
// FIFAWC.EXE; tasks consume these rows by index (TASK_HANDLERS_WC98.md §9).
std::vector<SciOp> decodeOpcodeStream(const SciFile& file);

} // namespace wc98::formats
