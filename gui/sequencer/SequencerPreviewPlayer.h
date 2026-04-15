#pragma once

// =============================================================================
// SequencerPreviewPlayer — Qt widget that displays the compositor's current
// output frame.
//
// Dead-simple: subclasses QWidget, overrides paintEvent.  setFrame(QImage)
// caches the frame and requests a repaint.  The painter scales the image
// to fit the widget while preserving aspect ratio (letterboxed on the
// short axis with black bars).
//
// Phase 1: CPU-side QImage path.  Phase 4 (Spout) will move to a
// QRhiWidget / D3D11 texture path so GPU sharing is zero-copy.
// =============================================================================

#include "core/sequencer/Tick.h"
#include <QWidget>
#include <QImage>

namespace sequencer {

class SequencerPreviewPlayer : public QWidget {
    Q_OBJECT
public:
    explicit SequencerPreviewPlayer(QWidget* parent = nullptr);

    // Size hint encourages a 16:9 layout; containers are free to override.
    QSize sizeHint() const override { return QSize(640, 360); }

public slots:
    // Wired directly to SequencerCompositor::frameReady.  Copies the
    // QImage (implicitly shared in Qt — cheap) and schedules a repaint.
    void onFrameReady(QImage frame, ::sequencer::Tick tick);

    // Clears the displayed frame (paints solid black).  Used when the
    // compositor has nothing to show (empty track, gap).
    void clearFrame();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QImage m_frame;
    Tick   m_lastTick { 0 };
};

} // namespace sequencer
