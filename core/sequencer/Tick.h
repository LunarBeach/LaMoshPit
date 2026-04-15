#pragma once

// =============================================================================
// Tick — master timebase for the NLE sequencer.
//
// One Tick = 1 / SequencerTickRate of a second, with SequencerTickRate = 90000.
// This matches MPEG PTS units and exactly represents all common framerates
// (23.976, 24, 25, 29.97, 30, 50, 59.94, 60) with no drift over long
// sequences — the 1001-denominator NTSC rates would drift if we used plain
// microseconds.
//
// All timeline positions, clip bounds, loop markers, and playhead values in
// the sequencer are stored as Tick (int64_t) relative to this master timebase.
// Conversions to/from FFmpeg stream timebases go through AVRational math via
// av_rescale_q_rnd so there is no floating-point loss at any boundary.
//
// This file is header-only — no .cpp needed.  All helpers are tiny and
// benefit from inlining at call sites (per-frame rescales in the decode path).
// =============================================================================

#include <cstdint>

extern "C" {
#include <libavutil/rational.h>
#include <libavutil/mathematics.h>
}

namespace sequencer {

using Tick = int64_t;

inline constexpr int SequencerTickRate = 90000;

inline AVRational masterTimeBase() {
    return AVRational{1, SequencerTickRate};
}

// Convert a duration in seconds (double) to master ticks.  Used for things
// like loop lead-in time where a sub-second human-readable value is handy.
// Rounds to nearest tick.
inline Tick secondsToTicks(double seconds) {
    return static_cast<Tick>(seconds * SequencerTickRate + 0.5);
}

inline double ticksToSeconds(Tick t) {
    return static_cast<double>(t) / SequencerTickRate;
}

// Map a master-tick value into a source stream's timebase, using rescale-round
// semantics so boundary frames land on the correct side consistently.  The
// NEAR_INF rounding mode matches FFmpeg's own internal behavior for timestamps.
inline int64_t ticksToStreamTs(Tick ticks, AVRational streamTimeBase) {
    return av_rescale_q_rnd(ticks, masterTimeBase(), streamTimeBase,
                            static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
}

inline Tick streamTsToTicks(int64_t streamTs, AVRational streamTimeBase) {
    return av_rescale_q_rnd(streamTs, streamTimeBase, masterTimeBase(),
                            static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
}

// Tick span for one frame at a given frame rate.  Example: 30 fps → 3000
// ticks per frame; 23.976 fps (AVRational{24000,1001}) → 3753.75 rounded to
// 3754 ticks, which is correct (average; over 1001 frames the cumulative
// tick count is exactly 3,756,753, matching 1001/24000 * 90000).
inline Tick frameDurationTicks(AVRational frameRate) {
    if (frameRate.num <= 0 || frameRate.den <= 0) return 0;
    return av_rescale_q(1, AVRational{frameRate.den, frameRate.num}, masterTimeBase());
}

} // namespace sequencer
