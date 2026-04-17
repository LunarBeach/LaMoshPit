#include "core/sequencer/ClipEffects.h"

#include <QImage>
#include <cstdint>

namespace sequencer {
namespace {

// Mirror Left: left half of the frame is flipped horizontally and written
// over the right half.  The left half itself is untouched; for odd widths
// the centre column is untouched.  Works per-row in BGRA8 (QImage's
// Format_ARGB32 in memory) so the compositor's existing stride assumptions
// still hold.
void applyMirrorLeft(QImage& frame)
{
    if (frame.isNull()) return;

    // Compositor path feeds Format_ARGB32 layer images, but we guard
    // defensively: converting here costs one extra copy only on an
    // unexpected format (never hit in the render/live paths today).
    if (frame.format() != QImage::Format_ARGB32
        && frame.format() != QImage::Format_ARGB32_Premultiplied) {
        frame = frame.convertToFormat(QImage::Format_ARGB32);
    }

    const int w = frame.width();
    const int h = frame.height();
    const int mid = w / 2;
    if (mid <= 0) return;

    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(frame.scanLine(y));
        for (int x = 0; x < mid; ++x) {
            row[w - 1 - x] = row[x];
        }
    }
}

} // namespace

void applyClipEffects(QImage& frame, const QVector<ClipEffect>& effects)
{
    if (effects.isEmpty() || frame.isNull()) return;

    for (ClipEffect e : effects) {
        switch (e) {
        case ClipEffect::MirrorLeft: applyMirrorLeft(frame); break;
        }
    }
}

QString clipEffectId(ClipEffect effect)
{
    switch (effect) {
    case ClipEffect::MirrorLeft: return QStringLiteral("mirrorLeft");
    }
    return {};
}

std::optional<ClipEffect> clipEffectFromId(const QString& id)
{
    if (id == QStringLiteral("mirrorLeft")) return ClipEffect::MirrorLeft;
    return std::nullopt;
}

QString clipEffectDisplayName(ClipEffect effect)
{
    switch (effect) {
    case ClipEffect::MirrorLeft: return QStringLiteral("Mirror Left");
    }
    return {};
}

QVector<ClipEffect> availableClipEffects()
{
    return { ClipEffect::MirrorLeft };
}

} // namespace sequencer
