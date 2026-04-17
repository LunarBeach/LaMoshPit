# NLE Sequencer → MLT Framework Migration Plan

**Date drafted:** 2026-04-17
**Status:** Planning (no implementation yet)
**Scope:** Rebuild the NLE Sequencer on the MLT Framework as a separate
child process, eventually add a GStreamer + Vulkan zero-copy pipeline.
Macroblock Editor, Global Encode Params, MediaBin, and the project/
settings ecosystem remain untouched in the LaMoshPit core process.

**Decisions locked in by user (2026-04-17):**
- LaMoshPit's NLE module will be **GPLv3** (inherits Shotcut's license).
- Toolchain: **Option D — two-process split.** LaMoshPit core stays MSVC;
  `LaMoshPit_NLE.exe` is built with MSYS2/MinGW matching Shotcut's native
  toolchain. The OS-enforced process boundary is the strongest possible
  guarantee that the NLE's HW-acceleration stack cannot touch the MB
  editor / GEP bitstream-accurate pipeline.
- **No legacy project compatibility.** Projects created before this
  migration cannot be opened afterwards. Project format becomes MLT XML
  stored at `<projectFolder>/nle/sequence.mlt`.

---

## Part 0 — Why we're doing this

The hand-rolled NLE playback engine (`FrameRouter`, `TrackPlaybackChain`,
`SequencerClipDecoder`, `SequencerPlaybackClock`, `applyBlend`) will not
scale to proper multi-track, mixed-frame-rate, effects-capable preview.
Adobe solves this with the proprietary Mercury Playback Engine; the
open-source analogue that actually ships in production NLEs is:

- **MLT Framework** — non-linear engine: tractors, playlists, filters,
  transitions, profiles, consumers.
- **Shotcut** — mature GPLv3 Qt6 NLE built entirely on MLT; serves as the
  reference implementation we can fork from.
- **(Later) GStreamer + Vulkan** — zero-copy GPU pipeline for real-time
  VFX processing and display.

**Licensing reality:** Shotcut is **GPLv3**. Embedding its timeline /
player code means LaMoshPit's derivative NLE module becomes GPLv3.
MLT itself is **LGPLv3** (linkable from any license). Decision needed
before fork: confirm the LaMoshPit codebase is GPLv3-compatible.

---

## Part 1 — Phase 1: MLT Integration (Planning)

### 1.0 — Preservation guarantee

Nothing in the following subsystems changes in Phase 1. They keep their
current files, behaviour, and data model:

- Macroblock Editor: [core/model/MBEditData.h](../core/model/MBEditData.h),
  [gui/widgets/MacroblockWidget.cpp](../gui/widgets/MacroblockWidget.cpp),
  [core/transform/FrameTransformer.cpp](../core/transform/FrameTransformer.cpp).
- Global Encode Params: [core/model/GlobalEncodeParams.h](../core/model/GlobalEncodeParams.h),
  [gui/widgets/GlobalParamsWidget.cpp](../gui/widgets/GlobalParamsWidget.cpp).
- MediaBin: [gui/widgets/MediaBinWidget.cpp](../gui/widgets/MediaBinWidget.cpp).
- Project/Settings: [core/project/Project.cpp](../core/project/Project.cpp),
  [gui/SettingsDialog.cpp](../gui/SettingsDialog.cpp).
- Quick Mosh / analysis pipeline: [gui/BitstreamAnalyzer.cpp](../gui/BitstreamAnalyzer.cpp),
  [core/pipeline/DecodePipeline.cpp](../core/pipeline/DecodePipeline.cpp).

The NLE sequencer files to be replaced are all under
[core/sequencer/](../core/sequencer/) and
[gui/sequencer/](../gui/sequencer/). Only the **dock host** (where the
replacement sequencer lives in the main window) stays the same shape as
seen by MainWindow.

### 1.1 — Step-by-step plan

#### Step 1 — Clean-room prep

- Remove all `LAMOSH_TICK_DEBUG_LOG` instrumentation (clock, router,
  dock, main.cpp). It was useful for the custom engine; irrelevant now.
- Delete or archive-branch the current NLE sequencer implementation so
  the replacement is not contaminated by leftover half-fixes.
  - [core/sequencer/FrameRouter.*](../core/sequencer/FrameRouter.h)
  - [core/sequencer/TrackPlaybackChain.*](../core/sequencer/TrackPlaybackChain.h)
  - [core/sequencer/SequencerPlaybackClock.*](../core/sequencer/SequencerPlaybackClock.h)
  - [core/sequencer/SequencerClipDecoder.*](../core/sequencer/SequencerClipDecoder.h)
  - [core/sequencer/BlendModes.*](../core/sequencer/BlendModes.h),
    [core/sequencer/ClipEffects.*](../core/sequencer/ClipEffects.h)
  - [core/sequencer/SequencerRenderer.*](../core/sequencer/SequencerRenderer.h)
  - [core/sequencer/Transition.*](../core/sequencer/Transition.h)
  - [core/sequencer/SequencerClip.h](../core/sequencer/SequencerClip.h),
    [core/sequencer/SequencerTrack.h](../core/sequencer/SequencerTrack.h)
  - [core/sequencer/EditCommand.*](../core/sequencer/EditCommand.h)
  - All [gui/sequencer/](../gui/sequencer/) files
- **Keep:** [core/sequencer/SpoutSender.*](../core/sequencer/SpoutSender.h)
  (consumed unchanged by VJ mode in step 7), [core/sequencer/HwDecode.*](../core/sequencer/HwDecode.h)
  (moves into MB-editor-only use; no longer NLE's concern).
- **Save:** [core/sequencer/SequencerProject.cpp](../core/sequencer/SequencerProject.cpp)'s
  JSON schema as a reference — we will write a one-time importer from
  it to MLT XML in step 7 so users don't lose existing projects.

#### Step 2 — Two-process toolchain setup

LaMoshPit core stays as an MSVC + vcpkg build, unchanged from today.
A **second executable**, `LaMoshPit_NLE.exe`, is built independently
using **MSYS2/MinGW** — the same toolchain Shotcut ships with on
Windows. No toolchain mixing inside either binary.

Build system layout:

- Top-level `CMakeLists.txt` stays as-is (MSVC, builds `LaMoshPit.exe`).
- New subdir `nle/` has its **own top-level CMakeLists.txt** that is
  intentionally NOT `add_subdirectory`d from the top. Instead it is
  configured and built separately via a helper script
  `scripts/build-nle-msys2.sh`. Adapted from Shotcut's own
  [build-shotcut-msys2.sh](../../Shotcut_source_code/shotcut/scripts/build-shotcut-msys2.sh),
  with Shotcut-specific deployment steps stripped.
- The two build trees never see each other. They meet only at install
  time when both exe + their DLLs land in the same deployment folder.

MSYS2 setup (one-time per dev machine):

```
# Install MSYS2 (https://www.msys2.org/)
# Then in the MSYS2 MINGW64 shell:
pacman -S --needed mingw-w64-x86_64-toolchain
pacman -S --needed mingw-w64-x86_64-cmake
pacman -S --needed mingw-w64-x86_64-ninja
pacman -S --needed mingw-w64-x86_64-qt6-base
pacman -S --needed mingw-w64-x86_64-qt6-multimedia
pacman -S --needed mingw-w64-x86_64-qt6-quickcontrols2
pacman -S --needed mingw-w64-x86_64-mlt
pacman -S --needed mingw-w64-x86_64-ffmpeg
```

The Shotcut team maintains an exhaustive dependency list in their
build script; we copy that list verbatim for starting out.

Deliverable of this step: a smoke-test `LaMoshPit_NLE.exe` that
displays an empty Shotcut-style main window with no project, built
green in MSYS2. No application features yet. No MSVC / core
involvement. Proves the toolchain is configured and reproducible.

#### Step 3 — Two-process architecture

Two separately-built executables, coordinated via a local Qt socket.

**LaMoshPit.exe (MSVC, unchanged host):**
- Owns MainWindow, MB Editor, Global Encode Params, MediaBin, Quick
  Mosh, settings, project folder lifecycle.
- Adds a tiny subsystem `gui/nle_bridge/` containing:
  - `NleLauncher` — spawns + supervises `LaMoshPit_NLE.exe` via
    `QProcess`. Restarts on crash (optional). Shuts down on close.
  - `NleControlChannel` — `QLocalServer` (named pipe); accepts one
    connection from the NLE at startup, exchanges JSON messages.
- Adds `View` menu items:
  - `View → NLE Sequencer` (checkable, default checked).
    → `NleControlChannel::send({"cmd":"show"})` /
     `{"cmd":"hide"})` to toggle NLE window visibility.
  - `View → NLE Media Bin` (checkable, default checked).
    → `{"cmd":"media_bin.show"}` / `{"cmd":"media_bin.hide"}`.
  - `View → VJ Mode` (checkable, default unchecked).
    → `{"cmd":"vj_mode.on"}` / `{"cmd":"vj_mode.off"}`.

**LaMoshPit_NLE.exe (MinGW, new):**
- Essentially Shotcut forked, with Shotcut-specific branding/menus
  stripped. See Step 5 for file-by-file fork map.
- Adds `nle/ipc/` containing:
  - `CoreControlClient` — `QLocalSocket`, connects to LaMoshPit's
    server at startup. Dispatches incoming JSON to slots.
  - `MediaBinDock` — reads project folder via `QFileSystemWatcher`;
    mirrors content of LaMoshPit core's MediaBin. Has its own
    show/hide slot controlled by IPC.
  - `VjModeController` — owns the VJ-mode toggle state + hotkey
    binding; swaps MLT consumers between preview+Spout pipelines.
    See Step 7 for details.

**Project folder layout (shared between both processes):**

```
<projectFolder>/
  project.lamosh.json        (MB editor state, settings, GEP; LaMoshPit owns)
  moshVideoFolder/           (shared media — both read/write)
  thumbnails/                (shared — both read/write)
  nle/
    sequence.mlt             (MLT XML — NLE reads/writes; core never touches)
    presets/                 (NLE's filter/transition presets)
```

**IPC protocol (JSON over named pipe):**

Direction LaMoshPit → NLE (commands):
- `{"cmd":"open_project","path":"<abs_project_folder>"}` — issued on
  app start and on any project switch. NLE loads
  `<path>/nle/sequence.mlt`, creating a blank one if missing.
- `{"cmd":"show"}` / `{"cmd":"hide"}` — window visibility.
- `{"cmd":"media_bin.show"}` / `{"cmd":"media_bin.hide"}`.
- `{"cmd":"vj_mode.on"}` / `{"cmd":"vj_mode.off"}`.
- `{"cmd":"focus"}` — bring NLE window to front (used when MB editor
  finishes Quick Mosh and wants user to continue in NLE).
- `{"cmd":"shutdown"}` — issued on LaMoshPit quit; NLE saves MLT XML
  and exits. LaMoshPit waits up to 2s for graceful exit, then kills.

Direction NLE → LaMoshPit (events):
- `{"evt":"ready","version":"..."}` — first message, on connect.
- `{"evt":"render_started","output":"<path>"}` — user clicked Render.
- `{"evt":"render_finished","output":"<path>"}` — MP4 is on disk;
  LaMoshPit auto-adds to MediaBin.
- `{"evt":"project_dirty","dirty":true|false}` — propagates unsaved-
  changes state so LaMoshPit's title bar can show a dot.
- `{"evt":"log","level":"warn","msg":"..."}` — pipe NLE log messages
  into LaMoshPit's status bar if desired.

#### Step 4 — Build system + deployment

**LaMoshPit.exe build (unchanged from today):**
- Keep MSVC + vcpkg + existing CMakeLists.txt.
- Add new sources under `gui/nle_bridge/` for the launcher and IPC
  server. Everything compiles against the existing Qt6 components
  already in use; no new vcpkg dependencies required.

**LaMoshPit_NLE.exe build (new, MinGW):**
- Added `scripts/build-nle-msys2.sh` (adapted from Shotcut's script).
  Handles: checkout of MLT/FFmpeg/etc. source into a sibling build
  tree, configure + compile in dependency order, installs into a
  self-contained staging directory.
- `nle/CMakeLists.txt` is the top-level for this exe. It uses
  pkg-config to find MLT (matching Shotcut line 60):
  `pkg_check_modules(mlt++ REQUIRED IMPORTED_TARGET mlt++-7)`.

**Deployment layout (one installer ships both):**

```
<install>/
  LaMoshPit.exe                       (MSVC build artifact)
  LaMoshPit_NLE.exe                   (MinGW build artifact)
  <MSVC Qt6 DLLs, FFmpeg DLLs, vcpkg runtime DLLs>
  nle/                                (NLE-only runtime, isolated)
    <MinGW Qt6 DLLs>
    lib/mlt/
      libmltavformat-7.dll
      libmltcore-7.dll
      libmlttransition-7.dll
      libmltxml-7.dll
      libmltvj.dll                    (LaMoshPit-specific plugin, Step 7)
      libmltspout.dll                 (LaMoshPit-specific Spout consumer)
      ... (other MLT modules)
    share/mlt/profiles/
      atsc_720p_30                    (MLT ships a library of these)
      atsc_1080p_60
      ...
    share/mlt/presets/                (transition/filter preset library)
```

The `nle/` subdirectory is entirely MinGW-land. LaMoshPit.exe never
loads DLLs from it. `LaMoshPit_NLE.exe` sets `MLT_REPOSITORY` + `MLT_DATA`
at startup to point into this isolated subdirectory, so no global
environment variables are required at install time.

#### Step 5 — Fork Shotcut's NLE subsystem

Per the user's instruction: **the NLE module should think of itself as
a fork of Shotcut**. Concrete files to pull in under GPL3 attribution
(all paths relative to `C:\Users\Thelu\Desktop\CodingProjects\Shotcut_source_code\shotcut\src`):

| Shotcut source | Purpose | LaMoshPit destination |
|---|---|---|
| `mltcontroller.{h,cpp}` | MLT Factory init, Producer/Consumer lifecycle | `nle/controllers/MltController.*` |
| `mltxmlchecker.{h,cpp}` | Validate MLT XML on load | `nle/controllers/MltXmlChecker.*` |
| `models/multitrackmodel.{h,cpp}` | QAbstractItemModel over Tractor+Playlists | `nle/models/MultitrackModel.*` |
| `commands/timelinecommands.{h,cpp}` | Edit commands (append/insert/trim/split/move) | `nle/commands/TimelineCommands.*` |
| `commands/playlistcommands.{h,cpp}` | Playlist-level commands | `nle/commands/PlaylistCommands.*` |
| `commands/filtercommands.{h,cpp}` | Filter add/remove/modify | `nle/commands/FilterCommands.*` |
| `controllers/filtercontroller.{h,cpp}` | Filter UI ↔ Mlt::Filter bridge | `nle/controllers/FilterController.*` |
| `docks/timelinedock.{h,cpp}` | Timeline UI orchestration | `nle/docks/TimelineDock.*` |
| `docks/filtersdock.{h,cpp}` | Filters panel | `nle/docks/FiltersDock.*` |
| `docks/encodedock.{h,cpp}` | Render dialog (plumb through GEP — see step 6) | `nle/docks/EncodeDock.*` |
| `jobs/meltjob.{h,cpp}`, `jobs/encodejob.{h,cpp}` | Export subprocess wrappers | `nle/jobs/*` |
| `videowidget.{h,cpp}` (OpenGL/D3D variants) | Preview player | `nle/widgets/VideoWidget.*` |
| `player.{h,cpp}` | Transport controls | `nle/widgets/Player.*` |
| `qml/views/timeline/*` | Timeline QML | `nle/qml/views/timeline/*` |

For each ported file:
1. Preserve the full Shotcut/Meltytech copyright header + GPLv3 notice.
2. Add a second header line: "Forked into LaMoshPit on 2026-04-17; see
   project CHANGELOG for modifications."
3. Rename top-level namespaces to avoid collisions (`Shotcut::` →
   `lamosh::nle::` where present).

#### Step 6 — Bridge to LaMoshPit's surrounding systems

This is the actual novel work of Phase 1. Five bridges:

**Bridge A — Shared MediaBin (dynamically linked)**
- Physical model: the project's `moshVideoFolder/` + `thumbnails/` is
  the single source of truth. Both processes watch it with
  `QFileSystemWatcher`.
- When LaMoshPit core imports a new file (via its existing MediaBin
  widget), it writes it into `moshVideoFolder`. NLE's file watcher
  fires → NLE's MediaBin refreshes within milliseconds.
- Same in reverse: if the NLE accepts a MediaBin drag-drop from the
  OS, it writes into `moshVideoFolder` and core refreshes.
- Drag-drop from NLE's MediaBin onto its timeline works locally
  (Shotcut already supports `text/uri-list`); no IPC involved.
- `View → NLE Media Bin` toggle (checkable menu item in LaMoshPit)
  sends an IPC message that shows/hides the NLE's MediaBin dock only.
  LaMoshPit core's own MediaBin has its own independent show/hide
  (unchanged from today).

**Bridge B — Project folder lifecycle**
- When LaMoshPit switches project (menu → Open Project, or
  --project-path on first launch), it sends
  `{"cmd":"open_project","path":"<abs>"}` to NLE.
- NLE loads `<abs>/nle/sequence.mlt`. If missing, creates an empty one
  and saves it before first preview.
- NLE autosaves its own project state on every mutation (Shotcut
  already does this via QSettings + dirty-flag). LaMoshPit core does
  NOT bundle NLE state into its own project JSON — the two are
  persisted independently.
- No migration path from today's sequencer JSON. Pre-migration
  projects simply won't open their sequencer side.

**Bridge C — Global Encode Params → MLT avformat consumer (via IPC)**
- When user clicks Render in NLE: the NLE sends
  `{"evt":"render_request","wants_gep":true}` to core.
- Core responds with `{"rsp":"gep_config",...}` carrying the current
  Global Encode Params serialised as JSON (codec, bitrate, profile,
  container, preset, etc.).
- NLE translates that into MLT avformat consumer arguments and uses
  its ported `EncodeJob` to run the render. The `melt.exe` used for
  the export lives in the NLE's MinGW runtime — GEP never crosses the
  MSVC/MinGW boundary as code, only as a JSON config blob.
- Alternate mode: user can still pick from Shotcut's built-in preset
  library if they want a non-GEP render. A radio toggle in the Encode
  dialog selects `LaMoshPit GEP` vs `Shotcut preset library`.
- When render finishes, NLE emits `{"evt":"render_finished","output":
  "<path>"}`. Core auto-adds the result to MediaBin (optionally opens
  it in the MB editor, matching today's post-render UX).

**Bridge D — MainWindow integration**
- Today's `m_seqProject` + `m_seqDock` are removed from MainWindow.
- Replaced by `m_nleLauncher` (spawns NLE) + `m_nleControl` (IPC
  server). Both live for the lifetime of MainWindow.
- `MainWindow::setActiveProject(...)` sends the new project path to
  NLE via IPC.
- Render button in the existing MB-editor UI still renders via GEP
  (unchanged). The NLE has its own Render button; those two render
  paths never mix.

**Bridge E — MB Editor ↔ NLE coexistence**
- The two features sit on opposite sides of a process boundary. No
  memory or framework is shared. The NLE can freely use Vulkan /
  GStreamer / GPU pipelines (Phase 2) without risk of touching the
  MB editor's bitstream-accurate decode path.
- The only data path between them is the shared MediaBin folder:
  1. User edits a clip in MB editor, renders via Quick Mosh → output
     lands in MediaBin → NLE sees it via file watcher → user can drag
     it onto the timeline.
  2. User renders a sequence in NLE → output lands in MediaBin → user
     can open it in the MB editor for further bitstream hacking.
- This is actually **the cleanest architecture we've had** — the two
  feature sets are genuinely orthogonal, and the process split makes
  that orthogonality enforceable by the OS.

#### Step 7 — Migrate existing NLE Sequencer features

Each current feature, explicitly addressed:

- **Per-clip opacity / blend / fade** → native MLT. Direct map in
  Bridge B. No new code.
- **Effects Rack + Mirror Left** → write a custom MLT filter module
  (`libmltlamosh.dll`) hosting our effects. Ship as a bundled plugin
  in `lib/mlt/`. Structure follows the frei0r or builtin MLT filter
  examples. Future effects are added by appending to this plugin.
- **Sequence Frame Rate dropdown** → maps directly to `Mlt::Profile`
  swap. Already supported in the ported EncodeDock.
- **Live VJ mode (Switch/Touch hotkeys, Spout, transitions)** →
  **rebuilt clean on MLT primitives.** No FrameRouter carry-over.
  Three pieces:
  1. **`VjModeController`** (lives in `nle/vj/`) — owns the mode-on
     flag + hotkey state. Installs a global shortcut filter on 1-9
     keys when active.
  2. **Output routing via MLT transition** — when VJ mode is on, the
     NLE swaps its normal preview graph for a VJ graph: all
     track-playlists stay decoded (warm), but a single
     `Mlt::Transition` at the output picks between active + incoming.
     On a hotkey, the transition animates from current-active to
     incoming using the user's chosen transition type (luma/composite/
     crossfade/wipe — MLT has these built in) and duration (from the
     existing Transition Panel UI, ported from today's LaMoshPit).
     When the animation completes, incoming becomes the new active.
  3. **`consumer_spout` MLT plugin** — new C-language MLT consumer
     packaged as `libmltspout.dll` under `nle/lib/mlt/`. Takes each
     rendered frame's BGRA buffer and hands it to
     `SpoutSender::onFrameReady` (port of our existing
     `core/sequencer/SpoutSender.*` — pure Qt code, drops straight
     into the MinGW build).
  4. **Dual output via MLT `multi` consumer** — in VJ mode the active
     MLT consumer is a `multi` that fans every frame to (a) the NLE
     preview window and (b) the Spout consumer. No double-decode.
- **Touch vs Switch mode** — both semantics map to the VJ graph above.
  Switch: on key press, animate to the key's track. Touch: on key
  press, cut instantly to the key's track; on key release, return to
  the top-layer track.
- **Effects on clips in VJ mode** → same MLT filter graph runs, so
  effects apply uniformly whether the output goes to preview only,
  Spout only, or both.
- **Ruler, playhead, timeline zoom, clip drag** → use Shotcut's
  QML timeline, already has all this.
- **Properties panel (opacity/blend/fade editors)** → Shotcut's
  FiltersDock + properties panel covers this; our custom knobs move
  from today's `SequencerClipPropertiesPanel` into the Shotcut-style
  filter panel.

#### Step 8 — Testing gates

Before declaring Phase 1 done, each of these must work:

1. **App launch** — LaMoshPit.exe starts, auto-launches
   LaMoshPit_NLE.exe, the NLE window appears on screen, IPC handshake
   succeeds. Closing LaMoshPit cleanly closes the NLE.
2. **View menu toggles** — `View → NLE Sequencer` hides/shows the
   NLE window without killing the process. `View → NLE Media Bin`
   hides/shows just the MediaBin dock within the NLE. `View → VJ Mode`
   swaps the NLE's consumer graph.
3. **Shared MediaBin sync** — import a clip in LaMoshPit's MediaBin;
   it appears in the NLE's MediaBin within 1s. Same in reverse.
4. **Smooth preview** — new project, drop 2 clips (mixed 24/30/60
   fps), scrub, play, stop. No stutter, no feedback spiral, no
   catch-up loop artifacts. Matches Shotcut's own preview quality.
5. **Opacity/blend live** — change opacity slider mid-playback;
   preview updates smoothly with no stall.
6. **Effects** — apply Mirror Left (ported as MLT filter) to a clip;
   visible in preview and in baked output.
7. **GEP render** — click Render, choose LaMoshPit GEP mode; output
   MP4 respects the user's current GEP config (codec, bitrate,
   profile, etc.). Compare to a Quick Mosh render of the same input
   — encoder settings identical.
8. **VJ mode** — press number keys 1-9; transitions fire with the
   chosen transition type and duration; Spout output is live and
   visible in OBS with ≤ 1 frame latency from keypress.
9. **MB Editor unchanged** — open an MP4 in the MB editor, apply
   edits, Quick Mosh render, behaviour identical to before migration.
10. **Process isolation** — kill LaMoshPit_NLE.exe via Task Manager
    while LaMoshPit.exe is running an MB edit. MB edit completes
    normally. LaMoshPit notices the NLE crash and offers to relaunch.
11. **Fresh-install deployment** — run the installer on a clean
    Windows VM, launch, complete scenarios 1-9. No "DLL not found"
    errors from either process.

---

## Part 2 — Phase 2: GStreamer + Vulkan Zero-Copy (Planning)

**Goal:** beyond Shotcut's baseline — a zero-copy GPU pipeline that
keeps decoded frames on the GPU from decode through effects through
display. Windows-only initially; Linux/macOS ports are community
contributions.

### 2.1 — Architecture

```
┌─ GUI Thread (Qt6 RHI = Vulkan) ──────────────────────────────────────┐
│                                                                      │
│   QML TimelineDock     QML VideoView ──→ QSGVulkanTexture display    │
│          │                    ▲                                      │
└──────────┼────────────────────┼──────────────────────────────────────┘
           │ edit commands       │ VkImage handles (zero-copy)
           ▼                    ▲
┌─ Worker Thread (MLT event loop) ────────────────────────────────────┐
│                                                                      │
│   Mlt::Controller   →   Mlt::Consumer  "gst" (custom)                │
│                                 │                                    │
│                                 ▼                                    │
│   GStreamer appsrc  →  d3d11tovulkan  →  vulkan filters  →  appsink │
│                                                        ↑             │
│                                        custom VFX (Mirror, etc.)     │
│                                        as GstVulkanShaderFilter      │
└─────────────────────────────────────────────────────────────────────┘
           ▲
           │ VkInstance + VkDevice SHARED with Qt RHI
           │  via QVulkanInstance + gst_context_new_vulkan_device
```

### 2.2 — Step-by-step plan

**Step 2.1 — Toolchain prep**
- Install LunarG Vulkan SDK (≥ 1.3). Add to system PATH.
- Install GStreamer 1.24+ via MSVC binaries (NOT MinGW — must match
  Qt/MLT toolchain). Include `gst-plugins-bad` (has Vulkan elements).
- Add vcpkg ports for `gstreamer`, `gst-plugins-base`, `gst-plugins-bad`
  if not already present.
- Verify with a standalone smoke test: `gst-launch-1.0 videotestsrc !
  d3d11upload ! d3d11tovulkan ! vulkansink` should display a test
  pattern in a native Vulkan window.

**Step 2.2 — Shared Vulkan device (the foundation)**
- In `main.cpp`, create the `QVulkanInstance` explicitly before any
  window is shown. Set `QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan)`.
- At NLE dock init, obtain the `VkInstance` and `VkDevice` from
  `QVulkanInstance::vkInstance()` + the first `QQuickWindow`'s RHI.
- Create a `GstVulkanInstance` / `GstVulkanDevice` from these handles
  via `gst_vulkan_instance_new_with_instance()` and
  `gst_vulkan_device_new_with_device()`.
- Inject into GStreamer's bus via `GstContext` so every Vulkan plugin
  uses the same device.

**Step 2.3 — MLT → GStreamer bridge (custom consumer)**
- Write a new MLT consumer plugin `consumer_gst` (C module, compiled
  into `libmltgst.dll`).
- The consumer receives `mlt_frame` structs from MLT, extracts the
  `mlt_image` buffer, pushes it to a GStreamer `appsrc` element as a
  `GstBuffer`.
- For hardware-decoded frames (when MLT's avformat input uses
  D3D11VA), the `mlt_frame` already holds a D3D11 texture. Wrap as
  `GstD3D11Memory`, feed directly to `d3d11tovulkan` — zero CPU touch.
- For software-decoded frames, the consumer uploads CPU pixels via
  `d3d11upload` → `d3d11tovulkan`.

**Step 2.4 — Vulkan VFX pipeline**
- Migrate each `ClipEffect` (Mirror Left and future) to a Vulkan
  compute or fragment shader wrapped as a `GstVulkanShaderFilter`
  subclass.
- Pipeline shape at runtime:
  `appsrc → d3d11tovulkan → vulkanupload (if needed) → {mirror_left, ...}
   → vulkandownload (only if final stage) → appsink`
- Filter ordering follows MLT filter attachment order, so the same
  filter graph the user built in Phase 1 renders on GPU in Phase 2.
- Blend (opacity, fade, blend mode) moves to a single merged shader
  at the end of each track — replaces CPU `applyBlend`.

**Step 2.5 — Zero-copy display**
- QML `VideoView` is a custom `QQuickItem` whose `updatePaintNode`:
  1. Pulls the latest `GstSample` from the `appsink`.
  2. Extracts the `VkImage` from the `GstVulkanImageMemory`.
  3. Wraps it: `QNativeInterface::QSGVulkanTexture::fromNative(vkImage,
     vkLayout, window, size)`.
  4. Assigns to a `QSGSimpleTextureNode`.
- No pixel data ever leaves the GPU between decode and display.

**Step 2.6 — Fallback path**
- Systems without Vulkan 1.3 / lacking `d3d11tovulkan` fall back to the
  Phase 1 path (CPU MLT consumer + OpenGL or raster display). This is
  important for community/older hardware.
- Detect at startup with `QVulkanInstance::supportsApi()` + probe
  GStreamer plugin availability.

**Step 2.7 — Performance validation**
- Metric: sustain 4 tracks, each 1080p60, with Mirror Left + opacity
  blend, at ≥ 55 fps preview on a mid-range GPU (RTX 3060 class).
- Measure end-to-end frame latency (decode → display) — target ≤ 50 ms.
- Any stall > 100 ms is a bug.

### 2.3 — Risks & open questions

- **D3D11VA interop on older GPUs** — not every Windows GPU driver
  exposes `VK_KHR_external_memory_win32` + D3D11 keyed mutex in a way
  `d3d11tovulkan` can use. Phase 2 step 2.1 smoke test catches this.
- **Qt6 Vulkan RHI maturity** — Qt 6.5+ recommended; some QML features
  still OpenGL-only. Validate that our minimum required Qt version
  supports Vulkan backend for everything we use.
- **Shared VkDevice lifetime** — tight coupling between Qt and
  GStreamer Vulkan contexts means teardown order matters. Needs
  explicit device-loss handling.
- **GPL licensing cascade** — GStreamer is LGPL2.1; fine. But if Phase
  1 made LaMoshPit GPLv3 (Shotcut fork), Phase 2 doesn't change that.

---

## Part 3 — Upstreaming strategy

If Phase 2 works, the obvious next step is to upstream the
GStreamer/Vulkan pipeline as a patch set against Shotcut proper.
Meltytech (Shotcut maintainers) have been open to GPU-path proposals
historically. Contributing back:

1. Write up the Vulkan consumer + shader-filter framework as a
   self-contained module that compiles as a Shotcut optional feature.
2. Submit PR to mltframework/shotcut with a feature flag
   (`--enable-vulkan-preview`).
3. Document the Windows build path — Shotcut's current MinGW scripts
   may need MSVC-compat work to pick this up cleanly.

If accepted: open-source VFX users on Windows get a real-time GPU
preview path without changing NLEs. If rejected: the code remains in
LaMoshPit only, still GPLv3, available to anyone who wants to fork it.

---

## Part 4 — Decisions locked in (2026-04-17)

- ✅ **GPLv3 licensing for the whole repo.** Add a top-level `LICENSE`
  (GPL-3.0) before implementation begins, so commits that bring in
  Shotcut code are already under a matching license. Compatibility
  verified with every current dependency:
  - x264 (GPL-2-or-later) → ✅ upgradable to GPL-3
  - h264bitstream (LGPL-2.1-or-later) → ✅ linkable from GPL-3
  - FFmpeg (LGPL-2.1-or-later; becomes GPL-2+ with `--enable-gpl`) → ✅
  - Qt6 (LGPL-3.0) → ✅
  - KDDockWidgets (GPL-2-or-later) → ✅
  - Spout2 (BSD-style permissive) → ✅
  - MLT (LGPL-2.1-or-later) → ✅ (Phase 1)
  - Shotcut (GPL-3) → ✅ exact match (Phase 1)
- ✅ **Toolchain: Option D (two-process split).** LaMoshPit.exe stays
  MSVC; LaMoshPit_NLE.exe is built with MSYS2/MinGW matching
  Shotcut's native toolchain. The OS process boundary guarantees the
  MB editor's bitstream-accurate pipeline cannot be touched by the
  NLE's HW-accel stack.
- ✅ **vcpkg overlay + bitstream-surgery patches fully preserved.**
  LaMoshPit.exe's existing vcpkg tree, overlay ports, and the 6
  bitstream-surgery hooks (port-version 3 as of 2026-04-16) stay
  byte-identical. The NLE has its own independent MSYS2/pacman
  dependency tree — it cannot affect, override, or require rebuilds
  of the patched FFmpeg / h264bitstream that the MB editor relies on.
  This is an incidental but important benefit of Option D: the MB
  editor's specialised FFmpeg build can stay pinned to its
  known-good version forever, while the NLE independently tracks
  whatever MLT/Shotcut upstream needs.
- ✅ **No legacy compatibility.** Pre-migration project sequencer JSON
  is discarded on first launch of the migrated build. Users start
  with an empty NLE sequence per project.

Phase 1 can begin with Step 1 (cleanup) whenever the user gives the
implementation go-ahead.

---

## Appendix A — File-by-file preservation map

### LaMoshPit.exe (MSVC) — preserved unchanged

- `core/model/*` (MBEditData, GlobalEncodeParams, SelectionMap, SelectionPreset)
- `core/pipeline/DecodePipeline.*`
- `core/transform/FrameTransformer.*`
- `core/logger/ControlLogger.*`
- `core/presets/*`
- `core/project/Project.*` (gains a `nleProjectPath()` helper pointing
  at `<projectFolder>/nle/sequence.mlt` — that's the only change)
- `core/util/*`
- `gui/widgets/MacroblockWidget.*`
- `gui/widgets/MediaBinWidget.*` (adds file-watcher for NLE sync)
- `gui/widgets/GlobalParamsWidget.*`
- `gui/BitstreamAnalyzer.*`
- `gui/SettingsDialog.*`
- `gui/AppFonts.*`
- `gui/undo/*`
- `gui/dialogs/*`
- `main.cpp`

### LaMoshPit.exe — new additions

- `gui/nle_bridge/NleLauncher.{h,cpp}` — spawns + supervises the NLE
  process via `QProcess`.
- `gui/nle_bridge/NleControlChannel.{h,cpp}` — `QLocalServer` with a
  JSON message pump.
- `gui/nle_bridge/NleViewMenu.{h,cpp}` — adds/manages the View menu
  items for NLE toggling.

### LaMoshPit.exe — deleted

All of `core/sequencer/` and `gui/sequencer/` goes away.
`SpoutSender.*` is ported into the NLE's MinGW tree (see below).

### LaMoshPit_NLE.exe (MinGW) — new executable, structure

- Forked from Shotcut's `src/` subtree:
  - `nle/commands/` ← Shotcut `src/commands/`
  - `nle/controllers/` ← Shotcut `src/controllers/` + MltController
  - `nle/docks/` ← Shotcut `src/docks/` (minus Shotcut-specific docks)
  - `nle/jobs/` ← Shotcut `src/jobs/`
  - `nle/models/` ← Shotcut `src/models/`
  - `nle/qml/` ← Shotcut `src/qml/`
  - `nle/widgets/` ← Shotcut `src/widgets/` + `videowidget.*` +
    `player.*`
  - `nle/main.cpp` ← adapted from Shotcut `src/main.cpp`
- LaMoshPit-specific subdirs:
  - `nle/ipc/CoreControlClient.{h,cpp}` — `QLocalSocket` client that
    connects to LaMoshPit.exe's IPC server.
  - `nle/vj/VjModeController.{h,cpp}` — VJ mode logic (hotkey
    handling, transition orchestration, consumer swap).
  - `nle/vj/SpoutSender.{h,cpp}` — ported from
    `core/sequencer/SpoutSender.*` — pure Qt code, drops straight into
    MinGW build with no changes.
- LaMoshPit-specific MLT plugins (new C modules):
  - `nle/plugins/mlt_lamosh/` — custom MLT filter plugin packaging
    the Mirror Left effect (and future custom effects).
  - `nle/plugins/mlt_spout/` — custom MLT consumer wrapping
    SpoutSender.

### Shared on disk (no shared code or memory)

- `<projectFolder>/moshVideoFolder/` — media storage, read+write by
  both processes.
- `<projectFolder>/thumbnails/` — thumbnails, read+write by both.
- `<projectFolder>/nle/sequence.mlt` — NLE's canonical project file,
  owned exclusively by LaMoshPit_NLE.exe.

---

## Appendix B — Shotcut reference file index

All paths relative to `C:\Users\Thelu\Desktop\CodingProjects\Shotcut_source_code\shotcut\`:

- MLT bootstrap: `src/main.cpp:123-136` (Windows DLL search path),
  `src/mltcontroller.cpp:69` (`Mlt::Factory::init()`),
  `src/mltcontroller.h:60-219` (Controller class).
- Project load/save: `src/mltcontroller.cpp:486-510` (xml consumer).
- Timeline model: `src/models/multitrackmodel.h:47`,
  `src/models/multitrackmodel.cpp`.
- Timeline UI (QML): `src/qml/views/timeline/timeline.qml`.
- Export: `src/jobs/meltjob.h`, `src/jobs/encodejob.h`,
  `src/docks/encodedock.h`, preset library in `filter-sets/`.
- Build: `CMakeLists.txt` (top-level), `src/CMakeLists.txt`,
  `scripts/build-shotcut-msys2.sh` (shows dependency list).
- License: `COPYING` (GPLv3).
