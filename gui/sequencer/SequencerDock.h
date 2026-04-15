#pragma once

// =============================================================================
// SequencerDock — top-level panel hosting the NLE sequencer.
//
// Contains preview + transport + timeline + transition panel + track
// headers.  Owns the playback engine instances: SequencerPlaybackClock,
// FrameRouter (which internally owns N TrackPlaybackChains).  Hotkey
// handling (number keys for track switch, I/O/L for loop) is installed at
// the dock level via event filter so it works whenever the dock is visible.
// =============================================================================

#include "core/sequencer/Tick.h"
#include "core/sequencer/SequencerRenderer.h"   // needed for renderRequested signal signature
#include <QDockWidget>
#include <memory>

class QPushButton;
class QSlider;
class QLabel;
class QCheckBox;
class QComboBox;

namespace sequencer {

class SequencerProject;
class SequencerPlaybackClock;
class FrameRouter;
class SpoutSender;
class SequencerPreviewPlayer;
class SequencerTimelineView;
class SequencerTrackHeader;
class SequencerTransitionPanel;

class SequencerDock : public QDockWidget {
    Q_OBJECT
public:
    SequencerDock(SequencerProject* project, QWidget* parent = nullptr);
    ~SequencerDock() override;

    // App-level event filter catches 1-9 press + release so the Touch-mode
    // behaviour can respond to key release.  QShortcut can't see releases,
    // so we use the filter for these keys only.  I/O/L/Space remain
    // QShortcut-based.
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    // Emitted when the user confirms the Render dialog.  MainWindow owns
    // the actual render lifecycle (worker thread, progress dialog,
    // import-back + thumbnail + Media Bin refresh).
    void renderRequested(const SequencerRenderer::Params& params,
                         bool importIntoProject);

private slots:
    void onPlayPauseClicked();
    void onStopClicked();
    void onSeekSliderMoved(int value);
    void onClockTickAdvanced(::sequencer::Tick tick);
    void onProjectChanged();
    void onMarkIn();
    void onMarkOut();
    void onLoopToggled(bool on);
    void onSpoutToggled(bool on);
    void onRenderClicked();

private:
    void refreshDurationUi();
    void applyLoopRegionToEngine();

    QString formatTime(Tick t) const;

    SequencerProject*                       m_project { nullptr };
    std::unique_ptr<SequencerPlaybackClock> m_clock;
    std::unique_ptr<FrameRouter>            m_router;
    std::unique_ptr<SpoutSender>            m_spout;

    SequencerPreviewPlayer*    m_preview        { nullptr };
    SequencerTrackHeader*      m_trackHeader    { nullptr };
    SequencerTimelineView*     m_timelineView   { nullptr };
    SequencerTransitionPanel*  m_transitionPanel{ nullptr };

    QPushButton* m_btnPlay     { nullptr };
    QPushButton* m_btnStop     { nullptr };
    QPushButton* m_btnMarkIn   { nullptr };
    QPushButton* m_btnMarkOut  { nullptr };
    QPushButton* m_btnRender   { nullptr };
    QCheckBox*   m_chkLoop     { nullptr };
    QCheckBox*   m_chkSpout    { nullptr };
    QLabel*      m_spoutStatus { nullptr };
    QComboBox*   m_hotkeyMode  { nullptr };
    QSlider*     m_seek        { nullptr };
    QLabel*      m_timeLabel   { nullptr };

    // Loop region state — kept here because the clock's loop values also
    // reflect enabled/disabled and we want the in/out points to survive
    // disable→re-enable toggles.
    Tick m_loopIn  { 0 };
    Tick m_loopOut { 0 };
};

} // namespace sequencer
