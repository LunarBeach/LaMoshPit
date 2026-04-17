#pragma once

// =============================================================================
// EditCommand — base class for all undoable operations on a SequencerProject.
//
// Every mutation (append clip, insert clip, remove clip, move clip, trim
// clip, split clip, add track, remove track) is a concrete subclass of
// EditCommand.  The project's executeCommand() calls redo() once to apply,
// then owns the command on its undo stack until either undo() is called
// (moves to redo stack) or a fresh executeCommand() clears redo.
//
// redo() returns false to indicate "cannot apply" (e.g. invalid params);
// in that case the project rejects the command and it is not pushed onto
// the undo stack.
//
// Commands capture enough state in their constructors or on first redo()
// to reverse themselves without re-reading the project.  This is critical
// for TrimClipCmd and MoveClipCmd where the "before" state must survive
// subsequent edits to other clips.
// =============================================================================

#include "core/sequencer/SequencerClip.h"
#include "core/sequencer/SequencerTrack.h"
#include <QString>

namespace sequencer {

class SequencerProject;

class EditCommand {
public:
    virtual ~EditCommand() = default;

    virtual bool redo(SequencerProject& project) = 0;
    virtual void undo(SequencerProject& project) = 0;

    // Human-readable label — shown in an "Undo X" menu item if the UI wants it.
    virtual QString label() const = 0;
};

// ── AddTrackCmd ──────────────────────────────────────────────────────────────
// Append a new empty track.  Capped at SequencerProject::MaxTracks.
class AddTrackCmd : public EditCommand {
public:
    explicit AddTrackCmd(QString name);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Add Track"; }
private:
    QString m_name;
    int     m_insertedAt { -1 };   // set on redo, read on undo
};

// ── RemoveTrackCmd ───────────────────────────────────────────────────────────
class RemoveTrackCmd : public EditCommand {
public:
    explicit RemoveTrackCmd(int trackIndex);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Remove Track"; }
private:
    int            m_trackIndex;
    SequencerTrack m_saved;            // captured on redo for undo restore
    int            m_savedActiveIndex { 0 };
};

// ── AppendClipCmd ────────────────────────────────────────────────────────────
// Add a clip to the END of a track.  The clip argument must already have
// sourcePath + probed metadata + trim window populated by the caller (the
// probe happens in the UI / import layer, outside the command).
class AppendClipCmd : public EditCommand {
public:
    AppendClipCmd(int trackIndex, SequencerClip clip);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Append Clip"; }
private:
    int           m_trackIndex;
    SequencerClip m_clip;
};

// ── InsertClipCmd ────────────────────────────────────────────────────────────
// Insert a clip at a specific index (0..track.clips.size()).  Subsequent
// clips on the track shift forward in time via repackContiguous().
class InsertClipCmd : public EditCommand {
public:
    InsertClipCmd(int trackIndex, int clipIndex, SequencerClip clip);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Insert Clip"; }
private:
    int           m_trackIndex;
    int           m_clipIndex;
    SequencerClip m_clip;
};

// ── RemoveClipCmd ────────────────────────────────────────────────────────────
class RemoveClipCmd : public EditCommand {
public:
    RemoveClipCmd(int trackIndex, int clipIndex);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Remove Clip"; }
private:
    int           m_trackIndex;
    int           m_clipIndex;
    SequencerClip m_saved;   // captured on redo
};

// ── MoveClipCmd ──────────────────────────────────────────────────────────────
// Reorder a clip within a single track.
class MoveClipCmd : public EditCommand {
public:
    MoveClipCmd(int trackIndex, int fromIndex, int toIndex);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Move Clip"; }
private:
    int m_trackIndex;
    int m_fromIndex;
    int m_toIndex;
};

// ── MoveClipAcrossTracksCmd ──────────────────────────────────────────────────
// Move a clip from (fromTrack, fromClipIdx) to (toTrack, toClipIdx).  Used
// when the user drags a clip vertically across track rows.  Implemented as
// one atomic command so undo restores both sides in one step.
class MoveClipAcrossTracksCmd : public EditCommand {
public:
    MoveClipAcrossTracksCmd(int fromTrack, int fromClipIdx,
                            int toTrack,   int toClipIdx);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Move Clip Across Tracks"; }
private:
    int           m_fromTrack;
    int           m_fromClipIdx;
    int           m_toTrack;
    int           m_toClipIdx;
    SequencerClip m_saved;
};

// ── TrimClipCmd ──────────────────────────────────────────────────────────────
// Adjust a clip's sourceInTicks / sourceOutTicks.  Clamped to
// [0, sourceDurationTicks] and in < out.  Downstream clips on the same
// track get repacked so the timeline stays contiguous.
class TrimClipCmd : public EditCommand {
public:
    TrimClipCmd(int trackIndex, int clipIndex, Tick newInTicks, Tick newOutTicks);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Trim Clip"; }
private:
    int  m_trackIndex;
    int  m_clipIndex;
    Tick m_newIn;
    Tick m_newOut;
    Tick m_oldIn  { 0 };
    Tick m_oldOut { 0 };
};

// ── SplitClipCmd ─────────────────────────────────────────────────────────────
// Cut the clip at clipIndex into two clips at the given timeline tick.  The
// tick must fall strictly inside the clip's timeline range; otherwise redo
// returns false and the command is rejected.
class SplitClipCmd : public EditCommand {
public:
    SplitClipCmd(int trackIndex, int clipIndex, Tick splitTimelineTick);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Split Clip"; }
private:
    int  m_trackIndex;
    int  m_clipIndex;
    Tick m_splitTimelineTick;
    // Captured on first redo for undo:
    Tick m_originalSourceOut { 0 };
};

// ── ChangeClipPropertyCmd ────────────────────────────────────────────────────
// Edit a clip's render-time properties (opacity, blendMode, fadeInTicks,
// fadeOutTicks) as a batch.  The clip itself is identified by (track,
// index).  All four fields are passed through because a single UI edit
// often nudges just one of them; undo/redo swap the whole four-field tuple
// atomically so partial edits don't accumulate if the user wiggles a knob
// and then hits Ctrl+Z.
//
// This command does NOT call repackContiguous (nothing changes about clip
// duration or timeline position) — safe to apply repeatedly.
class ChangeClipPropertyCmd : public EditCommand {
public:
    ChangeClipPropertyCmd(int trackIndex, int clipIndex,
                          float newOpacity, BlendMode newBlend,
                          Tick newFadeIn, Tick newFadeOut);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Change Clip Properties"; }
private:
    int       m_trackIndex;
    int       m_clipIndex;
    float     m_newOpacity;
    BlendMode m_newBlend;
    Tick      m_newFadeIn;
    Tick      m_newFadeOut;
    // Captured on first redo:
    float     m_oldOpacity  { 1.0f };
    BlendMode m_oldBlend    { BlendMode::Normal };
    Tick      m_oldFadeIn   { 0 };
    Tick      m_oldFadeOut  { 0 };
};

// ── ChangeClipEffectsCmd ─────────────────────────────────────────────────────
// Replace a clip's ordered effects list with a new one.  Used by the
// Effects Rack drag-drop flow (adds one effect to the tail) and by the
// clip properties panel's effects remove button.  The full new-list is
// passed through so undo restores an exact snapshot even if the user
// chains several drops before hitting Ctrl+Z.
class ChangeClipEffectsCmd : public EditCommand {
public:
    ChangeClipEffectsCmd(int trackIndex, int clipIndex,
                         QVector<ClipEffect> newEffects);
    bool redo(SequencerProject& project) override;
    void undo(SequencerProject& project) override;
    QString label() const override { return "Change Clip Effects"; }
private:
    int                 m_trackIndex;
    int                 m_clipIndex;
    QVector<ClipEffect> m_new;
    QVector<ClipEffect> m_old;   // captured on first redo
};

} // namespace sequencer
