# Skyzone SKY04X Pro Firmware Editor

Visual firmware editor for the **Skyzone SKY04X Pro** FPV goggles. Modify timing delays, image quality, panel initialization, and register tables through an intuitive dark-themed GUI — no hex editor required.

![Python 3.10+](https://img.shields.io/badge/Python-3.10%2B-blue?logo=python)
![Platform](https://img.shields.io/badge/Platform-Windows-0078d4?logo=windows)
![License](https://img.shields.io/badge/License-MIT-green)

---

## Features

### B Board — TW8836 (8051 Video Processor)
- **⏱ Delay Editor** — Find and adjust every `delay1ms()` call site with sliders. Presets for conservative (0.5×) and aggressive (0.1×) tuning.
- **🎨 Image Quality** — Slider controls for brightness, contrast, sharpness, saturation, color balance, and comb filter settings.
- **📋 Register Table** — Browse and edit the complete initialization register table (245+ documented registers).
- **💾 Hex Viewer** — Navigate the binary with quick-jump buttons, diff highlighting for modified bytes, and direct byte editing.

### A Board — MK22FN256 (ARM Cortex-M4 Display Controller)
- **🔧 Panel Init Tables** — Edit OLED panel driver initialization values (170 register entries, 100% coverage). Double-click to modify.
- **📖 Register Reference** — Complete read-only database for all four ICs:
  - Panel OLED Driver (75 regs)
  - LT9211 LVDS Bridge (106 regs)
  - IT6802 HDMI Receiver (26 regs)
  - MK22F Peripherals (66 regs)
- **📝 Strings** — View all embedded strings with code/data section tagging.
- **💾 Hex Viewer** — Decoded or raw (XOR-encoded) view with quick jumps to vector table, panel tables, and data section.

### Shared
- 🌙 **Dark theme** (Catppuccin Mocha)
- 💾 **Auto-backup** on first load (stock copy saved to `patched/backups/`)
- 📦 **Timestamped saves** to `patched/` folder — never overwrites stock firmware
- 📝 **Companion patch logs** — human-readable `.txt` alongside every saved binary
- 🖱️ **Fully scrollable UI** — mousewheel works everywhere, even with many entries

---

## Quick Start

### Option A: Download the .exe (Windows)
1. Download the latest release `.zip`
2. Extract to a folder
3. Place your stock firmware files in the `firmware/` subfolder (or next to the `.exe`):
   - `tw8836_mcu.bin` — B board MCU binary
   - `SKY04XPro_A_APP_V4.1.4.bin` — A board firmware
4. Run `skyzone_editor.exe`

### Option B: Run from Python
```bash
# Clone / download this repo
pip install tk   # usually included with Python

# Run from the project root
python tools/skyzone_editor.py

# Or specify firmware paths explicitly
python tools/skyzone_editor.py --b tw8836_mcu.bin --a firmware/A_APP.bin
```

---

## Firmware Files

| File | Board | MCU | Size | Description |
|------|-------|-----|------|-------------|
| `tw8836_mcu.bin` | B | TW8836 (8051) | 204,800 B | Video processor, OSD, analog decoder |
| `SKY04XPro_A_APP_V4.1.4.bin` | A | MK22FN256 (ARM) | ~222 KB | Display controller, HDMI Rx, LVDS bridge |

**⚠️ Firmware files are NOT included in this repo.** Extract them from your goggles' SPI flash or obtain from Skyzone support. The editor will auto-detect files placed in the project directory.

---

## Building the .exe

```bash
# From the project root
pip install pyinstaller
python build_release.py
```

This creates a `release/` folder containing:
```
release/
├── skyzone_editor.exe      # Standalone executable
├── firmware/               # Stock firmware files (if present)
│   ├── tw8836_mcu.bin
│   └── SKY04XPro_A_APP_V4.1.4.bin
├── tools/
│   └── skyzone_editor.py   # Python source
└── README.md
```

---

## Video Pipeline

```
                    A Board (MK22F ARM)                      B Board (TW8836 8051)
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────────────────────────────┐
│ HDMI Src │───→│ IT6802   │───→│ LT9211   │───→│ TW8836 — Scaler/OSD/Decoder     │
│          │    │ HDMI Rx  │    │ LVDS Brg  │    │                                  │
└──────────┘    └──────────┘    └──────────┘    └──────────┬───────────────────────┘
                                                           │
                                                    ┌──────┴──────┐
                                                    │  OLED Panel  │
                                                    │ (0x4C / 0x4D)│
                                                    └─────────────┘
```

---

## Project Structure

```
skyzone_re/
├── tools/
│   ├── skyzone_editor.py    # ← Unified editor (this project)
│   ├── fw_editor.py         # B board standalone editor
│   └── fw_editor_a.py       # A board standalone editor
├── firmware/                # Stock firmware files
├── patched/                 # Saved outputs (auto-created)
│   └── backups/             # Stock backups (auto-created)
├── build_release.py         # PyInstaller build script
└── README.md
```

---

## Contributing

Pull requests welcome. Areas of interest:
- Additional register documentation
- Preset profiles for different panel types
- Linux/macOS testing
- Firmware comparison tools

---

## Disclaimer

This tool is provided as-is for educational and hobbyist use. Modifying firmware may void your warranty and could potentially damage your hardware. Always keep a backup of your stock firmware. Use at your own risk.
