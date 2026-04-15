#pragma once

// =============================================================================
// SequencerTrack — one row on the sequencer; holds an ordered list of clips.
//
// Tracks are the "version" unit of the VJ model: each track is an alternative
// arrangement of clips on the same timeline.  The user can have up to 9
// tracks (numbered 1-9, mapped to number-row hotkeys for live switching) and
// exactly one is "active" at any moment — the router reads its frame out.
//
// Clips on a track are kept sorted by timelineStartTicks.  Today, clips are
// laid end-to-end with no gaps (MVP constraint); gap support may come later.
// Trim/move operations should go through the EditCommand system, which
// ensures the sort order and contiguity invariants are maintained.
//
// Track itself is a plain data holder.  All mutating behavior lives in
// EditCommand subclasses (AppendClipCmd, MoveClipCmd, etc.) so there is one
// undoable path for every edit.
// =============================================================================

#include "core/sequencer/SequencerClip.h"
#include <QList>
#include <QString>

namespace sequencer {

struct SequencerTrack {
    QString                name;        // Display label ("Track 1", "Moshed V1", etc.)
    bool                   enabled { true };  // Future: mute-like disable flag.
    QList<SequencerClip>   clips;

    // Sum of all clips' trimmed durations — the full track length.  O(n).
    Tick totalDurationTicks() const {
        Tick t = 0;
        for (const auto& c : clips) t += c.trimmedDurationTicks();
        return t;
    }

    // Find the clip that contains a given timeline tick, or -1 if the tick
    // falls past the last clip.  Linear scan — fine for small clip counts;
    // if tracks grow long a binary search over timelineStartTicks is easy.
    int clipIndexAtTick(Tick t) const {
        for (int i = 0; i < clips.size(); ++i) {
            if (clips[i].containsTimelineTick(t)) return i;
        }
        return -1;
    }

    // Re-flow clip positions so they are contiguous starting at tick 0.
    // Called by edit commands after any structural change (insert / remove /
    // move / trim) to re-establish the "no gaps" invariant.
    void repackContiguous() {
        Tick cursor = 0;
        for (auto& c : clips) {
            c.timelineStartTicks = cursor;
            cursor += c.trimmedDurationTicks();
        }
    }
};

} // namespace sequencer
