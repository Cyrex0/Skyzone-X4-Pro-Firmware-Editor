# Skyzone SKY04X Pro Firmware Editor

Visual firmware editor for the **Skyzone SKY04X Pro** FPV goggles. Modify timing delays, frame rate, image quality, panel initialization, and register tables through an intuitive dark-themed GUI — no hex editor required.

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
- **⏱️ Frame Timing** — Adjust OLED refresh rate with one-click presets for **100 fps** (stock), **120 fps**, and **144 fps**. Patches MOVW Thumb-2 frame-period immediates.
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
   - `SKY04X_Pro_B_APP_V4.0.2.bin` — B board MCU binary
   - `SKY04XPro_A_APP_V4.1.6.bin` or `SKY04XPro_A_APP_V4.1.7.bin` — A board firmware
4. Run `skyzone_editor.exe`

### Option B: Run from Python
```bash
pip install tk   # usually included with Python

python skyzone_editor.py

# Or specify firmware paths explicitly
python skyzone_editor.py --b firmware/SKY04X_Pro_B_APP_V4.0.2.bin --a firmware/SKY04XPro_A_APP_V4.1.7.bin
```

---

## Frame Timing / FPS Presets

The A board controls the OLED panel refresh rate via frame-period values stored as MOVW Thumb-2 immediates. The editor scans for these and provides one-click presets:

| Preset | Frame Period (µs) | Refresh Rate |
|--------|-------------------|--------------|
| Stock  | 10,000            | 100 fps      |
| 120fps | 8,333             | ≈120 fps     |
| 144fps | 6,944             | ≈144 fps     |

The timing tab patches the first two frame-period sites (which control the actual display timing). V4.1.6 ships with 144 fps at these sites by default; V4.1.7 reverted to 100 fps.

---

## A Board XOR Encoding

The A board firmware payload is XOR-encoded with a **per-block key schedule**:

```
key = (0x55 + floor(payload_offset / 512)) & 0xFF
```

Each 512-byte block uses an incrementing key starting from `0x55`. The editor handles this transparently — all UI values show decoded data, and saves re-encode correctly.

---

## Building the .exe

```bash
pip install pyinstaller
python build.py
```

This creates a `release/` folder containing:
```
release/
├── skyzone_editor.exe            # Standalone executable
├── skyzone_editor.py             # Python source
├── firmware/                     # Stock firmware files (if present)
│   ├── SKY04X_Pro_B_APP_V4.0.2.bin
│   ├── SKY04XPro_A_APP_V4.1.6.bin
│   └── SKY04XPro_A_APP_V4.1.7.bin
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
skyzone_editor/
├── skyzone_editor.py    # Main editor application
├── build.py             # PyInstaller build script
├── firmware/            # Stock firmware files
├── patched/             # Saved outputs (auto-created)
│   └── backups/         # Stock backups (auto-created)
├── .gitignore
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

## License

MIT

---

## Disclaimer

This tool is provided as-is for educational and hobbyist use. Modifying firmware may void your warranty and could potentially damage your hardware. Always keep a backup of your stock firmware. Use at your own risk.
