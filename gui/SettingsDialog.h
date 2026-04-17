#pragma once

// =============================================================================
// SettingsDialog — application-wide settings editor.
//
// Opened from File → Settings (Ctrl+,).  Values are persisted via QSettings
// under organization="LaMoshPit" / application="LaMoshPit".
//
// Keys read/written:
//   encode/useHwOnImport             bool   (default: false)
//   ui/selectionColor                string (colour name; default: "Yellow")
//   paths/moshVideoFolderOverride    string (absolute path; default: empty
//                                            = use {project}/MoshVideoFolder/)
//   undo/maxStepsMBEditor            int    (default: 50)
//   undo/maxStepsSequencer           int    (default: 50)
//
// MB editor and NLE sequencer have intentionally-separate undo stacks —
// they're two distinct workspaces and mixing them in one timeline causes
// surprising Ctrl+Z behaviour (e.g. a sequencer track trim undoing in
// the middle of an MB-editor session).  Two settings so each workspace's
// history depth can be tuned independently.
//
// Deliberate non-goals for v1:
//   - No encoder knobs (preset / bitrate) — we use sensible defaults
//   - No affecting the mosh-editor path — bitstream surgery requires our
//     forked libx264 which can't be swapped for an HW encoder
//   - No affecting in-session NLE renders — those pick per-render via the
//     SequencerRenderDialog's encoder radio
// =============================================================================

#include <QDialog>
#include <QColor>
#include <QString>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // Static helper — read the current "use HW on import" flag from
    // persistent settings.  Called by MainWindow before firing a new
    // import so we don't have to cache a copy of the setting ourselves.
    static bool importUsesHwEncode();

    // Static helper — look up the user's chosen MB-selection overlay /
    // brush-outline base colour.  Returns a fully-opaque QColor; callers
    // add alpha at draw time (overlay ~90, outline ~160).  Defaults to
    // yellow (255,200,0) for users who never touched Settings.
    static QColor selectionOverlayColor();

    // Static helper — absolute path of a user-chosen override for the per-
    // project MoshVideoFolder.  Empty string means "no override; use the
    // default {project}/MoshVideoFolder/ location".  Callers should prefer
    // Project::moshVideoFolder(), which consults this helper internally —
    // this is exposed only for settings-introspection and token expansion.
    static QString moshVideoFolderOverride();

    // Static helpers — maximum number of steps retained on each
    // workspace's undo stack.  Default 50 for both.  The MB editor cap
    // is consumed by UndoController; the sequencer cap is consumed by
    // SequencerProject::executeCommand.
    static int maxUndoStepsMBEditor();
    static int maxUndoStepsSequencer();

private slots:
    void onAccepted();
    void onBrowseMoshFolder();

private:
    QCheckBox* m_chkHwImport        { nullptr };
    QComboBox* m_cmbSelColor        { nullptr };
    QLineEdit* m_edMoshFolder       { nullptr };
    QSpinBox*  m_spinMaxUndoMB      { nullptr };
    QSpinBox*  m_spinMaxUndoSeq     { nullptr };
};
