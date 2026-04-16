# LaMoshPit UI Overhaul — 2026-04-15/16

## Overview

Major UI modernization and architectural refactor. Every panel is now a
KDDockWidgets-based dock that supports nested floating splits, tab-grouping,
drag-to-arrange, maximize-on-float, and persistent layout save/restore.
Typography upgraded to three custom font families. Several core encode bugs
fixed alongside the UI work.

---

## 1. Typography & Color System

### Custom fonts loaded via `gui/AppFonts.h/.cpp`
- **Ethnocentric-Regular** — section headings, tab labels, group box titles
- **CreatoDisplay-Regular** (+ Medium, Bold weights) — body text, labels, menus, inputs, combo boxes, spin boxes
- **Nodo** — dock window headings (now handled by dock title bars), button labels, large display text (splash "You might think you're ready...")

### Font loading
- Fonts bundled in `resources.qrc` and loaded at startup via `QFontDatabase::addApplicationFont`
- Global stylesheet in `main.cpp` references font families dynamically via `QString::arg()` substitution
- Global `QPushButton` rule uses Nodo; `QComboBox/QSpinBox/QLabel` rules use CreatoDisplay

### Color changes
- Pink accent (`#ff00ff`, `#ff88ff`) replaced with green (`#00ff88`) throughout:
  - QuickMosh heading, combo, Mosh Now button, banner border
  - PreviewPlayer video border, status label, slider handle/sub-page
  - MacroblockWidget preset combo
- B-frame semantic pink (`#ff64b4`) intentionally preserved in timeline badges, Force B button, and MB grid "B" indicator (data-viz color coding)

### Per-widget style upgrades
- **GlobalParamsWidget** — style constants (`kSection`, `kLabel`, `kSpin`, `kDSpin`, `kCombo`, `kBtn`, `kApplyBtn`) converted from static `const QString` to lazy builder functions that read `AppFonts` family names at runtime. All sizes bumped from 7pt to 10pt.
- **MacroblockWidget** — `kDarkBtn`, `kSpinStyle` converted to lazy builders. New helpers `kSmallBtn()`, `kSectionHeading()`, `kCtrlLabel()`. Section toggle buttons use Ethnocentric. Knob labels, nav label, zoom label, brush label all switched to CreatoDisplay 10pt. Save/Del/Import buttons enlarged (7pt -> 9pt, min-height 28px).
- **QuickMoshWidget** — preset label, Save/Del/Import buttons restyled and enlarged.
- **MediaBinWidget** — widget-level stylesheet updated for CreatoDisplay body + Nodo buttons.
- **MainWindow** — `makeTimelineBtn` uses Nodo 10pt, buttons enlarged (30->32 height, 88->96 min-width).

---

## 2. KDDockWidgets Integration

### Architecture
- `MainWindow` base class changed from `QMainWindow` to `KDDockWidgets::QtWidgets::MainWindow`
- KDDockWidgets v2.4.0 built from source in `third_party/KDDockWidgets/` against system Qt 6.11.0 (not vcpkg — avoids Qt version mismatch with vcpkg's Qt 6.10.2)
- Every panel wrapped in `KDDockWidgets::QtWidgets::DockWidget`:
  - Preview Player
  - MB Grid Canvas
  - MB Editor
  - Media Bin
  - Quick Mosh
  - Global Encode Params
  - Timeline (closable, listed in View menu)
  - Progress (new standalone panel)
  - Properties (hidden by default)
  - Bitstream Debug (hidden by default)
- SequencerDock remains a `QDockWidget` subclass — lives as a standalone floating window (shown/hidden via View menu). Cannot participate in KDDW nested-float layouts but all hotkey/render/timeline behavior preserved.

### Features gained
- **Nested floating splits**: drag a dock onto another floating dock to split/tab within the floating window
- **Tab grouping**: drag one dock onto another to create a tab bar
- **Maximize on float**: title bar maximize/restore button on every floating dock + double-click-to-maximize (`Flag_TitleBarHasMaximizeButton`, `Flag_DoubleClickMaximizes`)
- **Persistent layout save/restore**: Layout menu with Save Layout As, Load Layout submenu, Reset to Default. Uses `KDDockWidgets::LayoutSaver::serializeLayout/restoreLayout` via QSettings. Window geometry + dock state auto-saved on close and restored on next launch.
- **View menu auto-sync**: each dock's `toggleAction()` listed in View menu, auto-tracks open/close state

### CMake integration
- `add_subdirectory(third_party/KDDockWidgets EXCLUDE_FROM_ALL)` with options to disable examples/tests/docs/python/qtquick
- `KDDockWidgets_FRONTENDS` forced to `"qtwidgets"` only (prevents Qt6Quick dependency)
- Post-build step copies `kddockwidgets-qt6.dll` next to the EXE
- `windeployqt` post-build step deploys system Qt 6.11.0 DLLs

---

## 3. MB Editor / MB Grid Canvas Split

### MacroblockWidget refactored into two dockable panels
- `MacroblockWidget` is now a hidden coordinator (owns all state, signals, slots)
- `canvasPanel()` returns the canvas view: nav bar (prev/next/pop-out), zoom slider, grid scroll area, brush bar (brush size/mode, clear, deselect, selection tools)
- `controlsPanel()` returns the editor view: preset combo + Save/Del/Import, transient envelope, tabbed knob container
- Both panels exposed to MainWindow via getters; each wrapped in its own KDDW dock
- Pop-out button in canvas nav bar emits `canvasFloatToggleRequested()` signal — MainWindow toggles the canvas dock's floating state. `isFloatingChanged` signal back-updates the button icon.
- `dialogParentFor()` helper simplified to `m_canvasPanel->window()` — returns the correct top-level window whether the canvas dock is docked or floating

### Behavioral invariants preserved
- All signal connections (timeline selection -> MB editor, knob changes -> m_edits, Quick Mosh -> editMap, Global Params -> applyRequested) route through MacroblockWidget's single internal state — completely independent of visual hierarchy
- Every `connectKnobs` binding, every canvas event filter (viewport resize, wheel zoom), every dialog parent chain works identically after the split

---

## 4. Tab-ification

### GlobalParamsWidget
- Scrollable vertical section stack replaced with `QTabWidget`
- 9 tabs: Frame Structure, Rate Control, Motion Estimation, MB Type & DCT, B-Frame Prediction, Quantization Flags, Deblocking Filter, Psychovisual Optimisation, Rate-Control Fidelity
- Ungrouped controls (preset combo, Scene-Cut Detection toggle, Debug Logging toggle) remain above tabs
- RENDER button remains below tabs

### MacroblockWidget (MB Editor controls panel)
- Collapsible section buttons replaced with `QTabWidget`
- 8 tabs: Quantization, Motion & Temporal, Luma Corruption, Chroma & Colour, Spatial & Pixel, Bitstream Motion, Bitstream Prediction, Bitstream Residual
- Each tab page is a horizontally-scrollable knob row
- Preset combo + Save/Del/Import and Transient Envelope remain above tabs

### Tab styling
- Ethnocentric font for tab labels, dark background, green accent on active tab, 4px border-radius

---

## 5. Progress Panel

- New `gui/widgets/ProgressPanel.h/.cpp` — standalone widget holding progress bar + status label
- Wrapped in its own KDDW dock ("Progress"), listed in View menu
- QuickMoshWidget banner area completely removed (was 80px dead space)
- All `m_progressBar->setRange/setValue` and `setProgressVisible` calls in MainWindow migrated from QuickMosh to ProgressPanel

---

## 6. Removed Duplicate In-Dock Headings

Removed the large "GLOBAL ENCODE PARAMS", "MEDIA BIN", "QUICK MOSH ZONE", "MB GRID CANVAS", and "MB EDITOR" labels from inside each widget — the KDDW dock title bars already display these names.

---

## 7. Bug Fixes

### MB edits not applied during render (critical)
- **Root cause**: `m_mbWidget->isVisible()` was the gate for passing the edit map to the worker. After the MacroblockWidget split, the coordinator widget is permanently hidden, so `isVisible()` always returned false — empty `MBEditMap{}` was sent to every render.
- **Fix**: edit map is always sent for MBEditOnly renders. The "nothing to apply" early-return check now tests `m_mbCanvasDock->isOpen() || m_mbEditorDock->isOpen()` instead.

### Force P/B baked in pending MB edits (workflow bug)
- **Root cause**: `startTransform` always read `m_mbWidget->editMap()` regardless of operation type. Clicking Force P applied all in-progress MB edits alongside the type change.
- **Fix**: only pass the edit map when `type == MBEditOnly` (explicit RENDER). All structural operations (Force I/P/B, Delete, Dup, Interp, Flip, Flop) now pass an empty edit map. Default `FrameMBParams` values are verified no-ops for every field.

### Frame 250 stuck as I-frame on Force P (pre-existing)
- **Root cause**: x264's keyint schedule overrides pict_type hints at GOP boundaries. Default `gopSize=250` forced an IDR at frame 250 even when every frame was hinted as P.
- **Fix**: Force P and Force B operations now set `keyint=9999` (infinite GOP) and `bframes=0, b-adapt=0`, same treatment as `killIFrames`. x264 cannot override the user's explicit type choice at any boundary.

### Import encode produced no B-frames (pre-existing)
- **Root cause**: `b-adapt=2` (trellis lookahead) in DecodePipeline overrode the source's decoded `frame->pict_type` hints, typically deciding all-P was optimal.
- **Fix**: Changed to `b-adapt=0` so x264 respects the pict_type from the decoder, mirroring the source video's original I/P/B structure.

### Custom Selection Dialog no live preview (pre-existing)
- **Root cause**: dialog only computed the selection on OK — no visual feedback while adjusting sliders.
- **Fix**: Added `selectionPreview(QSet<int>)` signal emitted on every slider/dial/checkbox change. MacroblockWidget snapshots the original selection, pipes preview updates to the canvas, restores on Cancel.

---

## 8. PreviewPlayer Resize Fix

- `m_videoWidget->setMinimumSize` reduced from 640x360 to 160x90 so the containing dock can be resized freely. QVideoWidget preserves aspect ratio internally.

---

## Files Changed

### New files
- `gui/AppFonts.h`, `gui/AppFonts.cpp` — font loading + role-based accessors
- `gui/widgets/ProgressPanel.h`, `gui/widgets/ProgressPanel.cpp` — standalone progress dock
- `third_party/KDDockWidgets/` — KDDockWidgets v2.4.0 source (shallow clone)
- `assets/fonts/` — Ethnocentric, CreatoDisplay, Nodo font files + licenses

### Modified files
- `CMakeLists.txt` — KDDockWidgets in-tree build, windeployqt post-build, KDDW DLL copy
- `main.cpp` — KDDW init, font loading, Config flags, stylesheet template
- `resources.qrc` — font resources added
- `gui/MainWindow.h` — KDDW base class, dock pointers, layout menu, ProgressPanel
- `gui/MainWindow.cpp` — complete buildLayout/buildMenuBar rewrite, applyDefaultLayout, layout save/load/reset, closeEvent, progress migration, MB edit map gating
- `gui/widgets/MacroblockWidget.h/.cpp` — canvas/controls panel split, tab-ify knobs, font/color updates
- `gui/widgets/GlobalParamsWidget.h/.cpp` — tab-ify sections, font/color updates
- `gui/widgets/QuickMoshWidget.h/.cpp` — banner removed, progress API removed, font/color updates
- `gui/widgets/MediaBinWidget.cpp` — heading removed, font/color updates
- `gui/widgets/PreviewPlayer.cpp` — min size relaxed, pink->green
- `gui/dialogs/CustomSelectionDialog.h/.cpp` — live preview signal
- `core/transform/FrameTransformer.cpp` — Force P/B infinite GOP fix, MB edit map gating
- `core/pipeline/DecodePipeline.cpp` — b-adapt=0 for source frame type mirroring
