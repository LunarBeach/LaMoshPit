#include "core/sequencer/FrameRouter.h"
#include "core/sequencer/SequencerProject.h"
#include "core/sequencer/SequencerTrack.h"
#include "core/sequencer/SequencerPlaybackClock.h"
#include "core/sequencer/TrackPlaybackChain.h"
#include "core/sequencer/BlendModes.h"
#include "core/sequencer/ClipEffects.h"

#include <QDebug>
#include <QReadLocker>
#include <QThread>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>

// Toggle for the pacing log.  Keep in sync with the same flag in
// SequencerPlaybackClock.cpp — both are part of the playhead-jerks-back
// investigation.
#define LAMOSH_TICK_DEBUG_LOG 1
namespace sequencer {
qint64 tickLogWallMs();   // defined in SequencerPlaybackClock.cpp
} // namespace sequencer

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
    int trackCount = 0;
    {
        QReadLocker lk(&m_project->stateLock());
        trackCount = m_project->trackCount();
    }
    for (int i = 0; i < trackCount; ++i) {
        m_chains.append(new TrackPlaybackChain(i, this));
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
    int wantedChains = 0;
    if (m_project) {
        QReadLocker lk(&m_project->stateLock());
        wantedChains = m_project->trackCount();
    }
    const int currentChains = m_chains.size();
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
    // Dispatched to the router's own thread — Qt::AutoConnection resolves
    // to DirectConnection when the caller is already on that thread, and
    // to QueuedConnection otherwise.  This is the uniform pattern for
    // every FrameRouter mutator: GUI-thread callers don't touch router
    // state directly; their mutation runs later on the router thread.
    QMetaObject::invokeMethod(this, [this, typeId]() {
        auto t = Transition::create(typeId);
        if (!t) return;
        m_transition    = std::move(t);
        m_currentTypeId = typeId;
        m_inTransition  = false;   // cancel any in-flight effect
    }, Qt::AutoConnection);
}

void FrameRouter::setTransitionParams(const TransitionParams& params)
{
    QMetaObject::invokeMethod(this, [this, params]() {
        // Merge rather than replace so callers can update just one key.
        for (auto it = params.begin(); it != params.end(); ++it) {
            m_params[it.key()] = it.value();
        }
    }, Qt::AutoConnection);
}

bool FrameRouter::requestTrackSwitch(int toTrackIndex)
{
    // The actual transition kickoff runs on the router's thread; the
    // return value is a best-effort "is the request plausible" check
    // done from the caller's thread using atomic snapshots of the
    // compose/hotkey mode.  Definitive rejection (already-transitioning,
    // invalid track) happens inside the marshaled lambda.
    if (m_composeMode.load(std::memory_order_acquire)
        == ComposeMode::LayerComposite) return false;
    if (m_hotkeyMode.load(std::memory_order_acquire)
        != HotkeyMode::Switch) return false;

    QMetaObject::invokeMethod(this, [this, toTrackIndex]() {
        if (m_inTransition) return;
        if (!m_project || !m_transition) return;
        {
            QReadLocker lk(&m_project->stateLock());
            if (toTrackIndex < 0 || toTrackIndex >= m_project->trackCount()) return;
        }
        if (toTrackIndex == m_activeTrack) return;

        const double durSec = m_params.value("duration_sec", 1.0).toDouble();
        m_transitionDuration  = secondsToTicks(std::max(0.0, durSec));
        m_transitionStartTick = m_clock ? m_clock->currentTick() : 0;
        m_outgoingTrack       = m_activeTrack;
        m_incomingTrack       = toTrackIndex;
        m_inTransition        = true;
        m_transition->start(m_outgoingTrack, m_incomingTrack, m_params);
    }, Qt::AutoConnection);
    return true;
}

// =============================================================================
// Touch-mode API
// =============================================================================

void FrameRouter::setHotkeyMode(HotkeyMode m)
{
    QMetaObject::invokeMethod(this, [this, m]() {
        if (m == m_hotkeyMode.load(std::memory_order_acquire)) return;
        m_hotkeyMode.store(m, std::memory_order_release);
        m_touchStack.clear();
        m_inTransition = false;   // drop any in-flight effect
        if (m_clock) processTick(m_clock->currentTick());   // re-emit
    }, Qt::AutoConnection);
}

void FrameRouter::setComposeMode(ComposeMode m)
{
    QMetaObject::invokeMethod(this, [this, m]() {
        if (m == m_composeMode.load(std::memory_order_acquire)) return;
        m_composeMode.store(m, std::memory_order_release);
        // Switching modes — drop any transient state from the live-VJ path so
        // a Layer → Live toggle doesn't resume mid-transition, and a Live →
        // Layer toggle doesn't leave touch keys held semantically.
        m_touchStack.clear();
        m_inTransition = false;
        if (m_clock) processTick(m_clock->currentTick());   // re-emit now
    }, Qt::AutoConnection);
}

QJsonObject FrameRouter::configToJson() const
{
    // Called rarely (project save), but must see a consistent snapshot of
    // m_currentTypeId + m_params (both mutated by setTransitionTypeId /
    // setTransitionParams on the router's thread).  Use a blocking
    // queued invocation so the GUI caller returns synchronously with the
    // up-to-date values.  Safe against router-thread processTick stalls
    // because config reads aren't on the playback hot path.
    auto buildJson = [this]() {
        QJsonObject o;
        o["transitionTypeId"] = m_currentTypeId;
        o["transitionParams"] = QJsonObject::fromVariantMap(m_params);
        o["hotkeyMode"] = (m_hotkeyMode.load(std::memory_order_acquire)
                           == HotkeyMode::Touch)
                         ? QStringLiteral("touch")
                         : QStringLiteral("switch");
        o["composeMode"] = (m_composeMode.load(std::memory_order_acquire)
                            == ComposeMode::LiveVJ)
                         ? QStringLiteral("live_vj")
                         : QStringLiteral("layer_composite");
        return o;
    };

    if (QThread::currentThread() == this->thread()) {
        return buildJson();
    }
    QJsonObject result;
    QMetaObject::invokeMethod(
        const_cast<FrameRouter*>(this),
        [&]() { result = buildJson(); },
        Qt::BlockingQueuedConnection);
    return result;
}

void FrameRouter::configFromJson(const QJsonObject& obj)
{
    if (obj.isEmpty()) return;

    // Each helper below is itself a thread-safe mutator (dispatches to
    // the router's thread), so this entry point is safe to call from
    // any thread — the individual mutations serialize through the
    // router's event loop in order.

    const QString typeId = obj.value("transitionTypeId").toString();
    if (!typeId.isEmpty()) {
        setTransitionTypeId(typeId);
    }

    const QJsonValue paramsVal = obj.value("transitionParams");
    if (paramsVal.isObject()) {
        setTransitionParams(paramsVal.toObject().toVariantMap());
    }

    const QString hk = obj.value("hotkeyMode").toString();
    if (hk == QLatin1String("touch")) {
        setHotkeyMode(HotkeyMode::Touch);
    } else if (hk == QLatin1String("switch")) {
        setHotkeyMode(HotkeyMode::Switch);
    }

    // composeMode missing from old projects → keep the constructor default
    // (LayerComposite).  Explicit "live_vj" string flips to the VJ path.
    const QString cm = obj.value("composeMode").toString();
    if (cm == QLatin1String("live_vj")) {
        setComposeMode(ComposeMode::LiveVJ);
    } else if (cm == QLatin1String("layer_composite")) {
        setComposeMode(ComposeMode::LayerComposite);
    }
}

void FrameRouter::requestTrackHoldPress(int trackIndex)
{
    // Mode gates read atomics from the caller's thread.  The touchStack
    // mutation + processTick dispatch to the router's thread below.
    if (m_composeMode.load(std::memory_order_acquire)
        == ComposeMode::LayerComposite) return;
    if (m_hotkeyMode.load(std::memory_order_acquire)
        != HotkeyMode::Touch) return;

    QMetaObject::invokeMethod(this, [this, trackIndex]() {
        if (!m_project) return;
        {
            QReadLocker lk(&m_project->stateLock());
            if (trackIndex < 0 || trackIndex >= m_project->trackCount()) return;
        }
        // Remove any stale entry (if the same key somehow came through twice
        // without a release — e.g. focus lost during hold), then push on top.
        m_touchStack.removeAll(trackIndex);
        m_touchStack.append(trackIndex);
        // Instant route refresh so the user sees the change immediately.
        if (m_clock) processTick(m_clock->currentTick());
    }, Qt::AutoConnection);
}

void FrameRouter::requestTrackHoldRelease(int trackIndex)
{
    if (m_composeMode.load(std::memory_order_acquire)
        == ComposeMode::LayerComposite) return;
    if (m_hotkeyMode.load(std::memory_order_acquire)
        != HotkeyMode::Touch) return;

    QMetaObject::invokeMethod(this, [this, trackIndex]() {
        m_touchStack.removeAll(trackIndex);
        if (m_clock) processTick(m_clock->currentTick());
    }, Qt::AutoConnection);
}

int FrameRouter::topLayerTrackAtTick(Tick tick) const
{
    if (!m_project) return 0;
    QReadLocker lk(&m_project->stateLock());
    const int n = m_project->trackCount();
    for (int t = 0; t < n; ++t) {
        const auto& track = m_project->track(t);
        if (track.clipIndexAtTick(tick) >= 0) return t;
    }
    return 0;   // nothing playing — default to track 0
}

void FrameRouter::refreshCurrentFrame()
{
    // Also called from onProjectChanged which already runs on this thread,
    // but accept cross-thread callers (e.g. MainWindow forcing a refresh
    // after a project load).  Qt::AutoConnection resolves correctly in
    // both cases.
    QMetaObject::invokeMethod(this, [this]() {
        if (!m_clock) return;
        processTick(m_clock->currentTick());
    }, Qt::AutoConnection);
}

// =============================================================================
// Per-tick processing
// =============================================================================

void FrameRouter::onTickAdvanced(Tick tick)
{
#if LAMOSH_TICK_DEBUG_LOG
    const Tick prev = m_pendingTick.load(std::memory_order_acquire);
    const bool wasQueued = m_pendingTickQueued.load(std::memory_order_acquire);
    qDebug() << "[rtr]" << tickLogWallMs() << "ms onTick tick=" << tick
             << "prevPending=" << prev << "wasQueued=" << wasQueued;
#endif
    // Coalesce: store the latest tick and ensure exactly one
    // processPendingTick() invocation is queued.  See member declaration
    // for the rationale — in particular, this is what stops the "plays on
    // briefly after spacebar" bug, because once pause() stops the clock no
    // new events fire, and the single pending invocation drains one tick
    // at most (not N).
    m_pendingTick.store(tick, std::memory_order_release);
    bool expected = false;
    if (m_pendingTickQueued.compare_exchange_strong(
            expected, true,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        QMetaObject::invokeMethod(this, [this]() { processPendingTick(); },
                                  Qt::QueuedConnection);
    }
}

void FrameRouter::processPendingTick()
{
    // Clear the queued flag FIRST so a tickAdvanced arriving mid-processTick
    // can queue the next invocation.  Reading m_pendingTick after clearing
    // the flag is safe: if a newer tick stored after our load, it will
    // observe the cleared flag and queue a fresh invocation; we just
    // process a slightly stale tick, and the fresh invocation processes
    // the newer one.  No tick is ever lost; some may be skipped (by design).
    m_pendingTickQueued.store(false, std::memory_order_release);
    const Tick tick = m_pendingTick.load(std::memory_order_acquire);
    if (tick < 0) return;
    processTick(tick);
    // The ENTER/EXIT markers inside processTick itself cover all call paths
    // including this one, so we don't need a separate "done" log here.
}

namespace {

// Per-tick snapshot of the data the compositor needs from the project.
// Copied out under a read-lock so the actual decode + blend phases can
// run lock-free (and, in the parallel-decode case, across pool threads
// without any project access at all).
struct TrackSnapshot {
    bool          enabled     { false };
    int           clipIdx     { -1 };
    SequencerClip clip;          // full snapshot when clipIdx >= 0
};

} // namespace

void FrameRouter::processTick(Tick tick)
{
#if LAMOSH_TICK_DEBUG_LOG
    const qint64 __ptStart = tickLogWallMs();
    qDebug() << "[rtr]" << __ptStart << "ms processTick ENTER tick=" << tick;
    // RAII guard — logs EXIT regardless of which return path we take.
    struct ExitLog {
        Tick t; qint64 startMs;
        ~ExitLog() {
            qDebug() << "[rtr]" << tickLogWallMs()
                     << "ms processTick EXIT tick=" << t
                     << "wall_dur_ms=" << (tickLogWallMs() - startMs);
        }
    } __exitLog { tick, __ptStart };
#endif
    if (m_chains.isEmpty() || !m_project) return;

    const int n = m_chains.size();

    // ── Phase 1: snapshot project state under the read lock ─────────────
    // This is the only place we touch m_project during the tick.  Copying
    // a SequencerClip is cheap (~100 bytes + a small QVector for effects);
    // doing it up-front frees the lock before any decode / blend work and
    // lets parallel decode tasks run on pool threads without races against
    // GUI-thread edits.
    QVector<TrackSnapshot> snap(n);
    {
        QReadLocker lk(&m_project->stateLock());
        const int pn = std::min(n, m_project->trackCount());
        for (int t = 0; t < pn; ++t) {
            const auto& track = m_project->track(t);
            snap[t].enabled = track.enabled;
            const int ci = track.clipIndexAtTick(tick);
            snap[t].clipIdx = ci;
            if (ci >= 0) snap[t].clip = track.clips[ci];
        }
    }

    // ── LayerComposite mode ──────────────────────────────────────────────
    // Bottom-up walk of every enabled track: decode the clip at this tick,
    // blend onto the accumulator using clip.effectiveOpacity(tick) and
    // clip.blendMode.  Decodes run in parallel across tracks; composite
    // step runs serially once all decodes complete.  This is the path used
    // while authoring a sequence so the preview matches what the offline
    // renderer will bake.  Hotkey switches and transitions are silently
    // inert in this mode.
    if (m_composeMode.load(std::memory_order_acquire)
        == ComposeMode::LayerComposite) {
        // Collect the indices of tracks whose frames we actually need.
        QVector<int> decodeIdx;
        decodeIdx.reserve(n);
        for (int t = 0; t < n; ++t) {
            if (snap[t].enabled && snap[t].clipIdx >= 0)
                decodeIdx.append(t);
        }
#if LAMOSH_TICK_DEBUG_LOG
        const qint64 __phDec = tickLogWallMs();
        qDebug() << "[rtr]" << __phDec << "ms LC decode start tracks="
                 << decodeIdx.size();
#endif

        QVector<QImage> layers(n);
        // Parallel decode: each pool worker touches exactly one chain, so
        // FFmpeg state per decoder stays single-threaded.  blockingMap
        // returns only when every decode finishes, so composite below sees
        // fully-populated layers[].
        QtConcurrent::blockingMap(decodeIdx, [&](int t) {
            QImage tmp;
            m_chains[t]->decodeToTick(snap[t].clipIdx, snap[t].clip,
                                      tick, /*produceImage=*/true, tmp);
            if (!tmp.isNull() && !snap[t].clip.effects.isEmpty()) {
                tmp.detach();
                applyClipEffects(tmp, snap[t].clip.effects);
            }
            layers[t] = std::move(tmp);
        });
#if LAMOSH_TICK_DEBUG_LOG
        const qint64 __phBlend = tickLogWallMs();
        qDebug() << "[rtr]" << __phBlend << "ms LC decode end decode_ms="
                 << (__phBlend - __phDec);
#endif

        QImage accum;
        for (int t = 0; t < n; ++t) {
            if (layers[t].isNull()) continue;
            QImage layer = std::move(layers[t]);

            if (accum.isNull()) {
                // First non-null layer sets the output size; start from black
                // so per-layer opacity blends "against the world" rather
                // than leaking the base layer's alpha.
                accum = QImage(layer.size(), QImage::Format_ARGB32);
                accum.fill(Qt::black);
            } else if (layer.size() != accum.size()) {
                // Mixed-resolution clips — rescale to the accumulator.
                layer = layer.scaled(accum.size(),
                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }

            const float alpha = snap[t].clip.effectiveOpacity(tick);
            if (alpha <= 0.0f) continue;
#if LAMOSH_TICK_DEBUG_LOG
            const qint64 __bStart = tickLogWallMs();
#endif
            applyBlend(accum, layer, alpha, snap[t].clip.blendMode);
#if LAMOSH_TICK_DEBUG_LOG
            qDebug() << "[rtr]" << tickLogWallMs()
                     << "ms LC blend track=" << t
                     << "alpha=" << alpha
                     << "ms=" << (tickLogWallMs() - __bStart);
#endif
        }

#if LAMOSH_TICK_DEBUG_LOG
        qDebug() << "[rtr]" << tickLogWallMs() << "ms LC blend end total_ms="
                 << (tickLogWallMs() - __phBlend);
#endif

        if (!accum.isNull()) {
            emit frameReady(std::move(accum), tick);
        }
        return;
    }

    // ── LiveVJ mode ──────────────────────────────────────────────────────
    // Touch mode — no transitions, just pick the held key (most recent)
    // or fall back to the top-layer track.
    if (m_hotkeyMode.load(std::memory_order_acquire) == HotkeyMode::Touch) {
        int effective = -1;
        if (!m_touchStack.isEmpty()) {
            effective = m_touchStack.last();
        } else {
            for (int t = 0; t < n; ++t) {
                if (snap[t].clipIdx >= 0) { effective = t; break; }
            }
            if (effective < 0) effective = 0;
        }

        // Parallel sync-only decode for inactive tracks + active decode.
        QVector<int> allIdx;
        allIdx.reserve(n);
        for (int i = 0; i < n; ++i) allIdx.append(i);

        QVector<QImage> produced(n);
        QtConcurrent::blockingMap(allIdx, [&](int i) {
            QImage tmp;
            const bool produce = (i == effective);
            m_chains[i]->decodeToTick(snap[i].clipIdx, snap[i].clip,
                                      tick, produce, tmp);
            if (produce) produced[i] = std::move(tmp);
        });

        if (effective >= 0 && !produced[effective].isNull()) {
            QImage out = std::move(produced[effective]);
            if (!snap[effective].clip.effects.isEmpty()) {
                out.detach();
                applyClipEffects(out, snap[effective].clip.effects);
            }
            if (m_activeTrack != effective) {
                m_activeTrack = effective;
                if (m_project) m_project->setActiveTrackIndex(effective);
            }
            emit frameReady(std::move(out), tick);
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
        if (elapsed >= m_transitionDuration)      progress = 1.0;
        else if (elapsed <= 0)                    progress = 0.0;
        else                                      progress =
            static_cast<double>(elapsed)
            / static_cast<double>(m_transitionDuration);
    }

    // Switch mode (steady-state or transitioning).  All chains decode to
    // stay in sync; produceImage=true only on the tracks whose pixels we
    // actually emit (active, or outgoing+incoming during a transition).
    QVector<bool> wantImg(n, false);
    if (m_inTransition) {
        if (m_outgoingTrack >= 0 && m_outgoingTrack < n) wantImg[m_outgoingTrack] = true;
        if (m_incomingTrack >= 0 && m_incomingTrack < n) wantImg[m_incomingTrack] = true;
    } else if (m_activeTrack >= 0 && m_activeTrack < n) {
        wantImg[m_activeTrack] = true;
    }

    QVector<int> allIdx;
    allIdx.reserve(n);
    for (int i = 0; i < n; ++i) allIdx.append(i);

    QVector<QImage> produced(n);
    QtConcurrent::blockingMap(allIdx, [&](int i) {
        QImage tmp;
        m_chains[i]->decodeToTick(snap[i].clipIdx, snap[i].clip,
                                  tick, wantImg[i], tmp);
        if (wantImg[i] && !tmp.isNull() && !snap[i].clip.effects.isEmpty()) {
            tmp.detach();
            applyClipEffects(tmp, snap[i].clip.effects);
        }
        if (wantImg[i]) produced[i] = std::move(tmp);
    });

    QImage finalImg;
    if (m_inTransition && m_transition
        && m_outgoingTrack >= 0 && m_outgoingTrack < n
        && m_incomingTrack >= 0 && m_incomingTrack < n) {
        const QImage& a = produced[m_outgoingTrack];
        const QImage& b = produced[m_incomingTrack];
        if (!a.isNull() && !b.isNull() && a.size() == b.size()) {
            finalImg = m_transition->compose(a, b, progress, m_params);
        } else if (!b.isNull()) {
            finalImg = b;
        } else if (!a.isNull()) {
            finalImg = a;
        }
        if (progress >= 1.0) {
            m_activeTrack  = m_incomingTrack;
            m_inTransition = false;
            if (m_project) m_project->setActiveTrackIndex(m_activeTrack);
        }
    } else if (m_activeTrack >= 0 && m_activeTrack < n) {
        finalImg = std::move(produced[m_activeTrack]);
    }

    if (!finalImg.isNull()) {
        emit frameReady(std::move(finalImg), tick);
    }
}

} // namespace sequencer
