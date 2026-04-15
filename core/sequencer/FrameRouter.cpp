#include "core/sequencer/FrameRouter.h"
#include "core/sequencer/SequencerProject.h"
#include "core/sequencer/SequencerTrack.h"
#include "core/sequencer/SequencerPlaybackClock.h"
#include "core/sequencer/TrackPlaybackChain.h"

#include <QDebug>
#include <algorithm>

namespace sequencer {

FrameRouter::FrameRouter(SequencerProject* project,
                         SequencerPlaybackClock* clock,
                         QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_clock(clock)
{
    // Start with default parameter block.  1 second duration, 16 px MB,
    // linear curve, randomised-each-run seed.
    m_params["duration_sec"] = 1.0;
    m_params["mb_size"]      = 16;
    m_params["curve"]        = "linear";
    m_params["seed"]         = 0u;

    // Install default transition (hard cut).
    m_transition = Transition::create(m_currentTypeId);

    if (m_project) {
        connect(m_project, &SequencerProject::projectChanged,
                this,      &FrameRouter::onProjectChanged);
        connect(m_project, &SequencerProject::activeTrackChanged,
                this,      &FrameRouter::onActiveTrackChanged);
        m_activeTrack = m_project->activeTrackIndex();
    }
    if (m_clock) {
        connect(m_clock, &SequencerPlaybackClock::tickAdvanced,
                this,    &FrameRouter::onTickAdvanced);
    }

    rebuildChains();
}

FrameRouter::~FrameRouter()
{
    qDeleteAll(m_chains);
}

// =============================================================================
// Chain management
// =============================================================================

void FrameRouter::rebuildChains()
{
    qDeleteAll(m_chains);
    m_chains.clear();
    if (!m_project) return;
    for (int i = 0; i < m_project->trackCount(); ++i) {
        m_chains.append(new TrackPlaybackChain(m_project, i, this));
    }
    // Clamp routing indices into the new track range.
    const int n = m_chains.size();
    if (n == 0) { m_activeTrack = 0; m_inTransition = false; return; }
    m_activeTrack   = std::clamp(m_activeTrack, 0, n - 1);
    m_outgoingTrack = std::clamp(m_outgoingTrack, 0, n - 1);
    m_incomingTrack = std::clamp(m_incomingTrack, 0, n - 1);
}

void FrameRouter::onProjectChanged()
{
    // Track structure may have changed → rebuild chains.  Clips-only edits
    // only require invalidating existing chains (cheap); track add/remove
    // requires a full rebuild.  For v1 we rebuild in all cases — 9 chains
    // is trivial to reconstruct.
    const int currentChains = m_chains.size();
    const int wantedChains  = m_project ? m_project->trackCount() : 0;
    if (currentChains != wantedChains) {
        rebuildChains();
    } else {
        for (auto* c : m_chains) if (c) c->invalidate();
    }
    // If paused, re-pull immediately so the user sees the edit.
    if (m_clock && !m_clock->isPlaying()) {
        refreshCurrentFrame();
    }
}

void FrameRouter::onActiveTrackChanged(int idx)
{
    // When the user clicks a track header (not a hotkey-driven transition),
    // treat it as an instant cut — the panel click implies "I want this
    // now" more than "start a transition."  Hotkey-driven switches go
    // through requestTrackSwitch() which fires the transition.
    if (idx < 0 || idx >= m_chains.size()) return;
    m_activeTrack   = idx;
    m_inTransition  = false;
}

// =============================================================================
// Transition API
// =============================================================================

void FrameRouter::setTransitionTypeId(const QString& typeId)
{
    auto t = Transition::create(typeId);
    if (!t) return;
    m_transition    = std::move(t);
    m_currentTypeId = typeId;
    m_inTransition  = false;   // cancel any in-flight effect
}

void FrameRouter::setTransitionParams(const TransitionParams& params)
{
    // Merge rather than replace so callers can update just one key.
    for (auto it = params.begin(); it != params.end(); ++it) {
        m_params[it.key()] = it.value();
    }
}

bool FrameRouter::requestTrackSwitch(int toTrackIndex)
{
    if (m_hotkeyMode != HotkeyMode::Switch) return false;
    if (m_inTransition) return false;
    if (!m_project || !m_transition) return false;
    if (toTrackIndex < 0 || toTrackIndex >= m_project->trackCount()) return false;
    if (toTrackIndex == m_activeTrack) return false;

    const double durSec = m_params.value("duration_sec", 1.0).toDouble();
    m_transitionDuration  = secondsToTicks(std::max(0.0, durSec));
    m_transitionStartTick = m_clock ? m_clock->currentTick() : 0;
    m_outgoingTrack       = m_activeTrack;
    m_incomingTrack       = toTrackIndex;
    m_inTransition        = true;

    m_transition->start(m_outgoingTrack, m_incomingTrack, m_params);
    return true;
}

// =============================================================================
// Touch-mode API
// =============================================================================

void FrameRouter::setHotkeyMode(HotkeyMode m)
{
    if (m == m_hotkeyMode) return;
    m_hotkeyMode   = m;
    m_touchStack.clear();
    m_inTransition = false;   // drop any in-flight effect
    if (m_clock) processTick(m_clock->currentTick());   // re-emit
}

void FrameRouter::requestTrackHoldPress(int trackIndex)
{
    if (m_hotkeyMode != HotkeyMode::Touch) return;
    if (!m_project || trackIndex < 0 || trackIndex >= m_project->trackCount()) return;

    // Remove any stale entry (if the same key somehow came through twice
    // without a release — e.g. focus lost during hold), then push on top.
    m_touchStack.removeAll(trackIndex);
    m_touchStack.append(trackIndex);

    // Instant route refresh so the user sees the change immediately.
    if (m_clock) processTick(m_clock->currentTick());
}

void FrameRouter::requestTrackHoldRelease(int trackIndex)
{
    if (m_hotkeyMode != HotkeyMode::Touch) return;
    m_touchStack.removeAll(trackIndex);
    if (m_clock) processTick(m_clock->currentTick());
}

int FrameRouter::topLayerTrackAtTick(Tick tick) const
{
    if (!m_project) return 0;
    const int n = m_project->trackCount();
    for (int t = 0; t < n; ++t) {
        const auto& track = m_project->track(t);
        if (track.clipIndexAtTick(tick) >= 0) return t;
    }
    return 0;   // nothing playing — default to track 0
}

void FrameRouter::refreshCurrentFrame()
{
    if (!m_clock) return;
    processTick(m_clock->currentTick());
}

// =============================================================================
// Per-tick processing
// =============================================================================

void FrameRouter::onTickAdvanced(Tick tick)
{
    processTick(tick);
}

void FrameRouter::processTick(Tick tick)
{
    if (m_chains.isEmpty()) return;

    // Touch mode has its own routing path — no transitions, just pick the
    // held key (most recent) or fall back to the top-layer track.  Project's
    // activeTrackIndex is updated to mirror what's visible so the header
    // column highlight stays meaningful.
    if (m_hotkeyMode == HotkeyMode::Touch) {
        const int effective = m_touchStack.isEmpty()
            ? topLayerTrackAtTick(tick)
            : m_touchStack.last();
        const int n = m_chains.size();
        for (int i = 0; i < n; ++i) {
            QImage tmp;
            m_chains[i]->decodeToTick(tick, /*produceImage=*/i == effective, tmp);
            if (i == effective && !tmp.isNull()) {
                if (m_activeTrack != effective) {
                    m_activeTrack = effective;
                    if (m_project) m_project->setActiveTrackIndex(effective);
                }
                emit frameReady(std::move(tmp), tick);
            }
        }
        return;
    }

    // Hard-cut zero-duration fast-path: snap to incoming immediately.
    if (m_inTransition && m_transitionDuration <= 0) {
        m_activeTrack  = m_incomingTrack;
        m_inTransition = false;
    }

    // Progress in [0, 1] — capped so we never pass the tail.
    double progress = 0.0;
    if (m_inTransition && m_transitionDuration > 0) {
        const Tick elapsed = tick - m_transitionStartTick;
        if (elapsed >= m_transitionDuration) {
            progress       = 1.0;
            // Transition just completed on this tick; after emitting, flip
            // active to incoming so subsequent ticks are steady-state.
        } else if (elapsed <= 0) {
            progress = 0.0;
        } else {
            progress = static_cast<double>(elapsed)
                     / static_cast<double>(m_transitionDuration);
        }
    }

    // Decide which tracks need a full QImage output.  Steady state =
    // active track only.  Transition = outgoing + incoming.
    const int n = m_chains.size();
    QList<QImage> produced; produced.resize(n);
    QList<bool>   wantImg;  wantImg.resize(n);
    for (int i = 0; i < n; ++i) wantImg[i] = false;
    if (m_inTransition) {
        wantImg[m_outgoingTrack] = true;
        wantImg[m_incomingTrack] = true;
    } else {
        wantImg[m_activeTrack] = true;
    }

    for (int i = 0; i < n; ++i) {
        QImage out;
        m_chains[i]->decodeToTick(tick, wantImg[i], out);
        if (wantImg[i]) produced[i] = std::move(out);
    }

    QImage finalImg;
    if (m_inTransition && m_transition) {
        const QImage& a = produced[m_outgoingTrack];
        const QImage& b = produced[m_incomingTrack];
        if (!a.isNull() && !b.isNull() && a.size() == b.size()) {
            finalImg = m_transition->compose(a, b, progress, m_params);
        } else if (!b.isNull()) {
            finalImg = b;   // degraded fallback: skip compose
        } else if (!a.isNull()) {
            finalImg = a;
        }
        // Finalise transition if we just emitted progress=1.0.
        if (progress >= 1.0) {
            m_activeTrack  = m_incomingTrack;
            m_inTransition = false;
            // Make the project's activeTrackIndex follow the router so the
            // header column highlight stays in sync with live output.
            if (m_project) m_project->setActiveTrackIndex(m_activeTrack);
        }
    } else {
        finalImg = produced[m_activeTrack];
    }

    if (!finalImg.isNull()) {
        emit frameReady(std::move(finalImg), tick);
    }
}

} // namespace sequencer
