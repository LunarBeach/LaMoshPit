#include "core/sequencer/TrackPlaybackChain.h"
#include "core/sequencer/SequencerProject.h"
#include "core/sequencer/SequencerTrack.h"
#include "core/sequencer/SequencerClipDecoder.h"

#include <QDebug>

namespace sequencer {

TrackPlaybackChain::TrackPlaybackChain(SequencerProject* project, int trackIndex,
                                       QObject* parent)
    : QObject(parent)
    , m_project(project)
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
}

bool TrackPlaybackChain::findClipAtTick(Tick t, int& outClipIdx) const
{
    if (!m_project) return false;
    if (m_trackIndex < 0 || m_trackIndex >= m_project->trackCount()) return false;
    const auto& tr = m_project->track(m_trackIndex);
    const int idx = tr.clipIndexAtTick(t);
    if (idx < 0) return false;
    outClipIdx = idx;
    return true;
}

bool TrackPlaybackChain::ensureDecoderForClip(int clipIdx)
{
    if (clipIdx == m_openClipIdx && m_decoder && m_decoder->isOpen()) return true;
    if (!m_project) return false;
    if (m_trackIndex < 0 || m_trackIndex >= m_project->trackCount()) return false;
    const auto& clip = m_project->track(m_trackIndex).clips[clipIdx];

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

bool TrackPlaybackChain::decodeToTick(Tick masterTick, bool produceImage,
                                      QImage& outImg)
{
    int clipIdx = -1;
    if (!findClipAtTick(masterTick, clipIdx)) {
        m_lastTick = masterTick;
        return false;
    }
    if (!ensureDecoderForClip(clipIdx)) {
        m_lastTick = masterTick;
        return false;
    }

    // Detect backward jump or fresh clip → re-seek.
    const bool backwardJump = (m_lastTick >= 0 && masterTick < m_lastTick);
    const bool firstCall    = (m_lastTick < 0);
    if (backwardJump || firstCall) {
        m_decoder->seekToMasterTick(masterTick);
    }

    QImage img;
    Tick   gotTick = 0;
    const bool ok = m_decoder->pullFrame(img, gotTick, produceImage);
    m_lastTick = masterTick;
    if (!ok) return false;
    if (produceImage) outImg = std::move(img);
    return true;
}

} // namespace sequencer
