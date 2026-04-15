#include "ThumbnailGenerator.h"

#include <QImage>
#include <QFileInfo>
#include <QDir>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// =============================================================================
// Helper: scoped cleanup guards.  The decoder open/close sequence has several
// early-return paths; RAII-style scope exit avoids the cleanup cascade.
// =============================================================================

namespace {

struct FmtCtxGuard {
    AVFormatContext* p = nullptr;
    ~FmtCtxGuard() { if (p) avformat_close_input(&p); }
};
struct CodecCtxGuard {
    AVCodecContext* p = nullptr;
    ~CodecCtxGuard() { if (p) avcodec_free_context(&p); }
};
struct FrameGuard {
    AVFrame* p = nullptr;
    ~FrameGuard() { if (p) av_frame_free(&p); }
};
struct PacketGuard {
    AVPacket* p = nullptr;
    ~PacketGuard() { if (p) av_packet_free(&p); }
};
struct SwsGuard {
    SwsContext* p = nullptr;
    ~SwsGuard() { if (p) sws_freeContext(p); }
};

}  // namespace

// =============================================================================

bool ThumbnailGenerator::generateMidFrameThumbnail(const QString& videoPath,
                                                   const QString& outPngPath,
                                                   QString& errorMsg,
                                                   int outWidth,
                                                   int outHeight)
{
    const QByteArray videoPathUtf8 = videoPath.toUtf8();

    // Open input file.
    FmtCtxGuard fmtCtx;
    if (avformat_open_input(&fmtCtx.p, videoPathUtf8.constData(),
                            nullptr, nullptr) < 0) {
        errorMsg = "avformat_open_input failed: " + videoPath;
        return false;
    }
    if (avformat_find_stream_info(fmtCtx.p, nullptr) < 0) {
        errorMsg = "avformat_find_stream_info failed";
        return false;
    }

    // Pick the first video stream.
    int videoStreamIdx = -1;
    for (unsigned i = 0; i < fmtCtx.p->nb_streams; ++i) {
        if (fmtCtx.p->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = (int)i;
            break;
        }
    }
    if (videoStreamIdx < 0) {
        errorMsg = "No video stream in: " + videoPath;
        return false;
    }
    AVStream* vStream = fmtCtx.p->streams[videoStreamIdx];

    // Open decoder.
    const AVCodec* decoder = avcodec_find_decoder(vStream->codecpar->codec_id);
    if (!decoder) { errorMsg = "No decoder available"; return false; }

    CodecCtxGuard decCtx;
    decCtx.p = avcodec_alloc_context3(decoder);
    if (!decCtx.p) { errorMsg = "avcodec_alloc_context3 failed"; return false; }
    if (avcodec_parameters_to_context(decCtx.p, vStream->codecpar) < 0) {
        errorMsg = "avcodec_parameters_to_context failed"; return false;
    }
    if (avcodec_open2(decCtx.p, decoder, nullptr) < 0) {
        errorMsg = "avcodec_open2 failed"; return false;
    }

    // Seek to roughly the middle of the file.  A simple approach: half the
    // stream duration in its own time base.  If seek fails (stream not
    // seekable, very short) we fall through and just decode the first frame
    // we get.  Either way the user gets *some* thumbnail.
    if (vStream->duration > 0) {
        const int64_t target = vStream->duration / 2;
        av_seek_frame(fmtCtx.p, videoStreamIdx, target, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(decCtx.p);
    }

    // Decode frames until we get one back.  After a seek we may need to
    // skip several packets before the decoder emits a frame.
    PacketGuard pkt;  pkt.p   = av_packet_alloc();
    FrameGuard  frame; frame.p = av_frame_alloc();
    if (!pkt.p || !frame.p) {
        errorMsg = "alloc packet/frame failed"; return false;
    }

    bool gotFrame = false;
    int  loopGuard = 0;
    while (!gotFrame && loopGuard++ < 500 &&
           av_read_frame(fmtCtx.p, pkt.p) >= 0) {
        if (pkt.p->stream_index == videoStreamIdx) {
            if (avcodec_send_packet(decCtx.p, pkt.p) == 0) {
                while (avcodec_receive_frame(decCtx.p, frame.p) == 0) {
                    gotFrame = true;
                    break;  // first decoded frame is fine
                }
            }
        }
        av_packet_unref(pkt.p);
        if (gotFrame) break;
    }
    if (!gotFrame) {
        errorMsg = "No frame decoded from: " + videoPath;
        return false;
    }

    // Scale YUV → RGB32 at the requested output size.  AVFrame's linesize is
    // the intermediate stride we feed to QImage below; QImage::Format_RGB32
    // is 4 bytes per pixel (BGRA byte order on LE).
    SwsGuard sws;
    sws.p = sws_getContext(
        frame.p->width, frame.p->height,
        (AVPixelFormat)frame.p->format,
        outWidth, outHeight,
        AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws.p) { errorMsg = "sws_getContext failed"; return false; }

    QImage image(outWidth, outHeight, QImage::Format_RGB32);
    uint8_t* dstData[4]     = { image.bits(), nullptr, nullptr, nullptr };
    int      dstLinesize[4] = { (int)image.bytesPerLine(), 0, 0, 0 };

    sws_scale(sws.p, frame.p->data, frame.p->linesize, 0, frame.p->height,
              dstData, dstLinesize);

    // Ensure parent folder exists — callers pass thumbnailPathFor() which is
    // under the project's thumbnails/ subdir; defensive mkpath costs nothing.
    const QFileInfo outInfo(outPngPath);
    outInfo.absoluteDir().mkpath(".");

    if (!image.save(outPngPath, "PNG")) {
        errorMsg = "QImage::save failed for " + outPngPath;
        return false;
    }

    return true;
}
