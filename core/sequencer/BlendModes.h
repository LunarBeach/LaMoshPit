#pragma once

// =============================================================================
// BlendModes — pixel compositing kernels for the offline renderer's layer
// stack.  Each kernel composites src onto dst in-place, modulated by a
// scalar coverage alpha in [0, 1].  Both images must be the same size and
// same format (BGRA8 / Format_ARGB32 on little-endian — standard throughout
// the sequencer pipeline; see Transition.h's contract).
//
// The "blend mode" determines how channels are combined before the alpha
// lerp back toward dst.  "Normal" is straight src-over-dst alpha compositing.
// Multiply, Screen, Add, and Overlay are the classic NLE blend modes,
// applied per-channel (RGB; alpha channel is kept opaque).
//
// alpha is a scalar (not per-pixel) because the layer compositor combines
// clip.opacity with clip.fadeEnvelope(t) into one number per tick per clip.
// Per-pixel alpha would need src to carry meaningful alpha data, which we
// don't today (decoded frames are always opaque).
// =============================================================================

#include "core/sequencer/SequencerClip.h"   // for BlendMode

#include <QImage>

namespace sequencer {

void applyBlend(QImage& dst, const QImage& src, float alpha, BlendMode mode);

} // namespace sequencer
