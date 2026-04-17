#pragma once

#include <QDialog>
#include <QSet>
#include <QVector>
#include <functional>

class QSlider;
class QLabel;
class QDial;
class QDialogButtonBox;
class QPushButton;

// =============================================================================
// GrowShrinkDialog
//
// Bipolar amount slider that erodes (left) or dilates (right) the MB
// selection on the current frame — and optionally on a range of neighbouring
// frames — while preserving the "islands floor at 1 MB each" rule.
//
// Range controls:
//   • Direction (3-way knob): Forward / Backward / Outward
//   • Length (slider): dynamically clamped to clip boundaries
//   (Length 0 = current frame only.)
//
// Amount slider:
//   • Range is [-shrinkCap, +growCap] where shrinkCap / growCap are the
//     MAX morphology steps across all currently-targeted frames.  Frames
//     that reach their per-frame floor earlier stay at that floor while
//     the slider keeps moving for the larger frames.
//
// Live preview:
//   • Whenever the amount changes, `previewCb(steps)` is invoked so the
//     caller can visually update the current frame's canvas.  On reject,
//     `revertCb()` is invoked to restore the canvas.
//   • On accept, the caller reads `amount()` + `targetFrames()` and
//     applies morphology (via SelectionMorphology::apply) per-frame.
// =============================================================================
class GrowShrinkDialog : public QDialog {
    Q_OBJECT
public:
    enum Direction { Forward = 0, Backward = 1, Outward = 2 };

    // getBaseSelection(frameIdx) must return the pre-operation MB selection
    // for that frame (the starting point for morphology).  The dialog uses
    // it to compute per-frame limits and live-preview the current frame.
    using SelectionGetter = std::function<QSet<int>(int)>;

    GrowShrinkDialog(int mbCols, int mbRows,
                     int currentFrame, int totalFrames,
                     SelectionGetter getBaseSelection,
                     std::function<void(int steps)> previewCb,
                     std::function<void()>           revertCb,
                     QWidget* parent = nullptr);

    // After accept()
    int          amount()       const;   // slider value (may be negative)
    Direction    direction()    const;
    int          length()       const;   // 0 = current frame only
    QVector<int> targetFrames() const;   // always includes currentFrame

    // Live preview (used internally; public for tests)
    int currentFrameShrinkCap() const { return m_curShrinkCap; }
    int currentFrameGrowCap()   const { return m_curGrowCap;   }

private slots:
    void onDirectionChanged(int v);
    void onLengthChanged(int v);
    void onAmountChanged(int v);

protected:
    // Fire revert on Cancel / X close so the canvas always snaps back.
    void reject() override;

private:
    void recomputeRangeAndCaps();  // recompute slider range based on frame targets
    void updateLengthRange();
    void updateAmountLabel();
    void firePreview();

    int m_mbCols = 0, m_mbRows = 0;
    int m_currentFrame = 0, m_totalFrames = 0;
    SelectionGetter               m_getSel;
    std::function<void(int)>      m_previewCb;
    std::function<void()>         m_revertCb;

    // Cached caps for the current frame (always valid since frame is always
    // in-range).  Amount slider uses global caps across all target frames.
    int m_curShrinkCap = 0;
    int m_curGrowCap   = 0;
    int m_globalShrinkCap = 0;
    int m_globalGrowCap   = 0;

    // ── Widgets ──────────────────────────────────────────────────────────
    QDial*            m_dialDir   { nullptr };
    QLabel*           m_lblDir    { nullptr };
    QSlider*          m_sliderLen { nullptr };
    QLabel*           m_lblLen    { nullptr };
    QSlider*          m_sliderAmt { nullptr };
    QLabel*           m_lblAmt    { nullptr };
    QLabel*           m_lblLimits { nullptr };
    QLabel*           m_lblPreview{ nullptr };
    QDialogButtonBox* m_btnBox    { nullptr };
    QPushButton*      m_btnApply  { nullptr };
};
