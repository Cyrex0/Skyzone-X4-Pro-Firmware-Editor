#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  annotations.h — PDB-style annotation database for firmware analysis
//  JSON-based, shareable between users
// ═══════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// Forward declaration (defined in disasm.h)
struct Function;

struct AnnotationDB {
    std::string target;   // "8051" or "arm"
    int         version = 1;

    // Function renames: address -> user-assigned name
    std::unordered_map<uint32_t, std::string> func_names;

    // Function comments: address -> description
    std::unordered_map<uint32_t, std::string> func_comments;

    // Inline comments: address -> annotation
    std::unordered_map<uint32_t, std::string> addr_comments;

    // Data labels: address -> label name
    std::unordered_map<uint32_t, std::string> labels;

    // ── Accessors ────────────────────────────────────────────────────
    void RenameFunction(uint32_t addr, const std::string& name);
    void SetFuncComment(uint32_t addr, const std::string& comment);
    void SetAddrComment(uint32_t addr, const std::string& comment);
    void SetLabel(uint32_t addr, const std::string& label);

    std::string GetFuncName(uint32_t addr, const char* fallback = nullptr) const;
    std::string GetAddrComment(uint32_t addr) const;

    // Apply renames to Function list (from disassembler)
    void ApplyToFunctions(std::vector<Function>& funcs) const;

    // Merge another DB into this one (for sharing)
    void Merge(const AnnotationDB& other);

    void Clear();
    bool Empty() const;

    // ── Serialization ────────────────────────────────────────────────
    bool SaveJSON(const std::string& path) const;
    bool LoadJSON(const std::string& path);
};
