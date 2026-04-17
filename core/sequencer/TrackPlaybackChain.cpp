#include "core/sequencer/TrackPlaybackChain.h"
#include "core/sequencer/SequencerClipDecoder.h"

#include <QDebug>

namespace sequencer {

TrackPlaybackChain::TrackPlaybackChain(int trackIndex, QObject* parent)
    : QObject(parent)
    , m_trackIndex(trackIndex)
{}

TrackPlaybackChain::~TrackPlaybackChain() = default;

void TrackPlaybackChain::setTrackIndex(int idx)
{
    if (idx == m_trackIndex) return;
    m_trackIndex = idx;
    invalidate();
}

void TrackPlaybackChain::invalidate()
{
    m_decoder.reset();
    m_openClipIdx = -1;
    m_lastTick    = -1;
    m_cachedTick  = -1;
    m_cachedImage = QImage();
}

bool TrackPlaybackChain::ensureDecoderForClip(int clipIdx,
                                              const SequencerClip& clip)
{
    if (clipIdx == m_openClipIdx && m_decoder && m_decoder->isOpen()) return true;

    m_decoder = std::make_unique<SequencerClipDecoder>();
    if (!m_decoder->open(clip)) {
        qWarning() << "[TrackPlaybackChain T" << m_trackIndex
                   << "] open failed:" << m_decoder->lastError();
        m_decoder.reset();
        m_openClipIdx = -1;
        return false;
    }
    m_openClipIdx = clipIdx;
    return true;
}

bool TrackPlaybackChain::decodeToTick(int clipIdx, const SequencerClip& clip,
                                      Tick masterTick, bool produceImage,
                                      QImage& outImg)
{
    if (clipIdx < 0) {
        m_lastTick = masterTick;
        return false;
    }
    const bool clipChanged = (clipIdx != m_openClipIdx);
    if (!ensureDecoderForClip(clipIdx, clip)) {
        m_lastTick = masterTick;
        return false;
    }

    // Detect backward jump, fresh clip, or first call → re-seek and drop
    // the cached frame.  Forward motion within the same clip does NOT
    // reseek; the catch-up loop below walks the decoder forward.
    const bool backwardJump = (m_lastTick >= 0 && masterTick < m_lastTick);
    const bool firstCall    = (m_lastTick < 0);
    if (backwardJump || firstCall || clipChanged) {
        m_decoder->seekToMasterTick(masterTick);
        m_cachedTick  = -1;
        m_cachedImage = QImage();
    }

    // If the caller suddenly wants an image but our cached frame is empty
    // (e.g. a LiveVJ track flipped from inactive/produceImage=false to
    // active mid-playback — we'd been advancing the decoder cursor via
    // produce=false calls, so m_cachedTick is current but m_cachedImage
    // never got populated), re-seek so the loop below can decode a fresh
    // frame AT masterTick rather than overshooting.
    if (produceImage && m_cachedImage.isNull() && m_cachedTick >= 0) {
        m_decoder->seekToMasterTick(masterTick);
        m_cachedTick = -1;
    }

    // Catch-up loop: advance the decoder until its cursor reaches masterTick,
    // caching the most recent frame.  See class header for full semantics.
    while (m_cachedTick < masterTick) {
        QImage img;
        Tick   gotTick = 0;
        if (!m_decoder->pullFrame(img, gotTick, produceImage)) break;
        m_cachedTick = gotTick;
        if (produceImage) m_cachedImage = std::move(img);
    }

    m_lastTick = masterTick;
    if (produceImage) outImg = m_cachedImage;
    return !m_cachedImage.isNull() || !produceImage;
}

} // namespace sequencer
