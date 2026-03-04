#!/usr/bin/env python3
"""
Skyzone SKY04X Pro — Unified Firmware Editor  v1.0

Visual editor for modifying both MCU firmwares in the Skyzone SKY04X Pro
FPV goggles:

  B Board — Intersil TW8836 (8051 MCU)
    Video processor, OSD engine, analog decoder
    Handles: delay timing, image quality, register init tables
    Format: Raw MCU binary (204,800 bytes) or SPI flash image

  A Board — NXP Kinetis MK22FN256 (ARM Cortex-M4)
    Display controller, HDMI receiver, LVDS bridge
    Handles: panel driver init tables, register reference, strings
    Format: Skyzone header + XOR-0x55 encoded payload (~222 KB)

Features:
  - Visual slider controls for image quality registers
  - Editable delay timing with presets
  - Panel OLED driver init table editor
  - Complete register databases (500+ documented registers)
  - Hex viewer with modification highlighting
  - Auto-backup, timestamped saves, companion patch logs
  - Dark theme (Catppuccin Mocha)

Usage:
    python tools/skyzone_editor.py
    python tools/skyzone_editor.py --a path/to/A.bin --b path/to/B.bin
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox, font as tkfont
import struct
import hashlib
import shutil
import sys
import os
from pathlib import Path
from datetime import datetime
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple
from collections import Counter

VERSION = "1.0.0"

# ════════════════════════════════════════════════════════════════════════
# Theme / Colors  (Catppuccin Mocha)
# ════════════════════════════════════════════════════════════════════════
C = {
    "bg":       "#1e1e2e",
    "bg2":      "#282840",
    "bg3":      "#313145",
    "fg":       "#cdd6f4",
    "dim":      "#6c7086",
    "accent":   "#89b4fa",
    "accent2":  "#74c7ec",
    "warn":     "#fab387",
    "err":      "#f38ba8",
    "ok":       "#a6e3a1",
    "mod":      "#f9e2af",
    "border":   "#45475a",
    "sel":      "#585b70",
}


# ════════════════════════════════════════════════════════════════════════
# Scrollable Frame Helper
# ════════════════════════════════════════════════════════════════════════

class ScrollFrame(ttk.Frame):
    """A vertically-scrollable frame.  Pack content into *.inner*."""

    def __init__(self, parent, **kw):
        super().__init__(parent, **kw)
        self.canvas = tk.Canvas(self, bg=C["bg"], highlightthickness=0, bd=0)
        self.vsb = ttk.Scrollbar(self, orient="vertical",
                                  command=self.canvas.yview)
        self.inner = ttk.Frame(self.canvas)
        self._win = self.canvas.create_window((0, 0), window=self.inner,
                                               anchor="nw")
        self.inner.bind("<Configure>",
            lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>",
            lambda e: self.canvas.itemconfig(self._win, width=e.width))
        self.canvas.configure(yscrollcommand=self.vsb.set)
        self.vsb.pack(side="right", fill="y")
        self.canvas.pack(side="left", fill="both", expand=True)

    def bind_scroll(self):
        """Bind mousewheel scrolling — call after populating .inner."""
        self._do_bind(self.canvas)
        self._do_bind(self.inner)
        for child in self._all_children(self.inner):
            self._do_bind(child)

    def _do_bind(self, widget):
        widget.bind("<MouseWheel>", self._on_wheel)

    def _all_children(self, widget):
        out = []
        for ch in widget.winfo_children():
            out.append(ch)
            out.extend(self._all_children(ch))
        return out

    def _on_wheel(self, event):
        self.canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")
        return "break"


# ╔══════════════════════════════════════════════════════════════════════╗
# ║  B BOARD — Intersil TW8836 (8051 MCU Video Processor)             ║
# ╚══════════════════════════════════════════════════════════════════════╝

REGISTER_INFO: Dict[int, Tuple[str, str]] = {
    # ── Page 0 — System ──────────────────────────────────────────────
    0x002: ("INT_STATUS",          "Interrupt Status clear, write 0xFF to clear all"),
    0x003: ("INT_MASK",            "Interrupt Mask, 0xFF=all masked"),
    0x006: ("SYSTEM_CTRL",         "System Control, stock=0x06"),
    0x007: ("OUTPUT_CTRL",         "Output Ctrl (TCONSEL[2:0], EN656OUT[3])"),
    0x008: ("OUTPUT_FORMAT",       "Output Format, stock=0x86"),
    0x00F: ("PANEL_CTRL",          "Panel Control"),
    0x01F: ("OUTPUT_CTRL2",        "Output Control II"),
    # ── Page 0 — Input ───────────────────────────────────────────────
    0x040: ("INPUT_CTRL_I",        "Input Control I (mux select)"),
    0x041: ("INPUT_CTRL_II",       "Input Control II (ImplicitDE[4], FieldPol[3], YUV[0])"),
    0x042: ("INPUT_SEL",           "Input Selection / Mux, stock=0x02"),
    0x043: ("INPUT_MISC",          "Input Misc, stock=0x20"),
    0x044: ("INPUT_CROP_HI",       "Input Crop High bits"),
    0x045: ("INPUT_CROP_HSTART",   "Input Crop H Start"),
    0x047: ("BT656_CTRL",          "BT656 Control, stock=0x02"),
    0x048: ("BT656_CTRL2",         "BT656 Control 2"),
    0x04B: ("BT656_MISC",          "BT656 Misc"),
    # ── Page 0 — DTV ─────────────────────────────────────────────────
    0x050: ("DTV_CTRL0",           "DTV Control 0"),
    0x051: ("DTV_CTRL1",           "DTV Control 1"),
    0x052: ("DTV_CTRL2",           "DTV Control 2"),
    0x053: ("DTV_CTRL3",           "DTV Control 3"),
    0x054: ("DTV_CTRL4",           "DTV Control 4"),
    0x055: ("DTV_CTRL5",           "DTV Control 5"),
    0x056: ("DTV_CTRL6",           "DTV Control 6"),
    0x057: ("DTV_CTRL7",           "DTV Control 7"),
    0x058: ("DTV_CTRL8",           "DTV Control 8"),
    0x059: ("DTV_CTRL9",           "DTV Control 9"),
    0x05A: ("DTV_CTRL10",          "DTV Control 10"),
    0x05B: ("DTV_CTRL11",          "DTV Control 11"),
    0x05C: ("DTV_CTRL12",          "DTV Control 12"),
    0x05D: ("DTV_CTRL13",          "DTV Control 13"),
    0x05E: ("DTV_CTRL14",          "DTV Control 14"),
    0x05F: ("DTV_CTRL15",          "DTV Control 15"),
    # ── Page 0 — GPIO ────────────────────────────────────────────────
    0x080: ("GPIO_00",             "GPIO Control 0x80, init=0x00"),
    0x081: ("GPIO_01",             "GPIO Control 0x81"),
    0x082: ("GPIO_02",             "GPIO Control 0x82"),
    0x083: ("GPIO_03",             "GPIO Control 0x83"),
    0x084: ("GPIO_04",             "GPIO Control 0x84"),
    0x085: ("GPIO_05",             "GPIO Control 0x85"),
    0x086: ("GPIO_06",             "GPIO Control 0x86"),
    0x087: ("GPIO_07",             "GPIO Control 0x87"),
    0x088: ("GPIO_08",             "GPIO Control 0x88"),
    0x089: ("GPIO_09",             "GPIO Control 0x89"),
    0x08A: ("GPIO_0A",             "GPIO Control 0x8A"),
    0x08B: ("GPIO_0B",             "GPIO Control 0x8B"),
    0x08C: ("GPIO_0C",             "GPIO Control 0x8C"),
    0x08D: ("GPIO_0D",             "GPIO Control 0x8D"),
    0x08E: ("GPIO_0E",             "GPIO Control 0x8E"),
    0x08F: ("GPIO_0F",             "GPIO Control 0x8F"),
    0x090: ("GPIO_10",             "GPIO Control 0x90"),
    0x091: ("GPIO_11",             "GPIO Control 0x91"),
    0x092: ("GPIO_12",             "GPIO Control 0x92"),
    0x093: ("GPIO_13",             "GPIO Control 0x93"),
    0x094: ("GPIO_14",             "GPIO Control 0x94"),
    0x095: ("GPIO_15",             "GPIO Control 0x95"),
    0x096: ("GPIO_16",             "GPIO Control 0x96"),
    0x097: ("GPIO_17",             "GPIO Control 0x97"),
    0x098: ("GPIO_18",             "GPIO Control 0x98"),
    0x099: ("GPIO_19",             "GPIO Control 0x99"),
    0x09A: ("GPIO_1A",             "GPIO Control 0x9A"),
    0x09B: ("GPIO_1B",             "GPIO Control 0x9B"),
    0x09C: ("GPIO_1C",             "GPIO Control 0x9C"),
    0x09D: ("GPIO_1D",             "GPIO Control 0x9D"),
    0x09E: ("GPIO_1E",             "GPIO Control 0x9E"),
    0x09F: ("GPIO_1F",             "GPIO Control 0x9F"),
    # ── Page 0 — MBIST ───────────────────────────────────────────────
    0x0A0: ("MBIST_CTRL",          "Memory BIST Control"),
    # ── Page 0 — Touch / TSC ─────────────────────────────────────────
    0x0B0: ("TSC_ADC_CTRL",        "Touch ADC Control, [7]=enable, stock=0x87"),
    0x0B1: ("TSC_CONFIG",          "Touch Config, stock=0xC0"),
    0x0B4: ("TSC_ADC_CFG",         "Touch ADC Config, stock=0x0A"),
    # ── Page 0 — LOPOR / Power ────────────────────────────────────────
    0x0D4: ("LOPOR_CTRL",          "Low Power (LOPOR) Control"),
    0x0D6: ("PWM_CTRL",            "PWM / LEDC / Backlight Control"),
    # ── Page 0 — SSPLL ───────────────────────────────────────────────
    0x0F6: ("SSPLL_0",             "SSPLL Register 0"),
    0x0F7: ("SSPLL_1",             "SSPLL Register 1, stock=0x16"),
    0x0F8: ("FPLL0",               "SSPLL FPLL[19:16], PCLK divider"),
    0x0F9: ("FPLL1",               "SSPLL FPLL[15:8]"),
    0x0FA: ("FPLL2",               "SSPLL FPLL[7:0]"),
    0x0FB: ("SSPLL_5",             "SSPLL Register 5, stock=0x40"),
    0x0FC: ("SSPLL_6",             "SSPLL Register 6, stock=0x23"),
    0x0FD: ("SSPLL_ANALOG",        "SSPLL Analog: POST[7:6] VCO[5:4] Pump[2:0]"),
    # ── Page 1 — Video Decoder ────────────────────────────────────────
    0x101: ("DEC_CSTATUS",         "Chip Status (read-only)"),
    0x102: ("DEC_INFORM",          "Input Format / Mux Select"),
    0x104: ("DEC_HSYNC_DLY",       "HSYNC Delay Control"),
    0x105: ("DEC_AFE_MODE",        "AFE Mode / Anti-Aliasing Filter on/off"),
    0x106: ("DEC_ACNTL",           "Analog Control (ADC power), stock=0x03"),
    0x107: ("DEC_CROP_HI",         "Cropping High bits (V/H MSBs), stock=0x02"),
    0x108: ("★ DEC_VDELAY",        "Vertical Delay Low, stock=0x12 (18 lines)"),
    0x109: ("DEC_VACTIVE",         "Vertical Active Low, stock=0xF0 (240 lines)"),
    0x10A: ("DEC_HDELAY",          "Horizontal Delay Low, stock=0x0B (11)"),
    0x10B: ("DEC_HACTIVE",         "Horizontal Active Low, stock=0xD0 (720px)"),
    0x10C: ("★ DEC_CNTRL1",        "Decoder Ctrl 1 (comb: 0xCC=2D, 0xDC=3D)"),
    0x10D: ("DEC_CNTRL2",          "Decoder Control 2"),
    0x110: ("★ DEC_BRIGHT",        "Brightness, stock=0x00, 0x80=neutral"),
    0x111: ("★ DEC_CONTRAST",      "Contrast, stock=0x5C (92), range 0-255"),
    0x112: ("★ DEC_SHARPNESS",     "Sharpness, stock=0x11 (17), range 0-255"),
    0x113: ("★ DEC_SAT_U",         "Chroma U Saturation, stock=0x80 (128)"),
    0x114: ("★ DEC_SAT_V",         "Chroma V Saturation, stock=0x80 (128)"),
    0x115: ("DEC_HUE",             "Hue Control, stock=0x00"),
    0x117: ("DEC_V_PEAKING",       "Vertical Peaking, stock=0x80"),
    0x118: ("DEC_MISC_TIMING",     "Decoder Misc Timing, stock=0x44"),
    0x11A: ("DEC_CLAMP_ADJ",       "Clamp Adjust / Level Control"),
    0x11C: ("DEC_SDT",             "Standard Selection (auto-detect), stock=0x07"),
    0x11D: ("DEC_SDTR",            "Standard Recognition (read-only)"),
    0x11E: ("DEC_CTRL3",           "Decoder Control 3"),
    # ── Page 1 — CTI ─────────────────────────────────────────────────
    0x120: ("CTI_COEFF_0",         "CTI Coefficient 0, stock=0x50"),
    0x121: ("CTI_COEFF_1",         "CTI Coefficient 1, stock=0x22"),
    0x122: ("CTI_COEFF_2",         "CTI Coefficient 2, stock=0xF0"),
    0x123: ("CTI_COEFF_3",         "CTI Coefficient 3, stock=0xD8"),
    0x124: ("CTI_COEFF_4",         "CTI Coefficient 4, stock=0xBC"),
    0x125: ("CTI_COEFF_5",         "CTI Coefficient 5, stock=0xB8"),
    0x126: ("CTI_COEFF_6",         "CTI Coefficient 6, stock=0x44"),
    0x127: ("CTI_COEFF_7",         "CTI Coefficient 7, stock=0x38"),
    0x128: ("CTI_COEFF_8",         "CTI Coefficient 8, stock=0x00"),
    0x129: ("DEC_V_CTRL2",         "Vertical Control II, stock=0x00"),
    0x12A: ("CTI_COEFF_A",         "CTI Coefficient A, stock=0x78"),
    0x12B: ("CTI_COEFF_B",         "CTI Coefficient B, stock=0x44"),
    0x12C: ("DEC_HFILTER",         "Horizontal Filter, stock=0x30"),
    0x12D: ("DEC_MISC1",           "Miscellaneous Control 1, stock=0x14"),
    0x12E: ("DEC_MISC2",           "Miscellaneous Control 2, stock=0xA5"),
    0x12F: ("DEC_MISC3",           "Miscellaneous Control 3, stock=0xE0"),
    # ── Page 1 — Decoder Freerun / VBI / BT656 ────────────────────────
    0x133: ("DEC_FREERUN",         "Freerun [7:6]=mode(0:Auto,2:60Hz,3:50Hz)"),
    0x134: ("DEC_VBI_CNTL2",       "VBI Control 2 / WSSEN, stock=0x1A"),
    0x135: ("DEC_CC_ODDLINE",      "CC Odd Line, stock=0x00"),
    0x136: ("DEC_CC_EVENLINE",     "CC Even Line, stock=0x03"),
    0x137: ("BT656_DI_HDELAY",     "BT656 DeInterlace H Delay, stock=0x28"),
    0x138: ("BT656_DI_HSTART",     "BT656 DeInterlace H Start, stock=0xAF"),
    # ── Page 1 — LLPLL ───────────────────────────────────────────────
    0x1C0: ("LLPLL_INPUT_CFG",     "LLPLL Input Config, [0]=clk(0=PLL,1=27MHz)"),
    0x1C2: ("LLPLL_VCO_CP",        "VCO Range[5:4] & Charge Pump[2:0], stock=0xD2"),
    0x1C3: ("LLPLL_DIV_H",         "LLPLL Divider High [3:0]"),
    0x1C4: ("LLPLL_DIV_L",         "LLPLL Divider Low [7:0], total=(H:L)=Htotal-1"),
    0x1C5: ("LLPLL_PHASE",         "PLL Phase [4:0]"),
    0x1C6: ("LLPLL_FILTER_BW",     "PLL Loop Filter Bandwidth, stock=0x20"),
    0x1C7: ("LLPLL_VCO_NOM_H",     "VCO Nominal Freq High, stock=0x04"),
    0x1C8: ("LLPLL_VCO_NOM_L",     "VCO Nominal Freq Low, stock=0x00"),
    0x1C9: ("LLPLL_PRE_COAST",     "Pre-Coast, stock=0x06"),
    0x1CA: ("LLPLL_POST_COAST",    "Post-Coast, stock=0x06"),
    0x1CB: ("VADC_SOG_PLL",        "SOG[7]/PLL[6] Power, SOG Threshold[4:0]"),
    0x1CC: ("VADC_SYNC_SEL",       "Sync Output/Polarity Select"),
    0x1CD: ("LLPLL_CTRL",          "LLPLL Control/Status, [0]=init trigger"),
    # ── Page 1 — VADC ────────────────────────────────────────────────
    0x1D0: ("VADC_GAIN_MSB",       "ADC Gain MSBs [2:0]=Y/C/V ch MSBs"),
    0x1D1: ("VADC_GAIN_Y",         "Y/G Channel Gain, stock=0xF0"),
    0x1D2: ("VADC_GAIN_C",         "C/B Channel Gain, stock=0xF0"),
    0x1D3: ("VADC_GAIN_V",         "V/R Channel Gain, stock=0xF0"),
    0x1D4: ("VADC_CLAMP_MODE",     "Clamp Mode Control"),
    0x1D5: ("VADC_CLAMP_START",    "Clamp Start Position"),
    0x1D6: ("VADC_CLAMP_STOP",     "Clamp Stop Position"),
    0x1D7: ("VADC_CLAMP_LOC",      "Master Clamp Location"),
    0x1D8: ("VADC_DEBUG",          "VADC Debug Register"),
    0x1D9: ("VADC_CLAMP_GY",       "Clamp G/Y Level"),
    0x1DA: ("VADC_CLAMP_BU",       "Clamp B/U Level"),
    0x1DB: ("VADC_CLAMP_RV",       "Clamp R/V Level"),
    0x1DC: ("VADC_HS_WIDTH",       "HS Width Control"),
    # ── Page 1 — AFE ─────────────────────────────────────────────────
    0x1E0: ("AFE_TEST",            "AFE Test Mode"),
    0x1E1: ("AFE_GPLL_PD",         "GPLL Power Down [5]=PD, stock=0x05"),
    0x1E2: ("AFE_BIAS_VREF",       "AFE Bias & VREF, stock=0x59"),
    0x1E3: ("AFE_PATH_0",          "AFE Bias cfg (DEC=0x07, aRGB=0x37)"),
    0x1E4: ("AFE_PATH_1",          "AFE Bias cfg (DEC=0x33, aRGB=0x55)"),
    0x1E5: ("AFE_PATH_2",          "AFE Bias cfg (DEC=0x33, aRGB=0x55)"),
    0x1E6: ("AFE_PATH_3",          "AFE PGA Speed (DEC=0x00, aRGB=0x20)"),
    0x1E7: ("AFE_AAF",             "Anti-Aliasing Filter, stock=0x2A"),
    0x1E8: ("AFE_PATH_5",          "AFE Path Select (DEC=0x0F, aRGB=0x00)"),
    0x1E9: ("AFE_LLCLK",           "LLCLK Polarity & CLKO Select"),
    0x1EA: ("AFE_MISC",            "AFE Misc, stock=0x03"),
    0x1F6: ("VADC_MISC2",          "VADC Miscellaneous 2"),
    # ── Page 2 — Scaler ──────────────────────────────────────────────
    0x201: ("SC_CTRL1",            "Scaler Control 1"),
    0x202: ("SC_XUP_H",            "H Up-Scale Ratio High"),
    0x203: ("SC_XUP_L",            "H Up-Scale Ratio Low"),
    0x204: ("SC_XDOWN_H",          "H Down-Scale Ratio High"),
    0x205: ("SC_VSCALE_L",         "V Scale Ratio Low"),
    0x206: ("SC_VSCALE_H",         "V Scale Ratio High"),
    0x207: ("SC_HSCALE_H",         "H Scale Ratio High"),
    0x208: ("SC_HSCALE_L",         "H Scale Ratio Low"),
    0x209: ("SC_XDOWN_L",          "H Down-Scale Ratio Low"),
    0x20A: ("SC_PANORAMA",         "Panorama / Water-Glass Effect"),
    0x20B: ("★ SC_LINEBUF_DLY",    "Line Buffer Delay, init=0x10, CVBS→0x62"),
    0x20C: ("SC_LINEBUF_SZ_H",     "Line Buffer Size High"),
    0x20D: ("★ SC_PCLKO_DIV",      "PCLKO Divider[1:0] + HPol[2] VPol[3]"),
    0x20E: ("SC_LINEBUF_SZ_L",     "Line Buffer Size Low, stock=0x20"),
    0x20F: ("SC_CTRL2",            "Scaler Control 2"),
    0x210: ("★ SC_HDE_START",      "H Display Enable Start position"),
    0x211: ("SC_OUT_W_H",          "Output Width High + misc bits"),
    0x212: ("SC_OUT_W_L",          "Output Width Low (800+1=0x321)"),
    0x213: ("SC_HSYNC_POS",        "HSync Position"),
    0x214: ("SC_HSYNC_WIDTH",      "HSync Pulse Width"),
    0x215: ("★ SC_VDE_START",      "V Display Enable Start position"),
    0x216: ("SC_OUT_H_L",          "Output Height Low (480=0x1E0)"),
    0x217: ("SC_OUT_H_H",          "Output Height High"),
    0x218: ("SC_VSYNC_POS",        "VSync Position"),
    0x219: ("SC_FREERUN_VT_L",     "Freerun V Total Low"),
    0x21A: ("SC_VSYNC_WIDTH",      "VSync Pulse Width"),
    0x21B: ("SC_VDE_MASK",         "VDE Mask (top/bottom blanking)"),
    0x21C: ("★ SC_FREERUN_CTRL",   "Freerun Control (auto/manual mute)"),
    0x21D: ("SC_FREERUN_HT_L",     "Freerun H Total Low"),
    0x21E: ("SC_FIXED_VLINE",      "Fixed VLine enable, stock=0x02"),
    0x220: ("SC_PANORAMA_H",       "Panorama Control High"),
    0x221: ("SC_HSYNC_POS_H",      "HSync Position High bits"),
    # ── Page 2 — TCON ─────────────────────────────────────────────────
    0x240: ("TCON_0",              "TCON Register 0, stock=0x10"),
    0x241: ("TCON_1",              "TCON Register 1"),
    0x242: ("TCON_2",              "TCON Register 2, stock=0x05"),
    0x243: ("TCON_3",              "TCON Register 3, stock=0x01"),
    0x244: ("TCON_4",              "TCON Register 4, stock=0x64"),
    0x245: ("TCON_5",              "TCON Register 5, stock=0xF4"),
    0x246: ("TCON_6",              "TCON Register 6"),
    0x247: ("TCON_7",              "TCON Register 7, stock=0x0A"),
    0x248: ("TCON_8",              "TCON Register 8, stock=0x36"),
    0x249: ("TCON_9",              "TCON Register 9, stock=0x10"),
    0x24A: ("TCON_A",              "TCON Register A"),
    0x24B: ("TCON_B",              "TCON Register B"),
    0x24C: ("TCON_C",              "TCON Register C"),
    0x24D: ("TCON_D",              "TCON Register D, stock=0x44"),
    0x24E: ("TCON_E",              "TCON Register E, stock=0x04"),
    # ── Page 2 — Image Adjustment ─────────────────────────────────────
    0x280: ("★ IA_HUE",            "RGB Hue [5:0], stock=0x20"),
    0x281: ("★ IA_CONTRAST_R",     "Red Contrast, stock=0x80"),
    0x282: ("★ IA_CONTRAST_G",     "Green Contrast, stock=0x80"),
    0x283: ("★ IA_CONTRAST_B",     "Blue Contrast, stock=0x80"),
    0x284: ("★ IA_CONTRAST_Y",     "Y Luminance Contrast, stock=0x80"),
    0x285: ("IA_CONTRAST_CB",      "Cb Chrominance Contrast, stock=0x80"),
    0x286: ("IA_CONTRAST_CR",      "Cr Chrominance Contrast, stock=0x80"),
    0x287: ("★ IA_BRIGHT_R",       "Red Brightness, stock=0x80"),
    0x288: ("★ IA_BRIGHT_G",       "Green Brightness, stock=0x80"),
    0x289: ("★ IA_BRIGHT_B",       "Blue Brightness, stock=0x80"),
    0x28A: ("IA_BRIGHT_Y",         "Y Luminance Brightness, stock=0x80"),
    0x28B: ("★ IA_SHARPNESS",      "Panel Sharpness [3:0], stock=0x40"),
    0x28C: ("IA_CTRL",             "Image Adjust Control"),
    # ── Page 2 — Gamma / Dither ──────────────────────────────────────
    0x2BE: ("IA_CTRL2",            "Image Adjust Control 2 (misc)"),
    0x2BF: ("TEST_PATTERN",        "Test Pattern Control, stock=0x00"),
    0x2E0: ("GAMMA_CTRL0",         "Gamma Control 0"),
    0x2E1: ("GAMMA_CTRL1",         "Gamma Control 1"),
    0x2E2: ("GAMMA_CTRL2",         "Gamma Control 2"),
    0x2E3: ("GAMMA_CTRL3",         "Gamma Control 3"),
    0x2E4: ("DITHER_CTRL",         "Dither Control, stock=0x21"),
    # ── Page 2 — 8-bit Panel Interface ────────────────────────────────
    0x2F8: ("PANEL_8BIT_0",        "8-bit Panel Interface 0"),
    0x2F9: ("PANEL_8BIT_1",        "8-bit Panel Interface 1, stock=0x80"),
    # ── Page 3 — Font OSD ────────────────────────────────────────────
    0x304: ("FOSD_RAM_ACC",        "OSD RAM Auto Access Enable"),
    0x30C: ("FOSD_CTRL",           "Font OSD On/Off, [6]=enable"),
    0x310: ("FOSD_WIN1",           "Font OSD Window 1 Enable"),
    0x320: ("FOSD_WIN2",           "Font OSD Window 2 Enable"),
    0x330: ("FOSD_WIN3",           "Font OSD Window 3 Enable"),
    0x340: ("FOSD_WIN4",           "Font OSD Window 4 Enable"),
    0x350: ("FOSD_WIN5",           "Font OSD Window 5 Enable"),
    0x360: ("FOSD_WIN6",           "Font OSD Window 6 Enable"),
    0x370: ("FOSD_WIN7",           "Font OSD Window 7 Enable"),
    0x380: ("FOSD_WIN8",           "Font OSD Window 8 Enable"),
    # ── Page 4 — Sprite OSD ──────────────────────────────────────────
    0x400: ("SOSD_CTRL",           "Sprite OSD Control"),
    # ── Page 4 — SPI ─────────────────────────────────────────────────
    0x4C0: ("SPI_BASE",            "SPI Base Register"),
    # ── Page 4 — Clock ───────────────────────────────────────────────
    0x4E0: ("CLOCK_CTRL0",         "Clock Control 0"),
    0x4E1: ("CLOCK_CTRL1",         "Clock Control 1"),
}

BINARY_REGIONS = [
    (0x00000, 0x0007F, "vectors",  "Interrupt Vectors"),
    (0x00080, 0x03BFF, "strings",  "String Data"),
    (0x03C00, 0x07FFF, "common",   "Common Code"),
    (0x08000, 0x0FFFF, "bank1",    "Bank 1"),
    (0x10000, 0x17FFF, "bank2",    "Bank 2"),
    (0x18000, 0x1FFFF, "bank3",    "Bank 3"),
    (0x20000, 0x27FFF, "bank4",    "Bank 4"),
    (0x28000, 0x2FFFF, "bank5",    "Bank 5"),
    (0x30000, 0x31FFF, "bank6",    "Bank 6"),
]

KNOWN_DELAYS = {
    0x0F570: "Decoder init — analog decoder startup settle (bank1)",
    0x106C9: "IT6802 HDMI Rx init — long power-up sequence",
    0x10917: "IT6802 HDMI Rx init — PLL lock / link training",
    0x10C8B: "IT6802 HDMI stabilization — signal lock timeout",
    0x10CAD: "IT6802 HDMI stabilization — video detect wait",
    0x10E4E: "IT6802 power-up — longest HDMI init timeout",
    0x11E20: "PLL lock wait — clock synthesizer settling",
    0x11E3C: "PLL lock wait — clock synthesizer settling",
    0x1D25C: "General stabilization — mode switch settle (bank3)",
    0x1D76B: "Stabilization — intermediate delay (bank3)",
    0x1D86C: "System reset recovery — post-reset stabilization",
    0x22CD8: "LLPLL lock — line-locked PLL settling",
    0x22E4F: "Post-PLL stabilization — scaler pipeline flush",
    0x249DA: "Input path setup — pre-AFE configuration wait",
    0x24A1A: "AFE stabilization — analog front-end settling",
    0x24A4E: "AFE stabilization — analog front-end gain settle",
    0x2655F: "Video path stabilization (bank4)",
    0x265B9: "Video path — extended mode change wait (bank4)",
    0x2A173: "Power-up sequence — long startup initialization",
    0x2A3F0: "AFE/GPIO init — analog + GPIO peripheral setup",
    0x2A68C: "AFE/GPIO init — extended peripheral settling",
    0x2A9DB: "LLPLL settle — line-locked PLL fine adjust",
    0x2AA19: "AFE settle — analog front-end fine calibration",
    0x2AD7D: "Inter-stage delay — pipeline stabilization (bank5)",
    0x2C38D: "OSD/System — display overlay engine settle",
    0x2C75C: "ChangeDecoder() — analog decoder stabilization wait",
    0x2CA1D: "Scaler/OSD mode change — full video pipeline flush",
    0x2DBE0: "DTV mode stabilization — digital TV signal lock",
    0x2FFA5: "Output stabilization — final display path settle",
    0x2FFB2: "Output stabilization — final display path settle",
    0x30E3C: "System delay — extended stabilization (bank6)",
    0x31300: "System delay — final initialization settle (bank6)",
}

MCU_SIZE = 0x32000  # 204800 bytes


@dataclass
class DelayCall:
    offset: int
    value_ms: int
    r7_off: int
    r6_off: int
    bank: str
    desc: str = ""
    original_ms: int = 0

    def __post_init__(self):
        if self.original_ms == 0:
            self.original_ms = self.value_ms


@dataclass
class InitRegEntry:
    file_offset: int
    reg: int
    value: int
    original: int = 0

    def __post_init__(self):
        if self.original == 0:
            self.original = self.value


@dataclass
class Mod:
    offset: int
    old: bytes
    new: bytes
    desc: str


# ════════════════════════════════════════════════════════════════════════
# B Board Firmware Analyzer  (TW8836)
# ════════════════════════════════════════════════════════════════════════

class FirmwareAnalyzer:
    def __init__(self):
        self.data: Optional[bytearray] = None
        self.original: Optional[bytes] = None
        self.path: str = ""
        self.is_flash: bool = False
        self.delays: List[DelayCall] = []
        self.init_regs: List[InitRegEntry] = []
        self.mods: List[Mod] = []
        self.init_table_offset: int = -1
        self._flash_data: Optional[bytearray] = None

    def load(self, path: str):
        with open(path, "rb") as f:
            raw = f.read()
        self.path = path
        self.is_flash = len(raw) > MCU_SIZE * 2
        if self.is_flash:
            self.data = bytearray(raw[:MCU_SIZE])
            self._flash_data = bytearray(raw)
        else:
            self.data = bytearray(raw)
            self._flash_data = None
        self.original = bytes(self.data)
        self.delays.clear()
        self.init_regs.clear()
        self.mods.clear()
        self._find_delays()
        self._find_init_table()

    def get_bank(self, off: int) -> str:
        for start, end, name, _ in BINARY_REGIONS:
            if start <= off <= end:
                return name
        return "?"

    def _auto_delay_desc(self, off: int, ms: int) -> str:
        """Generate a description for a delay based on nearby register context."""
        d = self.data
        bank = self.get_bank(off)
        # Scan nearby bytes for register page writes (0x00-0x04 = TW8836 pages)
        subsystems: set[str] = set()
        page_names = {0: "decoder", 1: "LLPLL", 2: "scaler",
                      3: "AFE/DTV", 4: "OSD/system"}
        for j in range(max(0, off - 40), min(len(d), off + 10)):
            if j < len(d) - 2 and d[j] <= 0x04 and d[j+1] < 0x100:
                reg = (d[j] << 8) | d[j+1]
                if reg in REGISTER_INFO:
                    subsystems.add(page_names.get(d[j], "unknown"))
        # Build description from context
        parts = []
        if subsystems:
            parts.append("/".join(sorted(subsystems)))
        if ms >= 3000:
            parts.append("long timeout/init")
        elif ms >= 1000:
            parts.append("stabilization wait")
        elif ms >= 300:
            parts.append("settling delay")
        else:
            parts.append("short settle")
        return f"{' — '.join(parts)} ({bank})"

    def _find_delays(self):
        d = self.data
        n = len(d)
        i = 0x3C00
        while i < n - 5:
            found = False
            if i < n - 6 and d[i] == 0xE4 and d[i+1] == 0x7F and d[i+3] == 0x7E \
               and d[i+5] == 0xFD and d[i+6] == 0xFC:
                r7, r6 = d[i+2], d[i+4]
                ms = r6 * 256 + r7
                if 5 <= ms <= 10000:
                    desc = KNOWN_DELAYS.get(i+1, "")
                    if not desc:
                        desc = self._auto_delay_desc(i+1, ms)
                    self.delays.append(DelayCall(
                        offset=i+1, value_ms=ms, r7_off=i+2, r6_off=i+4,
                        bank=self.get_bank(i), desc=desc))
                    found = True
                    i += 7
            if not found and i < n - 7 and d[i] == 0x7F and d[i+2] == 0x7E \
               and d[i+4] == 0x7D and d[i+5] == 0x00 \
               and d[i+6] == 0x7C and d[i+7] == 0x00:
                r7, r6 = d[i+1], d[i+3]
                ms = r6 * 256 + r7
                if 5 <= ms <= 10000:
                    desc = KNOWN_DELAYS.get(i, "")
                    if not desc:
                        desc = self._auto_delay_desc(i, ms)
                    self.delays.append(DelayCall(
                        offset=i, value_ms=ms, r7_off=i+1, r6_off=i+3,
                        bank=self.get_bank(i), desc=desc))
                    i += 8
                    found = True
            if not found:
                i += 1
        self.delays.sort(key=lambda x: x.offset)

    def _find_init_table(self):
        d = self.data
        sig = bytes([0x00, 0x06, 0x06, 0x00, 0x07])
        pos = d.find(sig, 0x3C00)
        if pos != -1:
            self.init_table_offset = pos
            self._parse_init_table_3byte(pos)
        self._find_sub_tables()

    def _parse_init_table_3byte(self, start: int):
        d = self.data
        i = start
        safety = 0
        while i < min(len(d) - 2, start + 2000) and safety < 700:
            safety += 1
            hi, lo, val = d[i], d[i+1], d[i+2]
            if hi == 0x0F and lo == 0xFF and val == 0xFF:
                break
            if hi > 0x04:
                break
            reg = (hi << 8) | lo
            self.init_regs.append(InitRegEntry(
                file_offset=i + 2, reg=reg, value=val, original=val))
            i += 3

    def _find_sub_tables(self):
        d = self.data
        markers = []
        for i in range(0x3C00, len(d) - 2):
            if d[i] == 0x0F and d[i+1] == 0xFF and d[i+2] == 0xFF:
                markers.append(i)
        main_end = self.init_table_offset + len(self.init_regs) * 3 \
            if self.init_regs else 0
        for end_off in markers:
            if end_off <= main_end:
                continue
            start = end_off - 3
            while start >= max(0, end_off - 300):
                hi = d[start]
                if hi > 0x04:
                    start += 3
                    break
                if start >= 3 and d[start-3] == 0x0F and d[start-2] == 0xFF \
                   and d[start-1] == 0xFF:
                    break
                start -= 3
            if start < end_off:
                self._parse_sub_table(start, end_off)

    def _parse_sub_table(self, start: int, end: int):
        d = self.data
        i = start
        while i < end:
            hi, lo, val = d[i], d[i+1], d[i+2]
            if hi > 0x04:
                break
            reg = (hi << 8) | lo
            if not any(e.file_offset == i + 2 for e in self.init_regs):
                self.init_regs.append(InitRegEntry(
                    file_offset=i + 2, reg=reg, value=val, original=val))
            i += 3

    def set_delay(self, dc: DelayCall, new_ms: int):
        new_ms = max(1, min(9999, new_ms))
        old_r7 = self.data[dc.r7_off]
        old_r6 = self.data[dc.r6_off]
        r7 = new_ms & 0xFF
        r6 = (new_ms >> 8) & 0xFF
        self.data[dc.r7_off] = r7
        self.data[dc.r6_off] = r6
        self.mods.append(Mod(
            dc.r7_off, bytes([old_r7, old_r6]), bytes([r7, r6]),
            f"delay1ms({dc.value_ms}) → delay1ms({new_ms}) @ 0x{dc.offset:05X}"))
        dc.value_ms = new_ms

    def set_init_reg(self, entry: InitRegEntry, new_val: int):
        new_val = new_val & 0xFF
        old = self.data[entry.file_offset]
        self.data[entry.file_offset] = new_val
        name = REGISTER_INFO.get(entry.reg, ("?", ""))[0]
        self.mods.append(Mod(
            entry.file_offset, bytes([old]), bytes([new_val]),
            f"REG{entry.reg:03X} ({name}): 0x{old:02X} → 0x{new_val:02X}"))
        entry.value = new_val

    def set_byte(self, offset: int, new_val: int):
        old = self.data[offset]
        self.data[offset] = new_val & 0xFF
        self.mods.append(Mod(offset, bytes([old]), bytes([new_val & 0xFF]),
                             f"Byte @0x{offset:05X}: 0x{old:02X} → 0x{new_val:02X}"))

    def reset_all(self):
        if self.original:
            self.data = bytearray(self.original)
            self.mods.clear()
            self.delays.clear()
            self.init_regs.clear()
            self._find_delays()
            self._find_init_table()

    def diff_count(self) -> int:
        if not self.original:
            return 0
        return sum(1 for a, b in zip(self.data, self.original) if a != b)

    def save_mcu(self, path: str):
        with open(path, "wb") as f:
            f.write(self.data)

    def save_flash(self, path: str):
        if self._flash_data:
            out = bytearray(self._flash_data)
            out[:MCU_SIZE] = self.data
            with open(path, "wb") as f:
                f.write(out)
        else:
            self.save_mcu(path)

    def sha(self) -> str:
        return hashlib.sha256(self.data).hexdigest()[:16] if self.data else ""

    def sha_orig(self) -> str:
        return hashlib.sha256(self.original).hexdigest()[:16] if self.original else ""

    def hex_dump(self, offset: int, length: int = 256) -> str:
        if not self.data:
            return ""
        lines = []
        end = min(offset + length, len(self.data))
        for addr in range(offset, end, 16):
            row = self.data[addr:addr+16]
            hx = " ".join(f"{b:02X}" for b in row)
            asc = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in row)
            lines.append(f"{addr:05X}  {hx:<48s}  {asc}")
        return "\n".join(lines)


# ╔══════════════════════════════════════════════════════════════════════╗
# ║  040 B BOARD — TW8836 (block-keyed XOR encoded firmware)           ║
# ╚══════════════════════════════════════════════════════════════════════╝

class FirmwareAnalyzer040B:
    """TW8836 B-board firmware for SKY04O Pro (040 series).

    The 040 B firmware uses the same block-keyed XOR scheme as the A board:
        key[i] = (0x55 + i // 512) & 0xFF
    applied from raw file offset 0x210 (A_PAYLOAD_START).
    """

    PAYLOAD_START = 0x210
    BLOCK_SIZE    = 512
    XOR_BASE      = 0x55

    def __init__(self):
        self.raw: Optional[bytearray]  = None   # full encoded file
        self.data: Optional[bytearray] = None   # decoded payload
        self.original: Optional[bytes] = None   # snapshot for reset/diff
        self.path: str = ""
        self.is_flash: bool = True
        self.delays:    List[DelayCall]    = []
        self.init_regs: List[InitRegEntry] = []
        self.mods:      List[Mod]          = []
        self.init_table_offset: int = -1
        self._flash_data = None              # kept for interface compat
        self._mod_offsets: set = set()       # payload offsets written

    # ── Encode / decode helpers ────────────────────────────────────────
    def _key(self, payload_offset: int) -> int:
        return (self.XOR_BASE + payload_offset // self.BLOCK_SIZE) & 0xFF

    def _decode_payload(self, raw_bytes) -> bytearray:
        """Fast block-XOR decode using bytes.translate()."""
        payload_len = len(raw_bytes) - self.PAYLOAD_START
        out = bytearray(payload_len)
        BSIZ = self.BLOCK_SIZE
        for blk in range((payload_len + BSIZ - 1) // BSIZ):
            key = (self.XOR_BASE + blk) & 0xFF
            tbl = bytes(i ^ key for i in range(256))
            s = blk * BSIZ
            e = min(s + BSIZ, payload_len)
            chunk = bytes(raw_bytes[self.PAYLOAD_START + s:
                                    self.PAYLOAD_START + e])
            out[s:e] = chunk.translate(tbl)
        return out

    def _encode_back(self, payload_offset: int):
        """Re-encode one decoded byte back into self.raw."""
        key = self._key(payload_offset)
        self.raw[self.PAYLOAD_START + payload_offset] = \
            self.data[payload_offset] ^ key

    def _write_decoded(self, poff: int, value: int):
        """Write to decoded payload + keep raw in sync + track offset."""
        self.data[poff] = value & 0xFF
        self._encode_back(poff)
        self._mod_offsets.add(poff)

    # ── Load ──────────────────────────────────────────────────────────
    def load(self, path: str):
        with open(path, "rb") as f:
            raw = f.read()
        if len(raw) < self.PAYLOAD_START + 0x1000:
            raise ValueError(f"File too small: {len(raw)} bytes")
        if raw[0] != 0x04:
            raise ValueError(
                f"Not a Skyzone firmware (byte[0]=0x{raw[0]:02X})")
        self.path = path
        self.raw  = bytearray(raw)
        self.data = self._decode_payload(raw)
        self.original = bytes(self.data)
        self._mod_offsets.clear()
        self.delays.clear()
        self.init_regs.clear()
        self.mods.clear()
        self.init_table_offset = -1
        self._find_delays()
        self._find_init_table()

    # ── Bank name (compatibility) ──────────────────────────────────────
    def get_bank(self, off: int) -> str:
        return "MCU"

    def _auto_delay_desc(self, off: int, ms: int) -> str:
        return f"delay {ms}ms"

    # ── Delay pattern search ──────────────────────────────────────────
    def _find_delays(self):
        """Find 8051 delay1ms() patterns using compiled regex (fast on 15MB)."""
        import re
        d = bytes(self.data)
        pat = re.compile(b'\xe4\x7f([\x00-\xff])\x7e([\x00-\xff])\xfd\xfc')
        for m in pat.finditer(d):
            r7 = m.group(1)[0]
            r6 = m.group(2)[0]
            ms = r6 * 256 + r7
            if 5 <= ms <= 30000:
                off = m.start()
                self.delays.append(DelayCall(
                    offset=off + 1, value_ms=ms,
                    r7_off=off + 2, r6_off=off + 4,
                    bank="MCU",
                    desc=KNOWN_DELAYS.get(off + 1, f"delay {ms}ms")))
        self.delays.sort(key=lambda x: x.offset)

    # ── Init table search ─────────────────────────────────────────────
    def _find_init_table(self):
        sig = bytes([0x00, 0x06, 0x06, 0x00, 0x07])
        pos = self.data.find(sig)
        if pos != -1:
            self.init_table_offset = pos
            self._parse_init_table_3byte(pos)

    def _parse_init_table_3byte(self, start: int):
        d = self.data
        i = start
        safety = 0
        while i < min(len(d) - 2, start + 2100) and safety < 750:
            safety += 1
            hi, lo, val = d[i], d[i+1], d[i+2]
            if hi == 0x0F and lo == 0xFF and val == 0xFF:
                break
            if hi > 0x04:
                break
            reg = (hi << 8) | lo
            self.init_regs.append(InitRegEntry(
                file_offset=i + 2, reg=reg, value=val, original=val))
            i += 3

    # ── Setters ───────────────────────────────────────────────────────
    def set_delay(self, dc: DelayCall, new_ms: int):
        new_ms = max(1, min(30000, new_ms))
        old_r7 = self.data[dc.r7_off]
        old_r6 = self.data[dc.r6_off]
        r7 = new_ms & 0xFF
        r6 = (new_ms >> 8) & 0xFF
        self._write_decoded(dc.r7_off, r7)
        self._write_decoded(dc.r6_off, r6)
        self.mods.append(Mod(
            dc.r7_off, bytes([old_r7, old_r6]), bytes([r7, r6]),
            f"delay1ms({dc.value_ms}) → delay1ms({new_ms})"
            f" @ 0x{dc.offset:05X}"))
        dc.value_ms = new_ms

    def set_init_reg(self, entry: InitRegEntry, new_val: int):
        new_val = new_val & 0xFF
        old = self.data[entry.file_offset]
        self._write_decoded(entry.file_offset, new_val)
        name = REGISTER_INFO.get(entry.reg, ("?", ""))[0]
        self.mods.append(Mod(
            entry.file_offset, bytes([old]), bytes([new_val]),
            f"REG{entry.reg:03X} ({name}): 0x{old:02X} → 0x{new_val:02X}"))
        entry.value = new_val

    def set_byte(self, offset: int, new_val: int):
        old = self.data[offset]
        self._write_decoded(offset, new_val)
        self.mods.append(Mod(
            offset, bytes([old]), bytes([new_val & 0xFF]),
            f"Byte @0x{offset:05X}: 0x{old:02X} → 0x{new_val:02X}"))

    # ── Reset ─────────────────────────────────────────────────────────
    def reset_all(self):
        if not self.original:
            return
        # Restore each modified decoded byte and re-encode into raw
        for poff in self._mod_offsets:
            if 0 <= poff < len(self.original):
                self.data[poff] = self.original[poff]
                self._encode_back(poff)
        # Also restore r6_off for delay mods (r6_off = r7_off + 2)
        extra = set()
        for m in self.mods:
            if len(m.old) == 2:       # delay mod: stores [old_r7, old_r6]
                poff2 = m.offset + 2  # r6_off = r7_off + 2
                if poff2 not in self._mod_offsets:
                    extra.add(poff2)
        for poff in extra:
            if 0 <= poff < len(self.original):
                self.data[poff] = self.original[poff]
                self._encode_back(poff)
        self._mod_offsets.clear()
        self.mods.clear()
        self.delays.clear()
        self.init_regs.clear()
        self.init_table_offset = -1
        self._find_delays()
        self._find_init_table()

    # ── Stats ─────────────────────────────────────────────────────────
    def diff_count(self) -> int:
        return len(self._mod_offsets)

    def save_mcu(self, path: str):
        """Save decoded payload (for inspection; raw 15 MB decoded)."""
        with open(path, "wb") as f:
            f.write(self.data)

    def save_flash(self, path: str):
        """Save full re-encoded firmware file."""
        with open(path, "wb") as f:
            f.write(self.raw)

    def sha(self) -> str:
        if not self.mods:
            return self.sha_orig()
        import json
        s = json.dumps([(m.offset, list(m.new)) for m in self.mods])
        return hashlib.sha256(s.encode()).hexdigest()[:16]

    def sha_orig(self) -> str:
        if not self.raw:
            return ""
        return hashlib.sha256(bytes(self.raw[:1024])).hexdigest()[:16]

    def hex_dump(self, offset: int, length: int = 256) -> str:
        if not self.data:
            return ""
        lines = []
        end = min(offset + length, len(self.data))
        orig = self.original
        for addr in range(offset, end, 16):
            row = self.data[addr:addr+16]
            hx_parts = []
            for j, bv in enumerate(row):
                is_mod = (orig is not None and
                          addr+j < len(orig) and bv != orig[addr+j])
                hx_parts.append(f"[{bv:02X}]" if is_mod else f" {bv:02X} ")
            hx = "".join(hx_parts)
            asc = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in row)
            lines.append(f"{addr:05X}  {hx}  {asc}")
        return "\n".join(lines)


# ╔══════════════════════════════════════════════════════════════════════╗
# ║  A BOARD — NXP Kinetis MK22FN256 (ARM Cortex-M4 Display Ctrl)     ║
# ╚══════════════════════════════════════════════════════════════════════╝

# ── A Board firmware format constants ─────────────────────────────────
A_HEADER_SIZE      = 0x200
A_PREAMBLE_SIZE    = 0x10
A_PAYLOAD_START    = 0x210
A_XOR_BASE_KEY     = 0x55
A_XOR_BLOCK_SIZE   = 512
A_FLASH_BASE       = 0xC000
A_MAX_FILE_SIZE    = 300000

# ── Frame period constants (MOVW Rd, #imm16 in microseconds) ──────────
FRAME_PERIOD_STOCK = 10000   # 100 fps  (1 000 000 / 100)
FRAME_PERIOD_120   = 8333    # 120 fps  (1 000 000 / 120)
FRAME_PERIOD_144   = 6944    # ~144 fps (1 000 000 / 144)

# ── Panel OLED Driver Register Database (75 registers) ────────────────
PANEL_REGS: Dict[int, Tuple[str, str]] = {
    0x00: ("NOP",                   "No operation / table padding byte"),
    0x01: ("SOFT_RESET",            "Software reset command"),
    0x02: ("POWER_CTRL",           "Power control / driver output level"),
    0x03: ("DISP_ID",              "Display identification readback"),
    0x04: ("READ_DDB",             "Read display identification"),
    0x05: ("DSI_ERR",              "DSI error status / flags"),
    0x06: ("DISP_FUNC_CTRL",       "Display function control, timing"),
    0x07: ("DISP_CTRL",            "Display control configuration"),
    0x08: ("PANEL_STATE",           "Panel state / configuration data"),
    0x09: ("DISP_STATUS",          "Display status / diagnostic readback"),
    0x0A: ("GET_POWER_STATE",       "Read power state register"),
    0x0B: ("GET_ADDR_MODE",          "Read address mode / scan direction"),
    0x0C: ("GET_PIXEL_FMT",         "Read pixel format register"),
    0x0D: ("GET_POWER_MODE",       "Read power mode / display state"),
    0x0E: ("GET_IMG_MODE",          "Read image display mode"),
    0x0F: ("GET_DIAG_RESULT",        "Read self-diagnostic result / signal mode"),
    0x10: ("SLEEP_IN",             "Enter sleep mode (low power)"),
    0x11: ("SLEEP_OUT",            "Exit sleep mode (wait after write)"),
    0x12: ("PARTIAL_MODE_ON",      "Partial display mode enable"),
    0x14: ("NORMAL_DISP_ON",       "Normal display mode / partial area"),
    0x15: ("PARTIAL_AREA",         "Partial area row start/end"),
    0x16: ("GAMMA_SET_A",          "Gamma correction voltage A"),
    0x17: ("GAMMA_REF_VOLT",       "Gamma reference voltage / gate output"),
    0x18: ("READ_FRAME_CNT",         "Read frame count / display line status"),
    0x19: ("VCOM_CTRL_1",          "VCOM voltage control 1"),
    0x1A: ("VCOM_CTRL_2",          "VCOM voltage control 2"),
    0x1B: ("VCOM_OFFSET",           "VCOM offset / fine trim"),
    0x1C: ("READ_ID4",               "Read display identification 4 (extended ID)"),
    0x1D: ("COL_ADDR_SET",         "Column address set"),
    0x1E: ("PIXEL_FORMAT",         "Pixel format (RGB / interface bits)"),
    0x1F: ("SELF_DIAG",            "Self-diagnostics result readback"),
    0x20: ("DISP_INVERSION",       "Display inversion control"),
    0x21: ("DISP_INV_ON",           "Display inversion on"),
    0x22: ("ALL_PIX_OFF",           "All pixels off command"),
    0x23: ("PANEL_CFG",            "Panel configuration register"),
    0x24: ("PANEL_DRIVE",          "Panel drive strength"),
    0x25: ("CTRL_DISP_WRITE",        "Write control display / CABC enable register"),
    0x26: ("GAMMA_SET",             "Gamma curve selection"),
    0x27: ("CONTRAST_CTRL",        "Contrast / brightness fine adjust"),
    0x28: ("DISP_OFF",              "Display off"),
    0x29: ("DISP_ON",               "Display on command"),
    0x2A: ("COL_ADDR_START",        "Column address start"),
    0x2C: ("MEM_WRITE",             "Memory write start"),
    0x2E: ("MEM_READ",              "Memory read start"),
    0x2F: ("MEM_WRITE_CONT",       "Memory write continue (data stream)"),
    0x30: ("PARTIAL_AREA_DEF",      "Partial area definition"),
    0x31: ("GATE_CTRL",            "Gate driver control"),
    0x32: ("SCROLL_AREA",          "Vertical scroll area definition"),
    0x33: ("SCROLL_START",         "Vertical scroll start address"),
    0x34: ("TEAR_OFF",               "Tear effect line off"),
    0x35: ("TEAR_ON",                "Tear effect line on / mode select"),
    0x36: ("MEM_ACCESS_CTRL",      "Memory access / scan direction (MADCTL)"),
    0x37: ("VSCROLL_ADDR",          "Vertical scroll start address"),
    0x38: ("IDLE_OFF",              "Idle mode off"),
    0x39: ("IDLE_ON",               "Idle mode on"),
    0x3C: ("DISP_TIMING",          "Display timing / blanking control"),
    0x40: ("GATE_SCAN_START",      "Gate scan start position / voltage"),
    0x42: ("MEM_WRITE_CONT",         "Memory write continue / stream data"),
    0x43: ("GATE_CTRL_EXT",         "Gate control extended"),
    0x44: ("SET_TEAR_LINE",         "Set tear effect scanline"),
    0x45: ("GET_SCANLINE",         "Read current scan line position"),
    0x46: ("SET_SCROLL_START",       "Set vertical scroll start address"),
    0x47: ("GET_BRIGHTNESS",       "Read current brightness level"),
    0x48: ("READ_CTRL_DISPLAY",      "Read control display register / CABC state"),
    0x49: ("SRC_DRV_VOLTAGE",      "Source driver voltage adjust"),
    0x4B: ("SRC_DRV_CTRL",          "Source driver control"),
    0x4D: ("SRC_DRV_BIAS",          "Source driver bias level"),
    0x4E: ("SRC_DRV_FINE",          "Source driver fine adjust"),
    0x4F: ("PANEL_TIMING",          "Panel timing configuration"),
    0x50: ("CABC_CTRL",             "Content adaptive brightness control enable"),
    0x51: ("WRITE_BRIGHTNESS",      "Write display brightness value"),
    0x52: ("READ_BRIGHTNESS",       "Read display brightness value"),
    0x53: ("WRITE_CTRL_DISPLAY",    "Write CTRL_DISPLAY register (backlight/CABC)"),
    0x55: ("INTERFACE_PIXEL",      "Interface pixel mode select"),
    0x56: ("ADAPTIVE_CTRL",        "Adaptive brightness / CABC control"),
    0x57: ("CABC_MIN_BRIGHT",        "CABC minimum brightness level"),
    0x58: ("CABC_MIN_BRIGHT_EXT",    "CABC minimum brightness extended / still-image"),
    0x60: ("GAMMA_CH_0",           "Gamma channel 0 — voltage level"),
    0x61: ("GAMMA_CH_1",           "Gamma channel 1 — voltage level"),
    0x62: ("GAMMA_CH_2",           "Gamma channel 2 — voltage level"),
    0x63: ("GAMMA_CH_3",           "Gamma channel 3 — voltage level"),
    0x64: ("GAMMA_CH_4",           "Gamma channel 4 — voltage level"),
    0x66: ("GAMMA_CH_6",           "Gamma channel 6 — voltage level"),
    0x67: ("GAMMA_CH_7",           "Gamma channel 7 — voltage level"),
    0x68: ("GAMMA_CH_8",           "Gamma channel 8 — voltage level"),
    0x6A: ("GAMMA_CH_A",            "Gamma channel A — voltage level"),
    0x6B: ("GAMMA_CH_B",           "Gamma channel B — voltage level"),
    0x6C: ("FRAME_RATE_A",         "Frame rate control A"),
    0x6D: ("FRAME_RATE_B",         "Frame rate control B"),
    0x6E: ("BRIGHTNESS",           "Panel brightness / greyscale level"),
    0x6F: ("FRAME_RATE_C",          "Frame rate control C"),
    0x70: ("GATE_DRV_CTRL",         "Gate driver control / scan mode"),
    0x72: ("GATE_OUT_EXT",         "Gate output extended / gamma trim"),
    0x73: ("READ_ID_EXT",            "Read extended identification / register bank"),
    0x74: ("SRC_VOLTAGE_A",         "Source output voltage level A"),
    0x75: ("CONTRAST",             "Contrast control value"),
    0x76: ("SRC_OUT_LEVEL",        "Source output level trim"),
    0x78: ("SRC_TIMING",           "Source timing control"),
    0x79: ("SRC_TIMING_D",          "Source timing control D"),
    0x7A: ("SRC_TIMING_B",         "Source timing control B"),
    0x7B: ("SRC_TIMING_C",         "Source timing control C"),
    0x7D: ("SRC_CLK_A",             "Source clock adjustment A"),
    0x7E: ("SRC_CLK_B",             "Source clock adjustment B"),
    0x80: ("GATE_OUT_0",           "Gate output level 0"),
    0x81: ("GATE_OUT_1",           "Gate output level 1"),
    0x82: ("GATE_OUT_2",           "Gate output level 2"),
    0x83: ("GATE_OUT_3",           "Gate output level 3"),
    0x84: ("GATE_OUT_4",           "Gate output level 4"),
    0x85: ("GATE_OUT_5",           "Gate output level 5"),
    0x87: ("GATE_OUT_7",           "Gate output level 7"),
    0x88: ("GATE_TIMING_A",        "Gate timing control A"),
    0x8B: ("GATE_TIMING_D",        "Gate timing control D"),
    0x8D: ("GATE_TIMING_E",         "Gate timing control E"),
    0x8E: ("GATE_TIMING_B",        "Gate timing control B"),
    0x8F: ("GATE_TIMING_C",        "Gate timing control C"),
    0x90: ("DSC_CTRL",              "Display stream compression control"),
    0x91: ("DSC_MODE",              "Display stream compression mode / bits-per-pixel"),
    0x93: ("SRC_CLK_CTRL",         "Source clock control"),
    0x95: ("DSC_PARAMS_A",           "Display stream compression parameters A"),
    0x96: ("POWER_GATE_A",          "Power / gate combined control A"),
    0x97: ("DSC_PARAMS_B",           "Display stream compression parameters B"),
    0x98: ("POWER_CFG_A",          "Power configuration A"),
    0x99: ("POWER_CFG_B",          "Power configuration B"),
    0x9A: ("POWER_CFG_C",          "Power configuration C"),
    0x9B: ("DSC_PARAMS_C",           "Display stream compression parameters C"),
    0x9C: ("POWER_TIMING",         "Power timing / sequencing"),
    0x9D: ("POWER_DRV_FINE",        "Power driver fine adjust"),
    0x9E: ("POWER_AMP",             "Power amplifier control"),
    0x9F: ("POWER_CFG_D",           "Power configuration D"),
    0xA0: ("CHECKSUM_A",             "Image data checksum A / first checksum"),
    0xA1: ("CHECKSUM_B",             "Image data checksum B / continue checksum"),
    0xA2: ("POWER_DRV_A",          "Power driver strength A"),
    0xA3: ("POWER_DRV_B",          "Power driver strength B"),
    0xA4: ("POWER_DRV_C",           "Power driver strength C"),
    0xA5: ("POWER_DRV_D",           "Power driver strength D"),
    0xA6: ("POWER_LEVEL_A",        "Power voltage level A"),
    0xA7: ("POWER_LEVEL_B",        "Power voltage level B"),
    0xA8: ("POWER_LEVEL_C",        "Power voltage level C / ref"),
    0xA9: ("POWER_LEVEL_D",        "Power voltage level D"),
    0xAB: ("POWER_REF_A",          "Power reference level A"),
    0xAC: ("POWER_REF_B",          "Power reference level B"),
    0xAD: ("FREE_RUN_CTRL",        "Free-running / scan mode control"),
    0xAE: ("DEEP_STANDBY",          "Deep standby mode control"),
    0xB0: ("POWER_SEQ_A",          "Power sequence control A"),
    0xB1: ("POWER_SEQ_B",          "Power sequence control B"),
    0xB3: ("INTF_TIMING",          "Interface timing control"),
    0xB4: ("DISP_CTRL_EXT",          "Display control extended / backlight PWM mode"),
    0xB5: ("BLANKING_CTRL",         "Blanking porch control"),
    0xBA: ("VCOM_DRV",              "VCOM driving strength"),
    0xBB: ("VCOM_LEVEL",           "VCOM DC level adjust"),
    0xBC: ("VCOM_DRV_EXT",          "VCOM driving extended control"),
    0xBF: ("POWER_EXT",            "Power control extended"),
    0xC0: ("MFR_PWR_CTRL_1",       "Manufacturer power control 1"),
    0xC2: ("MFR_PWR_CTRL_2",       "Manufacturer power control 2"),
    0xC3: ("MFR_PWR_CTRL_3",       "Manufacturer power control 3"),
    0xC4: ("MFR_PWR_CTRL_4",       "Manufacturer power control 4"),
    0xC5: ("MFR_PWR_CTRL_5",       "Manufacturer power control 5"),
    0xC6: ("MFR_PWR_CTRL_6",       "Manufacturer power control 6"),
    0xC7: ("MFR_PWR_CTRL_7",        "Manufacturer power control 7"),
    0xC8: ("MFR_GAMMA_A",          "Manufacturer gamma control A"),
    0xC9: ("MFR_GAMMA_B",          "Manufacturer gamma control B"),
    0xCA: ("MFR_GAMMA_C",          "Manufacturer gamma control C"),
    0xCB: ("MFR_GAMMA_D",          "Manufacturer gamma control D"),
    0xCC: ("MFR_GAMMA_E",          "Manufacturer gamma control E"),
    0xCD: ("MFR_GAMMA_F",          "Manufacturer gamma control F"),
    0xCE: ("MFR_DRV_CTRL_A",       "Manufacturer driver control A"),
    0xCF: ("MFR_DRV_CTRL_B",       "Manufacturer driver control B"),
    0xD0: ("MFR_POWER_A",          "Manufacturer power setting A"),
    0xD1: ("MFR_POWER_B",          "Manufacturer power setting B"),
    0xD3: ("MFR_POWER_C",          "Manufacturer power setting C"),
    0xD4: ("MFR_POWER_D",          "Manufacturer power setting D"),
    0xD8: ("MFR_POWER_E",           "Manufacturer power setting E"),
    0xD9: ("MFR_NVM_CTRL",          "Manufacturer NVM / status control"),
    0xDA: ("MFR_DEVICE_ID",         "Manufacturer device ID readback"),
    0xDB: ("MFR_VCOM_A",           "Manufacturer VCOM adjust A"),
    0xDD: ("MFR_VCOM_B",           "Manufacturer VCOM adjust B"),
    0xDE: ("MFR_VCOM_C",           "Manufacturer VCOM adjust C"),
    0xE0: ("POS_GAMMA",            "Positive gamma correction"),
    0xE1: ("POS_GAMMA_EXT",        "Positive gamma correction extended"),
    0xE2: ("POS_GAMMA_FINE",       "Positive gamma fine adjust"),
    0xE3: ("NEG_GAMMA",            "Negative gamma correction curve"),
    0xE4: ("NEG_GAMMA_EXT",        "Negative gamma correction extended"),
    0xE5: ("NEG_GAMMA_FINE",       "Negative gamma fine adjust"),
    0xE6: ("GAMMA_DRV_CTRL",       "Gamma driver control"),
    0xE7: ("GAMMA_CTRL",           "Gamma curve control"),
    0xE8: ("DRIVER_TIMING_A",      "Driver timing control A"),
    0xE9: ("DRIVER_TIMING_B",      "Driver timing control B / user adj"),
    0xEA: ("POWER_ON_SEQ",         "Power-on sequence control"),
    0xEB: ("POWER_ON_SEQ_EXT",     "Power-on sequence extended"),
    0xEC: ("MFR_TIMING_A",         "Manufacturer timing control A"),
    0xED: ("MFR_TIMING_B",         "Manufacturer timing control B"),
    0xEE: ("MFR_TIMING_C",         "Manufacturer timing control C"),
    0xEF: ("MFR_TIMING_D",         "Manufacturer timing control D"),
    0xF0: ("MFR_CMD_SET_A",        "Manufacturer command set enable A"),
    0xF1: ("MFR_CMD_SET_B",        "Manufacturer command set enable B"),
    0xF2: ("MFR_TEST_B",             "Manufacturer test / calibration command B"),
    0xF3: ("MFR_SEQ_CTRL",         "Manufacturer sequence control"),
    0xF4: ("INTERFACE_CTRL",       "Interface / format control"),
    0xF5: ("MFR_PUMP_A",           "Manufacturer charge pump A"),
    0xF6: ("MFR_PUMP_B",           "Manufacturer charge pump B"),
    0xF7: ("CMD_ACCESS",           "Command access protection"),
    0xF8: ("SPI_MFR_ACCESS",       "SPI manufacturer access / key"),
    0xF9: ("PUMP_RATIO",           "Charge pump ratio control"),
    0xFA: ("PUMP_CTRL",            "Charge pump control"),
    0xFB: ("MFR_TEST_CMD",         "Manufacturer test command"),
    0xFC: ("OSC_CTRL",             "Internal oscillator control"),
    0xFD: ("MFR_KEY",              "Manufacturer command key"),
    0xFE: ("CMD_SET_SELECT",       "Command set select / page switch"),
    0xFF: ("BANK_SELECT",          "Register bank select"),
}

# ── LT9211 Register Database (106 registers) ─────────────────────────
LT9211_REGS: Dict[int, Tuple[str, str]] = {
    0x8100: ("CHIP_ID_0",           "Chip ID byte 0, expect 0x18"),
    0x8101: ("CHIP_ID_1",           "Chip ID byte 1, expect 0x01"),
    0x8102: ("CHIP_ID_2",           "Chip ID byte 2, expect 0xE3"),
    0x810A: ("CLK_DIV_RST",         "Clock divider reset/release"),
    0x810B: ("CLK_DIST_EN",         "Clock distribution enable, 0xFE=on"),
    0x8120: ("MASTER_RESET",        "Master reset / clock gate control"),
    0x816B: ("CLK_ENABLE",          "Clock enable, 0xFF=all on"),
    0x8201: ("SYS_CTRL",            "System control"),
    0x8202: ("RX_PHY_0",            "RX PHY config 0"),
    0x8204: ("RX_PHY_1",            "RX PHY config 1"),
    0x8205: ("RX_PHY_2",            "RX PHY config 2"),
    0x8207: ("RX_PHY_3",            "RX PHY config 3"),
    0x8208: ("RX_PHY_4",            "RX PHY config 4"),
    0x8209: ("RX_DN_DP_SWAP",       "DSI DN/DP swap (ORR 0xF8 to enable)"),
    0x8217: ("RX_PHY_5",            "RX PHY config 5"),
    0x822D: ("DESSC_PLL_REF",       "DeSSC PLL reference (0x48=25 MHz XTal)"),
    0x8235: ("DESSC_PLL_DIV",       "DeSSC PLL divider — clock range select"),
    0x8236: ("TX_PLL_PD",           "TX PLL power down (0x01=reset)"),
    0x8237: ("TX_PLL_CFG0",         "TX PLL config 0"),
    0x8238: ("TX_PLL_CFG1",         "TX PLL config 1"),
    0x8239: ("TX_PLL_CFG2",         "TX PLL config 2"),
    0x823A: ("TX_PLL_CFG3",         "TX PLL config 3"),
    0x823B: ("LVDS_CFG",            "LVDS config (BIT7=dual-port)"),
    0x823E: ("TX_PHY_0",            "TX PHY register 0"),
    0x823F: ("TX_PHY_1",            "TX PHY register 1"),
    0x8240: ("TX_PHY_2",            "TX PHY register 2"),
    0x8243: ("TX_PHY_3",            "TX PHY register 3"),
    0x8244: ("TX_PHY_4",            "TX PHY register 4"),
    0x8245: ("TX_PHY_5",            "TX PHY register 5"),
    0x8249: ("TX_PHY_6",            "TX PHY register 6"),
    0x824A: ("TX_PHY_7",            "TX PHY register 7"),
    0x824E: ("TX_PHY_8",            "TX PHY register 8"),
    0x824F: ("TX_PHY_9",            "TX PHY register 9"),
    0x8250: ("TX_PHY_10",           "TX PHY register 10"),
    0x8253: ("TX_PHY_11",           "TX PHY register 11"),
    0x8254: ("TX_PHY_12",           "TX PHY register 12"),
    0x8262: ("DPI_OUT_EN",          "DPI output enable/disable"),
    0x8559: ("LVDS_FORMAT",         "LVDS: BIT7=JEIDA, BIT5=DE, BIT4=24bpp"),
    0x855A: ("LVDS_MAP_0",          "LVDS data mapping 0"),
    0x855B: ("LVDS_MAP_1",          "LVDS data mapping 1"),
    0x855C: ("LVDS_DUAL_LINK",      "Dual-link enable (BIT0)"),
    0x8588: ("IO_MODE",             "BIT6=MIPI-RX, BIT4=LVDS-TX"),
    0x85A1: ("DIG_CFG",             "LVDS digital config"),
    0x8600: ("BYTECLK_MEAS",        "ByteClock measurement trigger"),
    0x8606: ("RX_MEAS_CFG0",        "RX measurement config 0"),
    0x8607: ("RX_MEAS_CFG1",        "RX measurement config 1"),
    0x8608: ("BYTECLK_19_16",       "ByteClock [19:16] readback"),
    0x8609: ("BYTECLK_15_8",        "ByteClock [15:8] readback"),
    0x860A: ("BYTECLK_7_0",         "ByteClock [7:0] readback"),
    0x8630: ("RX_DIG_MODE",         "RX digital mode"),
    0x8633: ("RX_PHY_DIG",          "RX PHY digital"),
    0x8640: ("LVDS_OUT_CFG0",       "LVDS output config 0"),
    0x8641: ("LVDS_OUT_CFG1",       "LVDS output config 1"),
    0x8642: ("LVDS_OUT_CFG2",       "LVDS output config 2"),
    0x8643: ("LVDS_OUT_CFG3",       "LVDS output config 3 (swing)"),
    0x8644: ("LVDS_OUT_CFG4",       "LVDS output config 4 (pre-emphasis)"),
    0x8645: ("LVDS_OUT_CFG5",       "LVDS output config 5 (termination)"),
    0x8646: ("LVDS_CH_ORDER",       "Ch order (0x10=A:B, 0x40=swap)"),
    0x8713: ("TX_PLL_RST",          "TX PLL reset/release"),
    0x8714: ("TX_PLL_TMR0",         "TX PLL timer 0"),
    0x8715: ("TX_PLL_TMR1",         "TX PLL timer 1"),
    0x8718: ("TX_PLL_TMR2",         "TX PLL timer 2"),
    0x8722: ("TX_PLL_TMR3",         "TX PLL timer 3"),
    0x8723: ("TX_PLL_TMR4",         "TX PLL timer 4"),
    0x8726: ("TX_PLL_TMR5",         "TX PLL timer 5"),
    0x8737: ("TX_PLL_FINE",         "TX PLL fine-tune"),
    0x871F: ("TX_PLL_LOCK_A",       "TX PLL lock status A (bit 7)"),
    0x8720: ("TX_PLL_LOCK_B",       "TX PLL lock status B (bit 7)"),
    0xD000: ("DSI_LANE_CNT",        "DSI lane count"),
    0xD002: ("DSI_CFG",             "DSI config"),
    0xD00D: ("VTOTAL_H",            "Vertical total high byte"),
    0xD00E: ("VTOTAL_L",            "Vertical total low byte"),
    0xD00F: ("VACTIVE_H",           "Vertical active high byte"),
    0xD010: ("VACTIVE_L",           "Vertical active low byte"),
    0xD011: ("HTOTAL_H",            "Horizontal total high byte"),
    0xD012: ("HTOTAL_L",            "Horizontal total low byte"),
    0xD013: ("HACTIVE_H",           "Horizontal active high byte"),
    0xD014: ("HACTIVE_L",           "Horizontal active low byte"),
    0xD015: ("VS_WIDTH",            "VSync width"),
    0xD016: ("HS_WIDTH",            "HSync width"),
    0xD017: ("VFP_H",               "V front porch high"),
    0xD018: ("VFP_L",               "V front porch low"),
    0xD019: ("HFP_H",               "H front porch high"),
    0xD01A: ("HFP_L",               "H front porch low"),
    0xD023: ("PCR_CFG0",            "Pixel Clock Recovery config 0"),
    0xD026: ("PCR_CFG1",            "PCR config 1"),
    0xD027: ("PCR_CFG2",            "PCR config 2"),
    0xD02D: ("PCR_CFG3",            "PCR config 3"),
    0xD031: ("PCR_CFG4",            "PCR config 4"),
    0xD038: ("PCR_FLT0",            "PCR filter 0"),
    0xD039: ("PCR_FLT1",            "PCR filter 1"),
    0xD03A: ("PCR_FLT2",            "PCR filter 2"),
    0xD03B: ("PCR_FLT3",            "PCR filter 3"),
    0xD03F: ("PCR_FLT4",            "PCR filter 4"),
    0xD040: ("PCR_FLT5",            "PCR filter 5"),
    0xD041: ("PCR_FLT6",            "PCR filter 6"),
    0xD082: ("AUTO_WIDTH_H",        "Auto-detected width high"),
    0xD083: ("AUTO_WIDTH_L",        "Auto-detected width low"),
    0xD084: ("AUTO_FMT",            "Auto format (0x3=YUV, 0xA=RGB)"),
    0xD085: ("AUTO_HEIGHT_H",       "Auto-detected height high"),
    0xD086: ("AUTO_HEIGHT_L",       "Auto-detected height low"),
    0xD087: ("PCR_LOCK",            "PCR lock status (bit 3)"),
    0xD404: ("OUT_CFG_A",           "Output config A"),
    0xD405: ("OUT_CFG_B",           "Output config B"),
    0xD420: ("LVDS_OUT_FMT",        "LVDS output format"),
    0xD421: ("LVDS_OUT_POL",        "LVDS output polarity/mapping"),
}

# ── IT6802 HDMI Receiver Register Database (26 registers) ─────────────
IT6802_REGS: Dict[int, Tuple[str, str]] = {
    0x13: ("CONTRAST_LO",          "Contrast coarse / CSC control"),
    0x14: ("CONTRAST_HI",          "Contrast fine"),
    0x15: ("BRIGHTNESS",           "Brightness offset (signed 8-bit)"),
    0x84: ("HUE",                  "Hue angle (bank 2)"),
    0x8A: ("SATURATION",           "Color saturation gain (bank 2/8)"),
    0x8B: ("SHARPNESS",            "Edge enhancement / sharpness"),
    0x04: ("SYS_CTRL",             "System control"),
    0x05: ("SYS_STATUS",           "System status (read-only)"),
    0x06: ("INT_CTRL",             "Interrupt control"),
    0x10: ("HDMI_STATUS",          "HDMI link status"),
    0xFF: ("BANK_SEL",             "Register bank select"),
    0x9A: ("H_TOTAL_H",            "Horizontal total high"),
    0x9B: ("H_TOTAL_L",            "Horizontal total low"),
    0x9C: ("H_ACTIVE_H",           "Horizontal active high"),
    0x9D: ("H_ACTIVE_L",           "Horizontal active low"),
    0x9E: ("V_TOTAL_H",            "Vertical total high"),
    0x9F: ("V_TOTAL_L",            "Vertical total low"),
    0xA0: ("V_ACTIVE_H",           "Vertical active high"),
    0xA1: ("V_ACTIVE_L",           "Vertical active low"),
    0xA8: ("VID_MODE",             "Interlace/Progressive detect"),
    0x58: ("PLL_CTL_0",            "PLL config 0"),
    0x59: ("PLL_CTL_1",            "PLL config 1"),
    0x5A: ("PLL_CTL_2",            "PLL config 2"),
    0x60: ("TMDS_CLK_0",           "TMDS clock byte 0"),
    0x61: ("TMDS_CLK_1",           "TMDS clock byte 1"),
    0x62: ("TMDS_CLK_2",           "TMDS clock byte 2"),
}

# ── NXP Kinetis MK22F Peripheral Register Database (66 registers) ────
MK22F_PERIPH: Dict[int, Tuple[str, str]] = {
    0x40047000: ("SIM_SOPT1",        "System Options 1"),
    0x40048004: ("SIM_SOPT2",        "System Options 2 (clock src select)"),
    0x40048034: ("SIM_SCGC4",        "Clock Gate 4 (UART/I2C/SPI)"),
    0x40048038: ("SIM_SCGC5",        "Clock Gate 5 (PORT A-E)"),
    0x4004803C: ("SIM_SCGC6",        "Clock Gate 6 (FTM/PIT/ADC/DAC)"),
    0x40048040: ("SIM_SCGC7",        "Clock Gate 7 (DMA/FlexBus)"),
    0x40048044: ("SIM_CLKDIV1",      "Clock Divider 1 (Core/Bus/Flash)"),
    0x40048048: ("SIM_CLKDIV2",      "Clock Divider 2 (USB)"),
    0x4004804C: ("SIM_FCFG1",        "Flash Config 1"),
    0x40048050: ("SIM_FCFG2",        "Flash Config 2"),
    0x40064000: ("MCG_C1",           "MCG Control 1 (CLKS, FRDIV)"),
    0x40064001: ("MCG_C2",           "MCG Control 2 (RANGE, HGO)"),
    0x40064004: ("MCG_C5",           "MCG Control 5 (PRDIV)"),
    0x40064005: ("MCG_C6",           "MCG Control 6 (VDIV, PLLS)"),
    0x40064006: ("MCG_S",            "MCG Status (CLKST, LOCK)"),
    0x40065000: ("OSC_CR",           "Oscillator Control (ERCLKEN)"),
    0x40052000: ("WDOG_STCTRLH",     "Watchdog Status/Control High"),
    0x4005200E: ("WDOG_UNLOCK",      "Watchdog Unlock (0xC520, 0xD928)"),
    0x40066000: ("I2C0_A1",          "I2C0 Address 1"),
    0x40066001: ("I2C0_F",           "I2C0 Frequency Divider"),
    0x40066002: ("I2C0_C1",          "I2C0 Control 1"),
    0x40066003: ("I2C0_S",           "I2C0 Status"),
    0x40066004: ("I2C0_D",           "I2C0 Data"),
    0x40067000: ("I2C1_A1",          "I2C1 Address 1"),
    0x40067001: ("I2C1_F",           "I2C1 Frequency Divider"),
    0x40067002: ("I2C1_C1",          "I2C1 Control 1"),
    0x4002C000: ("SPI0_MCR",         "SPI0 Module Config"),
    0x4002C00C: ("SPI0_CTAR0",       "SPI0 Clock/Transfer Attr 0"),
    0x4002C034: ("SPI0_PUSHR",       "SPI0 PUSH TX FIFO"),
    0x4006A000: ("UART0_BDH",        "UART0 Baud Rate High"),
    0x4006A001: ("UART0_BDL",        "UART0 Baud Rate Low"),
    0x4006A002: ("UART0_C1",         "UART0 Control 1"),
    0x4006A003: ("UART0_C2",         "UART0 Control 2"),
    0x4006A007: ("UART0_D",          "UART0 Data"),
    0x40038000: ("FTM0_SC",          "FTM0 Status/Control (CLKS, PS)"),
    0x40038008: ("FTM0_MOD",         "FTM0 Modulo (period)"),
    0x40039000: ("FTM1_SC",          "FTM1 Status/Control"),
    0x4003A000: ("FTM2_SC",          "FTM2 Status/Control"),
    0x40037000: ("PIT_MCR",          "PIT Module Control"),
    0x40037100: ("PIT0_LDVAL",       "PIT0 Load Value (period)"),
    0x40037108: ("PIT0_TCTRL",       "PIT0 Timer Control"),
    0x40037110: ("PIT1_LDVAL",       "PIT1 Load Value"),
    0x4003B000: ("ADC0_SC1A",        "ADC0 Status/Control 1A"),
    0x4003B008: ("ADC0_CFG1",        "ADC0 Config 1 (ADLPC, MODE)"),
    0x4003B00C: ("ADC0_CFG2",        "ADC0 Config 2"),
    0x40049000: ("PORTA_PCR0",       "Port A Pin 0 Control"),
    0x4004A000: ("PORTB_PCR0",       "Port B Pin 0 Control"),
    0x4004B000: ("PORTC_PCR0",       "Port C Pin 0 Control"),
    0x4004C000: ("PORTD_PCR0",       "Port D Pin 0 Control"),
    0x4004D000: ("PORTE_PCR0",       "Port E Pin 0 Control"),
    0x400FF000: ("GPIOA_PDOR",       "GPIO A Port Data Output"),
    0x400FF014: ("GPIOA_PDDR",       "GPIO A Port Data Direction"),
    0x400FF040: ("GPIOB_PDOR",       "GPIO B Port Data Output"),
    0x400FF080: ("GPIOC_PDOR",       "GPIO C Port Data Output"),
    0x400FF0C0: ("GPIOD_PDOR",       "GPIO D Port Data Output"),
    0x400FF100: ("GPIOE_PDOR",       "GPIO E Port Data Output"),
    0x4007D000: ("PMC_LVDSC1",       "Low Voltage Detect Status/Ctrl 1"),
    0x4007D002: ("PMC_REGSC",        "Regulator Status/Control"),
    0x4007F000: ("RCM_SRS0",         "System Reset Status 0"),
    0x40020000: ("FTFA_FSTAT",       "Flash Status"),
    0x40020002: ("FTFA_FSEC",        "Flash Security"),
    0x40020003: ("FTFA_FOPT",        "Flash Option"),
    0xE000E010: ("SYST_CSR",         "SysTick Control/Status"),
    0xE000E014: ("SYST_RVR",         "SysTick Reload Value"),
    0xE000ED08: ("SCB_VTOR",         "Vector Table Offset"),
    0xE000ED0C: ("SCB_AIRCR",        "App Interrupt/Reset Control"),
}


# ── A Board Data Classes ─────────────────────────────────────────────

@dataclass
class PanelInitEntry:
    file_offset: int
    payload_offset: int
    reg: int
    value: int
    original: int
    table_id: str
    group_idx: int
    pair_pos: int

    @property
    def reg_name(self) -> str:
        return PANEL_REGS[self.reg][0] if self.reg in PANEL_REGS \
            else f"PANEL_0x{self.reg:02X}"

    @property
    def reg_desc(self) -> str:
        return PANEL_REGS[self.reg][1] if self.reg in PANEL_REGS \
            else f"Panel driver register 0x{self.reg:02X}"


@dataclass
class StringEntry:
    file_offset: int
    payload_offset: int
    text: str
    section: str


@dataclass
class ModA:
    file_offset: int
    old: int
    new: int
    desc: str


@dataclass
class FrameTimingSite:
    payload_offset: int
    register: int        # ARM Rd (0-15)
    value_us: int        # current period in µs
    original_us: int     # stock period in µs

    @property
    def fps(self) -> float:
        return 1_000_000 / self.value_us if self.value_us > 0 else 0.0

    @property
    def file_offset(self) -> int:
        return self.payload_offset + A_PAYLOAD_START


# ════════════════════════════════════════════════════════════════════════
# A Board Firmware Analyzer  (MK22F)
# ════════════════════════════════════════════════════════════════════════

class FirmwareAnalyzerA:

    def __init__(self):
        self.raw: Optional[bytearray] = None
        self.decoded: Optional[bytearray] = None
        self.original_raw: Optional[bytes] = None
        self.path: str = ""
        self.file_size: int = 0
        self.payload_size: int = 0
        self.code_section_len: int = 0
        self.panel_entries: List[PanelInitEntry] = []
        self.strings: List[StringEntry] = []
        self.mods: List[ModA] = []
        self.frame_timing_sites: List[FrameTimingSite] = []
        self.sp_init: int = 0
        self.reset_vector: int = 0
        self.build_date: str = ""
        self.fw_version: str = ""
        self.device_type: str = "X4Pro"  # 'X4Pro' or '040'

    # ── Address Conversions ────────────────────────────────────────────
    def payload_to_file(self, poff: int) -> int:
        return poff + A_PAYLOAD_START

    def file_to_payload(self, foff: int) -> int:
        return foff - A_PAYLOAD_START

    def runtime_to_payload(self, raddr: int) -> int:
        return raddr - A_FLASH_BASE

    def payload_to_runtime(self, poff: int) -> int:
        return poff + A_FLASH_BASE

    # ── Load ──────────────────────────────────────────────────────────
    def load(self, path: str):
        with open(path, "rb") as f:
            self.raw = bytearray(f.read())
        self.path = path
        self.file_size = len(self.raw)
        self.original_raw = bytes(self.raw)
        self.payload_size = self.file_size - A_PAYLOAD_START

        if self.file_size < A_PAYLOAD_START + 0x200:
            raise ValueError(f"File too small: {self.file_size} bytes")
        if self.file_size > A_MAX_FILE_SIZE:
            raise ValueError(f"File too large: {self.file_size} bytes")
        if self.raw[0] != 0x04 or self.raw[3] not in (0x00, 0x02):
            raise ValueError(f"Not a Skyzone A firmware: "
                             f"header={self.raw[:6].hex()}")
        # byte[1]: 0x01 = SKY04X Pro, 0x10 = SKY04O Pro (040)
        self.device_type = "040" if self.raw[1] == 0x10 else "X4Pro"

        # ── Block-keyed XOR decode ───────────────────────────────────
        # Key schedule: key[i] = (0x55 + floor(i / 512)) & 0xFF
        self.decoded = bytearray(self.payload_size)
        for i in range(self.payload_size):
            key = (A_XOR_BASE_KEY + i // A_XOR_BLOCK_SIZE) & 0xFF
            self.decoded[i] = self.raw[A_PAYLOAD_START + i] ^ key

        self._detect_code_boundary()   # uses decoded data

        self.sp_init = struct.unpack_from('<I', self.decoded, 0)[0]
        self.reset_vector = struct.unpack_from('<I', self.decoded, 4)[0]

        self.panel_entries.clear()
        self.strings.clear()
        self.mods.clear()
        self.frame_timing_sites.clear()
        self._find_strings()
        self._find_frame_timing_sites()
        self._find_panel_init_tables()

    def _detect_code_boundary(self):
        """Find where ARM code ends and string data begins."""
        markers = [b'VSTATE', b'ASTATE', b'HDCP', b'SyncWait',
                   b'ColorDetect', b'WaitForReady']
        first_hit = len(self.decoded)
        for m in markers:
            pos = self.decoded.find(m)
            if 0 <= pos < first_hit:
                first_hit = pos
        if first_hit < len(self.decoded):
            self.code_section_len = (first_hit // 256) * 256
        else:
            self.code_section_len = len(self.decoded)

    def _find_strings(self):
        self.strings.clear()
        current = ""
        start = 0
        for i in range(self.payload_size):
            b = self.decoded[i]
            if 32 <= b < 127:
                if not current:
                    start = i
                current += chr(b)
            else:
                if len(current) >= 6:
                    sec = "data" if start >= self.code_section_len else "code"
                    self.strings.append(StringEntry(
                        self.payload_to_file(start), start, current, sec))
                current = ""
        if len(current) >= 6:
            sec = "data" if start >= self.code_section_len else "code"
            self.strings.append(StringEntry(
                self.payload_to_file(start), start, current, sec))
        self.build_date = ""
        for s in self.strings:
            for m in ("Jan", "Feb", "Mar", "Apr", "May", "Jun",
                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"):
                if m in s.text and "20" in s.text:
                    self.build_date = s.text.strip()
                    return

    # ── MOVW Thumb-2 helpers ────────────────────────────────────────
    @staticmethod
    def _decode_movw(hw1: int, hw2: int) -> Tuple[int, int]:
        """Decode MOVW from two halfwords → (Rd, imm16)."""
        i_bit = (hw1 >> 10) & 1
        imm4 = hw1 & 0xF
        imm3 = (hw2 >> 12) & 0x7
        rd = (hw2 >> 8) & 0xF
        imm8 = hw2 & 0xFF
        imm16 = (imm4 << 12) | (i_bit << 11) | (imm3 << 8) | imm8
        return rd, imm16

    @staticmethod
    def _encode_movw(rd: int, imm16: int) -> Tuple[int, int]:
        """Encode MOVW Rd, #imm16 → (hw1, hw2)."""
        imm4 = (imm16 >> 12) & 0xF
        i_bit = (imm16 >> 11) & 1
        imm3 = (imm16 >> 8) & 0x7
        imm8 = imm16 & 0xFF
        hw1 = 0xF240 | (i_bit << 10) | imm4
        hw2 = (imm3 << 12) | (rd << 8) | imm8
        return hw1, hw2

    def _find_frame_timing_sites(self):
        """Scan for MOVW Rd, #N where N is a known frame period value."""
        self.frame_timing_sites.clear()
        known_periods = {FRAME_PERIOD_STOCK, FRAME_PERIOD_120,
                         FRAME_PERIOD_144}
        for i in range(0, len(self.decoded) - 3, 2):
            hw1 = self.decoded[i] | (self.decoded[i + 1] << 8)
            hw2 = self.decoded[i + 2] | (self.decoded[i + 3] << 8)
            if (hw1 & 0xFBF0) != 0xF240:
                continue
            rd, imm16 = self._decode_movw(hw1, hw2)
            if imm16 in known_periods:
                self.frame_timing_sites.append(
                    FrameTimingSite(i, rd, imm16, imm16))

    # ── Panel Init Table Scanner ─────────────────────────────────────
    def _find_panel_init_tables(self):
        """Dynamically locate panel init data tables in the decoded payload.

        The panel init function writes {reg, val} pairs to OLED panels
        via I2C (device 0x4C = left eye, 0x4D = right eye).  The function
        uses a loop that reads from a data table in flash via:

            LDRH  R2, [Rn, #0]   ; read halfword from table
            ADDS  R3, Rn, #2     ; pointer to next halfword
            MOVS  R1, #0x4D      ; I2C device address
            MOVS  R0, #0         ; bus 0
            BL    sub_18330       ; I2C write
            ADDS  Rn, #4         ; advance table pointer

        We find this pattern, then trace backward to the function
        prologue to extract table addresses from LDR Rx,[PC,#imm]
        instructions in the literal pool.
        """
        self.panel_entries.clear()

        # ── Step 1: find LDRH Rx,[Rn]; ADDS R3,Rn,#2; MOVS R1,#0x4D;
        #            MOVS R0,#0  in the decoded payload
        i2c_pat = bytes([0x4D, 0x21, 0x00, 0x20])
        hits = []                             # (ldrh_poff, table_reg_Rn)
        for i in range(4, len(self.decoded) - 8):
            if self.decoded[i:i + 4] != i2c_pat:
                continue
            # preceding 4 bytes: LDRH R2,[Rn,#0] + ADDS R3,Rn,#2
            b_ldrh_hi = self.decoded[i - 3]   # high byte of LDRH
            b_adds_hi = self.decoded[i - 1]   # high byte of ADDS
            if b_ldrh_hi == 0x88 and b_adds_hi == 0x1C:
                rn = (self.decoded[i - 4] >> 3) & 0x07
                rt = self.decoded[i - 4] & 0x07
                if rt == 2:                   # LDRH R2, [Rn]
                    hits.append((i - 4, rn))

        if not hits:
            return

        # ── Step 2: for each hit find the enclosing function (PUSH) ──
        best_func = None
        best_ldr_count = 0

        seen_funcs: set = set()
        for ldrh_off, rn in hits:
            # walk backward for PUSH {…, LR}  (B5xx)
            push_off = None
            for delta in range(2, 500, 2):
                check = ldrh_off - delta
                if check < 0:
                    break
                if self.decoded[check + 1] == 0xB5:
                    push_off = check
                    break
            if push_off is None or push_off in seen_funcs:
                continue
            seen_funcs.add(push_off)

            # Scan function body for LDR Rx,[PC,#imm] instructions
            scan_end = min(ldrh_off + 160, len(self.decoded) - 2)
            ldr_pcs = []
            for j in range(push_off, scan_end, 2):
                b1 = self.decoded[j + 1]
                if (b1 & 0xF8) != 0x48:
                    continue
                rd = b1 & 0x07
                imm8 = self.decoded[j]
                pc_val = (j + A_FLASH_BASE + 4) & ~3
                pool_addr = pc_val + imm8 * 4
                pool_poff = pool_addr - A_FLASH_BASE
                if 0 <= pool_poff + 3 < self.payload_size:
                    pool_val = struct.unpack_from(
                        '<I', self.decoded, pool_poff)[0]
                    if A_FLASH_BASE <= pool_val < A_FLASH_BASE + self.payload_size:
                        ldr_pcs.append((j, rd, pool_val))

            flash_count = sum(1 for _, rd2, _ in ldr_pcs if rd2 == rn)
            # Prefer callee-saved registers (R4-R7) because they
            # survive across BL calls, meaning this is a LOOP over a
            # data table rather than individual writes.
            score = flash_count + (1000 if rn >= 4 else 0)
            if score > best_ldr_count:
                best_ldr_count = score
                best_func = {
                    'push_off': push_off,
                    'table_reg': rn,
                    'ldr_pcs': ldr_pcs,
                    'scan_end': scan_end,
                }

        if best_func is None:
            return

        # ── Step 3: extract table addresses & counts ─────────────────
        treg = best_func['table_reg']
        push_off = best_func['push_off']
        scan_end = best_func['scan_end']

        # Unique table runtime addresses (loaded into the table register)
        table_addrs = []
        seen_addrs: set = set()
        for _, rd, pv in best_func['ldr_pcs']:
            if rd == treg and pv not in seen_addrs:
                seen_addrs.add(pv)
                table_addrs.append(pv)

        if not table_addrs:
            # fallback: any flash LDR
            for _, rd, pv in best_func['ldr_pcs']:
                if pv not in seen_addrs:
                    seen_addrs.add(pv)
                    table_addrs.append(pv)

        # MOVS Rx, #imm  where Rx is NOT R0/R1 (bus/addr) and imm 15..100
        count_for_addr: dict = {}
        movs_list = []
        for j in range(push_off, scan_end, 2):
            b0 = self.decoded[j]
            b1 = self.decoded[j + 1]
            if (b1 & 0xF8) == 0x20:
                rd = b1 & 0x07
                imm = b0
                if rd not in (0, 1) and 15 <= imm <= 100:
                    movs_list.append((j, rd, imm))

        # Pair each table address with closest count MOVS
        for addr in table_addrs:
            ldr_off = next(
                j for j, rd, pv in best_func['ldr_pcs'] if pv == addr)
            best_dist = 999
            best_count = 65           # default
            for mj, _, mcount in movs_list:
                d = abs(mj - ldr_off)
                if d < best_dist:
                    best_dist = d
                    best_count = mcount
            count_for_addr[addr] = best_count

        # Deduplicate overlapping tables  (V4.1.6 style)
        table_addrs.sort()
        merged: list = []
        for addr in table_addrs:
            if merged:
                prev_addr, prev_count = merged[-1]
                prev_end = prev_addr + prev_count * 4
                if addr < prev_end:
                    # overlapping → extend previous
                    new_end = max(prev_end, addr + count_for_addr[addr] * 4)
                    merged[-1] = (prev_addr, (new_end - prev_addr) // 4)
                    continue
            merged.append((addr, count_for_addr[addr]))

        # Check for a hidden B table in the gap between two tables
        if len(merged) >= 2:
            extras = []
            for ti in range(len(merged) - 1):
                a1, c1 = merged[ti]
                a2, _c2 = merged[ti + 1]
                gap = a2 - (a1 + c1 * 4)
                if gap > 0 and gap % 4 == 0:
                    gap_count = gap // 4
                    if 5 <= gap_count <= 50:
                        extras.append((a1 + c1 * 4, gap_count))
            merged.extend(extras)
            merged.sort(key=lambda t: t[0])

        # ── Step 4: populate panel entries ───────────────────────────
        table_labels = "ABCDEFGH"
        for ti, (addr, count) in enumerate(merged):
            poff = addr - A_FLASH_BASE
            tid = table_labels[ti] if ti < len(table_labels) else str(ti)
            for grp in range(count):
                grp_off = poff + grp * 4
                if grp_off + 3 >= self.payload_size:
                    break
                for pair in range(2):
                    byte_off = grp_off + pair * 2
                    reg = self.decoded[byte_off]
                    val = self.decoded[byte_off + 1]
                    foff = self.payload_to_file(byte_off)
                    self.panel_entries.append(PanelInitEntry(
                        file_offset=foff,
                        payload_offset=byte_off,
                        reg=reg,
                        value=val,
                        original=val,
                        table_id=tid,
                        group_idx=grp,
                        pair_pos=pair,
                    ))

    def set_frame_period(self, site: FrameTimingSite, new_us: int):
        """Patch a MOVW site to a new frame period (µs)."""
        hw1, hw2 = self._encode_movw(site.register, new_us)
        b = struct.pack('<HH', hw1, hw2)
        for j in range(4):
            old_desc = (f"Frame period R{site.register}: "
                        f"{site.value_us}µs→{new_us}µs "
                        f"({site.value_us and 1_000_000//site.value_us or 0}"
                        f"→{1_000_000//new_us if new_us else 0} fps)")
            self.set_decoded_byte(site.payload_offset + j, b[j],
                                  old_desc if j == 0 else "")
        site.value_us = new_us

    def set_panel_value(self, entry: PanelInitEntry, new_val: int):
        new_val &= 0xFF
        old = entry.value
        if old == new_val:
            return
        entry.value = new_val
        val_poff = entry.payload_offset + 1
        self.decoded[val_poff] = new_val
        foff = self.payload_to_file(val_poff)
        key = (A_XOR_BASE_KEY + val_poff // A_XOR_BLOCK_SIZE) & 0xFF
        self.raw[foff] = new_val ^ key
        self.mods.append(ModA(foff, old, new_val,
            f"Panel {entry.table_id} {entry.reg_name} "
            f"(0x{entry.reg:02X}): 0x{old:02X} → 0x{new_val:02X}"))

    def set_decoded_byte(self, payload_offset: int, new_val: int, desc: str = ""):
        new_val &= 0xFF
        old = self.decoded[payload_offset]
        if old == new_val:
            return
        self.decoded[payload_offset] = new_val
        foff = self.payload_to_file(payload_offset)
        key = (A_XOR_BASE_KEY + payload_offset // A_XOR_BLOCK_SIZE) & 0xFF
        self.raw[foff] = new_val ^ key
        self.mods.append(ModA(foff, old, new_val,
            desc or f"@payload 0x{payload_offset:05X}: "
                    f"0x{old:02X} → 0x{new_val:02X}"))

    def reset_all(self):
        if self.original_raw:
            self.raw = bytearray(self.original_raw)
            self.load(self.path)

    def diff_count(self) -> int:
        if not self.original_raw:
            return 0
        return sum(1 for a, b in zip(self.raw, self.original_raw) if a != b)

    def save(self, path: str):
        with open(path, "wb") as f:
            f.write(self.raw)

    def sha(self) -> str:
        return hashlib.sha256(self.raw).hexdigest()[:16] if self.raw else ""

    def sha_orig(self) -> str:
        return hashlib.sha256(self.original_raw).hexdigest()[:16] \
            if self.original_raw else ""

    def hex_dump(self, payload_offset: int, length: int = 512,
                 raw_view: bool = False) -> str:
        if raw_view:
            data = self.raw
            base = A_PAYLOAD_START + payload_offset
        else:
            data = self.decoded
            base = payload_offset
        lines = []
        end = min(base + length, len(data))
        for addr in range(base, end, 16):
            row = data[addr:addr + 16]
            hx = " ".join(f"{b:02X}" for b in row)
            asc = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in row)
            prefix = f"F:{addr:05X}" if raw_view else f"P:{addr:05X}"
            lines.append(f"{prefix}  {hx:<48s}  {asc}")
        return "\n".join(lines)

    def panel_coverage(self) -> Tuple[int, int, float]:
        total = len(self.panel_entries)
        named = sum(1 for e in self.panel_entries if e.reg in PANEL_REGS)
        pct = named * 100.0 / total if total else 0
        return named, total, pct


# ╔══════════════════════════════════════════════════════════════════════╗
# ║  UNIFIED GUI APPLICATION                                           ║
# ╚══════════════════════════════════════════════════════════════════════╝

class App:
    def __init__(self, a_path: str = "", b_path: str = "",
                 b040_path: str = ""):
        self.fw_b    = FirmwareAnalyzer()
        self.fw_a    = FirmwareAnalyzerA()
        self.fw_040b = FirmwareAnalyzer040B()
        self.root = tk.Tk()
        self.root.title(f"Skyzone SKY04X / SKY04O Pro Firmware Editor  v{VERSION}")
        self.root.geometry("1400x920")
        self.root.minsize(1000, 650)
        self.root.configure(bg=C["bg"])

        self._setup_fonts()
        self._setup_styles()
        self._build_menu()
        self._build_ui()
        self._build_status()

        if b_path and os.path.isfile(b_path):
            self._load_b(b_path)
        if a_path and os.path.isfile(a_path):
            self._load_a(a_path)
        if b040_path and os.path.isfile(b040_path):
            self._load_b040(b040_path)

    # ══════════════════════════════════════════════════════════════════
    # Fonts / Styles
    # ══════════════════════════════════════════════════════════════════
    def _setup_fonts(self):
        self.font_ui      = tkfont.Font(family="Segoe UI", size=10)
        self.font_ui_bold = tkfont.Font(family="Segoe UI", size=10, weight="bold")
        self.font_header  = tkfont.Font(family="Segoe UI", size=13, weight="bold")
        self.font_mono    = tkfont.Font(family="Consolas", size=10)
        self.font_mono_sm = tkfont.Font(family="Consolas", size=9)
        self.font_big     = tkfont.Font(family="Segoe UI", size=11)
        self.font_board   = tkfont.Font(family="Segoe UI", size=11, weight="bold")

    def _setup_styles(self):
        s = ttk.Style()
        s.theme_use("clam")
        s.configure(".", background=C["bg"], foreground=C["fg"],
                     fieldbackground=C["bg3"], bordercolor=C["border"],
                     darkcolor=C["bg"], lightcolor=C["bg2"],
                     troughcolor=C["bg3"], selectbackground=C["accent"],
                     selectforeground="#000", insertcolor=C["fg"])
        s.configure("TNotebook", background=C["bg"], borderwidth=0, padding=0)
        s.configure("TNotebook.Tab", background=C["bg2"], foreground=C["dim"],
                     padding=[14, 7], font=self.font_ui_bold)
        s.map("TNotebook.Tab",
              background=[("selected", C["bg3"])],
              foreground=[("selected", C["accent"])])
        s.configure("TFrame", background=C["bg"])
        s.configure("Card.TFrame", background=C["bg2"])
        s.configure("TLabel", background=C["bg"], foreground=C["fg"],
                     font=self.font_ui)
        s.configure("H.TLabel", foreground=C["accent"], font=self.font_header)
        s.configure("Dim.TLabel", foreground=C["dim"])
        s.configure("Warn.TLabel", foreground=C["warn"])
        s.configure("Ok.TLabel", foreground=C["ok"])
        s.configure("Mod.TLabel", foreground=C["mod"])
        s.configure("Err.TLabel", foreground=C["err"])
        s.configure("TButton", background=C["bg3"], foreground=C["fg"],
                     padding=[10, 5], font=self.font_ui)
        s.map("TButton", background=[("active", C["sel"])])
        s.configure("Accent.TButton", background=C["accent"],
                     foreground="#000")
        s.map("Accent.TButton", background=[("active", C["accent2"])])
        s.configure("TSpinbox", fieldbackground=C["bg3"], foreground=C["fg"],
                     arrowcolor=C["fg"])
        s.configure("TScale", background=C["bg"], troughcolor=C["bg3"],
                     sliderthickness=16)
        s.configure("Horizontal.TScale", sliderlength=20)
        s.configure("TScrollbar", background=C["bg3"], troughcolor=C["bg"],
                     arrowcolor=C["fg"])
        s.configure("Treeview", background=C["bg2"], foreground=C["fg"],
                     fieldbackground=C["bg2"], borderwidth=0, rowheight=26,
                     font=self.font_mono_sm)
        s.configure("Treeview.Heading", background=C["bg3"],
                     foreground=C["accent"], font=self.font_ui_bold)
        s.map("Treeview", background=[("selected", C["sel"])],
              foreground=[("selected", C["fg"])])
        s.configure("TSeparator", background=C["border"])
        s.configure("TLabelframe", background=C["bg"], foreground=C["accent"])
        s.configure("TLabelframe.Label", background=C["bg"],
                     foreground=C["accent"], font=self.font_ui_bold)

    # ══════════════════════════════════════════════════════════════════
    # Menu
    # ══════════════════════════════════════════════════════════════════
    def _build_menu(self):
        mb = tk.Menu(self.root, bg=C["bg2"], fg=C["fg"],
                     activebackground=C["accent"], activeforeground="#000",
                     borderwidth=0, font=self.font_ui)
        # File
        fm = tk.Menu(mb, tearoff=0, bg=C["bg2"], fg=C["fg"],
                     activebackground=C["accent"], activeforeground="#000",
                     font=self.font_ui)
        fm.add_command(label="  Open B Board MCU…",   command=self._open_b_mcu)
        fm.add_command(label="  Open B Board Flash…", command=self._open_b_flash)
        fm.add_command(label="  Open A Board FW…",    command=self._open_a_fw)
        fm.add_command(label="  Open 040 B Board Flash…", command=self._open_b040_flash)
        fm.add_separator()
        fm.add_command(label="  Save B Patched MCU…",   command=self._save_b_mcu)
        fm.add_command(label="  Save B Patched Flash…", command=self._save_b_flash)
        fm.add_command(label="  Save A Patched FW…",    command=self._save_a)
        fm.add_command(label="  Save 040 B Patched Flash…", command=self._save_b040_flash)
        fm.add_separator()
        fm.add_command(label="  Export B Patch Log…",    command=self._export_b_log)
        fm.add_command(label="  Export A Patch Log…",    command=self._export_a_log)
        fm.add_command(label="  Export 040 B Patch Log…", command=self._export_b040_log)
        fm.add_separator()
        fm.add_command(label="  Exit", command=self.root.quit)
        mb.add_cascade(label=" File ", menu=fm)
        # Presets
        pm = tk.Menu(mb, tearoff=0, bg=C["bg2"], fg=C["fg"],
                     activebackground=C["accent"], activeforeground="#000",
                     font=self.font_ui)
        pm.add_command(label="  Reset B Board",
                       command=self._reset_b)
        pm.add_command(label="  Reset A Board",
                       command=self._reset_a)
        pm.add_command(label="  Reset 040 B Board",
                       command=self._reset_b040)
        pm.add_separator()
        pm.add_command(label="  B: Conservative Delays (0.5×)",
                       command=lambda: self._preset_b_delays(0.5))
        pm.add_command(label="  B: Aggressive Delays (0.1×)",
                       command=lambda: self._preset_b_delays(0.1))
        pm.add_command(label="  B: High Sharpness",
                       command=self._preset_b_sharp)
        pm.add_separator()
        pm.add_command(label="  040 B: Conservative Delays (0.5×)",
                       command=lambda: self._preset_b040_delays(0.5))
        pm.add_command(label="  040 B: Aggressive Delays (0.1×)",
                       command=lambda: self._preset_b040_delays(0.1))
        mb.add_cascade(label=" Presets ", menu=pm)
        self.root.config(menu=mb)
        self.root.bind("<Control-s>", lambda e: self._save_current())

    # ══════════════════════════════════════════════════════════════════
    # Main UI — Top-level board tabs
    # ══════════════════════════════════════════════════════════════════
    def _build_ui(self):
        self.board_nb = ttk.Notebook(self.root)
        self.board_nb.pack(fill="both", expand=True, padx=6, pady=(6, 0))

        # ── B Board ──────────────────────────────────────────────────
        b_frame = ttk.Frame(self.board_nb)
        self.board_nb.add(b_frame, text="  ⚡ B Board — TW8836  ")
        self.b_nb = ttk.Notebook(b_frame)
        self.b_nb.pack(fill="both", expand=True)

        for attr, title in [
            ("_b_tab_overview", "  Overview  "),
            ("_b_tab_delays",   "  ⏱ Delays  "),
            ("_b_tab_imgq",     "  🎨 Image Quality  "),
            ("_b_tab_regs",     "  📋 Registers  "),
            ("_b_tab_hex",      "  💾 Hex View  "),
            ("_b_tab_patch",    "  📦 Patch Output  "),
        ]:
            f = ttk.Frame(self.b_nb)
            setattr(self, attr, f)
            self.b_nb.add(f, text=title)

        self._build_b_overview()
        self._build_b_delays()
        self._build_b_imgq()
        self._build_b_regs()
        self._build_b_hex()
        self._build_b_patch()

        # ── A Board ──────────────────────────────────────────────────
        a_frame = ttk.Frame(self.board_nb)
        self.board_nb.add(a_frame, text="  ⚡ A Board — MK22F  ")
        self.a_nb = ttk.Notebook(a_frame)
        self.a_nb.pack(fill="both", expand=True)

        for attr, title in [
            ("_a_tab_overview", "  Overview  "),
            ("_a_tab_timing",   "  ⏱️ Frame Timing  "),
            ("_a_tab_panel",    "  🔧 Panel Init  "),
            ("_a_tab_regref",   "  📖 Register Reference  "),
            ("_a_tab_strings",  "  📝 Strings  "),
            ("_a_tab_hex",      "  💾 Hex View  "),
            ("_a_tab_patch",    "  📦 Patch Output  "),
        ]:
            f = ttk.Frame(self.a_nb)
            setattr(self, attr, f)
            self.a_nb.add(f, text=title)

        self._build_a_overview()
        self._build_a_timing()
        self._build_a_panel()
        self._build_a_regref()
        self._build_a_strings()
        self._build_a_hex()
        self._build_a_patch()

        # ── 040 B Board ───────────────────────────────────────────────
        b040_frame = ttk.Frame(self.board_nb)
        self.board_nb.add(b040_frame, text="  🆕 040 B Board — TW8836  ")
        self.b040_nb = ttk.Notebook(b040_frame)
        self.b040_nb.pack(fill="both", expand=True)

        for attr, title in [
            ("_b040_tab_overview", "  Overview  "),
            ("_b040_tab_delays",   "  ⏱ Delays  "),
            ("_b040_tab_imgq",     "  🎨 Image Quality  "),
            ("_b040_tab_regs",     "  📋 Registers  "),
            ("_b040_tab_hex",      "  💾 Hex View  "),
            ("_b040_tab_patch",    "  📦 Patch Output  "),
        ]:
            f = ttk.Frame(self.b040_nb)
            setattr(self, attr, f)
            self.b040_nb.add(f, text=title)

        self._build_b040_overview()
        self._build_b040_delays()
        self._build_b040_imgq()
        self._build_b040_regs()
        self._build_b040_hex()
        self._build_b040_patch()

        self.board_nb.bind("<<NotebookTabChanged>>",
                           lambda e: self._update_status())

    # ══════════════════════════════════════════════════════════════════
    # Status Bar
    # ══════════════════════════════════════════════════════════════════
    def _build_status(self):
        sf = tk.Frame(self.root, bg=C["bg2"], height=28)
        sf.pack(fill="x", side="bottom")
        sf.pack_propagate(False)
        self.status_lbl = tk.Label(sf, text="No firmware loaded", bg=C["bg2"],
                                   fg=C["dim"], font=self.font_mono_sm,
                                   anchor="w", padx=10)
        self.status_lbl.pack(side="left", fill="x", expand=True)
        self.status_mods = tk.Label(sf, text="", bg=C["bg2"], fg=C["mod"],
                                    font=self.font_mono_sm, anchor="e",
                                    padx=10)
        self.status_mods.pack(side="right")

    def _update_status(self):
        try:
            sel = self.board_nb.index(self.board_nb.select())
        except Exception:
            sel = 0
        if sel == 0:  # B Board
            fw = self.fw_b
            if fw.data:
                name = Path(fw.path).name
                dc = fw.diff_count()
                self.status_lbl.config(
                    text=f"B Board: {name}  |  {len(fw.data):,} bytes  "
                         f"|  SHA: {fw.sha()}  "
                         f"|  {len(fw.delays)} delays  "
                         f"|  {len(fw.init_regs)} regs",
                    fg=C["fg"])
                self.status_mods.config(
                    text=f"✏️ {dc} bytes modified" if dc else "No changes",
                    fg=C["mod"] if dc else C["dim"])
            else:
                self.status_lbl.config(text="B Board: No file loaded",
                                       fg=C["dim"])
                self.status_mods.config(text="")
        elif sel == 1:  # A Board
            fw = self.fw_a
            if fw.raw:
                name = Path(fw.path).name
                dc = fw.diff_count()
                n, t, pct = fw.panel_coverage()
                self.status_lbl.config(
                    text=f"A Board: {name}  |  {fw.file_size:,} bytes  "
                         f"|  SHA: {fw.sha()}  "
                         f"|  Panel: {n}/{t} = {pct:.0f}%",
                    fg=C["fg"])
                self.status_mods.config(
                    text=f"✏️ {dc} bytes modified" if dc else "No changes",
                    fg=C["mod"] if dc else C["dim"])
            else:
                self.status_lbl.config(text="A Board: No file loaded",
                                       fg=C["dim"])
                self.status_mods.config(text="")
        elif sel == 2:  # 040 B Board
            fw = self.fw_040b
            if fw.data:
                name = Path(fw.path).name
                dc = fw.diff_count()
                self.status_lbl.config(
                    text=f"040 B Board: {name}  |  {len(fw.data):,} bytes  "
                         f"|  {len(fw.delays)} delays  "
                         f"|  {len(fw.init_regs)} regs",
                    fg=C["fg"])
                self.status_mods.config(
                    text=f"✏️ {dc} bytes modified" if dc else "No changes",
                    fg=C["mod"] if dc else C["dim"])
            else:
                self.status_lbl.config(text="040 B Board: No file loaded",
                                       fg=C["dim"])
                self.status_mods.config(text="")

    # ══════════════════════════════════════════════════════════════════
    #  B BOARD TABS
    # ══════════════════════════════════════════════════════════════════

    # ── B: Overview ───────────────────────────────────────────────────
    def _build_b_overview(self):
        f = self._b_tab_overview
        ttk.Label(f, text="TW8836 (B Board) — Video Processor",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        ttk.Label(f, text="8051 MCU: analog decoder, scaler, OSD, SPI flash",
                  style="Dim.TLabel").pack(padx=16, anchor="w")
        ttk.Separator(f).pack(fill="x", padx=16, pady=10)
        self.b_ov_text = tk.Text(f, bg=C["bg2"], fg=C["fg"],
                                  font=self.font_mono, relief="flat",
                                  padx=16, pady=12, insertbackground=C["fg"],
                                  state="disabled", highlightthickness=1,
                                  highlightbackground=C["border"])
        self.b_ov_text.pack(fill="both", expand=True, padx=8, pady=8)
        bf = ttk.Frame(f)
        bf.pack(padx=16, pady=8, anchor="w")
        ttk.Button(bf, text="Open MCU Binary…",
                   command=self._open_b_mcu).pack(side="left", padx=(0, 8))

    def _refresh_b_overview(self):
        t = self.b_ov_text
        t.config(state="normal")
        t.delete("1.0", "end")
        fw = self.fw_b
        if not fw.data:
            t.insert("end", "  No file loaded.\n\n"
                     "  Use File → Open B Board MCU to load a TW8836 binary.")
            t.config(state="disabled")
            return
        lines = [
            f"  File:            {fw.path}",
            f"  Type:            {'SPI Flash (MCU extracted)' if fw.is_flash else 'MCU Binary'}",
            f"  MCU Size:        {len(fw.data):,} bytes (0x{len(fw.data):X})",
        ]
        if fw._flash_data:
            lines.append(f"  Flash Size:      {len(fw._flash_data):,} bytes")
        lines += [
            f"  SHA256 (orig):   {fw.sha_orig()}",
            f"  SHA256 (current):{fw.sha()}",
            "",
            f"  ── Analysis ─────────────────────────────────────────────",
            f"  delay1ms() calls found:    {len(fw.delays)}",
            f"  Init register entries:     {len(fw.init_regs)}",
        ]
        if fw.init_table_offset >= 0:
            lines.append(
                f"  Init table offset:         0x{fw.init_table_offset:05X}")
        lines += [
            f"  Modifications:             {fw.diff_count()} bytes changed",
            "",
            f"  ── Binary Layout ────────────────────────────────────────",
        ]
        for start, end, name, desc in BINARY_REGIONS:
            if end < len(fw.data):
                sz = end - start + 1
                lines.append(
                    f"  0x{start:05X}-0x{end:05X}  {sz:6,}B  {desc}")
        if fw.delays:
            lines += ["", f"  ── Delay Summary ────────────────────────────────────────"]
            dc = Counter(d.value_ms for d in fw.delays)
            for ms, cnt in sorted(dc.items(), reverse=True):
                lines.append(f"  delay1ms({ms:>5}):  {cnt:>3} call site(s)")
        t.insert("end", "\n".join(lines))
        t.config(state="disabled")

    # ── B: Delays ─────────────────────────────────────────────────────
    def _build_b_delays(self):
        f = self._b_tab_delays
        ttk.Label(f, text="Timing Delays — delay1ms() Call Sites",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        ttk.Label(f, text="Modify stabilization/settling delays. "
                  "Lower = faster switching, but may cause instability.",
                  style="Dim.TLabel").pack(padx=16, anchor="w")
        # Filter bar
        fb = ttk.Frame(f)
        fb.pack(fill="x", padx=16, pady=8)
        ttk.Label(fb, text="Filter:").pack(side="left")
        self.b_delay_filter = tk.StringVar(value="all")
        for txt, val in [("All", "all"), ("≥100ms", "100"),
                         ("≥300ms", "300"), ("Modified", "mod")]:
            ttk.Radiobutton(fb, text=txt, variable=self.b_delay_filter,
                            value=val, command=self._refresh_b_delays
                            ).pack(side="left", padx=6)
        ttk.Button(fb, text="Halve All ≥100ms",
                   command=lambda: self._bulk_b_delay(0.5)
                   ).pack(side="right", padx=4)
        ttk.Button(fb, text="Reset Delays",
                   command=self._reset_b_delays).pack(side="right", padx=4)
        # Scrollable delay list
        self._b_delay_scroll = ScrollFrame(f)
        self._b_delay_scroll.pack(fill="both", expand=True,
                                   padx=16, pady=(0, 8))
        self._b_delay_widgets: List[tk.Widget] = []

    def _refresh_b_delays(self):
        for w in self._b_delay_widgets:
            w.destroy()
        self._b_delay_widgets.clear()
        parent = self._b_delay_scroll.inner

        if not self.fw_b.data:
            lbl = ttk.Label(parent, text="No firmware loaded",
                            style="Dim.TLabel")
            lbl.pack(pady=20)
            self._b_delay_widgets.append(lbl)
            self._b_delay_scroll.bind_scroll()
            return

        filt = self.b_delay_filter.get()
        # Header
        hdr = ttk.Frame(parent)
        hdr.pack(fill="x", pady=(0, 4))
        for txt, w in [("Offset", 9), ("Bank", 7), ("Value(ms)", 10),
                       ("Adjust", 28), ("", 8), ("Description", 40)]:
            ttk.Label(hdr, text=txt, style="Dim.TLabel", width=w,
                      font=self.font_ui_bold).pack(side="left", padx=2)
        self._b_delay_widgets.append(hdr)

        for dc in self.fw_b.delays:
            if filt == "100" and dc.value_ms < 100:
                continue
            if filt == "300" and dc.value_ms < 300:
                continue
            if filt == "mod" and dc.value_ms == dc.original_ms:
                continue
            row = ttk.Frame(parent)
            row.pack(fill="x", pady=1)
            self._b_delay_widgets.append(row)
            is_mod = dc.value_ms != dc.original_ms
            style = "Mod.TLabel" if is_mod else "TLabel"
            ttk.Label(row, text=f"0x{dc.offset:05X}",
                      font=self.font_mono_sm, width=9,
                      style=style).pack(side="left", padx=2)
            ttk.Label(row, text=dc.bank, font=self.font_mono_sm, width=7,
                      style="Dim.TLabel").pack(side="left", padx=2)
            var = tk.IntVar(value=dc.value_ms)
            ttk.Spinbox(row, from_=1, to=9999, width=7, textvariable=var,
                        font=self.font_mono_sm).pack(side="left", padx=2)
            ttk.Scale(row, from_=1, to=max(1000, dc.original_ms * 2),
                      orient="horizontal", length=200, variable=var
                      ).pack(side="left", padx=4)

            def _apply(d=dc, v=var):
                self.fw_b.set_delay(d, v.get())
                self._refresh_b_delays()
                self._update_status()
            ttk.Button(row, text="Set", command=_apply, width=4
                       ).pack(side="left", padx=2)
            desc = dc.desc if dc.desc else ""
            if is_mod:
                desc = f"[MOD {dc.original_ms}→{dc.value_ms}] " + desc
            ttk.Label(row, text=desc, font=self.font_ui, width=45,
                      style=style, anchor="w").pack(side="left", padx=4)

        self._b_delay_scroll.bind_scroll()

    # ── B: Image Quality ──────────────────────────────────────────────
    def _build_b_imgq(self):
        f = self._b_tab_imgq
        ttk.Label(f, text="Image Quality — Decoder Init Registers",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        ttk.Label(f, text="Adjust picture quality set during init. "
                  "Changes take effect on next boot.",
                  style="Dim.TLabel").pack(padx=16, anchor="w")
        ttk.Separator(f).pack(fill="x", padx=16, pady=10)
        self._b_imgq_scroll = ScrollFrame(f)
        self._b_imgq_scroll.pack(fill="both", expand=True, padx=16,
                                  pady=(0, 8))
        self._b_imgq_widgets: List[tk.Widget] = []
        self._b_imgq_regs = [0x110, 0x111, 0x112, 0x113, 0x114, 0x10C,
                              0x117, 0x20B, 0x210, 0x215, 0x21C,
                              0x281, 0x282, 0x283]

    def _refresh_b_imgq(self):
        for w in self._b_imgq_widgets:
            w.destroy()
        self._b_imgq_widgets.clear()
        parent = self._b_imgq_scroll.inner

        if not self.fw_b.init_regs:
            lbl = ttk.Label(parent, text="No init register table found.\n"
                            "Load a TW8836 MCU binary first.",
                            style="Dim.TLabel")
            lbl.pack(pady=30)
            self._b_imgq_widgets.append(lbl)
            self._b_imgq_scroll.bind_scroll()
            return

        for target_reg in self._b_imgq_regs:
            entry = next((e for e in self.fw_b.init_regs
                          if e.reg == target_reg), None)
            if not entry:
                continue
            info = REGISTER_INFO.get(target_reg, ("Unknown", ""))
            name, desc = info
            card = tk.Frame(parent, bg=C["bg2"], bd=0,
                            highlightthickness=1,
                            highlightbackground=C["border"])
            card.pack(fill="x", pady=4)
            self._b_imgq_widgets.append(card)
            # Title row
            tf = tk.Frame(card, bg=C["bg2"])
            tf.pack(fill="x", padx=12, pady=(8, 2))
            is_mod = entry.value != entry.original
            fg = C["mod"] if is_mod else C["accent"]
            tk.Label(tf, text=f"REG{target_reg:03X}", bg=C["bg2"],
                     fg=C["dim"], font=self.font_mono_sm).pack(side="left")
            tk.Label(tf, text=f"  {name}", bg=C["bg2"], fg=fg,
                     font=self.font_ui_bold).pack(side="left")
            tk.Label(tf, text=f"  {desc}", bg=C["bg2"], fg=C["dim"],
                     font=self.font_ui).pack(side="left")
            if is_mod:
                tk.Label(tf, text=f"  [Stock: 0x{entry.original:02X}]",
                         bg=C["bg2"], fg=C["warn"],
                         font=self.font_mono_sm).pack(side="right")
            # Slider row
            sf = tk.Frame(card, bg=C["bg2"])
            sf.pack(fill="x", padx=12, pady=(2, 8))
            var = tk.IntVar(value=entry.value)
            val_label = tk.Label(sf,
                text=f"0x{entry.value:02X} ({entry.value})",
                bg=C["bg2"], fg=C["fg"], font=self.font_mono,
                width=12, anchor="w")

            def _on_slide(val, vl=val_label, v=var):
                iv = int(float(val))
                v.set(iv)
                vl.config(text=f"0x{iv:02X} ({iv})")

            ttk.Scale(sf, from_=0, to=255, orient="horizontal",
                      variable=var, length=400,
                      command=_on_slide).pack(side="left", padx=(0, 10))
            val_label.pack(side="left", padx=(0, 10))

            def _apply(e=entry, v=var):
                self.fw_b.set_init_reg(e, v.get())
                self._refresh_b_imgq()
                self._update_status()

            ttk.Button(sf, text="Apply", command=_apply, width=6
                       ).pack(side="left", padx=4)

            def _reset_one(e=entry):
                self.fw_b.set_init_reg(e, e.original)
                self._refresh_b_imgq()
                self._update_status()

            ttk.Button(sf, text="Reset", command=_reset_one, width=6
                       ).pack(side="left")

        self._b_imgq_scroll.bind_scroll()

    # ── B: Registers ──────────────────────────────────────────────────
    def _build_b_regs(self):
        f = self._b_tab_regs
        ttk.Label(f, text="Init Register Table — All Entries",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        ttk.Label(f, text="Complete register initialization table "
                  "found in firmware.",
                  style="Dim.TLabel").pack(padx=16, anchor="w")
        cols = ("offset", "register", "name", "value", "stock", "description")
        tf = ttk.Frame(f)
        tf.pack(fill="both", expand=True, padx=16, pady=8)
        self.b_reg_tree = ttk.Treeview(tf, columns=cols, show="headings",
                                        height=24)
        for col, txt, w in [
            ("offset", "Offset", 80), ("register", "Register", 90),
            ("name", "Name", 160), ("value", "Value", 80),
            ("stock", "Stock", 80), ("description", "Description", 300),
        ]:
            self.b_reg_tree.heading(col, text=txt)
            self.b_reg_tree.column(col, width=w, minwidth=50)
        sb = ttk.Scrollbar(tf, orient="vertical",
                            command=self.b_reg_tree.yview)
        self.b_reg_tree.configure(yscrollcommand=sb.set)
        self.b_reg_tree.pack(fill="both", expand=True, side="left")
        sb.pack(fill="y", side="right")
        self.b_reg_tree.bind("<MouseWheel>",
            lambda e: self.b_reg_tree.yview_scroll(
                int(-1*(e.delta/120)), "units"))
        # Edit frame
        ef = ttk.LabelFrame(f, text=" Edit Selected Register ")
        ef.pack(fill="x", padx=16, pady=(0, 8))
        inner = ttk.Frame(ef)
        inner.pack(padx=8, pady=8)
        ttk.Label(inner, text="New value (0-255):").pack(side="left")
        self.b_reg_edit_var = tk.IntVar()
        ttk.Spinbox(inner, from_=0, to=255, width=6,
                     textvariable=self.b_reg_edit_var,
                     font=self.font_mono).pack(side="left", padx=6)
        ttk.Button(inner, text="Apply",
                   command=self._apply_b_reg_edit).pack(side="left", padx=4)

    def _refresh_b_regs(self):
        tree = self.b_reg_tree
        tree.delete(*tree.get_children())
        for entry in self.fw_b.init_regs:
            info = REGISTER_INFO.get(entry.reg, ("", ""))
            is_mod = entry.value != entry.original
            tag = "mod" if is_mod else ""
            tree.insert("", "end", values=(
                f"0x{entry.file_offset:05X}",
                f"REG{entry.reg:03X}",
                info[0],
                f"0x{entry.value:02X}" +
                    (f" ← 0x{entry.original:02X}" if is_mod else ""),
                f"0x{entry.original:02X}",
                info[1],
            ), tags=(tag,))
        tree.tag_configure("mod", foreground=C["mod"])

    def _apply_b_reg_edit(self):
        sel = self.b_reg_tree.selection()
        if not sel:
            return
        item = self.b_reg_tree.item(sel[0])
        off_str = item["values"][0]
        offset = int(off_str, 16)
        entry = next((e for e in self.fw_b.init_regs
                      if e.file_offset == offset), None)
        if entry:
            self.fw_b.set_init_reg(entry, self.b_reg_edit_var.get())
            self._refresh_b_regs()
            self._refresh_b_imgq()
            self._update_status()

    # ── B: Hex View ───────────────────────────────────────────────────
    def _build_b_hex(self):
        f = self._b_tab_hex
        ttk.Label(f, text="Hex Viewer", style="H.TLabel"
                  ).pack(padx=16, pady=(12, 4), anchor="w")
        nav = ttk.Frame(f)
        nav.pack(fill="x", padx=16, pady=4)
        ttk.Label(nav, text="Offset:").pack(side="left")
        self.b_hex_offset = tk.StringVar(value="0x00000")
        ttk.Entry(nav, textvariable=self.b_hex_offset, width=10,
                  font=self.font_mono).pack(side="left", padx=4)
        ttk.Label(nav, text="Bytes:").pack(side="left", padx=(10, 0))
        self.b_hex_size = tk.StringVar(value="512")
        ttk.Entry(nav, textvariable=self.b_hex_size, width=6,
                  font=self.font_mono).pack(side="left", padx=4)
        ttk.Button(nav, text="Go",
                   command=self._refresh_b_hex).pack(side="left", padx=4)
        ttk.Button(nav, text="Go to Init Table",
                   command=self._hex_b_goto_init).pack(side="left", padx=4)
        qf = ttk.Frame(f)
        qf.pack(fill="x", padx=16, pady=2)
        ttk.Label(qf, text="Jump:", style="Dim.TLabel").pack(side="left")
        for name, off in [("Vectors", 0), ("Strings", 0x80),
                          ("Common", 0x3C00), ("Bank1", 0x8000),
                          ("Bank5", 0x28000)]:
            ttk.Button(qf, text=name,
                       command=lambda o=off: self._hex_b_goto(o)
                       ).pack(side="left", padx=3)
        self.b_hex_text = tk.Text(f, bg=C["bg2"], fg=C["fg"],
                                   font=self.font_mono, relief="flat",
                                   state="disabled", insertbackground=C["fg"],
                                   highlightthickness=1,
                                   highlightbackground=C["border"],
                                   selectbackground=C["sel"])
        self.b_hex_text.pack(fill="both", expand=True, padx=16, pady=8)
        self.b_hex_text.tag_configure("modified", foreground=C["mod"],
                                       font=self.font_mono)
        self.b_hex_text.tag_configure("header", foreground=C["dim"])
        # Edit bar
        eb = ttk.Frame(f)
        eb.pack(fill="x", padx=16, pady=(0, 8))
        ttk.Label(eb, text="Edit byte at offset:").pack(side="left")
        self.b_hex_edit_off = tk.StringVar()
        ttk.Entry(eb, textvariable=self.b_hex_edit_off, width=10,
                  font=self.font_mono).pack(side="left", padx=4)
        ttk.Label(eb, text="New value:").pack(side="left")
        self.b_hex_edit_val = tk.StringVar()
        ttk.Entry(eb, textvariable=self.b_hex_edit_val, width=5,
                  font=self.font_mono).pack(side="left", padx=4)
        ttk.Button(eb, text="Write",
                   command=self._hex_b_write).pack(side="left", padx=4)

    def _refresh_b_hex(self):
        if not self.fw_b.data:
            return
        try:
            off = int(self.b_hex_offset.get(), 0)
            sz = int(self.b_hex_size.get(), 0)
        except ValueError:
            return
        txt = self.b_hex_text
        txt.config(state="normal")
        txt.delete("1.0", "end")
        end = min(off + sz, len(self.fw_b.data))
        for addr in range(off, end, 16):
            row = self.fw_b.data[addr:addr+16]
            orig_row = self.fw_b.original[addr:addr+16] \
                if self.fw_b.original else row
            txt.insert("end", f"{addr:05X}  ", "header")
            for j, b in enumerate(row):
                is_mod = j < len(orig_row) and b != orig_row[j]
                tag = "modified" if is_mod else ""
                txt.insert("end", f"{b:02X} ", tag)
            if len(row) < 16:
                txt.insert("end", "   " * (16 - len(row)))
            txt.insert("end", " ")
            for b in row:
                txt.insert("end", chr(b) if 0x20 <= b < 0x7F else ".")
            txt.insert("end", "\n")
        txt.config(state="disabled")

    def _hex_b_goto(self, offset: int):
        self.b_hex_offset.set(f"0x{offset:05X}")
        self._refresh_b_hex()

    def _hex_b_goto_init(self):
        if self.fw_b.init_table_offset >= 0:
            self._hex_b_goto(self.fw_b.init_table_offset)

    def _hex_b_write(self):
        if not self.fw_b.data:
            return
        try:
            off = int(self.b_hex_edit_off.get(), 0)
            val = int(self.b_hex_edit_val.get(), 0)
        except ValueError:
            messagebox.showerror("Error", "Invalid offset or value")
            return
        if 0 <= off < len(self.fw_b.data) and 0 <= val <= 255:
            self.fw_b.set_byte(off, val)
            self._refresh_b_hex()
            self._update_status()

    # ── B: Patch Output ───────────────────────────────────────────────
    def _build_b_patch(self):
        f = self._b_tab_patch
        ttk.Label(f, text="Patch Output — Changes Summary",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        bf = ttk.Frame(f)
        bf.pack(fill="x", padx=16, pady=8)
        ttk.Button(bf, text="Save Patched MCU…", command=self._save_b_mcu,
                   style="Accent.TButton").pack(side="left", padx=4)
        ttk.Button(bf, text="Save Patched Flash…",
                   command=self._save_b_flash,
                   style="Accent.TButton").pack(side="left", padx=4)
        ttk.Button(bf, text="Export Log…", command=self._export_b_log
                   ).pack(side="left", padx=4)
        ttk.Button(bf, text="Refresh", command=self._refresh_b_patch
                   ).pack(side="right", padx=4)
        self.b_patch_text = tk.Text(f, bg=C["bg2"], fg=C["fg"],
                                     font=self.font_mono, relief="flat",
                                     state="disabled",
                                     insertbackground=C["fg"],
                                     highlightthickness=1,
                                     highlightbackground=C["border"])
        self.b_patch_text.pack(fill="both", expand=True, padx=16,
                                pady=(0, 8))
        self.b_patch_text.tag_configure("header", foreground=C["accent"])
        self.b_patch_text.tag_configure("add", foreground=C["ok"])
        self.b_patch_text.tag_configure("del", foreground=C["err"])
        self.b_patch_text.tag_configure("info", foreground=C["dim"])

    def _refresh_b_patch(self):
        txt = self.b_patch_text
        txt.config(state="normal")
        txt.delete("1.0", "end")
        fw = self.fw_b
        if not fw.data:
            txt.insert("end", "No firmware loaded.", "info")
            txt.config(state="disabled")
            return
        dc = fw.diff_count()
        txt.insert("end", f"═══ B Board Patch Summary ═══\n", "header")
        txt.insert("end", f"Original SHA256: {fw.sha_orig()}\n", "info")
        txt.insert("end", f"Current SHA256:  {fw.sha()}\n", "info")
        txt.insert("end", f"Bytes changed:   {dc}\n", "info")
        txt.insert("end", f"Modifications:   {len(fw.mods)}\n\n", "info")
        if not fw.mods:
            txt.insert("end", "No modifications applied.\n", "info")
        else:
            txt.insert("end", f"═══ Change Log ═══\n", "header")
            for i, mod in enumerate(fw.mods, 1):
                txt.insert("end", f"\n  [{i}] {mod.desc}\n")
                txt.insert("end", f"      Offset: 0x{mod.offset:05X}\n",
                           "info")
                txt.insert("end", f"      Old:    {mod.old.hex(' ')}\n",
                           "del")
                txt.insert("end", f"      New:    {mod.new.hex(' ')}\n",
                           "add")
            txt.insert("end", f"\n═══ Binary Diff ═══\n", "header")
            if fw.original:
                for i, (a, b) in enumerate(zip(fw.original, fw.data)):
                    if a != b:
                        bank = fw.get_bank(i)
                        txt.insert("end",
                            f"  0x{i:05X} [{bank:>7s}]: ", "info")
                        txt.insert("end", f"0x{a:02X}", "del")
                        txt.insert("end", " → ")
                        txt.insert("end", f"0x{b:02X}", "add")
                        txt.insert("end", "\n")
        txt.config(state="disabled")

    # ══════════════════════════════════════════════════════════════════
    #  A BOARD TABS
    # ══════════════════════════════════════════════════════════════════

    # ── A: Overview ───────────────────────────────────────────────────
    def _build_a_overview(self):
        f = self._a_tab_overview
        ttk.Label(f, text="MK22F (A Board) — Display Controller",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        ttk.Label(f, text="ARM Cortex-M4: HDMI Rx, LVDS bridge, "
                  "OLED panel driver",
                  style="Dim.TLabel").pack(padx=16, anchor="w")
        ttk.Separator(f).pack(fill="x", padx=16, pady=10)
        self.a_ov_text = tk.Text(f, bg=C["bg2"], fg=C["fg"],
                                  font=self.font_mono, relief="flat",
                                  padx=16, pady=12, wrap="word",
                                  insertbackground=C["fg"],
                                  state="disabled")
        self.a_ov_text.pack(fill="both", expand=True, padx=8, pady=8)
        bf = ttk.Frame(f)
        bf.pack(padx=16, pady=8, anchor="w")
        ttk.Button(bf, text="Open A Board Firmware…",
                   command=self._open_a_fw).pack(side="left")

    def _refresh_a_overview(self):
        t = self.a_ov_text
        t.config(state="normal")
        t.delete("1.0", "end")
        fw = self.fw_a
        if not fw.raw:
            t.insert("end", "  No file loaded.\n\n"
                     "  Use File → Open A Board FW to load a MK22F binary.")
            t.config(state="disabled")
            return
        n, total, pct = fw.panel_coverage()
        ta = sum(1 for e in fw.panel_entries if e.table_id == "A")
        tb = sum(1 for e in fw.panel_entries if e.table_id == "B")
        cs = sum(1 for s in fw.strings if s.section == "code")
        ds = sum(1 for s in fw.strings if s.section == "data")
        lines = [
            f"{'=' * 72}",
            f"  SKY04X Pro — A Board (MK22F) Firmware Overview"
            if fw.device_type == "X4Pro" else
            f"  SKY04O Pro (040) — A Board (MK22F) Firmware Overview",
            f"{'=' * 72}",
            "",
            f"  File:           {Path(fw.path).name}",
            f"  Size:           {fw.file_size:,} bytes (0x{fw.file_size:X})",
            f"  Payload:        {fw.payload_size:,} bytes",
            f"  Encoding:       Block-keyed XOR  "
            f"(key = 0x55 + block#,  block = 512 B,  "
            f"code: ~0x{fw.code_section_len:X} bytes)",
            f"  Build Date:     {fw.build_date or '(none detected)'}",
            f"  SHA256 current: {fw.sha()}",
            f"  SHA256 stock:   {fw.sha_orig()}",
            "",
            f"  {'─' * 62}",
            f"  Vector Table",
            f"  {'─' * 62}",
            f"  Stack Pointer:  0x{fw.sp_init:08X}",
            f"  Reset Vector:   0x{fw.reset_vector:08X}",
            f"  Flash Base:     0x{A_FLASH_BASE:08X}  (48 KB bootloader)",
            "",
            f"  {'─' * 62}",
            f"  Display",
            f"  {'─' * 62}",
            *([
            f"  OLED Panel:     1280×720  (720p)",
            f"  FOV:            42°",
            f"  HDMI Input:     60 FPS",
            ] if fw.device_type == "040" else [
            f"  OLED Panel:     1920×1080  (1080p)",
            f"  FOV:            52°",
            f"  HDMI Input:     720P 100 FPS",
            ]),
            "",
            f"  {'─' * 62}",
            f"  Chip",
            f"  {'─' * 62}",
            f"  MCU:            NXP Kinetis MK22FN256VLH12",
            f"  Core:           ARM Cortex-M4 @ 120 MHz (no FPU)",
            f"  Flash:          256 KB  (48 KB bootloader + ~208 KB app)",
            f"  SRAM:           32 KB",
            f"  I2C (bit-bang): LT9211 (0x2D), IT6802 (0x49),"
            f" Panel (0x4C/0x4D)",
            "",
            f"  {'─' * 62}",
            f"  Parsed Data",
            f"  {'─' * 62}",
            f"  Panel init entries:  {total}",
            f"    Table A (0x4C, left eye):  {ta}",
            f"    Table B (0x4D, right eye): {tb}",
            f"  Panel reg coverage:  {n}/{total} = {pct:.1f} %",
            f"  Frame timing sites:  {len(fw.frame_timing_sites)}"
            f"  (MOVW Rd, #10000)",
            f"  Strings:             {len(fw.strings)}",
            f"    in code section:   {cs}",
            f"    in data section:   {ds}",
            f"  Modifications:       {fw.diff_count()}",
            "",
            f"  {'─' * 62}",
            f"  Video Pipeline",
            f"  {'─' * 62}",
            f"  HDMI in  →  IT6802 Rx (0x49)  →  MK22F  →  "
            f"LT9211 (0x2D)  →  Panel (0x4C/0x4D)",
            "",
            f"  {'─' * 62}",
            f"  Register Databases",
            f"  {'─' * 62}",
            f"  Panel OLED driver:  {len(PANEL_REGS)}",
            f"  LT9211 LVDS:        {len(LT9211_REGS)}",
            f"  IT6802 HDMI Rx:     {len(IT6802_REGS)}",
            f"  MK22F peripherals:  {len(MK22F_PERIPH)}",
            f"  Total documented:   "
            f"{len(PANEL_REGS)+len(LT9211_REGS)+len(IT6802_REGS)+len(MK22F_PERIPH)}",
            "",
        ]
        t.insert("end", "\n".join(lines))
        t.config(state="disabled")

    # ── A: Frame Timing ──────────────────────────────────────────────
    def _build_a_timing(self):
        f = self._a_tab_timing
        ttk.Label(f, text="Frame Period Timing (A Board)",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        # Dynamic device type label (updated on firmware load)
        self._a_timing_device_lbl = ttk.Label(f, text="",
                                               style="Dim.TLabel")
        self._a_timing_device_lbl.pack(padx=16, anchor="w")
        ttk.Label(f,
            text="Patch MOVW Rd, #10000 sites to change the display "
                 "frame period.  Lower µs = higher fps = lower latency.",
            style="Dim.TLabel").pack(padx=16, anchor="w")
        # 040-specific warning (hidden until 040 firmware loaded)
        self._a_timing_040_warn = tk.Label(f, text="",
            bg=C["bg"], fg="#fab387",
            font=("Segoe UI", 9), justify="left", anchor="w")
        ttk.Separator(f).pack(fill="x", padx=16, pady=8)

        # Preset buttons
        pf = ttk.LabelFrame(f, text="  Quick Presets  ")
        pf.pack(fill="x", padx=16, pady=(0, 8))
        inner_pf = ttk.Frame(pf)
        inner_pf.pack(padx=12, pady=10)
        ttk.Button(inner_pf, text="🏁 100 fps (stock)",
                   command=lambda: self._apply_fps_preset(
                       FRAME_PERIOD_STOCK)
                   ).pack(side="left", padx=6)
        ttk.Button(inner_pf, text="⚡ 120 fps (low latency)",
                   command=lambda: self._apply_fps_preset(
                       FRAME_PERIOD_120),
                   style="Accent.TButton"
                   ).pack(side="left", padx=6)
        ttk.Button(inner_pf, text="🔥 144 fps (ultra low)",
                   command=lambda: self._apply_fps_preset(
                       FRAME_PERIOD_144)
                   ).pack(side="left", padx=6)
        self._a_timing_preset_warn = ttk.Label(inner_pf,
                  text="  ⚠ 120 fps is recommended.  "
                       "144 fps may cause artifacts on some units.",
                  style="Warn.TLabel")
        self._a_timing_preset_warn.pack(side="left", padx=12)

        # Site table
        cols = ("idx", "poff", "reg", "stock", "current", "fps")
        tf2 = ttk.Frame(f)
        tf2.pack(fill="both", expand=True, padx=16, pady=4)
        self.a_timing_tree = ttk.Treeview(tf2, columns=cols,
                                           show="headings", height=8)
        widths = {"idx": 40, "poff": 110, "reg": 60, "stock": 100,
                  "current": 100, "fps": 100}
        hdrs = {"idx": "#", "poff": "PAYLOAD OFF", "reg": "REG",
                "stock": "STOCK (µs)", "current": "CURRENT (µs)",
                "fps": "FPS"}
        for col in cols:
            self.a_timing_tree.heading(col, text=hdrs.get(col, col))
            self.a_timing_tree.column(col, width=widths.get(col, 80),
                                       anchor="center")
        self.a_timing_tree.pack(fill="both", expand=True, side="left")
        vsb = ttk.Scrollbar(tf2, orient="vertical",
                             command=self.a_timing_tree.yview)
        self.a_timing_tree.configure(yscrollcommand=vsb.set)
        vsb.pack(fill="y", side="right")

        # Info panel
        info = ttk.LabelFrame(f, text="  How It Works  ")
        info.pack(fill="x", padx=16, pady=8)
        self._a_timing_info_label = tk.Label(info, text="",
                 bg=C["bg"], fg=C["dim"],
                 font=self.font_mono_sm, justify="left", anchor="w")
        self._a_timing_info_label.pack(padx=8, pady=8, fill="x")

    def _refresh_a_timing(self):
        tree = self.a_timing_tree
        tree.delete(*tree.get_children())
        # Update device-type label and info panel
        if self.fw_a.raw:
            dt = self.fw_a.device_type
            if dt == "040":
                self._a_timing_device_lbl.config(
                    text="Device: SKY04O Pro (040)  —  MK22FN256 + LT9211 "
                         "+ 1280×720 MIPI OLED  (42° FOV)")
                self._a_timing_040_warn.pack_forget()
                self._a_timing_preset_warn.config(
                    text="  ⚠ 120 fps is recommended.  "
                         "144 fps may cause artifacts on some units.")
                info_text = (
                    "  The A board firmware uses a timer interrupt to "
                    "drive the display frame period.\n"
                    "  Each MOVW site loads a period value in "
                    "microseconds into a register.\n\n"
                    "  • 100 fps = 10,000 µs  (stock)\n"
                    "  • 120 fps =  8,333 µs  (recommended "
                    "low-latency)\n"
                    "  • 144 fps =  6,944 µs  (aggressive — test "
                    "carefully)\n\n"
                    "  040 OLED: 1280×720 (720p, 42° FOV)\n"
                    "  040 and X4Pro share identical LT9211 bridge "
                    "config and PLL pixel clock.\n"
                    "  The 720p panel has fewer pixels per frame, "
                    "so high fps presets should work well.\n\n"
                    "  Presets patch the FIRST TWO sites only.\n"
                    "  The instruction is MOVW Rd, #imm16 "
                    "(Thumb-2 encoding, 4 bytes)."
                )
            else:
                self._a_timing_device_lbl.config(
                    text="Device: SKY04X Pro  —  MK22FN256 + LT9211 "
                         "+ 1920×1080 MIPI OLED  (52° FOV)")
                self._a_timing_040_warn.pack_forget()
                self._a_timing_preset_warn.config(
                    text="  ⚠ 120 fps is recommended.  "
                         "144 fps may cause artifacts on some units.")
                info_text = (
                    "  The A board firmware uses a timer interrupt to "
                    "drive the display frame period.\n"
                    "  Each MOVW site loads a period value in "
                    "microseconds into a register.\n\n"
                    "  • 100 fps = 10,000 µs  (V4.1.7 stock)\n"
                    "  • 120 fps =  8,333 µs  (recommended "
                    "low-latency)\n"
                    "  • 144 fps =  6,944 µs  (V4.1.6 stock for "
                    "first two sites)\n\n"
                    "  X4Pro OLED: 1920×1080 (1080p, 52° FOV)\n\n"
                    "  Presets patch the FIRST TWO sites only, "
                    "matching known latency patches.\n"
                    "  The instruction is MOVW Rd, #imm16 "
                    "(Thumb-2 encoding, 4 bytes)."
                )
            self._a_timing_info_label.config(text=info_text)
        if not self.fw_a.raw:
            return
        for i, site in enumerate(self.fw_a.frame_timing_sites):
            is_mod = site.value_us != site.original_us
            tag = "mod" if is_mod else ""
            fps_str = f"{site.fps:.0f}"
            tree.insert("", "end", values=(
                i,
                f"0x{site.payload_offset:05X}",
                f"R{site.register}",
                f"{site.original_us}",
                f"{site.value_us}" + (
                    f" ← {site.original_us}" if is_mod else ""),
                fps_str,
            ), tags=(tag,))
        tree.tag_configure("mod", foreground=C["mod"])

    def _apply_fps_preset(self, period_us: int):
        """Apply a frame period preset to the first two timing sites."""
        if not self.fw_a.raw:
            messagebox.showwarning("Warning", "No A firmware loaded.")
            return
        sites = self.fw_a.frame_timing_sites
        if not sites:
            messagebox.showwarning("Warning",
                "No frame timing sites found in this firmware.")
            return
        # Only patch the first two sites (matching known 120fps patches)
        count = min(2, len(sites))
        fps = 1_000_000 / period_us if period_us > 0 else 0
        if period_us != FRAME_PERIOD_STOCK:
            if not messagebox.askyesno("Confirm",
                f"Set first {count} timing site(s) to "
                f"{period_us} µs ({fps:.0f} fps)?\n\n"
                f"This modifies {count * 4} bytes in the firmware."):
                return
        for site in sites[:count]:
            self.fw_a.set_frame_period(site, period_us)
        self._refresh_a_timing()
        self._refresh_a_patch()
        self._update_status()

    # ── A: Panel Init ─────────────────────────────────────────────────
    def _build_a_panel(self):
        f = self._a_tab_panel
        ttk.Label(f, text="OLED Panel Driver Init Tables",
                  style="H.TLabel").pack(pady=(12, 4), padx=12, anchor="w")
        ttk.Label(f,
            text="I2C {reg, val} pairs — Table A → 0x4C left eye,  "
                 "Table B → 0x4D right eye.  Double-click VALUE to edit.",
            style="Dim.TLabel").pack(padx=12, anchor="w")
        self.a_panel_cov = ttk.Label(f, text="", style="Ok.TLabel")
        self.a_panel_cov.pack(padx=12, pady=(4, 0), anchor="w")
        cols = ("idx", "tbl", "grp", "foff", "reg", "name",
                "stock", "value", "desc")
        tf = ttk.Frame(f)
        tf.pack(fill="both", expand=True, padx=8, pady=8)
        self.a_panel_tree = ttk.Treeview(tf, columns=cols, show="headings",
                                          height=24)
        widths = {"idx": 40, "tbl": 30, "grp": 40, "foff": 78, "reg": 50,
                  "name": 175, "stock": 50, "value": 50, "desc": 380}
        for col in cols:
            anc = "w" if col in ("name", "desc") else "center"
            self.a_panel_tree.heading(col, text=col.upper())
            self.a_panel_tree.column(col, width=widths.get(col, 60),
                                      anchor=anc)
        vsb = ttk.Scrollbar(tf, orient="vertical",
                             command=self.a_panel_tree.yview)
        self.a_panel_tree.configure(yscrollcommand=vsb.set)
        self.a_panel_tree.pack(fill="both", expand=True, side="left")
        vsb.pack(fill="y", side="right")
        self.a_panel_tree.bind("<Double-1>", self._on_a_panel_edit)
        self.a_panel_tree.bind("<MouseWheel>",
            lambda e: self.a_panel_tree.yview_scroll(
                int(-1*(e.delta/120)), "units"))

    def _refresh_a_panel(self):
        tree = self.a_panel_tree
        tree.delete(*tree.get_children())
        for i, e in enumerate(self.fw_a.panel_entries):
            tag = "mod" if e.value != e.original else ""
            tree.insert("", "end", iid=str(i), values=(
                i, e.table_id, e.group_idx,
                f"0x{e.file_offset:05X}", f"0x{e.reg:02X}",
                e.reg_name, f"0x{e.original:02X}", f"0x{e.value:02X}",
                e.reg_desc), tags=(tag,))
        tree.tag_configure("mod", foreground=C["mod"])
        n, t, pct = self.fw_a.panel_coverage()
        self.a_panel_cov.config(
            text=f"Register name coverage: {n} / {t} = {pct:.1f} %")

    def _on_a_panel_edit(self, event):
        item = self.a_panel_tree.focus()
        if not item:
            return
        col = self.a_panel_tree.identify_column(event.x)
        if col != "#8":  # "value" column
            return
        idx = int(item)
        entry = self.fw_a.panel_entries[idx]
        new_str = self._ask_hex(
            f"Panel {entry.table_id}  reg 0x{entry.reg:02X}  "
            f"({entry.reg_name})",
            entry.value)
        if new_str is not None:
            try:
                new_val = int(new_str, 16) & 0xFF
                self.fw_a.set_panel_value(entry, new_val)
                self._refresh_a_panel()
                self._refresh_a_patch()
                self._update_status()
            except ValueError:
                messagebox.showerror("Error", "Invalid hex value")

    # ── A: Register Reference ─────────────────────────────────────────
    def _build_a_regref(self):
        f = self._a_tab_regref
        ttk.Label(f, text="Complete Register Databases",
                  style="H.TLabel").pack(pady=(12, 4), padx=12, anchor="w")
        ttk.Label(f, text="Read-only reference for every documented "
                  "register across all four ICs.",
                  style="Dim.TLabel").pack(padx=12, anchor="w")
        sub = ttk.Notebook(f)
        sub.pack(fill="both", expand=True, padx=8, pady=8)
        for label, db, fmt in [
            ("Panel OLED Driver", PANEL_REGS, "8"),
            ("LT9211 (LVDS Bridge)", LT9211_REGS, "16"),
            ("IT6802 (HDMI Rx)", IT6802_REGS, "8"),
            ("MK22F Peripherals", MK22F_PERIPH, "32"),
        ]:
            sf = ttk.Frame(sub)
            sub.add(sf, text=f"  {label}  ({len(db)})  ")
            cols = ("addr", "name", "desc")
            tree = ttk.Treeview(sf, columns=cols, show="headings", height=20)
            aw = {"8": 60, "16": 70, "32": 100}[fmt]
            tree.heading("addr", text="ADDR")
            tree.column("addr", width=aw, anchor="center")
            tree.heading("name", text="NAME")
            tree.column("name", width=200, anchor="w")
            tree.heading("desc", text="DESCRIPTION")
            tree.column("desc", width=600, anchor="w")
            vsb = ttk.Scrollbar(sf, orient="vertical", command=tree.yview)
            tree.configure(yscrollcommand=vsb.set)
            tree.pack(fill="both", expand=True, side="left")
            vsb.pack(fill="y", side="right")
            tree.bind("<MouseWheel>",
                lambda e, t=tree: t.yview_scroll(
                    int(-1*(e.delta/120)), "units"))
            for addr in sorted(db.keys()):
                name, desc = db[addr]
                if fmt == "32":
                    a = f"0x{addr:08X}"
                elif fmt == "16":
                    a = f"0x{addr:04X}"
                else:
                    a = f"0x{addr:02X}"
                tree.insert("", "end", values=(a, name, desc))

    # ── A: Strings ────────────────────────────────────────────────────
    def _build_a_strings(self):
        f = self._a_tab_strings
        ttk.Label(f, text="Embedded Strings",
                  style="H.TLabel").pack(pady=(12, 4), padx=12, anchor="w")
        ttk.Label(f, text="Green = data section,  blue = LT9211,  "
                  "orange = IT6802/HDMI.",
                  style="Dim.TLabel").pack(padx=12, anchor="w")
        cols = ("poff", "section", "text")
        tf = ttk.Frame(f)
        tf.pack(fill="both", expand=True, padx=8, pady=8)
        self.a_str_tree = ttk.Treeview(tf, columns=cols, show="headings",
                                        height=22)
        for col, w, a in [("poff", 100, "center"), ("section", 60, "center"),
                          ("text", 960, "w")]:
            self.a_str_tree.heading(col, text=col.upper())
            self.a_str_tree.column(col, width=w, anchor=a)
        vsb = ttk.Scrollbar(tf, orient="vertical",
                             command=self.a_str_tree.yview)
        self.a_str_tree.configure(yscrollcommand=vsb.set)
        self.a_str_tree.pack(fill="both", expand=True, side="left")
        vsb.pack(fill="y", side="right")
        self.a_str_tree.bind("<MouseWheel>",
            lambda e: self.a_str_tree.yview_scroll(
                int(-1*(e.delta/120)), "units"))

    def _refresh_a_strings(self):
        tree = self.a_str_tree
        tree.delete(*tree.get_children())
        for s in self.fw_a.strings:
            tag = ""
            if s.section == "data":
                tag = "data"
            elif "LT9211" in s.text or "LT921" in s.text:
                tag = "lt"
            elif any(k in s.text for k in ("ITE", "HDCP", "IT68",
                                           "HDMI", "VSTATE", "ASTATE")):
                tag = "it"
            tree.insert("", "end", values=(
                f"0x{s.payload_offset:05X}", s.section, s.text[:140]),
                tags=(tag,))
        tree.tag_configure("data", foreground=C["ok"])
        tree.tag_configure("lt", foreground=C["accent2"])
        tree.tag_configure("it", foreground=C["warn"])

    # ── A: Hex View ───────────────────────────────────────────────────
    def _build_a_hex(self):
        f = self._a_tab_hex
        ttk.Label(f, text="Hex Viewer (Decoded Payload)",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        ctrl = ttk.Frame(f)
        ctrl.pack(fill="x", padx=8, pady=8)
        ttk.Label(ctrl, text="Payload offset (hex):").pack(side="left",
                                                            padx=4)
        self.a_hex_off = tk.StringVar(value="0")
        e = ttk.Entry(ctrl, textvariable=self.a_hex_off, width=10,
                      font=self.font_mono)
        e.pack(side="left", padx=4)
        e.bind("<Return>", lambda ev: self._refresh_a_hex())
        ttk.Button(ctrl, text="Go",
                   command=self._refresh_a_hex).pack(side="left", padx=4)
        self.a_hex_raw = tk.BooleanVar(value=False)
        ttk.Checkbutton(ctrl, text="Raw (XOR-encoded)",
                        variable=self.a_hex_raw,
                        command=self._refresh_a_hex
                        ).pack(side="left", padx=12)
        ttk.Label(ctrl, text="Jump:", style="Dim.TLabel"
                  ).pack(side="left", padx=(20, 4))
        jumps = [
            ("Vectors", "0"),
            ("Strings", f"0x{self.fw_a.code_section_len:X}"
             if self.fw_a.raw else "0x35600"),
            ("End-256", f"0x{max(0, self.fw_a.payload_size - 256):X}"
             if self.fw_a.raw else "0x35000"),
        ]
        for label, off in jumps:
            ttk.Button(ctrl, text=label,
                       command=lambda o=off: self._jump_a_hex(o)
                       ).pack(side="left", padx=2)
        self.a_hex_text = tk.Text(f, bg=C["bg2"], fg=C["fg"],
                                   font=self.font_mono_sm, wrap="none",
                                   relief="flat", padx=10, pady=8)
        self.a_hex_text.pack(fill="both", expand=True, padx=8, pady=(0, 8))

    def _jump_a_hex(self, off_str: str):
        self.a_hex_off.set(off_str)
        self._refresh_a_hex()

    def _refresh_a_hex(self):
        self.a_hex_text.delete("1.0", "end")
        if not self.fw_a.raw:
            return
        try:
            off = int(self.a_hex_off.get(), 16)
        except ValueError:
            off = 0
        dump = self.fw_a.hex_dump(off, 768,
                                   raw_view=self.a_hex_raw.get())
        self.a_hex_text.insert("end", dump)

    # ── A: Patch Output ───────────────────────────────────────────────
    def _build_a_patch(self):
        f = self._a_tab_patch
        ttk.Label(f, text="Modification Log",
                  style="H.TLabel").pack(pady=(12, 4), padx=12, anchor="w")
        self.a_patch_text = tk.Text(f, bg=C["bg2"], fg=C["fg"],
                                     font=self.font_mono, wrap="word",
                                     relief="flat", padx=16, pady=12)
        self.a_patch_text.pack(fill="both", expand=True, padx=8, pady=8)
        bf = ttk.Frame(f)
        bf.pack(fill="x", padx=8, pady=(0, 8))
        ttk.Button(bf, text="Save Patched A Firmware…",
                   command=self._save_a,
                   style="Accent.TButton").pack(side="right", padx=4)
        ttk.Button(bf, text="Reset A Board",
                   command=self._reset_a).pack(side="right", padx=4)

    def _refresh_a_patch(self):
        txt = self.a_patch_text
        txt.config(state="normal")
        txt.delete("1.0", "end")
        dc = self.fw_a.diff_count()
        if dc == 0:
            txt.insert("end", "No modifications.\n")
        else:
            txt.insert("end", f"=== {dc} byte(s) modified ===\n\n")
            for m in self.fw_a.mods:
                txt.insert("end",
                    f"  file 0x{m.file_offset:05X}: "
                    f"0x{m.old:02X} → 0x{m.new:02X}   {m.desc}\n")
            txt.insert("end",
                f"\nStock SHA256:   {self.fw_a.sha_orig()}\n"
                f"Current SHA256: {self.fw_a.sha()}\n")
        txt.config(state="disabled")

    # ══════════════════════════════════════════════════════════════════
    #  040 B BOARD TABS
    # ══════════════════════════════════════════════════════════════════

    # ── 040 B: Overview ───────────────────────────────────────────────
    def _build_b040_overview(self):
        f = self._b040_tab_overview
        ttk.Label(f, text="TW8836 (040 B Board) — SKY04O Pro Video Processor",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        ttk.Label(f, text="8051 MCU — block-keyed XOR encoded firmware  "
                  "(same key schedule as A board)",
                  style="Dim.TLabel").pack(padx=16, anchor="w")
        ttk.Separator(f).pack(fill="x", padx=16, pady=10)
        self.b040_ov_text = tk.Text(f, bg=C["bg2"], fg=C["fg"],
                                    font=self.font_mono, relief="flat",
                                    padx=16, pady=12,
                                    insertbackground=C["fg"],
                                    state="disabled", highlightthickness=1,
                                    highlightbackground=C["border"])
        self.b040_ov_text.pack(fill="both", expand=True, padx=8, pady=8)
        bf = ttk.Frame(f)
        bf.pack(padx=16, pady=8, anchor="w")
        ttk.Button(bf, text="Open 040 B Board Flash…",
                   command=self._open_b040_flash).pack(side="left", padx=(0,8))

    def _refresh_b040_overview(self):
        t = self.b040_ov_text
        t.config(state="normal")
        t.delete("1.0", "end")
        fw = self.fw_040b
        if not fw.data:
            t.insert("end", "  No file loaded.\n\n"
                     "  Use File → Open 040 B Board Flash to load the "
                     "SKY04O_Pro_B firmware.")
            t.config(state="disabled")
            return
        size_mb = len(fw.raw) / 1_048_576
        lines = [
            f"  File:              {fw.path}",
            f"  Raw size:          {len(fw.raw):,} bytes ({size_mb:.2f} MB)",
            f"  Decoded payload:   {len(fw.data):,} bytes",
            f"  Encoding:          Block-keyed XOR  "
            f"(key = 0x55 + block#, block = 512 B)",
            f"  Header ID:         {bytes(fw.raw[:6]).hex(' ')}",
            f"  SHA256 (header):   {fw.sha_orig()}",
            "",
            f"  ── Analysis ─────────────────────────────────────────────",
            f"  delay1ms() call sites found: {len(fw.delays)}",
            f"  Init register entries:       {len(fw.init_regs)}",
        ]
        if fw.init_table_offset >= 0:
            lines.append(
                f"  Init table offset (decoded): 0x{fw.init_table_offset:05X}")
        lines += [
            f"  Modifications:               {fw.diff_count()} bytes changed",
            "",
            f"  ── Delay Summary ────────────────────────────────────────",
        ]
        if fw.delays:
            dc = Counter(d.value_ms for d in fw.delays)
            for ms, cnt in sorted(dc.items(), reverse=True):
                lines.append(f"  delay1ms({ms:>6}):  {cnt:>3} call site(s)")
        else:
            lines.append("  (none found)")
        t.insert("end", "\n".join(lines))
        t.config(state="disabled")

    # ── 040 B: Delays ─────────────────────────────────────────────────
    def _build_b040_delays(self):
        f = self._b040_tab_delays
        ttk.Label(f, text="Timing Delays — delay1ms() Call Sites (040 B)",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        ttk.Label(f, text="Modify stabilization/settling delays. "
                  "Lower = faster switching, but may cause instability.",
                  style="Dim.TLabel").pack(padx=16, anchor="w")
        fb = ttk.Frame(f)
        fb.pack(fill="x", padx=16, pady=8)
        ttk.Label(fb, text="Filter:").pack(side="left")
        self.b040_delay_filter = tk.StringVar(value="all")
        for txt, val in [("All", "all"), ("≥100ms", "100"),
                         ("≥1000ms", "1000"), ("Modified", "mod")]:
            ttk.Radiobutton(fb, text=txt, variable=self.b040_delay_filter,
                            value=val, command=self._refresh_b040_delays
                            ).pack(side="left", padx=6)
        ttk.Button(fb, text="Halve All ≥100ms",
                   command=lambda: self._bulk_b040_delay(0.5)
                   ).pack(side="right", padx=4)
        ttk.Button(fb, text="Reset Delays",
                   command=self._reset_b040_delays).pack(side="right", padx=4)
        self._b040_delay_scroll = ScrollFrame(f)
        self._b040_delay_scroll.pack(fill="both", expand=True,
                                     padx=16, pady=(0, 8))
        self._b040_delay_widgets: List[tk.Widget] = []

    def _refresh_b040_delays(self):
        for w in self._b040_delay_widgets:
            w.destroy()
        self._b040_delay_widgets.clear()
        parent = self._b040_delay_scroll.inner

        if not self.fw_040b.data:
            lbl = ttk.Label(parent, text="No firmware loaded",
                            style="Dim.TLabel")
            lbl.pack(pady=20)
            self._b040_delay_widgets.append(lbl)
            self._b040_delay_scroll.bind_scroll()
            return

        filt = self.b040_delay_filter.get()
        hdr = ttk.Frame(parent)
        hdr.pack(fill="x", pady=(0, 4))
        for txt, w in [("Offset", 10), ("Value(ms)", 10),
                       ("Adjust", 28), ("", 8), ("Description", 40)]:
            ttk.Label(hdr, text=txt, style="Dim.TLabel", width=w,
                      font=self.font_ui_bold).pack(side="left", padx=2)
        self._b040_delay_widgets.append(hdr)

        for dc in self.fw_040b.delays:
            if filt == "100"  and dc.value_ms < 100:
                continue
            if filt == "1000" and dc.value_ms < 1000:
                continue
            if filt == "mod"  and dc.value_ms == dc.original_ms:
                continue
            row = ttk.Frame(parent)
            row.pack(fill="x", pady=1)
            self._b040_delay_widgets.append(row)
            is_mod = dc.value_ms != dc.original_ms
            style = "Mod.TLabel" if is_mod else "TLabel"
            ttk.Label(row, text=f"0x{dc.offset:06X}",
                      font=self.font_mono_sm, width=10,
                      style=style).pack(side="left", padx=2)
            var = tk.IntVar(value=dc.value_ms)
            ttk.Spinbox(row, from_=1, to=30000, width=7,
                        textvariable=var,
                        font=self.font_mono_sm).pack(side="left", padx=2)
            ttk.Scale(row, from_=1,
                      to=max(5000, dc.original_ms * 2),
                      orient="horizontal", length=200,
                      variable=var).pack(side="left", padx=4)

            def _apply(d=dc, v=var):
                self.fw_040b.set_delay(d, v.get())
                self._refresh_b040_delays()
                self._update_status()
            ttk.Button(row, text="Set", command=_apply, width=4
                       ).pack(side="left", padx=2)
            desc = dc.desc or ""
            if is_mod:
                desc = f"[MOD {dc.original_ms}→{dc.value_ms}] " + desc
            ttk.Label(row, text=desc, font=self.font_ui, width=45,
                      style=style, anchor="w").pack(side="left", padx=4)

        self._b040_delay_scroll.bind_scroll()

    def _bulk_b040_delay(self, factor: float):
        if not self.fw_040b.data:
            return
        for dc in self.fw_040b.delays:
            if dc.value_ms >= 100:
                self.fw_040b.set_delay(dc, max(1, int(dc.value_ms * factor)))
        self._refresh_b040_delays()
        self._update_status()

    def _reset_b040_delays(self):
        if not self.fw_040b.data:
            return
        changed = [dc for dc in self.fw_040b.delays
                   if dc.value_ms != dc.original_ms]
        for dc in changed:
            self.fw_040b.set_delay(dc, dc.original_ms)
        self._refresh_b040_delays()
        self._update_status()

    # ── 040 B: Image Quality ──────────────────────────────────────────
    def _build_b040_imgq(self):
        f = self._b040_tab_imgq
        ttk.Label(f, text="Image Quality — Decoder Init Registers (040 B)",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        ttk.Label(f, text="Adjust picture quality set during init. "
                  "Changes take effect on next boot.",
                  style="Dim.TLabel").pack(padx=16, anchor="w")
        ttk.Separator(f).pack(fill="x", padx=16, pady=10)
        self._b040_imgq_scroll = ScrollFrame(f)
        self._b040_imgq_scroll.pack(fill="both", expand=True,
                                    padx=16, pady=(0, 8))
        self._b040_imgq_widgets: List[tk.Widget] = []
        self._b040_imgq_regs = [0x110, 0x111, 0x112, 0x113, 0x114, 0x10C,
                                 0x117, 0x20B, 0x210, 0x215, 0x21C,
                                 0x281, 0x282, 0x283]

    def _refresh_b040_imgq(self):
        for w in self._b040_imgq_widgets:
            w.destroy()
        self._b040_imgq_widgets.clear()
        parent = self._b040_imgq_scroll.inner

        if not self.fw_040b.init_regs:
            lbl = ttk.Label(parent, text="No init register table found.\n"
                            "Load a 040 B firmware first.",
                            style="Dim.TLabel")
            lbl.pack(pady=30)
            self._b040_imgq_widgets.append(lbl)
            self._b040_imgq_scroll.bind_scroll()
            return

        for target_reg in self._b040_imgq_regs:
            entry = next((e for e in self.fw_040b.init_regs
                          if e.reg == target_reg), None)
            if not entry:
                continue
            info = REGISTER_INFO.get(target_reg, ("Unknown", ""))
            name, desc = info
            card = tk.Frame(parent, bg=C["bg2"], bd=0,
                            highlightthickness=1,
                            highlightbackground=C["border"])
            card.pack(fill="x", pady=4)
            self._b040_imgq_widgets.append(card)
            tf2 = tk.Frame(card, bg=C["bg2"])
            tf2.pack(fill="x", padx=12, pady=(8, 2))
            is_mod = entry.value != entry.original
            fg = C["mod"] if is_mod else C["accent"]
            tk.Label(tf2, text=f"REG{target_reg:03X}", bg=C["bg2"],
                     fg=C["dim"], font=self.font_mono_sm).pack(side="left")
            tk.Label(tf2, text=f"  {name}", bg=C["bg2"], fg=fg,
                     font=self.font_ui_bold).pack(side="left")
            tk.Label(tf2, text=f"  {desc}", bg=C["bg2"], fg=C["dim"],
                     font=self.font_ui).pack(side="left")
            if is_mod:
                tk.Label(tf2, text=f"  [Stock: 0x{entry.original:02X}]",
                         bg=C["bg2"], fg=C["warn"],
                         font=self.font_mono_sm).pack(side="right")
            sf2 = tk.Frame(card, bg=C["bg2"])
            sf2.pack(fill="x", padx=12, pady=(2, 8))
            var = tk.IntVar(value=entry.value)
            val_label = tk.Label(sf2,
                text=f"0x{entry.value:02X} ({entry.value})",
                bg=C["bg2"], fg=C["fg"], font=self.font_mono,
                width=12, anchor="w")

            def _on_slide(val, vl=val_label, v=var):
                iv = int(float(val))
                v.set(iv)
                vl.config(text=f"0x{iv:02X} ({iv})")

            ttk.Scale(sf2, from_=0, to=255, orient="horizontal",
                      variable=var, length=400,
                      command=_on_slide).pack(side="left", padx=(0, 10))
            val_label.pack(side="left", padx=(0, 10))

            def _apply(e=entry, v=var):
                self.fw_040b.set_init_reg(e, v.get())
                self._refresh_b040_imgq()
                self._update_status()

            ttk.Button(sf2, text="Apply", command=_apply, width=6
                       ).pack(side="left", padx=4)

            def _reset_one(e=entry):
                self.fw_040b.set_init_reg(e, e.original)
                self._refresh_b040_imgq()
                self._update_status()

            ttk.Button(sf2, text="Reset", command=_reset_one, width=6
                       ).pack(side="left")

        self._b040_imgq_scroll.bind_scroll()

    # ── 040 B: Registers ──────────────────────────────────────────────
    def _build_b040_regs(self):
        f = self._b040_tab_regs
        ttk.Label(f, text="Init Register Table — All Entries (040 B)",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        cols = ("offset", "register", "name", "value", "stock", "description")
        tf = ttk.Frame(f)
        tf.pack(fill="both", expand=True, padx=16, pady=8)
        self.b040_reg_tree = ttk.Treeview(tf, columns=cols, show="headings",
                                           height=24)
        for col, txt, w in [
            ("offset", "Offset", 80), ("register", "Register", 90),
            ("name", "Name", 160), ("value", "Value", 80),
            ("stock", "Stock", 80), ("description", "Description", 300),
        ]:
            self.b040_reg_tree.heading(col, text=txt)
            self.b040_reg_tree.column(col, width=w, minwidth=50)
        sb = ttk.Scrollbar(tf, orient="vertical",
                            command=self.b040_reg_tree.yview)
        self.b040_reg_tree.configure(yscrollcommand=sb.set)
        self.b040_reg_tree.pack(fill="both", expand=True, side="left")
        sb.pack(fill="y", side="right")
        self.b040_reg_tree.bind("<MouseWheel>",
            lambda e: self.b040_reg_tree.yview_scroll(
                int(-1*(e.delta/120)), "units"))
        ef = ttk.LabelFrame(f, text=" Edit Selected Register ")
        ef.pack(fill="x", padx=16, pady=(0, 8))
        inner = ttk.Frame(ef)
        inner.pack(padx=8, pady=8)
        ttk.Label(inner, text="New value (0-255):").pack(side="left")
        self.b040_reg_edit_var = tk.IntVar()
        ttk.Spinbox(inner, from_=0, to=255, width=6,
                    textvariable=self.b040_reg_edit_var,
                    font=self.font_mono).pack(side="left", padx=6)
        ttk.Button(inner, text="Apply",
                   command=self._apply_b040_reg_edit).pack(side="left", padx=4)

    def _refresh_b040_regs(self):
        tree = self.b040_reg_tree
        tree.delete(*tree.get_children())
        for entry in self.fw_040b.init_regs:
            info = REGISTER_INFO.get(entry.reg, ("", ""))
            is_mod = entry.value != entry.original
            tag = "mod" if is_mod else ""
            tree.insert("", "end", values=(
                f"0x{entry.file_offset:06X}",
                f"REG{entry.reg:03X}",
                info[0],
                f"0x{entry.value:02X}" +
                    (f" ← 0x{entry.original:02X}" if is_mod else ""),
                f"0x{entry.original:02X}",
                info[1],
            ), tags=(tag,))
        tree.tag_configure("mod", foreground=C["mod"])

    def _apply_b040_reg_edit(self):
        sel = self.b040_reg_tree.selection()
        if not sel:
            return
        item = self.b040_reg_tree.item(sel[0])
        off_str = item["values"][0]
        offset = int(off_str, 16)
        entry = next((e for e in self.fw_040b.init_regs
                      if e.file_offset == offset), None)
        if entry:
            self.fw_040b.set_init_reg(entry, self.b040_reg_edit_var.get())
            self._refresh_b040_regs()
            self._refresh_b040_imgq()
            self._update_status()

    # ── 040 B: Hex View ───────────────────────────────────────────────
    def _build_b040_hex(self):
        f = self._b040_tab_hex
        ttk.Label(f, text="Hex Viewer (Decoded Payload) — 040 B",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        nav = ttk.Frame(f)
        nav.pack(fill="x", padx=16, pady=4)
        ttk.Label(nav, text="Offset:").pack(side="left")
        self.b040_hex_offset = tk.StringVar(value="0x00000")
        ttk.Entry(nav, textvariable=self.b040_hex_offset, width=10,
                  font=self.font_mono).pack(side="left", padx=4)
        ttk.Label(nav, text="Bytes:").pack(side="left", padx=(10, 0))
        self.b040_hex_size = tk.StringVar(value="512")
        ttk.Entry(nav, textvariable=self.b040_hex_size, width=6,
                  font=self.font_mono).pack(side="left", padx=4)
        ttk.Button(nav, text="Go",
                   command=self._refresh_b040_hex).pack(side="left", padx=4)
        ttk.Button(nav, text="Go to Init Table",
                   command=self._hex_b040_goto_init).pack(side="left", padx=4)
        qf = ttk.Frame(f)
        qf.pack(fill="x", padx=16, pady=2)
        ttk.Label(qf, text="Jump:", style="Dim.TLabel").pack(side="left")
        for name2, off in [("Strings", 0x0A5), ("Init Table", 0x5443),
                           ("First Delay", 0xFD4F)]:
            ttk.Button(qf, text=name2,
                       command=lambda o=off: self._hex_b040_goto(o)
                       ).pack(side="left", padx=3)
        self.b040_hex_text = tk.Text(f, bg=C["bg2"], fg=C["fg"],
                                     font=self.font_mono, relief="flat",
                                     state="disabled",
                                     insertbackground=C["fg"],
                                     highlightthickness=1,
                                     highlightbackground=C["border"],
                                     selectbackground=C["sel"])
        self.b040_hex_text.pack(fill="both", expand=True, padx=16, pady=8)
        self.b040_hex_text.tag_configure("modified", foreground=C["mod"])
        self.b040_hex_text.tag_configure("header", foreground=C["dim"])
        eb = ttk.Frame(f)
        eb.pack(fill="x", padx=16, pady=(0, 8))
        ttk.Label(eb, text="Edit byte at offset:").pack(side="left")
        self.b040_hex_edit_off = tk.StringVar()
        ttk.Entry(eb, textvariable=self.b040_hex_edit_off, width=10,
                  font=self.font_mono).pack(side="left", padx=4)
        ttk.Label(eb, text="New value:").pack(side="left")
        self.b040_hex_edit_val = tk.StringVar()
        ttk.Entry(eb, textvariable=self.b040_hex_edit_val, width=5,
                  font=self.font_mono).pack(side="left", padx=4)
        ttk.Button(eb, text="Write",
                   command=self._hex_b040_write).pack(side="left", padx=4)

    def _refresh_b040_hex(self):
        if not self.fw_040b.data:
            return
        try:
            off = int(self.b040_hex_offset.get(), 0)
            sz  = int(self.b040_hex_size.get(), 0)
        except ValueError:
            return
        txt = self.b040_hex_text
        txt.config(state="normal")
        txt.delete("1.0", "end")
        d = self.fw_040b.data
        orig = self.fw_040b.original
        end = min(off + sz, len(d))
        for addr in range(off, end, 16):
            row = d[addr:addr+16]
            txt.insert("end", f"{addr:06X}  ", "header")
            for j, b in enumerate(row):
                is_mod = (orig is not None and
                          addr+j < len(orig) and b != orig[addr+j])
                tag = "modified" if is_mod else ""
                txt.insert("end", f"{b:02X} ", tag)
            if len(row) < 16:
                txt.insert("end", "   " * (16 - len(row)))
            txt.insert("end", " ")
            for b in row:
                txt.insert("end", chr(b) if 0x20 <= b < 0x7F else ".")
            txt.insert("end", "\n")
        txt.config(state="disabled")

    def _hex_b040_goto(self, offset: int):
        self.b040_hex_offset.set(f"0x{offset:06X}")
        self._refresh_b040_hex()

    def _hex_b040_goto_init(self):
        if self.fw_040b.init_table_offset >= 0:
            self._hex_b040_goto(self.fw_040b.init_table_offset)

    def _hex_b040_write(self):
        if not self.fw_040b.data:
            return
        try:
            off = int(self.b040_hex_edit_off.get(), 0)
            val = int(self.b040_hex_edit_val.get(), 0)
        except ValueError:
            messagebox.showerror("Error", "Invalid offset or value")
            return
        if 0 <= off < len(self.fw_040b.data) and 0 <= val <= 255:
            self.fw_040b.set_byte(off, val)
            self._refresh_b040_hex()
            self._update_status()

    # ── 040 B: Patch Output ───────────────────────────────────────────
    def _build_b040_patch(self):
        f = self._b040_tab_patch
        ttk.Label(f, text="Patch Output — Changes Summary (040 B)",
                  style="H.TLabel").pack(padx=16, pady=(12, 4), anchor="w")
        bf = ttk.Frame(f)
        bf.pack(fill="x", padx=16, pady=8)
        ttk.Button(bf, text="Save Patched Flash…",
                   command=self._save_b040_flash,
                   style="Accent.TButton").pack(side="left", padx=4)
        ttk.Button(bf, text="Export Log…",
                   command=self._export_b040_log).pack(side="left", padx=4)
        ttk.Button(bf, text="Refresh",
                   command=self._refresh_b040_patch).pack(side="right", padx=4)
        self.b040_patch_text = tk.Text(f, bg=C["bg2"], fg=C["fg"],
                                       font=self.font_mono, relief="flat",
                                       state="disabled",
                                       insertbackground=C["fg"],
                                       highlightthickness=1,
                                       highlightbackground=C["border"])
        self.b040_patch_text.pack(fill="both", expand=True, padx=16,
                                  pady=(0, 8))
        self.b040_patch_text.tag_configure("header", foreground=C["accent"])
        self.b040_patch_text.tag_configure("add",    foreground=C["ok"])
        self.b040_patch_text.tag_configure("del",    foreground=C["err"])
        self.b040_patch_text.tag_configure("info",   foreground=C["dim"])

    def _refresh_b040_patch(self):
        txt = self.b040_patch_text
        txt.config(state="normal")
        txt.delete("1.0", "end")
        fw = self.fw_040b
        if not fw.data:
            txt.insert("end", "No firmware loaded.", "info")
            txt.config(state="disabled")
            return
        dc = fw.diff_count()
        txt.insert("end", "═══ 040 B Board Patch Summary ═══\n", "header")
        txt.insert("end", f"File:            {fw.path}\n", "info")
        txt.insert("end", f"Header SHA256:   {fw.sha_orig()}\n", "info")
        txt.insert("end", f"Bytes changed:   {dc}\n", "info")
        txt.insert("end", f"Modifications:   {len(fw.mods)}\n\n", "info")
        if not fw.mods:
            txt.insert("end", "No modifications applied.\n", "info")
        else:
            txt.insert("end", "═══ Change Log ═══\n", "header")
            for i, mod in enumerate(fw.mods, 1):
                txt.insert("end", f"\n  [{i}] {mod.desc}\n")
                txt.insert("end",
                           f"      Decoded offset: 0x{mod.offset:06X}\n",
                           "info")
                txt.insert("end",
                           f"      Old:  {mod.old.hex(' ')}\n", "del")
                txt.insert("end",
                           f"      New:  {mod.new.hex(' ')}\n", "add")
        txt.config(state="disabled")

    # ── 040 B: refresh all ────────────────────────────────────────────
    def _refresh_b040_all(self):
        self._refresh_b040_overview()
        self._refresh_b040_delays()
        self._refresh_b040_imgq()
        self._refresh_b040_regs()
        self._refresh_b040_hex()
        self._refresh_b040_patch()
        self._update_status()

    # ══════════════════════════════════════════════════════════════════
    #  HELPERS
    # ══════════════════════════════════════════════════════════════════

    def _ask_hex(self, title: str, current: int) -> Optional[str]:
        dlg = tk.Toplevel(self.root)
        dlg.title(title)
        dlg.geometry("360x130")
        dlg.configure(bg=C["bg"])
        dlg.transient(self.root)
        dlg.grab_set()
        ttk.Label(dlg, text=f"Current: 0x{current:02X}  ({current})").pack(
            pady=(12, 4))
        ttk.Label(dlg, text="New value (hex):").pack()
        var = tk.StringVar(value=f"{current:02X}")
        entry = ttk.Entry(dlg, textvariable=var, width=12,
                          font=self.font_mono)
        entry.pack(pady=4)
        entry.select_range(0, "end")
        entry.focus()
        result = [None]

        def on_ok(event=None):
            result[0] = var.get().strip()
            dlg.destroy()

        entry.bind("<Return>", on_ok)
        ttk.Button(dlg, text="OK", command=on_ok).pack(pady=4)
        dlg.wait_window()
        return result[0]

    # ══════════════════════════════════════════════════════════════════
    #  FILE OPERATIONS
    # ══════════════════════════════════════════════════════════════════

    def _open_b_mcu(self):
        path = filedialog.askopenfilename(
            title="Open TW8836 MCU Binary",
            filetypes=[("Binary files", "*.bin"), ("All files", "*.*")],
            initialdir=str(Path(self.fw_b.path).parent)
                        if self.fw_b.path else ".")
        if path:
            self._load_b(path)

    def _open_b_flash(self):
        path = filedialog.askopenfilename(
            title="Open SPI Flash Image",
            filetypes=[("Binary files", "*.bin"), ("All files", "*.*")],
            initialdir=str(Path(self.fw_b.path).parent)
                        if self.fw_b.path else ".")
        if path:
            self._load_b(path)

    def _open_b040_flash(self):
        path = filedialog.askopenfilename(
            title="Open SKY04O Pro B Board Firmware",
            filetypes=[("BIN files", "*.bin"), ("All files", "*.*")],
            initialdir=str(Path(self.fw_040b.path).parent)
                        if self.fw_040b.path else ".")
        if path:
            self._load_b040(path)

    def _open_a_fw(self):
        path = filedialog.askopenfilename(
            title="Open A Board Firmware",
            filetypes=[("BIN files", "*.bin"), ("All files", "*.*")],
            initialdir=str(Path(self.fw_a.path).parent)
                        if self.fw_a.path else ".")
        if path:
            self._load_a(path)

    def _load_b(self, path: str):
        try:
            self.fw_b.load(path)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load B firmware: {e}")
            return
        self._auto_backup(path)
        self._refresh_b_all()
        self.board_nb.select(0)  # Switch to B board tab

    def _load_b040(self, path: str):
        try:
            # Show a progress dialog since decoding 15 MB takes a moment
            self.root.config(cursor="watch")
            self.root.update()
            self.fw_040b.load(path)
        except Exception as e:
            self.root.config(cursor="")
            messagebox.showerror("Error",
                                 f"Failed to load 040 B firmware: {e}")
            return
        finally:
            self.root.config(cursor="")
        self._auto_backup(path)
        self._refresh_b040_all()
        self.board_nb.select(2)  # Switch to 040 B tab

    def _load_a(self, path: str):
        try:
            self.fw_a.load(path)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load A firmware: {e}")
            return
        self._auto_backup(path)
        self._refresh_a_all()

    def _auto_backup(self, path: str):
        src = Path(path)
        backup_dir = self._get_output_dir() / "backups"
        backup_dir.mkdir(parents=True, exist_ok=True)
        backup_path = backup_dir / f"{src.stem}_STOCK{src.suffix}"
        if not backup_path.exists():
            shutil.copy2(str(src), str(backup_path))

    def _get_output_dir(self) -> Path:
        for fw_path in [self.fw_b.path, self.fw_a.path, self.fw_040b.path]:
            if fw_path:
                p = Path(fw_path).resolve()
                for parent in [p.parent, p.parent.parent,
                               p.parent.parent.parent]:
                    if (parent / "tools").is_dir() or \
                       (parent / "firmware").is_dir():
                        return parent / "patched"
        if getattr(sys, 'frozen', False):
            return Path(sys.executable).parent / "patched"
        return Path(__file__).resolve().parent.parent / "patched"

    def _save_b_mcu(self):
        if not self.fw_b.data:
            return
        if self.fw_b.diff_count() == 0:
            messagebox.showinfo("Info", "No B board modifications to save.")
            return
        out_dir = self._get_output_dir()
        out_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        stem = Path(self.fw_b.path).stem
        path = filedialog.asksaveasfilename(
            title="Save Patched MCU Binary",
            defaultextension=".bin",
            filetypes=[("Binary files", "*.bin")],
            initialdir=str(out_dir),
            initialfile=f"{stem}_patched_{ts}.bin")
        if path:
            self.fw_b.save_mcu(path)
            log = self._auto_export_log_b(path)
            msg = f"Patched MCU saved to:\n{path}"
            if log:
                msg += f"\n\nPatch log:\n{log}"
            messagebox.showinfo("Saved", msg)

    def _save_b_flash(self):
        if not self.fw_b.data:
            return
        if not self.fw_b._flash_data:
            messagebox.showwarning("Warning",
                "No flash image loaded. Use Save Patched MCU.")
            return
        if self.fw_b.diff_count() == 0:
            messagebox.showinfo("Info", "No B board modifications to save.")
            return
        out_dir = self._get_output_dir()
        out_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = filedialog.asksaveasfilename(
            title="Save Patched SPI Flash",
            defaultextension=".bin",
            filetypes=[("Binary files", "*.bin")],
            initialdir=str(out_dir),
            initialfile=f"SKY04X_Pro_B_flash_patched_{ts}.bin")
        if path:
            self.fw_b.save_flash(path)
            log = self._auto_export_log_b(path)
            msg = f"Patched flash saved to:\n{path}"
            if log:
                msg += f"\n\nPatch log:\n{log}"
            messagebox.showinfo("Saved", msg)

    def _save_a(self):
        if not self.fw_a.raw:
            return
        if self.fw_a.diff_count() == 0:
            messagebox.showinfo("Info", "No A board modifications to save.")
            return
        out_dir = self._get_output_dir()
        out_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        stem = Path(self.fw_a.path).stem
        path = filedialog.asksaveasfilename(
            title="Save Patched A Firmware",
            defaultextension=".bin",
            filetypes=[("BIN files", "*.bin"), ("All files", "*.*")],
            initialdir=str(out_dir),
            initialfile=f"{stem}_patched_{ts}.bin")
        if path:
            self.fw_a.save(path)
            log = self._auto_export_log_a(path)
            msg = f"Patched A firmware saved to:\n{path}"
            if log:
                msg += f"\n\nPatch log:\n{log}"
            messagebox.showinfo("Saved", msg)

    def _save_current(self):
        """Ctrl+S handler — save the currently active board."""
        try:
            sel = self.board_nb.index(self.board_nb.select())
        except Exception:
            return
        if sel == 0:
            self._save_b_mcu()
        elif sel == 2:
            self._save_b040_flash()
        else:
            self._save_a()

    def _save_b040_flash(self):
        if not self.fw_040b.data:
            return
        if self.fw_040b.diff_count() == 0:
            messagebox.showinfo("Info",
                                "No 040 B board modifications to save.")
            return
        out_dir = self._get_output_dir()
        out_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        stem = Path(self.fw_040b.path).stem
        path = filedialog.asksaveasfilename(
            title="Save Patched 040 B Flash",
            defaultextension=".bin",
            filetypes=[("Binary files", "*.bin")],
            initialdir=str(out_dir),
            initialfile=f"{stem}_patched_{ts}.bin")
        if path:
            self.root.config(cursor="watch")
            self.root.update()
            try:
                self.fw_040b.save_flash(path)
            finally:
                self.root.config(cursor="")
            log = self._auto_export_log_b040(path)
            msg = f"Patched 040 B flash saved to:\n{path}"
            if log:
                msg += f"\n\nPatch log:\n{log}"
            messagebox.showinfo("Saved", msg)

    def _auto_export_log_b040(self, bin_path: str) -> Optional[str]:
        if not self.fw_040b.mods:
            return None
        log_path = Path(bin_path).with_suffix(".txt")
        self._write_b040_log(str(log_path))
        return str(log_path)

    def _export_b040_log(self):
        if not self.fw_040b.mods:
            messagebox.showinfo("Info",
                                "No 040 B board modifications to export.")
            return
        out_dir = self._get_output_dir()
        out_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = filedialog.asksaveasfilename(
            title="Export 040 B Patch Log",
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt")],
            initialdir=str(out_dir),
            initialfile=f"040B_patch_log_{ts}.txt")
        if path:
            self._write_b040_log(path)
            messagebox.showinfo("Exported", f"Log saved to:\n{path}")

    def _write_b040_log(self, path: str):
        fw = self.fw_040b
        with open(path, "w") as f:
            f.write("TW8836 (040 B Board) Firmware Patch Log\n")
            f.write(f"{'='*60}\n")
            f.write(f"Generated:       "
                    f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"Source:          {fw.path}\n")
            f.write(f"Header SHA256:   {fw.sha_orig()}\n")
            f.write(f"Bytes changed:   {fw.diff_count()}\n\n")
            for i, mod in enumerate(fw.mods, 1):
                f.write(f"[{i}] {mod.desc}\n")
                f.write(f"    Decoded offset: 0x{mod.offset:06X}\n")
                f.write(f"    Old: {mod.old.hex(' ')}\n")
                f.write(f"    New: {mod.new.hex(' ')}\n\n")

    def _auto_export_log_b(self, bin_path: str) -> Optional[str]:
        if not self.fw_b.mods:
            return None
        log_path = Path(bin_path).with_suffix(".txt")
        self._write_b_log(str(log_path))
        return str(log_path)

    def _auto_export_log_a(self, bin_path: str) -> Optional[str]:
        if not self.fw_a.mods:
            return None
        log_path = Path(bin_path).with_suffix(".txt")
        self._write_a_log(str(log_path))
        return str(log_path)

    def _export_b_log(self):
        if not self.fw_b.mods:
            messagebox.showinfo("Info", "No B board modifications to export.")
            return
        out_dir = self._get_output_dir()
        out_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = filedialog.asksaveasfilename(
            title="Export B Patch Log",
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt")],
            initialdir=str(out_dir),
            initialfile=f"B_patch_log_{ts}.txt")
        if path:
            self._write_b_log(path)
            messagebox.showinfo("Exported", f"Log saved to:\n{path}")

    def _export_a_log(self):
        if not self.fw_a.mods:
            messagebox.showinfo("Info", "No A board modifications to export.")
            return
        out_dir = self._get_output_dir()
        out_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = filedialog.asksaveasfilename(
            title="Export A Patch Log",
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt")],
            initialdir=str(out_dir),
            initialfile=f"A_patch_log_{ts}.txt")
        if path:
            self._write_a_log(path)
            messagebox.showinfo("Exported", f"Log saved to:\n{path}")

    def _write_b_log(self, path: str):
        with open(path, "w") as f:
            f.write(f"TW8836 (B Board) Firmware Patch Log\n")
            f.write(f"{'='*60}\n")
            f.write(f"Generated:       "
                    f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"Source:          {self.fw_b.path}\n")
            f.write(f"Original SHA256: {self.fw_b.sha_orig()}\n")
            f.write(f"Patched SHA256:  {self.fw_b.sha()}\n")
            f.write(f"Bytes changed:   {self.fw_b.diff_count()}\n\n")
            for i, mod in enumerate(self.fw_b.mods, 1):
                f.write(f"[{i}] {mod.desc}\n")
                f.write(f"    Offset: 0x{mod.offset:05X}\n")
                f.write(f"    Old: {mod.old.hex(' ')}\n")
                f.write(f"    New: {mod.new.hex(' ')}\n\n")

    def _write_a_log(self, path: str):
        fw = self.fw_a
        n, t, pct = fw.panel_coverage()
        with open(path, "w") as f:
            f.write(f"MK22F (A Board) Firmware Patch Log\n")
            f.write(f"{'='*60}\n")
            f.write(f"Generated:       "
                    f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"Source:          {fw.path}\n")
            f.write(f"Original SHA256: {fw.sha_orig()}\n")
            f.write(f"Patched SHA256:  {fw.sha()}\n")
            f.write(f"Bytes changed:   {fw.diff_count()}\n")
            f.write(f"Panel coverage:  {n}/{t} = {pct:.1f}%\n\n")
            for i, mod in enumerate(fw.mods, 1):
                f.write(f"[{i}] {mod.desc}\n")
                f.write(f"    File offset: 0x{mod.file_offset:05X}\n")
                f.write(f"    Old: 0x{mod.old:02X}  New: 0x{mod.new:02X}\n\n")

    # ══════════════════════════════════════════════════════════════════
    #  PRESETS
    # ══════════════════════════════════════════════════════════════════

    def _reset_b(self):
        if not self.fw_b.data:
            return
        if messagebox.askyesno("Confirm",
                               "Reset all B board changes to stock?"):
            self.fw_b.reset_all()
            self._refresh_b_all()

    def _reset_a(self):
        if not self.fw_a.raw:
            return
        if messagebox.askyesno("Confirm",
                               "Reset all A board changes to stock?"):
            self.fw_a.reset_all()
            self._refresh_a_all()

    def _reset_b040(self):
        if not self.fw_040b.data:
            return
        if messagebox.askyesno("Confirm",
                               "Reset all 040 B board changes to stock?"):
            self.fw_040b.reset_all()
            self._refresh_b040_all()

    def _preset_b040_delays(self, factor: float):
        if not self.fw_040b.data:
            messagebox.showwarning("Warning", "No 040 B firmware loaded.")
            return
        for dc in self.fw_040b.delays:
            if dc.original_ms >= 100:
                self.fw_040b.set_delay(
                    dc, max(5, int(dc.original_ms * factor)))
        self._refresh_b040_all()

    def _reset_b_delays(self):
        if not self.fw_b.data:
            return
        self.fw_b.reset_all()
        self._refresh_b_all()

    def _bulk_b_delay(self, factor: float):
        if not self.fw_b.data:
            return
        for dc in self.fw_b.delays:
            if dc.value_ms >= 100:
                self.fw_b.set_delay(dc, max(10, int(dc.original_ms * factor)))
        self._refresh_b_delays()
        self._update_status()

    def _preset_b_delays(self, factor: float):
        if not self.fw_b.data:
            return
        for dc in self.fw_b.delays:
            if dc.original_ms >= 50:
                self.fw_b.set_delay(dc,
                                     max(5, int(dc.original_ms * factor)))
        self._refresh_b_all()

    def _preset_b_sharp(self):
        if not self.fw_b.data:
            return
        for entry in self.fw_b.init_regs:
            if entry.reg == 0x112:
                self.fw_b.set_init_reg(entry, 0x1F)
            elif entry.reg == 0x111:
                self.fw_b.set_init_reg(entry, 0x70)
        self._refresh_b_all()

    # ══════════════════════════════════════════════════════════════════
    #  REFRESH
    # ══════════════════════════════════════════════════════════════════

    def _refresh_b_all(self):
        self._refresh_b_overview()
        self._refresh_b_delays()
        self._refresh_b_imgq()
        self._refresh_b_regs()
        self._refresh_b_hex()
        self._refresh_b_patch()
        self._update_status()

    def _refresh_a_all(self):
        self._refresh_a_overview()
        self._refresh_a_timing()
        self._refresh_a_panel()
        self._refresh_a_strings()
        self._refresh_a_hex()
        self._refresh_a_patch()
        self._update_status()

    # ══════════════════════════════════════════════════════════════════
    #  RUN
    # ══════════════════════════════════════════════════════════════════

    def run(self):
        self.root.mainloop()


# ════════════════════════════════════════════════════════════════════════
# Entry Point
# ════════════════════════════════════════════════════════════════════════

def main():
    a_path = ""
    b_path = ""

    args = sys.argv[1:]
    if "--a" in args:
        i = args.index("--a")
        if i + 1 < len(args):
            a_path = args[i + 1]
    if "--b" in args:
        i = args.index("--b")
        if i + 1 < len(args):
            b_path = args[i + 1]

    # Auto-detect firmware files
    if not b_path:
        for c in ["firmware/SKY04X_Pro_B_APP_V4.0.2.bin",
                   "firmware/fix/SKY04X_Pro_B_APP_V4.0.2.bin",
                   "tw8836_mcu.bin"]:
            if os.path.isfile(c):
                b_path = c
                break
    if not a_path:
        for c in ["firmware/SKY04XPro_A_APP_V4.1.7.bin",
                   "firmware/sky04xpro_v4.1.7/SKY04XPro_A_APP_V4.1.7.bin",
                   "firmware/SKY04XPro_A_APP_V4.1.6.bin",
                   "firmware/fix/SKY04XPro_A_APP_V4.1.6.bin"]:
            if os.path.isfile(c):
                a_path = c
                break

    app = App(a_path, b_path)
    app.run()


if __name__ == "__main__":
    main()
