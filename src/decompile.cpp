// ═══════════════════════════════════════════════════════════════════
//  decompile.cpp — IDA-style pseudocode generator
//  Produces C-like output from disassembled 8051 / ARM Thumb code
// ═══════════════════════════════════════════════════════════════════
#include "decompile.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <sstream>

// ── Helpers ──────────────────────────────────────────────────────
static std::string Hex8(uint8_t v)  { char b[8];  snprintf(b,sizeof(b),"0x%02X",v); return b; }
static std::string Hex16(uint16_t v){ char b[12]; snprintf(b,sizeof(b),"0x%04X",v); return b; }
static std::string Hex32(uint32_t v){ char b[16]; snprintf(b,sizeof(b),"0x%08X",v); return b; }

static PseudoLine MakeLine(int indent, const std::string& text,
                           const std::string& comment = "", uint32_t addr = 0) {
    return {indent, text, comment, addr, false};
}
static PseudoLine MakeLabel(const std::string& label, uint32_t addr) {
    PseudoLine p; p.indent = 0; p.text = label + ":";
    p.addr = addr; p.is_label = true;
    return p;
}

static std::vector<const DisasmLine*> GetFuncLines(
    const Function& func, const std::vector<DisasmLine>& all)
{
    std::vector<const DisasmLine*> out;
    for (auto& ln : all) {
        if (ln.address >= func.start &&
            (func.end == 0 || ln.address < func.end))
            out.push_back(&ln);
    }
    return out;
}

static std::string GetLabel(const AnnotationDB& db, uint32_t target, bool arm) {
    auto it = db.labels.find(target);
    if (it != db.labels.end()) return it->second;
    char lbl[32];
    if (arm) snprintf(lbl, sizeof(lbl), "loc_%08X", target);
    else     snprintf(lbl, sizeof(lbl), "loc_%04X", target);
    return lbl;
}

// ═══════════════════════════════════════════════════════════════════
//  8051 Decompiler
// ═══════════════════════════════════════════════════════════════════

// 8051 SFR names
static const char* SFR_Name(uint8_t addr) {
    switch (addr) {
        case 0x80: return "P0"; case 0x81: return "SP";  case 0x82: return "DPL";
        case 0x83: return "DPH";case 0x87: return "PCON";case 0x88: return "TCON";
        case 0x89: return "TMOD";case 0x8A: return "TL0";case 0x8B: return "TL1";
        case 0x8C: return "TH0";case 0x8D: return "TH1";case 0x90: return "P1";
        case 0x98: return "SCON";case 0x99: return "SBUF";case 0xA0: return "P2";
        case 0xA8: return "IE";  case 0xB0: return "P3";  case 0xB8: return "IP";
        case 0xC8: return "T2CON"; case 0xCA: return "RCAP2L"; case 0xCB: return "RCAP2H";
        case 0xCC: return "TL2"; case 0xCD: return "TH2";
        case 0xD0: return "PSW"; case 0xE0: return "ACC"; case 0xF0: return "B";
        default: return nullptr;
    }
}

static std::string Direct8051(uint8_t addr) {
    const char* n = SFR_Name(addr);
    if (n) return n;
    if (addr < 0x80) {
        char b[16]; snprintf(b, sizeof(b), "iram_%02X", addr);
        return b;
    }
    char b[16]; snprintf(b, sizeof(b), "SFR_%02X", addr);
    return b;
}

// Bit-addressable name resolution
static std::string BitName(uint8_t bit) {
    // Bits 0x00-0x7F → IRAM byte 0x20+(bit/8), bit (bit%8)
    // Bits 0x80+ → SFR bit addressing
    if (bit < 0x80) {
        char b[32]; snprintf(b, sizeof(b), "iram_%02X.%d", 0x20 + bit/8, bit & 7);
        return b;
    }
    uint8_t sfr = bit & 0xF8;
    uint8_t bpos = bit & 0x07;
    const char* sn = SFR_Name(sfr);
    char b[32];
    if (sn) snprintf(b, sizeof(b), "%s.%d", sn, bpos);
    else    snprintf(b, sizeof(b), "SFR_%02X.%d", sfr, bpos);
    return b;
}

std::vector<PseudoLine> Decompile8051(
    const Function& func,
    const std::vector<DisasmLine>& all_lines,
    const AnnotationDB& db)
{
    std::vector<PseudoLine> out;
    auto lines = GetFuncLines(func, all_lines);
    if (lines.empty()) return out;

    // Function signature
    std::string fname = db.GetFuncName(func.start, func.name.c_str());
    std::string sig = "void " + fname + "(void)";
    if (!func.callers.empty()) {
        char buf[32]; snprintf(buf, sizeof(buf), "// %zu callers", func.callers.size());
        out.push_back(MakeLine(0, sig + " {", buf, func.start));
    } else {
        out.push_back(MakeLine(0, sig + " {", "", func.start));
    }

    // Collect branch targets for labels
    std::unordered_set<uint32_t> branch_targets;
    for (auto* ln : lines) {
        if ((ln->is_branch || ln->is_call) && ln->branch_target >= func.start &&
            (func.end == 0 || ln->branch_target < func.end))
            branch_targets.insert(ln->branch_target);
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        auto& ln = *lines[i];
        if (ln.raw.empty()) continue;
        uint8_t op = ln.raw[0];
        std::string asm_comment = ln.mnemonic + " " + ln.operands;
        std::string user_comment = db.GetAddrComment(ln.address);
        if (!user_comment.empty()) asm_comment += " // " + user_comment;

        // Insert label if branch target
        if (branch_targets.count(ln.address))
            out.push_back(MakeLabel(GetLabel(db, ln.address, false), ln.address));

        // ── NOP ──
        if (ln.mnemonic == "NOP") {
            out.push_back(MakeLine(1, "/* nop */", asm_comment, ln.address));
        }
        // ── RET / RETI ──
        else if (ln.mnemonic == "RET" || ln.mnemonic == "RETI") {
            out.push_back(MakeLine(1, "return;", asm_comment, ln.address));
        }
        // ── LCALL / ACALL ──
        else if (ln.mnemonic == "LCALL" || ln.mnemonic == "ACALL") {
            std::string tn = db.GetFuncName(ln.branch_target);
            out.push_back(MakeLine(1, tn + "();", asm_comment, ln.address));
        }
        // ── LJMP / AJMP / SJMP ──
        else if (ln.mnemonic == "LJMP" || ln.mnemonic == "AJMP" || ln.mnemonic == "SJMP") {
            if (ln.branch_target >= func.start && (func.end == 0 || ln.branch_target < func.end)) {
                out.push_back(MakeLine(1, "goto " + GetLabel(db, ln.branch_target, false) + ";",
                    asm_comment, ln.address));
            } else {
                std::string tn = db.GetFuncName(ln.branch_target);
                out.push_back(MakeLine(1, tn + "(); return;", asm_comment + " // tail call", ln.address));
            }
        }
        // ── MOV ──
        else if (ln.mnemonic == "MOV") {
            if (ln.size == 1) {
                if (op >= 0xE8 && op <= 0xEF) { // MOV A, Rn
                    char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0xE8);
                    out.push_back(MakeLine(1, std::string("ACC = ") + rn + ";", asm_comment, ln.address));
                }
                else if (op >= 0xF8 && op <= 0xFF) { // MOV Rn, A
                    char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0xF8);
                    out.push_back(MakeLine(1, std::string(rn) + " = ACC;", asm_comment, ln.address));
                }
                else if (op >= 0xE6 && op <= 0xE7) { // MOV A, @Ri
                    char ri[8]; snprintf(ri, sizeof(ri), "@R%d", op - 0xE6);
                    out.push_back(MakeLine(1, std::string("ACC = iram[R") + (char)('0' + op - 0xE6) + "];", asm_comment, ln.address));
                }
                else if (op >= 0xF6 && op <= 0xF7) { // MOV @Ri, A
                    out.push_back(MakeLine(1, std::string("iram[R") + (char)('0' + op - 0xF6) + "] = ACC;", asm_comment, ln.address));
                }
                else {
                    out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
                }
            }
            else if (ln.size == 2) {
                if (op == 0x74) { // MOV A, #imm
                    out.push_back(MakeLine(1, "ACC = " + Hex8(ln.raw[1]) + ";", asm_comment, ln.address));
                }
                else if (op == 0xE5) { // MOV A, direct
                    out.push_back(MakeLine(1, "ACC = " + Direct8051(ln.raw[1]) + ";", asm_comment, ln.address));
                }
                else if (op == 0xF5) { // MOV direct, A
                    out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + " = ACC;", asm_comment, ln.address));
                }
                else if (op >= 0x78 && op <= 0x7F) { // MOV Rn, #imm
                    char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0x78);
                    out.push_back(MakeLine(1, std::string(rn) + " = " + Hex8(ln.raw[1]) + ";", asm_comment, ln.address));
                }
                else if (op >= 0xA8 && op <= 0xAF) { // MOV Rn, direct
                    char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0xA8);
                    out.push_back(MakeLine(1, std::string(rn) + " = " + Direct8051(ln.raw[1]) + ";", asm_comment, ln.address));
                }
                else if (op >= 0x88 && op <= 0x8F) { // MOV direct, Rn
                    char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0x88);
                    out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + " = " + rn + ";", asm_comment, ln.address));
                }
                else if (op >= 0xA6 && op <= 0xA7) { // MOV @Ri, direct
                    out.push_back(MakeLine(1, std::string("iram[R") + (char)('0' + op - 0xA6) + "] = " + Direct8051(ln.raw[1]) + ";", asm_comment, ln.address));
                }
                else if (op >= 0x86 && op <= 0x87) { // MOV direct, @Ri
                    out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + " = iram[R" + (char)('0' + op - 0x86) + "];", asm_comment, ln.address));
                }
                else if (op >= 0x76 && op <= 0x77) { // MOV @Ri, #imm
                    out.push_back(MakeLine(1, std::string("iram[R") + (char)('0' + op - 0x76) + "] = " + Hex8(ln.raw[1]) + ";", asm_comment, ln.address));
                }
                else {
                    out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
                }
            }
            else if (ln.size == 3) {
                if (op == 0x75) { // MOV direct, #imm
                    out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + " = " + Hex8(ln.raw[2]) + ";", asm_comment, ln.address));
                }
                else if (op == 0x85) { // MOV direct, direct
                    out.push_back(MakeLine(1, Direct8051(ln.raw[2]) + " = " + Direct8051(ln.raw[1]) + ";", asm_comment, ln.address));
                }
                else if (op == 0x90) { // MOV DPTR, #imm16
                    uint16_t val = ((uint16_t)ln.raw[1] << 8) | ln.raw[2];
                    out.push_back(MakeLine(1, "DPTR = " + Hex16(val) + ";", asm_comment, ln.address));
                }
                else {
                    out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
                }
            }
            else {
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            }
        }
        // ── INC ──
        else if (ln.mnemonic == "INC") {
            if (ln.size == 1) {
                if (op == 0x04) out.push_back(MakeLine(1, "ACC++;", asm_comment, ln.address));
                else if (op == 0xA3) out.push_back(MakeLine(1, "DPTR++;", asm_comment, ln.address));
                else if (op >= 0x08 && op <= 0x0F) {
                    char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0x08);
                    out.push_back(MakeLine(1, std::string(rn) + "++;", asm_comment, ln.address));
                }
                else if (op >= 0x06 && op <= 0x07) {
                    out.push_back(MakeLine(1, std::string("iram[R") + (char)('0' + op - 0x06) + "]++;", asm_comment, ln.address));
                }
                else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            } else {
                out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + "++;", asm_comment, ln.address));
            }
        }
        // ── DEC ──
        else if (ln.mnemonic == "DEC") {
            if (ln.size == 1) {
                if (op == 0x14) out.push_back(MakeLine(1, "ACC--;", asm_comment, ln.address));
                else if (op >= 0x18 && op <= 0x1F) {
                    char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0x18);
                    out.push_back(MakeLine(1, std::string(rn) + "--;", asm_comment, ln.address));
                }
                else if (op >= 0x16 && op <= 0x17) {
                    out.push_back(MakeLine(1, std::string("iram[R") + (char)('0' + op - 0x16) + "]--;", asm_comment, ln.address));
                }
                else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            } else {
                out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + "--;", asm_comment, ln.address));
            }
        }
        // ── PUSH / POP ──
        else if (ln.mnemonic == "PUSH") {
            out.push_back(MakeLine(1, "push(" + Direct8051(ln.raw[1]) + ");", asm_comment, ln.address));
        }
        else if (ln.mnemonic == "POP") {
            out.push_back(MakeLine(1, "pop(" + Direct8051(ln.raw[1]) + ");", asm_comment, ln.address));
        }
        // ── ADD / ADDC ──
        else if (ln.mnemonic == "ADD" || ln.mnemonic == "ADDC") {
            std::string carry = (ln.mnemonic == "ADDC") ? " + CY" : "";
            if (op == 0x24 || op == 0x34) // ADD/ADDC A, #imm
                out.push_back(MakeLine(1, "ACC += " + Hex8(ln.raw[1]) + carry + ";", asm_comment, ln.address));
            else if (op == 0x25 || op == 0x35) // ADD/ADDC A, direct
                out.push_back(MakeLine(1, "ACC += " + Direct8051(ln.raw[1]) + carry + ";", asm_comment, ln.address));
            else if ((op >= 0x28 && op <= 0x2F) || (op >= 0x38 && op <= 0x3F)) {
                char rn[4]; snprintf(rn, sizeof(rn), "R%d", op & 0x07);
                out.push_back(MakeLine(1, std::string("ACC += ") + rn + carry + ";", asm_comment, ln.address));
            }
            else if ((op >= 0x26 && op <= 0x27) || (op >= 0x36 && op <= 0x37)) {
                out.push_back(MakeLine(1, "ACC += iram[R" + std::string(1, '0' + (op & 1)) + "]" + carry + ";", asm_comment, ln.address));
            }
            else out.push_back(MakeLine(1, "ACC += " + ln.operands + carry + ";", asm_comment, ln.address));
        }
        // ── SUBB ──
        else if (ln.mnemonic == "SUBB") {
            if (op == 0x94)
                out.push_back(MakeLine(1, "ACC -= " + Hex8(ln.raw[1]) + " + CY;", asm_comment, ln.address));
            else if (op == 0x95)
                out.push_back(MakeLine(1, "ACC -= " + Direct8051(ln.raw[1]) + " + CY;", asm_comment, ln.address));
            else if (op >= 0x98 && op <= 0x9F) {
                char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0x98);
                out.push_back(MakeLine(1, std::string("ACC -= ") + rn + " + CY;", asm_comment, ln.address));
            }
            else if (op >= 0x96 && op <= 0x97)
                out.push_back(MakeLine(1, "ACC -= iram[R" + std::string(1, '0' + (op & 1)) + "] + CY;", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "ACC -= " + ln.operands + ";", asm_comment, ln.address));
        }
        // ── ORL ──
        else if (ln.mnemonic == "ORL") {
            if (op == 0x44) out.push_back(MakeLine(1, "ACC |= " + Hex8(ln.raw[1]) + ";", asm_comment, ln.address));
            else if (op == 0x45) out.push_back(MakeLine(1, "ACC |= " + Direct8051(ln.raw[1]) + ";", asm_comment, ln.address));
            else if (op >= 0x48 && op <= 0x4F) {
                char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0x48);
                out.push_back(MakeLine(1, std::string("ACC |= ") + rn + ";", asm_comment, ln.address));
            }
            else if (op >= 0x46 && op <= 0x47)
                out.push_back(MakeLine(1, "ACC |= iram[R" + std::string(1, '0' + (op & 1)) + "];", asm_comment, ln.address));
            else if (op == 0x42) out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + " |= ACC;", asm_comment, ln.address));
            else if (op == 0x43) out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + " |= " + Hex8(ln.raw[2]) + ";", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── ANL ──
        else if (ln.mnemonic == "ANL") {
            if (op == 0x54) out.push_back(MakeLine(1, "ACC &= " + Hex8(ln.raw[1]) + ";", asm_comment, ln.address));
            else if (op == 0x55) out.push_back(MakeLine(1, "ACC &= " + Direct8051(ln.raw[1]) + ";", asm_comment, ln.address));
            else if (op >= 0x58 && op <= 0x5F) {
                char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0x58);
                out.push_back(MakeLine(1, std::string("ACC &= ") + rn + ";", asm_comment, ln.address));
            }
            else if (op >= 0x56 && op <= 0x57)
                out.push_back(MakeLine(1, "ACC &= iram[R" + std::string(1, '0' + (op & 1)) + "];", asm_comment, ln.address));
            else if (op == 0x52) out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + " &= ACC;", asm_comment, ln.address));
            else if (op == 0x53) out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + " &= " + Hex8(ln.raw[2]) + ";", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── XRL ──
        else if (ln.mnemonic == "XRL") {
            if (op == 0x64) out.push_back(MakeLine(1, "ACC ^= " + Hex8(ln.raw[1]) + ";", asm_comment, ln.address));
            else if (op == 0x65) out.push_back(MakeLine(1, "ACC ^= " + Direct8051(ln.raw[1]) + ";", asm_comment, ln.address));
            else if (op >= 0x68 && op <= 0x6F) {
                char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0x68);
                out.push_back(MakeLine(1, std::string("ACC ^= ") + rn + ";", asm_comment, ln.address));
            }
            else if (op >= 0x66 && op <= 0x67)
                out.push_back(MakeLine(1, "ACC ^= iram[R" + std::string(1, '0' + (op & 1)) + "];", asm_comment, ln.address));
            else if (op == 0x62) out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + " ^= ACC;", asm_comment, ln.address));
            else if (op == 0x63) out.push_back(MakeLine(1, Direct8051(ln.raw[1]) + " ^= " + Hex8(ln.raw[2]) + ";", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── CLR ──
        else if (ln.mnemonic == "CLR") {
            if (op == 0xE4) out.push_back(MakeLine(1, "ACC = 0;", asm_comment, ln.address));
            else if (op == 0xC3) out.push_back(MakeLine(1, "CY = 0;", asm_comment, ln.address));
            else if (op == 0xC2) out.push_back(MakeLine(1, BitName(ln.raw[1]) + " = 0;", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── SETB ──
        else if (ln.mnemonic == "SETB") {
            if (op == 0xD3) out.push_back(MakeLine(1, "CY = 1;", asm_comment, ln.address));
            else if (op == 0xD2) out.push_back(MakeLine(1, BitName(ln.raw[1]) + " = 1;", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── CPL ──
        else if (ln.mnemonic == "CPL") {
            if (op == 0xF4) out.push_back(MakeLine(1, "ACC = ~ACC;", asm_comment, ln.address));
            else if (op == 0xB3) out.push_back(MakeLine(1, "CY = !CY;", asm_comment, ln.address));
            else if (op == 0xB2) out.push_back(MakeLine(1, BitName(ln.raw[1]) + " ^= 1;", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── SWAP ──
        else if (ln.mnemonic == "SWAP") {
            out.push_back(MakeLine(1, "ACC = (ACC >> 4) | (ACC << 4);", asm_comment, ln.address));
        }
        // ── XCH ──
        else if (ln.mnemonic == "XCH") {
            if (op == 0xC5) out.push_back(MakeLine(1, "{ uint8_t t = ACC; ACC = " + Direct8051(ln.raw[1]) + "; " + Direct8051(ln.raw[1]) + " = t; }", asm_comment, ln.address));
            else if (op >= 0xC8 && op <= 0xCF) {
                char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0xC8);
                out.push_back(MakeLine(1, std::string("{ uint8_t t = ACC; ACC = ") + rn + "; " + rn + " = t; }", asm_comment, ln.address));
            }
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── RL / RLC / RR / RRC ──
        else if (ln.mnemonic == "RL") {
            out.push_back(MakeLine(1, "ACC = (ACC << 1) | (ACC >> 7);", asm_comment, ln.address));
        }
        else if (ln.mnemonic == "RLC") {
            out.push_back(MakeLine(1, "{ uint8_t c = CY; CY = ACC >> 7; ACC = (ACC << 1) | c; }", asm_comment, ln.address));
        }
        else if (ln.mnemonic == "RR") {
            out.push_back(MakeLine(1, "ACC = (ACC >> 1) | (ACC << 7);", asm_comment, ln.address));
        }
        else if (ln.mnemonic == "RRC") {
            out.push_back(MakeLine(1, "{ uint8_t c = CY; CY = ACC & 1; ACC = (ACC >> 1) | (c << 7); }", asm_comment, ln.address));
        }
        // ── MOVX ──
        else if (ln.mnemonic == "MOVX") {
            if (op == 0xE0) out.push_back(MakeLine(1, "ACC = xdata[DPTR];", asm_comment, ln.address));
            else if (op == 0xF0) out.push_back(MakeLine(1, "xdata[DPTR] = ACC;", asm_comment, ln.address));
            else if (op == 0xE2 || op == 0xE3)
                out.push_back(MakeLine(1, "ACC = xdata[R" + std::string(1, '0' + (op & 1)) + "];", asm_comment, ln.address));
            else if (op == 0xF2 || op == 0xF3)
                out.push_back(MakeLine(1, "xdata[R" + std::string(1, '0' + (op & 1)) + "] = ACC;", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── MOVC ──
        else if (ln.mnemonic == "MOVC") {
            if (op == 0x93) out.push_back(MakeLine(1, "ACC = code[ACC + DPTR];", asm_comment, ln.address));
            else if (op == 0x83) out.push_back(MakeLine(1, "ACC = code[ACC + PC];", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── JZ / JNZ ──
        else if (ln.mnemonic == "JZ") {
            out.push_back(MakeLine(1, "if (ACC == 0) goto " + GetLabel(db, ln.branch_target, false) + ";", asm_comment, ln.address));
        }
        else if (ln.mnemonic == "JNZ") {
            out.push_back(MakeLine(1, "if (ACC != 0) goto " + GetLabel(db, ln.branch_target, false) + ";", asm_comment, ln.address));
        }
        // ── JC / JNC ──
        else if (ln.mnemonic == "JC") {
            out.push_back(MakeLine(1, "if (CY) goto " + GetLabel(db, ln.branch_target, false) + ";", asm_comment, ln.address));
        }
        else if (ln.mnemonic == "JNC") {
            out.push_back(MakeLine(1, "if (!CY) goto " + GetLabel(db, ln.branch_target, false) + ";", asm_comment, ln.address));
        }
        // ── JB / JNB / JBC ──
        else if (ln.mnemonic == "JB") {
            out.push_back(MakeLine(1, "if (" + BitName(ln.raw[1]) + ") goto " + GetLabel(db, ln.branch_target, false) + ";", asm_comment, ln.address));
        }
        else if (ln.mnemonic == "JNB") {
            out.push_back(MakeLine(1, "if (!" + BitName(ln.raw[1]) + ") goto " + GetLabel(db, ln.branch_target, false) + ";", asm_comment, ln.address));
        }
        else if (ln.mnemonic == "JBC") {
            out.push_back(MakeLine(1, "if (" + BitName(ln.raw[1]) + ") { " + BitName(ln.raw[1]) + " = 0; goto " + GetLabel(db, ln.branch_target, false) + "; }", asm_comment, ln.address));
        }
        // ── CJNE ──
        else if (ln.mnemonic == "CJNE") {
            std::string lbl = GetLabel(db, ln.branch_target, false);
            if (op == 0xB4) // CJNE A, #imm, rel
                out.push_back(MakeLine(1, "if (ACC != " + Hex8(ln.raw[1]) + ") goto " + lbl + ";", asm_comment, ln.address));
            else if (op == 0xB5) // CJNE A, direct, rel
                out.push_back(MakeLine(1, "if (ACC != " + Direct8051(ln.raw[1]) + ") goto " + lbl + ";", asm_comment, ln.address));
            else if (op >= 0xB8 && op <= 0xBF) { // CJNE Rn, #imm, rel
                char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0xB8);
                out.push_back(MakeLine(1, "if (" + std::string(rn) + " != " + Hex8(ln.raw[1]) + ") goto " + lbl + ";", asm_comment, ln.address));
            }
            else if (op >= 0xB6 && op <= 0xB7) { // CJNE @Ri, #imm, rel
                out.push_back(MakeLine(1, "if (iram[R" + std::string(1, '0' + (op & 1)) + "] != " + Hex8(ln.raw[1]) + ") goto " + lbl + ";", asm_comment, ln.address));
            }
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── DJNZ ──
        else if (ln.mnemonic == "DJNZ") {
            std::string lbl = GetLabel(db, ln.branch_target, false);
            if (op >= 0xD8 && op <= 0xDF) { // DJNZ Rn, rel
                char rn[4]; snprintf(rn, sizeof(rn), "R%d", op - 0xD8);
                out.push_back(MakeLine(1, "if (--" + std::string(rn) + " != 0) goto " + lbl + ";", asm_comment, ln.address));
            }
            else if (op == 0xD5) { // DJNZ direct, rel
                out.push_back(MakeLine(1, "if (--" + Direct8051(ln.raw[1]) + " != 0) goto " + lbl + ";", asm_comment, ln.address));
            }
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── MUL / DIV ──
        else if (ln.mnemonic == "MUL") {
            out.push_back(MakeLine(1, "{ uint16_t t = ACC * B; B = t >> 8; ACC = t & 0xFF; }", asm_comment, ln.address));
        }
        else if (ln.mnemonic == "DIV") {
            out.push_back(MakeLine(1, "{ uint8_t t = ACC / B; B = ACC % B; ACC = t; }", asm_comment, ln.address));
        }
        // ── DA ──
        else if (ln.mnemonic == "DA") {
            out.push_back(MakeLine(1, "ACC = bcd_adjust(ACC);", asm_comment, ln.address));
        }
        // ── Fallback ──
        else {
            out.push_back(MakeLine(1, "/* " + asm_comment + " */", !ln.comment.empty() ? ln.comment : "", ln.address));
        }
    }

    out.push_back(MakeLine(0, "}", "", 0));
    return out;
}

// ═══════════════════════════════════════════════════════════════════
//  ARM Thumb Decompiler
// ═══════════════════════════════════════════════════════════════════
static const char* ArmReg(int r) {
    static const char* names[] = {
        "r0","r1","r2","r3","r4","r5","r6","r7",
        "r8","r9","r10","r11","r12","sp","lr","pc"
    };
    return (r >= 0 && r < 16) ? names[r] : "??";
}

// Parse operands helper: splits "r0, r1, #5" into parts
static std::vector<std::string> SplitOps(const std::string& ops) {
    std::vector<std::string> parts;
    std::string cur;
    int depth = 0;
    for (char c : ops) {
        if (c == '[') depth++;
        if (c == ']') depth--;
        if (c == ',' && depth == 0) {
            // trim
            while (!cur.empty() && cur.front() == ' ') cur.erase(cur.begin());
            while (!cur.empty() && cur.back() == ' ') cur.pop_back();
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    while (!cur.empty() && cur.front() == ' ') cur.erase(cur.begin());
    while (!cur.empty() && cur.back() == ' ') cur.pop_back();
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

// Generate memory access expression from "[Rn, #off]" or "[Rn, Rm]"
static std::string MemExpr(const std::string& src, const char* cast) {
    auto br1 = src.find('['), br2 = src.find(']');
    if (br1 == std::string::npos || br2 == std::string::npos) return "load(" + src + ")";
    std::string inner = src.substr(br1 + 1, br2 - br1 - 1);
    // Trim
    while (!inner.empty() && inner.front() == ' ') inner.erase(inner.begin());
    while (!inner.empty() && inner.back() == ' ') inner.pop_back();
    auto ic = inner.find(',');
    if (ic != std::string::npos) {
        std::string base = inner.substr(0, ic);
        std::string off = inner.substr(ic + 1);
        while (!base.empty() && base.back() == ' ') base.pop_back();
        while (!off.empty() && off.front() == ' ') off.erase(off.begin());
        if (off == "#0" || off == "#0x0")
            return std::string("*(") + cast + "*)(" + base + ")";
        return std::string("*(") + cast + "*)(" + base + " + " + off + ")";
    }
    return std::string("*(") + cast + "*)(" + inner + ")";
}

std::vector<PseudoLine> DecompileThumb(
    const Function& func,
    const std::vector<DisasmLine>& all_lines,
    const AnnotationDB& db)
{
    std::vector<PseudoLine> out;
    auto lines = GetFuncLines(func, all_lines);
    if (lines.empty()) return out;

    std::string fname = db.GetFuncName(func.start, func.name.c_str());

    // Determine parameters from early register usage before any writes
    std::unordered_set<int> used_params;
    std::unordered_set<int> written;
    for (size_t i = 0; i < lines.size() && i < 30; ++i) {
        auto& ln = *lines[i];
        if (ln.mnemonic == "PUSH" || ln.mnemonic == "POP") continue;
        auto parts = SplitOps(ln.operands);
        // For most instructions, first operand is destination, rest are sources
        for (size_t p = 1; p < parts.size(); ++p) {
            for (int r = 0; r < 4; ++r) {
                if (parts[p] == ArmReg(r) && !written.count(r))
                    used_params.insert(r);
            }
        }
        // Mark destination as written
        if (!parts.empty()) {
            for (int r = 0; r < 4; ++r) {
                if (parts[0] == ArmReg(r)) written.insert(r);
            }
        }
        if (ln.mnemonic == "BL" || ln.mnemonic == "BLX") break; // past setup
    }

    // Function signature
    std::string sig = "int " + fname + "(";
    bool first = true;
    for (int p = 0; p < 4; ++p) {
        if (used_params.count(p)) {
            if (!first) sig += ", ";
            sig += "int " + std::string(ArmReg(p));
            first = false;
        }
    }
    if (first) sig += "void";
    sig += ")";

    if (!func.callers.empty()) {
        char buf[32]; snprintf(buf, sizeof(buf), "// %zu callers", func.callers.size());
        out.push_back(MakeLine(0, sig + " {", buf, func.start));
    } else {
        out.push_back(MakeLine(0, sig + " {", "", func.start));
    }

    // Branch targets for labels
    std::unordered_set<uint32_t> branch_targets;
    for (auto* ln : lines) {
        if (ln->is_branch && ln->branch_target >= func.start &&
            (func.end == 0 || ln->branch_target < func.end))
            branch_targets.insert(ln->branch_target);
    }

    // Track CMP state for branch fusion
    std::string cmp_lhs, cmp_rhs;
    bool has_cmp = false;
    int stack_frame = 0;

    for (size_t i = 0; i < lines.size(); ++i) {
        auto& ln = *lines[i];
        std::string asm_comment = ln.mnemonic + " " + ln.operands;
        std::string user_comment = db.GetAddrComment(ln.address);
        if (!user_comment.empty()) asm_comment += " // " + user_comment;
        auto parts = SplitOps(ln.operands);

        // Labels
        if (branch_targets.count(ln.address))
            out.push_back(MakeLabel(GetLabel(db, ln.address, true), ln.address));

        // ── PUSH ──
        if (ln.mnemonic == "PUSH") {
            out.push_back(MakeLine(1, "/* prologue: " + ln.operands + " */", asm_comment, ln.address));
        }
        // ── POP ──
        else if (ln.mnemonic == "POP") {
            if (ln.is_ret)
                out.push_back(MakeLine(1, "return r0;", asm_comment + " // epilogue", ln.address));
            else
                out.push_back(MakeLine(1, "/* epilogue: " + ln.operands + " */", asm_comment, ln.address));
        }
        // ── BX ──
        else if (ln.mnemonic == "BX") {
            if (ln.is_ret)
                out.push_back(MakeLine(1, "return r0;", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── BL / BLX (function call) ──
        else if (ln.mnemonic == "BL" || ln.mnemonic == "BLX") {
            std::string tn = db.GetFuncName(ln.branch_target);
            // Build param list from used registers
            std::string params;
            int max_param = -1;
            // Simple heuristic: look at preceding MOVS/MOV to r0-r3
            for (int p = 0; p <= 3; ++p) {
                // Check if r(p) was set in last 10 instructions
                bool set = false;
                for (int back = (int)i - 1; back >= 0 && back >= (int)i - 10; --back) {
                    auto& prev = *lines[back];
                    if (prev.mnemonic == "BL" || prev.mnemonic == "BLX") break; // previous call
                    auto pp = SplitOps(prev.operands);
                    if (!pp.empty() && pp[0] == ArmReg(p) &&
                        prev.mnemonic != "CMP" && prev.mnemonic != "TST" &&
                        prev.mnemonic != "PUSH") {
                        set = true; break;
                    }
                }
                if (set) max_param = p;
            }
            for (int p = 0; p <= max_param; ++p) {
                if (!params.empty()) params += ", ";
                params += ArmReg(p);
            }
            has_cmp = false;
            out.push_back(MakeLine(1, "r0 = " + tn + "(" + params + ");", asm_comment, ln.address));
        }
        // ── MOV / MOVS ──
        else if (ln.mnemonic == "MOVS" || ln.mnemonic == "MOV") {
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            has_cmp = false;
        }
        // ── MOVW / MOVT ──
        else if (ln.mnemonic == "MOVW" || ln.mnemonic == "MOVT") {
            if (parts.size() >= 2) {
                if (ln.mnemonic == "MOVT")
                    out.push_back(MakeLine(1, parts[0] + " = (" + parts[0] + " & 0xFFFF) | (" + parts[1] + " << 16);", asm_comment, ln.address));
                else
                    out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + ";", asm_comment, ln.address));
            } else {
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            }
        }
        // ── ADD / ADDS ──
        else if (ln.mnemonic == "ADDS" || ln.mnemonic == "ADD" || ln.mnemonic == "ADD.W") {
            has_cmp = false;
            if (parts.size() == 3) {
                if (parts[0] == "sp" && parts[1] == "sp")
                    out.push_back(MakeLine(1, "sp += " + parts[2] + ";", asm_comment + " // stack adjust", ln.address));
                else
                    out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " + " + parts[2] + ";", asm_comment, ln.address));
            } else if (parts.size() == 2) {
                if (parts[0] == "sp")
                    out.push_back(MakeLine(1, "sp += " + parts[1] + ";", asm_comment + " // stack adjust", ln.address));
                else
                    out.push_back(MakeLine(1, parts[0] + " += " + parts[1] + ";", asm_comment, ln.address));
            } else {
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            }
        }
        // ── SUB / SUBS ──
        else if (ln.mnemonic == "SUBS" || ln.mnemonic == "SUB" || ln.mnemonic == "SUB.W") {
            has_cmp = false;
            if (parts.size() == 3) {
                if (parts[0] == "sp" && parts[1] == "sp") {
                    int imm = 0;
                    if (parts[2].find('#') != std::string::npos)
                        imm = (int)strtol(parts[2].c_str() + parts[2].find('#') + 1, nullptr, 0);
                    stack_frame = imm;
                    out.push_back(MakeLine(1, "sp -= " + parts[2] + ";", asm_comment + " // alloc " + std::to_string(imm) + " bytes", ln.address));
                } else {
                    out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " - " + parts[2] + ";", asm_comment, ln.address));
                }
            } else if (parts.size() == 2) {
                if (parts[0] == "sp") {
                    int imm = 0;
                    if (parts[1].find('#') != std::string::npos)
                        imm = (int)strtol(parts[1].c_str() + parts[1].find('#') + 1, nullptr, 0);
                    stack_frame = imm;
                    out.push_back(MakeLine(1, "sp -= " + parts[1] + ";", asm_comment + " // alloc " + std::to_string(imm) + " bytes", ln.address));
                } else {
                    out.push_back(MakeLine(1, parts[0] + " -= " + parts[1] + ";", asm_comment, ln.address));
                }
            } else {
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            }
        }
        // ── CMP / CMN ──
        else if (ln.mnemonic == "CMP" || ln.mnemonic == "CMN") {
            if (parts.size() >= 2) {
                cmp_lhs = parts[0]; cmp_rhs = parts[1];
                has_cmp = true;
                // Don't emit CMP as a separate line - it will fuse with the branch
            } else {
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
                has_cmp = false;
            }
        }
        // ── TST ──
        else if (ln.mnemonic == "TST") {
            if (parts.size() >= 2) {
                cmp_lhs = parts[0] + " & " + parts[1];
                cmp_rhs = "0";
                has_cmp = true;
            } else {
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            }
        }
        // ── LDR / LDR.W ──
        else if (ln.mnemonic == "LDR" || ln.mnemonic == "LDR.W") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = " + MemExpr(ln.operands.substr(ln.operands.find(',') + 1), "uint32_t") + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            if (!ln.comment.empty()) out.back().comment = ln.comment;
        }
        // ── LDRB / LDRB.W ──
        else if (ln.mnemonic == "LDRB" || ln.mnemonic == "LDRB.W") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = " + MemExpr(ln.operands.substr(ln.operands.find(',') + 1), "uint8_t") + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            if (!ln.comment.empty()) out.back().comment = ln.comment;
        }
        // ── LDRH / LDRH.W ──
        else if (ln.mnemonic == "LDRH" || ln.mnemonic == "LDRH.W") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = " + MemExpr(ln.operands.substr(ln.operands.find(',') + 1), "uint16_t") + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            if (!ln.comment.empty()) out.back().comment = ln.comment;
        }
        // ── LDRSB / LDRSH ──
        else if (ln.mnemonic == "LDRSB" || ln.mnemonic == "LDRSB.W") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = " + MemExpr(ln.operands.substr(ln.operands.find(',') + 1), "int8_t") + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        else if (ln.mnemonic == "LDRSH" || ln.mnemonic == "LDRSH.W") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = " + MemExpr(ln.operands.substr(ln.operands.find(',') + 1), "int16_t") + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── STR / STR.W / STRB / STRH ──
        else if (ln.mnemonic == "STR" || ln.mnemonic == "STR.W" ||
                 ln.mnemonic == "STRB" || ln.mnemonic == "STRB.W" ||
                 ln.mnemonic == "STRH" || ln.mnemonic == "STRH.W") {
            has_cmp = false;
            const char* cast = "uint32_t";
            if (ln.mnemonic.find("STRB") == 0) cast = "uint8_t";
            else if (ln.mnemonic.find("STRH") == 0) cast = "uint16_t";
            if (parts.size() >= 2) {
                std::string addr_part = ln.operands.substr(ln.operands.find(',') + 1);
                while (!addr_part.empty() && addr_part.front() == ' ') addr_part.erase(addr_part.begin());
                auto br1 = addr_part.find('['), br2 = addr_part.find(']');
                if (br1 != std::string::npos && br2 != std::string::npos) {
                    std::string inner = addr_part.substr(br1 + 1, br2 - br1 - 1);
                    auto ic = inner.find(',');
                    if (ic != std::string::npos) {
                        std::string base = inner.substr(0, ic);
                        std::string off = inner.substr(ic + 1);
                        while (!base.empty() && base.back() == ' ') base.pop_back();
                        while (!off.empty() && off.front() == ' ') off.erase(off.begin());
                        if (off == "#0" || off == "#0x0")
                            out.push_back(MakeLine(1, std::string("*(") + cast + "*)(" + base + ") = " + parts[0] + ";", asm_comment, ln.address));
                        else
                            out.push_back(MakeLine(1, std::string("*(") + cast + "*)(" + base + " + " + off + ") = " + parts[0] + ";", asm_comment, ln.address));
                    } else {
                        out.push_back(MakeLine(1, std::string("*(") + cast + "*)(" + inner + ") = " + parts[0] + ";", asm_comment, ln.address));
                    }
                } else {
                    out.push_back(MakeLine(1, "store(" + addr_part + ", " + parts[0] + ");", asm_comment, ln.address));
                }
            } else {
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            }
        }
        // ── Shift ops: LSLS / LSRS / ASRS / LSL / LSR / ASR ──
        else if (ln.mnemonic == "LSLS" || ln.mnemonic == "LSRS" || ln.mnemonic == "ASRS" ||
                 ln.mnemonic == "LSL" || ln.mnemonic == "LSR" || ln.mnemonic == "ASR" ||
                 ln.mnemonic == "LSL.W" || ln.mnemonic == "LSR.W" || ln.mnemonic == "ASR.W") {
            has_cmp = false;
            std::string op = (ln.mnemonic.find("LSL") != std::string::npos) ? "<<" :
                             (ln.mnemonic.find("ASR") != std::string::npos) ? " >> " : ">>";
            if (parts.size() == 3)
                out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " " + op + " " + parts[2] + ";", asm_comment, ln.address));
            else if (parts.size() == 2)
                out.push_back(MakeLine(1, parts[0] + " " + op + "= " + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── Bitwise: ANDS / AND / ORRS / ORR / EORS / EOR / BICS / BIC ──
        else if (ln.mnemonic == "ANDS" || ln.mnemonic == "AND" || ln.mnemonic == "AND.W") {
            has_cmp = false;
            if (parts.size() == 3)
                out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " & " + parts[2] + ";", asm_comment, ln.address));
            else if (parts.size() == 2)
                out.push_back(MakeLine(1, parts[0] + " &= " + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        else if (ln.mnemonic == "ORRS" || ln.mnemonic == "ORR" || ln.mnemonic == "ORR.W") {
            has_cmp = false;
            if (parts.size() == 3)
                out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " | " + parts[2] + ";", asm_comment, ln.address));
            else if (parts.size() == 2)
                out.push_back(MakeLine(1, parts[0] + " |= " + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        else if (ln.mnemonic == "EORS" || ln.mnemonic == "EOR" || ln.mnemonic == "EOR.W") {
            has_cmp = false;
            if (parts.size() == 3)
                out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " ^ " + parts[2] + ";", asm_comment, ln.address));
            else if (parts.size() == 2)
                out.push_back(MakeLine(1, parts[0] + " ^= " + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        else if (ln.mnemonic == "BICS" || ln.mnemonic == "BIC" || ln.mnemonic == "BIC.W") {
            has_cmp = false;
            if (parts.size() == 3)
                out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " & ~" + parts[2] + ";", asm_comment, ln.address));
            else if (parts.size() == 2)
                out.push_back(MakeLine(1, parts[0] + " &= ~" + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── MVN / MVNS ──
        else if (ln.mnemonic == "MVNS" || ln.mnemonic == "MVN" || ln.mnemonic == "MVN.W") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = ~" + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── NEG / NEGS / RSBS ──
        else if (ln.mnemonic == "NEGS" || ln.mnemonic == "NEG" || ln.mnemonic == "RSBS") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = -" + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── MUL / MULS ──
        else if (ln.mnemonic == "MULS" || ln.mnemonic == "MUL" || ln.mnemonic == "MUL.W") {
            has_cmp = false;
            if (parts.size() == 3)
                out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " * " + parts[2] + ";", asm_comment, ln.address));
            else if (parts.size() == 2)
                out.push_back(MakeLine(1, parts[0] + " *= " + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── SDIV / UDIV ──
        else if (ln.mnemonic == "SDIV" || ln.mnemonic == "UDIV") {
            has_cmp = false;
            if (parts.size() == 3)
                out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " / " + parts[2] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── UXTB / UXTH / SXTB / SXTH ──
        else if (ln.mnemonic == "UXTB" || ln.mnemonic == "UXTB.W") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = (uint8_t)" + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        else if (ln.mnemonic == "UXTH" || ln.mnemonic == "UXTH.W") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = (uint16_t)" + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        else if (ln.mnemonic == "SXTB" || ln.mnemonic == "SXTB.W") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = (int8_t)" + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        else if (ln.mnemonic == "SXTH" || ln.mnemonic == "SXTH.W") {
            has_cmp = false;
            if (parts.size() >= 2)
                out.push_back(MakeLine(1, parts[0] + " = (int16_t)" + parts[1] + ";", asm_comment, ln.address));
            else
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── ADC / SBC / RSB ──
        else if (ln.mnemonic == "ADCS" || ln.mnemonic == "ADC" || ln.mnemonic == "ADC.W") {
            has_cmp = false;
            if (parts.size() == 3)
                out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " + " + parts[2] + " + carry;", asm_comment, ln.address));
            else if (parts.size() == 2)
                out.push_back(MakeLine(1, parts[0] + " += " + parts[1] + " + carry;", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        else if (ln.mnemonic == "SBCS" || ln.mnemonic == "SBC" || ln.mnemonic == "SBC.W") {
            has_cmp = false;
            if (parts.size() == 3)
                out.push_back(MakeLine(1, parts[0] + " = " + parts[1] + " - " + parts[2] + " - !carry;", asm_comment, ln.address));
            else if (parts.size() == 2)
                out.push_back(MakeLine(1, parts[0] + " -= " + parts[1] + " + !carry;", asm_comment, ln.address));
            else out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
        }
        // ── Conditional branches → if (condition) goto label ──
        else if (ln.is_branch && ln.mnemonic.size() >= 2 && ln.mnemonic[0] == 'B') {
            std::string mn = ln.mnemonic;
            // Strip .W or .N suffix
            if (mn.size() > 2 && (mn.substr(mn.size()-2) == ".W" || mn.substr(mn.size()-2) == ".N"))
                mn = mn.substr(0, mn.size() - 2);

            // Map condition code to operator
            std::string cond_op;
            if      (mn == "BEQ") cond_op = "==";
            else if (mn == "BNE") cond_op = "!=";
            else if (mn == "BCS" || mn == "BHS") cond_op = ">= (unsigned)";
            else if (mn == "BCC" || mn == "BLO") cond_op = "< (unsigned)";
            else if (mn == "BGT") cond_op = ">";
            else if (mn == "BLT") cond_op = "<";
            else if (mn == "BGE") cond_op = ">=";
            else if (mn == "BLE") cond_op = "<=";
            else if (mn == "BHI") cond_op = "> (unsigned)";
            else if (mn == "BLS") cond_op = "<= (unsigned)";
            else if (mn == "BMI") cond_op = "< 0";
            else if (mn == "BPL") cond_op = ">= 0";
            else if (mn == "B")   cond_op = "";

            bool in_func = ln.branch_target >= func.start && (func.end == 0 || ln.branch_target < func.end);
            std::string target = in_func ? GetLabel(db, ln.branch_target, true) : db.GetFuncName(ln.branch_target);

            if (cond_op.empty()) {
                // Unconditional branch
                if (in_func)
                    out.push_back(MakeLine(1, "goto " + target + ";", asm_comment, ln.address));
                else
                    out.push_back(MakeLine(1, target + "(); return r0;", asm_comment + " // tail call", ln.address));
            } else {
                // Build condition string with CMP fusion
                std::string cond;
                if (has_cmp && !cmp_lhs.empty()) {
                    cond = cmp_lhs + " " + cond_op + " " + cmp_rhs;
                } else {
                    cond = cond_op;
                }
                has_cmp = false;

                if (in_func)
                    out.push_back(MakeLine(1, "if (" + cond + ") goto " + target + ";", asm_comment, ln.address));
                else
                    out.push_back(MakeLine(1, "if (" + cond + ") { " + target + "(); return r0; }", asm_comment, ln.address));
            }
        }
        // ── NOP ──
        else if (ln.mnemonic == "NOP" || ln.mnemonic == "NOP.W") {
            out.push_back(MakeLine(1, "/* nop */", asm_comment, ln.address));
        }
        // ── SVC ──
        else if (ln.mnemonic == "SVC") {
            out.push_back(MakeLine(1, "svc(" + ln.operands + ");", asm_comment, ln.address));
        }
        // ── DMB / DSB / ISB ──
        else if (ln.mnemonic == "DMB" || ln.mnemonic == "DSB" || ln.mnemonic == "ISB") {
            out.push_back(MakeLine(1, "__" + ln.mnemonic + "();", asm_comment, ln.address));
        }
        // ── CPSID / CPSIE ──
        else if (ln.mnemonic == "CPSID") {
            out.push_back(MakeLine(1, "__disable_irq();", asm_comment, ln.address));
        }
        else if (ln.mnemonic == "CPSIE") {
            out.push_back(MakeLine(1, "__enable_irq();", asm_comment, ln.address));
        }
        // ── CBZ / CBNZ ──
        else if (ln.mnemonic == "CBZ" || ln.mnemonic == "CBNZ") {
            bool in_func = ln.branch_target >= func.start && (func.end == 0 || ln.branch_target < func.end);
            std::string target = in_func ? GetLabel(db, ln.branch_target, true) : db.GetFuncName(ln.branch_target);
            std::string op = (ln.mnemonic == "CBZ") ? "==" : "!=";
            if (parts.size() >= 1) {
                if (in_func)
                    out.push_back(MakeLine(1, "if (" + parts[0] + " " + op + " 0) goto " + target + ";", asm_comment, ln.address));
                else
                    out.push_back(MakeLine(1, "if (" + parts[0] + " " + op + " 0) { " + target + "(); return r0; }", asm_comment, ln.address));
            } else {
                out.push_back(MakeLine(1, "/* " + asm_comment + " */", "", ln.address));
            }
            has_cmp = false;
        }
        // ── IT (If-Then) ──
        else if (ln.mnemonic.substr(0, 2) == "IT") {
            out.push_back(MakeLine(1, "/* " + asm_comment + " */", "// conditional block", ln.address));
        }
        // ── Fallback ──
        else {
            out.push_back(MakeLine(1, "/* " + asm_comment + " */", !ln.comment.empty() ? ln.comment : "", ln.address));
            has_cmp = false;
        }
    }

    out.push_back(MakeLine(0, "}", "", 0));
    return out;
}
