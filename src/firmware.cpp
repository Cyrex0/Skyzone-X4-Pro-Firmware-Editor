// ═══════════════════════════════════════════════════════════════════
//  firmware.cpp — B board (TW8836) + 040B firmware parsers
// ═══════════════════════════════════════════════════════════════════
#include "firmware.h"
#include <fstream>
#include <cstring>
#include <set>
#include <algorithm>

// ── Register info database (full TW8836 register map) ──────────────
const std::unordered_map<uint16_t, RegInfo>& GetRegisterInfo() {
    static const std::unordered_map<uint16_t, RegInfo> m = {
        // ── Page 0 — System ──────────────────────────────────────
        {0x002,{"INT_STATUS","Interrupt Status clear, write 0xFF to clear all"}},
        {0x003,{"INT_MASK","Interrupt Mask, 0xFF=all masked"}},
        {0x006,{"SYSTEM_CTRL","System Control, stock=0x06"}},
        {0x007,{"OUTPUT_CTRL","Output Ctrl (TCONSEL[2:0], EN656OUT[3])"}},
        {0x008,{"OUTPUT_FORMAT","Output Format, stock=0x86"}},
        {0x00F,{"PANEL_CTRL","Panel Control"}},
        {0x01F,{"OUTPUT_CTRL2","Output Control II"}},
        // ── Page 0 — Input ───────────────────────────────────────
        {0x040,{"INPUT_CTRL_I","Input Control I (mux select)"}},
        {0x041,{"INPUT_CTRL_II","Input Control II (ImplicitDE[4], FieldPol[3], YUV[0])"}},
        {0x042,{"INPUT_SEL","Input Selection / Mux, stock=0x02"}},
        {0x043,{"INPUT_MISC","Input Misc, stock=0x20"}},
        {0x044,{"INPUT_CROP_HI","Input Crop High bits"}},
        {0x045,{"INPUT_CROP_HSTART","Input Crop H Start"}},
        {0x047,{"BT656_CTRL","BT656 Control, stock=0x02"}},
        {0x048,{"BT656_CTRL2","BT656 Control 2"}},
        {0x04B,{"BT656_MISC","BT656 Misc"}},
        // ── Page 0 — DTV ─────────────────────────────────────────
        {0x050,{"DTV_CTRL0","DTV Control 0"}},
        {0x051,{"DTV_CTRL1","DTV Control 1"}},
        {0x052,{"DTV_CTRL2","DTV Control 2"}},
        {0x053,{"DTV_CTRL3","DTV Control 3"}},
        {0x054,{"DTV_CTRL4","DTV Control 4"}},
        {0x055,{"DTV_CTRL5","DTV Control 5"}},
        {0x056,{"DTV_CTRL6","DTV Control 6"}},
        {0x057,{"DTV_CTRL7","DTV Control 7"}},
        {0x058,{"DTV_CTRL8","DTV Control 8"}},
        {0x059,{"DTV_CTRL9","DTV Control 9"}},
        {0x05A,{"DTV_CTRL10","DTV Control 10"}},
        {0x05B,{"DTV_CTRL11","DTV Control 11"}},
        {0x05C,{"DTV_CTRL12","DTV Control 12"}},
        {0x05D,{"DTV_CTRL13","DTV Control 13"}},
        {0x05E,{"DTV_CTRL14","DTV Control 14"}},
        {0x05F,{"DTV_CTRL15","DTV Control 15"}},
        // ── Page 0 — GPIO ────────────────────────────────────────
        {0x080,{"GPIO_00","GPIO Control 0x80, init=0x00"}},
        {0x081,{"GPIO_01","GPIO Control 0x81"}},
        {0x082,{"GPIO_02","GPIO Control 0x82"}},
        {0x083,{"GPIO_03","GPIO Control 0x83"}},
        {0x084,{"GPIO_04","GPIO Control 0x84"}},
        {0x085,{"GPIO_05","GPIO Control 0x85"}},
        {0x086,{"GPIO_06","GPIO Control 0x86"}},
        {0x087,{"GPIO_07","GPIO Control 0x87"}},
        {0x088,{"GPIO_08","GPIO Control 0x88"}},
        {0x089,{"GPIO_09","GPIO Control 0x89"}},
        {0x08A,{"GPIO_0A","GPIO Control 0x8A"}},
        {0x08B,{"GPIO_0B","GPIO Control 0x8B"}},
        {0x08C,{"GPIO_0C","GPIO Control 0x8C"}},
        {0x08D,{"GPIO_0D","GPIO Control 0x8D"}},
        {0x08E,{"GPIO_0E","GPIO Control 0x8E"}},
        {0x08F,{"GPIO_0F","GPIO Control 0x8F"}},
        {0x090,{"GPIO_10","GPIO Control 0x90"}},
        {0x091,{"GPIO_11","GPIO Control 0x91"}},
        {0x092,{"GPIO_12","GPIO Control 0x92"}},
        {0x093,{"GPIO_13","GPIO Control 0x93"}},
        {0x094,{"GPIO_14","GPIO Control 0x94"}},
        {0x095,{"GPIO_15","GPIO Control 0x95"}},
        {0x096,{"GPIO_16","GPIO Control 0x96"}},
        {0x097,{"GPIO_17","GPIO Control 0x97"}},
        {0x098,{"GPIO_18","GPIO Control 0x98"}},
        {0x099,{"GPIO_19","GPIO Control 0x99"}},
        {0x09A,{"GPIO_1A","GPIO Control 0x9A"}},
        {0x09B,{"GPIO_1B","GPIO Control 0x9B"}},
        {0x09C,{"GPIO_1C","GPIO Control 0x9C"}},
        {0x09D,{"GPIO_1D","GPIO Control 0x9D"}},
        {0x09E,{"GPIO_1E","GPIO Control 0x9E"}},
        {0x09F,{"GPIO_1F","GPIO Control 0x9F"}},
        // ── Page 0 — MBIST ───────────────────────────────────────
        {0x0A0,{"MBIST_CTRL","Memory BIST Control"}},
        // ── Page 0 — Touch / TSC ─────────────────────────────────
        {0x0B0,{"TSC_ADC_CTRL","Touch ADC Control, [7]=enable, stock=0x87"}},
        {0x0B1,{"TSC_CONFIG","Touch Config, stock=0xC0"}},
        {0x0B4,{"TSC_ADC_CFG","Touch ADC Config, stock=0x0A"}},
        // ── Page 0 — LOPOR / Power ───────────────────────────────
        {0x0D4,{"LOPOR_CTRL","Low Power (LOPOR) Control"}},
        {0x0D6,{"PWM_CTRL","PWM / LEDC / Backlight Control"}},
        // ── Page 0 — SSPLL ───────────────────────────────────────
        {0x0F6,{"SSPLL_0","SSPLL Register 0"}},
        {0x0F7,{"SSPLL_1","SSPLL Register 1, stock=0x16"}},
        {0x0F8,{"FPLL0","SSPLL FPLL[19:16], PCLK divider"}},
        {0x0F9,{"FPLL1","SSPLL FPLL[15:8]"}},
        {0x0FA,{"FPLL2","SSPLL FPLL[7:0]"}},
        {0x0FB,{"SSPLL_5","SSPLL Register 5, stock=0x40"}},
        {0x0FC,{"SSPLL_6","SSPLL Register 6, stock=0x23"}},
        {0x0FD,{"SSPLL_ANALOG","SSPLL Analog: POST[7:6] VCO[5:4] Pump[2:0]"}},
        // ── Page 1 — Video Decoder ───────────────────────────────
        {0x101,{"DEC_CSTATUS","Chip Status (read-only)"}},
        {0x102,{"DEC_INFORM","Input Format / Mux Select"}},
        {0x104,{"DEC_HSYNC_DLY","HSYNC Delay Control"}},
        {0x105,{"DEC_AFE_MODE","AFE Mode / Anti-Aliasing Filter on/off"}},
        {0x106,{"DEC_ACNTL","Analog Control (ADC power), stock=0x03"}},
        {0x107,{"DEC_CROP_HI","Cropping High bits (V/H MSBs), stock=0x02"}},
        {0x108,{"DEC_VDELAY","Vertical Delay Low, stock=0x12 (18 lines)"}},
        {0x109,{"DEC_VACTIVE","Vertical Active Low, stock=0xF0 (240 lines)"}},
        {0x10A,{"DEC_HDELAY","Horizontal Delay Low, stock=0x0B (11)"}},
        {0x10B,{"DEC_HACTIVE","Horizontal Active Low, stock=0xD0 (720px)"}},
        {0x10C,{"DEC_CNTRL1","Decoder Ctrl 1 (comb: 0xCC=2D, 0xDC=3D)"}},
        {0x10D,{"DEC_CNTRL2","Decoder Control 2"}},
        {0x110,{"DEC_BRIGHT","Brightness, stock=0x00, 0x80=neutral"}},
        {0x111,{"DEC_CONTRAST","Contrast, stock=0x5C (92), range 0-255"}},
        {0x112,{"DEC_SHARPNESS","Sharpness, stock=0x11 (17), range 0-255"}},
        {0x113,{"DEC_SAT_U","Chroma U Saturation, stock=0x80 (128)"}},
        {0x114,{"DEC_SAT_V","Chroma V Saturation, stock=0x80 (128)"}},
        {0x115,{"DEC_HUE","Hue Control, stock=0x00"}},
        {0x117,{"DEC_V_PEAKING","Vertical Peaking, stock=0x80"}},
        {0x118,{"DEC_MISC_TIMING","Decoder Misc Timing, stock=0x44"}},
        {0x11A,{"DEC_CLAMP_ADJ","Clamp Adjust / Level Control"}},
        {0x11C,{"DEC_SDT","Standard Selection (auto-detect), stock=0x07"}},
        {0x11D,{"DEC_SDTR","Standard Recognition (read-only)"}},
        {0x11E,{"DEC_CTRL3","Decoder Control 3"}},
        // ── Page 1 — CTI ─────────────────────────────────────────
        {0x120,{"CTI_COEFF_0","CTI Coefficient 0, stock=0x50"}},
        {0x121,{"CTI_COEFF_1","CTI Coefficient 1, stock=0x22"}},
        {0x122,{"CTI_COEFF_2","CTI Coefficient 2, stock=0xF0"}},
        {0x123,{"CTI_COEFF_3","CTI Coefficient 3, stock=0xD8"}},
        {0x124,{"CTI_COEFF_4","CTI Coefficient 4, stock=0xBC"}},
        {0x125,{"CTI_COEFF_5","CTI Coefficient 5, stock=0xB8"}},
        {0x126,{"CTI_COEFF_6","CTI Coefficient 6, stock=0x44"}},
        {0x127,{"CTI_COEFF_7","CTI Coefficient 7, stock=0x38"}},
        {0x128,{"CTI_COEFF_8","CTI Coefficient 8, stock=0x00"}},
        {0x129,{"DEC_V_CTRL2","Vertical Control II, stock=0x00"}},
        {0x12A,{"CTI_COEFF_A","CTI Coefficient A, stock=0x78"}},
        {0x12B,{"CTI_COEFF_B","CTI Coefficient B, stock=0x44"}},
        {0x12C,{"DEC_HFILTER","Horizontal Filter, stock=0x30"}},
        {0x12D,{"DEC_MISC1","Miscellaneous Control 1, stock=0x14"}},
        {0x12E,{"DEC_MISC2","Miscellaneous Control 2, stock=0xA5"}},
        {0x12F,{"DEC_MISC3","Miscellaneous Control 3, stock=0xE0"}},
        // ── Page 1 — Decoder Freerun / VBI / BT656 ──────────────
        {0x133,{"DEC_FREERUN","Freerun [7:6]=mode(0:Auto,2:60Hz,3:50Hz)"}},
        {0x134,{"DEC_VBI_CNTL2","VBI Control 2 / WSSEN, stock=0x1A"}},
        {0x135,{"DEC_CC_ODDLINE","CC Odd Line, stock=0x00"}},
        {0x136,{"DEC_CC_EVENLINE","CC Even Line, stock=0x03"}},
        {0x137,{"BT656_DI_HDELAY","BT656 DeInterlace H Delay, stock=0x28"}},
        {0x138,{"BT656_DI_HSTART","BT656 DeInterlace H Start, stock=0xAF"}},
        // ── Page 1 — LLPLL ───────────────────────────────────────
        {0x1C0,{"LLPLL_INPUT_CFG","LLPLL Input Config, [0]=clk(0=PLL,1=27MHz)"}},
        {0x1C2,{"LLPLL_VCO_CP","VCO Range[5:4] & Charge Pump[2:0], stock=0xD2"}},
        {0x1C3,{"LLPLL_DIV_H","LLPLL Divider High [3:0]"}},
        {0x1C4,{"LLPLL_DIV_L","LLPLL Divider Low [7:0], total=(H:L)=Htotal-1"}},
        {0x1C5,{"LLPLL_PHASE","PLL Phase [4:0]"}},
        {0x1C6,{"LLPLL_FILTER_BW","PLL Loop Filter Bandwidth, stock=0x20"}},
        {0x1C7,{"LLPLL_VCO_NOM_H","VCO Nominal Freq High, stock=0x04"}},
        {0x1C8,{"LLPLL_VCO_NOM_L","VCO Nominal Freq Low, stock=0x00"}},
        {0x1C9,{"LLPLL_PRE_COAST","Pre-Coast, stock=0x06"}},
        {0x1CA,{"LLPLL_POST_COAST","Post-Coast, stock=0x06"}},
        {0x1CB,{"VADC_SOG_PLL","SOG[7]/PLL[6] Power, SOG Threshold[4:0]"}},
        {0x1CC,{"VADC_SYNC_SEL","Sync Output/Polarity Select"}},
        {0x1CD,{"LLPLL_CTRL","LLPLL Control/Status, [0]=init trigger"}},
        // ── Page 1 — VADC ────────────────────────────────────────
        {0x1D0,{"VADC_GAIN_MSB","ADC Gain MSBs [2:0]=Y/C/V ch MSBs"}},
        {0x1D1,{"VADC_GAIN_Y","Y/G Channel Gain, stock=0xF0"}},
        {0x1D2,{"VADC_GAIN_C","C/B Channel Gain, stock=0xF0"}},
        {0x1D3,{"VADC_GAIN_V","V/R Channel Gain, stock=0xF0"}},
        {0x1D4,{"VADC_CLAMP_MODE","Clamp Mode Control"}},
        {0x1D5,{"VADC_CLAMP_START","Clamp Start Position"}},
        {0x1D6,{"VADC_CLAMP_STOP","Clamp Stop Position"}},
        {0x1D7,{"VADC_CLAMP_LOC","Master Clamp Location"}},
        {0x1D8,{"VADC_DEBUG","VADC Debug Register"}},
        {0x1D9,{"VADC_CLAMP_GY","Clamp G/Y Level"}},
        {0x1DA,{"VADC_CLAMP_BU","Clamp B/U Level"}},
        {0x1DB,{"VADC_CLAMP_RV","Clamp R/V Level"}},
        {0x1DC,{"VADC_HS_WIDTH","HS Width Control"}},
        // ── Page 1 — AFE ─────────────────────────────────────────
        {0x1E0,{"AFE_TEST","AFE Test Mode"}},
        {0x1E1,{"AFE_GPLL_PD","GPLL Power Down [5]=PD, stock=0x05"}},
        {0x1E2,{"AFE_BIAS_VREF","AFE Bias & VREF, stock=0x59"}},
        {0x1E3,{"AFE_PATH_0","AFE Bias cfg (DEC=0x07, aRGB=0x37)"}},
        {0x1E4,{"AFE_PATH_1","AFE Bias cfg (DEC=0x33, aRGB=0x55)"}},
        {0x1E5,{"AFE_PATH_2","AFE Bias cfg (DEC=0x33, aRGB=0x55)"}},
        {0x1E6,{"AFE_PATH_3","AFE PGA Speed (DEC=0x00, aRGB=0x20)"}},
        {0x1E7,{"AFE_AAF","Anti-Aliasing Filter, stock=0x2A"}},
        {0x1E8,{"AFE_PATH_5","AFE Path Select (DEC=0x0F, aRGB=0x00)"}},
        {0x1E9,{"AFE_LLCLK","LLCLK Polarity & CLKO Select"}},
        {0x1EA,{"AFE_MISC","AFE Misc, stock=0x03"}},
        {0x1F6,{"VADC_MISC2","VADC Miscellaneous 2"}},
        // ── Page 2 — Scaler ──────────────────────────────────────
        {0x201,{"SC_CTRL1","Scaler Control 1"}},
        {0x202,{"SC_XUP_H","H Up-Scale Ratio High"}},
        {0x203,{"SC_XUP_L","H Up-Scale Ratio Low"}},
        {0x204,{"SC_XDOWN_H","H Down-Scale Ratio High"}},
        {0x205,{"SC_VSCALE_L","V Scale Ratio Low"}},
        {0x206,{"SC_VSCALE_H","V Scale Ratio High"}},
        {0x207,{"SC_HSCALE_H","H Scale Ratio High"}},
        {0x208,{"SC_HSCALE_L","H Scale Ratio Low"}},
        {0x209,{"SC_XDOWN_L","H Down-Scale Ratio Low"}},
        {0x20A,{"SC_PANORAMA","Panorama / Water-Glass Effect"}},
        {0x20B,{"SC_LINEBUF_DLY","Line Buffer Delay, init=0x10, CVBS=0x62"}},
        {0x20C,{"SC_LINEBUF_SZ_H","Line Buffer Size High"}},
        {0x20D,{"SC_PCLKO_DIV","PCLKO Divider[1:0] + HPol[2] VPol[3]"}},
        {0x20E,{"SC_LINEBUF_SZ_L","Line Buffer Size Low, stock=0x20"}},
        {0x20F,{"SC_CTRL2","Scaler Control 2"}},
        {0x210,{"SC_HDE_START","H Display Enable Start position"}},
        {0x211,{"SC_OUT_W_H","Output Width High + misc bits"}},
        {0x212,{"SC_OUT_W_L","Output Width Low (800+1=0x321)"}},
        {0x213,{"SC_HSYNC_POS","HSync Position"}},
        {0x214,{"SC_HSYNC_WIDTH","HSync Pulse Width"}},
        {0x215,{"SC_VDE_START","V Display Enable Start position"}},
        {0x216,{"SC_OUT_H_L","Output Height Low (480=0x1E0)"}},
        {0x217,{"SC_OUT_H_H","Output Height High"}},
        {0x218,{"SC_VSYNC_POS","VSync Position"}},
        {0x219,{"SC_FREERUN_VT_L","Freerun V Total Low"}},
        {0x21A,{"SC_VSYNC_WIDTH","VSync Pulse Width"}},
        {0x21B,{"SC_VDE_MASK","VDE Mask (top/bottom blanking)"}},
        {0x21C,{"SC_FREERUN_CTRL","Freerun Control (auto/manual mute)"}},
        {0x21D,{"SC_FREERUN_HT_L","Freerun H Total Low"}},
        {0x21E,{"SC_FIXED_VLINE","Fixed VLine enable, stock=0x02"}},
        {0x220,{"SC_PANORAMA_H","Panorama Control High"}},
        {0x221,{"SC_HSYNC_POS_H","HSync Position High bits"}},
        // ── Page 2 — TCON ────────────────────────────────────────
        {0x240,{"TCON_0","TCON Register 0, stock=0x10"}},
        {0x241,{"TCON_1","TCON Register 1"}},
        {0x242,{"TCON_2","TCON Register 2, stock=0x05"}},
        {0x243,{"TCON_3","TCON Register 3, stock=0x01"}},
        {0x244,{"TCON_4","TCON Register 4, stock=0x64"}},
        {0x245,{"TCON_5","TCON Register 5, stock=0xF4"}},
        {0x246,{"TCON_6","TCON Register 6"}},
        {0x247,{"TCON_7","TCON Register 7, stock=0x0A"}},
        {0x248,{"TCON_8","TCON Register 8, stock=0x36"}},
        {0x249,{"TCON_9","TCON Register 9, stock=0x10"}},
        {0x24A,{"TCON_A","TCON Register A"}},
        {0x24B,{"TCON_B","TCON Register B"}},
        {0x24C,{"TCON_C","TCON Register C"}},
        {0x24D,{"TCON_D","TCON Register D, stock=0x44"}},
        {0x24E,{"TCON_E","TCON Register E, stock=0x04"}},
        // ── Page 2 — Image Adjustment ────────────────────────────
        {0x280,{"IA_HUE","RGB Hue [5:0], stock=0x20"}},
        {0x281,{"IA_CONTRAST_R","Red Contrast, stock=0x80"}},
        {0x282,{"IA_CONTRAST_G","Green Contrast, stock=0x80"}},
        {0x283,{"IA_CONTRAST_B","Blue Contrast, stock=0x80"}},
        {0x284,{"IA_CONTRAST_Y","Y Luminance Contrast, stock=0x80"}},
        {0x285,{"IA_CONTRAST_CB","Cb Chrominance Contrast, stock=0x80"}},
        {0x286,{"IA_CONTRAST_CR","Cr Chrominance Contrast, stock=0x80"}},
        {0x287,{"IA_BRIGHT_R","Red Brightness, stock=0x80"}},
        {0x288,{"IA_BRIGHT_G","Green Brightness, stock=0x80"}},
        {0x289,{"IA_BRIGHT_B","Blue Brightness, stock=0x80"}},
        {0x28A,{"IA_BRIGHT_Y","Y Luminance Brightness, stock=0x80"}},
        {0x28B,{"IA_SHARPNESS","Panel Sharpness [3:0], stock=0x40"}},
        {0x28C,{"IA_CTRL","Image Adjust Control"}},
        // ── Page 2 — Gamma / Dither ──────────────────────────────
        {0x2BE,{"IA_CTRL2","Image Adjust Control 2 (misc)"}},
        {0x2BF,{"TEST_PATTERN","Test Pattern Control, stock=0x00"}},
        {0x2E0,{"GAMMA_CTRL0","Gamma Control 0"}},
        {0x2E1,{"GAMMA_CTRL1","Gamma Control 1"}},
        {0x2E2,{"GAMMA_CTRL2","Gamma Control 2"}},
        {0x2E3,{"GAMMA_CTRL3","Gamma Control 3"}},
        {0x2E4,{"DITHER_CTRL","Dither Control, stock=0x21"}},
        // ── Page 2 — 8-bit Panel Interface ───────────────────────
        {0x2F8,{"PANEL_8BIT_0","8-bit Panel Interface 0"}},
        {0x2F9,{"PANEL_8BIT_1","8-bit Panel Interface 1, stock=0x80"}},
        // ── Page 3 — Font OSD ────────────────────────────────────
        {0x304,{"FOSD_RAM_ACC","OSD RAM Auto Access Enable"}},
        {0x30C,{"FOSD_CTRL","Font OSD On/Off, [6]=enable"}},
        {0x310,{"FOSD_WIN1","Font OSD Window 1 Enable"}},
        {0x320,{"FOSD_WIN2","Font OSD Window 2 Enable"}},
        {0x330,{"FOSD_WIN3","Font OSD Window 3 Enable"}},
        {0x340,{"FOSD_WIN4","Font OSD Window 4 Enable"}},
        {0x350,{"FOSD_WIN5","Font OSD Window 5 Enable"}},
        {0x360,{"FOSD_WIN6","Font OSD Window 6 Enable"}},
        {0x370,{"FOSD_WIN7","Font OSD Window 7 Enable"}},
        {0x380,{"FOSD_WIN8","Font OSD Window 8 Enable"}},
        // ── Page 4 — Sprite OSD ──────────────────────────────────
        {0x400,{"SOSD_CTRL","Sprite OSD Control"}},
        // ── Page 4 — SPI ─────────────────────────────────────────
        {0x4C0,{"SPI_BASE","SPI Base Register"}},
        // ── Page 4 — Clock ───────────────────────────────────────
        {0x4E0,{"CLOCK_CTRL0","Clock Control 0"}},
        {0x4E1,{"CLOCK_CTRL1","Clock Control 1"}},
    };
    return m;
}

const std::unordered_map<uint32_t, std::string>& GetKnownDelays() {
    static const std::unordered_map<uint32_t, std::string> m = {
        {0x0F570,"Decoder init - analog startup settle"},
        {0x106C9,"IT6802 HDMI Rx init - power-up"},
        {0x10917,"IT6802 HDMI Rx - PLL lock"},
        {0x10C8B,"IT6802 stabilization - signal lock"},
        {0x10CAD,"IT6802 stabilization - video detect"},
        {0x10E4E,"IT6802 power-up - longest timeout"},
        {0x11E20,"PLL lock wait - clock settling"},
        {0x11E3C,"PLL lock wait - clock settling"},
        {0x1D25C,"Mode switch settle (bank3)"},
        {0x1D76B,"Intermediate delay (bank3)"},
        {0x1D86C,"System reset recovery"},
        {0x22CD8,"LLPLL lock - settling"},
        {0x22E4F,"Post-PLL - scaler flush"},
        {0x249DA,"Pre-AFE configuration"},
        {0x24A1A,"AFE settling"},
        {0x24A4E,"AFE gain settle"},
        {0x2655F,"Video path stabilization"},
        {0x265B9,"Extended mode change (bank4)"},
        {0x2A173,"Power-up startup init"},
        {0x2A3F0,"AFE/GPIO peripheral setup"},
        {0x2A68C,"AFE/GPIO extended settling"},
        {0x2A9DB,"LLPLL fine adjust"},
        {0x2AA19,"AFE fine calibration"},
        {0x2AD7D,"Pipeline stabilization (bank5)"},
        {0x2C38D,"OSD engine settle"},
        {0x2C75C,"ChangeDecoder() - decoder settle"},
        {0x2CA1D,"Scaler/OSD mode change"},
        {0x2DBE0,"DTV mode stabilization"},
        {0x2FFA5,"Output path settle"},
        {0x2FFB2,"Output path settle"},
        {0x30E3C,"Extended stabilization (bank6)"},
        {0x31300,"Final init settle (bank6)"},
    };
    return m;
}

// Auto-generate delay description from surrounding register context
static std::string AutoDelayDesc(const uint8_t* data, uint32_t data_len,
                                 uint32_t off, uint16_t ms)
{
    // Page names for TW8836 register pages
    static const char* page_names[] = {
        "decoder", "LLPLL/decoder", "scaler/TCON", "AFE/DTV", "OSD/SPI"
    };
    auto& regdb = GetRegisterInfo();
    std::set<std::string> subsystems;

    // Scan window: 40 bytes before, 10 after the delay offset
    uint32_t scan_start = (off > 40) ? off - 40 : 0;
    uint32_t scan_end   = std::min(off + 10, data_len - 2);
    for (uint32_t j = scan_start; j < scan_end; ++j) {
        uint8_t page = data[j];
        if (page > 4) continue;
        uint16_t reg = ((uint16_t)page << 8) | data[j + 1];
        if (regdb.count(reg)) {
            subsystems.insert(page_names[page]);
        }
    }

    // Build description
    std::string parts;
    if (!subsystems.empty()) {
        for (auto& s : subsystems) {
            if (!parts.empty()) parts += "/";
            parts += s;
        }
        parts += " \xe2\x80\x94 "; // em-dash
    }

    if (ms >= 3000)
        parts += "long timeout/init";
    else if (ms >= 1000)
        parts += "stabilization wait";
    else if (ms >= 300)
        parts += "settling delay";
    else
        parts += "short settle";

    // Append bank
    const char* bank = BankForOffset(off);
    if (bank[0] != '?') {
        parts += " (";
        parts += bank;
        parts += ")";
    }
    return parts;
}

// ═══════════════════════════════════════════════════════════════════
//                   FirmwareBBoard (raw TW8836)
// ═══════════════════════════════════════════════════════════════════
static std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf((size_t)sz);
    f.read((char*)buf.data(), sz);
    return buf;
}

bool FirmwareBBoard::Load(const std::string& path) {
    auto buf = ReadFile(path);
    if (buf.empty()) return false;

    auto pos = path.find_last_of("/\\");
    filename_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;

    if (buf.size() > MCU_SIZE * 2) {
        is_flash_ = true;
        flash_data_ = buf;
        data_.assign(buf.begin(), buf.begin() + MCU_SIZE);
    } else {
        data_ = std::move(buf);
    }

    FindDelays();
    FindInitRegs();
    FindSubTables();
    return true;
}

bool FirmwareBBoard::Save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    if (is_flash_) {
        auto out = flash_data_;
        std::copy(data_.begin(), data_.end(), out.begin());
        f.write((char*)out.data(), out.size());
    } else {
        f.write((char*)data_.data(), data_.size());
    }
    return true;
}

void FirmwareBBoard::SetDelay(DelayCall& d, uint16_t ms) {
    d.value_ms = (float)ms;
    data_[d.r7_off] = ms & 0xFF;
    data_[d.r6_off] = (ms >> 8) & 0xFF;
}

void FirmwareBBoard::SetInitReg(InitRegEntry& e, uint8_t val) {
    e.value = val;
    data_[e.file_offset] = val;
}

void FirmwareBBoard::FindDelays() {
    delays_.clear();
    if (data_.size() < 0x3C00 + 8) return;
    auto& kd = GetKnownDelays();

    for (uint32_t i = 0x3C00; i + 7 < (uint32_t)data_.size(); ++i) {
        uint32_t doff = 0, r7 = 0, r6 = 0;
        // Pattern 1: E4 7F xx 7E xx FD FC
        if (data_[i]==0xE4 && data_[i+1]==0x7F && data_[i+3]==0x7E
            && data_[i+5]==0xFD && data_[i+6]==0xFC) {
            doff = i; r7 = i+2; r6 = i+4;
        }
        // Pattern 2: 7F xx 7E xx 7D 00 7C 00
        else if (i+7 < (uint32_t)data_.size()
                 && data_[i]==0x7F && data_[i+2]==0x7E
                 && data_[i+4]==0x7D && data_[i+5]==0x00
                 && data_[i+6]==0x7C && data_[i+7]==0x00) {
            doff = i; r7 = i+1; r6 = i+3;
        }
        else continue;

        uint16_t ms = data_[r7] | ((uint16_t)data_[r6] << 8);
        if (ms < 5 || ms > 10000) continue;

        bool dup = false;
        for (auto& d : delays_) if (d.file_offset == doff) { dup = true; break; }
        if (dup) continue;

        DelayCall d;
        d.file_offset = doff; d.value_ms = (float)ms; d.original_ms = (float)ms;
        d.r7_off = r7; d.r6_off = r6;
        d.bank = BankForOffset(doff);
        auto it = kd.find(doff);
        if (it != kd.end()) d.desc = it->second;
        else d.desc = AutoDelayDesc(data_.data(), (uint32_t)data_.size(), doff, ms);
        delays_.push_back(d);
    }
    std::sort(delays_.begin(), delays_.end(),
              [](auto& a, auto& b){ return a.file_offset < b.file_offset; });
}

void FirmwareBBoard::FindInitRegs() {
    init_regs_.clear();
    const uint8_t sig[] = {0x00, 0x06, 0x06, 0x00, 0x07};
    for (uint32_t i = 0x3C00; i + 5 < (uint32_t)data_.size(); ++i) {
        if (memcmp(&data_[i], sig, 5) == 0) {
            init_table_off_ = i;
            uint32_t pos = i;
            int count = 0;
            while (pos + 3 <= (uint32_t)data_.size() && count < 700
                   && (pos - i) < 2000) {
                uint8_t hi  = data_[pos];
                uint8_t lo  = data_[pos+1];
                uint8_t val = data_[pos+2];
                if (hi == 0x0F && lo == 0xFF && val == 0xFF) break;
                if (hi > 0x04) break;
                uint16_t reg = ((uint16_t)hi << 8) | lo;
                InitRegEntry e;
                e.file_offset = pos + 2;
                e.reg = reg; e.page = hi; e.index = lo;
                e.value = val; e.original = val;
                init_regs_.push_back(e);
                pos += 3; count++;
            }
            break;
        }
    }
}

void FirmwareBBoard::FindSubTables() {
    std::set<uint16_t> seen;
    for (auto& e : init_regs_) seen.insert(e.reg);

    std::vector<uint32_t> terms;
    for (uint32_t i = 0x3C00; i + 3 < (uint32_t)data_.size(); ++i) {
        if (data_[i]==0x0F && data_[i+1]==0xFF && data_[i+2]==0xFF)
            terms.push_back(i);
    }

    uint32_t main_end = init_table_off_;
    for (auto& e : init_regs_)
        main_end = std::max(main_end, e.file_offset);

    sub_table_count_ = 0;
    for (auto t : terms) {
        if (t <= main_end + 3) continue;
        uint32_t pos = t - 3;
        std::vector<InitRegEntry> sub;
        while (pos >= 0x3C00 && (t - pos) < 2000) {
            uint8_t hi = data_[pos], lo = data_[pos+1], val = data_[pos+2];
            if (hi > 0x04) break;
            if (hi == 0x0F && lo == 0xFF && val == 0xFF) break;
            uint16_t reg = ((uint16_t)hi << 8) | lo;
            if (seen.count(reg) == 0) {
                InitRegEntry e;
                e.file_offset = pos + 2; e.reg = reg;
                e.page = hi; e.index = lo;
                e.value = val; e.original = val;
                sub.push_back(e);
                seen.insert(reg);
            }
            if (pos < 3) break;
            pos -= 3;
        }
        std::reverse(sub.begin(), sub.end());
        sub_table_count_ += sub.size();
        init_regs_.insert(init_regs_.end(), sub.begin(), sub.end());
    }
}

// ═══════════════════════════════════════════════════════════════════
//                   Firmware040B (XOR-encoded TW8836)
// ═══════════════════════════════════════════════════════════════════
bool Firmware040B::Load(const std::string& path) {
    raw_ = ReadFile(path);
    if (raw_.size() < PAYLOAD_START + 100) return false;
    if (raw_[0] != 0x04) return false;

    auto pos = path.find_last_of("/\\");
    filename_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;

    Decode();
    FindDelays();
    FindInitRegs();
    FindSubTables();
    return true;
}

bool Firmware040B::Save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((char*)raw_.data(), raw_.size());
    return true;
}

void Firmware040B::Decode() {
    uint32_t payload_len = (uint32_t)raw_.size() - PAYLOAD_START;
    decoded_.resize(payload_len);
    for (uint32_t i = 0; i < payload_len; ++i) {
        uint8_t key = (XOR_BASE + i / BLOCK_SIZE) & 0xFF;
        decoded_[i] = raw_[PAYLOAD_START + i] ^ key;
    }
}

void Firmware040B::WriteDecoded(uint32_t poff, uint8_t val) {
    decoded_[poff] = val;
    uint8_t key = (XOR_BASE + poff / BLOCK_SIZE) & 0xFF;
    raw_[PAYLOAD_START + poff] = val ^ key;
}

void Firmware040B::SetDelay(DelayCall& d, uint16_t ms) {
    d.value_ms = (float)ms;
    WriteDecoded(d.r7_off, ms & 0xFF);
    WriteDecoded(d.r6_off, (ms >> 8) & 0xFF);
}

void Firmware040B::SetInitReg(InitRegEntry& e, uint8_t val) {
    e.value = val;
    WriteDecoded(e.file_offset, val);
}

void Firmware040B::FindDelays() {
    delays_.clear();
    if (decoded_.size() < 0x3C00 + 8) return;
    auto& kd = GetKnownDelays();

    for (uint32_t i = 0x3C00; i + 7 < (uint32_t)decoded_.size(); ++i) {
        uint32_t doff = 0, r7 = 0, r6 = 0;
        if (decoded_[i]==0xE4 && decoded_[i+1]==0x7F && decoded_[i+3]==0x7E
            && decoded_[i+5]==0xFD && decoded_[i+6]==0xFC) {
            doff = i; r7 = i+2; r6 = i+4;
        }
        else if (i+7 < (uint32_t)decoded_.size()
                 && decoded_[i]==0x7F && decoded_[i+2]==0x7E
                 && decoded_[i+4]==0x7D && decoded_[i+5]==0x00
                 && decoded_[i+6]==0x7C && decoded_[i+7]==0x00) {
            doff = i; r7 = i+1; r6 = i+3;
        }
        else continue;

        uint16_t ms = decoded_[r7] | ((uint16_t)decoded_[r6] << 8);
        if (ms < 5 || ms > 30000) continue;
        bool dup = false;
        for (auto& d : delays_) if (d.file_offset == doff) { dup = true; break; }
        if (dup) continue;

        DelayCall d;
        d.file_offset = doff; d.value_ms = (float)ms; d.original_ms = (float)ms;
        d.r7_off = r7; d.r6_off = r6;
        d.bank = BankForOffset(doff);
        auto it = kd.find(doff);
        if (it != kd.end()) d.desc = it->second;
        else d.desc = AutoDelayDesc(decoded_.data(), (uint32_t)decoded_.size(), doff, ms);
        delays_.push_back(d);
    }
    std::sort(delays_.begin(), delays_.end(),
              [](auto& a, auto& b){ return a.file_offset < b.file_offset; });
}

void Firmware040B::FindInitRegs() {
    init_regs_.clear();
    const uint8_t sig[] = {0x00, 0x06, 0x06, 0x00, 0x07};
    for (uint32_t i = 0x3C00; i + 5 < (uint32_t)decoded_.size(); ++i) {
        if (memcmp(&decoded_[i], sig, 5) == 0) {
            init_table_off_ = i;
            uint32_t pos = i;
            int count = 0;
            while (pos + 3 <= (uint32_t)decoded_.size() && count < 750
                   && (pos - i) < 2100) {
                uint8_t hi = decoded_[pos], lo = decoded_[pos+1], val = decoded_[pos+2];
                if (hi == 0x0F && lo == 0xFF && val == 0xFF) break;
                if (hi > 0x04) break;
                uint16_t reg = ((uint16_t)hi << 8) | lo;
                InitRegEntry e;
                e.file_offset = pos + 2; e.reg = reg;
                e.page = hi; e.index = lo;
                e.value = val; e.original = val;
                init_regs_.push_back(e);
                pos += 3; count++;
            }
            break;
        }
    }
}

void Firmware040B::FindSubTables() {
    std::set<uint16_t> seen;
    for (auto& e : init_regs_) seen.insert(e.reg);

    std::vector<uint32_t> terms;
    for (uint32_t i = 0x3C00; i + 3 < (uint32_t)decoded_.size(); ++i) {
        if (decoded_[i]==0x0F && decoded_[i+1]==0xFF && decoded_[i+2]==0xFF)
            terms.push_back(i);
    }

    uint32_t main_end = init_table_off_;
    for (auto& e : init_regs_)
        main_end = std::max(main_end, e.file_offset);

    sub_table_count_ = 0;
    for (auto t : terms) {
        if (t <= main_end + 3) continue;
        uint32_t pos = t - 3;
        std::vector<InitRegEntry> sub;
        while (pos >= 0x3C00 && (t - pos) < 2100) {
            uint8_t hi = decoded_[pos], lo = decoded_[pos+1], val = decoded_[pos+2];
            if (hi > 0x04) break;
            if (hi == 0x0F && lo == 0xFF && val == 0xFF) break;
            uint16_t reg = ((uint16_t)hi << 8) | lo;
            if (seen.count(reg) == 0) {
                InitRegEntry e;
                e.file_offset = pos + 2; e.reg = reg;
                e.page = hi; e.index = lo;
                e.value = val; e.original = val;
                sub.push_back(e);
                seen.insert(reg);
            }
            if (pos < 3) break;
            pos -= 3;
        }
        std::reverse(sub.begin(), sub.end());
        sub_table_count_ += sub.size();
        init_regs_.insert(init_regs_.end(), sub.begin(), sub.end());
    }
}
