# LaMoshPit NLE Sequencer — Build Summary (2026-04-15)

Complete rewrite of the NLE timeline feature from the ground up, plus a
first-class VJ live-performance mode and application-wide settings.

Scope split across six phases, all landed in one day. The original NLE
(single-decoder, one timeline, no transitions, no live output) was
removed entirely; the new architecture below replaces it.

---

## Phase 0 — Foundation

Clean the slate, build the data model.

**Removed:** `core/sequence/` (Sequence, NleCompositor, SequenceRenderer),
`gui/widgets/Nle*`, `gui/widgets/SequenceRenderDialog`.

**Added under `core/sequencer/`:**
- `Tick.h` — master timebase at `AVRational{1, 90000}` (MPEG standard,
  represents 23.976 / 29.97 / 50 / 60 fps without drift).
  `ticksToStreamTs`, `streamTsToTicks`, `frameDurationTicks` helpers wrap
  `av_rescale_q_rnd` for precise conversions.
- `SequencerClip.h` — struct with source path, probed timebase/fps/duration,
  trim window, timeline position, speed stub.
- `SequencerTrack.h` — ordered clip list + `clipIndexAtTick` +
  `repackContiguous` to maintain the no-gaps invariant.
- `SequencerProject.{h,cpp}` — up to 9 tracks, active track index, loop
  region, output framerate, undo/redo command stack.
- `EditCommand.{h,cpp}` — base + 8 subclasses: `AddTrackCmd`,
  `RemoveTrackCmd`, `AppendClipCmd`, `InsertClipCmd`, `RemoveClipCmd`,
  `MoveClipCmd`, `TrimClipCmd`, `SplitClipCmd`. All mutations flow through
  `executeCommand`.

---

## Phase 1 — HW Decode + Single-Track Playback

Prove the playback engine end-to-end.

**Added under `core/sequencer/`:**
- `HwDecode.{h,cpp}` — D3D11VA hardware decoder attach + SW fallback +
  `transferHwFrameToSw` to bring HW frames to CPU for swscale.
- `SequencerClipDecoder.{h,cpp}` — one open decoder per clip; HW decode
  with SW fallback; tick-based seek via `av_seek_frame(BACKWARD)` +
  forward-decode loop. Produces `QImage::Format_ARGB32` (memory-equivalent
  to BGRA on little-endian Windows). Added `produceImage` flag so
  background tracks can stay sync'd without the swscale cost (Phase 3
  uses this).
- `SequencerPlaybackClock.{h,cpp}` — `QTimer` + `QElapsedTimer` master
  tick emitter at project FPS, drift-free accumulator, loop region
  wrap-around built in.

**Added under `gui/sequencer/`:**
- `SequencerPreviewPlayer.{h,cpp}` — `QWidget` with letterboxed
  `drawImage` paintEvent.
- Initial `SequencerDock` with Play/Stop/seek/time readout and a
  temporary "Add Clip…" button for testing.

---

## Phase 2 — Multi-Track Timeline UI

Full graphics-view timeline with drag-drop, trim, reorder, zoom, and
cross-track moves.

**Added under `gui/sequencer/`:**
- `SequencerTimelineConstants.h` — scene-coord system (100 px/sec at 1x
  zoom, 58 px track height, 26 px ruler), function-local static
  `timelineZoomXRef()` as the single source of truth for horizontal zoom.
- `SequencerClipItem.{h,cpp}` — `QGraphicsItem`; 6px edge zones switch
  between Move / TrimLeft / TrimRight on mouse-down. Passes dx + dy back
  to the view for cross-track drag.
- `SequencerRulerItem.{h,cpp}` — time axis with adaptive tick spacing
  (0.1s / 0.2s / 0.5s / 1s / 2s / 5s / 10s / 30s / 1m / 5m ladder), loop
  region brackets when armed.
- `SequencerPlayheadItem.{h,cpp}` — red vertical line with triangular
  handle, auto-follows the clock.
- `SequencerTimelineView.{h,cpp}` — `QGraphicsView` orchestration:
  drops from MediaBin (text/uri-list), clip-item drag callbacks with
  cross-track logic, keyboard (Delete / S-split), Ctrl+wheel zoom
  anchored at cursor with `kMinZoomX`=0.05 and `kMaxZoomX`=50.
- `SequencerTrackHeader.{h,cpp}` — left column with 1-9 badge + name +
  active-track highlight + Add Track button.

**Crash fix landed during Phase 2:** deferred item deletion in
`rebuildClipItems()` via `QTimer::singleShot(0, ...)` to avoid
use-after-free when a clip item's own `mouseReleaseEvent` triggers a
project mutation that destroys the item mid-event.

**Edit command added:** `MoveClipAcrossTracksCmd` for atomic
cross-track clip movement with undo.

---

## Phase 3 — Multi-Track Live Playback + Transitions + Loop + Hotkeys

VJ live-performance mode: all N tracks decode in parallel, hotkeys
switch which track feeds the output, with pluggable transitions.

**Added under `core/sequencer/`:**
- `TrackPlaybackChain.{h,cpp}` — per-track decoder host. `decodeToTick`
  with `produceImage` flag: full path for active/incoming-transition
  tracks, decode-only for background tracks.
- `Transition.{h,cpp}` — pluggable interface + 3 implementations:
  - `HardCutTransition` — instant swap (zero-duration default)
  - `CrossfadeTransition` — alpha blend with configurable easing curve
  - `MBRandomTransition` — sporadic macroblock switching; `Fisher-Yates`
    permutation over MB indices, activates N MBs proportional to
    progress. MB size, easing curve, and seed all user-controllable.
  - Easing curves: linear / ease_in / ease_out / smooth (smoothstep).
- `FrameRouter.{h,cpp}` — N-way orchestrator. Owns all chains,
  subscribes to the clock, decides each tick which tracks need a full
  QImage (active + incoming during transition), composites via active
  transition. Hard-cut zero-duration fast-path. Also hosts:
  - **Switch mode** (`requestTrackSwitch`) — press triggers transition.
  - **Touch mode** (`requestTrackHoldPress` / `requestTrackHoldRelease`)
    — held key routes instantly; stack tracks pressed order;
    releasing drops to the most-recent-still-held or falls back to the
    "top-layer" track (lowest index with content at playhead).

**Removed:** `SequencerCompositor` (replaced by `TrackPlaybackChain` +
`FrameRouter`).

**Added under `gui/sequencer/`:**
- `SequencerTransitionPanel.{h,cpp}` — type dropdown, duration /
  MB-size / curve spinboxes, Re-roll seed button, **Hotkey Mode**
  dropdown (Switch vs Touch).
- Loop UI in the dock: Mark In / Mark Out buttons, Loop checkbox,
  yellow bracket overlay rendered by the ruler.

**Hotkey handling rebuilt:**
- I / O / L / Space use `QShortcut` with `Qt::WidgetWithChildrenShortcut`
  scope.
- 1-9 use an app-level event filter (necessary because `QShortcut`
  can't deliver key releases, required for Touch mode). Filter
  self-scopes to "focus is inside our dock" via `isAncestorOf`.
  Auto-repeat events consumed so holds don't re-fire press handler.

---

## Phase 4 — Spout Output

Publish the router's frames to OBS via Spout2 shared textures.

**Integrated:** `third_party/Spout2/` (user-cloned SDK). Embedded as
`spout2_embed` static library — only the SpoutDX + SpoutGL subset we
need; skipped Spout2's top-level CMake to avoid OpenGL targets /
examples. Links `d3d11`, `dxgi`, `shlwapi`, `psapi`.

**Added under `core/sequencer/`:**
- `SpoutSender.{h,cpp}` — wraps `spoutDX`. `start(name)` opens DirectX11
  + allocates a sender named "LaMoshPit" with
  `DXGI_FORMAT_B8G8R8A8_UNORM` (matches our QImage byte order exactly —
  no conversion). `onFrameReady` slot → `SendImage(pixels, w, h, pitch)`.
  Early-outs when inactive (zero idle cost).

**Dock integration:**
- **Spout Out** checkbox next to the transition panel with live status
  label (green "sending as LaMoshPit" / grey "(off)" / red "init failed").
- Router `frameReady` fans out to both preview and SpoutSender.

OBS side: user installs the free Spout2 plugin, adds a Spout source,
picks `LaMoshPit`.

---

## Phase 5 — NLE Render-to-File

Export the chosen track as a clean H.264 MP4, with optional
import-back into the project.

**Added under `core/sequencer/`:**
- `SequencerRenderer.{h,cpp}` — `QObject` worker running on a `QThread`.
  Walks the chosen track's clips via `SequencerClipDecoder`, swscales
  to YUV420P (libx264) or NV12 (HW encoders), encodes, muxes MP4.
  Three encoder modes:
  - `Libx264Default` — CRF 18 + scenecut=0
  - `Libx264FromGlobal` — honours a subset of `GlobalEncodeParams`
    (gopSize / bFrames / refFrames / qpOverride / qpMin-qmax /
    killIFrames); caller populates from the mosh-editor's live panel.
  - `Hardware` — picks via `hwenc::findBestH264Encoder`, falls back
    to libx264 if unavailable.
- Honours render range (entire sequence or loop region).

**Added under `gui/sequencer/`:**
- `SequencerRenderDialog.{h,cpp}` — modal with source track picker,
  range radio (Full / Loop), encoder radio (Default / Global Params /
  Hardware), destination Browse, Import-into-project checkbox.
  Returns a filled `SequencerRenderer::Params` + import flag.

**MainWindow wiring:**
- `SequencerDock::renderRequested` → `MainWindow::onNleRenderRequested`.
- Main window patches `GlobalEncodeParams` from the live mosh-editor
  panel when the user chose that option.
- Spawns worker on a `QThread`; connects progress to the main progress
  bar; on completion, if import-back was ticked, copies to
  `{project}/imported_videos/`, runs `ThumbnailGenerator`, refreshes
  the Media Bin.

---

## Post-Phase — HW Import Setting + `h264_mf` Fallback

App-wide settings dialog, shared HW encoder picker, HW-accelerated
imports.

**Added under `core/util/`:**
- `HwEncoder.{h,cpp}` — shared `hwenc::findBestH264Encoder` used by
  both `SequencerRenderer` and `DecodePipeline`. Preference order:
  `h264_nvenc → h264_amf → h264_qsv → h264_mf`. (h264_mf added this
  phase as a generic Windows Media Foundation fallback before libx264.)

**`DecodePipeline` extended:**
- New `useHwEncode` bool param (default false → unchanged behavior).
- HW path: NV12 pixel format + YUV420P→NV12 swscale between the
  existing rotation stage and `avcodec_send_frame`. Rotation code
  unchanged — still operates on YUV420P.
- If `avcodec_open2` fails on the chosen HW encoder (no drivers, no
  compatible device), the codec context is rebuilt with libx264 +
  YUV420P so imports never hard-fail.

**Added under `gui/`:**
- `SettingsDialog.{h,cpp}` — modal with a single group "New Import
  Encode Settings" containing the **Use hardware acceleration for new
  file imports** checkbox + an explainer paragraph clarifying that
  mosh-editor and Global Encode Params renders stay on CPU libx264
  (because bitstream surgery requires the forked libx264).
- Persistent via `QSettings("LaMoshPit", "LaMoshPit")` / key
  `encode/useHwOnImport` (default false).
- Static `SettingsDialog::importUsesHwEncode()` getter so callers
  don't have to instantiate the dialog.

**MainWindow wiring:**
- **File → Settings…** (Ctrl+,) opens the dialog.
- Import flow reads `SettingsDialog::importUsesHwEncode()` per-import
  and passes to `DecodePipeline::standardizeVideo`, so the user can
  toggle between imports.

---

## Encoding Contexts — Clean Separation

Three separate encoding paths, each with its own policy:

| Context                        | Encoder                        | Who decides                |
|--------------------------------|--------------------------------|----------------------------|
| New file imports               | HW or libx264                  | File → Settings checkbox   |
| NLE timeline renders           | Default / Global Params / HW   | Per-render dialog choice   |
| Mosh editor (Force I/P/B, etc.)| Forked libx264 only (CPU)      | Always — not a setting     |

The mosh-editor path deliberately cannot be switched to HW because
its bitstream-surgery hooks (per-MB `force_mb_type`, `force_intra_mode`,
`mvd_injection`, `dct_scale`, etc.) only exist in our forked libx264.
HW encoders don't expose those knobs and never will.

---

## Architectural Summary

```
┌─ Input: MediaBin drag or direct file drop ─┐
│
├─→ DecodePipeline (standardize to H.264)
│     └─ CPU libx264 OR HW encoder (Settings)
│
├─→ Mosh editor (Force I/P/B, MB edits, Quick Mosh, Global Params)
│     └─ Forked libx264 (CPU, always) — bitstream surgery
│
└─→ NLE Sequencer
      ├─ Timeline (QGraphicsView, up to 9 tracks, drag/trim/cut, zoom)
      ├─ FrameRouter (N parallel TrackPlaybackChains)
      │    ├─ Switch mode — 1-9 triggers active Transition
      │    └─ Touch mode  — 1-9 press-and-hold, release = top layer
      ├─ Transition system
      │    ├─ Hard Cut
      │    ├─ Crossfade (with easing)
      │    └─ MB Random (macroblock-level sporadic switch, seedable)
      ├─ Output fan-out
      │    ├─ Preview widget (letterbox QImage)
      │    └─ Spout sender → OBS (live performance)
      └─ Render-to-file (SequencerRenderer worker)
           └─ libx264 / libx264+GlobalParams / HW
                → optional import-back → mosh editor → …
```

Master timebase everywhere: `AVRational{1, 90000}`. Horizontal zoom
held in a function-local static (single writer: the view's Ctrl+wheel
handler). Clip geometry in scene pixels, clip data in master ticks,
conversion via `tickToSceneX` / `sceneXToTick`.

---

## Files Added

**`core/sequencer/`** (11 new pairs)
`Tick.h`, `SequencerClip.h`, `SequencerTrack.h`, `SequencerProject.{h,cpp}`,
`EditCommand.{h,cpp}`, `HwDecode.{h,cpp}`, `SequencerClipDecoder.{h,cpp}`,
`SequencerPlaybackClock.{h,cpp}`, `TrackPlaybackChain.{h,cpp}`,
`Transition.{h,cpp}`, `FrameRouter.{h,cpp}`, `SpoutSender.{h,cpp}`,
`SequencerRenderer.{h,cpp}`

**`core/util/`**
`HwEncoder.{h,cpp}`

**`gui/sequencer/`** (8 new pairs)
`SequencerPreviewPlayer.{h,cpp}`, `SequencerDock.{h,cpp}`,
`SequencerTimelineConstants.h`, `SequencerClipItem.{h,cpp}`,
`SequencerRulerItem.{h,cpp}`, `SequencerPlayheadItem.{h,cpp}`,
`SequencerTimelineView.{h,cpp}`, `SequencerTrackHeader.{h,cpp}`,
`SequencerTransitionPanel.{h,cpp}`, `SequencerRenderDialog.{h,cpp}`

**`gui/`**
`SettingsDialog.{h,cpp}`

**`third_party/`**
Spout2 SDK clone (user-supplied)

## Files Modified

- `CMakeLists.txt` — Spout2 embed target, all new sources registered
- `gui/MainWindow.{h,cpp}` — Sequencer dock wiring, render handler,
  Settings menu entry, HW-import plumbing, removed old NLE references
- `core/pipeline/DecodePipeline.{h,cpp}` — optional HW encode path
- `gui/widgets/MediaBinWidget.cpp` — drag source emitting text/uri-list
  (already in place from earlier work, verified here)

## Files Removed

- `gui/widgets/NleDock.{h,cpp}`
- `gui/widgets/NlePreviewPlayer.{h,cpp}`
- `gui/widgets/NleTimelineWidget.{h,cpp}`
- `gui/widgets/SequenceRenderDialog.{h,cpp}` (old)
- `core/sequence/` (entire directory — Sequence, NleCompositor,
  SequenceRenderer)
- `core/sequencer/SequencerCompositor.{h,cpp}` (replaced after Phase 3)

---

## Build Status

Debug + Release both compile clean with no new warnings. All Phase 1–5
features verified by the user through manual testing during build;
HW import path verified by runtime encoder detection in
`avcodec-62.dll` (all four HW H.264 encoders shipped in our vcpkg
FFmpeg build).
