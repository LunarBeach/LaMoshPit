#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "core/util/VideoMetaProbe.h"

class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QDialogButtonBox;

// =============================================================================
// ImportSelectionMapDialog
//
// User flow:
//   1. Browse to a black-and-white "map" video on disk.
//   2. Probe its metadata (width/height/fps/frameCount).
//   3. The target-clip dropdown is populated with every imported clip in
//      the project, with incompatible clips greyed out (can't be selected).
//      Compatibility requires matching width, height, frame count, and fps
//      (within ±0.01 tolerance).
//   4. User enters a display name (defaults to the map file's base name).
//   5. On OK: the map file is copied into {project}/selection_maps/ (with
//      filename de-duplication if needed), and a {name, file} entry is
//      appended to the target clip's sidecar .maps.json.
//
// When invoked from the Apply Map dialog, a specific clip can be pre-
// selected and locked.
// =============================================================================
class ImportSelectionMapDialog : public QDialog {
    Q_OBJECT
public:
    // projectMoshVideoFolder  : absolute path to {project}/MoshVideoFolder/
    // projectMapsDir          : absolute path to {project}/selection_maps/
    // preselectedClipPath     : if non-empty, this clip is chosen in the
    //                           dropdown and the control is locked (used
    //                           from ApplyMapDialog's "Import Map..." button).
    ImportSelectionMapDialog(const QString& projectMoshVideoFolder,
                             const QString& projectMapsDir,
                             const QString& preselectedClipPath,
                             QWidget* parent = nullptr);

    // After exec() == Accepted, returns the absolute path of the clip the
    // new map was associated with (so the caller can refresh UI).
    QString associatedClipPath() const { return m_associatedClip; }

private slots:
    void onBrowseMap();
    void onClipIndexChanged(int idx);
    void onAccept();

private:
    void refreshClipDropdown();          // re-evaluates compatibility tags
    void updateAcceptEnabled();

    // ── Project paths ─────────────────────────────────────────────────────
    QString m_moshVideoFolder;
    QString m_mapsDir;
    QString m_preselectedClip;

    // ── Resolved state ────────────────────────────────────────────────────
    QString   m_mapPath;       // current source map file
    VideoMeta m_mapMeta;       // metadata for m_mapPath
    QString   m_associatedClip;// filled on accept

    QVector<QString> m_clipPaths;  // parallel to dropdown entries

    // ── Widgets ───────────────────────────────────────────────────────────
    QLineEdit*        m_edMapPath   { nullptr };
    QPushButton*      m_btnBrowse   { nullptr };
    QLabel*           m_lblMapMeta  { nullptr };
    QLineEdit*        m_edName      { nullptr };
    QComboBox*        m_cmbClip     { nullptr };
    QLabel*           m_lblClipMeta { nullptr };
    QDialogButtonBox* m_btnBox      { nullptr };
};
