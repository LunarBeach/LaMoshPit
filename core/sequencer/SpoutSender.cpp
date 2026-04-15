#include "core/sequencer/SpoutSender.h"

#include <SpoutDX.h>   // spoutDX class (from third_party/Spout2/.../SpoutDX)

#include <QDebug>

namespace sequencer {

SpoutSender::SpoutSender(QObject* parent)
    : QObject(parent)
    , m_spout(std::make_unique<spoutDX>())
{
}

SpoutSender::~SpoutSender()
{
    stop();
}

bool SpoutSender::start(const QString& senderName)
{
    if (m_active) return true;   // already running

    if (!m_spout->OpenDirectX11()) {
        qWarning() << "[SpoutSender] OpenDirectX11 failed";
        return false;
    }

    m_name = senderName.isEmpty() ? QStringLiteral("LaMoshPit") : senderName;
    m_spout->SetSenderName(m_name.toLocal8Bit().constData());
    // B8G8R8A8_UNORM matches QImage::Format_ARGB32 byte order on little-endian
    // Windows (memory = BGRA).  This is the Spout default anyway but set it
    // explicitly so behavior doesn't drift if the SDK default changes.
    m_spout->SetSenderFormat(DXGI_FORMAT_B8G8R8A8_UNORM);
    m_active     = true;
    m_lastWidth  = 0;
    m_lastHeight = 0;
    return true;
}

void SpoutSender::stop()
{
    if (!m_active) return;
    m_spout->ReleaseSender();
    m_spout->CloseDirectX11();
    m_active = false;
}

bool SpoutSender::sendFrame(const QImage& img)
{
    if (!m_active || img.isNull()) return false;

    // Ensure the image is in a BGRA-compatible layout.  Format_ARGB32 maps
    // to BGRA bytes on little-endian Windows (what AV_PIX_FMT_BGRA also
    // produces); other formats get converted on the fly.
    const QImage* src = &img;
    QImage converted;
    if (img.format() != QImage::Format_ARGB32 &&
        img.format() != QImage::Format_ARGB32_Premultiplied) {
        converted = img.convertToFormat(QImage::Format_ARGB32);
        src = &converted;
    }

    const unsigned int w     = static_cast<unsigned int>(src->width());
    const unsigned int h     = static_cast<unsigned int>(src->height());
    const unsigned int pitch = static_cast<unsigned int>(src->bytesPerLine());

    m_lastWidth  = w;
    m_lastHeight = h;

    return m_spout->SendImage(src->constBits(), w, h, pitch);
}

void SpoutSender::onFrameReady(QImage frame, Tick /*tick*/)
{
    sendFrame(frame);
}

} // namespace sequencer
