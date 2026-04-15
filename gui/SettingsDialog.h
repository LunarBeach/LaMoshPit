#pragma once

// =============================================================================
// SettingsDialog — application-wide settings editor.
//
// Opened from File → Settings (Ctrl+,).  Values are persisted via QSettings
// under organization="LaMoshPit" / application="LaMoshPit".
//
// Current scope (v1): one group, one checkbox — "Use hardware acceleration
// for new file imports".  Dialog is designed so additional groups can be
// appended without restructuring the layout.
//
// Keys read/written:
//   encode/useHwOnImport   bool   (default: false)
//
// Deliberate non-goals for v1:
//   - No encoder knobs (preset / bitrate) — we use sensible defaults
//   - No affecting the mosh-editor path — bitstream surgery requires our
//     forked libx264 which can't be swapped for an HW encoder
//   - No affecting in-session NLE renders — those pick per-render via the
//     SequencerRenderDialog's encoder radio
// =============================================================================

#include <QDialog>

class QCheckBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // Static helper — read the current "use HW on import" flag from
    // persistent settings.  Called by MainWindow before firing a new
    // import so we don't have to cache a copy of the setting ourselves.
    static bool importUsesHwEncode();

private slots:
    void onAccepted();

private:
    QCheckBox* m_chkHwImport { nullptr };
};
