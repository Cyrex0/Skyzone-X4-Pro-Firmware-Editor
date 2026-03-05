#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  disasm.h — 8051 + ARM Thumb disassembler / decompiler view
// ═══════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <atomic>

// ── Disassembled instruction ─────────────────────────────────────────
struct DisasmLine {
    uint32_t    address    = 0;
    uint8_t     size       = 0;  // bytes consumed
    std::string mnemonic;
    std::string operands;
    std::string comment;         // auto-generated annotations
    bool        is_branch  = false;
    uint32_t    branch_target = 0;
    bool        is_call    = false;
    bool        is_ret     = false;
    std::vector<uint8_t> raw;
};

// ── Cross-reference ──────────────────────────────────────────────────
enum class XRefType { CALL, BRANCH, DATA_REF };

struct XRef {
    uint32_t from;
    uint32_t to;
    XRefType type;
};

// ── Function boundary ────────────────────────────────────────────────
struct Function {
    uint32_t    start = 0;
    uint32_t    end   = 0;
    std::string name;
    std::vector<uint32_t> callers;
    std::vector<DisasmLine> lines;   // instructions belonging to this function
};

// ═══════════════════════════════════════════════════════════════════════
//  8051 Disassembler (for B board TW8836)
// ═══════════════════════════════════════════════════════════════════════
class Disasm8051 {
public:
    void Disassemble(const uint8_t* data, uint32_t size,
                     uint32_t base_addr = 0);

    // Convenience: accept vector
    void Disassemble(const std::vector<uint8_t>& data, uint32_t base_addr = 0) {
        if (!data.empty()) Disassemble(data.data(), (uint32_t)data.size(), base_addr);
    }

    // Progress callback (0.0–1.0), called from worker thread
    void SetProgressCallback(std::function<void(float)> cb) { progress_cb_ = cb; }

    // Clear all state
    void Clear() { lines_.clear(); functions_.clear(); xrefs_.clear(); }

    std::vector<DisasmLine>& Lines()     { return lines_; }
    std::vector<Function>&   Functions() { return functions_; }
    std::vector<XRef>&       XRefs()     { return xrefs_; }
    const std::vector<DisasmLine>& Lines() const { return lines_; }
    const std::vector<Function>&   Functions() const { return functions_; }
    const std::vector<XRef>&       XRefs() const { return xrefs_; }

private:
    void DetectFunctions();
    void AnnotateStrings(const uint8_t* data, uint32_t size, uint32_t base);

    std::vector<DisasmLine> lines_;
    std::vector<Function>   functions_;
    std::vector<XRef>       xrefs_;
    std::function<void(float)> progress_cb_;
};

// ═══════════════════════════════════════════════════════════════════════
//  ARM Thumb / Thumb-2 Disassembler (for A board MK22F)
// ═══════════════════════════════════════════════════════════════════════
class DisasmThumb {
public:
    void Disassemble(const uint8_t* data, uint32_t size,
                     uint32_t base_addr = 0xC000);

    // Convenience: accept vector
    void Disassemble(const std::vector<uint8_t>& data, uint32_t base_addr = 0xC000) {
        if (!data.empty()) Disassemble(data.data(), (uint32_t)data.size(), base_addr);
    }

    // Progress callback (0.0–1.0), called from worker thread
    void SetProgressCallback(std::function<void(float)> cb) { progress_cb_ = cb; }

    // Clear all state
    void Clear() { lines_.clear(); functions_.clear(); xrefs_.clear(); }

    std::vector<DisasmLine>& Lines()     { return lines_; }
    std::vector<Function>&   Functions() { return functions_; }
    std::vector<XRef>&       XRefs()     { return xrefs_; }
    const std::vector<DisasmLine>& Lines() const { return lines_; }
    const std::vector<Function>&   Functions() const { return functions_; }
    const std::vector<XRef>&       XRefs() const { return xrefs_; }

private:
    void DetectFunctions();
    void AnnotateStrings(const uint8_t* data, uint32_t size, uint32_t base);

    std::vector<DisasmLine> lines_;
    std::vector<Function>   functions_;
    std::vector<XRef>       xrefs_;
    std::function<void(float)> progress_cb_;
};
