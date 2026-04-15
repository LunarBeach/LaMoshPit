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
#include <memory>

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
};

} // namespace sequencer
