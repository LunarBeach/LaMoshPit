#include "gui/sequencer/SequencerRulerItem.h"
#include "gui/sequencer/SequencerTimelineConstants.h"
#include "gui/sequencer/SequencerTimelineView.h"

#include <QPainter>
#include <QGraphicsSceneMouseEvent>

namespace sequencer {

SequencerRulerItem::SequencerRulerItem(SequencerTimelineView* view)
    : m_view(view)
{
    setZValue(100.0);   // paints on top of track backgrounds
    setPos(0, 0);
}

void SequencerRulerItem::setSceneWidth(double sceneWidth)
{
    if (sceneWidth == m_sceneWidth) return;
    prepareGeometryChange();
    m_sceneWidth = sceneWidth;
    update();
}

void SequencerRulerItem::setLoopRegion(Tick inTicks, Tick outTicks, bool enabled)
{
    m_loopIn  = inTicks;
    m_loopOut = outTicks;
    m_loopOn  = enabled;
    update();
}

QRectF SequencerRulerItem::boundingRect() const
{
    return QRectF(0, 0, m_sceneWidth, kRulerHeight);
}

void SequencerRulerItem::paint(QPainter* p,
                               const QStyleOptionGraphicsItem* /*opt*/,
                               QWidget* /*w*/)
{
    const QRectF r = boundingRect();
    p->fillRect(r, QColor(0x1a, 0x1a, 0x1a));
    p->setPen(QColor(0x3a, 0x3a, 0x3a));
    p->drawLine(QPointF(0, r.height() - 0.5),
                QPointF(r.width(), r.height() - 0.5));

    // Adaptive tick spacing based on current zoom.  Goal: keep major label
    // spacing in the 60-200 scene-pixel range, which reads comfortably.
    const double pxPerSec = scenePxPerSecond();   // zoom-aware

    // Pick a "seconds between major ticks" from a canonical ladder so
    // numbers stay human-readable at any zoom.
    const double ladder[] = { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 300.0 };
    double majorSec = 1.0;
    for (double cand : ladder) {
        if (cand * pxPerSec >= 60.0) { majorSec = cand; break; }
        majorSec = cand;
    }
    const double minorSec = majorSec / 5.0;   // five minor ticks per major

    QFont f = p->font();
    f.setPointSize(8);
    p->setFont(f);

    // Minor ticks.
    p->setPen(QColor(0x60, 0x60, 0x60));
    const int nMinor = static_cast<int>(r.width() / (minorSec * pxPerSec)) + 1;
    for (int i = 0; i <= nMinor; ++i) {
        const double x = i * minorSec * pxPerSec;
        if (x > r.width()) break;
        p->drawLine(QPointF(x, r.height() - 1),
                    QPointF(x, r.height() - 4));
    }

    // Major ticks + labels.
    p->setPen(QColor(0xc0, 0xc0, 0xc0));
    const int nMajor = static_cast<int>(r.width() / (majorSec * pxPerSec)) + 1;
    for (int i = 0; i <= nMajor; ++i) {
        const double x = i * majorSec * pxPerSec;
        if (x > r.width()) break;
        p->drawLine(QPointF(x, r.height() - 1),
                    QPointF(x, r.height() - 10));
        const double sec = i * majorSec;
        QString label;
        if (majorSec >= 60.0) {
            label = QString("%1m").arg(int(sec / 60.0));
        } else if (majorSec >= 1.0) {
            label = QString("%1s").arg(sec, 0, 'f', 0);
        } else {
            label = QString("%1s").arg(sec, 0, 'f', 1);
        }
        p->drawText(QPointF(x + 3, r.height() - 12), label);
    }

    // Loop region brackets — yellow tinted overlay band + bracket glyphs
    // at in/out points when loop is armed.
    if (m_loopOn && m_loopOut > m_loopIn) {
        const double xi = tickToSceneX(m_loopIn);
        const double xo = tickToSceneX(m_loopOut);
        p->fillRect(QRectF(xi, 0, xo - xi, r.height()),
                    QColor(0xff, 0xd7, 0x40, 45));
        QPen bracketPen(QColor(0xff, 0xd7, 0x40), 1.5);
        p->setPen(bracketPen);
        // In-bracket: |-
        p->drawLine(QPointF(xi, 2), QPointF(xi, r.height() - 2));
        p->drawLine(QPointF(xi, 2), QPointF(xi + 6, 2));
        p->drawLine(QPointF(xi, r.height() - 2),
                    QPointF(xi + 6, r.height() - 2));
        // Out-bracket: -|
        p->drawLine(QPointF(xo, 2), QPointF(xo, r.height() - 2));
        p->drawLine(QPointF(xo, 2), QPointF(xo - 6, 2));
        p->drawLine(QPointF(xo, r.height() - 2),
                    QPointF(xo - 6, r.height() - 2));
    }
}

void SequencerRulerItem::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_view) {
        m_view->seekToSceneX(e->scenePos().x());
        e->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(e);
}

void SequencerRulerItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton && m_view) {
        m_view->seekToSceneX(e->scenePos().x());
        e->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(e);
}

} // namespace sequencer
