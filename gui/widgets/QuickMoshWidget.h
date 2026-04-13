#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QProgressBar;

// =============================================================================
// QuickMoshWidget
//
// A compact "one-click datamosh" panel.  The user picks a saved preset from
// the combo and clicks "Mosh Now!" — the panel emits userMoshRequested(name)
// so MainWindow can load the preset and fire the encode pipeline.
//
// The top area displays a background image with an embedded progress bar
// that MainWindow uses to show transform progress.
// =============================================================================
class QuickMoshWidget : public QWidget {
    Q_OBJECT
public:
    explicit QuickMoshWidget(QWidget* parent = nullptr);

    // Enable/disable the Mosh Now button (MainWindow disables during encode).
    void setMoshEnabled(bool enabled);

    // Reload the preset combo from disk.
    void refreshUserPresets();

    // Progress bar access — MainWindow drives this during transforms.
    QProgressBar* progressBar() const { return m_progressBar; }

    // Show/hide the progress area (bar + "Operation in progress..." label).
    void setProgressVisible(bool visible);

signals:
    void saveUserPresetRequested();
    void userMoshRequested(QString presetName);

private slots:
    void onUserPresetDelete();
    void onUserPresetImport();

private:
    QComboBox*    m_combo;        // user presets
    QLabel*       m_desc;
    QPushButton*  m_btnMosh;
    QProgressBar* m_progressBar { nullptr };
    QLabel*       m_opLabel     { nullptr };

    // Preset management buttons
    QPushButton* m_btnUserSave       { nullptr };
    QPushButton* m_btnUserDel        { nullptr };
    QPushButton* m_btnUserImport     { nullptr };
};
