#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QRadioButton;
class QSlider;
class QLabel;
class QDial;
class QDialogButtonBox;
class QGroupBox;

// =============================================================================
// SaveSelectionDialog
//
// Captures painted MB selections as a reusable preset.  The user picks a
// scope:
//   • This Frame only
//   • All Frames in Clip
//   • Frame Range — with the familiar Left/Right/Outward direction knob
//     and a dynamically-clamped length slider.
//
// The dialog does NOT do the save itself — it just exposes the user's
// choices.  The caller (MacroblockWidget) assembles the preset by pulling
// selectedMBs from m_edits for each frame in the chosen range.
// =============================================================================
class SaveSelectionDialog : public QDialog {
    Q_OBJECT
public:
    enum Scope     { ThisFrame = 0, AllFrames = 1, FrameRange = 2 };
    enum Direction { Left = 0, Right = 1, Outward = 2 };

    SaveSelectionDialog(int currentFrame, int totalFrames,
                        QWidget* parent = nullptr);

    QString   presetName()   const;
    Scope     scope()        const;
    Direction direction()    const;
    int       rangeLength()  const;

    // Convenience: the contiguous list of frame indices covered by the
    // user's current scope choice.  For FrameRange this respects direction.
    QList<int> frameRangeIndices() const;

private slots:
    void onScopeChanged();
    void onDirectionChanged(int v);
    void onLengthChanged(int v);

private:
    void updateLengthRange();
    void updateRangeControlsEnabled();
    void updateAcceptEnabled();

    int m_currentFrame = 0;
    int m_totalFrames  = 0;

    QLineEdit*        m_edName     { nullptr };
    QRadioButton*     m_rbThis     { nullptr };
    QRadioButton*     m_rbAll      { nullptr };
    QRadioButton*     m_rbRange    { nullptr };
    QGroupBox*        m_rangeBox   { nullptr };
    QDial*            m_dialDir    { nullptr };
    QLabel*           m_lblDir     { nullptr };
    QSlider*          m_sliderLen  { nullptr };
    QLabel*           m_lblLen     { nullptr };
    QDialogButtonBox* m_btnBox     { nullptr };
};
