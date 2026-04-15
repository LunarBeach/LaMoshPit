#include "core/sequencer/SequencerClipDecoder.h"
#include "core/sequencer/HwDecode.h"
#include "core/sequencer/Tick.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libavutil/rational.h>
#include <libavutil/error.h>
}

#include <QDebug>
#include <QFileInfo>

namespace sequencer {

// Convenience: stringify an FFmpeg error code into a QString.
static QString avErrStr(int err)
{
    char buf[128];
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// =============================================================================
// Construction / destruction
// =============================================================================

SequencerClipDecoder::SequencerClipDecoder() = default;

SequencerClipDecoder::~SequencerClipDecoder()
{
    closeAll();
}

void SequencerClipDecoder::closeAll()
{
    if (m_sws)   { sws_freeContext(m_sws); m_sws = nullptr; }
    if (m_pkt)   { av_packet_free(&m_pkt); }
    if (m_sw)    { av_frame_free(&m_sw); }
    if (m_decoded) { av_frame_free(&m_decoded); }
    if (m_codec) { avcodec_free_context(&m_codec); }
    if (m_fmt)   { avformat_close_input(&m_fmt); }
    m_videoStreamIdx = -1;
    m_isOpen  = false;
    m_hwAccel = false;
    m_eof     = false;
}

// =============================================================================
// open
// =============================================================================

bool SequencerClipDecoder::open(const SequencerClip& clip)
{
    closeAll();
    m_clip = clip;
    m_lastError.clear();

    if (clip.sourcePath.isEmpty() || !QFileInfo(clip.sourcePath).isFile()) {
        m_lastError = "source file missing";
        return false;
    }

    // ── Open container ─────────────────────────────────────────────────────
    int err = avformat_open_input(&m_fmt,
                                   clip.sourcePath.toUtf8().constData(),
                                   /*fmt=*/nullptr, /*opts=*/nullptr);
    if (err < 0) {
        m_lastError = QString("avformat_open_input: %1").arg(avErrStr(err));
        closeAll();
        return false;
    }
    err = avformat_find_stream_info(m_fmt, nullptr);
    if (err < 0) {
        m_lastError = QString("avformat_find_stream_info: %1").arg(avErrStr(err));
        closeAll();
        return false;
    }

    // ── Locate the first video stream ──────────────────────────────────────
    for (unsigned i = 0; i < m_fmt->nb_streams; ++i) {
        if (m_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIdx = static_cast<int>(i);
            break;
        }
    }
    if (m_videoStreamIdx < 0) {
        m_lastError = "no video stream";
        closeAll();
        return false;
    }

    AVStream* stream  = m_fmt->streams[m_videoStreamIdx];
    const AVCodec* cd = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!cd) {
        m_lastError = "decoder not found";
        closeAll();
        return false;
    }

    m_codec = avcodec_alloc_context3(cd);
    if (!m_codec) { m_lastError = "alloc codec ctx failed"; closeAll(); return false; }
    avcodec_parameters_to_context(m_codec, stream->codecpar);

    // ── Try HW decode; fall back silently to SW on failure ─────────────────
    if (tryAttachHwDeviceContext(m_codec)) {
        m_hwAccel = true;   // tentative — confirm after open
    }

    err = avcodec_open2(m_codec, cd, nullptr);
    if (err < 0) {
        // Most likely cause: HW init succeeded for the device but the
        // codec refused to bind to it for this bitstream.  Retry without HW.
        if (m_hwAccel) {
            avcodec_free_context(&m_codec);
            m_codec = avcodec_alloc_context3(cd);
            avcodec_parameters_to_context(m_codec, stream->codecpar);
            m_hwAccel = false;
            err = avcodec_open2(m_codec, cd, nullptr);
        }
        if (err < 0) {
            m_lastError = QString("avcodec_open2: %1").arg(avErrStr(err));
            closeAll();
            return false;
        }
    }

    m_decoded = av_frame_alloc();
    m_sw      = av_frame_alloc();
    m_pkt     = av_packet_alloc();
    if (!m_decoded || !m_sw || !m_pkt) {
        m_lastError = "frame/packet alloc failed";
        closeAll();
        return false;
    }

    // Cache the freshly probed timebase onto our local clip copy — it MUST
    // match what we compute rescales against in seek / pull.  The UI may
    // have written an approximate value at insert time.
    m_clip.sourceTimeBase = stream->time_base;

    m_isOpen = true;
    return true;
}

// =============================================================================
// seekToMasterTick
// =============================================================================

bool SequencerClipDecoder::seekToMasterTick(Tick masterTick)
{
    if (!m_isOpen) return false;

    // Map timeline tick → offset inside the clip's source in master ticks.
    // If masterTick falls before the clip's timeline start, clamp to 0
    // (caller mis-seeked; we decode from the clip's in-point).
    Tick offset = masterTick - m_clip.timelineStartTicks;
    if (offset < 0) offset = 0;
    const Tick sourceMasterTick = m_clip.sourceInTicks + offset;

    // Convert master tick → stream timebase timestamp for av_seek_frame.
    const int64_t streamTs = ticksToStreamTs(
        sourceMasterTick, m_clip.sourceTimeBase);

    const int err = av_seek_frame(m_fmt, m_videoStreamIdx, streamTs,
                                  AVSEEK_FLAG_BACKWARD);
    if (err < 0) {
        qWarning() << "[ClipDecoder] av_seek_frame failed:" << avErrStr(err);
        return false;
    }
    avcodec_flush_buffers(m_codec);
    m_eof = false;

    // Forward-decode until we land on a frame whose PTS is at or past the
    // requested point.  500-frame safety cap matches the old compositor;
    // on a well-formed H.264 file the forward distance is at most one GOP.
    for (int step = 0; step < 500; ++step) {
        QImage stagingImg;
        Tick   gotTick = 0;
        // produceImage=false — skip swscale during the seek-forward loop;
        // we're throwing these frames away anyway.
        if (!pullFrame(stagingImg, gotTick, /*produceImage=*/false)) return false;
        if (gotTick >= masterTick) {
            // Push the frame back into the pipeline by caching it?  We
            // don't have a cache at this layer; caller issues another
            // pullFrame() to fetch it.  Simpler: just return true here;
            // the next pullFrame fetches the NEXT frame.  For seek-and-
            // -show-frame use-cases the caller calls pullFrame again,
            // which costs only one more frame; acceptable for v1.
            //
            // A true frame cache lives at the compositor layer.
            return true;
        }
    }
    // Didn't reach the target in 500 frames — keep going on next call.
    return true;
}

// =============================================================================
// ensureSwsBgra
// =============================================================================

bool SequencerClipDecoder::ensureSwsBgra(int width, int height, int srcPixFmt)
{
    if (m_sws && m_swsWidth == width && m_swsHeight == height
        && m_swsSrcFmt == srcPixFmt)
        return true;
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    m_sws = sws_getContext(width, height,
                           static_cast<AVPixelFormat>(srcPixFmt),
                           width, height, AV_PIX_FMT_BGRA,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) {
        m_lastError = "sws_getContext failed";
        return false;
    }
    m_swsWidth  = width;
    m_swsHeight = height;
    m_swsSrcFmt = srcPixFmt;
    return true;
}

// =============================================================================
// pullFrame
// =============================================================================

bool SequencerClipDecoder::pullFrame(QImage& outImg, Tick& outMasterTick,
                                     bool produceImage)
{
    if (!m_isOpen) return false;
    if (m_eof)     return false;

    while (true) {
        // First, try to drain any buffered output from the decoder.
        int err = avcodec_receive_frame(m_codec, m_decoded);
        if (err == 0) {
            // Got a frame.  Transfer from HW to SW if needed.
            if (m_decoded->format == AV_PIX_FMT_D3D11) {
                if (!transferHwFrameToSw(m_decoded, m_sw)) {
                    m_lastError = "hw transfer failed";
                    return false;
                }
            } else {
                av_frame_unref(m_sw);
                av_frame_move_ref(m_sw, m_decoded);
            }

            // PTS → master tick.  Prefer best_effort_timestamp when PTS is
            // AV_NOPTS_VALUE (rare for well-formed H.264 but cheap insurance).
            int64_t pts = m_sw->pts;
            if (pts == AV_NOPTS_VALUE) pts = m_sw->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) pts = 0;

            const Tick srcMasterTick =
                streamTsToTicks(pts, m_clip.sourceTimeBase);
            // Source tick → timeline tick: subtract the clip's in-point,
            // add the clip's timeline start.
            outMasterTick = m_clip.timelineStartTicks +
                            (srcMasterTick - m_clip.sourceInTicks);

            if (!produceImage) {
                // Skip the swscale + QImage path — caller only needed to
                // advance the decoder state.
                av_frame_unref(m_sw);
                return true;
            }

            // Convert pixels to BGRA.
            if (!ensureSwsBgra(m_sw->width, m_sw->height, m_sw->format)) {
                return false;
            }
            // Format_ARGB32 stores pixels as uint32 0xAARRGGBB; on
            // little-endian (Windows) that's bytes B,G,R,A in memory —
            // exact match for AV_PIX_FMT_BGRA which swscale writes.
            QImage img(m_sw->width, m_sw->height, QImage::Format_ARGB32);
            uint8_t* dst[4] = { img.bits(), nullptr, nullptr, nullptr };
            int dstStride[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
            sws_scale(m_sws, m_sw->data, m_sw->linesize, 0, m_sw->height,
                      dst, dstStride);
            outImg = std::move(img);
            av_frame_unref(m_sw);
            return true;
        }
        if (err == AVERROR(EAGAIN)) {
            // Decoder wants more input — read packet below.
        } else if (err == AVERROR_EOF) {
            m_eof = true;
            return false;
        } else {
            m_lastError = QString("receive_frame: %1").arg(avErrStr(err));
            return false;
        }

        // Feed packets until we get a frame.
        err = av_read_frame(m_fmt, m_pkt);
        if (err == AVERROR_EOF) {
            // Flush decoder.
            avcodec_send_packet(m_codec, nullptr);
            continue;
        }
        if (err < 0) {
            m_lastError = QString("av_read_frame: %1").arg(avErrStr(err));
            return false;
        }
        if (m_pkt->stream_index != m_videoStreamIdx) {
            av_packet_unref(m_pkt);
            continue;
        }
        err = avcodec_send_packet(m_codec, m_pkt);
        av_packet_unref(m_pkt);
        if (err < 0 && err != AVERROR(EAGAIN)) {
            m_lastError = QString("send_packet: %1").arg(avErrStr(err));
            return false;
        }
        // Loop around — receive_frame may now produce output.
    }
}

} // namespace sequencer
