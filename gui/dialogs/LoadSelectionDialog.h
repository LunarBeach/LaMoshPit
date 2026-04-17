#pragma once

#include <QDialog>
#include <QString>

#include "core/model/SelectionPreset.h"

class QComboBox;
class QSlider;
class QLabel;
class QCheckBox;
class QDial;
class QPushButton;
class QDialogButtonBox;

// =============================================================================
// LoadSelectionDialog
//
// Pick an installed selection preset (or import one from an external .json
// file a friend sent you) and apply it starting at the user's current frame.
//
// Controls:
//   • Preset dropdown — populated from {AppData}/selection_presets/.
//   • "Load New Selection Preset" button — file-pick a .json from disk; copied
//     into the user's preset folder, then the dropdown refreshes.
//   • "This frame only" checkbox — if checked, only the preset's offset-0
//     selection is applied to the current clip frame.  Disables the length
//     slider.
//   • Length slider — dynamically clamped to min(preset.frameCount,
//     clip.totalFrames - currentFrame).  Determines how many preset frames
//     are applied starting at the current frame.
//   • Merge / Override knob — same semantics as elsewhere:
//       Merge     = target.selectedMBs |= preset.mbs(offset)
//       Override  = target.selectedMBs  = preset.mbs(offset)   (destructive)
//   • Apply Selection button — applies and closes.
//
// The dialog does NOT write to the clip's edit map itself.  It exposes the
// user's choices so MacroblockWidget can do the application and route
// updates through onMBSelectionChanged for logging / signalling.
// =============================================================================
class LoadSelectionDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { Merge = 0, Override = 1 };

    LoadSelectionDialog(int mbCols, int mbRows,
                        int currentFrame, int totalFrames,
                        QWidget* parent = nullptr);

    // Available after accept().
    const SelectionPreset& chosenPreset() const { return m_loaded; }
    bool   thisFrameOnly() const;
    int    clipLength()    const;   // # preset frames to apply
    Mode   mode()          const;

private slots:
    void onImportFile();
    void onPresetChanged(int idx);
    void onThisFrameOnlyToggled(bool on);
    void onLengthChanged(int v);
    void onModeChanged(int v);
    void onApplyClicked();

private:
    void refreshPresetList();
    void updateLengthRange();
    void updateAcceptEnabled();

    int m_mbCols       = 0;
    int m_mbRows       = 0;
    int m_currentFrame = 0;
    int m_totalFrames  = 0;

    QStringList     m_presetPaths;  // parallel to combo entries
    SelectionPreset m_loaded;       // current preset (fully loaded)

    QComboBox*        m_cmbPreset  { nullptr };
    QPushButton*      m_btnImport  { nullptr };
    QLabel*           m_lblMeta    { nullptr };
    QCheckBox*        m_chkThis    { nullptr };
    QSlider*          m_sliderLen  { nullptr };
    QLabel*           m_lblLen     { nullptr };
    QDial*            m_dialMode   { nullptr };
    QLabel*           m_lblMode    { nullptr };
    QPushButton*      m_btnApply   { nullptr };
    QDialogButtonBox* m_btnBox     { nullptr };
};
