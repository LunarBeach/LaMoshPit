#pragma once

// =============================================================================
// SequencerClipDecoder — one FFmpeg decoder dedicated to one SequencerClip.
//
// Owns the AVFormatContext + AVCodecContext + optional D3D11VA hw context +
// an swscale context that produces BGRA for display.  A ClipDecoder is
// "one clip's worth" of decode state: opening it seeks to the clip's
// sourceInTicks; pulling frames walks forward until sourceOutTicks.  Clip
// boundaries are the compositor's responsibility — this class doesn't know
// about the sequence.
//
// API is synchronous and called from the compositor's thread (single
// consumer).  No internal locking.  The class is non-copyable non-movable
// because AVCodecContext has non-portable internal self-references.
//
// Typical lifecycle:
//
//   auto dec = std::make_unique<SequencerClipDecoder>();
//   if (!dec->open(clip)) { ... }
//   dec->seekToMasterTick(clip.timelineStartTicks);
//   while (playing) {
//       QImage img;
//       Tick   outTick;
//       if (dec->pullFrame(img, outTick)) { displaySink(img); }
//   }
//
// Output format: QImage::Format_BGRA8888, pre-scaled to the source
// resolution.  Downscaling / letterboxing is the widget's responsibility.
// =============================================================================

#include "core/sequencer/SequencerClip.h"
#include <QImage>
#include <memory>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace sequencer {

class SequencerClipDecoder {
public:
    SequencerClipDecoder();
    ~SequencerClipDecoder();

    SequencerClipDecoder(const SequencerClipDecoder&)            = delete;
    SequencerClipDecoder& operator=(const SequencerClipDecoder&) = delete;

    // Open the clip's source file and select its video stream.  Sets up the
    // codec (tries D3D11VA first, falls back to SW).  Call once per clip;
    // a ClipDecoder instance is bound to one source file for its lifetime.
    // Returns false on any failure with an error string available via
    // lastError().
    bool open(const SequencerClip& clip);

    // Seek so the next pullFrame() returns the frame at (or just past)
    // masterTick in the timeline.  masterTick is absolute timeline time;
    // the decoder converts it internally to the clip's source PTS.
    // Returns false on seek failure (rare; usually EOF attempts).
    bool seekToMasterTick(Tick masterTick);

    // Decode the next frame.  On success fills img with a BGRA QImage
    // (shares no buffers with the decoder — safe to keep) and sets
    // outMasterTick to the frame's presentation time in master ticks.
    // Returns false at EOF / clip-out / fatal decode error.
    //
    // produceImage=false skips the swscale + QImage allocation step (used
    // by the FrameRouter to keep background tracks' decoder state in sync
    // without paying the per-frame CPU cost).  outImg is left untouched in
    // that case; outMasterTick is still populated.
    bool pullFrame(QImage& outImg, Tick& outMasterTick, bool produceImage = true);

    // True once open() succeeded and a stream is ready for pullFrame().
    bool isOpen() const { return m_isOpen; }

    // Indicates whether the codec ended up using HW acceleration.
    // Informational only — both paths produce identical output frames.
    bool isHwAccelerated() const { return m_hwAccel; }

    const QString& lastError() const { return m_lastError; }

    const SequencerClip& clip() const { return m_clip; }

private:
    // Helpers kept out of the header to minimize compile-time includes.
    bool ensureSwsBgra(int width, int height, int srcPixFmt);
    void closeAll();

    SequencerClip    m_clip;
    AVFormatContext* m_fmt       { nullptr };
    AVCodecContext*  m_codec     { nullptr };
    AVFrame*         m_decoded   { nullptr };   // raw decode output (HW or SW)
    AVFrame*         m_sw        { nullptr };   // CPU-side frame for swscale input
    AVPacket*        m_pkt       { nullptr };
    SwsContext*      m_sws       { nullptr };
    int              m_videoStreamIdx { -1 };
    int              m_swsWidth  { 0 };
    int              m_swsHeight { 0 };
    int              m_swsSrcFmt { -1 };
    bool             m_isOpen    { false };
    bool             m_hwAccel   { false };
    bool             m_eof       { false };
    QString          m_lastError;
};

} // namespace sequencer
