#include "VideoMetaProbe.h"

#include <cmath>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

VideoMeta VideoMetaProbe::probe(const QString& videoPath)
{
    VideoMeta out;
    AVFormatContext* fmtCtx = nullptr;
    AVPacket*        pkt    = nullptr;

    if (avformat_open_input(&fmtCtx, videoPath.toUtf8().constData(),
                            nullptr, nullptr) < 0) {
        out.errorMsg = "avformat_open_input failed";
        goto done;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        out.errorMsg = "avformat_find_stream_info failed";
        goto done;
    }

    {
        int vsIdx = -1;
        for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
            if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                vsIdx = int(i);
                break;
            }
        }
        if (vsIdx < 0) {
            out.errorMsg = "no video stream";
            goto done;
        }

        AVStream* st = fmtCtx->streams[vsIdx];
        out.width  = st->codecpar->width;
        out.height = st->codecpar->height;

        const AVRational guessed = av_guess_frame_rate(fmtCtx, st, nullptr);
        if (guessed.num > 0 && guessed.den > 0)
            out.fps = double(guessed.num) / double(guessed.den);

        // Prefer the reported nb_frames if present; fall back to packet count.
        if (st->nb_frames > 0) {
            out.totalFrames = int(st->nb_frames);
        } else {
            pkt = av_packet_alloc();
            int count = 0;
            while (av_read_frame(fmtCtx, pkt) >= 0) {
                if (pkt->stream_index == vsIdx) ++count;
                av_packet_unref(pkt);
            }
            out.totalFrames = count;
        }

        out.ok = (out.width > 0 && out.height > 0 && out.totalFrames > 0);
        if (!out.ok && out.errorMsg.isEmpty())
            out.errorMsg = "invalid video dimensions or frame count";
    }

done:
    if (pkt)    av_packet_free(&pkt);
    if (fmtCtx) avformat_close_input(&fmtCtx);
    return out;
}

bool VideoMetaProbe::compatible(const VideoMeta& a, const VideoMeta& b,
                                double fpsTolerance)
{
    if (!a.ok || !b.ok)                              return false;
    if (a.width       != b.width)                    return false;
    if (a.height      != b.height)                   return false;
    if (a.totalFrames != b.totalFrames)              return false;
    if (std::fabs(a.fps - b.fps) > fpsTolerance)     return false;
    return true;
}
