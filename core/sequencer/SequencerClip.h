#pragma once

// =============================================================================
// SequencerClip — one trimmed range of a source video placed on one track.
//
// A clip does not own its source file — it references a path on disk.  The
// source is expected to live in the project's MoshVideoFolder/ directory
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
//
// opacity / blendMode / fadeIn / fadeOut are used by the offline renderer
// AND by FrameRouter's LayerComposite preview mode.  LiveVJ mode (single-
// active-track routing with hotkey-triggered transitions) still ignores
// them — there's nothing to composite when only one track is emitted at a
// time.  Per-clip effects (see `effects` below) are the exception: those
// run in every path, including LiveVJ.  The effective per-pixel coverage
// of a clip at a given tick is:
//     opacity * min(fadeInProgress(t), fadeOutProgress(t))
// where each fade progress is in [0, 1] and linearly interpolates across
// the fade ticks.  fadeIn and fadeOut are measured from the clip's own
// trim boundaries, not the timeline.
// =============================================================================

#include "core/sequencer/Tick.h"
#include <QString>
#include <QVector>
#include <algorithm>

extern "C" {
#include <libavutil/rational.h>
}

namespace sequencer {

// ── Layer-compositor blend modes (offline render only) ────────────────────
// "Normal" is straight alpha-over compositing (src * a + dst * (1-a)).
// Other modes operate on 8-bit RGB channels independently; see
// core/sequencer/BlendModes.h for the math.  Enum values are stable ints
// so they can be serialised as numbers in project.json.
enum class BlendMode : int {
    Normal   = 0,
    Multiply = 1,
    Screen   = 2,
    Add      = 3,
    Overlay  = 4,
};

// ── Per-clip image effects ────────────────────────────────────────────────
// Effects are applied to each decoded layer frame BEFORE the opacity/blend
// composite step.  Applied in the order stored in SequencerClip::effects, so
// a later effect sees the result of the earlier ones.  Serialised as string
// ids (see core/sequencer/ClipEffects.h) rather than ints so adding/removing
// effect kinds stays forward-compatible.
enum class ClipEffect {
    MirrorLeft,   // take left half, flip horizontally, paste to right half
};

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

    // ── Layer compositor properties (render-time only) ───────────────────
    // opacity in [0, 1].  blendMode applied per pixel.  fadeInTicks /
    // fadeOutTicks are master ticks measured inward from the clip's trim
    // edges: a 1-second fade-in on a 30fps project ≈ 30 ticks (fps-agnostic
    // below — the renderer uses the output frame-rate to compute progress).
    // All four default to "no effect" so existing clips render identically.
    float       opacity      { 1.0f };
    BlendMode   blendMode    { BlendMode::Normal };
    Tick        fadeInTicks  { 0 };
    Tick        fadeOutTicks { 0 };

    // Ordered list of image effects applied to each decoded layer frame
    // before the composite step.  Empty by default so existing clips and
    // older project files read identically.  Duplicates are allowed (the
    // compositor applies them in order), though the UI adds one at a time.
    QVector<ClipEffect> effects;

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

    // Fade-in/out progress envelope at a given timeline tick, clamped to
    // [0, 1].  Returns 1.0 outside the fade regions and for clips with no
    // fades.  When fade-in and fade-out ranges would meet in the middle of
    // a very short clip, the minimum wins (clip never reaches full opacity
    // but does not overshoot either).
    float fadeEnvelope(Tick t) const {
        if (!containsTimelineTick(t)) return 0.0f;
        float env = 1.0f;
        if (fadeInTicks > 0) {
            const Tick into = t - timelineStartTicks;
            if (into < fadeInTicks)
                env = std::min(env, float(into + 1) / float(fadeInTicks));
        }
        if (fadeOutTicks > 0) {
            const Tick toEnd = timelineEndTicks() - 1 - t;
            if (toEnd < fadeOutTicks)
                env = std::min(env, float(toEnd + 1) / float(fadeOutTicks));
        }
        return env < 0.0f ? 0.0f : (env > 1.0f ? 1.0f : env);
    }

    // Effective [0, 1] coverage at a timeline tick — what the compositor
    // actually uses for blending.  Combines base opacity with the fade
    // envelope.
    float effectiveOpacity(Tick t) const {
        return opacity * fadeEnvelope(t);
    }
};

} // namespace sequencer
