#pragma once

// =============================================================================
// SequencerTimelineView — main QGraphicsView for the NLE timeline.
//
// Owns the QGraphicsScene, the ruler, the playhead, and a collection of
// SequencerClipItems (one per clip in the project).  Keeps the scene in
// sync with the project model by listening to SequencerProject signals and
// rebuilding as needed.
//
// Responsibilities:
//   - Render track backgrounds (alternating shades per track row).
//   - Host SequencerClipItems.
//   - Handle drops from MediaBin (text/uri-list) → AppendClip/InsertClip.
//   - Handle clip-item drag callbacks (move / trim) → edit commands.
//   - Handle ruler-click / playhead-drag → seek on the playback clock.
//   - Handle Delete key → remove selected clip.
//   - Handle "S" key → split at playhead.
// =============================================================================

#include "core/sequencer/Tick.h"
#include "gui/sequencer/SequencerClipItem.h"
#include <QGraphicsView>
#include <QHash>

class QGraphicsScene;

namespace sequencer {

class SequencerProject;
class SequencerPlaybackClock;
class SequencerRulerItem;
class SequencerPlayheadItem;

class SequencerTimelineView : public QGraphicsView {
    Q_OBJECT
public:
    SequencerTimelineView(SequencerProject* project,
                          SequencerPlaybackClock* clock,
                          QWidget* parent = nullptr);
    ~SequencerTimelineView() override;

    // Called by SequencerClipItem during drag/drop interactions.
    void clipItemPressed (SequencerClipItem* item);
    void clipItemDragged (SequencerClipItem* item,
                          SequencerClipItem::DragMode mode,
                          Tick deltaTicks, double deltaYScene);
    void clipItemReleased(SequencerClipItem* item,
                          SequencerClipItem::DragMode mode,
                          Tick deltaTicks, double deltaYScene);

    // Called by ruler / playhead items on user seek.
    void seekToSceneX(double x);

    // Zoom setter.  Clamped to [kMinZoomX, kMaxZoomX]; triggers reposition
    // of all items.  Returns the applied value.
    double setZoomX(double newZoom);
    double zoomX() const;

    // Forward loop region info to the ruler for visual brackets.
    void setLoopRegion(Tick inTicks, Tick outTicks, bool enabled);

signals:
    // Emitted when the selected clip index changes (-1 = none).  Dock may
    // use this later to show clip properties.
    void selectedClipChanged(int trackIndex, int clipIndex);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent (QDragMoveEvent* e) override;
    void dropEvent     (QDropEvent* e) override;
    void keyPressEvent (QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void wheelEvent    (QWheelEvent* e) override;

private slots:
    void onProjectChanged();
    void onTickAdvanced(::sequencer::Tick tick);

private:
    // Fully rebuild SequencerClipItems from the project state.  Called on
    // any structural change — Phase 2 is simple; Phase 3 can do incremental
    // patching if profiling demands it.
    void rebuildClipItems();

    // Recompute scene rect + playhead height from current project state.
    void refreshSceneExtent();

    // Update every existing clip item's geometry without rebuilding the list.
    // Used on zoom changes where structure hasn't changed.
    void repositionAllItems();

    // Probe a dropped file into a SequencerClip (path + timebase + duration).
    // Returns true on success.  Duplicates SequencerDock::probeFile so the
    // view doesn't need to reach back into the dock.
    bool probeFile(const QString& path, struct SequencerClip& outClip) const;

    SequencerProject*       m_project { nullptr };
    SequencerPlaybackClock* m_clock   { nullptr };

    QGraphicsScene*        m_scene     { nullptr };
    SequencerRulerItem*    m_ruler     { nullptr };
    SequencerPlayheadItem* m_playhead  { nullptr };

    // Clip items indexed by (track, clip) — cleared/rebuilt on projectChanged.
    QList<SequencerClipItem*> m_clipItems;

    // During a drag, indicates the target track (row under cursor) so we
    // can draw a drop indicator.  -1 when no drag in progress.
    int m_dropHoverTrack { -1 };
};

} // namespace sequencer
