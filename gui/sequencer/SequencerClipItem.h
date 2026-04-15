#pragma once

// =============================================================================
// SequencerClipItem — QGraphicsItem for one clip on the timeline.
//
// Renders as a rounded rect spanning [timelineStart, timelineEnd) with the
// clip's filename drawn inside.  Hit handling:
//
//   - Click body (middle 6+ px from edges): select + drag to move within
//     track.  On release, the item signals the view which issues a
//     MoveClipCmd if the drag crossed a neighbor's midpoint.
//   - Drag left edge (outer 6 px): trim in-point.
//   - Drag right edge (outer 6 px): trim out-point.
//
// Item geometry is in scene coords — see SequencerTimelineConstants.h.
// Position is managed by the view (set via setPos), not stored internally.
// =============================================================================

#include "core/sequencer/Tick.h"
#include <QGraphicsItem>
#include <QString>

namespace sequencer {

class SequencerTimelineView;  // owner; receives drag events via callbacks

class SequencerClipItem : public QGraphicsItem {
public:
    enum DragMode { None, Move, TrimLeft, TrimRight };

    SequencerClipItem(SequencerTimelineView* view,
                      int trackIndex, int clipIndex,
                      const QString& displayName,
                      Tick timelineStart, Tick duration);

    // Update displayed geometry without mutating the project.  Called by
    // the view after a re-sync when position/duration changed due to edit
    // commands or other item moves.
    void updateLayout(int trackIndex, int clipIndex,
                      Tick timelineStart, Tick duration);

    int  trackIndex() const { return m_trackIndex; }
    int  clipIndex()  const { return m_clipIndex;  }
    Tick timelineStart() const { return m_timelineStart; }
    Tick duration()      const { return m_duration;      }

    // QGraphicsItem overrides.
    QRectF boundingRect() const override;
    void   paint(QPainter* p, const QStyleOptionGraphicsItem* opt,
                 QWidget* w) override;

protected:
    void mousePressEvent  (QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent   (QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    // Classify the point (in item coords) into a drag mode based on which
    // 6-pixel band it falls in.
    DragMode classifyPos(const QPointF& itemPos) const;

    SequencerTimelineView* m_view { nullptr };
    int     m_trackIndex    { 0 };
    int     m_clipIndex     { 0 };
    QString m_displayName;
    Tick    m_timelineStart { 0 };
    Tick    m_duration      { 0 };

    // Drag state.
    DragMode m_dragMode      { None };
    QPointF  m_dragStartItem;     // mouse point in item coords at press
    QPointF  m_dragStartScene;    // mouse point in scene coords at press
    Tick     m_dragStartTickStart { 0 };  // clip's start at press
    Tick     m_dragStartDuration  { 0 };
    Tick     m_dragStartSourceIn  { 0 };
    Tick     m_dragStartSourceOut { 0 };
};

} // namespace sequencer
