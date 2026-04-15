#include "gui/sequencer/SequencerPlayheadItem.h"
#include "gui/sequencer/SequencerTimelineConstants.h"

#include <QPainter>

namespace sequencer {

SequencerPlayheadItem::SequencerPlayheadItem()
{
    setZValue(200.0);   // paint on top of everything
    setPos(0, 0);
}

void SequencerPlayheadItem::setTrackCount(int n)
{
    if (n == m_trackCount) return;
    prepareGeometryChange();
    m_trackCount = n;
    update();
}

void SequencerPlayheadItem::setTick(Tick t)
{
    m_tick = t;
    setPos(tickToSceneX(t), 0);
}

QRectF SequencerPlayheadItem::boundingRect() const
{
    const double h = kRulerHeight
                   + (m_trackCount > 0
                      ? m_trackCount * (kTrackHeight + kTrackGap)
                      : kTrackHeight);
    // Extend 3 px either side so the handle glyph isn't clipped.
    return QRectF(-3.0, 0.0, 6.0, h);
}

void SequencerPlayheadItem::paint(QPainter* p,
                                  const QStyleOptionGraphicsItem* /*opt*/,
                                  QWidget* /*w*/)
{
    const QRectF r = boundingRect();
    // Line itself — 1px red, spans full height.
    p->setPen(QPen(QColor(0xff, 0x30, 0x30), 1.0));
    p->drawLine(QPointF(0, 0), QPointF(0, r.height()));

    // Arrow handle at the ruler band so it reads as a draggable element
    // even when the scene is dark.
    p->setBrush(QColor(0xff, 0x30, 0x30));
    p->setPen(Qt::NoPen);
    QPointF tri[3] = {
        QPointF(-3.0, 0.0),
        QPointF(3.0,  0.0),
        QPointF(0.0,  8.0)
    };
    p->drawPolygon(tri, 3);
}

} // namespace sequencer
