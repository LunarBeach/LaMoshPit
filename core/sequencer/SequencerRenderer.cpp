#include "core/sequencer/SequencerRenderer.h"
#include "core/sequencer/SequencerProject.h"
#include "core/sequencer/SequencerTrack.h"
#include "core/sequencer/SequencerClip.h"
#include "core/sequencer/SequencerClipDecoder.h"
#include "core/util/HwEncoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/error.h>
}

#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <algorithm>

namespace sequencer {

namespace {

QString fferr(int e)
{
    char buf[128];
    av_strerror(e, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// Finding the HW encoder is delegated to hwenc::findBestH264Encoder so
// the preference order (nvenc → amf → qsv → mf → libx264-fallback) is
// maintained in a single place, shared with DecodePipeline.

// Apply the most practical subset of GlobalEncodeParams to an
// AVCodecContext + x264 private options dict.  The exotic knobs
// (motion-estimation method, deadzones, psy-rd, etc.) are skipped in
// v1 — they rarely affect a clean sequencer render and the full mapping
// is the mosh editor's job.
//
// Applied:
//   gopSize       → codec->gop_size  (0 → 9999 infinite-GOP shorthand)
//   bFrames       → codec->max_b_frames
//   refFrames     → codec->refs
//   qpOverride    → x264 "qp=<n>"  (forces fixed-QP mode)
//   qpMin/qpMax   → codec->qmin / qmax
//   killIFrames   → caller stamps pict_type=P on every non-first frame
//
// Returns the killIFrames flag so the caller's encode loop can honour it.
bool applyGlobalParamsToX264(AVCodecContext* codec, AVDictionary*& opts,
                             const GlobalEncodeParams& gp)
{
    if (gp.gopSize == 0)           codec->gop_size = 9999;
    else if (gp.gopSize > 0)       codec->gop_size = gp.gopSize;

    if (gp.bFrames >= 0)           codec->max_b_frames = gp.bFrames;
    if (gp.refFrames > 0)          codec->refs         = gp.refFrames;

    if (gp.qpMin >= 0)             codec->qmin = gp.qpMin;
    if (gp.qpMax >= 0)             codec->qmax = gp.qpMax;

    if (gp.qpOverride >= 0) {
        av_dict_set(&opts, "qp",
                    QString::number(gp.qpOverride).toLocal8Bit().constData(), 0);
    } else {
        av_dict_set(&opts, "crf", "18", 0);
    }

    // Scene-cut detection: always off for datamosh-adjacent renders.
    av_dict_set(&opts, "x264-params", "scenecut=0:no-scenecut=1", 0);
    return gp.killIFrames;
}

} // namespace

// =============================================================================

SequencerRenderer::SequencerRenderer(const SequencerProject* project,
                                     Params params, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_params(std::move(params))
{}

SequencerRenderer::~SequencerRenderer() = default;

// =============================================================================
// run — main render loop
// =============================================================================

void SequencerRenderer::run()
{
    // ── Preflight ─────────────────────────────────────────────────────────
    if (!m_project) {
        emit done(false, "No project", m_params.outputPath);
        return;
    }
    if (m_params.sourceTrackIndex < 0
        || m_params.sourceTrackIndex >= m_project->trackCount()) {
        emit done(false, "Track index out of range", m_params.outputPath);
        return;
    }
    const auto& track = m_project->track(m_params.sourceTrackIndex);
    if (track.clips.isEmpty()) {
        emit done(false, "Chosen track is empty", m_params.outputPath);
        return;
    }

    // Resolve the actual render range.
    const Tick totalTrack = track.totalDurationTicks();
    Tick rStart = std::max<Tick>(0, m_params.rangeStartTicks);
    Tick rEnd   = (m_params.rangeEndTicks > 0)
                  ? std::min<Tick>(m_params.rangeEndTicks, totalTrack)
                  : totalTrack;
    if (rEnd <= rStart) {
        emit done(false, "Empty render range", m_params.outputPath);
        return;
    }

    // Frame dimensions — taken from the FIRST clip's source so we have
    // something concrete to lock to.  Mid-sequence resolution changes
    // are handled by swscale rescaling every frame to these dims.
    int outW = 0, outH = 0;
    {
        SequencerClipDecoder probe;
        if (!probe.open(track.clips.first())) {
            emit done(false, "Could not probe first clip: " + probe.lastError(),
                      m_params.outputPath);
            return;
        }
        QImage firstImg; Tick firstTick = 0;
        if (!probe.pullFrame(firstImg, firstTick, true) || firstImg.isNull()) {
            emit done(false, "Could not decode first clip", m_params.outputPath);
            return;
        }
        outW = firstImg.width();
        outH = firstImg.height();
    }

    // Output framerate → output timebase (1/fps).
    const AVRational fpsRat = m_project->outputFrameRate();
    const AVRational outTb  = { fpsRat.den, fpsRat.num };    // 1/fps

    // ── Output container ──────────────────────────────────────────────────
    AVFormatContext* fmt = nullptr;
    int err = avformat_alloc_output_context2(&fmt, nullptr, nullptr,
        m_params.outputPath.toUtf8().constData());
    if (err < 0 || !fmt) {
        emit done(false, "alloc output ctx: " + fferr(err), m_params.outputPath);
        return;
    }

    // ── Encoder selection ────────────────────────────────────────────────
    const AVCodec* enc = nullptr;
    QString hwName;
    bool usingHardware = false;
    if (m_params.encoderMode == EncoderMode::Hardware) {
        enc = hwenc::findBestH264Encoder(&hwName);
        if (enc) {
            usingHardware = true;
            qInfo() << "[Renderer] Using HW encoder:" << hwName;
        } else {
            qWarning() << "[Renderer] No HW H.264 encoder found, falling back to libx264";
        }
    }
    if (!enc) enc = avcodec_find_encoder_by_name("libx264");
    if (!enc) enc = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!enc) {
        avformat_free_context(fmt);
        emit done(false, "No H.264 encoder available", m_params.outputPath);
        return;
    }

    AVStream* vs = avformat_new_stream(fmt, enc);
    AVCodecContext* codec = avcodec_alloc_context3(enc);
    if (!vs || !codec) {
        if (codec) avcodec_free_context(&codec);
        if (fmt)   avformat_free_context(fmt);
        emit done(false, "alloc stream/codec", m_params.outputPath);
        return;
    }

    codec->width       = outW;
    codec->height      = outH;
    codec->time_base   = outTb;
    codec->framerate   = fpsRat;
    codec->pix_fmt     = usingHardware ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
    codec->gop_size    = 12;
    codec->max_b_frames= 3;
    if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
        codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary* opts = nullptr;
    bool killIFrames = false;
    if (m_params.encoderMode == EncoderMode::Libx264FromGlobal && !usingHardware) {
        killIFrames = applyGlobalParamsToX264(codec, opts, m_params.globalParams);
    } else if (!usingHardware) {
        // Libx264Default — solid defaults.
        av_dict_set(&opts, "crf", "18", 0);
        av_dict_set(&opts, "x264-params", "scenecut=0:no-scenecut=1", 0);
    } else {
        // Hardware defaults — pick sensible generic quality presets.
        av_dict_set(&opts, "preset", "medium", 0);
        // NVENC/AMF understand cq, QSV uses global_quality; set both safely.
        av_dict_set(&opts, "cq", "20", 0);
    }

    err = avcodec_open2(codec, enc, &opts);
    if (opts) av_dict_free(&opts);
    if (err < 0) {
        avcodec_free_context(&codec);
        avformat_free_context(fmt);
        emit done(false, "encoder open failed: " + fferr(err),
                  m_params.outputPath);
        return;
    }
    avcodec_parameters_from_context(vs->codecpar, codec);
    vs->time_base = outTb;

    if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
        err = avio_open(&fmt->pb,
                        m_params.outputPath.toUtf8().constData(),
                        AVIO_FLAG_WRITE);
        if (err < 0) {
            avcodec_free_context(&codec);
            avformat_free_context(fmt);
            emit done(false, "Could not open output file: " + fferr(err),
                      m_params.outputPath);
            return;
        }
    }

    err = avformat_write_header(fmt, nullptr);
    if (err < 0) {
        if (!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
        avcodec_free_context(&codec);
        avformat_free_context(fmt);
        emit done(false, "write_header failed: " + fferr(err),
                  m_params.outputPath);
        return;
    }

    // ── Encode loop ──────────────────────────────────────────────────────
    SwsContext* sws = nullptr;
    int swsSrcW = 0, swsSrcH = 0, swsSrcFmt = -1;
    auto ensureSws = [&](int w, int h, int srcFmt) -> bool {
        if (sws && swsSrcW == w && swsSrcH == h && swsSrcFmt == srcFmt) return true;
        if (sws) { sws_freeContext(sws); sws = nullptr; }
        const AVPixelFormat dstFmt = usingHardware ? AV_PIX_FMT_NV12
                                                   : AV_PIX_FMT_YUV420P;
        sws = sws_getContext(w, h, static_cast<AVPixelFormat>(srcFmt),
                             outW, outH, dstFmt,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) return false;
        swsSrcW = w; swsSrcH = h; swsSrcFmt = srcFmt;
        return true;
    };

    AVFrame* encFrame = av_frame_alloc();
    encFrame->format = codec->pix_fmt;
    encFrame->width  = outW;
    encFrame->height = outH;
    err = av_frame_get_buffer(encFrame, 32);
    if (err < 0) {
        av_frame_free(&encFrame);
        avcodec_free_context(&codec);
        if (!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
        avformat_free_context(fmt);
        emit done(false, "frame buffer alloc failed: " + fferr(err),
                  m_params.outputPath);
        return;
    }

    AVPacket* pkt = av_packet_alloc();
    int64_t outputFrameIdx = 0;

    // Estimate total frames for progress reporting.
    const double rangeSec = ticksToSeconds(rEnd - rStart);
    const int totalFrames = static_cast<int>(rangeSec * av_q2d(fpsRat) + 0.5);

    bool aborted = false;
    QString abortMsg;

    // Helper to drain the encoder and mux packets.
    auto drainEncoder = [&](bool flushing) -> bool {
        while (true) {
            int r = avcodec_receive_packet(codec, pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) { abortMsg = "receive_packet: " + fferr(r); return false; }
            pkt->stream_index = vs->index;
            av_packet_rescale_ts(pkt, codec->time_base, vs->time_base);
            int w = av_interleaved_write_frame(fmt, pkt);
            av_packet_unref(pkt);
            if (w < 0) { abortMsg = "write_frame: " + fferr(w); return false; }
        }
        (void)flushing;
    };

    // Walk every clip that has any overlap with the render range.
    for (int ci = 0; ci < track.clips.size() && !aborted; ++ci) {
        const auto& clip = track.clips[ci];
        const Tick clipStart = clip.timelineStartTicks;
        const Tick clipEnd   = clip.timelineEndTicks();
        if (clipEnd <= rStart) continue;    // wholly before range
        if (clipStart >= rEnd) break;       // wholly after range — track is ordered

        SequencerClipDecoder dec;
        if (!dec.open(clip)) {
            qWarning() << "[Renderer] open clip failed:" << dec.lastError();
            continue;   // skip this clip; don't abort the whole render
        }

        const Tick effStart = std::max(clipStart, rStart);
        dec.seekToMasterTick(effStart);

        while (!aborted) {
            QImage bgra;
            Tick   gotTick = 0;
            if (!dec.pullFrame(bgra, gotTick, true)) break;   // EOF or error
            if (gotTick < effStart) continue;                 // pre-roll tail
            if (gotTick >= std::min(clipEnd, rEnd)) break;

            // Feed the BGRA QImage into swscale → YUV420P/NV12 AVFrame.
            if (!ensureSws(bgra.width(), bgra.height(), AV_PIX_FMT_BGRA)) {
                aborted = true; abortMsg = "sws_getContext failed"; break;
            }
            err = av_frame_make_writable(encFrame);
            if (err < 0) { aborted = true; abortMsg = "frame_make_writable"; break; }

            const uint8_t* src[4] = {
                bgra.constBits(), nullptr, nullptr, nullptr };
            int srcStride[4] = {
                static_cast<int>(bgra.bytesPerLine()), 0, 0, 0 };
            sws_scale(sws, src, srcStride, 0, bgra.height(),
                      encFrame->data, encFrame->linesize);

            encFrame->pts       = outputFrameIdx;
            encFrame->pkt_dts   = outputFrameIdx;
            // Kill-I-frames: every frame except the first pretends to be a
            // P-frame so x264 doesn't insert IDRs anywhere in the GOP.
            if (killIFrames && outputFrameIdx > 0) {
                encFrame->pict_type = AV_PICTURE_TYPE_P;
            } else {
                encFrame->pict_type = AV_PICTURE_TYPE_NONE;
            }

            err = avcodec_send_frame(codec, encFrame);
            if (err < 0) {
                aborted = true; abortMsg = "send_frame: " + fferr(err); break;
            }
            if (!drainEncoder(false)) { aborted = true; break; }

            ++outputFrameIdx;
            if ((outputFrameIdx % 4) == 0) {
                emit progress(static_cast<int>(outputFrameIdx), totalFrames);
            }
        }
    }

    // Flush encoder.
    if (!aborted) {
        avcodec_send_frame(codec, nullptr);
        drainEncoder(true);
    }

    av_write_trailer(fmt);

    // ── Cleanup ───────────────────────────────────────────────────────────
    if (sws)      sws_freeContext(sws);
    if (pkt)      av_packet_free(&pkt);
    if (encFrame) av_frame_free(&encFrame);
    avcodec_free_context(&codec);
    if (!(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
    avformat_free_context(fmt);

    if (aborted) {
        emit done(false, abortMsg.isEmpty() ? "Render aborted" : abortMsg,
                  m_params.outputPath);
        return;
    }

    emit progress(static_cast<int>(outputFrameIdx), totalFrames);
    emit done(true, QString(), m_params.outputPath);
}

} // namespace sequencer
