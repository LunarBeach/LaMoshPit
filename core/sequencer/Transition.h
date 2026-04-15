#pragma once

// =============================================================================
// Transition — pluggable track-switch effect system.
//
// When the user presses a hotkey to switch tracks in live mode, the
// FrameRouter triggers the currently-selected transition instead of
// instantly swapping which track feeds the output.  A transition takes
// two QImages (outgoing, incoming) plus a progress value in [0, 1] and
// returns the composited output frame.
//
// The base Transition is a virtual interface; concrete subclasses
// implement name(), default parameter UI hints, and the compose() kernel.
// Global-per-session policy: one transition is "active" at a time; the
// user switches types via the SequencerTransitionPanel UI.  All transitions
// share a common TransitionParams dictionary (duration, MB size, curve,
// etc.) — each subclass reads whichever params are meaningful to it and
// ignores the rest.
//
// Adding a new transition type requires:
//   1. Subclass Transition, implement name() + compose().
//   2. Register it in Transition::availableTypes() (in Transition.cpp).
//   3. Optional: add a default param block for the UI panel.
// =============================================================================

#include "core/sequencer/Tick.h"
#include <QImage>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <vector>

namespace sequencer {

// Shared parameter map — all transition types read from the same dict.
// Keys (canonical):
//   "duration_sec"   double    length of the transition in seconds
//   "mb_size"        int       macroblock size (pixels), used by MBRandom
//   "curve"          QString   "linear" | "ease_in" | "ease_out" | "smooth"
//   "seed"           uint32    RNG seed for MBRandom (0 = randomize each run)
using TransitionParams = QVariantMap;

class Transition {
public:
    virtual ~Transition() = default;

    // Unique type id — used by serialization + type dropdown.  Must be
    // stable across versions.
    virtual QString typeId() const = 0;

    // Human-readable display name for the UI dropdown.
    virtual QString displayName() const = 0;

    // Compose the output frame at the given progress in [0, 1].  The two
    // input frames are guaranteed same-size and same-format (BGRA).
    // Subclasses may assume that — conversion happens upstream.
    virtual QImage compose(const QImage& outgoing, const QImage& incoming,
                           double progress, const TransitionParams& params) = 0;

    // Called once when the router enters the transition state.  Default is
    // a no-op; MBRandom uses it to generate the permutation.
    virtual void start(int fromTrack, int toTrack,
                       const TransitionParams& params) {
        (void)fromTrack; (void)toTrack; (void)params;
    }

    // Factory — create a transition instance by typeId.  Returns nullptr
    // if the id is unknown.
    static std::unique_ptr<Transition> create(const QString& typeId);

    // List all registered transitions (typeId, displayName) pairs for UI.
    static std::vector<std::pair<QString, QString>> availableTypes();

    // Apply one of the canonical easing curves to a linear t in [0,1].
    static double applyCurve(double t, const QString& curve);
};

} // namespace sequencer
