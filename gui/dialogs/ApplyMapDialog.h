#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "core/model/SelectionMap.h"

class QSlider;
class QLabel;
class QDial;
class QComboBox;
class QPushButton;
class QDialogButtonBox;

// =============================================================================
// ApplyMapDialog
//
// Applies one of the clip's imported selection maps to a range of frames.
// Direction (Forward / Backward / Outward) and length work the same way
// as SeedDialog — length is dynamically clamped to clip boundaries.
//
// Length of 0 means "current frame only".
//
// If the clip has no selection maps yet (or all existing ones are missing
// on disk), the dropdown is empty.  An "Import Map..." button next to it
// opens ImportSelectionMapDialog with this clip pre-selected, making the
// mid-workflow import path smooth.
//
// On accept, callers retrieve:
//   • selectedMapPath()   — absolute path of the chosen map video
//   • targetFrames()      — ordered list of frame indices to apply to
// The actual map-decode + MB-selection work is done by the caller.
// =============================================================================
class ApplyMapDialog : public QDialog {
    Q_OBJECT
public:
    enum Direction { Forward = 0, Backward = 1, Outward = 2 };

    ApplyMapDialog(const QString& clipVideoPath,
                   const QString& projectMoshVideoFolder,
                   const QString& projectMapsDir,
                   int currentFrame,
                   int totalFrames,
                   QWidget* parent = nullptr);

    QString   selectedMapPath() const;
    Direction direction() const;
    int       length() const;
    QVector<int> targetFrames() const;   // includes currentFrame

private slots:
    void onDirectionChanged(int v);
    void onLengthChanged(int v);
    void onImportMapClicked();

private:
    void reloadMaps();                    // re-read sidecar + refresh combo
    void updateLengthRange();
    void updatePreview();
    void updateAcceptEnabled();

    // ── State ─────────────────────────────────────────────────────────────
    QString m_clipPath;
    QString m_moshVideoFolder;
    QString m_mapsDir;
    int     m_currentFrame = 0;
    int     m_totalFrames  = 0;

    QList<SelectionMapEntry> m_maps;

    // ── Widgets ───────────────────────────────────────────────────────────
    QComboBox*        m_cmbMap     { nullptr };
    QPushButton*      m_btnImport  { nullptr };
    QDial*            m_dialDir    { nullptr };
    QLabel*           m_lblDir     { nullptr };
    QSlider*          m_sliderLen  { nullptr };
    QLabel*           m_lblLen     { nullptr };
    QLabel*           m_lblPreview { nullptr };
    QDialogButtonBox* m_btnBox     { nullptr };
};
