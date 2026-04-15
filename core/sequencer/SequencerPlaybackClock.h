#pragma once

// =============================================================================
// SequencerPlaybackClock — drives the master tick that all per-track
// compositors follow during playback.
//
// The clock is decoupled from any individual clip's framerate.  It emits
// tickAdvanced() at the project's output framerate (typically 30 or 60 fps);
// compositors respond by rendering whichever source frame maps to the new
// tick.  This is what makes mixed-FPS clips on the same timeline work:
// the clock cadence is fixed, the per-clip resampling happens at lookup
// time.
//
// Implementation: wraps QTimer for the cadence + std::chrono::steady_clock
// for drift-free absolute time.  On each timer tick we compute how many
// master ticks have elapsed since playback start and emit exactly once,
// even if the timer was late (Qt's event loop can stall briefly under GUI
// load).  This keeps long-session drift to zero.
//
// Loop mode: when the clock reaches loopOutTicks and looping is enabled,
// it snaps the accumulator back to loopInTicks and keeps running.  No
// gap in the emitted tick stream — callers should treat a backward jump
// in tickAdvanced() as "loop wrapped" and re-seek their decoders.
// =============================================================================

#include "core/sequencer/Tick.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>

namespace sequencer {

class SequencerPlaybackClock : public QObject {
    Q_OBJECT
public:
    explicit SequencerPlaybackClock(QObject* parent = nullptr);
    ~SequencerPlaybackClock() override;

    // Set how often tickAdvanced() fires.  Given in frames-per-second via
    // AVRational to keep NTSC rates exact.  Default is 30/1.
    void setOutputFrameRate(AVRational fps);

    // Set loop region + enable flag.  Safe to call during playback — the
    // next wrap uses the new values.
    void setLoopRegion(Tick inTicks, Tick outTicks, bool enabled);

    // Set the upper bound of non-loop playback.  When the clock reaches
    // this tick and looping is OFF, playback auto-stops.  Set to 0 to
    // disable the stop (clock runs forever).
    void setEndTicks(Tick endTicks);

    // ── Transport ────────────────────────────────────────────────────────
    void play();
    void pause();
    void stop();   // pause + rewind to 0

    // Seek the clock's current tick without changing play state.  Emits
    // tickAdvanced() once with the new value so listeners re-sync.
    void seek(Tick tick);

    bool isPlaying() const { return m_playing; }
    Tick currentTick() const { return m_currentTick; }

signals:
    // Emitted each time the master tick advances (or wraps).  Listeners
    // should treat newTick < prevTick as a loop wrap or an explicit seek.
    void tickAdvanced(Tick newTick);

    // Emitted when playback stops automatically at endTicks (looping off).
    void reachedEnd();

private slots:
    void onTimerTick();

private:
    Tick computeTickDelta();  // ticks to advance since last onTimerTick

    QTimer        m_timer;
    QElapsedTimer m_wall;          // monotonic wall clock
    qint64        m_lastWallNs { 0 };

    AVRational    m_fps         { 30, 1 };
    Tick          m_tickPerFrame{ 0 };

    Tick m_currentTick { 0 };
    Tick m_loopIn      { 0 };
    Tick m_loopOut     { 0 };
    Tick m_endTicks    { 0 };
    bool m_loopEnabled { false };
    bool m_playing     { false };
};

} // namespace sequencer
