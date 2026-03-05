#pragma once
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  firmware_a.h â€” MK22F A-board firmware parser (ARM Cortex-M4)
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

struct FrameTimingSite {
    uint32_t payload_offset = 0;
    uint8_t  arm_register   = 0;
    uint16_t value_us       = 0;
    uint16_t original_us    = 0;
    float    Fps() const { return value_us ? 1000000.0f / value_us : 0; }
};

struct PanelInitEntry {
    uint32_t file_offset    = 0;
    uint32_t payload_offset = 0;
    uint8_t  reg            = 0;
    uint8_t  value          = 0;
    uint8_t  original       = 0;
    char     table_id       = 'A';
    uint16_t group_idx      = 0;
    uint8_t  pair_pos       = 0;
};

struct StringEntry {
    uint32_t payload_offset = 0;
    std::string text;
    std::string section; // "code" or "data"
};

struct VersionStringEntry {
    uint32_t    payload_offset = 0;
    uint32_t    max_length     = 0;   // max chars (excluding null terminator)
    std::string text;                 // current value
    std::string original;             // original value (for reset)
    std::string label;                // "Model Name", "Firmware Version", etc.
    bool        modified       = false;
};

// â”€â”€ Panel OLED register database â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
struct PanelRegInfo { const char* name; const char* desc; };

inline const std::unordered_map<uint8_t, PanelRegInfo>& GetPanelRegs() {
    static const std::unordered_map<uint8_t, PanelRegInfo> m = {
        {0x00,{"NOP","No operation / table padding byte"}},
        {0x01,{"SOFT_RESET","Software reset command"}},
        {0x02,{"POWER_CTRL","Power control / driver output level"}},
        {0x03,{"DISP_ID","Display identification readback"}},
        {0x04,{"READ_DDB","Read display identification"}},
        {0x05,{"DSI_ERR","DSI error status / flags"}},
        {0x06,{"DISP_FUNC_CTRL","Display function control, timing"}},
        {0x07,{"DISP_CTRL","Display control configuration"}},
        {0x08,{"PANEL_STATE","Panel state / configuration data"}},
        {0x09,{"DISP_STATUS","Display status / diagnostic readback"}},
        {0x0A,{"GET_POWER_STATE","Read power state register"}},
        {0x0B,{"GET_ADDR_MODE","Read address mode / scan direction"}},
        {0x0C,{"GET_PIXEL_FMT","Read pixel format register"}},
        {0x0D,{"GET_POWER_MODE","Read power mode / display state"}},
        {0x0E,{"GET_IMG_MODE","Read image display mode"}},
        {0x0F,{"GET_DIAG_RESULT","Read self-diagnostic result / signal mode"}},
        {0x10,{"SLEEP_IN","Enter sleep mode (low power)"}},
        {0x11,{"SLEEP_OUT","Exit sleep mode (wait after write)"}},
        {0x12,{"PARTIAL_MODE_ON","Partial display mode enable"}},
        {0x14,{"NORMAL_DISP_ON","Normal display mode / partial area"}},
        {0x15,{"PARTIAL_AREA","Partial area row start/end"}},
        {0x16,{"GAMMA_SET_A","Gamma correction voltage A"}},
        {0x17,{"GAMMA_REF_VOLT","Gamma reference voltage / gate output"}},
        {0x18,{"READ_FRAME_CNT","Read frame count / display line status"}},
        {0x19,{"VCOM_CTRL_1","VCOM voltage control 1"}},
        {0x1A,{"VCOM_CTRL_2","VCOM voltage control 2"}},
        {0x1B,{"VCOM_OFFSET","VCOM offset / fine trim"}},
        {0x1C,{"READ_ID4","Read display identification 4 (extended ID)"}},
        {0x1D,{"COL_ADDR_SET","Column address set"}},
        {0x1E,{"PIXEL_FORMAT","Pixel format (RGB / interface bits)"}},
        {0x1F,{"SELF_DIAG","Self-diagnostics result readback"}},
        {0x20,{"DISP_INVERSION","Display inversion control"}},
        {0x21,{"DISP_INV_ON","Display inversion on"}},
        {0x22,{"ALL_PIX_OFF","All pixels off command"}},
        {0x23,{"PANEL_CFG","Panel configuration register"}},
        {0x24,{"PANEL_DRIVE","Panel drive strength"}},
        {0x25,{"CTRL_DISP_WRITE","Write control display / CABC enable"}},
        {0x26,{"GAMMA_SET","Gamma curve selection"}},
        {0x27,{"CONTRAST_CTRL","Contrast / brightness fine adjust"}},
        {0x28,{"DISP_OFF","Display off"}},
        {0x29,{"DISP_ON","Display on command"}},
        {0x2A,{"COL_ADDR_START","Column address start"}},
        {0x2C,{"MEM_WRITE","Memory write start"}},
        {0x2E,{"MEM_READ","Memory read start"}},
        {0x2F,{"MEM_WRITE_CONT","Memory write continue (data stream)"}},
        {0x30,{"PARTIAL_AREA_DEF","Partial area definition"}},
        {0x31,{"GATE_CTRL","Gate driver control"}},
        {0x32,{"SCROLL_AREA","Vertical scroll area definition"}},
        {0x33,{"SCROLL_START","Vertical scroll start address"}},
        {0x34,{"TEAR_OFF","Tear effect line off"}},
        {0x35,{"TEAR_ON","Tear effect line on / mode select"}},
        {0x36,{"MEM_ACCESS_CTRL","Memory access / scan direction (MADCTL)"}},
        {0x37,{"VSCROLL_ADDR","Vertical scroll start address"}},
        {0x38,{"IDLE_OFF","Idle mode off"}},
        {0x39,{"IDLE_ON","Idle mode on"}},
        {0x3C,{"DISP_TIMING","Display timing / blanking control"}},
        {0x40,{"GATE_SCAN_START","Gate scan start position / voltage"}},
        {0x42,{"MEM_WRITE_CONT2","Memory write continue / stream data"}},
        {0x43,{"GATE_CTRL_EXT","Gate control extended"}},
        {0x44,{"SET_TEAR_LINE","Set tear effect scanline"}},
        {0x45,{"GET_SCANLINE","Read current scan line position"}},
        {0x46,{"SET_SCROLL_START","Set vertical scroll start address"}},
        {0x47,{"GET_BRIGHTNESS","Read current brightness level"}},
        {0x48,{"READ_CTRL_DISPLAY","Read control display / CABC state"}},
        {0x49,{"SRC_DRV_VOLTAGE","Source driver voltage adjust"}},
        {0x4B,{"SRC_DRV_CTRL","Source driver control"}},
        {0x4D,{"SRC_DRV_BIAS","Source driver bias level"}},
        {0x4E,{"SRC_DRV_FINE","Source driver fine adjust"}},
        {0x4F,{"PANEL_TIMING","Panel timing configuration"}},
        {0x50,{"CABC_CTRL","Content adaptive brightness control"}},
        {0x51,{"WRITE_BRIGHTNESS","Write display brightness value"}},
        {0x52,{"READ_BRIGHTNESS","Read display brightness value"}},
        {0x53,{"WRITE_CTRL_DISPLAY","Write CTRL_DISPLAY (backlight/CABC)"}},
        {0x55,{"INTERFACE_PIXEL","Interface pixel mode select"}},
        {0x56,{"ADAPTIVE_CTRL","Adaptive brightness / CABC control"}},
        {0x57,{"CABC_MIN_BRIGHT","CABC minimum brightness level"}},
        {0x58,{"CABC_MIN_BRIGHT_EXT","CABC minimum brightness extended"}},
        {0x60,{"GAMMA_CH_0","Gamma channel 0 voltage level"}},
        {0x61,{"GAMMA_CH_1","Gamma channel 1 voltage level"}},
        {0x62,{"GAMMA_CH_2","Gamma channel 2 voltage level"}},
        {0x63,{"GAMMA_CH_3","Gamma channel 3 voltage level"}},
        {0x64,{"GAMMA_CH_4","Gamma channel 4 voltage level"}},
        {0x66,{"GAMMA_CH_6","Gamma channel 6 voltage level"}},
        {0x67,{"GAMMA_CH_7","Gamma channel 7 voltage level"}},
        {0x68,{"GAMMA_CH_8","Gamma channel 8 voltage level"}},
        {0x6A,{"GAMMA_CH_A","Gamma channel A voltage level"}},
        {0x6B,{"GAMMA_CH_B","Gamma channel B voltage level"}},
        {0x6C,{"FRAME_RATE_A","Frame rate control A"}},
        {0x6D,{"FRAME_RATE_B","Frame rate control B"}},
        {0x6E,{"BRIGHTNESS","Panel brightness / greyscale level"}},
        {0x6F,{"FRAME_RATE_C","Frame rate control C"}},
        {0x70,{"GATE_DRV_CTRL","Gate driver control / scan mode"}},
        {0x72,{"GATE_OUT_EXT","Gate output extended / gamma trim"}},
        {0x73,{"READ_ID_EXT","Read extended identification / register bank"}},
        {0x74,{"SRC_VOLTAGE_A","Source output voltage level A"}},
        {0x75,{"CONTRAST","Contrast control value"}},
        {0x76,{"SRC_OUT_LEVEL","Source output level trim"}},
        {0x78,{"SRC_TIMING","Source timing control"}},
        {0x79,{"SRC_TIMING_D","Source timing control D"}},
        {0x7A,{"SRC_TIMING_B","Source timing control B"}},
        {0x7B,{"SRC_TIMING_C","Source timing control C"}},
        {0x7D,{"SRC_CLK_A","Source clock adjustment A"}},
        {0x7E,{"SRC_CLK_B","Source clock adjustment B"}},
        {0x80,{"GATE_OUT_0","Gate output level 0"}},
        {0x81,{"GATE_OUT_1","Gate output level 1"}},
        {0x82,{"GATE_OUT_2","Gate output level 2"}},
        {0x83,{"GATE_OUT_3","Gate output level 3"}},
        {0x84,{"GATE_OUT_4","Gate output level 4"}},
        {0x85,{"GATE_OUT_5","Gate output level 5"}},
        {0x87,{"GATE_OUT_7","Gate output level 7"}},
        {0x88,{"GATE_TIMING_A","Gate timing control A"}},
        {0x8B,{"GATE_TIMING_D","Gate timing control D"}},
        {0x8D,{"GATE_TIMING_E","Gate timing control E"}},
        {0x8E,{"GATE_TIMING_B","Gate timing control B"}},
        {0x8F,{"GATE_TIMING_C","Gate timing control C"}},
        {0x90,{"DSC_CTRL","Display stream compression control"}},
        {0x91,{"DSC_MODE","Display stream compression mode / bits-per-pixel"}},
        {0x93,{"SRC_CLK_CTRL","Source clock control"}},
        {0x95,{"DSC_PARAMS_A","Display stream compression parameters A"}},
        {0x96,{"POWER_GATE_A","Power / gate combined control A"}},
        {0x97,{"DSC_PARAMS_B","Display stream compression parameters B"}},
        {0x98,{"POWER_CFG_A","Power configuration A"}},
        {0x99,{"POWER_CFG_B","Power configuration B"}},
        {0x9A,{"POWER_CFG_C","Power configuration C"}},
        {0x9B,{"DSC_PARAMS_C","Display stream compression parameters C"}},
        {0x9C,{"POWER_TIMING","Power timing / sequencing"}},
        {0x9D,{"POWER_DRV_FINE","Power driver fine adjust"}},
        {0x9E,{"POWER_AMP","Power amplifier control"}},
        {0x9F,{"POWER_CFG_D","Power configuration D"}},
        {0xA0,{"CHECKSUM_A","Image data checksum A / first checksum"}},
        {0xA1,{"CHECKSUM_B","Image data checksum B / continue checksum"}},
        {0xA2,{"POWER_DRV_A","Power driver strength A"}},
        {0xA3,{"POWER_DRV_B","Power driver strength B"}},
        {0xA4,{"POWER_DRV_C","Power driver strength C"}},
        {0xA5,{"POWER_DRV_D","Power driver strength D"}},
        {0xA6,{"POWER_LEVEL_A","Power voltage level A"}},
        {0xA7,{"POWER_LEVEL_B","Power voltage level B"}},
        {0xA8,{"POWER_LEVEL_C","Power voltage level C / ref"}},
        {0xA9,{"POWER_LEVEL_D","Power voltage level D"}},
        {0xAB,{"POWER_REF_A","Power reference level A"}},
        {0xAC,{"POWER_REF_B","Power reference level B"}},
        {0xAD,{"FREE_RUN_CTRL","Free-running / scan mode control"}},
        {0xAE,{"DEEP_STANDBY","Deep standby mode control"}},
        {0xB0,{"POWER_SEQ_A","Power sequence control A"}},
        {0xB1,{"POWER_SEQ_B","Power sequence control B"}},
        {0xB3,{"INTF_TIMING","Interface timing control"}},
        {0xB4,{"DISP_CTRL_EXT","Display control extended / backlight PWM mode"}},
        {0xB5,{"BLANKING_CTRL","Blanking porch control"}},
        {0xBA,{"VCOM_DRV","VCOM driving strength"}},
        {0xBB,{"VCOM_LEVEL","VCOM DC level adjust"}},
        {0xBC,{"VCOM_DRV_EXT","VCOM driving extended control"}},
        {0xBF,{"POWER_EXT","Power control extended"}},
        {0xC0,{"MFR_PWR_CTRL_1","Manufacturer power control 1"}},
        {0xC2,{"MFR_PWR_CTRL_2","Manufacturer power control 2"}},
        {0xC3,{"MFR_PWR_CTRL_3","Manufacturer power control 3"}},
        {0xC4,{"MFR_PWR_CTRL_4","Manufacturer power control 4"}},
        {0xC5,{"MFR_PWR_CTRL_5","Manufacturer power control 5"}},
        {0xC6,{"MFR_PWR_CTRL_6","Manufacturer power control 6"}},
        {0xC7,{"MFR_PWR_CTRL_7","Manufacturer power control 7"}},
        {0xC8,{"MFR_GAMMA_A","Manufacturer gamma control A"}},
        {0xC9,{"MFR_GAMMA_B","Manufacturer gamma control B"}},
        {0xCA,{"MFR_GAMMA_C","Manufacturer gamma control C"}},
        {0xCB,{"MFR_GAMMA_D","Manufacturer gamma control D"}},
        {0xCC,{"MFR_GAMMA_E","Manufacturer gamma control E"}},
        {0xCD,{"MFR_GAMMA_F","Manufacturer gamma control F"}},
        {0xCE,{"MFR_DRV_CTRL_A","Manufacturer driver control A"}},
        {0xCF,{"MFR_DRV_CTRL_B","Manufacturer driver control B"}},
        {0xD0,{"MFR_POWER_A","Manufacturer power setting A"}},
        {0xD1,{"MFR_POWER_B","Manufacturer power setting B"}},
        {0xD3,{"MFR_POWER_C","Manufacturer power setting C"}},
        {0xD4,{"MFR_POWER_D","Manufacturer power setting D"}},
        {0xD8,{"MFR_POWER_E","Manufacturer power setting E"}},
        {0xD9,{"MFR_NVM_CTRL","Manufacturer NVM / status control"}},
        {0xDA,{"MFR_DEVICE_ID","Manufacturer device ID readback"}},
        {0xDB,{"MFR_VCOM_A","Manufacturer VCOM adjust A"}},
        {0xDD,{"MFR_VCOM_B","Manufacturer VCOM adjust B"}},
        {0xDE,{"MFR_VCOM_C","Manufacturer VCOM adjust C"}},
        {0xE0,{"POS_GAMMA","Positive gamma correction"}},
        {0xE1,{"POS_GAMMA_EXT","Positive gamma correction extended"}},
        {0xE2,{"POS_GAMMA_FINE","Positive gamma fine adjust"}},
        {0xE3,{"NEG_GAMMA","Negative gamma correction curve"}},
        {0xE4,{"NEG_GAMMA_EXT","Negative gamma correction extended"}},
        {0xE5,{"NEG_GAMMA_FINE","Negative gamma fine adjust"}},
        {0xE6,{"GAMMA_DRV_CTRL","Gamma driver control"}},
        {0xE7,{"GAMMA_CTRL","Gamma curve control"}},
        {0xE8,{"DRIVER_TIMING_A","Driver timing control A"}},
        {0xE9,{"DRIVER_TIMING_B","Driver timing control B / user adj"}},
        {0xEA,{"POWER_ON_SEQ","Power-on sequence control"}},
        {0xEB,{"POWER_ON_SEQ_EXT","Power-on sequence extended"}},
        {0xEC,{"MFR_TIMING_A","Manufacturer timing control A"}},
        {0xED,{"MFR_TIMING_B","Manufacturer timing control B"}},
        {0xEE,{"MFR_TIMING_C","Manufacturer timing control C"}},
        {0xEF,{"MFR_TIMING_D","Manufacturer timing control D"}},
        {0xF0,{"MFR_CMD_SET_A","Manufacturer command set enable A"}},
        {0xF1,{"MFR_CMD_SET_B","Manufacturer command set enable B"}},
        {0xF2,{"MFR_TEST_B","Manufacturer test / calibration command B"}},
        {0xF3,{"MFR_SEQ_CTRL","Manufacturer sequence control"}},
        {0xF4,{"INTERFACE_CTRL","Interface / format control"}},
        {0xF5,{"MFR_PUMP_A","Manufacturer charge pump A"}},
        {0xF6,{"MFR_PUMP_B","Manufacturer charge pump B"}},
        {0xF7,{"CMD_ACCESS","Command access protection"}},
        {0xF8,{"SPI_MFR_ACCESS","SPI manufacturer access / key"}},
        {0xF9,{"PUMP_RATIO","Charge pump ratio control"}},
        {0xFA,{"PUMP_CTRL","Charge pump control"}},
        {0xFB,{"MFR_TEST_CMD","Manufacturer test command"}},
        {0xFC,{"OSC_CTRL","Internal oscillator control"}},
        {0xFD,{"MFR_KEY","Manufacturer command key"}},
        {0xFE,{"CMD_SET_SELECT","Command set select / page switch"}},
        {0xFF,{"BANK_SELECT","Register bank select"}},
    };
    return m;
}

// -- MK22FN256 ARM Cortex-M4 peripheral register database ---
struct MK22FRegInfo { const char* name; const char* desc; };

inline const std::unordered_map<uint32_t, MK22FRegInfo>& GetMK22FRegs() {
    static const std::unordered_map<uint32_t, MK22FRegInfo> m = {
        {0x40047000,{"SIM_SOPT1","System Options 1"}},
        {0x40048004,{"SIM_SOPT2","System Options 2 (clock src select)"}},
        {0x40048034,{"SIM_SCGC4","Clock Gate 4 (UART/I2C/SPI)"}},
        {0x40048038,{"SIM_SCGC5","Clock Gate 5 (PORT A-E)"}},
        {0x4004803C,{"SIM_SCGC6","Clock Gate 6 (FTM/PIT/ADC/DAC)"}},
        {0x40048040,{"SIM_SCGC7","Clock Gate 7 (DMA/FlexBus)"}},
        {0x40048044,{"SIM_CLKDIV1","Clock Divider 1 (Core/Bus/Flash)"}},
        {0x40048048,{"SIM_CLKDIV2","Clock Divider 2 (USB)"}},
        {0x4004804C,{"SIM_FCFG1","Flash Config 1"}},
        {0x40048050,{"SIM_FCFG2","Flash Config 2"}},
        {0x40064000,{"MCG_C1","MCG Control 1 (CLKS, FRDIV)"}},
        {0x40064001,{"MCG_C2","MCG Control 2 (RANGE, HGO)"}},
        {0x40064004,{"MCG_C5","MCG Control 5 (PRDIV)"}},
        {0x40064005,{"MCG_C6","MCG Control 6 (VDIV, PLLS)"}},
        {0x40064006,{"MCG_S","MCG Status (CLKST, LOCK)"}},
        {0x40065000,{"OSC_CR","Oscillator Control (ERCLKEN)"}},
        {0x40052000,{"WDOG_STCTRLH","Watchdog Status/Control High"}},
        {0x4005200E,{"WDOG_UNLOCK","Watchdog Unlock (0xC520, 0xD928)"}},
        {0x40066000,{"I2C0_A1","I2C0 Address 1"}},
        {0x40066001,{"I2C0_F","I2C0 Frequency Divider"}},
        {0x40066002,{"I2C0_C1","I2C0 Control 1"}},
        {0x40066003,{"I2C0_S","I2C0 Status"}},
        {0x40066004,{"I2C0_D","I2C0 Data"}},
        {0x40067000,{"I2C1_A1","I2C1 Address 1"}},
        {0x40067001,{"I2C1_F","I2C1 Frequency Divider"}},
        {0x40067002,{"I2C1_C1","I2C1 Control 1"}},
        {0x4002C000,{"SPI0_MCR","SPI0 Module Config"}},
        {0x4002C00C,{"SPI0_CTAR0","SPI0 Clock/Transfer Attr 0"}},
        {0x4002C034,{"SPI0_PUSHR","SPI0 PUSH TX FIFO"}},
        {0x4006A000,{"UART0_BDH","UART0 Baud Rate High"}},
        {0x4006A001,{"UART0_BDL","UART0 Baud Rate Low"}},
        {0x4006A002,{"UART0_C1","UART0 Control 1"}},
        {0x4006A003,{"UART0_C2","UART0 Control 2"}},
        {0x4006A007,{"UART0_D","UART0 Data"}},
        {0x40038000,{"FTM0_SC","FTM0 Status/Control (CLKS, PS)"}},
        {0x40038008,{"FTM0_MOD","FTM0 Modulo (period)"}},
        {0x40039000,{"FTM1_SC","FTM1 Status/Control"}},
        {0x4003A000,{"FTM2_SC","FTM2 Status/Control"}},
        {0x40037000,{"PIT_MCR","PIT Module Control"}},
        {0x40037100,{"PIT0_LDVAL","PIT0 Load Value (period)"}},
        {0x40037108,{"PIT0_TCTRL","PIT0 Timer Control"}},
        {0x40037110,{"PIT1_LDVAL","PIT1 Load Value"}},
        {0x4003B000,{"ADC0_SC1A","ADC0 Status/Control 1A"}},
        {0x4003B008,{"ADC0_CFG1","ADC0 Config 1 (ADLPC, MODE)"}},
        {0x4003B00C,{"ADC0_CFG2","ADC0 Config 2"}},
        {0x40049000,{"PORTA_PCR0","Port A Pin 0 Control"}},
        {0x4004A000,{"PORTB_PCR0","Port B Pin 0 Control"}},
        {0x4004B000,{"PORTC_PCR0","Port C Pin 0 Control"}},
        {0x4004C000,{"PORTD_PCR0","Port D Pin 0 Control"}},
        {0x4004D000,{"PORTE_PCR0","Port E Pin 0 Control"}},
        {0x400FF000,{"GPIOA_PDOR","GPIO A Port Data Output"}},
        {0x400FF014,{"GPIOA_PDDR","GPIO A Port Data Direction"}},
        {0x400FF040,{"GPIOB_PDOR","GPIO B Port Data Output"}},
        {0x400FF080,{"GPIOC_PDOR","GPIO C Port Data Output"}},
        {0x400FF0C0,{"GPIOD_PDOR","GPIO D Port Data Output"}},
        {0x400FF100,{"GPIOE_PDOR","GPIO E Port Data Output"}},
        {0x4007D000,{"PMC_LVDSC1","Low Voltage Detect Status/Ctrl 1"}},
        {0x4007D002,{"PMC_REGSC","Regulator Status/Control"}},
        {0x4007F000,{"RCM_SRS0","System Reset Status 0"}},
        {0x40020000,{"FTFA_FSTAT","Flash Status"}},
        {0x40020002,{"FTFA_FSEC","Flash Security"}},
        {0x40020003,{"FTFA_FOPT","Flash Option"}},
        {0xE000E010,{"SYST_CSR","SysTick Control/Status"}},
        {0xE000E014,{"SYST_RVR","SysTick Reload Value"}},
        {0xE000ED08,{"SCB_VTOR","Vector Table Offset"}},
        {0xE000ED0C,{"SCB_AIRCR","App Interrupt/Reset Control"}},
    };
    return m;
}

// -- LT9211 MIPI-to-LVDS bridge register database ---
struct LT9211RegInfo { const char* name; const char* desc; };

inline const std::unordered_map<uint32_t, LT9211RegInfo>& GetLT9211Regs() {
    static const std::unordered_map<uint32_t, LT9211RegInfo> m = {
        {0x8100,{"CHIP_ID_0","Chip ID byte 0, expect 0x18"}},
        {0x8101,{"CHIP_ID_1","Chip ID byte 1, expect 0x01"}},
        {0x8102,{"CHIP_ID_2","Chip ID byte 2, expect 0xE3"}},
        {0x810A,{"CLK_DIV_RST","Clock divider reset/release"}},
        {0x810B,{"CLK_DIST_EN","Clock distribution enable, 0xFE=on"}},
        {0x8120,{"MASTER_RESET","Master reset / clock gate control"}},
        {0x816B,{"CLK_ENABLE","Clock enable, 0xFF=all on"}},
        {0x8201,{"SYS_CTRL","System control"}},
        {0x8202,{"RX_PHY_0","RX PHY config 0"}},
        {0x8204,{"RX_PHY_1","RX PHY config 1"}},
        {0x8205,{"RX_PHY_2","RX PHY config 2"}},
        {0x8207,{"RX_PHY_3","RX PHY config 3"}},
        {0x8208,{"RX_PHY_4","RX PHY config 4"}},
        {0x8209,{"RX_DN_DP_SWAP","DSI DN/DP swap (ORR 0xF8 to enable)"}},
        {0x8217,{"RX_PHY_5","RX PHY config 5"}},
        {0x822D,{"DESSC_PLL_REF","DeSSC PLL reference (0x48=25 MHz XTal)"}},
        {0x8235,{"DESSC_PLL_DIV","DeSSC PLL divider - clock range select"}},
        {0x8236,{"TX_PLL_PD","TX PLL power down (0x01=reset)"}},
        {0x8237,{"TX_PLL_CFG0","TX PLL config 0"}},
        {0x8238,{"TX_PLL_CFG1","TX PLL config 1"}},
        {0x8239,{"TX_PLL_CFG2","TX PLL config 2"}},
        {0x823A,{"TX_PLL_CFG3","TX PLL config 3"}},
        {0x823B,{"LVDS_CFG","LVDS config (BIT7=dual-port)"}},
        {0x823E,{"TX_PHY_0","TX PHY register 0"}},
        {0x823F,{"TX_PHY_1","TX PHY register 1"}},
        {0x8240,{"TX_PHY_2","TX PHY register 2"}},
        {0x8243,{"TX_PHY_3","TX PHY register 3"}},
        {0x8244,{"TX_PHY_4","TX PHY register 4"}},
        {0x8245,{"TX_PHY_5","TX PHY register 5"}},
        {0x8249,{"TX_PHY_6","TX PHY register 6"}},
        {0x824A,{"TX_PHY_7","TX PHY register 7"}},
        {0x824E,{"TX_PHY_8","TX PHY register 8"}},
        {0x824F,{"TX_PHY_9","TX PHY register 9"}},
        {0x8250,{"TX_PHY_10","TX PHY register 10"}},
        {0x8253,{"TX_PHY_11","TX PHY register 11"}},
        {0x8254,{"TX_PHY_12","TX PHY register 12"}},
        {0x8262,{"DPI_OUT_EN","DPI output enable/disable"}},
        {0x8559,{"LVDS_FORMAT","LVDS: BIT7=JEIDA, BIT5=DE, BIT4=24bpp"}},
        {0x855A,{"LVDS_MAP_0","LVDS data mapping 0"}},
        {0x855B,{"LVDS_MAP_1","LVDS data mapping 1"}},
        {0x855C,{"LVDS_DUAL_LINK","Dual-link enable (BIT0)"}},
        {0x8588,{"IO_MODE","BIT6=MIPI-RX, BIT4=LVDS-TX"}},
        {0x85A1,{"DIG_CFG","LVDS digital config"}},
        {0x8600,{"BYTECLK_MEAS","ByteClock measurement trigger"}},
        {0x8606,{"RX_MEAS_CFG0","RX measurement config 0"}},
        {0x8607,{"RX_MEAS_CFG1","RX measurement config 1"}},
        {0x8608,{"BYTECLK_19_16","ByteClock [19:16] readback"}},
        {0x8609,{"BYTECLK_15_8","ByteClock [15:8] readback"}},
        {0x860A,{"BYTECLK_7_0","ByteClock [7:0] readback"}},
        {0x8630,{"RX_DIG_MODE","RX digital mode"}},
        {0x8633,{"RX_PHY_DIG","RX PHY digital"}},
        {0x8640,{"LVDS_OUT_CFG0","LVDS output config 0"}},
        {0x8641,{"LVDS_OUT_CFG1","LVDS output config 1"}},
        {0x8642,{"LVDS_OUT_CFG2","LVDS output config 2"}},
        {0x8643,{"LVDS_OUT_CFG3","LVDS output config 3 (swing)"}},
        {0x8644,{"LVDS_OUT_CFG4","LVDS output config 4 (pre-emphasis)"}},
        {0x8645,{"LVDS_OUT_CFG5","LVDS output config 5 (termination)"}},
        {0x8646,{"LVDS_CH_ORDER","Ch order (0x10=A:B, 0x40=swap)"}},
        {0x8713,{"TX_PLL_RST","TX PLL reset/release"}},
        {0x8714,{"TX_PLL_TMR0","TX PLL timer 0"}},
        {0x8715,{"TX_PLL_TMR1","TX PLL timer 1"}},
        {0x8718,{"TX_PLL_TMR2","TX PLL timer 2"}},
        {0x8722,{"TX_PLL_TMR3","TX PLL timer 3"}},
        {0x8723,{"TX_PLL_TMR4","TX PLL timer 4"}},
        {0x8726,{"TX_PLL_TMR5","TX PLL timer 5"}},
        {0x8737,{"TX_PLL_FINE","TX PLL fine-tune"}},
        {0x871F,{"TX_PLL_LOCK_A","TX PLL lock status A (bit 7)"}},
        {0x8720,{"TX_PLL_LOCK_B","TX PLL lock status B (bit 7)"}},
        {0xD000,{"DSI_LANE_CNT","DSI lane count"}},
        {0xD002,{"DSI_CFG","DSI config"}},
        {0xD00D,{"VTOTAL_H","Vertical total high byte"}},
        {0xD00E,{"VTOTAL_L","Vertical total low byte"}},
        {0xD00F,{"VACTIVE_H","Vertical active high byte"}},
        {0xD010,{"VACTIVE_L","Vertical active low byte"}},
        {0xD011,{"HTOTAL_H","Horizontal total high byte"}},
        {0xD012,{"HTOTAL_L","Horizontal total low byte"}},
        {0xD013,{"HACTIVE_H","Horizontal active high byte"}},
        {0xD014,{"HACTIVE_L","Horizontal active low byte"}},
        {0xD015,{"VS_WIDTH","VSync width"}},
        {0xD016,{"HS_WIDTH","HSync width"}},
        {0xD017,{"VFP_H","V front porch high"}},
        {0xD018,{"VFP_L","V front porch low"}},
        {0xD019,{"HFP_H","H front porch high"}},
        {0xD01A,{"HFP_L","H front porch low"}},
        {0xD023,{"PCR_CFG0","Pixel Clock Recovery config 0"}},
        {0xD026,{"PCR_CFG1","PCR config 1"}},
        {0xD027,{"PCR_CFG2","PCR config 2"}},
        {0xD02D,{"PCR_CFG3","PCR config 3"}},
        {0xD031,{"PCR_CFG4","PCR config 4"}},
        {0xD038,{"PCR_FLT0","PCR filter 0"}},
        {0xD039,{"PCR_FLT1","PCR filter 1"}},
        {0xD03A,{"PCR_FLT2","PCR filter 2"}},
        {0xD03B,{"PCR_FLT3","PCR filter 3"}},
        {0xD03F,{"PCR_FLT4","PCR filter 4"}},
        {0xD040,{"PCR_FLT5","PCR filter 5"}},
        {0xD041,{"PCR_FLT6","PCR filter 6"}},
        {0xD082,{"AUTO_WIDTH_H","Auto-detected width high"}},
        {0xD083,{"AUTO_WIDTH_L","Auto-detected width low"}},
        {0xD084,{"AUTO_FMT","Auto format (0x3=YUV, 0xA=RGB)"}},
        {0xD085,{"AUTO_HEIGHT_H","Auto-detected height high"}},
        {0xD086,{"AUTO_HEIGHT_L","Auto-detected height low"}},
        {0xD087,{"PCR_LOCK","PCR lock status (bit 3)"}},
        {0xD404,{"OUT_CFG_A","Output config A"}},
        {0xD405,{"OUT_CFG_B","Output config B"}},
        {0xD420,{"LVDS_OUT_FMT","LVDS output format"}},
        {0xD421,{"LVDS_OUT_POL","LVDS output polarity/mapping"}},
    };
    return m;
}

// -- IT6802 HDMI Receiver register database ---
struct IT6802RegInfo { const char* name; const char* desc; };

inline const std::unordered_map<uint8_t, IT6802RegInfo>& GetIT6802Regs() {
    static const std::unordered_map<uint8_t, IT6802RegInfo> m = {
        {0x04,{"SYS_CTRL","System control"}},
        {0x05,{"SYS_STATUS","System status (read-only)"}},
        {0x06,{"INT_CTRL","Interrupt control"}},
        {0x10,{"HDMI_STATUS","HDMI link status"}},
        {0x13,{"CONTRAST_LO","Contrast coarse / CSC control"}},
        {0x14,{"CONTRAST_HI","Contrast fine"}},
        {0x15,{"BRIGHTNESS","Brightness offset (signed 8-bit)"}},
        {0x58,{"PLL_CTL_0","PLL config 0"}},
        {0x59,{"PLL_CTL_1","PLL config 1"}},
        {0x5A,{"PLL_CTL_2","PLL config 2"}},
        {0x60,{"TMDS_CLK_0","TMDS clock byte 0"}},
        {0x61,{"TMDS_CLK_1","TMDS clock byte 1"}},
        {0x62,{"TMDS_CLK_2","TMDS clock byte 2"}},
        {0x84,{"HUE","Hue angle (bank 2)"}},
        {0x8A,{"SATURATION","Color saturation gain (bank 2/8)"}},
        {0x8B,{"SHARPNESS","Edge enhancement / sharpness"}},
        {0x9A,{"H_TOTAL_H","Horizontal total high"}},
        {0x9B,{"H_TOTAL_L","Horizontal total low"}},
        {0x9C,{"H_ACTIVE_H","Horizontal active high"}},
        {0x9D,{"H_ACTIVE_L","Horizontal active low"}},
        {0x9E,{"V_TOTAL_H","Vertical total high"}},
        {0x9F,{"V_TOTAL_L","Vertical total low"}},
        {0xA0,{"V_ACTIVE_H","Vertical active high"}},
        {0xA1,{"V_ACTIVE_L","Vertical active low"}},
        {0xA8,{"VID_MODE","Interlace/Progressive detect"}},
        {0xFF,{"BANK_SEL","Register bank select"}},
    };
    return m;
}


// â”€â”€ Frame period constants â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

class FirmwareABoard {
public:
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    void SetFramePeriod(FrameTimingSite& s, uint16_t us);
    void SetPanelValue(PanelInitEntry& e, uint8_t val);
    bool PatchString(VersionStringEntry& vs, const std::string& new_text);

    // accessors
    const std::vector<uint8_t>& Decoded() const { return decoded_; }
    const std::vector<uint8_t>& Raw()     const { return raw_; }
    std::vector<FrameTimingSite>&  TimingSites()  { return timing_sites_; }
    std::vector<PanelInitEntry>&   PanelInits()   { return panel_inits_; }
    std::vector<StringEntry>&      Strings()      { return strings_; }
    std::vector<VersionStringEntry>& VersionStrings() { return version_strings_; }

    const std::string& Filename()  const { return filename_; }
    const std::string& BuildDate() const { return build_date_; }
    bool   IsLoaded()  const { return !decoded_.empty(); }
    bool   Is040()     const { return is_040_; }
    uint32_t SpInit()  const { return sp_init_; }
    uint32_t ResetVec()const { return reset_vec_; }
    uint32_t CodeBoundary() const { return code_boundary_; }

    static constexpr uint32_t PAYLOAD_START = 0x210;
    static constexpr uint32_t FLASH_BASE    = 0xC000;
    static constexpr uint16_t FRAME_PERIOD_STOCK = 10000; // 100 fps
    static constexpr uint16_t FRAME_PERIOD_120   = 8333;  // 120 fps
    static constexpr uint16_t FRAME_PERIOD_144   = 6944;  // ~144 fps

    uint32_t PayloadToFile(uint32_t p) const { return p + PAYLOAD_START; }
    uint32_t PayloadToRuntime(uint32_t p) const { return p + FLASH_BASE; }

private:
    void Decode();
    void WriteDecoded(uint32_t poff, uint8_t val);
    void FindCodeBoundary();
    void FindStrings();
    void FindTimingSites();
    void FindPanelInits();
    void FindVersionStrings();

    // ARM Thumb helpers
    static uint16_t DecodeMOVW(const uint8_t* p);
    static void     EncodeMOVW(uint8_t* p, uint8_t rd, uint16_t imm16);

    std::vector<uint8_t> raw_;
    std::vector<uint8_t> decoded_;
    std::vector<FrameTimingSite> timing_sites_;
    std::vector<PanelInitEntry>  panel_inits_;
    std::vector<StringEntry>     strings_;
    std::vector<VersionStringEntry> version_strings_;
    std::string filename_;
    std::string build_date_;
    bool is_040_ = false;
    uint32_t sp_init_  = 0, reset_vec_ = 0;
    uint32_t code_boundary_ = 0;
};
