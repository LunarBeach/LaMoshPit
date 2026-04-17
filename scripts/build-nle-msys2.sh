#!/usr/bin/env bash
#
# Configure + build LaMoshPit_NLE.exe from the MSYS2 MINGW64 shell.
#
# Usage:
#   bash scripts/build-nle-msys2.sh            # Release, incremental
#   bash scripts/build-nle-msys2.sh -d         # Debug, incremental
#   bash scripts/build-nle-msys2.sh --clean    # wipe build tree first
#
# Output: nle/build/LaMoshPit_NLE.exe
#
# Part of Phase 1 Step 2 of the NLE rebuild.
#
set -euo pipefail

if [[ "${MSYSTEM:-}" != "MINGW64" ]]; then
    echo "ERROR: this script must be run from the MSYS2 MINGW64 shell." >&2
    echo "Current MSYSTEM=${MSYSTEM:-<unset>}" >&2
    exit 1
fi

# Resolve repo root from the script's location so this works regardless
# of where the user cd'd to before invoking it.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
NLE_DIR="$REPO_ROOT/nle"
BUILD_DIR="$NLE_DIR/build"

BUILD_TYPE="Release"
DO_CLEAN=0

for arg in "$@"; do
    case "$arg" in
        -d|--debug)  BUILD_TYPE="Debug"  ;;
        -r|--release) BUILD_TYPE="Release" ;;
        --clean) DO_CLEAN=1 ;;
        -h|--help)
            sed -n '1,/^$/p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "Unknown option: $arg" >&2
            exit 2
            ;;
    esac
done

if [[ $DO_CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
    echo "== Wiping $BUILD_DIR =="
    rm -rf "$BUILD_DIR"
fi

cd "$NLE_DIR"

echo "== Configuring ($BUILD_TYPE) =="
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "== Building =="
cmake --build build

echo ""
echo "=== Done. ==="
echo "Output: $BUILD_DIR/LaMoshPit_NLE.exe"
echo ""
echo "Run from the MINGW64 shell (required so Qt6 DLLs are on PATH):"
echo "  ./build/LaMoshPit_NLE.exe"
