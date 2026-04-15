#include "core/sequencer/EditCommand.h"
#include "core/sequencer/SequencerProject.h"

#include <algorithm>

namespace sequencer {

// =============================================================================
// AddTrackCmd
// =============================================================================

AddTrackCmd::AddTrackCmd(QString name)
    : m_name(std::move(name)) {}

bool AddTrackCmd::redo(SequencerProject& project) {
    auto& tracks = project.tracksMutable();
    if (tracks.size() >= SequencerProject::MaxTracks) return false;
    SequencerTrack t;
    t.name = m_name;
    tracks.append(t);
    m_insertedAt = tracks.size() - 1;
    return true;
}

void AddTrackCmd::undo(SequencerProject& project) {
    auto& tracks = project.tracksMutable();
    if (m_insertedAt < 0 || m_insertedAt >= tracks.size()) return;
    tracks.removeAt(m_insertedAt);
    // Clamp active track if it pointed past the end now.
    if (project.activeTrackIndex() >= tracks.size() && !tracks.isEmpty()) {
        project.setActiveTrackIndex(tracks.size() - 1);
    }
}

// =============================================================================
// RemoveTrackCmd
// =============================================================================

RemoveTrackCmd::RemoveTrackCmd(int trackIndex)
    : m_trackIndex(trackIndex) {}

bool RemoveTrackCmd::redo(SequencerProject& project) {
    auto& tracks = project.tracksMutable();
    if (m_trackIndex < 0 || m_trackIndex >= tracks.size()) return false;
    m_saved = tracks[m_trackIndex];
    m_savedActiveIndex = project.activeTrackIndex();
    tracks.removeAt(m_trackIndex);
    // Rebind active index if it was this track or past it.
    if (!tracks.isEmpty()) {
        const int clamped = std::clamp(m_savedActiveIndex, 0, int(tracks.size() - 1));
        project.setActiveTrackIndex(clamped);
    }
    return true;
}

void RemoveTrackCmd::undo(SequencerProject& project) {
    auto& tracks = project.tracksMutable();
    const int idx = std::clamp(m_trackIndex, 0, int(tracks.size()));
    tracks.insert(idx, m_saved);
    project.setActiveTrackIndex(m_savedActiveIndex);
}

// =============================================================================
// AppendClipCmd
// =============================================================================

AppendClipCmd::AppendClipCmd(int trackIndex, SequencerClip clip)
    : m_trackIndex(trackIndex), m_clip(std::move(clip)) {}

bool AppendClipCmd::redo(SequencerProject& project) {
    if (m_trackIndex < 0 || m_trackIndex >= project.trackCount()) return false;
    auto& track = project.trackMutable(m_trackIndex);
    SequencerClip c = m_clip;
    c.timelineStartTicks = track.totalDurationTicks();
    track.clips.append(c);
    // No repack needed — we appended at the end using the exact current tail.
    return true;
}

void AppendClipCmd::undo(SequencerProject& project) {
    auto& track = project.trackMutable(m_trackIndex);
    if (track.clips.isEmpty()) return;
    track.clips.removeLast();
}

// =============================================================================
// InsertClipCmd
// =============================================================================

InsertClipCmd::InsertClipCmd(int trackIndex, int clipIndex, SequencerClip clip)
    : m_trackIndex(trackIndex), m_clipIndex(clipIndex), m_clip(std::move(clip)) {}

bool InsertClipCmd::redo(SequencerProject& project) {
    if (m_trackIndex < 0 || m_trackIndex >= project.trackCount()) return false;
    auto& track = project.trackMutable(m_trackIndex);
    if (m_clipIndex < 0 || m_clipIndex > track.clips.size()) return false;
    track.clips.insert(m_clipIndex, m_clip);
    track.repackContiguous();
    return true;
}

void InsertClipCmd::undo(SequencerProject& project) {
    auto& track = project.trackMutable(m_trackIndex);
    if (m_clipIndex < 0 || m_clipIndex >= track.clips.size()) return;
    track.clips.removeAt(m_clipIndex);
    track.repackContiguous();
}

// =============================================================================
// RemoveClipCmd
// =============================================================================

RemoveClipCmd::RemoveClipCmd(int trackIndex, int clipIndex)
    : m_trackIndex(trackIndex), m_clipIndex(clipIndex) {}

bool RemoveClipCmd::redo(SequencerProject& project) {
    if (m_trackIndex < 0 || m_trackIndex >= project.trackCount()) return false;
    auto& track = project.trackMutable(m_trackIndex);
    if (m_clipIndex < 0 || m_clipIndex >= track.clips.size()) return false;
    m_saved = track.clips[m_clipIndex];
    track.clips.removeAt(m_clipIndex);
    track.repackContiguous();
    return true;
}

void RemoveClipCmd::undo(SequencerProject& project) {
    auto& track = project.trackMutable(m_trackIndex);
    const int idx = std::clamp(m_clipIndex, 0, int(track.clips.size()));
    track.clips.insert(idx, m_saved);
    track.repackContiguous();
}

// =============================================================================
// MoveClipCmd
// =============================================================================

MoveClipCmd::MoveClipCmd(int trackIndex, int fromIndex, int toIndex)
    : m_trackIndex(trackIndex), m_fromIndex(fromIndex), m_toIndex(toIndex) {}

bool MoveClipCmd::redo(SequencerProject& project) {
    if (m_trackIndex < 0 || m_trackIndex >= project.trackCount()) return false;
    auto& track = project.trackMutable(m_trackIndex);
    const int n = track.clips.size();
    if (m_fromIndex < 0 || m_fromIndex >= n) return false;
    if (m_toIndex   < 0 || m_toIndex   >= n) return false;
    if (m_fromIndex == m_toIndex) return true;  // no-op but valid
    track.clips.move(m_fromIndex, m_toIndex);
    track.repackContiguous();
    return true;
}

void MoveClipCmd::undo(SequencerProject& project) {
    auto& track = project.trackMutable(m_trackIndex);
    // Reverse the move: back from m_toIndex to m_fromIndex.
    if (m_fromIndex == m_toIndex) return;
    track.clips.move(m_toIndex, m_fromIndex);
    track.repackContiguous();
}

// =============================================================================
// MoveClipAcrossTracksCmd
// =============================================================================

MoveClipAcrossTracksCmd::MoveClipAcrossTracksCmd(int fromTrack, int fromClipIdx,
                                                 int toTrack,   int toClipIdx)
    : m_fromTrack(fromTrack)
    , m_fromClipIdx(fromClipIdx)
    , m_toTrack(toTrack)
    , m_toClipIdx(toClipIdx)
{}

bool MoveClipAcrossTracksCmd::redo(SequencerProject& project) {
    if (m_fromTrack < 0 || m_fromTrack >= project.trackCount()) return false;
    if (m_toTrack   < 0 || m_toTrack   >= project.trackCount()) return false;
    if (m_fromTrack == m_toTrack) return false;  // use MoveClipCmd instead

    auto& src = project.trackMutable(m_fromTrack);
    if (m_fromClipIdx < 0 || m_fromClipIdx >= src.clips.size()) return false;

    m_saved = src.clips[m_fromClipIdx];
    src.clips.removeAt(m_fromClipIdx);
    src.repackContiguous();

    auto& dst = project.trackMutable(m_toTrack);
    const int dstIdx = std::clamp(m_toClipIdx, 0, int(dst.clips.size()));
    m_toClipIdx = dstIdx;   // remember the clamped index for undo
    dst.clips.insert(dstIdx, m_saved);
    dst.repackContiguous();
    return true;
}

void MoveClipAcrossTracksCmd::undo(SequencerProject& project) {
    auto& dst = project.trackMutable(m_toTrack);
    if (m_toClipIdx < 0 || m_toClipIdx >= dst.clips.size()) return;
    dst.clips.removeAt(m_toClipIdx);
    dst.repackContiguous();

    auto& src = project.trackMutable(m_fromTrack);
    const int srcIdx = std::clamp(m_fromClipIdx, 0, int(src.clips.size()));
    src.clips.insert(srcIdx, m_saved);
    src.repackContiguous();
}

// =============================================================================
// TrimClipCmd
// =============================================================================

TrimClipCmd::TrimClipCmd(int trackIndex, int clipIndex,
                         Tick newInTicks, Tick newOutTicks)
    : m_trackIndex(trackIndex)
    , m_clipIndex(clipIndex)
    , m_newIn(newInTicks)
    , m_newOut(newOutTicks) {}

bool TrimClipCmd::redo(SequencerProject& project) {
    if (m_trackIndex < 0 || m_trackIndex >= project.trackCount()) return false;
    auto& track = project.trackMutable(m_trackIndex);
    if (m_clipIndex < 0 || m_clipIndex >= track.clips.size()) return false;
    auto& clip = track.clips[m_clipIndex];

    // Clamp requested trim to legal source range.
    Tick in  = std::clamp<Tick>(m_newIn,  0, clip.sourceDurationTicks);
    Tick out = std::clamp<Tick>(m_newOut, 0, clip.sourceDurationTicks);
    if (out <= in) return false;   // invalid — reject, caller must fix

    m_oldIn  = clip.sourceInTicks;
    m_oldOut = clip.sourceOutTicks;
    clip.sourceInTicks  = in;
    clip.sourceOutTicks = out;
    track.repackContiguous();
    return true;
}

void TrimClipCmd::undo(SequencerProject& project) {
    auto& track = project.trackMutable(m_trackIndex);
    if (m_clipIndex < 0 || m_clipIndex >= track.clips.size()) return;
    auto& clip = track.clips[m_clipIndex];
    clip.sourceInTicks  = m_oldIn;
    clip.sourceOutTicks = m_oldOut;
    track.repackContiguous();
}

// =============================================================================
// SplitClipCmd
// =============================================================================

SplitClipCmd::SplitClipCmd(int trackIndex, int clipIndex, Tick splitTimelineTick)
    : m_trackIndex(trackIndex)
    , m_clipIndex(clipIndex)
    , m_splitTimelineTick(splitTimelineTick) {}

bool SplitClipCmd::redo(SequencerProject& project) {
    if (m_trackIndex < 0 || m_trackIndex >= project.trackCount()) return false;
    auto& track = project.trackMutable(m_trackIndex);
    if (m_clipIndex < 0 || m_clipIndex >= track.clips.size()) return false;
    auto& clip = track.clips[m_clipIndex];

    const Tick clipStart = clip.timelineStartTicks;
    const Tick clipEnd   = clip.timelineEndTicks();
    if (m_splitTimelineTick <= clipStart || m_splitTimelineTick >= clipEnd) {
        return false;   // split point outside clip — reject
    }

    // Offset inside the clip, mapped to source ticks.
    const Tick offsetTicks = m_splitTimelineTick - clipStart;
    const Tick splitSourceTick = clip.sourceInTicks + offsetTicks;

    // Build right half.  Same source, takes [splitSourceTick, originalOut].
    SequencerClip right = clip;
    right.sourceInTicks = splitSourceTick;
    // right.sourceOutTicks already equals original out.

    // Truncate the left half to end at splitSourceTick.
    m_originalSourceOut = clip.sourceOutTicks;
    clip.sourceOutTicks = splitSourceTick;

    track.clips.insert(m_clipIndex + 1, right);
    track.repackContiguous();
    return true;
}

void SplitClipCmd::undo(SequencerProject& project) {
    auto& track = project.trackMutable(m_trackIndex);
    // Remove the right half (inserted at m_clipIndex + 1).
    if (m_clipIndex + 1 >= track.clips.size()) return;
    track.clips.removeAt(m_clipIndex + 1);
    // Restore the left half's original out point.
    if (m_clipIndex < track.clips.size()) {
        track.clips[m_clipIndex].sourceOutTicks = m_originalSourceOut;
    }
    track.repackContiguous();
}

} // namespace sequencer
