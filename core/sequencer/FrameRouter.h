#pragma once

// =============================================================================
// FrameRouter — orchestrates N parallel track chains and routes their output
// to a single QImage stream (preview, future Spout, future virtual cam).
//
// Responsibilities:
//   1. Own one TrackPlaybackChain per project track (rebuilt on track
//      add/remove).
//   2. Subscribe to SequencerPlaybackClock::tickAdvanced.
//   3. On each tick:
//        a. Decide which tracks need a full QImage (active + incoming-if-
//           -transitioning); others get decode-only to stay sync'd.
//        b. Call decodeToTick on each chain.
//        c. Pass the active+incoming frames to the active Transition if
//           a switch is in progress; otherwise forward the active frame.
//        d. Emit frameReady(QImage, Tick) for consumers.
//   4. Expose a requestTrackSwitch(int) method that the UI / hotkey
//      handler calls.  The router kicks off the currently-selected
//      transition.
//
// Global-transition-per-session policy: exactly one Transition instance
// is "installed" at a time.  setTransition() swaps it.  The params dict
// can be mutated live (mid-transition changes don't disturb anything; the
// new params take effect from the next compose() call).
// =============================================================================

#include "core/sequencer/Tick.h"
#include "core/sequencer/Transition.h"
#include <QObject>
#include <QImage>
#include <QList>
#include <memory>

namespace sequencer {

class SequencerProject;
class SequencerPlaybackClock;
class TrackPlaybackChain;

class FrameRouter : public QObject {
    Q_OBJECT
public:
    // How the 1-9 number-row hotkeys behave in live mode.
    //   Switch : press triggers a track change via the active Transition.
    //            The new track stays live until another key is pressed.
    //            (This is the classic VJ "scene select" behaviour.)
    //   Touch  : while a number key is held, that track is displayed with
    //            no transition (instant cut).  When all track keys are
    //            released, output falls back to the "top layer" — the
    //            lowest-index track that has a clip at the current
    //            playhead.  Multiple keys are handled as a stack: the
    //            most-recently-pressed key wins; releasing it drops back
    //            to whichever key is still held, or the top layer.
    enum class HotkeyMode { Switch, Touch };

    FrameRouter(SequencerProject* project,
                SequencerPlaybackClock* clock,
                QObject* parent = nullptr);
    ~FrameRouter() override;

    // Switch transition type (creates a new Transition instance via the
    // factory).  Existing in-progress transition is finished immediately
    // as if it completed — keeps state sane if the user swaps mid-effect.
    void setTransitionTypeId(const QString& typeId);
    QString transitionTypeId() const { return m_currentTypeId; }

    // Mutate the params dict.  Applied on next compose().
    void setTransitionParams(const TransitionParams& params);
    const TransitionParams& transitionParams() const { return m_params; }

    // Hotkey mode.  Clears any in-flight transition and touch-stack state.
    void       setHotkeyMode(HotkeyMode m);
    HotkeyMode hotkeyMode() const { return m_hotkeyMode; }

    // Request a track switch (Switch-mode semantics).  If no transition is
    // active, starts one; if one is already running, the request is
    // ignored.  Returns true if the request was accepted.
    bool requestTrackSwitch(int toTrackIndex);

    // Touch-mode press/release.  Instant routing (no transition).  Ignored
    // when we're in Switch mode.
    void requestTrackHoldPress(int trackIndex);
    void requestTrackHoldRelease(int trackIndex);

    // Force a pull + emit at the current clock tick (used when paused and
    // the user drops a clip so the preview updates immediately).
    void refreshCurrentFrame();

    // True if a transition is in progress.
    bool isTransitioning() const { return m_inTransition; }

signals:
    void frameReady(QImage frame, ::sequencer::Tick tick);

private slots:
    void onProjectChanged();
    void onTickAdvanced(::sequencer::Tick tick);
    void onActiveTrackChanged(int idx);

private:
    void rebuildChains();
    void processTick(Tick tick);

    // Lowest-index track that has a clip at the given tick.  Falls back
    // to 0 if no track has coverage.  Used in Touch mode as the idle
    // output when the hold stack is empty.
    int topLayerTrackAtTick(Tick tick) const;

    SequencerProject*       m_project { nullptr };
    SequencerPlaybackClock* m_clock   { nullptr };

    QList<TrackPlaybackChain*> m_chains;   // owned

    // Routing state.
    int  m_activeTrack  { 0 };
    bool m_inTransition { false };
    int  m_outgoingTrack{ 0 };
    int  m_incomingTrack{ 0 };
    Tick m_transitionStartTick { 0 };
    Tick m_transitionDuration  { 0 };

    // Transition system.
    std::unique_ptr<Transition> m_transition;
    QString                     m_currentTypeId { "hard_cut" };
    TransitionParams            m_params;

    // Hotkey-mode state.
    HotkeyMode      m_hotkeyMode  { HotkeyMode::Switch };
    QList<int>      m_touchStack;         // indices of currently-held track keys,
                                          // in press order; last = most recent
};

} // namespace sequencer
