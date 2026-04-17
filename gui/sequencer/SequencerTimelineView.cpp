#include "gui/sequencer/SequencerTimelineView.h"
#include "gui/sequencer/SequencerTimelineConstants.h"
#include "gui/sequencer/SequencerRulerItem.h"
#include "gui/sequencer/SequencerPlayheadItem.h"
#include "core/sequencer/SequencerProject.h"
#include "core/sequencer/SequencerTrack.h"
#include "core/sequencer/SequencerPlaybackClock.h"
#include "core/sequencer/ClipEffects.h"
#include "core/sequencer/EditCommand.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <QGraphicsScene>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace sequencer {

// =============================================================================
// Construction
// =============================================================================

SequencerTimelineView::SequencerTimelineView(SequencerProject* project,
                                             SequencerPlaybackClock* clock,
                                             QWidget* parent)
    : QGraphicsView(parent)
    , m_project(project)
    , m_clock(clock)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing, true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(QColor(0x12, 0x12, 0x12));

    m_ruler    = new SequencerRulerItem(this);
    m_playhead = new SequencerPlayheadItem();
    m_scene->addItem(m_ruler);
    m_scene->addItem(m_playhead);

    if (m_project) {
        connect(m_project, &SequencerProject::projectChanged,
                this,      &SequencerTimelineView::onProjectChanged);
    }
    if (m_clock) {
        connect(m_clock, &SequencerPlaybackClock::tickAdvanced,
                this,    &SequencerTimelineView::onTickAdvanced);
    }

    onProjectChanged();   // initial build
}

SequencerTimelineView::~SequencerTimelineView() = default;

// =============================================================================
// Scene maintenance
// =============================================================================

void SequencerTimelineView::refreshSceneExtent()
{
    const double width = m_project
        ? tickToSceneX(std::max<Tick>(m_project->totalDurationTicks(),
                                      secondsToTicks(10)))
        : tickToSceneX(secondsToTicks(10));

    const int numTracks = m_project ? m_project->trackCount() : 0;
    // Push the track count so the flipped-Y mapping in trackTopY /
    // trackIndexAtY sees the right value this frame.
    timelineTrackCountRef() = std::max(numTracks, 1);

    const double height = kRulerHeight
                        + std::max(numTracks, 1)
                            * (kTrackHeight + kTrackGap) + kTrackGap;

    m_scene->setSceneRect(0, 0, width, height);
    m_ruler->setSceneWidth(width);
    m_playhead->setTrackCount(numTracks);
}

void SequencerTimelineView::rebuildClipItems()
{
    // Detach items from the scene immediately, but defer the actual delete
    // to the next event-loop iteration.  rebuildClipItems() is called via
    // projectChanged, which can fire from inside a clip item's own mouse
    // event (e.g. cross-track drag → MoveClipAcrossTracksCmd).  If we
    // delete now, that item's in-flight mouseReleaseEvent resumes on a
    // destroyed `this` and crashes.
    QList<QGraphicsItem*> toDelete;
    toDelete.reserve(m_clipItems.size());
    for (auto* ci : m_clipItems) {
        if (ci) {
            m_scene->removeItem(ci);
            toDelete.append(ci);
        }
    }
    m_clipItems.clear();
    if (!toDelete.isEmpty()) {
        QTimer::singleShot(0, this, [items = std::move(toDelete)]() {
            qDeleteAll(items);
        });
    }

    if (!m_project) return;
    for (int t = 0; t < m_project->trackCount(); ++t) {
        const auto& track = m_project->track(t);
        for (int c = 0; c < track.clips.size(); ++c) {
            const auto& clip = track.clips[c];
            const QString name = QFileInfo(clip.sourcePath).fileName();
            auto* item = new SequencerClipItem(this, t, c, name,
                                               clip.timelineStartTicks,
                                               clip.trimmedDurationTicks());
            m_scene->addItem(item);
            m_clipItems.append(item);
        }
    }

    // Re-apply the canonical selection after rebuild so the Clip Properties
    // panel stays in sync through project edits (e.g. ChangeClipPropertyCmd
    // emits projectChanged → rebuild, but the user's selection is still
    // conceptually the same clip).  If the index is now out of range (the
    // selected clip was removed), fall back to "no selection".
    if (m_selectedTrackIdx >= 0 && m_selectedClipIdx >= 0
        && m_selectedTrackIdx < m_project->trackCount()
        && m_selectedClipIdx < m_project->track(m_selectedTrackIdx).clips.size())
    {
        for (auto* ci : m_clipItems) {
            if (ci && ci->trackIndex() == m_selectedTrackIdx
                   && ci->clipIndex()  == m_selectedClipIdx) {
                ci->setSelected(true);
                break;
            }
        }
        emit selectedClipChanged(m_selectedTrackIdx, m_selectedClipIdx);
    } else if (m_selectedTrackIdx != -1 || m_selectedClipIdx != -1) {
        m_selectedTrackIdx = -1;
        m_selectedClipIdx  = -1;
        emit selectedClipChanged(-1, -1);
    }
}

void SequencerTimelineView::repositionAllItems()
{
    if (!m_project) return;
    int i = 0;
    for (int t = 0; t < m_project->trackCount(); ++t) {
        const auto& track = m_project->track(t);
        for (int c = 0; c < track.clips.size(); ++c) {
            if (i >= m_clipItems.size()) return;
            const auto& clip = track.clips[c];
            m_clipItems[i]->updateLayout(t, c,
                                          clip.timelineStartTicks,
                                          clip.trimmedDurationTicks());
            ++i;
        }
    }
}

double SequencerTimelineView::zoomX() const
{
    return timelineZoomX();
}

void SequencerTimelineView::setLoopRegion(Tick inTicks, Tick outTicks, bool enabled)
{
    if (m_ruler) m_ruler->setLoopRegion(inTicks, outTicks, enabled);
}

double SequencerTimelineView::setZoomX(double newZoom)
{
    newZoom = std::clamp(newZoom, kMinZoomX, kMaxZoomX);
    if (std::abs(newZoom - timelineZoomX()) < 1e-9) return timelineZoomX();
    timelineZoomXRef() = newZoom;
    refreshSceneExtent();
    repositionAllItems();
    if (m_clock) m_playhead->setTick(m_clock->currentTick());
    m_scene->update();
    return newZoom;
}

void SequencerTimelineView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        // Ctrl+wheel — zoom X axis.  Anchor zoom at cursor position by
        // remembering the scene point under the mouse before zoom and
        // scrolling after zoom so it stays under the mouse.
        const QPointF sceneAtCursorBefore = mapToScene(e->position().toPoint());
        const Tick    tickAtCursor        = sceneXToTick(sceneAtCursorBefore.x());

        const double factor = std::pow(1.0015, e->angleDelta().y());
        setZoomX(timelineZoomX() * factor);

        // Scroll so the tick that was under the cursor is under it again.
        const double newSceneX = tickToSceneX(tickAtCursor);
        const QPointF cursorVp = e->position();
        const double  deltaX   = newSceneX - sceneAtCursorBefore.x();
        // Translate via horizontal scroll bar.
        horizontalScrollBar()->setValue(
            horizontalScrollBar()->value() + static_cast<int>(deltaX));
        (void)cursorVp;
        e->accept();
        return;
    }
    QGraphicsView::wheelEvent(e);
}

void SequencerTimelineView::onProjectChanged()
{
    rebuildClipItems();
    refreshSceneExtent();
    if (m_clock) m_playhead->setTick(m_clock->currentTick());
    m_scene->update();
}

void SequencerTimelineView::onTickAdvanced(Tick tick)
{
    if (m_playhead) m_playhead->setTick(tick);
}

// =============================================================================
// Clip-item drag callbacks
// =============================================================================

void SequencerTimelineView::clipItemPressed(SequencerClipItem* item)
{
    if (!item) return;
    m_selectedTrackIdx = item->trackIndex();
    m_selectedClipIdx  = item->clipIndex();
    emit selectedClipChanged(m_selectedTrackIdx, m_selectedClipIdx);
}

void SequencerTimelineView::clipItemDragged(SequencerClipItem* item,
                                            SequencerClipItem::DragMode mode,
                                            Tick deltaTicks, double deltaYScene)
{
    if (!item) return;
    // Live visual feedback: shift or resize the item without committing a
    // command.  The real mutation happens on release.
    const Tick startBefore    = item->timelineStart();
    const Tick durationBefore = item->duration();
    switch (mode) {
        case SequencerClipItem::Move: {
            Tick newStart = startBefore + deltaTicks;
            if (newStart < 0) newStart = 0;
            // Vertical: follow the cursor across tracks during drag.  The
            // item's logical track index isn't updated until release; here
            // we just move the visual position.
            const double origTrackY = trackTopY(item->trackIndex());
            item->setPos(tickToSceneX(newStart), origTrackY + deltaYScene);
            break;
        }
        case SequencerClipItem::TrimLeft: {
            Tick newStart    = startBefore + deltaTicks;
            if (newStart < 0) newStart = 0;
            Tick newDuration = durationBefore - (newStart - startBefore);
            if (newDuration < 1) break;
            item->updateLayout(item->trackIndex(), item->clipIndex(),
                               newStart, newDuration);
            break;
        }
        case SequencerClipItem::TrimRight: {
            Tick newDuration = durationBefore + deltaTicks;
            if (newDuration < 1) break;
            item->updateLayout(item->trackIndex(), item->clipIndex(),
                               startBefore, newDuration);
            break;
        }
        default: break;
    }
}

void SequencerTimelineView::clipItemReleased(SequencerClipItem* item,
                                             SequencerClipItem::DragMode mode,
                                             Tick deltaTicks, double deltaYScene)
{
    if (!item || !m_project) { onProjectChanged(); return; }
    const int tIdx = item->trackIndex();
    const int cIdx = item->clipIndex();
    if (tIdx < 0 || tIdx >= m_project->trackCount()) { onProjectChanged(); return; }
    const auto& track = m_project->track(tIdx);
    if (cIdx < 0 || cIdx >= track.clips.size()) { onProjectChanged(); return; }

    switch (mode) {
        case SequencerClipItem::Move: {
            // Determine final target track from the vertical drag offset.
            const double finalY = trackTopY(tIdx) + deltaYScene + kTrackHeight / 2.0;
            int targetTrack = trackIndexAtY(finalY, m_project->trackCount());
            if (targetTrack < 0) targetTrack = tIdx;   // dragged into gap → stay

            // X position drives insertion index on the target track.
            const Tick newStart = track.clips[cIdx].timelineStartTicks + deltaTicks;

            if (targetTrack == tIdx) {
                // Intra-track re-order: midpoint-walk over neighbours.
                int targetIdx = cIdx;
                if (deltaTicks > 0) {
                    while (targetIdx + 1 < track.clips.size()) {
                        const auto& next = track.clips[targetIdx + 1];
                        const Tick mid = next.timelineStartTicks
                                       + next.trimmedDurationTicks() / 2;
                        if (newStart > mid) ++targetIdx; else break;
                    }
                } else if (deltaTicks < 0) {
                    while (targetIdx > 0) {
                        const auto& prev = track.clips[targetIdx - 1];
                        const Tick mid = prev.timelineStartTicks
                                       + prev.trimmedDurationTicks() / 2;
                        if (newStart < mid) --targetIdx; else break;
                    }
                }
                if (targetIdx != cIdx) {
                    m_project->executeCommand(
                        std::make_unique<MoveClipCmd>(tIdx, cIdx, targetIdx));
                } else {
                    onProjectChanged();   // snap back
                }
            } else {
                // Cross-track move.  Insert index = number of target-track
                // clips whose midpoint is to the left of newStart.
                const auto& dst = m_project->track(targetTrack);
                int insertIdx = 0;
                for (const auto& c : dst.clips) {
                    const Tick mid = c.timelineStartTicks
                                   + c.trimmedDurationTicks() / 2;
                    if (newStart > mid) ++insertIdx; else break;
                }
                m_project->executeCommand(
                    std::make_unique<MoveClipAcrossTracksCmd>(
                        tIdx, cIdx, targetTrack, insertIdx));
            }
            break;
        }
        case SequencerClipItem::TrimLeft: {
            const auto& clip = track.clips[cIdx];
            Tick newSourceIn = clip.sourceInTicks + deltaTicks;
            if (newSourceIn < 0) newSourceIn = 0;
            if (newSourceIn >= clip.sourceOutTicks) newSourceIn = clip.sourceOutTicks - 1;
            m_project->executeCommand(std::make_unique<TrimClipCmd>(
                tIdx, cIdx, newSourceIn, clip.sourceOutTicks));
            break;
        }
        case SequencerClipItem::TrimRight: {
            const auto& clip = track.clips[cIdx];
            Tick newSourceOut = clip.sourceOutTicks + deltaTicks;
            if (newSourceOut > clip.sourceDurationTicks)
                newSourceOut = clip.sourceDurationTicks;
            if (newSourceOut <= clip.sourceInTicks)
                newSourceOut = clip.sourceInTicks + 1;
            m_project->executeCommand(std::make_unique<TrimClipCmd>(
                tIdx, cIdx, clip.sourceInTicks, newSourceOut));
            break;
        }
        default:
            onProjectChanged();
            break;
    }
}

// =============================================================================
// Ruler click → seek
// =============================================================================

void SequencerTimelineView::seekToSceneX(double x)
{
    if (!m_clock) return;
    m_clock->seek(sceneXToTick(x));
}

// =============================================================================
// Drop events (from MediaBin or OS file drag)
// =============================================================================

void SequencerTimelineView::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()
        || e->mimeData()->hasFormat(QString::fromLatin1(kClipEffectMimeType))) {
        e->acceptProposedAction();
        return;
    }
    QGraphicsView::dragEnterEvent(e);
}

void SequencerTimelineView::dragMoveEvent(QDragMoveEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        const QPointF scenePos = mapToScene(e->position().toPoint());
        m_dropHoverTrack = trackIndexAtY(scenePos.y(),
                                         m_project ? m_project->trackCount() : 0);
        e->acceptProposedAction();
        return;
    }
    if (e->mimeData()->hasFormat(QString::fromLatin1(kClipEffectMimeType))) {
        // Effects drop onto a specific clip; the visual hover highlight for
        // URL drops tracks track rows, which would be misleading here, so
        // just accept the drag without updating m_dropHoverTrack.
        e->acceptProposedAction();
        return;
    }
    QGraphicsView::dragMoveEvent(e);
}

void SequencerTimelineView::dropEvent(QDropEvent* e)
{
    if (!m_project) { e->ignore(); return; }

    // ── Effect drop (from the Effects Rack) ──────────────────────────────
    // Find the clip whose timeline range contains the drop point and tail-
    // append the effect to its list.  Dropping on empty timeline space is a
    // no-op — silently ignored (no snackbar etc.; consistent with the rest
    // of the sequencer's drop UX).
    if (e->mimeData()->hasFormat(QString::fromLatin1(kClipEffectMimeType))) {
        const QByteArray payload = e->mimeData()->data(
            QString::fromLatin1(kClipEffectMimeType));
        const auto maybeEffect = clipEffectFromId(QString::fromUtf8(payload));
        if (!maybeEffect) { e->ignore(); return; }

        const QPointF scenePos = mapToScene(e->position().toPoint());
        const int trackIdx = trackIndexAtY(scenePos.y(), m_project->trackCount());
        if (trackIdx < 0) { e->ignore(); return; }

        const Tick tickAtDrop = sceneXToTick(scenePos.x());
        const auto& track = m_project->track(trackIdx);
        int targetClipIdx = -1;
        for (int i = 0; i < track.clips.size(); ++i) {
            if (track.clips[i].containsTimelineTick(tickAtDrop)) {
                targetClipIdx = i;
                break;
            }
        }
        if (targetClipIdx < 0) { e->ignore(); return; }

        QVector<ClipEffect> next = track.clips[targetClipIdx].effects;
        next.append(*maybeEffect);
        m_project->executeCommand(std::make_unique<ChangeClipEffectsCmd>(
            trackIdx, targetClipIdx, std::move(next)));
        e->acceptProposedAction();
        return;
    }

    if (!e->mimeData()->hasUrls()) { e->ignore(); return; }

    const QPointF scenePos = mapToScene(e->position().toPoint());
    int trackIdx = trackIndexAtY(scenePos.y(),
                                 m_project->trackCount());
    // Drop on the ruler band or past the last track: create a new track
    // if there's headroom, otherwise use the first track.
    if (trackIdx < 0) {
        if (m_project->trackCount() == 0) {
            m_project->executeCommand(std::make_unique<AddTrackCmd>("Track 1"));
            trackIdx = 0;
        } else {
            trackIdx = 0;
        }
    }

    for (const QUrl& url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.isEmpty()) continue;
        SequencerClip clip;
        clip.sourcePath = path;
        if (!probeFile(path, clip)) continue;

        // Insert at the end of the target track for now.  Snapping the drop
        // X to an insert-index requires knowing inter-clip boundaries; left
        // as a Phase-2-polish task.
        m_project->executeCommand(
            std::make_unique<AppendClipCmd>(trackIdx, clip));
    }

    m_dropHoverTrack = -1;
    e->acceptProposedAction();
}

// =============================================================================
// Keyboard
// =============================================================================

void SequencerTimelineView::keyPressEvent(QKeyEvent* e)
{
    if (!m_project) { QGraphicsView::keyPressEvent(e); return; }

    // Selected clip — take the first selected SequencerClipItem.
    SequencerClipItem* sel = nullptr;
    for (auto* ci : m_clipItems) {
        if (ci && ci->isSelected()) { sel = ci; break; }
    }

    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        if (sel) {
            m_project->executeCommand(std::make_unique<RemoveClipCmd>(
                sel->trackIndex(), sel->clipIndex()));
        }
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_S) {
        const Tick playhead = m_clock ? m_clock->currentTick() : 0;
        // Split the clip at playhead on the active track (not the selected
        // clip — more like an NLE razor tool that cuts whatever is under
        // the playhead).
        const int tIdx = m_project->activeTrackIndex();
        if (tIdx >= 0 && tIdx < m_project->trackCount()) {
            const auto& track = m_project->track(tIdx);
            const int cIdx = track.clipIndexAtTick(playhead);
            if (cIdx >= 0) {
                m_project->executeCommand(std::make_unique<SplitClipCmd>(
                    tIdx, cIdx, playhead));
            }
        }
        e->accept();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void SequencerTimelineView::mousePressEvent(QMouseEvent* e)
{
    // Click on empty space clears clip selection.  Let the base class run
    // first so clip items get their mousePressEvent before we decide we
    // clicked empty space.
    QGraphicsView::mousePressEvent(e);
    if (!e->isAccepted()) {
        for (auto* ci : m_clipItems) if (ci) ci->setSelected(false);
        m_selectedTrackIdx = -1;
        m_selectedClipIdx  = -1;
        // Notify listeners (e.g. the Clip Properties panel) that nothing
        // is selected now.  (-1, -1) is the "no selection" sentinel.
        emit selectedClipChanged(-1, -1);
    }
}

// =============================================================================
// probeFile — minimal duplicate of SequencerDock::probeFile.
// =============================================================================

bool SequencerTimelineView::probeFile(const QString& path,
                                      SequencerClip& outClip) const
{
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    int vIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vIdx = static_cast<int>(i);
            break;
        }
    }
    if (vIdx < 0) { avformat_close_input(&fmt); return false; }
    AVStream* s = fmt->streams[vIdx];
    outClip.sourceTimeBase  = s->time_base;
    outClip.sourceFrameRate = (s->avg_frame_rate.num > 0 && s->avg_frame_rate.den > 0)
                              ? s->avg_frame_rate
                              : (s->r_frame_rate.num > 0 ? s->r_frame_rate : AVRational{30, 1});
    int64_t dur = s->duration;
    AVRational durTb = s->time_base;
    if (dur <= 0) { dur = fmt->duration; durTb = AVRational{1, AV_TIME_BASE}; }
    outClip.sourceDurationTicks = (dur > 0) ? streamTsToTicks(dur, durTb) : 0;
    outClip.sourceInTicks  = 0;
    outClip.sourceOutTicks = outClip.sourceDurationTicks;
    avformat_close_input(&fmt);
    return outClip.sourceDurationTicks > 0;
}

} // namespace sequencer
