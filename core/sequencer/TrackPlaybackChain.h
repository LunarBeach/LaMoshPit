#pragma once

// =============================================================================
// TrackPlaybackChain — owns ONE track's decoder state and produces the
// frame at a requested master tick.
//
// One chain exists per track in the project (up to MaxTracks = 9).  All
// chains are subscribed indirectly to the same PlaybackClock through the
// FrameRouter, which calls decodeToTick(currentTick, produceImage) on each
// chain every frame.
//
// Two produceImage modes let the router avoid wasted work:
//   - true  : full decode + swscale + QImage — used for the active track
//             and (during a transition) the incoming track.
//   - false : decode only — used for background tracks so their decoder
//             stays in lockstep with the master clock, ready to be flipped
//             to active in zero frames.
//
// The chain reopens its internal SequencerClipDecoder whenever the active
// clip under the playhead changes, or whenever invalidate() is called
// (typically on a project edit).
// =============================================================================

#include "core/sequencer/Tick.h"
#include <QImage>
#include <QObject>
#include <memory>

namespace sequencer {

class SequencerProject;
class SequencerClipDecoder;

class TrackPlaybackChain : public QObject {
    Q_OBJECT
public:
    TrackPlaybackChain(SequencerProject* project, int trackIndex,
                       QObject* parent = nullptr);
    ~TrackPlaybackChain() override;

    int  trackIndex() const { return m_trackIndex; }
    void setTrackIndex(int idx);

    // Advance / seek this track's decoder to masterTick.  Returns true if
    // the frame was produced successfully.  outImg is populated only when
    // produceImage is true; otherwise it's left as-is.
    //
    // Returns false when there's no clip at the requested tick (gap or
    // past-end) or decode failed — in which case the caller should treat
    // the track as "no frame available" for this tick.
    bool decodeToTick(Tick masterTick, bool produceImage, QImage& outImg);

    // Drop any open decoder.  Next decodeToTick reopens against the
    // current project state.  Called on project edits.
    void invalidate();

private:
    bool ensureDecoderForClip(int clipIdx);
    bool findClipAtTick(Tick t, int& outClipIdx) const;

    SequencerProject* m_project { nullptr };
    int               m_trackIndex { 0 };

    std::unique_ptr<SequencerClipDecoder> m_decoder;
    int  m_openClipIdx { -1 };
    Tick m_lastTick    { -1 };
};

} // namespace sequencer
