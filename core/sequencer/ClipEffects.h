#pragma once

// =============================================================================
// ClipEffects — per-clip image transforms applied to each decoded layer frame
// before the compositor's blend step (SequencerRenderer + FrameRouter's
// LayerComposite path).  Keeping this separate from BlendModes.cpp because
// blend kernels are strict dst-plus-src kernels, whereas effects mutate a
// single image in-place.
//
// Registry pattern: each ClipEffect enum value has a stable string id (used
// for drag-drop MIME + project.json serialisation) and a human-readable
// display name (used in the Effects Rack list and the selected-clip
// properties panel).  Keep id strings stable forever — renaming one breaks
// projects saved with the old id.
// =============================================================================

#include "core/sequencer/SequencerClip.h"

#include <QImage>
#include <QString>
#include <QVector>
#include <optional>

namespace sequencer {

// Apply every effect in `effects` to `frame` in-place, in order.  No-op for
// an empty list.  Caller guarantees `frame` is detached (applying to a
// decoder-cached image would corrupt the cache for subsequent ticks).
void applyClipEffects(QImage& frame, const QVector<ClipEffect>& effects);

// Stable id for project.json + drag MIME data.  Do NOT rename returned
// strings — they persist in user project files.
QString           clipEffectId(ClipEffect effect);
std::optional<ClipEffect> clipEffectFromId(const QString& id);

// Human-readable label shown in the Effects Rack list and the clip
// properties panel's applied-effects list.
QString           clipEffectDisplayName(ClipEffect effect);

// Every effect the UI knows how to offer.  Returned in presentation order
// (matches the order new effects are listed in the Effects Rack).
QVector<ClipEffect> availableClipEffects();

// Drag-drop MIME type for effect items dragged out of the Effects Rack.
// Payload: effect id string encoded as UTF-8 bytes.
inline constexpr const char* kClipEffectMimeType = "application/x-lamosh-clip-effect";

} // namespace sequencer
