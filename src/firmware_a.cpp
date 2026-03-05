// ═══════════════════════════════════════════════════════════════════
//  firmware_a.cpp — A board (MK22F ARM Cortex-M4) firmware parser
// ═══════════════════════════════════════════════════════════════════
#include "firmware_a.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <set>

static std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg(); f.seekg(0);
    std::vector<uint8_t> buf((size_t)sz);
    f.read((char*)buf.data(), sz);
    return buf;
}

static uint32_t LE32(const uint8_t* p) {
    return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
}
static uint16_t LE16(const uint8_t* p) {
    return p[0] | (p[1]<<8);
}

// ── ARM Thumb-2 MOVW helpers ─────────────────────────────────────
uint16_t FirmwareABoard::DecodeMOVW(const uint8_t* p) {
    uint16_t hw1 = LE16(p), hw2 = LE16(p+2);
    uint8_t i    = (hw1 >> 10) & 1;
    uint8_t imm4 = hw1 & 0xF;
    uint8_t imm3 = (hw2 >> 12) & 0x7;
    uint8_t imm8 = hw2 & 0xFF;
    return (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8;
}

void FirmwareABoard::EncodeMOVW(uint8_t* p, uint8_t rd, uint16_t imm16) {
    uint8_t imm4 = (imm16 >> 12) & 0xF;
    uint8_t i    = (imm16 >> 11) & 1;
    uint8_t imm3 = (imm16 >> 8) & 0x7;
    uint8_t imm8 = imm16 & 0xFF;
    uint16_t hw1 = 0xF240 | (i << 10) | imm4;
    uint16_t hw2 = (imm3 << 12) | ((uint16_t)rd << 8) | imm8;
    p[0] = hw1 & 0xFF; p[1] = (hw1 >> 8) & 0xFF;
    p[2] = hw2 & 0xFF; p[3] = (hw2 >> 8) & 0xFF;
}

// ── Load / Save ──────────────────────────────────────────────────
bool FirmwareABoard::Load(const std::string& path) {
    raw_ = ReadFile(path);
    if (raw_.size() < PAYLOAD_START + 100 || raw_.size() > 300000) return false;
    if (raw_[0] != 0x04) return false;

    is_040_ = (raw_[1] == 0x10);

    auto pos = path.find_last_of("/\\");
    filename_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;

    Decode();
    if (decoded_.size() < 8) return false;

    sp_init_   = LE32(&decoded_[0]);
    reset_vec_ = LE32(&decoded_[4]);

    FindCodeBoundary();
    FindStrings();
    FindTimingSites();
    FindPanelInits();
    return true;
}

bool FirmwareABoard::Save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((char*)raw_.data(), raw_.size());
    return true;
}

void FirmwareABoard::Decode() {
    uint32_t payload_len = (uint32_t)raw_.size() - PAYLOAD_START;
    decoded_.resize(payload_len);
    for (uint32_t i = 0; i < payload_len; ++i) {
        uint8_t key = (0x55 + i / 512) & 0xFF;
        decoded_[i] = raw_[PAYLOAD_START + i] ^ key;
    }
}

void FirmwareABoard::WriteDecoded(uint32_t poff, uint8_t val) {
    decoded_[poff] = val;
    uint8_t key = (0x55 + poff / 512) & 0xFF;
    raw_[PAYLOAD_START + poff] = val ^ key;
}

void FirmwareABoard::SetFramePeriod(FrameTimingSite& s, uint16_t us) {
    s.value_us = us;
    uint8_t rd = s.arm_register;
    uint8_t buf[4];
    EncodeMOVW(buf, rd, us);
    for (int i = 0; i < 4; ++i)
        WriteDecoded(s.payload_offset + i, buf[i]);
}

void FirmwareABoard::SetPanelValue(PanelInitEntry& e, uint8_t val) {
    e.value = val;
    WriteDecoded(e.payload_offset, val);
}

// ── Code boundary ────────────────────────────────────────────────
void FirmwareABoard::FindCodeBoundary() {
    const char* markers[] = {"VSTATE","ASTATE","HDCP","SyncWait",
                             "ColorDetect","WaitForReady"};
    uint32_t earliest = (uint32_t)decoded_.size();
    for (auto m : markers) {
        size_t mlen = strlen(m);
        for (uint32_t i = 0; i + mlen < (uint32_t)decoded_.size(); ++i) {
            if (memcmp(&decoded_[i], m, mlen) == 0) {
                earliest = std::min(earliest, i);
                break;
            }
        }
    }
    code_boundary_ = (earliest / 256) * 256;
}

// ── String extraction ────────────────────────────────────────────
void FirmwareABoard::FindStrings() {
    strings_.clear();
    build_date_.clear();
    std::string run;
    uint32_t run_start = 0;
    for (uint32_t i = 0; i < (uint32_t)decoded_.size(); ++i) {
        uint8_t b = decoded_[i];
        if (b >= 32 && b < 127) {
            if (run.empty()) run_start = i;
            run += (char)b;
        } else {
            if (run.size() >= 6) {
                StringEntry se;
                se.payload_offset = run_start;
                se.text = run;
                se.section = (run_start < code_boundary_) ? "code" : "data";
                strings_.push_back(se);
                const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                        "Jul","Aug","Sep","Oct","Nov","Dec"};
                for (auto mo : months) {
                    auto p = run.find(mo);
                    if (p != std::string::npos && run.find("20", p) != std::string::npos) {
                        build_date_ = run;
                        break;
                    }
                }
            }
            run.clear();
        }
    }
}

// ── Frame timing sites ──────────────────────────────────────────
void FirmwareABoard::FindTimingSites() {
    timing_sites_.clear();
    uint16_t targets[] = {FRAME_PERIOD_STOCK, FRAME_PERIOD_120, FRAME_PERIOD_144};
    if (decoded_.size() < code_boundary_ + 4) return;

    for (uint32_t i = 0; i + 4 <= code_boundary_; i += 2) {
        uint16_t hw1 = LE16(&decoded_[i]);
        if ((hw1 & 0xFBF0) != 0xF240) continue;
        uint16_t imm16 = DecodeMOVW(&decoded_[i]);
        for (auto t : targets) {
            if (imm16 == t) {
                uint16_t hw2 = LE16(&decoded_[i+2]);
                uint8_t rd = (hw2 >> 8) & 0xF;
                FrameTimingSite s;
                s.payload_offset = i;
                s.arm_register = rd;
                s.value_us = imm16;
                s.original_us = imm16;
                timing_sites_.push_back(s);
                break;
            }
        }
    }
}

// ── Panel init tables (matches Python _find_panel_init_tables) ──
void FirmwareABoard::FindPanelInits() {
    panel_inits_.clear();
    if (code_boundary_ < 8) return;

    // I2C signature: MOVS R1, #addr; MOVS R0, #0
    // Primary display uses 0x4D, secondary display uses 0x4C
    const uint8_t i2c_sig_tail[] = {0x21, 0x00, 0x20}; // common tail bytes

    // ── Step 1: Find all I2C pattern hits with preceding LDRH + ADDS ──
    struct Hit { uint32_t offset; uint8_t rn; };
    std::vector<Hit> hits;

    for (uint32_t i = 4; i + 4 <= code_boundary_; i += 2) {
        uint8_t addr_byte = decoded_[i];
        if (addr_byte != 0x4D && addr_byte != 0x4C) continue;
        if (memcmp(&decoded_[i + 1], i2c_sig_tail, 3) != 0) continue;

        // Check preceding 4 bytes: LDRH Rt,[Rn,#0] + ADDS R3,Rn,#2
        // Layout: [i-4]=LDRH.lo [i-3]=LDRH.hi [i-2]=ADDS.lo [i-1]=ADDS.hi
        uint8_t ldrh_hi = decoded_[i - 3];   // should be 0x88 (LDRH encoding)
        uint8_t adds_hi = decoded_[i - 1];   // should be 0x1C (ADDS encoding)

        if (ldrh_hi != 0x88 || adds_hi != 0x1C) continue;

        // Extract Rn from LDRH low byte: LDRH Rt, [Rn, #0] → bits [5:3] = Rn
        uint8_t ldrh_lo = decoded_[i - 4];
        uint8_t rn = (ldrh_lo >> 3) & 0x07;

        // Verify Rt == 2 (LDRH R2, [Rn, #0])
        uint8_t rt = ldrh_lo & 0x07;
        if (rt != 2) continue;

        hits.push_back({i - 4, rn});
    }

    if (hits.empty()) return;

    // ── Step 2: For each hit, find enclosing function and score ──
    struct FuncCandidate {
        uint32_t push_off;
        uint8_t  table_reg;
        int      score;
        struct LdrEntry { uint32_t off; uint8_t rd; uint32_t pool_val; };
        std::vector<LdrEntry> ldr_pcs;
    };

    FuncCandidate best;
    best.push_off = 0;
    best.score = -1;

    for (auto& h : hits) {
        // Walk backward to find PUSH {.., LR} (high byte = 0xB5)
        uint32_t push_off = 0;
        for (int j = (int)h.offset - 2; j >= 0 && j > (int)h.offset - 500; j -= 2) {
            if ((j + 1) < (int)decoded_.size() && decoded_[j + 1] == 0xB5) {
                push_off = (uint32_t)j;
                break;
            }
        }
        if (push_off == 0) continue;

        // Scan for all LDR Rd, [PC, #imm8*4] within the function
        uint32_t scan_end = std::min(h.offset + 200, code_boundary_);
        std::vector<FuncCandidate::LdrEntry> ldr_pcs;

        for (uint32_t j = push_off; j + 2 <= scan_end; j += 2) {
            uint8_t hi = decoded_[j + 1], lo = decoded_[j];
            if ((hi & 0xF8) != 0x48) continue; // LDR Rd, [PC, #imm8*4]
            uint8_t rd = hi & 0x07;
            uint8_t imm8 = lo;
            uint32_t pc_val = ((j + FLASH_BASE + 4) & ~3u);
            uint32_t pool_addr = pc_val + imm8 * 4;
            uint32_t pool_poff = pool_addr - FLASH_BASE;
            if (pool_poff + 4 > (uint32_t)decoded_.size()) continue;
            uint32_t pool_val = LE32(&decoded_[pool_poff]);
            ldr_pcs.push_back({j, rd, pool_val});
        }

        // Score: count LDRs targeting table register + prefer callee-saved (R4+)
        int flash_count = 0;
        for (auto& l : ldr_pcs) {
            if (l.rd == h.rn) {
                uint32_t pv = l.pool_val;
                if (pv >= FLASH_BASE && pv < FLASH_BASE + decoded_.size())
                    flash_count++;
            }
        }
        int score = flash_count + (h.rn >= 4 ? 1000 : 0);

        if (score > best.score) {
            best.push_off = push_off;
            best.table_reg = h.rn;
            best.score = score;
            best.ldr_pcs = std::move(ldr_pcs);
        }
    }

    if (best.score <= 0) return;

    // ── Step 3: Extract table addresses and pair with counts ──
    struct TableInfo {
        uint32_t addr;    // flash address of table
        uint16_t count;   // number of register pairs
        char     id;
    };

    // Collect table addresses from LDR instructions targeting table_reg
    std::vector<std::pair<uint32_t, uint32_t>> raw_tables; // (ldr_off, table_addr)
    std::set<uint32_t> seen_addrs;
    for (auto& l : best.ldr_pcs) {
        if (l.rd != best.table_reg) continue;
        uint32_t pv = l.pool_val;
        if (pv < FLASH_BASE || pv >= FLASH_BASE + decoded_.size()) continue;
        if (seen_addrs.count(pv)) continue;
        seen_addrs.insert(pv);
        raw_tables.push_back({l.off, pv});
    }

    // Collect all MOVS Rd, #imm8 in the function as count candidates
    struct MovsCandidate { uint32_t off; uint8_t rd; uint8_t imm; };
    std::vector<MovsCandidate> movs_list;
    uint32_t func_end = std::min(best.push_off + 600, code_boundary_);
    for (uint32_t j = best.push_off; j + 2 <= func_end; j += 2) {
        uint8_t hi = decoded_[j + 1], lo = decoded_[j];
        if ((hi & 0xF8) == 0x20) { // MOVS Rd, #imm8
            uint8_t rd = hi & 0x07;
            if (rd != 0 && rd != 1 && lo >= 10 && lo <= 150) {
                movs_list.push_back({j, rd, lo});
            }
        }
    }

    // Pair each table address with the closest preceding MOVS count
    std::vector<TableInfo> tables;
    char next_id = 'A';
    for (auto& [ldr_off, taddr] : raw_tables) {
        uint16_t count = 65; // default
        uint32_t best_dist = UINT32_MAX;
        for (auto& mc : movs_list) {
            if (mc.off < ldr_off) {
                uint32_t dist = ldr_off - mc.off;
                if (dist < best_dist) {
                    best_dist = dist;
                    count = mc.imm;
                }
            }
        }
        TableInfo ti;
        ti.addr = taddr;
        ti.count = count;
        ti.id = next_id++;
        tables.push_back(ti);
    }

    // Check for hidden tables in gaps between known tables
    if (tables.size() >= 2) {
        std::vector<TableInfo> extra;
        for (size_t t = 0; t + 1 < tables.size(); ++t) {
            uint32_t end_a = tables[t].addr + tables[t].count * 4;
            uint32_t start_b = tables[t + 1].addr;
            if (start_b > end_a + 8) {
                // Gap exists — check if it looks like a valid table
                uint32_t gap_poff = end_a - FLASH_BASE;
                if (gap_poff + 4 <= (uint32_t)decoded_.size()) {
                    // Check first byte is a valid register (< 0x80)
                    if (decoded_[gap_poff] < 0x80) {
                        uint32_t gap_size = start_b - end_a;
                        uint16_t gap_count = (uint16_t)(gap_size / 4);
                        if (gap_count >= 5 && gap_count <= 150) {
                            TableInfo gt;
                            gt.addr = end_a;
                            gt.count = gap_count;
                            gt.id = next_id++;
                            extra.push_back(gt);
                        }
                    }
                }
            }
        }
        for (auto& e : extra) tables.push_back(e);
    }

    // ── Step 4: Parse each table into panel init entries ──
    for (auto& tc : tables) {
        uint32_t poff = tc.addr - FLASH_BASE;
        for (uint16_t grp = 0; grp < tc.count; ++grp) {
            uint32_t goff = poff + grp * 4;
            if (goff + 4 > (uint32_t)decoded_.size()) break;
            for (uint8_t pair = 0; pair < 2; ++pair) {
                uint32_t boff = goff + pair * 2;
                if (boff + 2 > (uint32_t)decoded_.size()) break;
                PanelInitEntry e;
                e.payload_offset = boff + 1; // value byte
                e.file_offset = PayloadToFile(boff + 1);
                e.reg = decoded_[boff];
                e.value = decoded_[boff + 1];
                e.original = e.value;
                e.table_id = tc.id;
                e.group_idx = grp;
                e.pair_pos = pair;
                panel_inits_.push_back(e);
            }
        }
    }
}