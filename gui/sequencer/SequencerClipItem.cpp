#include "gui/sequencer/SequencerClipItem.h"
#include "gui/sequencer/SequencerTimelineConstants.h"
#include "gui/sequencer/SequencerTimelineView.h"

#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QFontMetrics>

namespace sequencer {

static constexpr int kEdgeHitWidth = 6;   // px band on each edge for trim handles

SequencerClipItem::SequencerClipItem(SequencerTimelineView* view,
                                     int trackIndex, int clipIndex,
                                     const QString& displayName,
                                     Tick timelineStart, Tick duration)
    : m_view(view)
    , m_trackIndex(trackIndex)
    , m_clipIndex(clipIndex)
    , m_displayName(displayName)
    , m_timelineStart(timelineStart)
    , m_duration(duration)
{
    setFlag(ItemIsSelectable, true);
    setAcceptHoverEvents(true);
    setCursor(Qt::OpenHandCursor);
    // Position is set by the view via setPos() using scene coords; the item's
    // local origin (0,0) is at the top-left of the clip rect.
    setPos(tickToSceneX(timelineStart), trackTopY(trackIndex));
}

void SequencerClipItem::updateLayout(int trackIndex, int clipIndex,
                                     Tick timelineStart, Tick duration)
{
    prepareGeometryChange();
    m_trackIndex    = trackIndex;
    m_clipIndex     = clipIndex;
    m_timelineStart = timelineStart;
    m_duration      = duration;
    setPos(tickToSceneX(timelineStart), trackTopY(trackIndex));
    update();
}

QRectF SequencerClipItem::boundingRect() const
{
    return QRectF(0.0, 0.0, tickToSceneX(m_duration), kTrackHeight);
}

void SequencerClipItem::paint(QPainter* p,
                              const QStyleOptionGraphicsItem* /*opt*/,
                              QWidget* /*w*/)
{
    const QRectF r = boundingRect();
    if (r.width() < 1.0) return;

    // Rounded-rect body with a slightly darker border when selected.
    const bool selected = isSelected();
    QColor fill     = selected ? QColor(0x3f, 0x7a, 0xff) : QColor(0x2d, 0x60, 0xc0);
    QColor border   = selected ? QColor(0xff, 0xff, 0xff) : QColor(0x18, 0x38, 0x78);
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setBrush(fill);
    p->setPen(QPen(border, 1.2));
    p->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);

    // Trim-handle indicators — subtle tick marks inside the edge zones.
    if (r.width() > 2 * kEdgeHitWidth + 4) {
        p->setPen(QPen(QColor(0xff, 0xff, 0xff, 70), 1));
        p->drawLine(QPointF(kEdgeHitWidth, 4),
                    QPointF(kEdgeHitWidth, r.height() - 4));
        p->drawLine(QPointF(r.width() - kEdgeHitWidth, 4),
                    QPointF(r.width() - kEdgeHitWidth, r.height() - 4));
    }

    // Name label — elided to fit.
    p->setPen(QColor(0xff, 0xff, 0xff));
    QFont f = p->font();
    f.setPointSize(9);
    p->setFont(f);
    const QFontMetrics fm(f);
    const QString shown = fm.elidedText(m_displayName,
                                        Qt::ElideMiddle,
                                        static_cast<int>(r.width()) - 2 * kEdgeHitWidth - 4);
    p->drawText(QPointF(kEdgeHitWidth + 2, r.height() / 2 + fm.ascent() / 2 - 2),
                shown);
}

SequencerClipItem::DragMode SequencerClipItem::classifyPos(const QPointF& itemPos) const
{
    const double w = tickToSceneX(m_duration);
    if (w <= 0) return None;
    if (itemPos.x() < kEdgeHitWidth)        return TrimLeft;
    if (itemPos.x() > w - kEdgeHitWidth)    return TrimRight;
    return Move;
}

void SequencerClipItem::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { QGraphicsItem::mousePressEvent(e); return; }
    setSelected(true);
    m_dragMode        = classifyPos(e->pos());
    m_dragStartItem   = e->pos();
    m_dragStartScene  = e->scenePos();
    m_dragStartTickStart = m_timelineStart;
    m_dragStartDuration  = m_duration;
    if (m_view) m_view->clipItemPressed(this);
    setCursor(m_dragMode == Move ? Qt::ClosedHandCursor
              : (m_dragMode == TrimLeft || m_dragMode == TrimRight
                     ? Qt::SizeHorCursor
                     : Qt::OpenHandCursor));
    e->accept();
}

void SequencerClipItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_dragMode == None) { QGraphicsItem::mouseMoveEvent(e); return; }

    const double dxScene = e->scenePos().x() - m_dragStartScene.x();
    const double dyScene = e->scenePos().y() - m_dragStartScene.y();
    const Tick   dxTicks = sceneXToTick(std::abs(dxScene))
                           * (dxScene < 0 ? -1 : 1);

    if (m_view) {
        m_view->clipItemDragged(this, m_dragMode, dxTicks, dyScene);
    }
    e->accept();
}

void SequencerClipItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_dragMode != None) {
        const double dxScene = e->scenePos().x() - m_dragStartScene.x();
        const double dyScene = e->scenePos().y() - m_dragStartScene.y();
        const Tick   dxTicks = sceneXToTick(std::abs(dxScene))
                               * (dxScene < 0 ? -1 : 1);
        if (m_view) m_view->clipItemReleased(this, m_dragMode, dxTicks, dyScene);
    }
    m_dragMode = None;
    setCursor(Qt::OpenHandCursor);
    QGraphicsItem::mouseReleaseEvent(e);
}

} // namespace sequencer
