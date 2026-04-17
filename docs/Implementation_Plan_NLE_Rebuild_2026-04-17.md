# LaMoshPit Implementation Plan — NLE Rebuild (Option D, Two-Process)

**Date drafted:** 2026-04-17
**Status:** Approved for later implementation. Do **not** start until user
explicitly greenlights Phase 0.
**Companion doc:** [NLE_MLT_Migration_Plan_2026-04-17.md](NLE_MLT_Migration_Plan_2026-04-17.md)
(architectural decisions + rationale). This doc is the step-by-step
recipe; the companion doc is the *why*.

---

## 0. Context & locked-in decisions

**User confirmations captured on 2026-04-17:**

- ✅ **License updated on GitHub** — LaMoshPit repo is now GPL-3.0.
  Compatible with every current dependency (x264 GPL-2+, FFmpeg
  LGPL-2.1+ → GPL-2+ when built with `--enable-gpl`, h264bitstream
  LGPL-2.1+, Qt6 LGPL-3, KDDockWidgets GPL-2+, Spout2 BSD, MLT
  LGPL-2.1+, Shotcut GPL-3).
- ✅ **Toolchain: Option D (two-process split).** LaMoshPit.exe stays
  MSVC; `LaMoshPit_NLE.exe` is built with MSYS2/MinGW (same toolchain
  Shotcut ships with).
- ✅ **vcpkg overlay ports + the 6 bitstream-surgery patches are
  sacred.** They are guard-railed in Phase 0 and every later step.
  The MB editor's FFmpeg build can stay pinned forever while the NLE
  independently tracks MLT/Shotcut upstream.
- ✅ **No legacy project compatibility.** Pre-rebuild sequencer JSON
  discarded.
- ✅ **View menu + auto-launch + linked MediaBin + VJ mode** are Phase 1
  deliverables.

**Pre-flight items already staged by user (2026-04-17):**

- ✅ **MSYS2 installed at `C:\msys64\`** — default install path, ready
  for the Phase 1 Step 1 `pacman -S` dependency install. No further
  MSYS2 download needed; Step 1 proceeds directly to package
  installation.
- ✅ **Spout SDK source vendored in `third_party/`** — specifically the
  `SpoutLibrary` variant we need for cross-compiler (MinGW) builds.
  Exact locations used by Phase 1 Step 10:
  - **Primary — build from source:**
    `third_party/Spout2/SPOUTSDK/SpoutLibrary/`
    - `SpoutLibrary.h` — cross-compiler C API header
    - `SpoutLibrary.cpp` — implementation
    - `CMakeLists.txt` — official CMake build (consumed by NLE
      CMake as a subdirectory)
  - **Dependency source (pulled in by SpoutLibrary.cpp):**
    `third_party/Spout2/SPOUTSDK/SpoutGL/` +
    `third_party/Spout2/SPOUTSDK/SpoutDirectX/`
  - **Reference-only, not built:** prebuilt MSVC binaries at
    `third_party/Spout-SDK-binaries_2-007-017_1/` (MD + MT CRT
    variants). These are the wrong toolchain for MinGW and will be
    deleted in Phase 0 housekeeping; kept momentarily as reference.
  - **To be deleted in Phase 0:**
    - `third_party/SPOUT_2007-017/` — end-user demos and tools, not
      build-relevant.
    - `third_party/Spout-SDK-binaries_2-007-017_1/` — MSVC prebuilts,
      superseded by building from source under MinGW.
    - The legacy `third_party/spout/` directory (old MSVC-built
      SDK used by the current `core/sequencer/SpoutSender.*`) goes
      away alongside the sequencer deletion in Phase 0 Step B.3.2.

---

## Part A — Roadmap at a glance

```
Phase 0 — Housekeeping (1-3 days)
   └─ Dead code + assets removed, guard rails verified, clean baseline

Phase 1 — MLT integration via two-process architecture (weeks)
   ├─ Step 1  MSYS2 toolchain bring-up
   ├─ Step 2  Dual CMake skeleton
   ├─ Step 3  Empty NLE exe + IPC handshake
   ├─ Step 4  LaMoshPit-side launcher + View menu
   ├─ Step 5  Fork Shotcut subsystems into nle/
   ├─ Step 6  Shared project folder
   ├─ Step 7  Shared MediaBin (filesystem-watched)
   ├─ Step 8  GEP render bridge
   ├─ Step 9  Custom MLT filter plugin (Mirror Left + future effects)
   ├─ Step 10 Custom MLT Spout consumer
   ├─ Step 11 VJ mode
   ├─ Step 12 Deployment / installer
   └─ Step 13 Acceptance testing

Phase 2 — GStreamer + Vulkan zero-copy (later, separate effort)
```

---

## Part B — Phase 0: Housekeeping

### B.1 — Why first

The codebase has ~6 months of experimentation accumulated. Dead code
+ stale assets inflate compile times, complicate the refactor to
come, and make it harder to tell "is this used?" from "is this
dead?". Cleaning up now means Phase 1's file-shuffling touches a
smaller surface, and we don't port junk into the new architecture.

### B.2 — Guard rails (must not be touched)

**Every cleanup commit must leave these untouched, verified by diff
inspection before push:**

1. `vcpkg/` (overlay ports, custom triplets, port version bumps).
   Contains the 6 bitstream-surgery hooks.
2. `third_party/` subtree.
3. `core/transform/FrameTransformer.*` (bitstream surgery consumer).
4. `core/pipeline/DecodePipeline.*` (H.264 decode + hook injection).
5. `core/model/MBEditData.*` + `core/model/GlobalEncodeParams.*`
   (data contracts).
6. `gui/BitstreamAnalyzer.*` (entry point for hook application).
7. `gui/widgets/MacroblockWidget.*` + `gui/widgets/GlobalParamsWidget.*`.
8. `core/model/SelectionMap.*`, `core/model/SelectionPreset.*` (MB
   editor selection persistence).
9. `core/util/SelectionMorphology.*`, `core/util/MapFrameSampler.*`,
   `core/util/VideoMetaProbe.*`.
10. `CMakeLists.txt` lines that handle FFmpeg / h264bitstream linkage.
    Structural reorganisation comes in Phase 1 Step 2, not Phase 0.

**Enforcement mechanism:** before Phase 0 begins, create a text file
`scripts/phase0_guardrails.txt` listing the protected paths. Last
step of each Phase 0 commit runs:

```
git diff --name-only HEAD~1 HEAD | grep -Ff scripts/phase0_guardrails.txt
```

If the command prints anything, the commit touched a guarded path.
Revert those changes before pushing.

### B.3 — Step-by-step cleanup

**B.3.1 — Safety archive.**
Before anything else:
```
git checkout -b archive/pre-nle-rebuild-2026-04-17
git push origin archive/pre-nle-rebuild-2026-04-17
git checkout main
```
This pins a recoverable snapshot. Nothing in Phase 0 or Phase 1 gets
rid of it.

**B.3.2 — Strip tick-debug instrumentation.**
Search for `LAMOSH_TICK_DEBUG_LOG` and remove every occurrence. The
entire logging apparatus in these files becomes dead weight once the
FrameRouter goes away in Phase 1 Step 5:
- `core/sequencer/FrameRouter.cpp` / `.h`
- `core/sequencer/SequencerPlaybackClock.cpp` / `.h`
- `gui/sequencer/SequencerDock.cpp`
- `main.cpp` (the fileMessageHandler and g_dbgLogFile globals)

Also delete `scripts/check_session3.ps1`, `scripts/dump_session3_enters.ps1`
(diagnostic scripts used during the pacing investigation; no longer
needed).

**B.3.3 — Dead code audit.**

Run a mechanical grep pass to find unreferenced classes and functions:

```
# For each top-level class declared in a header, grep for its usage
# across the codebase. If 0 hits outside its own file, it's suspect.
```

Targets likely to surface:
- Old experimental widgets in `gui/widgets/` (any `_old.cpp`,
  `_experimental.cpp`, commented-out includes in CMakeLists).
- Unused dialog classes in `gui/dialogs/` (check each against
  MainWindow.cpp usage).
- Any `Transition` subclass in `core/sequencer/` that's registered
  but never selected by the UI (all of this goes away in Phase 1
  anyway but delete now so we don't port it).

For each candidate, verify by:
1. `grep -r "ClassName" --include="*.cpp" --include="*.h"` → count.
2. If only found in its own files + CMakeLists, flag for deletion.
3. Double-check in MainWindow.cpp's connect() calls (sometimes used
   only by signal name).

Batch deletions by subsystem (all unused dialogs in one commit, all
unused widgets in another) so rollback is granular.

**Gotcha:** Qt `Q_OBJECT` classes are referenced by moc and can
appear "unused" to grep even when they're loaded via QObject's meta
system (e.g., invoked by string name). Check for
`QMetaObject::invokeMethod(..., "methodName", ...)` patterns before
deleting any `Q_INVOKABLE` methods.

**B.3.4 — Dead asset audit.**

In `resources/` (or wherever Qt resources live) and any PNG/SVG/QSS
files bundled via `.qrc`:

```
# Extract all :/path references from code
grep -rEoh ":/[^\"']+" --include="*.cpp" --include="*.h" --include="*.qrc" \
  | sort -u > /tmp/referenced_resources.txt

# Compare against what's actually in .qrc files
```

Any `.qrc` entry not in `/tmp/referenced_resources.txt` is a
candidate for deletion. Likely survivors:
- Old icon sets (e.g., v1 splash screen PNGs) now replaced by
  current branding.
- Tutorial / placeholder images from early development.

**B.3.5 — Dead dependency audit.**

Review `vcpkg.json` / `vcpkg-configuration.json` for packages that
aren't actually `#include`'d anywhere in our source. Each one is
~100MB of vcpkg cache + build time.

```
# Extract every #include <...> line and bucket by library
grep -rEh "^#include <" --include="*.cpp" --include="*.h" \
  | sort -u > /tmp/includes.txt
```

Cross-reference against the packages declared in vcpkg.json. If a
declared package has no include referencing it, remove the vcpkg
dependency.

**Guard rail:** do NOT remove FFmpeg, h264bitstream, x264, or any
package listed in `vcpkg/ports/*` overlay directories. These are
load-bearing for MB editor bitstream surgery even if some of their
`#include`s are indirect.

**B.3.6 — Header cleanup.**

In every `.cpp` that survives, remove unused `#include`s. Tool
option: `include-what-you-use` or just manual. Don't aggressive-
auto-remove; some includes are transitive requirements for types
used in return values / parameters even if not directly named.

**B.3.7 — Commented-out code.**

Search for multi-line comment blocks that contain C++ tokens
(braces, semicolons). Delete them — git history preserves them if
we ever need them back.

**B.3.8 — Stale documentation.**

`docs/` contains some old planning docs (`docs/NLE_Sequencer_2026-04-15.md`).
Keep them — they're historically informative. Do NOT add anything
to `docs/` during Phase 0 except this plan and the companion
migration plan.

### B.4 — Phase 0 gotchas

**Gotcha G0.1 — "Unused" signal/slot connections.**
A `QObject::connect(...)` whose slot string is built at runtime
looks unused. Check MainWindow.cpp and SettingsDialog.cpp for
dynamic slot invocation before deleting any `public slots:`.

**Gotcha G0.2 — Resource referenced only from QSS.**
Qt stylesheets (`gui/main.cpp`'s `kGlobalStyleTemplate`, and any
inline `.setStyleSheet(...)` calls) can reference `url(:/path/asset.png)`
strings. Grep for `url(:/` in all .cpp and .qss files; add those
matches to your referenced-resources list before deleting assets.

**Gotcha G0.3 — MOC-only references.**
Classes referenced only via `qRegisterMetaType<T>()` or as a
Q_PROPERTY or Q_INVOKABLE parameter type can look dead. Check the
registration calls before deleting a "dead" type.

**Gotcha G0.4 — Build passing ≠ Phase 0 correct.**
Phase 0 commits must build **Release AND Debug** of the current
(pre-rebuild) LaMoshPit with zero errors/warnings introduced by the
cleanup. If a deletion removes something Debug still needed, back
it out.

**Gotcha G0.5 — Resource-only files.**
`resources/fonts/*.otf` (custom fonts Creato / Ethnocentric / Nodo)
are loaded by `gui/AppFonts.cpp`, not by any direct `#include`. Don't
delete them just because grep shows no hits. Verify by launching the
app and checking that fonts render.

### B.5 — Phase 0 exit criteria

- [ ] `archive/pre-nle-rebuild-2026-04-17` branch exists on remote.
- [ ] All `LAMOSH_TICK_DEBUG_LOG` removed; app builds clean Release +
      Debug with no diagnostic output.
- [ ] Every file in Phase 0 guard rail list is byte-identical to
      pre-cleanup state. Verified by `git diff
      archive/pre-nle-rebuild-2026-04-17..HEAD -- <guarded paths>`
      returning no output.
- [ ] All 6 bitstream-surgery hooks still active. Verified by
      running existing MB-editor workflow (import → edit → Quick
      Mosh render) end-to-end on a test video. Output matches
      pre-cleanup byte-for-byte.
- [ ] Binary size reduction and/or dependency reduction documented
      in the cleanup commit message.

---

## Part C — Phase 1: MLT integration

Each step below has substeps, gotchas with concrete mitigations, and
an exit criterion. **Do not start step N+1 until step N exits cleanly.**

### Step 1 — MSYS2 toolchain bring-up

**Goal:** a developer workstation can reproducibly build a trivial
MinGW Qt6 Hello-World against MLT. No LaMoshPit code yet.

**Substeps:**

1.1. ✅ MSYS2 already installed at `C:\msys64\` (done 2026-04-17).
     Confirm by launching the `MSYS2 MINGW64` start-menu shortcut;
     shell prompt should be green and the path banner should show
     `MINGW64`.

1.2. From `MSYS2 MINGW64` shell (NOT `MSYS2 MSYS`):
```
pacman -Syu                       # base system update; may require reopening shell
pacman -S --needed \
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
```

1.3. Sanity test:
```
cd /c/temp && mkdir nle-test && cd nle-test
cat > main.cpp <<EOF
#include <QApplication>
#include <QLabel>
#include <Mlt.h>
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    Mlt::Factory::init();
    QLabel lbl(QString("MLT %1 ready").arg(mlt_version_get_string()));
    lbl.show();
    return app.exec();
}
EOF
cat > CMakeLists.txt <<EOF
cmake_minimum_required(VERSION 3.22)
project(nle-test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MLTPP REQUIRED IMPORTED_TARGET mlt++-7)
add_executable(nle-test main.cpp)
target_link_libraries(nle-test PRIVATE Qt6::Widgets PkgConfig::MLTPP)
EOF
cmake -G Ninja -B build
cmake --build build
./build/nle-test.exe
```
Window should appear showing "MLT 7.x.x ready". If so, toolchain is green.

**Gotchas in Step 1:**

- **G1.1 — Wrong MSYS2 shell.** The MSYS2 installer puts three
  shortcuts in the start menu: `MSYS2 MSYS`, `MSYS2 MINGW64`, and
  `MSYS2 UCRT64`. Use `MINGW64` consistently. MSYS shell runs
  against the Cygwin-style msys-2.0.dll runtime (NOT what we want);
  UCRT64 is an alternative C runtime but Shotcut's existing scripts
  target MinGW64 so we follow that.
- **G1.2 — PATH pollution in MINGW64.** MINGW64 inherits the
  Windows PATH. If the Windows PATH has anything pointing at a
  different Qt installation (e.g., a vcpkg-built Qt from LaMoshPit's
  MSVC side), `cmake` may pick it up. Mitigation: always launch
  MINGW64 from its start-menu shortcut (which sets PATH correctly),
  never from `cmd.exe` or VS Code's integrated terminal unless the
  shell profile sources `/mingw64/bin` first.
- **G1.3 — Antivirus quarantine.** Windows Defender and many
  third-party AVs quarantine freshly-built MSYS2 DLLs and FFmpeg
  codec DLLs on first run. Add `C:\msys64` to AV exclusions, and
  add the `<install>\nle\` path too once we have one.
- **G1.4 — Long paths.** MSYS2 paths like
  `/mingw64/lib/cmake/Qt6Core/Qt6CoreConfig.cmake` can exceed 260
  chars once combined with build-tree paths. Enable long paths in
  Windows (registry + app manifest). Alternatively, build in a
  short path like `C:\src\lm\nle`.
- **G1.5 — Qt version drift.** MSYS2's Qt6 version may be newer or
  older than vcpkg's MSVC Qt6. This is fine — the two processes
  never share Qt code — but QML files and feature usage in the NLE
  code must match the MinGW Qt6 version. Pin a known-good MSYS2
  snapshot if needed.

**Exit criterion:** the sanity-test Hello-World window displays on
a fresh developer machine following these steps verbatim.
Documentation of the setup committed to `docs/NLE_MSYS2_Setup.md`.

### Step 2 — Dual CMake skeleton

**Goal:** the repo holds two build trees that never see each other.
Running the MSVC configure builds only LaMoshPit.exe; running the
MSYS2 configure builds only LaMoshPit_NLE.exe.

**Substeps:**

2.1. Create the directory tree:
```
nle/
  CMakeLists.txt              # top-level for NLE, MSYS2-only
  src/
    main.cpp                  # minimal for now
  CMakePresets.json           # MinGW Ninja preset
```

2.2. `nle/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.22)
project(LaMoshPit_NLE LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Widgets Network
    Quick QuickControls2 QuickWidgets Qml
    Multimedia
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MLTPP REQUIRED IMPORTED_TARGET mlt++-7>=7.36.0)

add_executable(LaMoshPit_NLE WIN32 src/main.cpp)
target_link_libraries(LaMoshPit_NLE PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
    Qt6::Quick Qt6::QuickControls2 Qt6::QuickWidgets Qt6::Qml
    Qt6::Multimedia
    PkgConfig::MLTPP
)
install(TARGETS LaMoshPit_NLE RUNTIME DESTINATION nle)
```

2.3. Add a guard to the MAIN CMakeLists.txt:
```cmake
# Prevent accidental configuring of nle/ from the MSVC build
if(DEFINED MSVC AND NOT DEFINED NLE_BUILD_ALLOW)
    # If someone tries add_subdirectory(nle) from this file, bail out.
endif()
```
And explicitly do NOT `add_subdirectory(nle)`. nle/ is invisible to
the top-level MSVC build.

2.4. Add `scripts/build-nle-msys2.sh` — a thin wrapper that handles
     configure + build from the MSYS2 shell:
```bash
#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../nle"
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

2.5. Document in `BUILDINSTRUCTIONS.md`:
```markdown
## Building the NLE (LaMoshPit_NLE.exe)

Requires MSYS2 MINGW64 (see docs/NLE_MSYS2_Setup.md).

From the MINGW64 shell:
    cd /c/Users/Thelu/Desktop/CodingProjects/LaMoshPit
    bash scripts/build-nle-msys2.sh

Output: nle/build/LaMoshPit_NLE.exe
```

**Gotchas in Step 2:**

- **G2.1 — Qt plugin discovery.** When both exes are in the same
  install directory, `LaMoshPit.exe` finds its MSVC Qt plugins via
  `qt.conf` or Qt's default search. `LaMoshPit_NLE.exe` must NOT
  load those plugins (ABI mismatch → instant crash). Mitigation: put
  the NLE's `qt.conf` next to `LaMoshPit_NLE.exe` in the `nle/`
  subdirectory, pointing at `nle/plugins/` and `nle/qml/`. Qt
  respects per-exe qt.conf in the same directory.
- **G2.2 — Debugger confusion.** VS debugs MSVC; GDB debugs MinGW.
  Don't try to use the MSVC debugger on `LaMoshPit_NLE.exe` or
  vice-versa. Set up VS Code launch configs for both so you know
  which you're running.
- **G2.3 — `main.cpp` naming collision.** The repo already has a
  top-level `main.cpp` for LaMoshPit.exe. The NLE's `main.cpp` is
  under `nle/src/` so paths don't collide, but watch out if you
  restructure.

**Exit criterion:** `bash scripts/build-nle-msys2.sh` produces
`nle/build/LaMoshPit_NLE.exe` that launches and shows an empty
QMainWindow. The existing LaMoshPit MSVC build is unaffected
(verify by running both configs).

### Step 3 — Empty NLE exe + IPC handshake

**Goal:** both exes launch, connect over a QLocalSocket named pipe,
exchange a hello/ready handshake. Still no user-visible features.

**Substeps:**

3.1. In `nle/src/`, add `CoreControlClient.{h,cpp}`:
```cpp
class CoreControlClient : public QObject {
    Q_OBJECT
public:
    explicit CoreControlClient(QObject* parent = nullptr);
    void connectToCore(const QString& pipeName);
signals:
    void commandReceived(const QJsonObject& msg);
    void connected();
    void disconnected();
public slots:
    void sendEvent(const QJsonObject& msg);
private:
    QLocalSocket* m_sock { nullptr };
    QString m_pipeName;
    QTimer m_reconnectTimer;
};
```
Connect logic: retry on `disconnected` with exponential backoff.
Message framing: one JSON object per line (LF-terminated). Keep it
text for debuggability.

3.2. In `nle/src/main.cpp`, parse `--ipc-pipe <name>` command-line
     arg. If present, construct a `CoreControlClient` and connect.
     Send `{"evt":"ready","version":"1.0"}` on connected signal.

3.3. In LaMoshPit main repo, add `gui/nle_bridge/`:
- `NleControlChannel.{h,cpp}` — a `QLocalServer` + `QLocalSocket`
  wrapper, mirror of the client.
- `NleLauncher.{h,cpp}` — wraps `QProcess`. Generates a unique
  pipe name (include PID to prevent collisions), starts the server
  listening on it, spawns `LaMoshPit_NLE.exe --ipc-pipe <name>`.

3.4. In `gui/MainWindow.cpp` constructor: instantiate
     `NleLauncher` after MainWindow basic UI is up but before we
     load a project. Wait for the NLE's `{"evt":"ready"}` with a
     3-second timeout (log a warning and continue if it doesn't
     arrive — NLE may just be slow or the exe is missing on this
     dev machine).

**Gotchas in Step 3:**

- **G3.1 — QLocalSocket pipe name.** On Windows, QLocalServer names
  are translated to `\\.\pipe\<name>`. Include PID or a UUID in the
  name so two copies of LaMoshPit running simultaneously don't
  collide: e.g., `lamosh-nle-<pid>-<random>`.
- **G3.2 — Race on connection.** LaMoshPit must `listen()` on the
  pipe BEFORE spawning the NLE process. If the NLE tries to connect
  before the server is listening, it sees ECONNREFUSED and must
  retry. Safer: LaMoshPit listens → spawns → waits for the first
  connection with a timeout.
- **G3.3 — JSON framing drift.** LF-terminated JSON is debuggable
  but fragile if any message accidentally contains a literal `\n`.
  Mitigation: always serialize with `QJsonDocument::Compact` (no
  embedded newlines), split incoming bytes by the first `\n` only,
  buffer partial messages across `readyRead` events.
- **G3.4 — Socket buffer limits.** `QLocalSocket` has a default
  read buffer. If the NLE floods events faster than LaMoshPit
  reads, messages block. Rate-limit high-frequency events (e.g.,
  playhead updates should NOT be sent over IPC on every tick).
- **G3.5 — Stale pipe after crash.** If LaMoshPit crashes and
  leaves a pipe dangling, the next launch may fail to `listen()`
  with EADDRINUSE. Before `listen()`, call `QLocalServer::removeServer(name)`.
- **G3.6 — QProcess zombies.** If LaMoshPit exits without a clean
  shutdown (Task Manager kill), `LaMoshPit_NLE.exe` continues
  running as an orphan. Windows Job Objects solve this: attach the
  NLE process to a job at launch, with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
  When LaMoshPit dies, Windows auto-kills the NLE. Implementation:
  on `CreateProcess`, wrap the NLE's process handle in a
  `CreateJobObject` call. Do this natively via Win32 API since Qt's
  QProcess doesn't expose this.

**Exit criterion:** launch LaMoshPit.exe, observe NLE window opens,
LaMoshPit's log shows `NLE connected; version=1.0`. Kill LaMoshPit
via Task Manager; NLE exits within 1 second (Job Object effect).

### Step 4 — LaMoshPit-side launcher + View menu

**Goal:** menu items wired, NLE window visibility toggleable,
project folder propagated.

**Substeps:**

4.1. Add three checkable menu items to `gui/MainWindow.cpp`'s View
     menu (after existing ones):
```cpp
m_actNleWindow   = viewMenu->addAction("NLE Sequencer");
m_actNleWindow->setCheckable(true);
m_actNleWindow->setChecked(true);
connect(m_actNleWindow, &QAction::toggled, [this](bool on){
    m_nleControl->send({ {"cmd", on ? "show" : "hide"} });
});

m_actNleMediaBin = viewMenu->addAction("NLE Media Bin");
m_actNleMediaBin->setCheckable(true);
m_actNleMediaBin->setChecked(true);
connect(m_actNleMediaBin, &QAction::toggled, [this](bool on){
    m_nleControl->send({ {"cmd", on ? "media_bin.show" : "media_bin.hide"} });
});

m_actVjMode      = viewMenu->addAction("VJ Mode");
m_actVjMode->setCheckable(true);
connect(m_actVjMode, &QAction::toggled, [this](bool on){
    m_nleControl->send({ {"cmd", on ? "vj_mode.on" : "vj_mode.off"} });
});
```

4.2. On NLE side, in `CoreControlClient::commandReceived`, dispatch
     `show` / `hide` to `QMainWindow::show()` / `hide()` (preserves
     state; does NOT terminate the process).

4.3. When LaMoshPit opens a project (via `setActiveProject`), send
     `{"cmd":"open_project","path":"<projectFolder>"}` to NLE.

**Gotchas in Step 4:**

- **G4.1 — Menu item state drift.** If the NLE window's visibility
  changes for reasons OTHER than the menu (user clicks the X),
  the menu checkbox becomes stale. Mitigation: NLE sends
  `{"evt":"visibility","visible":false}` on close event, LaMoshPit
  updates the menu to match with signal blocker.
- **G4.2 — NLE close button.** By default, closing the NLE window
  terminates its process. We don't want that — hiding is better
  (preserves state, MLT consumers stay alive). Override
  `closeEvent` to call `hide()` instead, and only truly quit on
  `{"cmd":"shutdown"}` from LaMoshPit.
- **G4.3 — Initial project load timing.** If LaMoshPit calls
  `open_project` before the NLE's `ready` handshake arrives, the
  message is dropped. Mitigation: LaMoshPit queues any `cmd`
  messages until `ready` is received.

**Exit criterion:** launch LaMoshPit, NLE appears. Toggle View →
NLE Sequencer: NLE hides/shows. Toggle View → VJ Mode: NLE logs the
command (no effect yet since VJ mode isn't implemented). Open a
project: NLE's title bar updates with the project name.

### Step 5 — Fork Shotcut subsystems into nle/

**Goal:** the NLE window becomes a functional MLT-based editor:
timeline, preview player, filter panel, transport controls. Works
end-to-end for a single-track project (import, drop, scrub, play,
stop, seek, export). No LaMoshPit-specific features yet.

**Shotcut subtrees to port, with destinations:**

| Source (Shotcut src/) | Destination (nle/src/) | Notes |
|---|---|---|
| `mltcontroller.{h,cpp}` | `controllers/MltController.{h,cpp}` | Core MLT factory + consumer lifecycle |
| `mltxmlchecker.{h,cpp}` | `controllers/MltXmlChecker.{h,cpp}` | XML validation on load |
| `models/multitrackmodel.{h,cpp}` | `models/MultitrackModel.{h,cpp}` | Qt model over Mlt::Tractor |
| `models/audiolevelstask.{h,cpp}` | `models/AudioLevelsTask.{h,cpp}` | Audio waveform rendering |
| `commands/timelinecommands.{h,cpp}` | `commands/TimelineCommands.{h,cpp}` | Edit ops (append/insert/trim/split/move) |
| `commands/filtercommands.{h,cpp}` | `commands/FilterCommands.{h,cpp}` | Filter add/remove/modify |
| `commands/playlistcommands.{h,cpp}` | `commands/PlaylistCommands.{h,cpp}` | Per-playlist ops |
| `controllers/filtercontroller.{h,cpp}` | `controllers/FilterController.{h,cpp}` | Filter UI ↔ Mlt::Filter |
| `docks/timelinedock.{h,cpp}` | `docks/TimelineDock.{h,cpp}` | Timeline main widget |
| `docks/filtersdock.{h,cpp}` | `docks/FiltersDock.{h,cpp}` | Filter parameter panel |
| `docks/playlistdock.{h,cpp}` | `docks/MediaBinDock.{h,cpp}` | **Rename**: becomes our MediaBin |
| `docks/encodedock.{h,cpp}` | `docks/EncodeDock.{h,cpp}` | Render dialog |
| `jobs/meltjob.{h,cpp}` | `jobs/MeltJob.{h,cpp}` | Melt CLI subprocess wrapper |
| `jobs/encodejob.{h,cpp}` | `jobs/EncodeJob.{h,cpp}` | Encode job |
| `videowidget.{h,cpp}` | `widgets/VideoWidget.{h,cpp}` | Preview widget (keep the D3D variant on Windows) |
| `player.{h,cpp}` | `widgets/Player.{h,cpp}` | Transport controls |
| `qml/views/timeline/*` | `qml/views/timeline/*` | Timeline QML |
| `qml/views/filter/*` | `qml/views/filter/*` | Filter QML |
| `mainwindow.{h,cpp}` | `MainWindow.{h,cpp}` | NLE's main window; needs substantial trimming |

**Substeps:**

5.1. Copy each file verbatim, preserving `Copyright (c) Meltytech,
     LLC` headers + GPL-3 notices. Add below:
```cpp
/*
 * Forked into LaMoshPit on 2026-04-17.
 * Modifications: see CHANGELOG.md.
 */
```

5.2. Rename C++ namespaces: `Shotcut::...` → `lamosh::nle::...`.
     (Not all Shotcut code is namespaced; rename where present.)

5.3. Update includes: `#include "mltcontroller.h"` →
     `#include "controllers/MltController.h"`. Easiest via a single
     scripted find-replace pass.

5.4. Strip Shotcut-specific UI we don't want:
     - "About Shotcut" dialog
     - "Report a bug" menu item  
     - Shotcut-specific tutorial videos link
     - Keyframe-specific UI we don't use initially (keep the
       models, strip the docks we don't need)
     - `screencapture/` subsystem (not in LaMoshPit's scope)
     - `spatialmedia/` subsystem (not needed)
     - `voices/` TTS-preview subsystem (not needed)
     - `dialogs/systray*` (not needed)

5.5. Wire up `CoreControlClient` commands in NLE's MainWindow:
     - `show` / `hide` → `QMainWindow::show()` / `hide()`
     - `media_bin.show` / `.hide` → toggle `MediaBinDock` visibility
     - `vj_mode.on` / `.off` → stubs that log for now (implemented
       in Step 11)
     - `open_project` → `MltController::openXml(<path>/nle/sequence.mlt)`;
       create empty one if missing
     - `shutdown` → save + `QCoreApplication::quit()`

**Gotchas in Step 5:**

- **G5.1 — GPL-3 license headers.** Every file forked MUST keep
  Meltytech's copyright header verbatim. Adding our own copyright
  below is fine (dual copyright); removing theirs is a license
  violation. Add a `scripts/check_license_headers.py` that verifies
  every `nle/src/*.h` and `.cpp` starts with a GPL-3 notice.
- **G5.2 — QML resource paths.** Shotcut's QML files use `qrc:/`
  and relative imports pinned to their source tree layout. After
  port, the QML module path changes. Use Qt6's `qt6_add_qml_module`
  to declare the module explicitly; update every `import "views/..."`
  in the QML.
- **G5.3 — Translation files.** Shotcut ships `.ts` / `.qm`
  translations. We don't. Decision: drop translations entirely for
  v1 — wrap all `tr(...)` calls in a no-op macro or leave them as
  English. We're English-only initially.
- **G5.4 — Shotcut's icon theme.** Shotcut uses a themed icon set
  via `QIcon::fromTheme`. MSYS2's Qt install may not include theme
  support. Bundle the icons explicitly in a qrc or swap to our own
  icon set.
- **G5.5 — MLT_REPOSITORY at runtime.** In NLE's `main.cpp`, set
  env vars BEFORE `Mlt::Factory::init()`:
```cpp
const QString appDir = QCoreApplication::applicationDirPath();
qputenv("MLT_REPOSITORY", QFile::encodeName(appDir + "/lib/mlt"));
qputenv("MLT_DATA",       QFile::encodeName(appDir + "/share/mlt"));
qputenv("MLT_PROFILES_PATH", QFile::encodeName(appDir + "/share/mlt/profiles"));
qputenv("MLT_REPOSITORY_DENY", "libmltqt:libmltglaxnimate:libmltopenfx");
```
- **G5.6 — D3D11VA vs software decode.** Shotcut's Windows
  `videowidget.cpp` uses D3D11. Since MLT does its own hardware
  decode via avformat + d3d11va, the widget just has to display
  the output. Verify that the MSYS2 MLT build has `d3d11va` enabled
  in its avformat plugin; if not, software decode works too, just
  slower.
- **G5.7 — Qt version features.** Some Shotcut code uses Qt 6.5+
  features. Pin MSYS2's Qt to >= 6.5 in the CMakeLists; verify
  with `find_package(Qt6 6.5 REQUIRED ...)`.
- **G5.8 — `Mlt::Producer` lifetime.** MLT uses reference counting
  via its C++ wrapper. Don't use `std::unique_ptr<Mlt::Producer>`
  — let MLT's refcount handle it. Bugs here cause use-after-free
  or leaks.
- **G5.9 — SDL audio consumer failures.** Shotcut's default audio
  output is SDL. On some systems (especially headless/VM), SDL
  fails to init → Shotcut falls back to JACK, which also fails →
  no audio. Catch this explicitly; log a warning; keep video
  playing.

**Exit criterion:** open LaMoshPit → NLE appears → drag a 10-second
MP4 into NLE's timeline → scrub, play, stop. No audio glitches. No
visual glitches. Timeline responds to seek. Render via Shotcut's
default preset produces a correct MP4.

### Step 6 — Shared project folder layout

**Goal:** the NLE's project file lives under `<projectFolder>/nle/sequence.mlt`;
LaMoshPit tells the NLE which folder to use; opening a LaMoshPit
project opens the corresponding NLE sequence.

**Substeps:**

6.1. LaMoshPit core: add `Project::nleSequencePath()` returning
     `<projectFolder>/nle/sequence.mlt`. On first call, ensure the
     `<projectFolder>/nle/` directory exists.

6.2. LaMoshPit core: on `setActiveProject`, send `{"cmd":"open_project","path":"<folder>"}`
     to NLE (not the MLT XML path, just the project folder root;
     NLE knows to append `/nle/sequence.mlt`).

6.3. NLE: on receiving `open_project`:
     - If current project is dirty, prompt save (Shotcut's standard
       flow) — BUT synchronously. We can't afford user interaction
       during project switch.
     - Actually better: LaMoshPit asks NLE first "can you save?"
       and waits for ack. Protocol:
       `{"cmd":"project.switch_prepare","path":"<new>"}`
       → NLE saves or prompts → `{"evt":"project.switch_ready"}`
       → LaMoshPit sends `{"cmd":"open_project","path":"<new>"}`
     - Load the new MLT XML.
     - Send `{"evt":"project.opened","path":"<folder>"}` back.

6.4. Autosave: NLE autosaves on every mutation (Shotcut already
     does this). File location: `<projectFolder>/nle/sequence.mlt`
     written atomically (write to `.mlt.tmp`, rename).

**Gotchas in Step 6:**

- **G6.1 — Atomic write.** Windows rename-over-existing fails on
  NTFS unless you use `MoveFileEx(MOVEFILE_REPLACE_EXISTING)`. Qt's
  `QSaveFile` handles this. Use it everywhere.
- **G6.2 — MLT XML with absolute paths.** By default Shotcut saves
  absolute paths in MLT XML. That breaks if the project folder
  moves. Configure the xml consumer with `root` = project folder
  and store relative paths. See Shotcut `mltcontroller.cpp:491-505`.
- **G6.3 — Concurrent access.** If somehow the user opens the same
  LaMoshPit project in two instances (unlikely but possible),
  both NLE processes write to the same `sequence.mlt`. Mitigation:
  at app startup, `QLockFile` on `<projectFolder>/.lamosh.lock`.
  Second instance refuses with a helpful dialog.
- **G6.4 — Media paths with Unicode.** Windows uses UTF-16 paths;
  MLT XML is UTF-8. `QString::toUtf8()` + `QFile::encodeName` for
  all path writes. Test with a path containing non-ASCII (e.g.,
  `C:\Users\Thel\Desktop\CodingProjects\LaMoshPit\プロジェクト\`).
- **G6.5 — Missing project folder.** On fresh install, the user's
  project folder may not have `<projectFolder>/nle/` yet. NLE must
  create the directory + a skeleton MLT XML on first `open_project`.

**Exit criterion:** open project A in LaMoshPit → NLE shows A's
sequence (empty initially). Add a clip to NLE's timeline. Open
project B → NLE saves A, switches to B, shows B's sequence (empty).
Return to project A → NLE shows A's clip as before.

### Step 7 — Shared MediaBin (filesystem-watched)

**Goal:** LaMoshPit core's MediaBin and NLE's MediaBinDock show
identical content, updated live when either side adds/removes files.

**Substeps:**

7.1. Identify the canonical media folder: `<projectFolder>/moshVideoFolder/`.
     Thumbnails: `<projectFolder>/thumbnails/`.

7.2. LaMoshPit core: `MediaBinWidget` already watches this folder
     (existing code). No change.

7.3. NLE side: `MediaBinDock` (renamed from Shotcut's
     `PlaylistDock`) uses `QFileSystemWatcher` + `QDir::entryList`
     to populate. On any watch event, debounce 200ms, re-scan.

7.4. Thumbnails: NLE displays thumbnails from the shared folder. If
     a thumbnail is missing, NLE sends `{"cmd":"thumbnail.request","path":"<media>"}`
     to LaMoshPit; core's existing `ThumbnailGenerator` produces it
     and writes to the shared folder; file watcher in NLE picks it
     up automatically.
     - This prevents duplicate thumbnail generators.

7.5. Drag-drop: within NLE, dragging a MediaBinDock item onto the
     timeline works via `text/uri-list` (Shotcut's existing
     behavior). No IPC needed.

**Gotchas in Step 7:**

- **G7.1 — QFileSystemWatcher flakiness on Windows.** Fires
  multiple times per operation (tmp write + rename + security
  scan). Debounce hard: collect events for 200ms after the last
  one, then scan once.
- **G7.2 — Watcher path limits.** `QFileSystemWatcher` on Windows
  has per-instance handle limits (~256 paths). Media folders with
  hundreds of files work because we watch the DIRECTORY not each
  file; directory-level events suffice.
- **G7.3 — Thumbnail race.** If core is still generating a
  thumbnail when NLE tries to load it, NLE reads a partial file.
  Fix: LaMoshPit writes thumbnails atomically (QSaveFile) — then
  even a racing read either gets the old version (still-valid) or
  the new version (just-valid), never corrupt.
- **G7.4 — Drag-drop MIME format.** LaMoshPit's MediaBin uses its
  own internal drag-drop mime type + `text/uri-list`. Ensure NLE's
  timeline accepts `text/uri-list` (it does, from Shotcut), so
  dragging from LaMoshPit's MediaBin into NLE's timeline works
  naturally once both windows are open side-by-side.
- **G7.5 — Shared-memory temptation.** Don't use shared memory for
  MediaBin state. Filesystem + watcher is simpler and already
  works; optimizing further is premature.

**Exit criterion:** import a new MP4 via LaMoshPit's MediaBin →
within 1 second, it appears in NLE's MediaBinDock with a thumbnail.
Drag it into NLE's timeline → works. Delete it in NLE's MediaBinDock
→ LaMoshPit's MediaBin shows it removed too.

### Step 8 — GEP render bridge

**Goal:** user clicks Render in NLE → dialog offers "Use LaMoshPit
Global Encode Params" as an option → if chosen, NLE asks LaMoshPit
for the current GEP, translates to MLT avformat args, renders.

**Substeps:**

8.1. In NLE's ported `EncodeDock`, add a radio at the top:
     - ( ) Shotcut preset library
     - (•) LaMoshPit Global Encode Params  (default)
     When the second is chosen, the rest of the dialog's preset
     UI is replaced with a read-only summary of the current GEP.

8.2. On dialog open (if GEP mode), NLE sends
     `{"cmd":"gep.fetch"}`. LaMoshPit responds with
     `{"rsp":"gep.current", <all GEP fields>}`. Display summary.

8.3. User clicks Render. NLE translates GEP → MLT avformat consumer
     args via a static mapping table (Step 8.4). Kicks off
     `EncodeJob` with those args.

8.4. Mapping table (incomplete; to be expanded as we port):
```
GEP.videoCodec ("h264")       → vcodec="libx264"
GEP.videoBitrate (int)        → b="Xk"
GEP.videoPreset ("slow")      → preset="slow"
GEP.videoProfile ("high")     → profile="high"
GEP.videoLevel ("4.1")        → level="4.1"
GEP.videoGopSize (int)        → g="X"
GEP.videoKeyintMin (int)      → keyint_min="X"
GEP.videoPixFmt ("yuv420p")   → pix_fmt="yuv420p"
GEP.audioCodec                → acodec="..."
GEP.audioBitrate              → ab="..."
GEP.audioSampleRate           → ar="..."
GEP.container ("mp4")         → f="mp4"
GEP.extraX264Params           → x264opts="..."
```

8.5. After render finishes, NLE sends `{"evt":"render_finished","output":"<path>"}`.
     LaMoshPit auto-adds to MediaBin. Optionally opens in MB editor
     (ask user via toast: "Render complete. Open in MB editor?").

**Gotchas in Step 8:**

- **G8.1 — GEP option not mappable.** If GEP has a knob that MLT
  doesn't support directly (e.g., custom x264 params),
  pass-through via `x264opts="..."`. If unrepresentable entirely,
  warn the user in the render dialog: "Your GEP uses X which the
  NLE render cannot honour; output will differ."
- **G8.2 — Bitstream-surgery hooks don't apply.** GEP's BSS hooks
  are an MB-editor Quick Mosh feature. NLE renders do NOT apply
  them — this is CORRECT BEHAVIOR but document it: if user wants
  bitstream surgery in the rendered sequence output, they re-import
  the NLE render into the MB editor and apply Quick Mosh.
- **G8.3 — Render cancelation.** User cancels render mid-way →
  NLE's `MeltJob` must kill its subprocess cleanly. Shotcut
  already handles this; keep it.
- **G8.4 — Progress reporting.** Shotcut parses melt stderr for
  progress. Pipe progress percentages over IPC to LaMoshPit for
  display in its own status bar (optional polish, not strictly
  needed).

**Exit criterion:** 30-second 2-track NLE sequence renders via GEP
with custom bitrate + codec; output MP4 has expected codec/bitrate
(verify via `ffprobe`). Re-imported into MB editor works.

### Step 9 — Custom MLT filter plugin for LaMoshPit effects

**Goal:** Mirror Left (our custom effect) is available as an MLT
filter inside the NLE's filter panel, applies correctly on preview
and on render.

**Substeps:**

9.1. Create `nle/plugins/mlt_lamosh/`:
     - `factory.c` — MLT module entry point (`mlt_register`).
     - `filter_mirror_left.c` — the filter.
     - `mirror_left.yml` — metadata (Shotcut uses YAML for filter
       descriptions).
     - `CMakeLists.txt` — builds `libmltlamosh.dll`.
     - Installs to `nle/lib/mlt/libmltlamosh.dll` + metadata to
       `nle/share/mlt/lamosh/`.

9.2. Filter implementation (simplified):
```c
static int filter_get_image(mlt_frame frame, uint8_t **image,
                             mlt_image_format *format, int *width,
                             int *height, int writable)
{
    int error = mlt_frame_get_image(frame, image, format, width, height, 1);
    if (!error && *format == mlt_image_rgb24a) {
        // left-to-right mirror of the left half into the right half
        for (int y = 0; y < *height; ++y) {
            uint8_t* row = *image + y * (*width) * 4;
            for (int x = *width / 2; x < *width; ++x) {
                uint8_t* dst = row + x * 4;
                uint8_t* src = row + (*width - 1 - x) * 4;
                memcpy(dst, src, 4);
            }
        }
    }
    return error;
}
```

9.3. Metadata YAML declares filter name, ID, parameters (none for
     Mirror Left), category. Shotcut's filter UI auto-populates
     from this.

9.4. Build + install: the MSYS2 CMake adds `nle/plugins/mlt_lamosh`
     as a subdirectory when the main NLE builds.

9.5. NLE reads `$MLT_REPOSITORY/libmltlamosh.dll` on Factory::init.
     Filter appears in FiltersDock under "LaMoshPit Effects" category.

**Gotchas in Step 9:**

- **G9.1 — MLT filter thread safety.** Filters may be called from
  multiple threads concurrently. Mirror Left has no state so it's
  safe; future filters with parameters must be thread-safe.
- **G9.2 — Image format conversion.** MLT passes frames in whatever
  format upstream provides (yuv420p, rgb24, rgb24a, ...). Request
  `mlt_image_rgb24a` at the start of the filter so we always get
  BGRA-equivalent. MLT auto-converts.
- **G9.3 — Metadata YAML schema.** Shotcut's filter YAML has
  undocumented required fields. Copy an existing built-in MLT
  filter's YAML as a template (e.g., `share/mlt/brightness/brightness.yml`).
- **G9.4 — Plugin unload.** If we hot-swap the plugin during
  development, MLT may keep DLL references. Restart the NLE to
  reload.

**Exit criterion:** in NLE, select a clip, apply Mirror Left filter
from the FiltersDock, see the effect in preview. Render sequence,
check output has the effect baked in.

### Step 10 — Custom MLT Spout consumer

**Goal:** when VJ mode is active, NLE's output goes to both the
preview window AND a Spout sender that OBS can pick up.

**Substeps:**

10.1. Create `nle/plugins/mlt_spout/`:
     - `factory.c` — MLT module entry.
     - `consumer_spout.c` — consumer that forwards BGRA frames to
       Spout.
     - `spout_sender.{h,cpp}` — port of existing
       `core/sequencer/SpoutSender.*` to C++ with C API wrapper for
       the MLT plugin to call.
     - `CMakeLists.txt` — builds `libmltspout.dll`, links Spout2 SDK.

10.2. Spout SDK source is already vendored at
     `third_party/Spout2/SPOUTSDK/SpoutLibrary/` (pulled 2026-04-17).
     The NLE's CMake adds it via `add_subdirectory` so SpoutLibrary
     builds under MinGW from source every time:
     ```cmake
     # In nle/plugins/mlt_spout/CMakeLists.txt:
     add_subdirectory(
         ${CMAKE_SOURCE_DIR}/../third_party/Spout2/SPOUTSDK/SpoutLibrary
         ${CMAKE_BINARY_DIR}/SpoutLibrary
     )
     target_link_libraries(mltspout PRIVATE SpoutLibrary)
     ```
     The resulting MinGW-built `SpoutLibrary.dll` ships in
     `<install>/nle/` alongside `LaMoshPit_NLE.exe` and `libmltspout.dll`.
     The MSVC prebuilt binaries in
     `third_party/Spout-SDK-binaries_2-007-017_1/` are NOT used;
     they're deleted during Phase 0 housekeeping.

10.3. Consumer config: on VJ mode toggle, NLE destroys the current
     consumer and constructs a `multi` consumer with two children:
     - preview (for the on-screen VideoWidget)
     - spout (for the output)
     Both share the same producer graph, so decode happens once.

**Gotchas in Step 10:**

- **G10.1 — Spout SDK toolchain mismatch.** Current
  `third_party/spout/` is MSVC-built. MinGW can't link the MSVC
  `.lib`. Options:
  1. Rebuild Spout SDK with MinGW (documented procedure exists).
  2. Use the Spout DLL loaded dynamically via `LoadLibrary` +
     `GetProcAddress` for each API function (ABI-agnostic).
  3. Use SpoutLibrary (separate Spout variant specifically for
     cross-compiler compatibility). This is likely the easiest:
     https://github.com/leadedge/Spout2/tree/master/SPOUTSDK/SpoutLibrary
  Go with option 3: SpoutLibrary is distributed as a DLL with a
  C-only API. MinGW can load it directly.
- **G10.2 — Texture format.** Spout expects BGRA. MLT's
  `mlt_image_rgb24a` is BGRA on little-endian Windows. Match.
- **G10.3 — Frame pacing.** Spout doesn't buffer; if we push faster
  than OBS reads, OBS just gets the latest. Good — simplifies
  pacing. Don't add our own backpressure.
- **G10.4 — Spout sender name.** Use `"LaMoshPit"` consistently so
  existing OBS setups don't need reconfiguration.

**Exit criterion:** toggle VJ Mode on → OBS with Spout2 plugin
discovers `"LaMoshPit"` source → shows current frame. Toggle VJ
Mode off → Spout sender unregisters.

### Step 11 — VJ mode: hotkeys + transitions

**Goal:** complete VJ experience — number keys 1-9 cue tracks,
transition animation plays, Spout output shows the composited
result, Touch vs Switch mode work.

**Substeps:**

11.1. `nle/src/vj/VjModeController.{h,cpp}`:
     - Owns mode-on flag, Switch/Touch submode, current transition
       type, current duration.
     - Installs a Qt global event filter on app for Key_1..Key_9.
     - On keypress: signals `VjRouteRequested(trackIdx, isTouch, isPressed)`.

11.2. `MltController` gains a `setVjMode(bool)` method. When
     enabled:
     - Drop current preview consumer.
     - Rebuild producer graph with VJ compositor layer:
       * All tracks' playlists remain in the tractor.
       * A single `Mlt::Transition` is attached at the output.
       * Initial state: transition shows active track (index 0 by
         default).
     - Attach `multi` consumer: preview + Spout.

11.3. On keypress `N` with Switch mode:
     - Start an MLT transition from current-active to track N.
     - Animate the transition's `progress` property via `QTimeLine`
       or frame-synced update.
     - On completion, incoming becomes active.

11.4. On keypress `N` with Touch mode:
     - Hard-cut immediately to track N.
     - On key release, return to top-layer track (lowest-index
       track with content at the playhead).

**Gotchas in Step 11:**

- **G11.1 — Global hotkeys vs focus.** When NLE is not focused, key
  events don't arrive. For VJ mode specifically, the user may want
  global hotkeys (triggered even when another app is focused). Win
  32 `RegisterHotKey` provides this, but requires a message pump.
  For v1: window-focused only. Document that user must keep NLE
  focused.
- **G11.2 — Touch mode key release.** Qt delivers `keyReleaseEvent`
  only when the window has focus. Auto-repeat fires keyPress
  repeatedly. Filter auto-repeat: `if (event->isAutoRepeat()) return;`
- **G11.3 — Transition animation frame-syncing.** Driving transition
  progress from a `QTimeLine` (GUI thread) vs from the MLT consumer
  thread creates potential tearing. Use MLT's own transition
  animation — set in/out points, let MLT animate.
- **G11.4 — Consumer swap atomicity.** Destroying the old consumer
  and creating a new one mid-playback can flash a black frame.
  Mitigation: before swap, pause the clock; after swap, resume.
  Sub-100ms gap is acceptable for a VJ.
- **G11.5 — Hotkey mode UI state drift.** If the user toggles VJ
  mode off via the View menu but also has a key held down (Touch
  mode), the NLE might "get stuck" with a held track. Clear touch
  state on VJ-mode-off.

**Exit criterion:** Two tracks with different clips. Toggle VJ
mode on. Press 1 → track 0 plays, OBS shows it via Spout. Press 2
→ transition animates, OBS shows track 1 after animation. Switch
to Touch mode. Hold 1 → track 0 plays. Release 1 → returns to
top-layer track. All latencies ≤ 1 frame (33ms) from keypress.

### Step 12 — Deployment / installer

**Goal:** a single installer that ships both executables correctly.

**Substeps:**

12.1. Install tree layout (final):
```
<install>/
  LaMoshPit.exe                          # MSVC
  <MSVC Qt6/FFmpeg/vcpkg DLLs>
  qt.conf                                # points MSVC Qt to ./plugins
  plugins/                               # MSVC Qt plugins (vcpkg)
  resources/                             # shared — fonts, PNGs, icons
  nle/
    LaMoshPit_NLE.exe                    # MinGW
    qt.conf                              # points MinGW Qt to nle/plugins
    <MinGW Qt6 DLLs>
    <MinGW FFmpeg DLLs — different from root>
    plugins/                             # MinGW Qt plugins
    qml/                                 # QML modules (Shotcut + our overrides)
    lib/
      mlt/
        libmltcore-7.dll
        libmltavformat-7.dll
        libmlttransition-7.dll
        libmltxml-7.dll
        libmltlamosh.dll                 # our Mirror Left etc.
        libmltspout.dll                  # our Spout consumer
        ...
    share/
      mlt/
        profiles/
        presets/
```

12.2. NSIS or Inno Setup installer script in `packaging/`.
     - Copy both exes + all their runtime DLLs.
     - Register file association for `.lamosh` project files
       (LaMoshPit only).
     - No registry keys for MLT — keep everything relative.

12.3. Uninstaller: remove everything in the install directory, the
     Start Menu shortcut, and the file association. Do NOT touch
     user project folders.

**Gotchas in Step 12:**

- **G12.1 — DLL collisions in root vs nle/.** `LaMoshPit.exe` in
  root finds its Qt DLLs in root via standard search order.
  `LaMoshPit_NLE.exe` in `nle/` finds its Qt DLLs in `nle/`. But
  Windows default DLL search includes the app's directory first,
  then the System32, then PATH. A MinGW `Qt6Core.dll` in `nle/`
  and MSVC `Qt6Core.dll` in root: Windows gets confused if one
  tries to load from the other's location.
  Mitigation: at NLE startup, call `SetDllDirectoryA(app_dir)` to
  scope DLL search to the NLE's own directory. This blocks the
  MSVC-side Qt DLLs from ever being picked up by MinGW.
- **G12.2 — Installer running as admin.** If user installs to
  Program Files (requires admin) but then runs LaMoshPit as a
  normal user, the MLT plugin directory must be readable by
  normal users. Default NSIS installer permissions are fine;
  just verify.
- **G12.3 — Windows Defender SmartScreen.** Unsigned installers
  get a SmartScreen warning on first run. Long-term: get a code
  signing cert. Short-term: document the warning in the release
  notes.
- **G12.4 — Antivirus false positives.** MLT's avformat plugin
  loads FFmpeg DLLs that sometimes trigger AV heuristics. Known
  issue; document.

**Exit criterion:** installer runs on a clean Windows 11 VM
(without MSYS2 or vcpkg installed) → LaMoshPit launches → NLE
auto-spawns → all Phase 1 features work.

### Step 13 — Acceptance testing

Run all test scenarios from Part 1 Step 8 of the companion plan
(NLE_MLT_Migration_Plan_2026-04-17.md, Step 8). Scoring:

- Each gate pass/fail logged.
- Performance measured: end-to-end preview latency, CPU/GPU load
  during 2-track playback, render time for a 60s test sequence.
- Compared against the pre-rebuild LaMoshPit baseline (captured in
  Phase 0). Expected: dramatically better playback smoothness; same
  or better render quality; identical MB editor behavior.

### Part C gotchas reference (pointer)

All step-specific gotchas are inline above. The highest-severity
ones worth reviewing before starting Phase 1:

- G1.3 (AV quarantine of MSYS2 DLLs — breaks build silently)
- G3.6 (Job Object to prevent orphan NLE)
- G5.1 (GPL-3 header preservation — license audit risk)
- G6.1 (Atomic MLT XML writes)
- G10.1 (Spout SDK toolchain — use SpoutLibrary variant)
- G12.1 (DLL search path isolation — use SetDllDirectoryA)

---

## Part D — Phase 2: GStreamer + Vulkan (later)

Phase 2 is gated on Phase 1 being fully deployed and stable. Detailed
planning deferred; high-level architecture is in the companion
migration doc Part 2. Rough scope:

- Replace NLE's software-raster preview path with a GStreamer pipeline
  fed by a custom MLT consumer.
- Use GStreamer Vulkan plugins (`gst-plugins-bad`) for effects.
- Shared VkDevice between Qt RHI and GStreamer.
- Zero-copy display via `QSGVulkanTexture::fromNative`.
- Target: 4× 1080p60 + effects sustained at ≥ 55 fps preview.

Phase 2 does NOT touch the MSVC side. It's entirely an
`LaMoshPit_NLE.exe` evolution.

---

## Part E — Running gotcha index (by severity)

### Blocking (will cause a build-breaking bug or silent data loss)

- **G3.6** Orphan NLE process after LaMoshPit crash. Fix: Windows
  Job Object at spawn.
- **G5.1** GPL-3 headers missing → legal exposure. Fix: automated
  check script gated in CI.
- **G6.1** Atomic MLT XML writes → data loss on crash during save.
  Fix: `QSaveFile`.
- **G10.1** Spout MSVC→MinGW ABI break → plugin doesn't load. Fix:
  use SpoutLibrary variant.
- **G12.1** DLL search collides between MSVC and MinGW Qt. Fix:
  `SetDllDirectoryA` in NLE main.cpp.

### Degrading (works but poorly)

- **G1.3** AV quarantine → build time 10× slower, flaky. Fix: AV
  exclusions documented.
- **G2.1** NLE loads MSVC Qt plugins and crashes on startup. Fix:
  per-exe `qt.conf` + explicit plugin path.
- **G5.5** MLT env vars must be set before Factory::init. Fix:
  first lines of NLE main().
- **G7.1** Filesystem watcher spam. Fix: 200ms debounce timer.

### Cosmetic / polish

- G4.1 Menu item state drift on NLE close.
- G5.3 Translation files — drop for v1.
- G11.1 Global hotkeys — window-focused only for v1.

---

## Part F — Guard rail list (files / directories that are untouchable)

Enforced by the `scripts/phase0_guardrails.txt` check before every
commit during Phase 0, and by human review in Phase 1:

```
vcpkg/
vcpkg.json
vcpkg-configuration.json
third_party/
core/transform/FrameTransformer.cpp
core/transform/FrameTransformer.h
core/pipeline/DecodePipeline.cpp
core/pipeline/DecodePipeline.h
core/model/MBEditData.cpp
core/model/MBEditData.h
core/model/GlobalEncodeParams.cpp
core/model/GlobalEncodeParams.h
core/model/SelectionMap.cpp
core/model/SelectionMap.h
core/model/SelectionPreset.cpp
core/model/SelectionPreset.h
core/util/SelectionMorphology.cpp
core/util/SelectionMorphology.h
core/util/MapFrameSampler.cpp
core/util/MapFrameSampler.h
core/util/VideoMetaProbe.cpp
core/util/VideoMetaProbe.h
gui/BitstreamAnalyzer.cpp
gui/BitstreamAnalyzer.h
gui/widgets/MacroblockWidget.cpp
gui/widgets/MacroblockWidget.h
gui/widgets/GlobalParamsWidget.cpp
gui/widgets/GlobalParamsWidget.h
```

Any diff touching these files during Phase 0 or Phase 1 (except
strictly additive CMake target_sources changes needed for the
rebuild) requires explicit user approval.

---

## Part G — When to revisit this plan

Trigger conditions for reviewing / updating this doc:

1. After Step 1 complete: update with actual MSYS2 package versions
   that ended up working, and any AV/path issues encountered.
2. After Step 5 complete: document how many Shotcut files were
   forked vs recreated; update the license-header check regex.
3. After Step 13 complete: convert this plan into a retrospective
   note + begin Phase 2 detailed planning.
4. If any new dep added to vcpkg for the MSVC side: verify GPL-3
   compatibility and add to the dependency list in Part 0.

---

## Appendix — Implementation go-ahead checklist

Pre-flight (complete or to-be-done before starting):
- [x] GitHub repo has GPL-3.0 LICENSE file (2026-04-17).
- [x] MSYS2 installed at `C:\msys64\` (2026-04-17).
- [x] Spout SDK source vendored at
      `third_party/Spout2/SPOUTSDK/SpoutLibrary/` (2026-04-17).
- [x] Windows Defender exclusions added for `C:\msys64\` and the
      LaMoshPit repo folder (2026-04-17). NLE install path
      exclusion deferred to Phase 1 Step 12 once the install path
      is chosen.
- [x] Long-path support enabled via registry
      (`HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled = 1`,
      verified 2026-04-17).
- [x] ≥ 100 GB free on dev drive (382 GB free on C: verified
      2026-04-17).

Start-of-Phase-0 checklist:
- [ ] This plan reviewed and approved by user.
- [ ] Current LaMoshPit builds cleanly (Release + Debug).
- [ ] Current MB editor workflow validated via a test video
      (baseline for comparison after Phase 0 cleanup).
- [ ] User available for daily check-ins during Phase 0 and Phase 1
      Step 1 (highest-risk stretch).
