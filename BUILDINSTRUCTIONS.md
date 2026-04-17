# LaMoshPit — Build Instructions

LaMoshPit is a **two-process** application. Each process has its own
build toolchain; they never share compiled code.

| Process | Role | Toolchain | Section below |
|---|---|---|---|
| `LaMoshPit.exe` | MB editor, Global Encode Params, MediaBin, Quick Mosh | **MSVC** (Visual Studio 2022) + vcpkg | §1–§4 |
| `LaMoshPit_NLE.exe` | NLE sequencer (MLT-based) | **MSYS2/MinGW64** | §5 |

For LaMoshPit.exe you use the existing PowerShell + VS-bundled CMake
recipe. For LaMoshPit_NLE.exe you use an MSYS2 MINGW64 shell. Never
try to build one with the other toolchain.

## §1 — Environment (LaMoshPit.exe / MSVC side)

- **Platform**: Windows 11
- **Compiler**: MSVC (Visual Studio 2022 Community)
- **Build system**: CMake (bundled with VS2022 — NOT on PATH)
- **CMake binary**: `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- **Build directory**: `<repo root>\build\` (pre-configured, do not re-run CMake configure)

## Re-configure (required after CMakeCache.txt deletion or first clone)

```powershell
powershell -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S 'C:\Users\Thelu\Desktop\CodingProjects\LaMoshPit' -B 'C:\Users\Thelu\Desktop\CodingProjects\LaMoshPit\build' -DCMAKE_TOOLCHAIN_FILE='C:\Users\Thelu\Desktop\CodingProjects\LaMoshPit\vcpkg\scripts\buildsystems\vcpkg.cmake' -DVCPKG_INSTALLED_DIR='C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/vcpkg/installed' -DVCPKG_TARGET_TRIPLET=x64-windows 2>&1 | Out-File -FilePath 'C:\Users\Thelu\Desktop\CodingProjects\LaMoshPit\build\configure_log.txt' -Encoding UTF8; Write-Host 'exit:' $LASTEXITCODE"
```

After configure, run a build as normal. You only need to re-configure if you delete `build\CMakeCache.txt` or add new source files to CMakeLists.txt (though source file additions now auto-configure since FindFFMPEG paths are pre-seeded in CMakeLists.txt itself).

## Running a Build

Always use PowerShell and the full cmake path. `cmake` is not on PATH in any shell.

### Release

```powershell
powershell -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build 'C:\Users\Thelu\Desktop\CodingProjects\LaMoshPit\build' --config Release 2>&1 | Out-File -FilePath 'C:\Users\Thelu\Desktop\CodingProjects\LaMoshPit\build\build_log.txt' -Encoding UTF8; Write-Host 'exit:' $LASTEXITCODE"
```

### Debug

```powershell
powershell -Command "& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build 'C:\Users\Thelu\Desktop\CodingProjects\LaMoshPit\build' --config Debug 2>&1 | Out-File -FilePath 'C:\Users\Thelu\Desktop\CodingProjects\LaMoshPit\build\build_log_debug.txt' -Encoding UTF8; Write-Host 'exit:' $LASTEXITCODE"
```

Then read the result:

```powershell
powershell -Command "Get-Content 'C:\Users\Thelu\Desktop\CodingProjects\LaMoshPit\build\build_log.txt' | Select-Object -Last 20"
```

## Output Binaries

| Config   | Path                                          |
|----------|-----------------------------------------------|
| Release  | `build\Release\LeeAnnesMoshPit.exe`           |
| Debug    | `build\Debug\LeeAnnesMoshPit.exe`             |

Always build **both** Release and Debug after non-trivial changes and verify both link cleanly.

## Reading the Build Log

- **Errors** appear as `error C####:` — these block the build.
- **Warnings** from `h264bitstream\` and `svc_split.c` are third-party noise; ignore them.
- **Warnings** from our source files (`gui\`, `core\`) should be investigated.
- A successful build ends with a line like:
  `LeeAnnesMoshPit.vcxproj -> ...\build\Release\LeeAnnesMoshPit.exe`
- The `nodiscard` warning in `ControlLogger.cpp` is pre-existing and benign.

## Known Shell Pitfalls

| What fails | Why | Use instead |
|---|---|---|
| `cmake --build ...` in bash | `cmake` not on PATH | PowerShell + full path above |
| `cmd /c "cmake ..."` | Output doesn't pipe back to bash | PowerShell + `Out-File` |
| Regex with `\|` in PowerShell one-liners | Shell escaping conflicts | Write output to file, then read |
| `\xNN` followed immediately by hex digits in C++ string literals | MSVC reads ALL following hex digits as one escape, not just 2 | Split with adjacent string literal: `"\xNN" "restofstring"` |

## C++ String Literal Gotcha (MSVC)

MSVC's `\x` hex escapes consume every following hex digit, not just two.
This silently corrupts values and causes `C2022: 'NNNNN': too big for character`.

**Bad** — `\x97` followed by `16` (both hex digits):
```cpp
"16\xc3\x9716 partitions"   // MSVC reads \x9716 = 38678, error
```

**Good** — break the adjacency with string concatenation:
```cpp
"16" "\xc3\x97" "16 partitions"  // each escape terminates cleanly
```

This matters anywhere a `\xNN` escape is immediately followed by `0-9` or `a-f`.

---

## §5 — Building LaMoshPit_NLE.exe (MSYS2/MinGW side)

Prerequisite: MSYS2 MINGW64 with the package set installed. See
[`docs/NLE_MSYS2_Setup.md`](docs/NLE_MSYS2_Setup.md) for the one-time
setup. First-time install command:

```bash
# From the MSYS2 MINGW64 shell (NOT cmd / PowerShell):
cd /c/Users/Thelu/Desktop/CodingProjects/LaMoshPit
bash scripts/install-msys2-deps.sh
```

### Incremental build

```bash
bash scripts/build-nle-msys2.sh          # Release
bash scripts/build-nle-msys2.sh -d       # Debug
bash scripts/build-nle-msys2.sh --clean  # wipe nle/build/ first
```

The script sanity-checks `$MSYSTEM=MINGW64`, runs CMake configure +
Ninja build in `nle/build/`, and emits `nle/build/LaMoshPit_NLE.exe`.

### Running the NLE exe

Run it from the same MINGW64 shell so Qt6 and MLT runtime DLLs are on
PATH:

```bash
./nle/build/LaMoshPit_NLE.exe
```

Running from PowerShell or double-clicking the exe will fail with
"Qt6Core.dll not found" until Phase 1 Step 12 adds the deploy step.

### The two builds are fully isolated

- `LaMoshPit.exe`'s build tree lives in `build/` (MSVC).
- `LaMoshPit_NLE.exe`'s build tree lives in `nle/build/` (MinGW).
- No CMake target from either side can be linked from the other.
- The MB editor's vcpkg overlay (with the 6 bitstream-surgery hooks) is
  **only** consumed by the MSVC build. The NLE never sees it. The
  two FFmpeg copies coexist on disk; Windows DLL search is scoped
  per-exe so they can't collide at runtime.

### Known gotchas

- **Wrong MSYS2 shell.** Use `MSYS2 MINGW64` (green prompt). Using
  plain `MSYS2 MSYS` links against `msys-2.0.dll`, not what we want.
- **Qt6 DLL not found when double-clicking the exe.** Expected until
  Step 12. Run from the MINGW64 shell.
- **`cmake: command not found` from MINGW64.** `pacman -S mingw-w64-x86_64-cmake`.
  The install script should have handled this.
- **MLT modules not found at runtime.** Set `MLT_REPOSITORY=/mingw64/lib/mlt-7`
  explicitly if MLT::Factory::init() logs a warning. Should work out
  of the box from MINGW64 where the env is already correct.
