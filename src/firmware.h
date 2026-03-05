#pragma once
// ═══════════════════════════════════════════════════════════════════
//  firmware.h — TW8836 B-board / 040B-board firmware parser
// ═══════════════════════════════════════════════════════════════════
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <algorithm>
#include <functional>

// ── Register metadata ─────────────────────────────────────────────
struct RegInfo { const char* name; const char* desc; };
const std::unordered_map<uint16_t, RegInfo>& GetRegisterInfo();

// ── Data structures ───────────────────────────────────────────────
struct DelayCall {
    uint32_t file_offset = 0;   // file/payload offset of delay pattern
    float    value_ms    = 0;
    uint32_t r7_off      = 0;
    uint32_t r6_off      = 0;
    std::string bank;
    std::string desc;
    float    original_ms = 0;
};

struct InitRegEntry {
    uint32_t file_offset = 0; // offset of VALUE byte in file/payload
    uint16_t reg         = 0; // (page<<8)|index
    uint8_t  page        = 0;
    uint8_t  index       = 0;
    uint8_t  value       = 0;
    uint8_t  original    = 0;
};

// ── Memory banks ──────────────────────────────────────────────────
struct MemBank { uint32_t start, end; const char* name; const char* desc; };
inline const std::vector<MemBank>& GetMemBanks() {
    static const std::vector<MemBank> b = {
        {0x00000,0x0007F,"vectors","Interrupt Vectors"},
        {0x00080,0x03BFF,"strings","String Data"},
        {0x03C00,0x07FFF,"common", "Common Code"},
        {0x08000,0x0FFFF,"bank1",  "Bank 1"},
        {0x10000,0x17FFF,"bank2",  "Bank 2"},
        {0x18000,0x1FFFF,"bank3",  "Bank 3"},
        {0x20000,0x27FFF,"bank4",  "Bank 4"},
        {0x28000,0x2FFFF,"bank5",  "Bank 5"},
        {0x30000,0x31FFF,"bank6",  "Bank 6"},
    };
    return b;
}

inline const char* BankForOffset(uint32_t off) {
    for (auto& b : GetMemBanks()) if (off >= b.start && off <= b.end) return b.name;
    return "?";
}

// ── Low-latency preset ───────────────────────────────────────────
struct LLReg { uint16_t reg; uint8_t low_lat; const char* desc; };
inline const std::vector<LLReg>& GetLowLatencyRegs() {
    static const std::vector<LLReg> v = {
        {0x112,0x00,"DEC_SHARPNESS off"},{0x117,0x00,"DEC_V_PEAKING off"},
        {0x12C,0x00,"DEC_HFILTER bypass"},
        {0x120,0x00,"CTI0"},{0x121,0x00,"CTI1"},{0x122,0x00,"CTI2"},
        {0x123,0x00,"CTI3"},{0x124,0x00,"CTI4"},{0x125,0x00,"CTI5"},
        {0x126,0x00,"CTI6"},{0x127,0x00,"CTI7"},{0x128,0x00,"CTI8"},
        {0x20B,0x02,"SC_LINEBUF_DLY 16->2 (~0.9ms saved)"},
        {0x284,0x80,"IA_CONTRAST_Y neutral"},{0x28A,0x80,"IA_BRIGHT_Y neutral"},
        {0x28B,0x00,"IA_SHARPNESS off"},{0x2E4,0x00,"DITHER off"},
    };
    return v;
}

// ── ImgQ register groups ─────────────────────────────────────────
struct ImgQGroup { std::string title; std::string hint; std::vector<uint16_t> regs; };
inline const std::vector<ImgQGroup>& GetImgQGroups() {
    static const std::vector<ImgQGroup> g = {
        {"Decoder Processing",
         "Analog video decoder. REG10C comb filter: keep 2D (0xCC); 3D (0xDC) adds ~16ms!",
         {0x10C,0x110,0x111,0x112,0x113,0x114,0x115,0x117,0x12C}},
        {"CTI — Color Transient Improvement",
         "Chroma edge coefficients. Zero all to disable (~1 line saved).",
         {0x120,0x121,0x122,0x123,0x124,0x125,0x126,0x127,0x128,0x12A,0x12B}},
        {"Scaler",
         "REG20B line-buffer is the biggest processing latency (~1ms at 16 lines).",
         {0x20B,0x210,0x215,0x21C}},
        {"Image Adjust — Contrast / Brightness",
         "Output-stage colour tuning. 0x80 = neutral / bypass.",
         {0x280,0x281,0x282,0x283,0x284,0x285,0x286,0x287,0x288,0x289,0x28A,0x28B}},
        {"Gamma / Dither",
         "Gamma correction and spatial dithering.",
         {0x2E0,0x2E4}},
    };
    return g;
}

// ── Known delay descriptions ─────────────────────────────────────
const std::unordered_map<uint32_t, std::string>& GetKnownDelays();

// ═══════════════════════════════════════════════════════════════════
//  FirmwareBBoard — raw TW8836 (X4Pro) parser
// ═══════════════════════════════════════════════════════════════════
static constexpr uint32_t MCU_SIZE = 0x32000; // 204800

class FirmwareBBoard {
public:
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    void SetDelay(DelayCall& d, uint16_t ms);
    void SetInitReg(InitRegEntry& e, uint8_t val);

    // accessors
    const std::vector<uint8_t>& Data() const { return data_; }
    std::vector<DelayCall>&     Delays()    { return delays_; }
    std::vector<InitRegEntry>&  InitRegs()  { return init_regs_; }
    uint32_t InitTableOffset() const { return init_table_off_; }
    const std::string& Filename() const { return filename_; }
    bool IsLoaded() const { return !data_.empty(); }
    size_t SubTableCount() const { return sub_table_count_; }

private:
    void FindDelays();
    void FindInitRegs();
    void FindSubTables();

    std::vector<uint8_t> data_;
    std::vector<uint8_t> flash_data_; // full SPI flash if applicable
    bool is_flash_ = false;
    std::vector<DelayCall>    delays_;
    std::vector<InitRegEntry> init_regs_;
    uint32_t init_table_off_ = 0;
    size_t   sub_table_count_ = 0;
    std::string filename_;
};

// ═══════════════════════════════════════════════════════════════════
//  Firmware040B — XOR-encoded TW8836 (040 Pro) parser
// ═══════════════════════════════════════════════════════════════════
class Firmware040B {
public:
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    void SetDelay(DelayCall& d, uint16_t ms);
    void SetInitReg(InitRegEntry& e, uint8_t val);

    const std::vector<uint8_t>& Data()    const { return decoded_; }
    const std::vector<uint8_t>& Decoded() const { return decoded_; }
    const std::vector<uint8_t>& Raw()     const { return raw_; }
    std::vector<DelayCall>&     Delays()    { return delays_; }
    std::vector<InitRegEntry>&  InitRegs()  { return init_regs_; }
    uint32_t InitTableOffset() const { return init_table_off_; }
    const std::string& Filename() const { return filename_; }
    bool IsLoaded() const { return !decoded_.empty(); }
    size_t SubTableCount() const { return sub_table_count_; }

private:
    void Decode();
    void WriteDecoded(uint32_t poff, uint8_t val);
    void FindDelays();
    void FindInitRegs();
    void FindSubTables();

    std::vector<uint8_t> raw_;
    std::vector<uint8_t> decoded_;
    std::vector<DelayCall>    delays_;
    std::vector<InitRegEntry> init_regs_;
    uint32_t init_table_off_ = 0;
    size_t   sub_table_count_ = 0;
    std::string filename_;

    static constexpr uint32_t PAYLOAD_START = 0x210;
    static constexpr uint32_t BLOCK_SIZE    = 512;
    static constexpr uint8_t  XOR_BASE      = 0x55;
};
