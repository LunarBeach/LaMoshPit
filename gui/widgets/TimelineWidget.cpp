#include "TimelineWidget.h"

#include <QPainter>
#include <QScrollBar>
#include <QScrollArea>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QThread>
#include <QVBoxLayout>
#include <QLabel>
#include <QDebug>
#include <QImage>
#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// =============================================================================
// ThumbnailLoader — runs on a worker thread, emits thumbnails as it decodes.
// Defined here (cpp-private) and brought into MOC via the include at EOF.
// =============================================================================
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    ThumbnailLoader(const QString& videoPath, int thumbW, int thumbH)
        : m_path(videoPath), m_thumbW(thumbW), m_thumbH(thumbH) {}

public slots:
    void run()
    {
        AVFormatContext* fmtCtx = nullptr;
        AVCodecContext*  decCtx = nullptr;
        SwsContext*      swsCtx = nullptr;
        AVFrame*  frame    = nullptr;
        AVFrame*  rgbFrame = nullptr;
        uint8_t*  rgbBuf   = nullptr;
        AVPacket* pkt      = nullptr;
        int vsIdx = -1;
        int frameIdx = 0;
        int scaledW = 0, scaledH = 0;

        if (avformat_open_input(&fmtCtx, m_path.toUtf8().constData(), nullptr, nullptr) < 0)
            goto done;
        if (avformat_find_stream_info(fmtCtx, nullptr) < 0)
            goto done;

        for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
            if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                vsIdx = (int)i; break;
            }
        }
        if (vsIdx < 0) goto done;

        {
            const AVCodec* codec = avcodec_find_decoder(fmtCtx->streams[vsIdx]->codecpar->codec_id);
            decCtx = avcodec_alloc_context3(codec);
            if (!decCtx) goto done;
            if (avcodec_parameters_to_context(decCtx, fmtCtx->streams[vsIdx]->codecpar) < 0) goto done;
            if (avcodec_open2(decCtx, codec, nullptr) < 0) goto done;
        }

        frame    = av_frame_alloc();
        rgbFrame = av_frame_alloc();
        pkt = av_packet_alloc();

// Macro: scale decoded frame → aspect-correct padded thumbnail, emit, advance
#define EMIT_THUMB() do { \
    if (frame->width && frame->height) { \
        if (scaledW == 0) { \
            double scX = (double)m_thumbW / frame->width; \
            double scY = (double)m_thumbH / frame->height; \
            double sc  = qMin(scX, scY); \
            scaledW = qMax(2, (int)(frame->width  * sc)) & ~1; \
            scaledH = qMax(2, (int)(frame->height * sc)) & ~1; \
            int bufSz = av_image_get_buffer_size(AV_PIX_FMT_RGB24, scaledW, scaledH, 1); \
            rgbBuf = (uint8_t*)av_malloc((size_t)bufSz); \
            av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, \
                                 rgbBuf, AV_PIX_FMT_RGB24, scaledW, scaledH, 1); \
            swsCtx = sws_getContext( \
                frame->width, frame->height, (AVPixelFormat)frame->format, \
                scaledW, scaledH, AV_PIX_FMT_RGB24, \
                SWS_BILINEAR, nullptr, nullptr, nullptr); \
        } \
        if (swsCtx) { \
            sws_scale(swsCtx, frame->data, frame->linesize, 0, frame->height, \
                      rgbFrame->data, rgbFrame->linesize); \
            QImage sc(rgbBuf, scaledW, scaledH, rgbFrame->linesize[0], QImage::Format_RGB888); \
            QImage pad(m_thumbW, m_thumbH, QImage::Format_RGB888); \
            pad.fill(QColor(0x11, 0x11, 0x11)); \
            QPainter ptr(&pad); \
            ptr.drawImage((m_thumbW - scaledW) / 2, (m_thumbH - scaledH) / 2, sc); \
            ptr.end(); \
            emit thumbnailReady(frameIdx, QPixmap::fromImage(pad)); \
        } \
    } \
    frameIdx++; \
    av_frame_unref(frame); \
} while(0)

        while (av_read_frame(fmtCtx, pkt) >= 0) {
            if (pkt->stream_index == vsIdx) {
                if (avcodec_send_packet(decCtx, pkt) == 0) {
                    while (avcodec_receive_frame(decCtx, frame) == 0)
                        EMIT_THUMB();
                }
            }
            av_packet_unref(pkt);
        }

        // Flush decoder
        avcodec_send_packet(decCtx, nullptr);
        while (avcodec_receive_frame(decCtx, frame) == 0)
            EMIT_THUMB();

#undef EMIT_THUMB

    done:
        if (swsCtx)  sws_freeContext(swsCtx);
        if (rgbBuf)  av_free(rgbBuf);
        if (rgbFrame) av_frame_free(&rgbFrame);
        if (frame)   av_frame_free(&frame);
        if (pkt)     av_packet_free(&pkt);
        if (decCtx)  avcodec_free_context(&decCtx);
        if (fmtCtx)  avformat_close_input(&fmtCtx);

        emit finished(frameIdx);
    }

signals:
    void thumbnailReady(int frameIndex, QPixmap pixmap);
    void finished(int totalCount);

private:
    QString m_path;
    int     m_thumbW, m_thumbH;
};

// =============================================================================
// FrameStripWidget
// =============================================================================

FrameStripWidget::FrameStripWidget(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::ClickFocus);
    setFixedHeight(WIDGET_H);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMinimumWidth(SLOT_W);
}

void FrameStripWidget::setFrameData(const QVector<char>& types,
                                    const QVector<bool>& idrs)
{
    int n = std::min(types.size(), idrs.size());
    m_frames.resize(n);
    for (int i = 0; i < n; i++) {
        m_frames[i].index       = i;
        m_frames[i].type        = types[i];
        m_frames[i].isIDR       = idrs[i];
        m_frames[i].thumbLoaded = false;
        m_frames[i].thumb       = QPixmap();
    }
    m_selected.clear();
    m_lastClicked = -1;

    int totalW = n > 0 ? n * SLOT_W + GAP : SLOT_W;
    setFixedWidth(totalW);
    update();
}

void FrameStripWidget::setThumbnail(int frameIndex, const QPixmap& thumb)
{
    if (frameIndex < 0 || frameIndex >= m_frames.size()) return;
    m_frames[frameIndex].thumb       = thumb;
    m_frames[frameIndex].thumbLoaded = true;
    // Only repaint the region for this cell
    update(cellRect(frameIndex).adjusted(-4, -4, 4, 4));
}

void FrameStripWidget::clear()
{
    m_frames.clear();
    m_selected.clear();
    m_lastClicked = -1;
    setFixedWidth(SLOT_W);
    update();
}

void FrameStripWidget::clearSelection()
{
    m_selected.clear();
    update();
    emit selectionChanged(m_selected);
}

void FrameStripWidget::setSelection(int frameIdx)
{
    if (frameIdx < 0 || frameIdx >= m_frames.size()) return;
    m_selected.clear();
    m_selected.append(frameIdx);
    m_lastClicked = frameIdx;
    update();
    emit selectionChanged(m_selected);
}

// ── Paint ──────────────────────────────────────────────────────────────────

void FrameStripWidget::paintEvent(QPaintEvent* e)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x14, 0x14, 0x14));

    if (m_frames.isEmpty()) {
        p.setPen(QColor(80, 80, 80));
        p.setFont(QFont("Arial", 11));
        p.drawText(rect(), Qt::AlignCenter,
                   "Import a video to populate the frame timeline");
        return;
    }

    // Only draw cells intersecting the dirty region for performance
    int firstCell = std::max(0,
        (e->rect().left() - GAP) / SLOT_W);
    int lastCell = std::min((int)m_frames.size() - 1,
        (e->rect().right() + SLOT_W - 1) / SLOT_W);

    for (int i = firstCell; i <= lastCell; i++)
        drawCell(p, i);

    // ── Drag-reorder overlay ──────────────────────────────────────────────
    if (m_dragActive && m_dropInsertIdx >= 0) {
        // Vertical drop-indicator line between the target gap.
        // The left edge of slot i is at i * SLOT_W; the gap sits at GAP/2 before it.
        const int lineX = m_dropInsertIdx * SLOT_W + GAP / 2 - 1;
        p.fillRect(lineX, MARGIN_V - 2, 3, CELL_H + 4, QColor(255, 160, 0, 230));
    }
}

void FrameStripWidget::drawCell(QPainter& p, int idx) const
{
    if (idx < 0 || idx >= m_frames.size()) return;

    const FrameThumbData& fd  = m_frames[idx];
    bool     selected = m_selected.contains(idx);
    QColor   bc       = borderColor(fd.type);
    QRect    cr       = cellRect(idx);

    // ── Selection halo (behind cell) ──────────────────────────────────────
    if (selected) {
        p.fillRect(cr.adjusted(-3, -3, 3, 3), QColor(255, 230, 30, 200));
    }

    // ── Colored border fill ───────────────────────────────────────────────
    p.fillRect(cr, bc);

    // ── Thumbnail ─────────────────────────────────────────────────────────
    QRect thumbR(cr.x() + BORDER,  cr.y() + BORDER,
                 THUMB_W,           THUMB_H);

    if (fd.thumbLoaded) {
        // Preserve aspect ratio: fit the thumbnail inside thumbR, centered
        p.fillRect(thumbR, QColor(0x11, 0x11, 0x11));
        const QSize ts = fd.thumb.size();
        if (!ts.isEmpty()) {
            const QSize fitted = ts.scaled(thumbR.size(), Qt::KeepAspectRatio);
            const int dx = (thumbR.width()  - fitted.width())  / 2;
            const int dy = (thumbR.height() - fitted.height()) / 2;
            const QRect dst(thumbR.x() + dx, thumbR.y() + dy,
                            fitted.width(), fitted.height());
            p.drawPixmap(dst, fd.thumb);
        }
    } else {
        // Placeholder: dark fill + dimmed frame number centered
        p.fillRect(thumbR, QColor(38, 38, 38));
        p.setPen(QColor(70, 70, 70));
        p.setFont(QFont("Consolas", 9));
        p.drawText(thumbR, Qt::AlignCenter, QString::number(idx));
    }

    // Selection overlay on the thumbnail
    if (selected) {
        p.fillRect(thumbR, QColor(255, 230, 30, 55));
    }

    // ── Label bar ─────────────────────────────────────────────────────────
    QRect labelR(cr.x() + BORDER,
                 cr.y() + BORDER + THUMB_H,
                 THUMB_W, LABEL_H);
    p.fillRect(labelR, QColor(12, 12, 12));

    // Label text: type char + index, or IDR tag
    QString labelTxt;
    if (fd.isIDR)
        labelTxt = QString("IDR %1").arg(idx);
    else
        labelTxt = QString("%1 %2").arg(fd.type).arg(idx);

    p.setPen(selected ? QColor(255, 230, 30) : bc);
    p.setFont(QFont("Consolas", 8));
    p.drawText(labelR, Qt::AlignCenter, labelTxt);

    // ── Drag ghost — dim the cell that is being dragged ───────────────────
    if (m_dragActive && idx == m_dragSourceIdx) {
        p.fillRect(cr, QColor(0, 0, 0, 170));
    }
}

QRect FrameStripWidget::cellRect(int idx) const
{
    int x = idx * SLOT_W + GAP / 2;
    int y = MARGIN_V;
    return QRect(x, y, CELL_W, CELL_H);
}

int FrameStripWidget::cellAtX(int x) const
{
    int idx = x / SLOT_W;
    if (idx < 0 || idx >= m_frames.size()) return -1;
    return idx;
}

// Returns the insertion gap index (0 … frameCount) nearest to x.
// Gap 0 = before frame 0, gap N = after the last frame.
int FrameStripWidget::dropTargetAtX(int x) const
{
    // The centre of the gap between frame[i-1] and frame[i] is at i * SLOT_W.
    int candidate = (x + SLOT_W / 2) / SLOT_W;
    return qBound(0, candidate, m_frames.size());
}

QColor FrameStripWidget::borderColor(char type) const
{
    switch (type) {
        case 'I': return QColor(255, 255, 255);    // white
        case 'P': return QColor(68,  136, 255);    // blue
        case 'B': return QColor(255, 100, 180);    // pink
        default:  return QColor(80,  80,  80);     // gray
    }
}

// ── Mouse / Keyboard ───────────────────────────────────────────────────────

void FrameStripWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { QWidget::mousePressEvent(e); return; }

    int idx = cellAtX(e->position().toPoint().x());
    if (idx < 0) { QWidget::mousePressEvent(e); return; }

    // Save drag-start state regardless of modifiers.  The actual drag is only
    // activated once the mouse moves more than the threshold in mouseMoveEvent.
    m_dragSourceIdx = idx;
    m_dragStartX    = e->position().toPoint().x();
    m_dragActive    = false;
    m_dropInsertIdx = -1;

    bool ctrl  = e->modifiers() & Qt::ControlModifier;
    bool shift = e->modifiers() & Qt::ShiftModifier;

    if (shift && m_lastClicked >= 0) {
        // Range-extend selection
        int lo = std::min(m_lastClicked, idx);
        int hi = std::max(m_lastClicked, idx);
        if (!ctrl) m_selected.clear();
        for (int i = lo; i <= hi; i++)
            if (!m_selected.contains(i))
                m_selected.append(i);
    } else if (ctrl) {
        // Toggle single cell
        int pos = m_selected.indexOf(idx);
        if (pos >= 0) m_selected.removeAt(pos);
        else          m_selected.append(idx);
        m_lastClicked = idx;
    } else {
        // Single select
        m_selected.clear();
        m_selected.append(idx);
        m_lastClicked = idx;
    }

    update();
    emit selectionChanged(m_selected);
    e->accept();
}

void FrameStripWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & Qt::LeftButton) || m_dragSourceIdx < 0) {
        QWidget::mouseMoveEvent(e); return;
    }

    const int x  = e->position().toPoint().x();
    const int dx = x - m_dragStartX;

    // Activate drag once the mouse moves more than 8 pixels horizontally.
    if (!m_dragActive && std::abs(dx) > 8)
        m_dragActive = true;

    if (m_dragActive) {
        m_dropInsertIdx = dropTargetAtX(x);
        update();
    }
    e->accept();
}

void FrameStripWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { QWidget::mouseReleaseEvent(e); return; }

    if (m_dragActive && m_dragSourceIdx >= 0 && m_dropInsertIdx >= 0) {
        emit frameReorderRequested(m_dragSourceIdx, m_dropInsertIdx);
    }

    // Reset drag state and repaint to clear the drop indicator.
    m_dragActive    = false;
    m_dragSourceIdx = -1;
    m_dragStartX    = -1;
    m_dropInsertIdx = -1;
    update();
    e->accept();
}

void FrameStripWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        clearSelection();
    } else if (e->key() == Qt::Key_A && (e->modifiers() & Qt::ControlModifier)) {
        // Ctrl+A: select all
        m_selected.clear();
        for (int i = 0; i < m_frames.size(); i++)
            m_selected.append(i);
        update();
        emit selectionChanged(m_selected);
    } else {
        QWidget::keyPressEvent(e);
    }
}

void FrameStripWidget::wheelEvent(QWheelEvent* e)
{
    // Route vertical scroll-wheel delta as horizontal scrolling of the parent
    // QScrollArea so the timeline can be panned left/right with the mouse wheel.
    if (e->angleDelta().y() != 0) {
        QScrollArea* sa = qobject_cast<QScrollArea*>(
            parentWidget() ? parentWidget()->parent() : nullptr);
        if (sa) {
            QScrollBar* hbar = sa->horizontalScrollBar();
            if (hbar) {
                hbar->setValue(hbar->value() - e->angleDelta().y() * 3);
                e->accept();
                return;
            }
        }
    }
    QWidget::wheelEvent(e);
}

// =============================================================================
// TimelineWidget
// =============================================================================

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent)
{
    m_strip  = new FrameStripWidget(this);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_strip);
    m_scroll->setWidgetResizable(false);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setFixedHeight(FrameStripWidget::WIDGET_H
                             + m_scroll->horizontalScrollBar()->sizeHint().height()
                             + 2);
    m_scroll->setStyleSheet(
        "QScrollArea { background: #141414; }"
        "QScrollBar:horizontal { background: #1e1e1e; height: 10px; }"
        "QScrollBar::handle:horizontal { background: #444; border-radius: 4px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_scroll);
    setLayout(layout);

    connect(m_strip, &FrameStripWidget::selectionChanged,
            this, &TimelineWidget::onStripSelectionChanged);

    connect(m_strip, &FrameStripWidget::frameReorderRequested,
            this, &TimelineWidget::frameReorderRequested);
}

TimelineWidget::~TimelineWidget()
{
    stopLoader();
}

void TimelineWidget::stopLoader()
{
    if (m_loaderThread) {
        m_loaderThread->quit();
        m_loaderThread->wait(3000);
        // loader and thread clean themselves up via deleteLater connections
        m_loaderThread = nullptr;
        m_loader       = nullptr;
    }
}

void TimelineWidget::loadVideo(const QString& videoPath,
                               const QVector<char>& frameTypes,
                               const QVector<bool>& idrFlags)
{
    stopLoader();
    m_strip->setFrameData(frameTypes, idrFlags);

    // Kick off async thumbnail extraction
    auto* loader = new ThumbnailLoader(videoPath,
                                       FrameStripWidget::THUMB_W,
                                       FrameStripWidget::THUMB_H);
    auto* thread = new QThread(this);
    loader->moveToThread(thread);

    connect(thread, &QThread::started,
            loader, &ThumbnailLoader::run);
    connect(loader, &ThumbnailLoader::thumbnailReady,
            this,   &TimelineWidget::onThumbnailReady);
    connect(loader, &ThumbnailLoader::finished,
            this,   &TimelineWidget::onLoadingDone);
    connect(loader, &ThumbnailLoader::finished,
            thread, &QThread::quit);
    connect(thread, &QThread::finished,
            loader, &QObject::deleteLater);
    connect(thread, &QThread::finished,
            thread, &QObject::deleteLater);

    m_loaderThread = thread;
    m_loader       = loader;
    thread->start();
}

QVector<int> TimelineWidget::selectedFrames() const
{
    return m_strip->selectedIndices();
}

void TimelineWidget::clearSelection()
{
    m_strip->clearSelection();
}

void TimelineWidget::setSelection(int frameIdx)
{
    m_strip->setSelection(frameIdx);
    // Scroll the selected cell into view, centred if possible
    int cellX = frameIdx * FrameStripWidget::SLOT_W;
    int target = cellX - m_scroll->viewport()->width() / 2
                       + FrameStripWidget::CELL_W / 2;
    m_scroll->horizontalScrollBar()->setValue(
        qBound(0, target, m_scroll->horizontalScrollBar()->maximum()));
}

void TimelineWidget::onThumbnailReady(int frameIndex, QPixmap thumb)
{
    m_strip->setThumbnail(frameIndex, thumb);
}

void TimelineWidget::onLoadingDone(int totalCount)
{
    qDebug() << "TimelineWidget: thumbnails loaded for" << totalCount << "frames";
    m_loaderThread = nullptr;
    m_loader       = nullptr;
}

void TimelineWidget::onStripSelectionChanged(QVector<int> sel)
{
    if (sel.size() == 1)
        emit frameSelected(sel.first());
    emit selectionChanged(sel);
}

// Bring private class ThumbnailLoader into MOC so its signals/slots compile
#include "TimelineWidget.moc"
