#pragma once

// =============================================================================
// TrackPlaybackChain — owns ONE track's decoder state and produces the
// frame at a requested master tick.
//
// One chain exists per track in the project (up to MaxTracks = 9).  The
// FrameRouter calls decodeToTick(clipIdx, clipSnapshot, tick, produceImage)
// on each chain every tick.  The chain takes an immutable clip snapshot so
// it can be invoked from a worker pool thread without holding the project
// read lock across the full decode — the router snapshots state under the
// lock, then releases it before dispatching parallel decode tasks.
//
// Two produceImage modes let the router avoid wasted work:
//   - true  : full decode + swscale + QImage — used for the active track
//             and (during a transition) the incoming track.
//   - false : decode only — used for background tracks so their decoder
//             stays in lockstep with the master clock, ready to be flipped
//             to active in zero frames.
//
// The chain reopens its internal SequencerClipDecoder whenever clipIdx
// changes (new clip under the playhead), or whenever invalidate() is
// called (typically on a project edit that might replace the clip).
//
// Thread-affinity: all decodeToTick / invalidate calls for a given chain
// must be serialised — FFmpeg decoder state is not thread-safe within one
// instance.  QtConcurrent::blockingMap over chains is safe (each chain is
// touched by exactly one pool worker per tick).
// =============================================================================

#include "core/sequencer/SequencerClip.h"
#include "core/sequencer/Tick.h"
#include <QImage>
#include <QObject>
#include <memory>

namespace sequencer {

class SequencerClipDecoder;

class TrackPlaybackChain : public QObject {
    Q_OBJECT
public:
    explicit TrackPlaybackChain(int trackIndex, QObject* parent = nullptr);
    ~TrackPlaybackChain() override;

    int  trackIndex() const { return m_trackIndex; }
    void setTrackIndex(int idx);

    // Advance / seek this track's decoder to masterTick, using the supplied
    // clip snapshot (the router has already read-locked the project to copy
    // these fields out).  clipIdx identifies which clip on the track the
    // snapshot corresponds to — when it changes, the chain reopens the
    // decoder.  produceImage controls swscale + QImage allocation; see
    // class header for semantics.  Returns true on success (frame cached
    // or produced).
    bool decodeToTick(int clipIdx, const SequencerClip& clip,
                      Tick masterTick, bool produceImage, QImage& outImg);

    // Drop any open decoder.  Next decodeToTick reopens against the
    // supplied snapshot.  Called on project edits that might invalidate
    // the currently-open clip (track add/remove, clip replacement).
    void invalidate();

private:
    bool ensureDecoderForClip(int clipIdx, const SequencerClip& clip);

    int m_trackIndex { 0 };

    std::unique_ptr<SequencerClipDecoder> m_decoder;
    int  m_openClipIdx { -1 };
    Tick m_lastTick    { -1 };

    // Catch-up state.  m_cachedTick is the master-timeline tick of the
    // most recent frame pulled from m_decoder (not the requested tick).
    // m_cachedImage holds the decoded pixels so we can re-emit the same
    // frame on subsequent ticks when the source frame-rate is lower than
    // the timeline's tick cadence (each source frame is "held" across
    // multiple ticks without re-decoding).  Reset on seek / clip change.
    Tick   m_cachedTick { -1 };
    QImage m_cachedImage;
};

} // namespace sequencer
