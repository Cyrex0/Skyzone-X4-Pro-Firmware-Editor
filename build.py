#!/usr/bin/env python3
"""
Build script for Skyzone SKY04X Pro Firmware Editor.

Packages skyzone_editor.py into a standalone Windows .exe via PyInstaller.

Usage:
    python build.py
"""

import os
import sys
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DIST_DIR = ROOT / "dist"
BUILD_DIR = ROOT / "build"
EDITOR_PY = ROOT / "skyzone_editor.py"

FIRMWARE_FILES = [
    "firmware/SKY04X_Pro_B_APP_V4.0.2.bin",
    "firmware/SKY04XPro_A_APP_V4.1.6.bin",
    "firmware/SKY04XPro_A_APP_V4.1.7.bin",
]


def check_pyinstaller():
    try:
        import PyInstaller  # noqa: F401
        print("[OK] PyInstaller found.")
    except ImportError:
        print("[!]  PyInstaller not found. Installing...")
        subprocess.check_call([sys.executable, "-m", "pip", "install",
                               "pyinstaller"])
        print("[OK] PyInstaller installed.")


def build_exe():
    print("\n=== Building skyzone_editor.exe ===\n")
    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--onefile",
        "--noconsole",
        "--name", "skyzone_editor",
        "--distpath", str(DIST_DIR),
        "--workpath", str(BUILD_DIR),
        "--specpath", str(BUILD_DIR),
        "--clean",
        str(EDITOR_PY),
    ]
    subprocess.check_call(cmd, cwd=str(ROOT))
    exe = DIST_DIR / "skyzone_editor.exe"
    if not exe.exists():
        print("[ERROR] Build failed — exe not found.")
        sys.exit(1)
    print(f"\n[OK] Built: {exe}  ({exe.stat().st_size:,} bytes)\n")
    return exe


def assemble_release(exe: Path):
    release = ROOT / "release"
    print("=== Assembling release folder ===\n")
    if release.exists():
        shutil.rmtree(release)
    release.mkdir()

    # exe
    shutil.copy2(str(exe), str(release / exe.name))
    print(f"  + {exe.name}")

    # firmware
    fw_dir = release / "firmware"
    fw_dir.mkdir()
    for rel in FIRMWARE_FILES:
        src = ROOT / rel
        if src.exists():
            shutil.copy2(str(src), str(fw_dir / src.name))
            print(f"  + firmware/{src.name}")
        else:
            print(f"  [SKIP] {src} not found")

    # source
    shutil.copy2(str(EDITOR_PY), str(release / EDITOR_PY.name))
    print(f"  + {EDITOR_PY.name}")

    # README
    readme = ROOT / "README.md"
    if readme.exists():
        shutil.copy2(str(readme), str(release / "README.md"))
        print("  + README.md")

    print(f"\n[OK] Release folder: {release}\n")
    print("Contents:")
    for p in sorted(release.rglob("*")):
        if p.is_file():
            rel_path = p.relative_to(release)
            print(f"  {rel_path}  ({p.stat().st_size:,} bytes)")


def main():
    print("Skyzone Editor Build Script")
    print(f"Root: {ROOT}\n")
    check_pyinstaller()
    exe = build_exe()
    assemble_release(exe)
    print("\n✅ Done!  The release/ folder is ready to zip and upload.")


if __name__ == "__main__":
    main()
