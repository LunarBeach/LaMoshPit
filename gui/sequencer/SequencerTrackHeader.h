#pragma once

// =============================================================================
// SequencerTrackHeader — left-side column showing one row per track.
//
// Each row shows:
//   - Track number badge (1-9 for hotkey reference)
//   - Track name label
//   - Small "remove" button
// Plus a footer with an "Add Track" button (disabled at MaxTracks).
//
// Vertical alignment with the SequencerTimelineView's scene is baked into
// the layout: row height equals kTrackHeight + kTrackGap, header band on
// top matches kRulerHeight.
// =============================================================================

#include <QWidget>

class QPushButton;
class QLabel;
class QVBoxLayout;

namespace sequencer {

class SequencerProject;

class SequencerTrackHeader : public QWidget {
    Q_OBJECT
public:
    explicit SequencerTrackHeader(SequencerProject* project,
                                  QWidget* parent = nullptr);
    QSize sizeHint() const override;

signals:
    // Emitted when the user clicks a track row's select area to make it
    // the active (live) track.
    void trackSelected(int trackIndex);

private slots:
    void onProjectChanged();
    void onActiveTrackChanged(int idx);
    void onAddTrackClicked();

private:
    void rebuildRows();

    SequencerProject* m_project { nullptr };
    QVBoxLayout*      m_layout  { nullptr };
    QWidget*          m_rowsHost{ nullptr };
    QVBoxLayout*      m_rowsLayout { nullptr };
    QPushButton*      m_btnAddTrack{ nullptr };
    int               m_activeTrack{ 0 };
};

} // namespace sequencer
