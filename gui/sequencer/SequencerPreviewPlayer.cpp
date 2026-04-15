#include "gui/sequencer/SequencerPreviewPlayer.h"

#include <QPainter>
#include <QPaintEvent>

namespace sequencer {

SequencerPreviewPlayer::SequencerPreviewPlayer(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
    // Opaque surface — we paint the full area every frame so Qt's
    // background erase is wasted work.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMinimumSize(160, 90);
}

void SequencerPreviewPlayer::onFrameReady(QImage frame, Tick tick)
{
    m_frame    = std::move(frame);
    m_lastTick = tick;
    update();
}

void SequencerPreviewPlayer::clearFrame()
{
    m_frame = QImage();
    update();
}

void SequencerPreviewPlayer::paintEvent(QPaintEvent* /*e*/)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (m_frame.isNull()) return;

    // Letterbox-fit: scale image to fit widget while preserving aspect.
    const QSize widgetSz = size();
    const QSize imgSz    = m_frame.size();
    QSize drawSz = imgSz;
    drawSz.scale(widgetSz, Qt::KeepAspectRatio);

    const int dx = (widgetSz.width()  - drawSz.width())  / 2;
    const int dy = (widgetSz.height() - drawSz.height()) / 2;
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(QRect(QPoint(dx, dy), drawSz), m_frame);
}

} // namespace sequencer
