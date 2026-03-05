# Skyzone Firmware Editor

Visual firmware editor for the **Skyzone SKY04X Pro** and **SKY040 Pro** FPV goggles. Modify frame rate, panel initialization, version branding, image quality, timing delays, and register tables through an intuitive ImGui interface — no hex editor required.

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)
![ImGui](https://img.shields.io/badge/GUI-Dear%20ImGui-blue)
![Platform](https://img.shields.io/badge/Platform-Windows-0078d4?logo=windows)
![License](https://img.shields.io/badge/License-MIT-green)

Built with **Dear ImGui 1.91 + SDL2 + OpenGL3**. Single ~826 KB executable, no installation needed.

---

## Features

### A Board — MK22FN256 (ARM Cortex-M4 Display Controller)
- **Frame Timing** — Adjust OLED refresh rate with one-click presets for **100 fps** (stock), **120 fps**, and **144 fps**. Patches MOVW Thumb-2 frame-period immediates in-place.
- **Version / Branding** — Edit firmware version strings embedded in the binary so the OSD shows custom firmware identification (e.g. ``SKY04X MOD``, ``4.1.7LL``). Quick presets for MOD, Low Latency, and Custom with per-field character limits and live validation.
- **Panel Init Tables** — Edit OLED panel driver initialization values (170+ register entries). Double-click any value to modify.
- **Register Reference** — Complete read-only database for all four A board ICs:
  - Panel OLED Driver (75 regs)
  - LT9211 MIPI-to-LVDS Bridge (106 regs)
  - IT6802 HDMI Receiver (26 regs)
  - MK22F ARM Peripherals (66 regs)
- **Strings** — View all embedded strings with code/data section tagging and filter.
- **Hex Viewer** — Decoded or raw (XOR-encoded) view with quick jumps to vector table, panel tables, and data section.
- **Patch Output** — See a full diff of every changed byte before saving.

### B Board — TW8836 (8051 Video Processor)
- **Delay Editor** — Find and adjust every ``delay1ms()`` call site with sliders. Presets for conservative (0.5x) and aggressive (0.1x) tuning.
- **Image Quality** — Slider controls for brightness, contrast, sharpness, saturation, color balance, and comb filter settings.
- **Register Table** — Browse and edit the complete initialization register table (245+ documented registers).
- **Hex Viewer** — Navigate the binary with diff highlighting for modified bytes and direct byte editing.

### 040 Pro Support
- Auto-detects **SKY040 Pro** B board firmware (0x10 header ID) alongside X4 Pro.
- A board firmware works for both models — same MK22F platform, same XOR encoding.

### Disassembler / Decompiler
- **8051 Disassembler** — Full disassembly of B board TW8836 MCU code with annotations and cross-references.
- **ARM Thumb Disassembler** — Disassemble A board MK22F firmware with symbol resolution.
- **Decompiler** — Experimental C-like decompilation output for both architectures.
- **Annotations** — Add custom labels, comments, and bookmarks to any address.

### General
- Dark theme (Catppuccin Mocha)
- Auto-backup on first load (stock copy saved to ``patched/backups/``)
- Timestamped saves to ``patched/`` folder — never overwrites stock firmware
- Companion patch logs — human-readable ``.txt`` alongside every saved binary
- Native file dialogs — standard Windows Open/Save dialogs
- Instant startup — ~826 KB standalone ``.exe``, no runtime dependencies

---

## Quick Start

1. Download the latest release ``.zip``
2. Extract to a folder
3. Place your stock firmware files in the ``firmware/`` subfolder:
   - ``firmware/04Xpros/`` — X4 Pro firmware files
   - ``firmware/040pro/`` — 040 Pro firmware files
4. Run ``SkyZoneEditor.exe``
5. Use **File > Open A Firmware** / **Open B Firmware** to load files

### Supported Firmware Files

| File | Board | Model |
|------|-------|-------|
| ``SKY04XPro_A_APP_V4.1.7.bin`` | A Board | X4 Pro |
| ``SKY04XPro_A_APP_V4.1.6.bin`` | A Board | X4 Pro |
| ``SKY04X_Pro_B_APP_V4.0.2.bin`` | B Board | X4 Pro |
| ``SKY04O_Pro_A_APP_V1.1.4.bin`` | A Board | 040 Pro |
| ``SKY04O_Pro_B_APP_V1.1.1.bin`` | B Board | 040 Pro |

---

## Frame Timing / FPS Presets

The A board controls the OLED panel refresh rate via frame-period values stored as MOVW Thumb-2 immediates. The editor scans for these and provides one-click presets:

| Preset | Frame Period (us) | Refresh Rate |
|--------|-------------------|--------------|
| Stock  | 10,000            | 100 fps      |
| 120fps | 8,333             | ~120 fps     |
| 144fps | 6,944             | ~144 fps     |

V4.1.6 ships with 144 fps by default; V4.1.7 reverted to 100 fps stock.

---

## Version / Branding

The A board firmware contains null-terminated version strings displayed on the goggle OSD. The editor finds these automatically and lets you modify them to identify custom firmware:

| Field | Stock Value | Max Chars | Example Patched |
|-------|-------------|-----------|-----------------|
| Firmware Version | ``4.1.7`` | 7 | ``4.1.7M`` |
| Version Prefix | ``V4`` | 3 | ``V4`` |
| Model Name | ``SKY04X PRO`` | 11 | ``SKY04X MOD`` |
| Software Version | ``V3.4.8`` | 8 | ``V3.4.8`` |

Quick preset buttons apply common branding patterns (MOD, LL, CUSTOM) with a single click. All changes respect the original string's allocated space — strings cannot exceed their maximum length.

---

## A Board XOR Encoding

The A board firmware payload (starting at offset ``0x210``) is XOR-encoded with a per-block key schedule:

```
key = (0x55 + floor(payload_offset / 512)) & 0xFF
```

Each 512-byte block uses an incrementing key starting from ``0x55``. The editor handles this transparently — all UI values show decoded data, and saves re-encode correctly.

---

## Video Pipeline

```
                    A Board (MK22F ARM)                      B Board (TW8836 8051)
+----------+    +----------+    +----------+    +-----------------------------------+
| HDMI Src |--->| IT6802   |--->| LT9211   |--->| TW8836 - Scaler/OSD/Decoder      |
|          |    | HDMI Rx  |    | LVDS Brg |    |                                   |
+----------+    +----------+    +----------+    +----------+------------------------+
                                                           |
                                                    +------+------+
                                                    |  OLED Panel  |
                                                    | (0x4C / 0x4D)|
                                                    +--------------+
```

---

## Building from Source

### Prerequisites
- **CMake** 3.20+
- **Visual Studio 2022** (or any C++20 compiler)
- No pre-installed libraries needed — SDL2 and ImGui are fetched automatically via CMake FetchContent

### Build
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The built executable and ``SDL2.dll`` are placed in ``build/Release/``. Copy both to the project root to run.

---

## Project Structure

```
Skyzone-X4-Pro-Firmware-Editor/
|-- src/
|   |-- main.cpp              # Entry point, ImGui/SDL2/OpenGL setup
|   |-- firmware.cpp/.h       # B board parser (TW8836 8051)
|   |-- firmware_a.cpp/.h     # A board parser (MK22F ARM, XOR decode)
|   |-- ui_editor.cpp         # Editor UI (all tabs, version branding)
|   |-- ui_disasm.cpp         # Disassembler/decompiler UI
|   |-- disasm.cpp/.h         # 8051 + ARM Thumb disassemblers
|   |-- decompile.cpp/.h      # Experimental decompiler
|   +-- annotations.cpp/.h    # User annotations database
|-- firmware/
|   |-- 04Xpros/              # X4 Pro firmware files
|   +-- 040pro/               # 040 Pro firmware files
|-- patched/                   # Saved outputs (auto-created)
|   +-- backups/               # Stock backups (auto-created)
|-- CMakeLists.txt             # CMake build (fetches SDL2 + ImGui)
|-- SkyZoneEditor.exe          # Pre-built Windows executable
|-- SDL2.dll                   # SDL2 runtime
+-- README.md
```

---

## Contributing

Pull requests welcome. Areas of interest:
- Additional register documentation for panel drivers
- Preset profiles for different panel types
- Linux/macOS build testing
- More firmware versions / models
- Decompiler improvements

---

## License

MIT

---

## Disclaimer

This tool is provided as-is for educational and hobbyist use. Modifying firmware may void your warranty and could potentially damage your hardware. Always keep a backup of your stock firmware. Use at your own risk.