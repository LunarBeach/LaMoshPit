#include "MapFrameSampler.h"

#include <QSet>
#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// =============================================================================
// Compute the MB selection set for a single map frame.
// frameGray is a luma-only image with stride `stride` and dimensions
// (widthPx × heightPx).  Each 16×16 block is averaged; blocks whose mean
// luma > 127 are added to the selection (indexed as row*mbCols+col).
// =============================================================================
static QSet<int> selectionFromGrayFrame(const uint8_t* frameGray,
                                        int stride,
                                        int widthPx, int heightPx,
                                        int mbCols, int mbRows)
{
    QSet<int> sel;
    if (!frameGray || mbCols <= 0 || mbRows <= 0) return sel;

    for (int row = 0; row < mbRows; ++row) {
        const int y0 = row * 16;
        const int y1 = std::min(y0 + 16, heightPx);
        if (y0 >= heightPx) break;
        for (int col = 0; col < mbCols; ++col) {
            const int x0 = col * 16;
            const int x1 = std::min(x0 + 16, widthPx);
            if (x0 >= widthPx) break;

            uint64_t sum = 0;
            uint32_t n   = 0;
            for (int y = y0; y < y1; ++y) {
                const uint8_t* row_p = frameGray + y * stride;
                for (int x = x0; x < x1; ++x) {
                    sum += row_p[x];
                    ++n;
                }
            }
            if (n == 0) continue;
            const int mean = int(sum / n);
            if (mean > 127) {
                sel.insert(row * mbCols + col);
            }
        }
    }
    return sel;
}

// =============================================================================
// sampleFrames — single-pass sequential decoder
// =============================================================================
QMap<int, QSet<int>> MapFrameSampler::sampleFrames(
    const QString& mapVideoPath,
    const QVector<int>& targetFrames,
    int mbCols, int mbRows,
    QString& errorMsg)
{
    QMap<int, QSet<int>> out;

    if (targetFrames.isEmpty() || mbCols <= 0 || mbRows <= 0) return out;

    // Fast lookup of requested frames + highest index we care about.
    QSet<int> wanted;
    int maxFrame = -1;
    for (int f : targetFrames) {
        if (f < 0) continue;
        wanted.insert(f);
        if (f > maxFrame) maxFrame = f;
    }
    if (wanted.isEmpty()) return out;

    AVFormatContext* fmtCtx  = nullptr;
    AVCodecContext*  decCtx  = nullptr;
    SwsContext*      swsCtx  = nullptr;
    AVFrame*         yuvFrm  = nullptr;
    AVFrame*         grayFrm = nullptr;
    uint8_t*         grayBuf = nullptr;
    AVPacket*        pkt     = nullptr;

    if (avformat_open_input(&fmtCtx, mapVideoPath.toUtf8().constData(),
                            nullptr, nullptr) < 0) {
        errorMsg = "avformat_open_input failed";
        goto done;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        errorMsg = "avformat_find_stream_info failed";
        goto done;
    }

    {
        int vsIdx = -1;
        for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
            if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                vsIdx = int(i); break;
            }
        }
        if (vsIdx < 0) { errorMsg = "no video stream"; goto done; }

        const AVCodec* codec =
            avcodec_find_decoder(fmtCtx->streams[vsIdx]->codecpar->codec_id);
        if (!codec) { errorMsg = "no decoder for map codec"; goto done; }

        decCtx = avcodec_alloc_context3(codec);
        if (!decCtx) { errorMsg = "alloc decoder context failed"; goto done; }
        if (avcodec_parameters_to_context(decCtx,
                fmtCtx->streams[vsIdx]->codecpar) < 0) {
            errorMsg = "parameters_to_context failed"; goto done;
        }
        if (avcodec_open2(decCtx, codec, nullptr) < 0) {
            errorMsg = "avcodec_open2 failed"; goto done;
        }

        const int widthPx  = decCtx->width;
        const int heightPx = decCtx->height;
        if (widthPx <= 0 || heightPx <= 0) {
            errorMsg = "invalid map dimensions"; goto done;
        }

        yuvFrm  = av_frame_alloc();
        grayFrm = av_frame_alloc();
        if (!yuvFrm || !grayFrm) { errorMsg = "av_frame_alloc failed"; goto done; }

        {
            const int bufSize = av_image_get_buffer_size(
                AV_PIX_FMT_GRAY8, widthPx, heightPx, 1);
            grayBuf = (uint8_t*)av_malloc((size_t)bufSize);
            if (!grayBuf) { errorMsg = "av_malloc failed"; goto done; }
            av_image_fill_arrays(grayFrm->data, grayFrm->linesize, grayBuf,
                                 AV_PIX_FMT_GRAY8, widthPx, heightPx, 1);
        }

        pkt = av_packet_alloc();
        int  frameIdx = 0;

        auto processIfWanted = [&](AVFrame* frm) {
            if (!wanted.contains(frameIdx)) return;
            if (!swsCtx) {
                swsCtx = sws_getContext(
                    frm->width, frm->height, (AVPixelFormat)frm->format,
                    widthPx, heightPx, AV_PIX_FMT_GRAY8,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
            }
            if (!swsCtx) return;
            sws_scale(swsCtx,
                      frm->data, frm->linesize, 0, frm->height,
                      grayFrm->data, grayFrm->linesize);
            out[frameIdx] = selectionFromGrayFrame(
                grayBuf, grayFrm->linesize[0],
                widthPx, heightPx, mbCols, mbRows);
        };

        while (frameIdx <= maxFrame && av_read_frame(fmtCtx, pkt) >= 0) {
            if (pkt->stream_index == vsIdx &&
                avcodec_send_packet(decCtx, pkt) == 0)
            {
                while (avcodec_receive_frame(decCtx, yuvFrm) == 0) {
                    processIfWanted(yuvFrm);
                    av_frame_unref(yuvFrm);
                    ++frameIdx;
                    if (frameIdx > maxFrame) break;
                }
            }
            av_packet_unref(pkt);
        }

        // Flush decoder for any trailing buffered frames (only if still needed).
        if (frameIdx <= maxFrame) {
            avcodec_send_packet(decCtx, nullptr);
            while (avcodec_receive_frame(decCtx, yuvFrm) == 0) {
                processIfWanted(yuvFrm);
                av_frame_unref(yuvFrm);
                ++frameIdx;
                if (frameIdx > maxFrame) break;
            }
        }
    }

done:
    if (swsCtx)  sws_freeContext(swsCtx);
    if (pkt)     av_packet_free(&pkt);
    if (grayBuf) av_free(grayBuf);
    if (grayFrm) av_frame_free(&grayFrm);
    if (yuvFrm)  av_frame_free(&yuvFrm);
    if (decCtx)  avcodec_free_context(&decCtx);
    if (fmtCtx)  avformat_close_input(&fmtCtx);
    return out;
}
