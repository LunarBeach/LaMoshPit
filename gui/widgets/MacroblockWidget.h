#pragma once

#include <QWidget>
#include <QImage>
#include <QFutureWatcher>
#include <QList>

#include "core/model/MBEditData.h"
#include "core/presets/PresetManager.h"
#include "gui/BitstreamAnalyzer.h"

class MBCanvas;
class QPushButton;
class QLabel;
class QDial;
class QSlider;
class QSpinBox;
class QComboBox;
class QScrollArea;
class QSplitter;
class QVBoxLayout;

// =============================================================================
// MacroblockWidget
//
// Displays one video frame with a 16×16 MB grid overlay.  The user paint-
// selects MBs and adjusts parameter knobs organised into five groups:
//
//   QUANTIZATION     — QP delta (encoder quantiser offset via ROI side data)
//   TEMPORAL REF     — Reference depth, ghost blend, MV drift X/Y
//   LUMA CORRUPTION  — Noise injection, DC pixel offset, luma inversion blend
//   CHROMA CORRUPT.  — Independent UV drift X/Y, UV DC offset
//   SPATIAL          — Spill radius (blast radius beyond painted selection)
//
// The widget is kept in sync with the MainWindow timeline:
//   • Pressing Prev/Next emits frameNavigated(int) so MainWindow can update
//     the timeline selection to match.
//   • MainWindow calls navigateToFrame(int) when the timeline selection changes
//     (this does NOT re-emit frameNavigated, preventing signal loops).
// =============================================================================
class MacroblockWidget : public QWidget {
    Q_OBJECT
public:
    explicit MacroblockWidget(QWidget* parent = nullptr);
    ~MacroblockWidget();

    // ── View accessors ──────────────────────────────────────────────────────
    // The MacroblockWidget instance itself is a hidden coordinator. Its two
    // visible sub-panels are exposed via these getters so MainWindow can host
    // each in its own QDockWidget. Signal/slot wiring and all internal state
    // remain owned by MacroblockWidget, so behaviour is identical to when
    // canvas + controls were nested together in a single splitter.
    QWidget* canvasPanel()   const { return m_canvasPanel; }
    QWidget* controlsPanel() const { return m_controlsPanel; }

    // Tell the widget which project it's working in.  Used to resolve the
    // current clip's selection-map sidecar and to open the Import Map dialog
    // with the right project paths.  Call this before/after every project
    // switch; call with empty strings when no project is active.
    void setProjectPaths(const QString& moshVideoFolder,
                         const QString& selectionMapsDir);

    // Re-read the user's chosen MB-selection overlay colour from app
    // settings and apply it to the canvas.  Called on construction and
    // whenever the Settings dialog has been dismissed.
    void refreshSelectionColor();

    // Load a new video — clears all accumulated edits.
    void setVideo(const QString& videoPath, const AnalysisReport& report);

    // Called after a transform completes (file replaced in-place).
    // Reloads metadata and re-decodes the current frame; preserves edits.
    void reload(const QString& videoPath, const AnalysisReport& report);

    // Snapshot of all per-frame MB edits passed to FrameTransformerWorker.
    const MBEditMap& editMap() const { return m_edits; }

    // Replace the entire edit map (used by Quick Mosh to inject preset edits).
    // Reloads knob displays and canvas selection for the current frame.
    void loadEditMap(const MBEditMap& edits);

    // Clear every edit on every frame.
    void clearAllEdits();

    // Navigate to a specific frame WITHOUT emitting frameNavigated.
    // Called by MainWindow when the timeline selection changes (prevents loops).
    void navigateToFrame(int frameIdx);

    // The MB selection on the currently-displayed frame (for spatial mask capture).
    QSet<int> currentSelection() const;

    // Called by MainWindow whenever the timeline selection changes so that
    // knob edits are replicated across every frame in the active range.
    void setActiveFrameRange(const QVector<int>& frames);

    // ── User preset API ───────────────────────────────────────────────────────
    // Read the current knob values (no selectedMBs).
    FrameMBParams currentControlParams() const;

    // Apply knob values to all frames in the active range; selectedMBs preserved.
    void applyControlParams(const FrameMBParams& p);

    // Reload the user-preset combo from disk (called after save/delete/import).
    void refreshUserPresets();

signals:
    // Emitted when the user navigates via Prev/Next buttons.
    // MainWindow connects this to update the timeline selection.
    void frameNavigated(int frameIdx);

    // Emitted whenever the painted MB selection changes (for spatial mask sync).
    void mbSelectionChanged(const QSet<int>& sel);

    // Scope C — fired whenever a user-initiated mutation of m_edits occurs
    // (knob turn, cascade slider, canvas selection, button-action slot).
    // Does NOT fire from loadEditMap / setVideo / reload (those are
    // programmatic paths used by undo commands and video loading).
    // MainWindow listens, debounces via QTimer, diffs, and creates an
    // MBEditMapReplaceCommand for the unified undo stack.
    void editCommitted();

    // Pop-out button inside the canvas nav bar was clicked. MainWindow
    // toggles the canvas dock's floating state in response. Kept as a
    // signal so this widget has no knowledge of QDockWidget.
    void canvasFloatToggleRequested();

public slots:
    // Called by MainWindow when the canvas dock's floating state changes,
    // so the pop-out button's icon reflects docked-vs-popped.
    void setCanvasFloatingIcon(bool floating);

private slots:
    void onFrameDecoded();
    void onPrev();
    void onNext();
    void onClearFrame();
    void onMBSelectionChanged(const QSet<int>& sel);

    // Selection painting slots
    void onDeselect();           // current frame: clear painted MBs (keep params)
    void onDeselectAll();        // all frames:    clear painted MBs (keep params)
    void onSeedSelection();      // open Seed dialog
    void onCustomSelection();    // open Custom Selection dialog
    void onApplyMap();           // open Apply Map dialog (selection-map videos)
    void onCopySelection();      // copy current frame's selection to clipboard
    void onPasteSelection();     // prompt Merge/Override, apply to current frame
    void onSaveSelection();      // open Save Selection dialog (create preset)
    void onLoadSelection();      // open Load Selection dialog (apply preset)
    void onGrowShrink();         // open Grow/Shrink dialog (morphology slider)

    // User preset slots
    void onUserPresetSave();
    void onUserPresetDelete();
    void onUserPresetImport();

private:
    void navigateTo(int frameIdx);
    void loadKnobsFromCurrentFrame();
    void updateNavLabel();
    void setControlsEnabled(bool enabled);
    bool eventFilter(QObject* obj, QEvent* e) override;

    // ── Video state ───────────────────────────────────────────────────────
    QString       m_videoPath;
    int           m_totalFrames = 0;
    int           m_mbCols      = 0;
    int           m_mbRows      = 0;
    QVector<char> m_frameTypes;
    int           m_currentFrame = 0;
    MBEditMap     m_edits;
    QVector<int>  m_activeRange; // frames in current timeline selection

    QFutureWatcher<QImage>* m_watcher = nullptr;

    // ── Canvas + navigation ───────────────────────────────────────────────
    MBCanvas*    m_canvas;
    QScrollArea* m_canvasScroll   { nullptr };
    float        m_zoom           { 1.0f };
    QPushButton* m_btnPrev;
    QPushButton* m_btnNext;
    QLabel*      m_navLabel;
    QPushButton* m_btnPopOutCanvas { nullptr };

    // ── View panels ────────────────────────────────────────────────────────
    // m_canvasPanel hosts the grid display + navigation + zoom + brush/tools.
    // m_controlsPanel hosts presets + knobs + transient envelope.
    // Both are children of `this`; MainWindow reparents each into a QDock.
    QWidget*     m_canvasPanel    { nullptr };
    QWidget*     m_controlsPanel  { nullptr };
    QVBoxLayout* m_canvasLayout   { nullptr };
    QVBoxLayout* m_controlsLayout { nullptr };

    // ── Knobs — QUANTIZATION ─────────────────────────────────────────────
    QDial* m_dialQP;    QSpinBox* m_sbQP;

    // ── Knobs — TEMPORAL REFERENCE ───────────────────────────────────────
    QDial* m_dialRef;   QSpinBox* m_sbRef;
    QDial* m_dialGhost; QSpinBox* m_sbGhost;
    QDial* m_dialMVX;   QSpinBox* m_sbMVX;
    QDial* m_dialMVY;   QSpinBox* m_sbMVY;

    // ── Knobs — LUMA CORRUPTION ───────────────────────────────────────────
    QDial* m_dialNoise;  QSpinBox* m_sbNoise;
    QDial* m_dialPxOff;  QSpinBox* m_sbPxOff;
    QDial* m_dialInvert; QSpinBox* m_sbInvert;

    // ── Knobs — CHROMA CORRUPTION ─────────────────────────────────────────
    QDial* m_dialChrX;   QSpinBox* m_sbChrX;
    QDial* m_dialChrY;   QSpinBox* m_sbChrY;
    QDial* m_dialChrOff; QSpinBox* m_sbChrOff;

    // ── Knobs — SPATIAL INFLUENCE ─────────────────────────────────────────
    QDial* m_dialSpill;        QSpinBox* m_sbSpill;
    QDial* m_dialSampleRadius; QSpinBox* m_sbSampleRadius;

    // ── Knobs — AMPLIFY ───────────────────────────────────────────────────
    QDial* m_dialMVAmp; QSpinBox* m_sbMVAmp;

    // ── Transient envelope (prominent sliders above the knob scroll area) ─
    QSlider* m_sliderTransLen;   QLabel* m_lblTransLen;
    QSlider* m_sliderTransDecay; QLabel* m_lblTransDecay;

    // ── Knobs — PIXEL MANIPULATION ────────────────────────────────────────
    QDial* m_dialBlockFlatten; QSpinBox* m_sbBlockFlatten;
    QDial* m_dialRefScatter;   QSpinBox* m_sbRefScatter;
    QDial* m_dialColorTwistU;  QSpinBox* m_sbColorTwistU;
    QDial* m_dialColorTwistV;  QSpinBox* m_sbColorTwistV;

    // ── Knobs — NEW PIXEL-DOMAIN ─────────────────────────────────────────
    QDial* m_dialPosterize;    QSpinBox* m_sbPosterize;
    QDial* m_dialPixelShuffle; QSpinBox* m_sbPixelShuffle;
    QDial* m_dialSharpen;      QSpinBox* m_sbSharpen;
    QDial* m_dialTempDiffAmp;  QSpinBox* m_sbTempDiffAmp;
    QDial* m_dialHueRotate;    QSpinBox* m_sbHueRotate;

    // ── Knobs — BITSTREAM SURGERY ────────────────────────────────────────
    QDial* m_dialBsMvdX;       QSpinBox* m_sbBsMvdX;
    QDial* m_dialBsMvdY;       QSpinBox* m_sbBsMvdY;
    QDial* m_dialBsForceSkip;  QSpinBox* m_sbBsForceSkip;
    QDial* m_dialBsIntraMode;  QSpinBox* m_sbBsIntraMode;
    // m_dialBsMbType / m_sbBsMbType removed — control migrated to Global
    // Encode Params → Partition Mode for stability and correctness.
    QDial* m_dialBsDctScale;   QSpinBox* m_sbBsDctScale;
    QDial* m_dialBsCbpZero;       QSpinBox* m_sbBsCbpZero;        // parent (0..100)
    QDial* m_dialBsCbpZeroLuma;   QSpinBox* m_sbBsCbpZeroLuma;    // -1..100
    QDial* m_dialBsCbpZeroChroma; QSpinBox* m_sbBsCbpZeroChroma;  // -1..100
    QDial* m_dialBsSuppressResOnMvd; QSpinBox* m_sbBsSuppressResOnMvd; // 0/1

    // All knob widgets — for bulk enable/disable and signal blocking
    QList<QDial*>    m_allDials;
    QList<QSpinBox*> m_allSpinboxes;

    // ── Other controls ────────────────────────────────────────────────────
    QSlider*     m_sliderBrush;
    QLabel*      m_lblBrush;
    QSlider*     m_sliderZoom;
    QLabel*      m_lblZoom;
    QPushButton* m_btnClearFrame;
    QPushButton* m_btnClearAll;
    QPushButton* m_btnBrushAdd       { nullptr };
    QPushButton* m_btnBrushSub       { nullptr };
    QPushButton* m_btnDeselect       { nullptr };
    QPushButton* m_btnDeselectAll    { nullptr };
    QPushButton* m_btnSeed           { nullptr };
    QPushButton* m_btnCustomSelect   { nullptr };
    QPushButton* m_btnApplyMap       { nullptr };
    QPushButton* m_btnCopySel        { nullptr };
    QPushButton* m_btnPasteSel       { nullptr };
    QPushButton* m_btnSaveSel        { nullptr };
    QPushButton* m_btnLoadSel        { nullptr };
    QPushButton* m_btnGrowShrink     { nullptr };

    // Clipboard for Copy Selection / Paste Selection.  Empty until the user
    // copies once; persists across frame navigations (intentional — the
    // clipboard's job is to survive navigation).  Cleared on clip change.
    QSet<int>    m_selectionClipboard;

    // ── Project paths (for selection-map import / apply) ─────────────────
    QString      m_projectMoshVideoFolder;
    QString      m_projectMapsDir;

    // ── Nav / brush wrapper widgets (live inside m_canvasPanel) ──────────
    QWidget*     m_navBar       { nullptr };
    QWidget*     m_brushBar     { nullptr };

    // ── Preset combo (built-in + user presets unified) ─────────────────
    QComboBox*   m_presetCombo     { nullptr };
    QPushButton* m_btnUserPresetSave{ nullptr };
    QPushButton* m_btnUserPresetDel { nullptr };
    QPushButton* m_btnUserPresetImport{ nullptr };
};
