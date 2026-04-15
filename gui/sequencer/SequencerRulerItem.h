#pragma once

// =============================================================================
// SequencerRulerItem — time axis drawn along the top of the timeline scene.
//
// Paints across the entire scene width at y=0 with height kRulerHeight.
// Tick spacing adapts to zoom: at 1x a major tick every 1 second, at higher
// zooms every 500ms / 100ms / 1 frame.  Minor ticks fill in between.
//
// Click / drag on the ruler performs a seek via the view (which talks to
// the playback clock).
// =============================================================================

#include "core/sequencer/Tick.h"
#include <QGraphicsItem>

namespace sequencer {

class SequencerTimelineView;

class SequencerRulerItem : public QGraphicsItem {
public:
    explicit SequencerRulerItem(SequencerTimelineView* view);

    // Scene-width extent — updated whenever the project duration grows.
    void setSceneWidth(double sceneWidth);

    // Draw a bracket pair at these ticks when enabled is true.
    void setLoopRegion(Tick inTicks, Tick outTicks, bool enabled);

    QRectF boundingRect() const override;
    void   paint(QPainter* p, const QStyleOptionGraphicsItem* opt,
                 QWidget* w) override;

protected:
    void mousePressEvent (QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent  (QGraphicsSceneMouseEvent* e) override;

private:
    SequencerTimelineView* m_view      { nullptr };
    double                 m_sceneWidth{ 0.0 };
    Tick                   m_loopIn    { 0 };
    Tick                   m_loopOut   { 0 };
    bool                   m_loopOn    { false };
};

} // namespace sequencer
