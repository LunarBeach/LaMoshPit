#pragma once

// =============================================================================
// SequencerClip — one trimmed range of a source video placed on one track.
//
// A clip does not own its source file — it references a path on disk.  The
// source is expected to live in the project's imported_videos/ directory
// (standardized H.264 by DecodePipeline), though any ffmpeg-openable file
// will work during editing.
//
// Trim points (sourceIn / sourceOut) are in master ticks, relative to the
// START of the source file (i.e. source timestamp 0 = sourceIn of 0).
// timelineStart is the clip's absolute position on its track, also in ticks.
//
// sourceTimeBase + sourceFrameRate are cached metadata from the probe done
// at insert time; the compositor uses them to rescale master ticks into the
// stream's native timebase for seeking.  sourceDurationTicks is the full
// duration of the source file (unrelated to trim), used for clamping trims.
// =============================================================================

#include "core/sequencer/Tick.h"
#include <QString>

extern "C" {
#include <libavutil/rational.h>
}

namespace sequencer {

struct SequencerClip {
    QString     sourcePath;

    // Probed metadata — cached at insert time, immutable thereafter.
    AVRational  sourceTimeBase    { 1, 90000 };
    AVRational  sourceFrameRate   { 30, 1 };
    Tick        sourceDurationTicks { 0 };

    // Trim window into the source, in master ticks.  Invariant:
    //   0 <= sourceInTicks < sourceOutTicks <= sourceDurationTicks
    Tick        sourceInTicks  { 0 };
    Tick        sourceOutTicks { 0 };

    // Absolute position on the clip's track, in master ticks.  Maintained by
    // SequencerTrack on edit operations (insert / move / trim).
    Tick        timelineStartTicks { 0 };

    // Future hook — retime/speed is not implemented in v1.  Stored as an
    // AVRational so 1/2-speed, 2x-speed, etc. stay exact.  speed == 1/1 is
    // the identity case the compositor fast-paths today.
    AVRational  speed { 1, 1 };

    // Convenience accessors.
    Tick trimmedDurationTicks() const {
        const Tick raw = sourceOutTicks - sourceInTicks;
        return raw > 0 ? raw : 0;
    }

    Tick timelineEndTicks() const {
        return timelineStartTicks + trimmedDurationTicks();
    }

    bool containsTimelineTick(Tick t) const {
        return t >= timelineStartTicks && t < timelineEndTicks();
    }
};

} // namespace sequencer
