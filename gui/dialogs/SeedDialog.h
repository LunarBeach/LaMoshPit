#pragma once

#include <QDialog>
#include <QVector>

class QSlider;
class QLabel;
class QDial;

// =============================================================================
// SeedDialog
//
// Replicates the current frame's painted MB selection onto neighbouring frames.
// User controls:
//   • Direction (3-way knob): Forward / Backward / Outward
//   • Length   (slider): number of target frames in chosen direction
//     - Forward:  1 .. (totalFrames - 1 - currentFrame)
//     - Backward: 1 .. currentFrame
//     - Outward:  1 .. min(currentFrame, totalFrames-1-currentFrame)
//       (length applies PER SIDE; total frames touched = 2*length)
//
// On accept, targetFrames() returns the display-order indices of the frames
// that should receive the current selection (unioned into existing selection).
// =============================================================================
class SeedDialog : public QDialog {
    Q_OBJECT
public:
    enum Direction { Forward = 0, Backward = 1, Outward = 2 };

    // Mode controls how the seeded selection interacts with each target
    // frame's existing selection:
    //   • Merge:    target.selectedMBs |= current selection (union).
    //   • Override: target.selectedMBs  = current selection (replace).
    enum Mode { Merge = 0, Override = 1 };

    SeedDialog(int currentFrame, int totalFrames, QWidget* parent = nullptr);

    Direction direction() const;
    Mode      mode() const;
    int       length() const;
    QVector<int> targetFrames() const;

private slots:
    void onDirectionChanged(int v);
    void onLengthChanged(int v);
    void onModeChanged(int v);

private:
    void updateLengthRange();
    void updatePreview();

    int m_currentFrame = 0;
    int m_totalFrames  = 0;

    QDial*   m_dialDir    { nullptr };
    QLabel*  m_lblDir     { nullptr };
    QDial*   m_dialMode   { nullptr };
    QLabel*  m_lblMode    { nullptr };
    QSlider* m_sliderLen  { nullptr };
    QLabel*  m_lblLen     { nullptr };
    QLabel*  m_lblPreview { nullptr };
};
