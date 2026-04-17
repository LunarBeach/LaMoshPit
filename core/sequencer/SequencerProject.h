#pragma once

// =============================================================================
// SequencerProject — the NLE sequencer's root model (distinct from the
// application-level Project class, which owns the project folder on disk).
//
// A SequencerProject owns:
//   - A list of tracks (1..9; enforced cap).
//   - The currently active track index (which track feeds the router output).
//   - Master loop region — loopInTicks / loopOutTicks, honored by playback.
//   - Output frame rate (used by the compositor to decide sampling cadence;
//     decoupled from any individual clip's native fps).
//   - The undo/redo command stack.
//
// All mutations go through executeCommand(std::unique_ptr<EditCommand>).
// This is the single choke-point so undo/redo works uniformly regardless of
// where the edit came from (UI drag, hotkey, scripted, etc.).
//
// Signals are emitted after each command so the UI, compositor, and router
// can react.  Keep them coarse for the v1 — fine-grained diffs can come
// later if repaint perf becomes an issue.
// =============================================================================

#include "core/sequencer/SequencerTrack.h"
#include <QObject>
#include <QList>
#include <QJsonObject>
#include <QReadWriteLock>
#include <memory>

class Project;   // core/project/Project.h — for token expansion only.

namespace sequencer {

class EditCommand;  // defined in EditCommand.h — forward to keep include cost low

class SequencerProject : public QObject {
    Q_OBJECT
public:
    explicit SequencerProject(QObject* parent = nullptr);
    ~SequencerProject() override;

    // ── Limits ───────────────────────────────────────────────────────────
    static constexpr int MaxTracks = 9;   // Matches number-row hotkeys 1-9.

    // ── Track access ─────────────────────────────────────────────────────
    int                   trackCount() const { return m_tracks.size(); }
    const SequencerTrack& track(int index) const;
    SequencerTrack&       trackMutable(int index);   // for EditCommand use
    const QList<SequencerTrack>& tracks() const { return m_tracks; }
    QList<SequencerTrack>&       tracksMutable() { return m_tracks; }

    // ── Active track (router read pointer) ───────────────────────────────
    int  activeTrackIndex() const { return m_activeTrackIndex; }
    void setActiveTrackIndex(int idx);

    // ── Loop region ──────────────────────────────────────────────────────
    Tick loopInTicks()  const { return m_loopInTicks;  }
    Tick loopOutTicks() const { return m_loopOutTicks; }
    bool loopEnabled()  const { return m_loopEnabled;  }
    void setLoopInTicks(Tick t);
    void setLoopOutTicks(Tick t);
    void setLoopEnabled(bool on);

    // ── Output rate (compositor sampling cadence) ────────────────────────
    AVRational outputFrameRate() const { return m_outputFrameRate; }
    void       setOutputFrameRate(AVRational r);

    // ── Longest track duration across all tracks (for ruler / playback end) ─
    Tick totalDurationTicks() const;

    // ── Edit pipeline ────────────────────────────────────────────────────
    // Takes ownership, calls redo() on the command, pushes onto undo stack,
    // clears the redo stack.  Returns true if the command succeeded.
    bool executeCommand(std::unique_ptr<EditCommand> cmd);

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    void undo();
    void redo();

    // Bound the undo stack.  On every executeCommand, if the stack would
    // exceed this cap, the oldest command is dropped.  Defaults to 50;
    // MainWindow pushes SettingsDialog::maxUndoStepsSequencer() in after
    // construction and on Settings-dialog accept.  The sequencer keeps a
    // separate cap from the MB editor because the two workspaces are
    // independent — different work cadence, different memory footprints.
    void setMaxUndoSteps(int n);
    int  maxUndoSteps() const { return m_maxUndoSteps; }

    // ── Persistence ──────────────────────────────────────────────────────
    // Serialize / deserialize the tracks, clips, loop region, output rate,
    // and active track index.  Clip source paths are tokenized via the
    // passed-in Project (absolute paths under moshVideoFolder() are stored
    // as "{MoshVideoFolder}/..." so the project survives a drive-letter
    // change or a vault relocation).
    //
    // fromJson replaces all current state.  It also clears the undo/redo
    // stacks (history from the previous session can't meaningfully apply
    // to freshly-loaded state).  Returns false only on a structurally-
    // malformed object; missing / partial fields fall back to defaults.
    QJsonObject toJson(const ::Project& proj) const;
    bool        fromJson(const QJsonObject& obj, const ::Project& proj);

    // Reset to the freshly-constructed empty state.  Called on project
    // switch (new/open) so the dock doesn't carry the previous project's
    // tracks and clips into the new session.
    void clear();

    // ── Thread-safety ────────────────────────────────────────────────────
    // The compositor runs on a worker thread and reads track/clip state
    // concurrently with GUI-thread edits.  Readers take `QReadLocker lk(&
    // project->stateLock())`; writers take `QWriteLocker`.  All public
    // mutators (executeCommand / undo / redo / setActiveTrackIndex /
    // setOutputFrameRate / setLoopXxx / clear / fromJson) acquire the
    // write lock internally — callers don't need to lock for those.
    // Readers MUST hold the read lock for the duration of their access,
    // because m_tracks / clip fields can change between calls.
    QReadWriteLock& stateLock() const { return m_stateLock; }

signals:
    void projectChanged();        // any structural change (tracks / clips)
    void activeTrackChanged(int newIndex);
    void loopChanged();
    void outputFrameRateChanged();

private:
    QList<SequencerTrack> m_tracks;
    int  m_activeTrackIndex { 0 };

    Tick m_loopInTicks  { 0 };
    Tick m_loopOutTicks { 0 };
    bool m_loopEnabled  { false };

    AVRational m_outputFrameRate { 30, 1 };

    // Undo / redo stacks — std::vector (not QList) because we move
    // unique_ptr values; Qt containers' value semantics fight std::move in
    // older Qt versions and there's no benefit to implicit sharing here.
    std::vector<std::unique_ptr<EditCommand>> m_undo;
    std::vector<std::unique_ptr<EditCommand>> m_redo;

    // Cap on m_undo.size().  Capping only the undo stack is sufficient:
    // m_redo is cleared on every new execute, and bounded by how many
    // times the user can undo (which is ≤ m_maxUndoSteps).
    int m_maxUndoSteps { 50 };

    // Read-write lock guarding m_tracks + all scalar state fields.  Mutable
    // so stateLock() stays const — logically the lock is not part of the
    // project's observable state.
    mutable QReadWriteLock m_stateLock;
};

} // namespace sequencer
