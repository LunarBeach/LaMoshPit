#include "core/sequencer/SequencerPlaybackClock.h"

extern "C" {
#include <libavutil/mathematics.h>
}

#include <algorithm>

namespace sequencer {

SequencerPlaybackClock::SequencerPlaybackClock(QObject* parent)
    : QObject(parent)
{
    // Cadence is derived from output framerate; setOutputFrameRate primes
    // m_tickPerFrame and the timer interval.
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout,
            this, &SequencerPlaybackClock::onTimerTick);
    setOutputFrameRate({30, 1});
}

SequencerPlaybackClock::~SequencerPlaybackClock() = default;

void SequencerPlaybackClock::setOutputFrameRate(AVRational fps)
{
    if (fps.num <= 0 || fps.den <= 0) return;
    m_fps = fps;
    m_tickPerFrame = frameDurationTicks(fps);
    // Timer interval in ms = 1000 * den / num.  For 30/1 this is 33 ms
    // which is close enough to 33.333 to keep up; the compensating
    // accumulator in onTimerTick absorbs the round-off.
    int intervalMs = static_cast<int>(
        av_rescale_q(1, AVRational{fps.den, fps.num}, AVRational{1, 1000}));
    if (intervalMs < 1) intervalMs = 1;
    m_timer.setInterval(intervalMs);
}

void SequencerPlaybackClock::setLoopRegion(Tick inTicks, Tick outTicks, bool enabled)
{
    m_loopIn      = std::max<Tick>(0, inTicks);
    m_loopOut     = std::max<Tick>(m_loopIn, outTicks);
    m_loopEnabled = enabled;
}

void SequencerPlaybackClock::setEndTicks(Tick endTicks)
{
    m_endTicks = std::max<Tick>(0, endTicks);
}

void SequencerPlaybackClock::play()
{
    if (m_playing) return;
    m_playing = true;
    m_wall.start();
    m_lastWallNs = m_wall.nsecsElapsed();
    m_timer.start();
}

void SequencerPlaybackClock::pause()
{
    if (!m_playing) return;
    m_playing = false;
    m_timer.stop();
}

void SequencerPlaybackClock::stop()
{
    pause();
    m_currentTick = 0;
    emit tickAdvanced(m_currentTick);
}

void SequencerPlaybackClock::seek(Tick tick)
{
    m_currentTick = std::max<Tick>(0, tick);
    m_lastWallNs  = m_wall.isValid() ? m_wall.nsecsElapsed() : 0;
    emit tickAdvanced(m_currentTick);
}

Tick SequencerPlaybackClock::computeTickDelta()
{
    if (!m_wall.isValid()) return 0;
    const qint64 nowNs = m_wall.nsecsElapsed();
    const qint64 dtNs  = nowNs - m_lastWallNs;
    m_lastWallNs = nowNs;
    if (dtNs <= 0) return 0;

    // ticks_per_second = SequencerTickRate (90000).  dt * 90000 / 1e9 ticks.
    // Use rescale to stay in int64 without overflow.
    return av_rescale(dtNs, SequencerTickRate, 1'000'000'000LL);
}

void SequencerPlaybackClock::onTimerTick()
{
    if (!m_playing) return;

    const Tick delta = computeTickDelta();
    if (delta <= 0) return;

    Tick next = m_currentTick + delta;

    // Loop-mode wrap.  If the new tick has crossed loopOut, wrap back to
    // loopIn plus the overshoot.  Use a while loop in case the delta is
    // massive (shouldn't happen under normal cadence but the invariant
    // protects us against long stalls).
    if (m_loopEnabled && m_loopOut > m_loopIn) {
        while (next >= m_loopOut) {
            const Tick span = m_loopOut - m_loopIn;
            next = m_loopIn + (next - m_loopOut) % std::max<Tick>(1, span);
            if (span == 0) { next = m_loopIn; break; }
        }
    } else if (m_endTicks > 0 && next >= m_endTicks) {
        // Non-loop: pin to end, stop, emit signal.
        m_currentTick = m_endTicks;
        pause();
        emit tickAdvanced(m_currentTick);
        emit reachedEnd();
        return;
    }

    m_currentTick = next;
    emit tickAdvanced(m_currentTick);
}

} // namespace sequencer
