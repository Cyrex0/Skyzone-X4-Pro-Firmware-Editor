#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  decompile.h — IDA-style pseudocode generator for 8051 + ARM Thumb
// ═══════════════════════════════════════════════════════════════════════
#include "disasm.h"
#include "annotations.h"
#include <string>
#include <vector>

struct PseudoLine {
    int         indent = 0;   // nesting depth (0 = function level)
    std::string text;         // C-like statement
    std::string comment;      // original asm or annotation
    uint32_t    addr = 0;     // source address (for linking back)
    bool        is_label = false;
};

// Decompile a single function to pseudocode
std::vector<PseudoLine> Decompile8051(
    const Function& func,
    const std::vector<DisasmLine>& all_lines,
    const AnnotationDB& db);

std::vector<PseudoLine> DecompileThumb(
    const Function& func,
    const std::vector<DisasmLine>& all_lines,
    const AnnotationDB& db);
