#!/usr/bin/env bash
#
# Installs the MSYS2/MinGW64 package set needed to build LaMoshPit_NLE.exe.
# Run from the MSYS2 MINGW64 shell (NOT the plain MSYS shell, NOT cmd.exe).
#
# Safe to re-run — pacman's --needed flag skips already-installed packages.
#
# Part of Phase 1 Step 1 of the NLE rebuild.  See
#   docs/NLE_MSYS2_Setup.md
# for the full procedure this script fits into.
#
set -euo pipefail

# Sanity check: are we actually running under MINGW64?
if [[ "${MSYSTEM:-}" != "MINGW64" ]]; then
    echo "ERROR: this script must be run from the MSYS2 MINGW64 shell." >&2
    echo "Current MSYSTEM=${MSYSTEM:-<unset>}" >&2
    echo "Launch 'MSYS2 MINGW64' from the Start Menu and re-run from there." >&2
    exit 1
fi

echo "=== Step 1/2: updating base MSYS2 ==="
# First invocation updates pacman itself + base runtime.  May trigger a
# "please close the shell and reopen it" notice; if so, re-run this script.
pacman -Syu --noconfirm

echo "=== Step 2/2: installing MinGW64 toolchain + Qt6 + MLT ==="
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-toolchain \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ninja \
    mingw-w64-x86_64-pkgconf \
    mingw-w64-x86_64-qt6-base \
    mingw-w64-x86_64-qt6-multimedia \
    mingw-w64-x86_64-qt6-quickcontrols2 \
    mingw-w64-x86_64-qt6-declarative \
    mingw-w64-x86_64-qt6-svg \
    mingw-w64-x86_64-qt6-tools \
    mingw-w64-x86_64-mlt \
    mingw-w64-x86_64-ffmpeg \
    mingw-w64-x86_64-fftw \
    mingw-w64-x86_64-libxml2 \
    mingw-w64-x86_64-sdl2 \
    git

echo ""
echo "=== Done. ==="
echo "Versions installed:"
g++ --version | head -1
cmake --version | head -1
pkgconf --modversion mlt++-7 2>/dev/null | sed 's/^/MLT++: /' || echo "MLT++: NOT FOUND (check install)"
pkgconf --modversion Qt6Core   2>/dev/null | sed 's/^/Qt6: /' || echo "Qt6: NOT FOUND"
echo ""
echo "Next step: build the sanity-test:"
echo "  cd scripts/nle-sanity-test"
echo "  cmake -G Ninja -B build"
echo "  cmake --build build"
echo "  ./build/nle-test.exe"
