#pragma once

// =============================================================================
// SequencerTimelineConstants — shared scene-coordinate geometry for the NLE
// timeline view and its items.
//
// Scene coord system:
//   X: pixels at "1x zoom" — one second = kScenePxPerSecond scene units.
//      View transform scales X on zoom (scale(zoomX, 1.0)).
//   Y: pixels, fixed.  Ruler at top, then tracks stacked below.
//
// Keeping these as compile-time constants avoids passing geometry state
// through every item's constructor.  Values were picked so 9 tracks fit in
// a ~600px vertical dock comfortably.
// =============================================================================

#include "core/sequencer/Tick.h"

namespace sequencer {

inline constexpr double kScenePxPerSecondAt1x = 100.0;      // 1 sec = 100 scene px at 1x zoom
inline constexpr double kScenePxPerTickAt1x   =
    kScenePxPerSecondAt1x / static_cast<double>(SequencerTickRate);

inline constexpr int    kRulerHeight   = 26;
inline constexpr int    kTrackHeight   = 58;
inline constexpr int    kTrackGap      = 2;                 // visual separator between rows

inline constexpr double kMinZoomX = 0.05;                   // 20s per 100 scene px
inline constexpr double kMaxZoomX = 50.0;                   // ~50 frames per 100 scene px

// Single source of truth for the current horizontal zoom level.  The
// SequencerTimelineView owns the setter and updates it on Ctrl+wheel; all
// items pull through tickToSceneX/sceneXToTick which consult this value.
// Using a function-local static (not a namespace-global) so the header
// stays header-only without a matching .cpp.
inline double& timelineZoomXRef()
{
    static double z = 1.0;
    return z;
}
inline double timelineZoomX() { return timelineZoomXRef(); }

// Track count — pushed by refreshSceneExtent and rebuildRows so the
// flipped Y mapping (highest track index at visual top) can invert.
inline int& timelineTrackCountRef()
{
    static int n = 1;
    return n;
}
inline int timelineTrackCount() { return timelineTrackCountRef(); }

inline double scenePxPerTick()   { return kScenePxPerTickAt1x   * timelineZoomX(); }
inline double scenePxPerSecond() { return kScenePxPerSecondAt1x * timelineZoomX(); }

inline double tickToSceneX(Tick t) {
    return static_cast<double>(t) * scenePxPerTick();
}
inline Tick sceneXToTick(double x) {
    if (x < 0) x = 0;
    const double pxPerTick = scenePxPerTick();
    if (pxPerTick <= 0.0) return 0;
    return static_cast<Tick>(x / pxPerTick + 0.5);
}

// Y position (scene units) of the top edge of a given track row.
// The mapping is FLIPPED so that the highest track index (= topmost
// compositor layer) sits at the visual top of the timeline, just below the
// ruler.  Track 0 (bottom of the layer stack) ends up at the bottom of the
// timeline view.  This matches the standard NLE "look down at the stack"
// convention (After Effects, Premiere, etc.).
inline double trackTopY(int trackIndex) {
    const int n = timelineTrackCount();
    const int visualRow = (n > 0) ? (n - 1 - trackIndex) : 0;
    return kRulerHeight + visualRow * (kTrackHeight + kTrackGap);
}

// Inverse of trackTopY — returns the track index whose row contains y, or
// -1 if y is outside any track (ruler band, below last track, etc.).
inline int trackIndexAtY(double y, int numTracks) {
    if (y < kRulerHeight) return -1;
    const double relY = y - kRulerHeight;
    const int visualRow = static_cast<int>(relY / (kTrackHeight + kTrackGap));
    // Flip back: visualRow 0 = top = highest track index.
    const int idx = (numTracks - 1) - visualRow;
    if (idx < 0 || idx >= numTracks) return -1;
    return idx;
}

} // namespace sequencer
