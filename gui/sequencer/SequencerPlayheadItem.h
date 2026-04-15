#pragma once

// =============================================================================
// SequencerPlayheadItem — vertical line marking the current playhead.
//
// Spans from y=0 (ruler top) to the bottom of the last track.  Height is
// updated whenever the track count changes (via setTrackCount).  X position
// is updated from the clock's tickAdvanced signal by the view.
// =============================================================================

#include "core/sequencer/Tick.h"
#include <QGraphicsItem>

namespace sequencer {

class SequencerPlayheadItem : public QGraphicsItem {
public:
    SequencerPlayheadItem();

    void setTrackCount(int n);        // updates vertical extent
    void setTick(Tick t);             // updates X position

    QRectF boundingRect() const override;
    void   paint(QPainter* p, const QStyleOptionGraphicsItem* opt,
                 QWidget* w) override;

private:
    int  m_trackCount { 0 };
    Tick m_tick       { 0 };
};

} // namespace sequencer
