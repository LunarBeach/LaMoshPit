# NLE Toolchain Setup — MSYS2 / MinGW64

**Date:** 2026-04-17
**Purpose:** reproducible developer setup for building `LaMoshPit_NLE.exe`
(the MinGW half of LaMoshPit's two-process architecture).  Phase 1 Step 1
of the NLE rebuild plan.

`LaMoshPit.exe` (the MSVC half, hosting the MB editor + Global Encode
Params + MediaBin) is unaffected — continue using Visual Studio 2022 +
vcpkg for that build.  These instructions only apply to the NLE side.

---

## Prerequisites (already satisfied on the current dev machine)

- ✅ MSYS2 installed at `C:\msys64\` (2026-04-17).
- ✅ Windows Defender exclusions for `C:\msys64\` and the LaMoshPit repo.
- ✅ `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled = 1`.

## Shell to use

**Always the `MSYS2 MINGW64` start-menu shortcut.**  Not `MSYS2 MSYS`, not
`MSYS2 UCRT64`, not `cmd.exe`, not PowerShell, not VS Code's integrated
terminal (unless you've verified `$MSYSTEM` prints `MINGW64` in it).

The MINGW64 shell sets `MSYSTEM=MINGW64` and puts `/mingw64/bin` at the
front of `PATH`, which makes `gcc`, `cmake`, `pkg-config`, and Qt6 tooling
resolve to the MinGW versions.  The regular `MSYS` shell uses a different
C runtime (`msys-2.0.dll`) that we don't want to link against.

---

## One-time dependency install

From the MSYS2 MINGW64 shell:

```bash
cd /c/Users/Thelu/Desktop/CodingProjects/LaMoshPit
bash scripts/install-msys2-deps.sh
```

The script:

1. Runs `pacman -Syu` to update base MSYS2.  On a fresh install this may
   print "WARNING: terminate MSYS2 without returning to shell and check
   for updates again."  If it does, close the shell, reopen MINGW64, and
   re-run the script.  Pacman's `--needed` flag makes subsequent runs
   idempotent.
2. Installs the MinGW64 toolchain + Qt6 + MLT + FFmpeg + build tools (see
   the script source for the full package list).
3. Prints the detected versions of g++, cmake, MLT++, and Qt6 at the end.

Expected package download size: ~1.5 GB.  First install takes 10-20
minutes depending on network + disk speed.

### Gotchas during install

- **"warning: skipping conflicting package"** — means a package with a
  name collision is already installed from a different repo.  Safe to
  ignore; the `--needed` flag handles it.
- **Network timeouts** — MSYS2 mirrors can be flaky.  Re-run the script;
  it resumes where it left off.
- **Antivirus quarantine during first `pacman -Syu`** — if the base
  update fails with "file not found" or "access denied" errors,
  double-check that `C:\msys64\` is in Windows Defender exclusions.

---

## Sanity test

After the install succeeds, run a minimal Qt6 + MLT Hello-World to confirm
the toolchain produces a working binary:

```bash
cd /c/Users/Thelu/Desktop/CodingProjects/LaMoshPit/scripts/nle-sanity-test
cmake -G Ninja -B build
cmake --build build
./build/nle-test.exe
```

**Expected output:** a small Qt window titled "LaMoshPit NLE — MSYS2
Toolchain Sanity Test" appears, showing:

```
MLT version:  7.X.Y
Repository:   repo loaded
If you can read this, the MinGW Qt6 + MLT toolchain is working.
```

Close the window; `cmake --build` returns 0.  Toolchain is green.

### If the cmake configure fails

- **"Could not find a package configuration file provided by 'Qt6'"** —
  MINGW64's Qt6 package is installed to `/mingw64/lib/cmake/Qt6/`.  The
  MINGW64 shell PATH normally finds this; if it doesn't, explicitly
  prepend `/mingw64/bin:/mingw64/lib/cmake/Qt6` to PATH.
- **"None of the required 'mlt++-7' found"** — pacman package
  `mingw-w64-x86_64-mlt` did not install cleanly.  Re-run
  `pacman -S --needed mingw-w64-x86_64-mlt` from MINGW64.

### If the build fails

- **"undefined reference to ..."** — library ordering issue in
  `CMakeLists.txt`.  Fix there; this test should link cleanly as-is.

### If the exe fails to launch

- **"The code execution cannot proceed because Qt6Core.dll was not
  found"** — Windows can't find MSYS2's Qt6 DLLs.  Run the exe from the
  MINGW64 shell (its PATH is already set), or copy all `Qt6*.dll` from
  `/mingw64/bin/` next to the exe.  For the real NLE build in Step 2
  we'll use `windeployqt.exe` to bundle these properly.
- **"A required MLT module could not be loaded"** — MLT couldn't find
  its modules.  Run in MINGW64 shell where `MLT_REPOSITORY` resolves
  correctly, or set `MLT_REPOSITORY=/mingw64/lib/mlt-7` explicitly.

---

## Versions pinned (installed 2026-04-17 on Mark's workstation)

This is the known-good baseline for every later Phase 1 step.  If your
MSYS2 install ends up with significantly newer versions of any of these,
pin down to this set first; newer versions may introduce regressions that
Shotcut hasn't yet adapted to.

```
g++ (MinGW)     : 15.2.0
cmake           : 4.3.1
MLT++           : 7.36.0   ← matches Shotcut's minimum required (>=7.36.0)
Qt6             : 6.11.0   ← same version as the MSVC side (coincidence, not required)
```

### Downgrade procedure if a future MSYS2 update breaks something

MSYS2 ships rolling-release packages; `pacman -Syu` will happily pull in
a newer MLT or Qt6 that might regress the NLE build.  If that happens:

1. Find the last-known-good package archive under
   `C:\msys64\var\cache\pacman\pkg\` (MSYS2 keeps prior versions).
2. `pacman -U mingw-w64-x86_64-<pkg>-<old-version>-*.pkg.tar.zst` to
   roll back to that specific version.
3. To prevent it creeping back on the next `-Syu`, add the package to
   `IgnorePkg` in `C:\msys64\etc\pacman.conf`.

---

## Cleanup

Once Phase 1 Step 2 (dual CMake skeleton) lands, the sanity-test scaffold
can be deleted:

```
rm -rf scripts/nle-sanity-test
```

The `install-msys2-deps.sh` script stays — it's the canonical install
recipe for every new dev workstation.
