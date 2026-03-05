// ═══════════════════════════════════════════════════════════════════
//  disasm.cpp — 8051 + ARM Thumb disassemblers (optimized)
// ═══════════════════════════════════════════════════════════════════
#include "disasm.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <thread>

// ═══════════════════════════════════════════════════════════════════
//  8051 Disassembler (B board / TW8836)
// ═══════════════════════════════════════════════════════════════════
struct Op8051 { const char* mn; uint8_t sz; };

// Opcode table (256 entries) — mnemonic and instruction length
static const Op8051 OPS_8051[256] = {
  {"NOP",1},{"AJMP",2},{"LJMP",3},{"RR",1},{"INC",1},{"INC",2},{"INC",1},{"INC",1},
  {"INC",1},{"INC",1},{"INC",1},{"INC",1},{"INC",1},{"INC",1},{"INC",1},{"INC",1},
  {"JBC",3},{"ACALL",2},{"LCALL",3},{"RRC",1},{"DEC",1},{"DEC",2},{"DEC",1},{"DEC",1},
  {"DEC",1},{"DEC",1},{"DEC",1},{"DEC",1},{"DEC",1},{"DEC",1},{"DEC",1},{"DEC",1},
  {"JB",3},{"AJMP",2},{"RET",1},{"RL",1},{"ADD",2},{"ADD",2},{"ADD",1},{"ADD",1},
  {"ADD",1},{"ADD",1},{"ADD",1},{"ADD",1},{"ADD",1},{"ADD",1},{"ADD",1},{"ADD",1},
  {"JNB",3},{"ACALL",2},{"RETI",1},{"RLC",1},{"ADDC",2},{"ADDC",2},{"ADDC",1},{"ADDC",1},
  {"ADDC",1},{"ADDC",1},{"ADDC",1},{"ADDC",1},{"ADDC",1},{"ADDC",1},{"ADDC",1},{"ADDC",1},
  {"JC",2},{"AJMP",2},{"ORL",2},{"ORL",2},{"ORL",2},{"ORL",2},{"ORL",1},{"ORL",1},
  {"ORL",1},{"ORL",1},{"ORL",1},{"ORL",1},{"ORL",1},{"ORL",1},{"ORL",1},{"ORL",1},
  {"JNC",2},{"ACALL",2},{"ANL",2},{"ANL",2},{"ANL",2},{"ANL",2},{"ANL",1},{"ANL",1},
  {"ANL",1},{"ANL",1},{"ANL",1},{"ANL",1},{"ANL",1},{"ANL",1},{"ANL",1},{"ANL",1},
  {"JZ",2},{"AJMP",2},{"XRL",2},{"XRL",2},{"XRL",2},{"XRL",2},{"XRL",1},{"XRL",1},
  {"XRL",1},{"XRL",1},{"XRL",1},{"XRL",1},{"XRL",1},{"XRL",1},{"XRL",1},{"XRL",1},
  {"JNZ",2},{"ACALL",2},{"ORL",2},{"JMP",1},{"MOV",2},{"MOV",3},{"MOV",2},{"MOV",2},
  {"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},
  {"SJMP",2},{"AJMP",2},{"ANL",2},{"MOVC",1},{"DIV",1},{"MOV",3},{"MOV",3},{"MOV",2},
  {"MOV",2},{"MOV",2},{"MOV",2},{"MOV",2},{"MOV",2},{"MOV",2},{"MOV",2},{"MOV",2},
  {"MOV",3},{"ACALL",2},{"MOV",2},{"MOVC",1},{"SUBB",2},{"SUBB",2},{"SUBB",1},{"SUBB",1},
  {"SUBB",1},{"SUBB",1},{"SUBB",1},{"SUBB",1},{"SUBB",1},{"SUBB",1},{"SUBB",1},{"SUBB",1},
  {"ORL",2},{"AJMP",2},{"MOV",2},{"INC",1},{"MUL",1},{"???",1},{"MOV",3},{"MOV",2},
  {"MOV",2},{"MOV",2},{"MOV",2},{"MOV",2},{"MOV",2},{"MOV",2},{"MOV",2},{"MOV",2},
  {"ANL",2},{"ACALL",2},{"CPL",2},{"CPL",1},{"CJNE",3},{"CJNE",3},{"CJNE",3},{"CJNE",3},
  {"CJNE",3},{"CJNE",3},{"CJNE",3},{"CJNE",3},{"CJNE",3},{"CJNE",3},{"CJNE",3},{"CJNE",3},
  {"PUSH",2},{"AJMP",2},{"CLR",2},{"CLR",1},{"SWAP",1},{"XCH",2},{"XCH",1},{"XCH",1},
  {"XCH",1},{"XCH",1},{"XCH",1},{"XCH",1},{"XCH",1},{"XCH",1},{"XCH",1},{"XCH",1},
  {"POP",2},{"ACALL",2},{"SETB",2},{"SETB",1},{"DA",1},{"DJNZ",3},{"XCHD",1},{"XCHD",1},
  {"DJNZ",2},{"DJNZ",2},{"DJNZ",2},{"DJNZ",2},{"DJNZ",2},{"DJNZ",2},{"DJNZ",2},{"DJNZ",2},
  {"MOVX",1},{"AJMP",2},{"MOVX",1},{"MOVX",1},{"CLR",1},{"MOV",2},{"MOV",1},{"MOV",1},
  {"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},
  {"MOVX",1},{"ACALL",2},{"MOVX",1},{"MOVX",1},{"CPL",1},{"MOV",2},{"MOV",1},{"MOV",1},
  {"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},{"MOV",1},
};

void Disasm8051::Disassemble(const uint8_t* data, uint32_t size, uint32_t base) {
    lines_.clear(); xrefs_.clear(); functions_.clear();

    uint32_t i = 0;
    while (i < size) {
        // Report progress every 4KB
        if (progress_cb_ && (i & 0xFFF) == 0)
            progress_cb_((float)i / size * 0.80f);

        uint8_t op = data[i];
        auto& e = OPS_8051[op];
        uint8_t sz = e.sz;
        if (i + sz > size) break;

        DisasmLine ln;
        ln.address = base + i;
        ln.size = sz;
        ln.mnemonic = e.mn;
        ln.is_branch = ln.is_call = ln.is_ret = false;
        ln.raw.assign(data + i, data + i + sz);

        char buf[64];
        switch (op) {
            case 0x02: // LJMP addr16
            case 0x12: // LCALL addr16
            {
                uint16_t tgt = ((uint16_t)data[i+1] << 8) | data[i+2];
                snprintf(buf, sizeof(buf), "0x%04X", tgt);
                ln.operands = buf;
                ln.is_branch = (op == 0x02); ln.is_call = (op == 0x12);
                ln.branch_target = tgt;
                XRef x; x.from = ln.address; x.to = tgt;
                x.type = ln.is_call ? XRefType::CALL : XRefType::BRANCH;
                xrefs_.push_back(x);
                break;
            }
            case 0x80: // SJMP rel
            case 0x40: case 0x50: case 0x60: case 0x70: // JC/JNC/JZ/JNZ
            {
                int8_t rel = (int8_t)data[i+1];
                uint32_t tgt = base + i + sz + rel;
                snprintf(buf, sizeof(buf), "0x%04X", tgt);
                ln.operands = buf;
                ln.is_branch = true; ln.branch_target = tgt;
                XRef x; x.from = ln.address; x.to = tgt; x.type = XRefType::BRANCH;
                xrefs_.push_back(x);
                break;
            }
            case 0x22: ln.is_ret = true; break;   // RET
            case 0x32: ln.is_ret = true; break;   // RETI
            default:
                // Format remaining operands generically
                if (sz == 2) {
                    snprintf(buf, sizeof(buf), "0x%02X", data[i+1]);
                    ln.operands = buf;
                } else if (sz == 3) {
                    snprintf(buf, sizeof(buf), "0x%02X, 0x%02X", data[i+1], data[i+2]);
                    ln.operands = buf;
                }
                break;
        }
        // AJMP / ACALL (xxx0_0001 / xxx1_0001)
        if ((op & 0x1F) == 0x01 || (op & 0x1F) == 0x11) {
            uint16_t pg = (base + i + 2) & 0xF800;
            uint16_t a10_8 = ((uint16_t)(op >> 5) & 7) << 8;
            uint16_t tgt = pg | a10_8 | data[i+1];
            snprintf(buf, sizeof(buf), "0x%04X", tgt);
            ln.operands = buf;
            ln.is_call = ((op & 0x1F) == 0x11);
            ln.is_branch = !ln.is_call;
            ln.branch_target = tgt;
            XRef x; x.from = ln.address; x.to = tgt;
            x.type = ln.is_call ? XRefType::CALL : XRefType::BRANCH;
            xrefs_.push_back(x);
        }

        lines_.push_back(ln);
        i += sz;
    }

    if (progress_cb_) progress_cb_(0.82f);
    DetectFunctions();
    if (progress_cb_) progress_cb_(0.92f);
    AnnotateStrings(data, size, base);
    if (progress_cb_) progress_cb_(1.0f);
}

// ── Helper: parallel for_each over a range of indices ────────────────
static void ParallelFor(size_t count, std::function<void(size_t, size_t)> work) {
    if (count == 0) return;
    unsigned n = std::max(1u, std::thread::hardware_concurrency());
    if (n > count) n = (unsigned)count;
    if (n <= 1) { work(0, count); return; }

    std::vector<std::thread> threads;
    size_t chunk = (count + n - 1) / n;
    for (unsigned t = 0; t < n; ++t) {
        size_t lo = t * chunk;
        size_t hi = std::min(lo + chunk, count);
        if (lo >= hi) break;
        threads.emplace_back(work, lo, hi);
    }
    for (auto& t : threads) t.join();
}

void Disasm8051::DetectFunctions() {
    // ── 1. Build caller map: call-target → list of caller addresses ──
    //    O(X) instead of the old O(T × X) nested scan
    std::unordered_map<uint32_t, std::vector<uint32_t>> caller_map;
    for (auto& x : xrefs_)
        if (x.type == XRefType::CALL)
            caller_map[x.to].push_back(x.from);

    // ── 2. Create function stubs with callers already populated ──
    std::vector<uint32_t> targets;
    targets.reserve(caller_map.size());
    for (auto& [tgt, _] : caller_map) targets.push_back(tgt);
    std::sort(targets.begin(), targets.end());

    char name[32];
    functions_.reserve(targets.size());
    for (auto t : targets) {
        Function f;
        f.start = t; f.end = 0;
        snprintf(name, sizeof(name), "sub_%04X", t);
        f.name = name;
        f.callers = std::move(caller_map[t]);
        functions_.push_back(std::move(f));
    }

    // ── 3. Set end addresses ──
    std::sort(functions_.begin(), functions_.end(),
              [](const Function& a, const Function& b) { return a.start < b.start; });
    for (size_t fi = 0; fi < functions_.size(); ++fi)
        functions_[fi].end = (fi + 1 < functions_.size()) ? functions_[fi+1].start : 0;

    // ── 4. Populate function lines — binary search + parallel ──
    //    Old: O(F × L) nested loop.  New: O(F × log(L)) with threads.
    ParallelFor(functions_.size(), [this](size_t lo, size_t hi) {
        for (size_t fi = lo; fi < hi; ++fi) {
            auto& f = functions_[fi];
            auto it = std::lower_bound(lines_.begin(), lines_.end(), f.start,
                [](const DisasmLine& l, uint32_t a) { return l.address < a; });
            for (; it != lines_.end(); ++it) {
                if (f.end != 0 && it->address >= f.end) break;
                f.lines.push_back(*it);
            }
        }
    });
}

void Disasm8051::AnnotateStrings(const uint8_t* data, uint32_t size, uint32_t base) {
    // ── 1. Build string table: address → text  (O(size)) ──
    //    Old code rescanned ALL lines_ for EVERY string found = O(S × L).
    std::unordered_map<uint32_t, std::string> str_map;
    for (uint32_t i = 0; i < size;) {
        if (data[i] >= 32 && data[i] < 127) {
            uint32_t start = i;
            while (i < size && data[i] >= 32 && data[i] < 127) ++i;
            if (i - start >= 4) {
                uint32_t len = std::min(i - start, 40u);
                str_map[base + start] = std::string((const char*)&data[start], len);
            }
        } else ++i;
    }

    // ── 2. Single pass over lines — O(L) with O(1) hash lookups ──
    for (auto& ln : lines_) {
        // Check branch target directly
        if (ln.branch_target != 0) {
            auto it = str_map.find(ln.branch_target);
            if (it != str_map.end()) {
                ln.comment = "\"" + it->second + "\"";
                continue;
            }
        }
        // Parse any hex addresses from operands
        const char* p = ln.operands.c_str();
        while (const char* found = strstr(p, "0x")) {
            uint32_t addr = (uint32_t)strtoul(found, nullptr, 16);
            auto it = str_map.find(addr);
            if (it != str_map.end()) {
                ln.comment = "\"" + it->second + "\"";
                break;
            }
            p = found + 2;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
//  ARM Thumb/Thumb-2 Disassembler (A board / Cortex-M4)
// ═══════════════════════════════════════════════════════════════════
static uint16_t ThumbLE16(const uint8_t* p) { return p[0] | (p[1]<<8); }

static const char* REG_NAMES[] = {
    "r0","r1","r2","r3","r4","r5","r6","r7",
    "r8","r9","r10","r11","r12","sp","lr","pc"
};

static std::string RegList(uint16_t mask) {
    std::string s = "{";
    bool first = true;
    for (int i = 0; i < 16; ++i) {
        if (mask & (1 << i)) {
            if (!first) s += ", ";
            s += REG_NAMES[i]; first = false;
        }
    }
    return s + "}";
}

void DisasmThumb::Disassemble(const uint8_t* data, uint32_t size, uint32_t base) {
    lines_.clear(); xrefs_.clear(); functions_.clear();

    uint32_t i = 0;
    while (i + 2 <= size) {
        // Report progress every 4KB
        if (progress_cb_ && (i & 0xFFF) == 0)
            progress_cb_((float)i / size * 0.80f);

        uint16_t hw1 = ThumbLE16(&data[i]);
        DisasmLine ln;
        ln.address = base + i;
        ln.is_branch = ln.is_call = ln.is_ret = false;
        char buf[128];

        // Is this a 32-bit Thumb-2 instruction?
        bool is32 = ((hw1 >> 11) >= 0x1D);

        if (is32 && i + 4 <= size) {
            uint16_t hw2 = ThumbLE16(&data[i+2]);
            ln.size = 4;
            ln.raw.assign(data + i, data + i + 4);

            // BL / BLX (T1)
            if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0xC000) == 0xC000) {
                uint32_t S  = (hw1 >> 10) & 1;
                uint32_t J1 = (hw2 >> 13) & 1;
                uint32_t J2 = (hw2 >> 11) & 1;
                uint32_t I1 = !(J1 ^ S); uint32_t I2 = !(J2 ^ S);
                uint32_t imm11 = hw2 & 0x7FF;
                uint32_t imm10 = hw1 & 0x3FF;
                int32_t offset = (int32_t)((S << 24) | (I1 << 23) | (I2 << 22) |
                                           (imm10 << 12) | (imm11 << 1));
                if (S) offset |= 0xFE000000; // sign extend
                uint32_t tgt = base + i + 4 + offset;
                bool isX = ((hw2 >> 12) & 1) == 0;
                ln.mnemonic = isX ? "BLX" : "BL";
                snprintf(buf, sizeof(buf), "0x%08X", tgt);
                ln.operands = buf; ln.is_call = true; ln.branch_target = tgt;
                XRef x; x.from = ln.address; x.to = tgt; x.type = XRefType::CALL;
                xrefs_.push_back(x);
            }
            // MOVW / MOVT
            else if ((hw1 & 0xFBF0) == 0xF240 || (hw1 & 0xFBF0) == 0xF2C0) {
                bool isT = (hw1 & 0xFBF0) == 0xF2C0;
                uint8_t rd = (hw2 >> 8) & 0xF;
                uint16_t i_bit = (hw1 >> 10) & 1;
                uint16_t imm4 = hw1 & 0xF;
                uint16_t imm3 = (hw2 >> 12) & 7;
                uint16_t imm8 = hw2 & 0xFF;
                uint16_t imm16 = (imm4 << 12) | (i_bit << 11) | (imm3 << 8) | imm8;
                ln.mnemonic = isT ? "MOVT" : "MOVW";
                snprintf(buf, sizeof(buf), "%s, #0x%04X", REG_NAMES[rd], imm16);
                ln.operands = buf;
            }
            // LDR.W / STR.W
            else if ((hw1 & 0xFFF0) == 0xF8D0) {
                uint8_t rn = hw1 & 0xF, rt = (hw2 >> 12) & 0xF;
                uint16_t imm12 = hw2 & 0xFFF;
                ln.mnemonic = "LDR.W";
                snprintf(buf, sizeof(buf), "%s, [%s, #0x%X]", REG_NAMES[rt], REG_NAMES[rn], imm12);
                ln.operands = buf;
            }
            else if ((hw1 & 0xFFF0) == 0xF8C0) {
                uint8_t rn = hw1 & 0xF, rt = (hw2 >> 12) & 0xF;
                uint16_t imm12 = hw2 & 0xFFF;
                ln.mnemonic = "STR.W";
                snprintf(buf, sizeof(buf), "%s, [%s, #0x%X]", REG_NAMES[rt], REG_NAMES[rn], imm12);
                ln.operands = buf;
            }
            // B.W (conditional/unconditional wide branch)
            else if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0xD000) == 0x8000) {
                uint32_t S = (hw1 >> 10) & 1;
                uint32_t cond = (hw1 >> 6) & 0xF;
                uint32_t imm6 = hw1 & 0x3F;
                uint32_t J1 = (hw2 >> 13) & 1;
                uint32_t J2 = (hw2 >> 11) & 1;
                uint32_t imm11 = hw2 & 0x7FF;
                int32_t offset = (int32_t)((S << 20) | (J2 << 19) | (J1 << 18) |
                                           (imm6 << 12) | (imm11 << 1));
                if (S) offset |= 0xFFE00000;
                uint32_t tgt = base + i + 4 + offset;
                const char* cc[] = {"BEQ.W","BNE.W","BCS.W","BCC.W","BMI.W","BPL.W","BVS.W","BVC.W",
                                    "BHI.W","BLS.W","BGE.W","BLT.W","BGT.W","BLE.W","B.W","B.W"};
                ln.mnemonic = cc[cond & 0xF];
                snprintf(buf, sizeof(buf), "0x%08X", tgt);
                ln.operands = buf; ln.is_branch = true; ln.branch_target = tgt;
                XRef x; x.from = ln.address; x.to = tgt; x.type = XRefType::BRANCH;
                xrefs_.push_back(x);
            }
            else {
                ln.mnemonic = "DCD.W";
                snprintf(buf, sizeof(buf), "0x%04X, 0x%04X", hw1, hw2);
                ln.operands = buf;
            }

            lines_.push_back(ln);
            i += 4;
            continue;
        }

        // 16-bit Thumb instructions
        ln.size = 2;
        ln.raw.assign(data + i, data + i + 2);

        // LSL/LSR/ASR imm
        if ((hw1 >> 13) == 0) {
            uint8_t op2 = (hw1 >> 11) & 3;
            uint8_t imm5 = (hw1 >> 6) & 0x1F;
            uint8_t rm = (hw1 >> 3) & 7, rd = hw1 & 7;
            const char* ops[] = {"LSLS","LSRS","ASRS"};
            if (op2 < 3) {
                ln.mnemonic = ops[op2];
                snprintf(buf, sizeof(buf), "%s, %s, #%d", REG_NAMES[rd], REG_NAMES[rm], imm5);
                ln.operands = buf;
            }
        }
        // ADD/SUB (3 reg, imm3)
        else if ((hw1 >> 11) == 3) {
            uint8_t op = (hw1 >> 9) & 3;
            uint8_t rn_or_imm = (hw1 >> 6) & 7;
            uint8_t rs = (hw1 >> 3) & 7, rd = hw1 & 7;
            if (op == 0) { ln.mnemonic = "ADDS"; snprintf(buf,sizeof(buf),"%s, %s, %s",REG_NAMES[rd],REG_NAMES[rs],REG_NAMES[rn_or_imm]); }
            else if (op == 1) { ln.mnemonic = "SUBS"; snprintf(buf,sizeof(buf),"%s, %s, %s",REG_NAMES[rd],REG_NAMES[rs],REG_NAMES[rn_or_imm]); }
            else if (op == 2) { ln.mnemonic = "ADDS"; snprintf(buf,sizeof(buf),"%s, %s, #%d",REG_NAMES[rd],REG_NAMES[rs],rn_or_imm); }
            else { ln.mnemonic = "SUBS"; snprintf(buf,sizeof(buf),"%s, %s, #%d",REG_NAMES[rd],REG_NAMES[rs],rn_or_imm); }
            ln.operands = buf;
        }
        // MOV/CMP/ADD/SUB imm8
        else if ((hw1 >> 13) == 1) {
            uint8_t op = (hw1 >> 11) & 3;
            uint8_t rd = (hw1 >> 8) & 7;
            uint8_t imm8 = hw1 & 0xFF;
            const char* ops[] = {"MOVS","CMP","ADDS","SUBS"};
            ln.mnemonic = ops[op];
            snprintf(buf, sizeof(buf), "%s, #0x%02X", REG_NAMES[rd], imm8);
            ln.operands = buf;
        }
        // LDR (literal pool — PC-relative)
        else if ((hw1 >> 11) == 0x09) {
            uint8_t rt = (hw1 >> 8) & 7;
            uint8_t imm8 = hw1 & 0xFF;
            uint32_t pc_val = (base + i + 4) & ~3u;
            uint32_t addr = pc_val + imm8 * 4;
            ln.mnemonic = "LDR";
            snprintf(buf, sizeof(buf), "%s, [PC, #0x%X] ; =0x%08X", REG_NAMES[rt], imm8*4, addr);
            ln.operands = buf;
            XRef x; x.from = ln.address; x.to = addr; x.type = XRefType::DATA_REF;
            xrefs_.push_back(x);
        }
        // LDR/STR (reg offset)
        else if ((hw1 >> 12) == 5) {
            uint8_t op = (hw1 >> 9) & 7;
            uint8_t rm = (hw1 >> 6) & 7, rn = (hw1 >> 3) & 7, rt = hw1 & 7;
            const char* ops[] = {"STR","STRH","STRB","LDRSB","LDR","LDRH","LDRB","LDRSH"};
            ln.mnemonic = ops[op];
            snprintf(buf, sizeof(buf), "%s, [%s, %s]", REG_NAMES[rt], REG_NAMES[rn], REG_NAMES[rm]);
            ln.operands = buf;
        }
        // LDR/STR imm5
        else if ((hw1 >> 13) == 3) {
            bool isB = (hw1 >> 12) & 1;
            bool isL = (hw1 >> 11) & 1;
            uint8_t imm5 = (hw1 >> 6) & 0x1F;
            uint8_t rn = (hw1 >> 3) & 7, rt = hw1 & 7;
            if (isB) { ln.mnemonic = isL ? "LDRB" : "STRB"; }
            else { ln.mnemonic = isL ? "LDR" : "STR"; imm5 <<= 2; }
            snprintf(buf, sizeof(buf), "%s, [%s, #0x%X]", REG_NAMES[rt], REG_NAMES[rn], imm5);
            ln.operands = buf;
        }
        // PUSH / POP
        else if ((hw1 & 0xFE00) == 0xB400) {
            bool isR = (hw1 >> 8) & 1;
            uint16_t rlist = hw1 & 0xFF;
            if (isR) rlist |= (1 << 14); // LR
            ln.mnemonic = "PUSH";
            ln.operands = RegList(rlist);
        }
        else if ((hw1 & 0xFE00) == 0xBC00) {
            bool isR = (hw1 >> 8) & 1;
            uint16_t rlist = hw1 & 0xFF;
            if (isR) rlist |= (1 << 15); // PC
            ln.mnemonic = "POP";
            ln.operands = RegList(rlist);
            if (isR) ln.is_ret = true;
        }
        // Conditional branch
        else if ((hw1 >> 12) == 0xD && ((hw1 >> 8) & 0xF) < 0xE) {
            uint8_t cond = (hw1 >> 8) & 0xF;
            int8_t off = (int8_t)(hw1 & 0xFF);
            uint32_t tgt = base + i + 4 + off * 2;
            const char* cc[] = {"BEQ","BNE","BCS","BCC","BMI","BPL","BVS","BVC",
                                "BHI","BLS","BGE","BLT","BGT","BLE"};
            ln.mnemonic = cc[cond];
            snprintf(buf, sizeof(buf), "0x%08X", tgt);
            ln.operands = buf; ln.is_branch = true; ln.branch_target = tgt;
            XRef x; x.from = ln.address; x.to = tgt; x.type = XRefType::BRANCH;
            xrefs_.push_back(x);
        }
        // Unconditional branch
        else if ((hw1 >> 11) == 0x1C) {
            int32_t off = (int32_t)(hw1 & 0x7FF);
            if (off & 0x400) off |= 0xFFFFF800;
            uint32_t tgt = base + i + 4 + off * 2;
            ln.mnemonic = "B";
            snprintf(buf, sizeof(buf), "0x%08X", tgt);
            ln.operands = buf; ln.is_branch = true; ln.branch_target = tgt;
            XRef x; x.from = ln.address; x.to = tgt; x.type = XRefType::BRANCH;
            xrefs_.push_back(x);
        }
        // BX / BLX (register)
        else if ((hw1 & 0xFF00) == 0x4700) {
            uint8_t rm = (hw1 >> 3) & 0xF;
            bool isL = (hw1 >> 7) & 1;
            ln.mnemonic = isL ? "BLX" : "BX";
            snprintf(buf, sizeof(buf), "%s", REG_NAMES[rm]);
            ln.operands = buf;
            if (!isL && rm == 14) ln.is_ret = true;
            if (isL) ln.is_call = true;
        }
        // MOV/ADD high registers
        else if ((hw1 & 0xFC00) == 0x4400) {
            uint8_t op = (hw1 >> 8) & 3;
            uint8_t D = (hw1 >> 7) & 1;
            uint8_t rm = (hw1 >> 3) & 0xF;
            uint8_t rd = (hw1 & 7) | (D << 3);
            if (op == 0) { ln.mnemonic = "ADD"; }
            else if (op == 1) { ln.mnemonic = "CMP"; }
            else { ln.mnemonic = "MOV"; }
            snprintf(buf, sizeof(buf), "%s, %s", REG_NAMES[rd], REG_NAMES[rm]);
            ln.operands = buf;
        }
        // ADD SP, imm7
        else if ((hw1 & 0xFF80) == 0xB000) {
            uint8_t imm7 = hw1 & 0x7F;
            bool neg = (hw1 >> 7) & 1;
            ln.mnemonic = neg ? "SUB" : "ADD";
            snprintf(buf, sizeof(buf), "sp, #0x%X", imm7 * 4);
            ln.operands = buf;
        }
        // SVC
        else if ((hw1 >> 8) == 0xDF) {
            ln.mnemonic = "SVC";
            snprintf(buf, sizeof(buf), "#%d", hw1 & 0xFF);
            ln.operands = buf;
        }
        // NOP
        else if (hw1 == 0xBF00) {
            ln.mnemonic = "NOP";
        }
        else {
            ln.mnemonic = "DCW";
            snprintf(buf, sizeof(buf), "0x%04X", hw1);
            ln.operands = buf;
        }

        lines_.push_back(ln);
        i += 2;
    }

    if (progress_cb_) progress_cb_(0.82f);
    DetectFunctions();
    if (progress_cb_) progress_cb_(0.92f);
    AnnotateStrings(data, size, base);
    if (progress_cb_) progress_cb_(1.0f);
}

void DisasmThumb::DetectFunctions() {
    // ── 1. Find function starts from PUSH {.., LR} ──
    for (auto& ln : lines_) {
        if (ln.mnemonic == "PUSH" && ln.operands.find("lr") != std::string::npos) {
            Function f;
            f.start = ln.address; f.end = 0;
            char name[32]; snprintf(name, sizeof(name), "sub_%08X", ln.address);
            f.name = name;
            functions_.push_back(f);
        }
    }

    // ── 2. Set function end addresses ──
    for (size_t fi = 0; fi < functions_.size(); ++fi)
        functions_[fi].end = (fi + 1 < functions_.size()) ? functions_[fi+1].start : 0;

    // ── 3. Match callers — O(X) with hash map instead of O(X × F) ──
    std::unordered_map<uint32_t, std::vector<size_t>> func_by_start;
    for (size_t i = 0; i < functions_.size(); ++i)
        func_by_start[functions_[i].start].push_back(i);

    for (auto& x : xrefs_) {
        if (x.type != XRefType::CALL) continue;
        auto it = func_by_start.find(x.to);
        if (it != func_by_start.end())
            for (auto fi : it->second)
                functions_[fi].callers.push_back(x.from);
    }

    // ── 4. Populate function lines — binary search + parallel ──
    //    Old: O(F × L) nested loop.  New: O(F × log(L)) with threads.
    ParallelFor(functions_.size(), [this](size_t lo, size_t hi) {
        for (size_t fi = lo; fi < hi; ++fi) {
            auto& f = functions_[fi];
            auto it = std::lower_bound(lines_.begin(), lines_.end(), f.start,
                [](const DisasmLine& l, uint32_t a) { return l.address < a; });
            for (; it != lines_.end(); ++it) {
                if (f.end != 0 && it->address >= f.end) break;
                f.lines.push_back(*it);
            }
        }
    });
}

void DisasmThumb::AnnotateStrings(const uint8_t* data, uint32_t size, uint32_t base) {
    // ── 1. Build string map: address → text  (O(size)) ──
    //    Old code iterated ALL strings for EVERY LDR = O(L × S).
    std::unordered_map<uint32_t, std::string> str_map;
    for (uint32_t i = 0; i < size;) {
        if (data[i] >= 32 && data[i] < 127) {
            uint32_t start = i;
            while (i < size && data[i] >= 32 && data[i] < 127) ++i;
            if (i - start >= 4) {
                uint32_t len = std::min(i - start, 48u);
                str_map[base + start] = std::string((const char*)&data[start], len);
            }
        } else ++i;
    }

    // ── 2. Single pass over LDR PC-relative — O(L) with O(1) lookups ──
    for (auto& ln : lines_) {
        if (ln.mnemonic != "LDR" || ln.operands.find("PC") == std::string::npos) continue;
        auto eq = ln.operands.find("=0x");
        if (eq == std::string::npos) continue;
        uint32_t pool_addr = (uint32_t)strtoul(ln.operands.c_str() + eq + 1, nullptr, 16);
        if (pool_addr < base || pool_addr >= base + size - 4) continue;
        uint32_t val = data[pool_addr - base] | (data[pool_addr - base + 1] << 8) |
                       (data[pool_addr - base + 2] << 16) | (data[pool_addr - base + 3] << 24);
        auto it = str_map.find(val);
        if (it != str_map.end())
            ln.comment = "\"" + it->second + "\"";
    }
}
