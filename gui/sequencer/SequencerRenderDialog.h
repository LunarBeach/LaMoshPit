#pragma once

// =============================================================================
// SequencerRenderDialog — modal dialog for configuring an NLE render.
//
// Collects:
//   - Source track (defaults to the currently active track)
//   - Range (entire sequence OR loop region if the user has I/O markers set)
//   - Encoder (default libx264 / Global Encode Params / hardware-accelerated)
//   - Destination file path (Browse… picker)
//   - "Import into project after render" checkbox
//
// The dialog doesn't run the render itself — it hands a filled-in
// SequencerRenderer::Params back to the caller, plus the import-back flag.
// MainWindow wires the actual render onto a QThread.
// =============================================================================

#include "core/sequencer/SequencerRenderer.h"
#include <QDialog>
#include <QString>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QRadioButton;
class QPushButton;

namespace sequencer {

class SequencerProject;

class SequencerRenderDialog : public QDialog {
    Q_OBJECT
public:
    struct Result {
        SequencerRenderer::Params renderParams;
        bool                      importToProject { false };
    };

    SequencerRenderDialog(const SequencerProject* project,
                          int activeTrackIndex,
                          bool loopRegionAvailable,
                          Tick loopInTicks,
                          Tick loopOutTicks,
                          const QString& defaultOutputDir,
                          QWidget* parent = nullptr);

    // Valid only when exec() returned QDialog::Accepted.
    Result result() const { return m_result; }

private slots:
    void onBrowseClicked();
    void onAcceptClicked();

private:
    void buildTrackList();

    const SequencerProject* m_project { nullptr };

    // Captured at construction — used to build the range option.
    bool m_loopRegionAvailable { false };
    Tick m_loopInTicks         { 0 };
    Tick m_loopOutTicks        { 0 };

    QComboBox*    m_trackCombo    { nullptr };
    QRadioButton* m_radioFullRange{ nullptr };
    QRadioButton* m_radioLoopRange{ nullptr };
    QRadioButton* m_radioEncDefault{ nullptr };
    QRadioButton* m_radioEncGlobal { nullptr };
    QRadioButton* m_radioEncHw     { nullptr };
    QLineEdit*    m_destPath       { nullptr };
    QPushButton*  m_btnBrowse      { nullptr };
    QCheckBox*    m_chkImportBack  { nullptr };
    QPushButton*  m_btnRender      { nullptr };
    QPushButton*  m_btnCancel      { nullptr };

    Result m_result;
};

} // namespace sequencer
