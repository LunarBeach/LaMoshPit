#pragma once

// =============================================================================
// SpoutSender — publishes the router's output frames to a Spout2 sender so
// OBS (via the Spout2 plugin) or any Spout-aware receiver can consume them
// as a live video source.
//
// Phase-4 scope: CPU-side SendImage path.  Each published frame is the BGRA
// QImage that the FrameRouter emits, forwarded into spoutDX::SendImage,
// which handles the D3D11 shared texture plumbing internally.
//
// The zero-copy GPU path (decode → D3D11 texture → spoutDX::SendTexture,
// skipping the swscale trip through CPU) is feasible later once we move
// the preview display to a QRhiWidget / D3D11 native path.  The current
// CPU route still achieves sub-millisecond sender overhead at 1080p.
//
// Lifecycle:
//   - Create the QObject; it's inert until start() is called.
//   - start(name) opens DirectX11 + allocates sender named "LaMoshPit"
//     (or whatever was passed).  OBS will see it immediately.
//   - onFrameReady(...) slot is connected to FrameRouter::frameReady;
//     each frame forwards to SendImage.
//   - stop() releases the sender; safe to restart afterwards.
// =============================================================================

#include "core/sequencer/Tick.h"
#include <QObject>
#include <QImage>
#include <QString>
#include <memory>

class spoutDX;   // forward — Spout2 SDK class

namespace sequencer {

class SpoutSender : public QObject {
    Q_OBJECT
public:
    explicit SpoutSender(QObject* parent = nullptr);
    ~SpoutSender() override;

    // Open Spout and publish under the given sender name.  Returns false
    // if DirectX11 init fails (rare on Win11; usually indicates no GPU
    // driver or remote-desktop session without GPU passthrough).  Safe
    // to call again after a stop().
    bool start(const QString& senderName = "LaMoshPit");

    // Close the sender.  OBS etc. will drop the feed immediately.
    void stop();

    bool    isActive()   const { return m_active; }
    QString senderName() const { return m_name; }

    // Forward a BGRA-packed QImage to Spout.  Silently ignored if not
    // active.  The QImage must be Format_ARGB32 or Format_ARGB32_Premultiplied
    // (both store as BGRA bytes in memory on little-endian Windows).
    bool sendFrame(const QImage& img);

public slots:
    // Convenience slot — wire FrameRouter::frameReady directly to this.
    // Drops the tick argument (Spout has no concept of presentation time).
    void onFrameReady(QImage frame, ::sequencer::Tick tick);

private:
    std::unique_ptr<spoutDX> m_spout;
    QString                  m_name;
    bool                     m_active     { false };
    unsigned int             m_lastWidth  { 0 };
    unsigned int             m_lastHeight { 0 };
};

} // namespace sequencer
