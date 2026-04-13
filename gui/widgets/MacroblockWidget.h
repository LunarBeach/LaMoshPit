#pragma once

#include <QWidget>
#include <QImage>
#include <QFutureWatcher>
#include <QList>

#include "core/model/MBEditData.h"
#include "gui/BitstreamAnalyzer.h"

class MBCanvas;
class QPushButton;
class QLabel;
class QDial;
class QSlider;
class QSpinBox;
class QScrollArea;
class QSplitter;

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

signals:
    // Emitted when the user navigates via Prev/Next buttons.
    // MainWindow connects this to update the timeline selection.
    void frameNavigated(int frameIdx);

    // Emitted whenever the painted MB selection changes (for spatial mask sync).
    void mbSelectionChanged(const QSet<int>& sel);

private slots:
    void onFrameDecoded();
    void onPrev();
    void onNext();
    void onClearFrame();
    void onMBSelectionChanged(const QSet<int>& sel);

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
    QSplitter*   m_innerSplitter  { nullptr };
    float        m_zoom           { 1.0f };
    bool         m_canvasIsPopped { false };
    QPushButton* m_btnPrev;
    QPushButton* m_btnNext;
    QLabel*      m_navLabel;
    QPushButton* m_btnPopOutCanvas { nullptr };

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
};
