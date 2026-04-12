#include "FrameTransformer.h"

#include "core/logger/ControlLogger.h"

#include <QFile>
#include <QDebug>
#include <QSet>
#include <cstdlib>   // std::rand()

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>
#include <libavutil/frame.h>
}

FrameTransformerWorker::FrameTransformerWorker(const QString& videoPath,
                                               const QVector<int>& frameIndices,
                                               TargetType targetType,
                                               int totalFrameCount,
                                               const QVector<char>& origFrameTypes,
                                               const MBEditMap&     mbEdits,
                                               const GlobalEncodeParams& globalParams,
                                               QObject* parent)
    : QObject(parent)
    , m_videoPath(videoPath)
    , m_frameIndices(frameIndices)
    , m_targetType(targetType)
    , m_totalFrames(totalFrameCount)
    , m_origFrameTypes(origFrameTypes)
    , m_mbEdits(mbEdits)
    , m_globalParams(globalParams)
{}

// ── Helper: map analysis type char → AVPictureType ───────────────────────────
static AVPictureType pictTypeFromChar(char c)
{
    switch (c) {
    case 'I': return AV_PICTURE_TYPE_I;
    case 'P': return AV_PICTURE_TYPE_P;
    case 'B': return AV_PICTURE_TYPE_B;
    default:  return AV_PICTURE_TYPE_NONE;
    }
}

// ── Helper: apply forced frame type to a decoded AVFrame ─────────────────────
static void applyForceType(AVFrame* frame, FrameTransformerWorker::TargetType type)
{
    switch (type) {
    case FrameTransformerWorker::ForceI:
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->flags    |= AV_FRAME_FLAG_KEY;   // request IDR
        break;
    case FrameTransformerWorker::ForceP:
        frame->pict_type = AV_PICTURE_TYPE_P;
        frame->flags    &= ~AV_FRAME_FLAG_KEY;
        break;
    case FrameTransformerWorker::ForceB:
        frame->pict_type = AV_PICTURE_TYPE_B;
        frame->flags    &= ~AV_FRAME_FLAG_KEY;
        break;
    default: break;
    }
}

// ── MB edit helpers ───────────────────────────────────────────────────────────

// Expand a set of MB indices by 'radius' MBs in every direction (square bloom).
// Used to apply the "spill radius" effect beyond the painted selection.
static QSet<int> expandedMBs(const QSet<int>& original,
                              int radius, int mbCols, int mbRows)
{
    if (radius <= 0) return original;
    QSet<int> result;
    for (int mbIdx : original) {
        int row = mbIdx / mbCols;
        int col = mbIdx % mbCols;
        for (int dr = -radius; dr <= radius; ++dr) {
            for (int dc = -radius; dc <= radius; ++dc) {
                int r = row + dr, c = col + dc;
                if (r >= 0 && r < mbRows && c >= 0 && c < mbCols)
                    result.insert(r * mbCols + c);
            }
        }
    }
    return result;
}

// Attach AVRegionOfInterest side data so the encoder applies per-MB QP offsets.
// qoffset = {qpDelta, 51} maps our ±51 range to roughly real QP units.
static void applyQPROI(AVFrame* frame,
                       const QSet<int>& selectedMBs, int qpDelta, int mbCols)
{
    if (selectedMBs.isEmpty() || qpDelta == 0) return;

    QVector<AVRegionOfInterest> rois;
    rois.reserve(selectedMBs.size());
    for (int mbIdx : selectedMBs) {
        int row = mbIdx / mbCols;
        int col = mbIdx % mbCols;
        AVRegionOfInterest roi = {};
        roi.self_size = sizeof(AVRegionOfInterest);
        roi.top    = row * 16;
        roi.bottom = roi.top  + 16;
        roi.left   = col * 16;
        roi.right  = roi.left + 16;
        roi.qoffset = AVRational{qpDelta, 51};
        rois.push_back(roi);
    }

    size_t sz = sizeof(AVRegionOfInterest) * (size_t)rois.size();
    AVFrameSideData* sd = av_frame_new_side_data(
        frame, AV_FRAME_DATA_REGIONS_OF_INTEREST, sz);
    if (sd) memcpy(sd->data, rois.constData(), sz);
}

// Replace Y+UV pixels in selected MBs with content from 'src' shifted by
// (driftX, driftY).  Encoder finds perfect prediction there → forces those MVs.
static void applyMVPixelDrift(AVFrame* dst,
                               const QSet<int>& selectedMBs,
                               int driftX, int driftY,
                               int mbCols, const AVFrame* src)
{
    if (!src || selectedMBs.isEmpty() || (driftX == 0 && driftY == 0)) return;
    if (av_frame_make_writable(dst) < 0) return;

    const int dstW = dst->width, dstH = dst->height;
    const int srcW = src->width, srcH = src->height;

    for (int mbIdx : selectedMBs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;

        // Y (16×16)
        int dstBX = mbCol * 16, dstBY = mbRow * 16;
        int srcBX = dstBX + driftX, srcBY = dstBY + driftY;
        for (int y = 0; y < 16; ++y) {
            int dy = dstBY + y, sy = srcBY + y;
            if (dy >= dstH || sy < 0 || sy >= srcH) continue;
            for (int x = 0; x < 16; ++x) {
                int dx = dstBX + x, sx = srcBX + x;
                if (dx >= dstW || sx < 0 || sx >= srcW) continue;
                dst->data[0][dy * dst->linesize[0] + dx] =
                    src->data[0][sy * src->linesize[0] + sx];
            }
        }

        // UV (8×8 each, 4:2:0)
        int dstUX = mbCol * 8, dstUY = mbRow * 8;
        int srcUX = dstUX + driftX / 2, srcUY = dstUY + driftY / 2;
        const int uvDstW = dstW / 2, uvDstH = dstH / 2;
        const int uvSrcW = srcW / 2, uvSrcH = srcH / 2;
        for (int plane = 1; plane <= 2; ++plane) {
            for (int y = 0; y < 8; ++y) {
                int dy = dstUY + y, sy = srcUY + y;
                if (dy >= uvDstH || sy < 0 || sy >= uvSrcH) continue;
                for (int x = 0; x < 8; ++x) {
                    int dx = dstUX + x, sx = srcUX + x;
                    if (dx >= uvDstW || sx < 0 || sx >= uvSrcW) continue;
                    dst->data[plane][dy * dst->linesize[plane] + dx] =
                        src->data[plane][sy * src->linesize[plane] + sx];
                }
            }
        }
    }
}

// Blend selected-MB pixels toward 'src' pixels at the SAME spatial position
// (no spatial shift).  ghostBlend=100 → full replacement by the reference frame;
// ghostBlend=0 → no change.  Creates a temporal echo / ghost artifact.
static void applyGhostBlend(AVFrame* dst,
                             const QSet<int>& selectedMBs,
                             int blendPct, int mbCols, const AVFrame* src)
{
    if (!src || selectedMBs.isEmpty() || blendPct <= 0) return;
    if (av_frame_make_writable(dst) < 0) return;

    const int dstW = dst->width, dstH = dst->height;
    const int srcW = src->width, srcH = src->height;
    const int inv  = 100 - blendPct;

    for (int mbIdx : selectedMBs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;

        // Y (16×16)
        for (int y = 0; y < 16; ++y) {
            int py = mbRow * 16 + y;
            if (py >= dstH) break;
            for (int x = 0; x < 16; ++x) {
                int px = mbCol * 16 + x;
                if (px >= dstW) break;
                int cur = dst->data[0][py * dst->linesize[0] + px];
                int ref = (py < srcH && px < srcW)
                          ? src->data[0][py * src->linesize[0] + px] : cur;
                dst->data[0][py * dst->linesize[0] + px] =
                    (uint8_t)((cur * inv + ref * blendPct) / 100);
            }
        }

        // UV (8×8 each)
        const int uvDstW = dstW / 2, uvDstH = dstH / 2;
        const int uvSrcW = srcW / 2, uvSrcH = srcH / 2;
        for (int plane = 1; plane <= 2; ++plane) {
            for (int y = 0; y < 8; ++y) {
                int py = mbRow * 8 + y;
                if (py >= uvDstH) break;
                for (int x = 0; x < 8; ++x) {
                    int px = mbCol * 8 + x;
                    if (px >= uvDstW) break;
                    int cur = dst->data[plane][py * dst->linesize[plane] + px];
                    int ref = (py < uvSrcH && px < uvSrcW)
                              ? src->data[plane][py * src->linesize[plane] + px] : cur;
                    dst->data[plane][py * dst->linesize[plane] + px] =
                        (uint8_t)((cur * inv + ref * blendPct) / 100);
                }
            }
        }
    }
}

// Modify Y-plane pixels: random noise injection, DC offset, and inversion blend.
static void applyLumaCorruption(AVFrame* frame,
                                 const QSet<int>& selectedMBs,
                                 int noiseLevel, int pixelOffset,
                                 int invertPct, int mbCols)
{
    if (selectedMBs.isEmpty()) return;
    if (noiseLevel == 0 && pixelOffset == 0 && invertPct == 0) return;
    if (av_frame_make_writable(frame) < 0) return;

    const int W = frame->width, H = frame->height;

    for (int mbIdx : selectedMBs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        for (int y = 0; y < 16; ++y) {
            int py = mbRow * 16 + y;
            if (py >= H) break;
            for (int x = 0; x < 16; ++x) {
                int px = mbCol * 16 + x;
                if (px >= W) break;
                int v = frame->data[0][py * frame->linesize[0] + px];

                if (pixelOffset != 0)
                    v = qBound(0, v + pixelOffset, 255);

                if (noiseLevel > 0) {
                    int noise = (std::rand() % (2 * noiseLevel + 1)) - noiseLevel;
                    v = qBound(0, v + noise, 255);
                }

                if (invertPct > 0)
                    v = (v * (100 - invertPct) + (255 - v) * invertPct) / 100;

                frame->data[0][py * frame->linesize[0] + px] = (uint8_t)v;
            }
        }
    }
}

// Modify U+V planes: independent spatial drift from reference, plus DC offset.
static void applyChromaCorruption(AVFrame* dst,
                                   const QSet<int>& selectedMBs,
                                   int driftX, int driftY, int dcOffset,
                                   int mbCols, const AVFrame* src)
{
    if (selectedMBs.isEmpty()) return;
    if (driftX == 0 && driftY == 0 && dcOffset == 0) return;
    if (av_frame_make_writable(dst) < 0) return;

    const int uvDstW = dst->width / 2, uvDstH = dst->height / 2;
    const int uvSrcW = src ? src->width  / 2 : 0;
    const int uvSrcH = src ? src->height / 2 : 0;
    // Half-pixel shift for 4:2:0
    const int udX = driftX / 2, udY = driftY / 2;

    for (int mbIdx : selectedMBs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int dstUX = mbCol * 8, dstUY = mbRow * 8;

        for (int plane = 1; plane <= 2; ++plane) {
            for (int y = 0; y < 8; ++y) {
                int dy = dstUY + y;
                if (dy >= uvDstH) break;
                for (int x = 0; x < 8; ++x) {
                    int dx = dstUX + x;
                    if (dx >= uvDstW) break;
                    int v;
                    if (src && (driftX != 0 || driftY != 0)) {
                        int sx = dx + udX, sy = dy + udY;
                        v = (sx >= 0 && sx < uvSrcW && sy >= 0 && sy < uvSrcH)
                            ? src->data[plane][sy * src->linesize[plane] + sx]
                            : dst->data[plane][dy * dst->linesize[plane] + dx];
                    } else {
                        v = dst->data[plane][dy * dst->linesize[plane] + dx];
                    }
                    if (dcOffset != 0)
                        v = qBound(0, v + dcOffset, 255);
                    dst->data[plane][dy * dst->linesize[plane] + dx] = (uint8_t)v;
                }
            }
        }
    }
}

// ── Additional pixel-domain helpers ──────────────────────────────────────────

// Ghost blend using a box-blurred neighbourhood of the reference.
// sampleRadius > 0 → each output pixel is the blend target averaged over a
// (2r+1)×(2r+1) window of reference pixels instead of the exact pixel.
// Creates a "diffuse temporal smear" rather than a sharp temporal echo.
static void applyGhostBlendBlurred(AVFrame* dst,
                                    const QSet<int>& selectedMBs,
                                    int blendPct, int mbCols,
                                    const AVFrame* src, int radius)
{
    if (!src || selectedMBs.isEmpty() || blendPct <= 0 || radius <= 0) return;
    if (av_frame_make_writable(dst) < 0) return;
    const int inv = 100 - blendPct;
    for (int mbIdx : selectedMBs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        // Y (16×16)
        for (int y = 0; y < 16; ++y) {
            int py = mbRow * 16 + y;
            if (py >= dst->height) break;
            for (int x = 0; x < 16; ++x) {
                int px = mbCol * 16 + x;
                if (px >= dst->width) break;
                long sum = 0; int cnt = 0;
                for (int ky = -radius; ky <= radius; ++ky) {
                    int sy = py + ky;
                    if (sy < 0 || sy >= src->height) continue;
                    for (int kx = -radius; kx <= radius; ++kx) {
                        int sx = px + kx;
                        if (sx < 0 || sx >= src->width) continue;
                        sum += src->data[0][sy * src->linesize[0] + sx];
                        ++cnt;
                    }
                }
                int refV = cnt > 0 ? (int)(sum / cnt)
                         : src->data[0][py * src->linesize[0] + px];
                int cur = dst->data[0][py * dst->linesize[0] + px];
                dst->data[0][py * dst->linesize[0] + px] =
                    (uint8_t)((cur * inv + refV * blendPct) / 100);
            }
        }
    }
}

// Scatter reference pixels: for each pixel, source from ref at a random offset
// of up to ±scatterPx.  Creates a "shattered glass" temporal artifact.
static void applyRefScatter(AVFrame* dst,
                             const QSet<int>& selectedMBs,
                             int scatterPx, int mbCols,
                             const AVFrame* src)
{
    if (!src || selectedMBs.isEmpty() || scatterPx <= 0) return;
    if (av_frame_make_writable(dst) < 0) return;
    for (int mbIdx : selectedMBs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        // Y
        for (int y = 0; y < 16; ++y) {
            int dy = mbRow * 16 + y;
            if (dy >= dst->height) break;
            for (int x = 0; x < 16; ++x) {
                int dx = mbCol * 16 + x;
                if (dx >= dst->width) break;
                int ox = (std::rand() % (2 * scatterPx + 1)) - scatterPx;
                int oy = (std::rand() % (2 * scatterPx + 1)) - scatterPx;
                int sx = dx + ox, sy = dy + oy;
                if (sx >= 0 && sx < src->width && sy >= 0 && sy < src->height)
                    dst->data[0][dy * dst->linesize[0] + dx] =
                        src->data[0][sy * src->linesize[0] + sx];
            }
        }
        // UV (half resolution)
        int dstUX = mbCol * 8, dstUY = mbRow * 8;
        int sp = qMax(1, scatterPx / 2);
        for (int plane = 1; plane <= 2; ++plane) {
            for (int y = 0; y < 8; ++y) {
                int dy = dstUY + y;
                if (dy >= dst->height / 2) break;
                for (int x = 0; x < 8; ++x) {
                    int dx = dstUX + x;
                    if (dx >= dst->width / 2) break;
                    int ox = (std::rand() % (2 * sp + 1)) - sp;
                    int oy = (std::rand() % (2 * sp + 1)) - sp;
                    int sx = dx + ox, sy = dy + oy;
                    if (sx >= 0 && sx < src->width / 2 &&
                        sy >= 0 && sy < src->height / 2)
                        dst->data[plane][dy * dst->linesize[plane] + dx] =
                            src->data[plane][sy * src->linesize[plane] + sx];
                }
            }
        }
    }
}

// Flatten each MB's luma toward its own spatial average.
// blendPct=100 → solid colour block; 50 → half-way to flat.
static void applyBlockFlatten(AVFrame* frame,
                               const QSet<int>& selectedMBs,
                               int blendPct, int mbCols)
{
    if (selectedMBs.isEmpty() || blendPct <= 0) return;
    if (av_frame_make_writable(frame) < 0) return;
    const int W = frame->width, H = frame->height;
    const int inv = 100 - blendPct;
    for (int mbIdx : selectedMBs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        long sum = 0; int cnt = 0;
        for (int y = 0; y < 16; ++y) {
            int py = mbRow * 16 + y; if (py >= H) break;
            for (int x = 0; x < 16; ++x) {
                int px = mbCol * 16 + x; if (px >= W) break;
                sum += frame->data[0][py * frame->linesize[0] + px]; ++cnt;
            }
        }
        int avg = cnt > 0 ? (int)(sum / cnt) : 128;
        for (int y = 0; y < 16; ++y) {
            int py = mbRow * 16 + y; if (py >= H) break;
            for (int x = 0; x < 16; ++x) {
                int px = mbCol * 16 + x; if (px >= W) break;
                int v = frame->data[0][py * frame->linesize[0] + px];
                frame->data[0][py * frame->linesize[0] + px] =
                    (uint8_t)((v * inv + avg * blendPct) / 100);
            }
        }
    }
}

// Independent DC offsets for U (Cb) and V (Cr) planes separately.
static void applyColorTwist(AVFrame* frame,
                             const QSet<int>& selectedMBs,
                             int twistU, int twistV, int mbCols)
{
    if (selectedMBs.isEmpty() || (twistU == 0 && twistV == 0)) return;
    if (av_frame_make_writable(frame) < 0) return;
    const int uvW = frame->width / 2, uvH = frame->height / 2;
    for (int mbIdx : selectedMBs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int dstUX = mbCol * 8, dstUY = mbRow * 8;
        for (int y = 0; y < 8; ++y) {
            int py = dstUY + y; if (py >= uvH) break;
            for (int x = 0; x < 8; ++x) {
                int px = dstUX + x; if (px >= uvW) break;
                if (twistU != 0) {
                    int v = frame->data[1][py * frame->linesize[1] + px] + twistU;
                    frame->data[1][py * frame->linesize[1] + px] =
                        (uint8_t)qBound(0, v, 255);
                }
                if (twistV != 0) {
                    int v = frame->data[2][py * frame->linesize[2] + px] + twistV;
                    frame->data[2][py * frame->linesize[2] + px] =
                        (uint8_t)qBound(0, v, 255);
                }
            }
        }
    }
}

// Applies all MB edits for the given frame index in-place.
// Must be called just before encodeAndDrain.
static void applyMBEdits(AVFrame* frame, int frameIdx,
                          int mbCols, int mbRows,
                          const QVector<AVFrame*>& refBuf,
                          const MBEditMap& edits)
{
    if (edits.isEmpty()) return;
    auto it = edits.constFind(frameIdx);
    if (it == edits.constEnd()) return;
    const FrameMBParams& p = *it;

    // Log the complete parameter state for this frame before any pixel work.
    ControlLogger::instance().logFrameEditApplied(frameIdx, p, mbCols, mbRows);

    // Resolve the working MB set.
    // Empty selectedMBs = global mode: every MB in the frame is affected.
    QSet<int> fullFrame;
    const QSet<int>* baseMBs = &p.selectedMBs;
    if (p.selectedMBs.isEmpty()) {
        fullFrame.reserve(mbCols * mbRows);
        for (int i = 0; i < mbCols * mbRows; ++i) fullFrame.insert(i);
        baseMBs = &fullFrame;
    }

    // Expand selection by spill radius (blast radius — this MB infects neighbours)
    const QSet<int>& mbs = (p.spillRadius > 0)
        ? expandedMBs(*baseMBs, p.spillRadius, mbCols, mbRows)
        : *baseMBs;

    // Resolve reference frame (shared by MV drift, ghost blend, chroma drift)
    const AVFrame* ref = nullptr;
    if (p.refDepth > 0) {
        int bufIdx = p.refDepth - 1;
        if (bufIdx < refBuf.size()) ref = refBuf[bufIdx];
    }

    // 1. Quantisation ROI (side data — no pixel modification)
    if (p.qpDelta != 0)
        applyQPROI(frame, mbs, p.qpDelta, mbCols);

    // 2. MV pixel drift — replace Y+UV with shifted reference content.
    //    mvAmplify is a gain multiplier on top of mvDriftX/Y.
    {
        const int amp       = qMax(1, p.mvAmplify);
        const int effDriftX = p.mvDriftX * amp;
        const int effDriftY = p.mvDriftY * amp;
        if (ref && (effDriftX != 0 || effDriftY != 0))
            applyMVPixelDrift(frame, mbs, effDriftX, effDriftY, mbCols, ref);
    }

    // 3. Ghost blend — mix current frame toward reference at same position.
    //    sampleRadius > 0 → use a blurred neighbourhood of the reference
    //    (incoming influence radius: larger = drawn from a wider area).
    if (ref && p.ghostBlend > 0) {
        if (p.sampleRadius > 0)
            applyGhostBlendBlurred(frame, mbs, p.ghostBlend, mbCols, ref,
                                   p.sampleRadius * 16); // MB-units → pixels
        else
            applyGhostBlend(frame, mbs, p.ghostBlend, mbCols, ref);
    }

    // 4. Reference scatter — randomly sample from ref at offset positions
    if (ref && p.refScatter > 0)
        applyRefScatter(frame, mbs, p.refScatter, mbCols, ref);

    // 5. Block flatten — collapse MB luma toward spatial average
    if (p.blockFlatten > 0)
        applyBlockFlatten(frame, mbs, p.blockFlatten, mbCols);

    // 6. Luma corruption — noise / DC offset / inversion (Y plane)
    if (p.noiseLevel != 0 || p.pixelOffset != 0 || p.invertLuma != 0)
        applyLumaCorruption(frame, mbs, p.noiseLevel,
                             p.pixelOffset, p.invertLuma, mbCols);

    // 7. Chroma corruption — independent UV drift + DC offset (both planes)
    if (p.chromaDriftX != 0 || p.chromaDriftY != 0 || p.chromaOffset != 0)
        applyChromaCorruption(frame, mbs, p.chromaDriftX, p.chromaDriftY,
                               p.chromaOffset, mbCols, ref);

    // 8. Color twist — separate U/V DC offsets (independent hue steering)
    if (p.colorTwistU != 0 || p.colorTwistV != 0)
        applyColorTwist(frame, mbs, p.colorTwistU, p.colorTwistV, mbCols);
}

// ── Main re-encode work ────────────────────────────────────────────────────
void FrameTransformerWorker::run()
{
    // Log Apply start — records total frames and how many carry edits.
    ControlLogger::instance().logApplyStarted(m_totalFrames, m_mbEdits.size());

    const QString tempPath = m_videoPath + ".xform_tmp.mp4";
    QString errorMsg;
    bool    ok   = false;

    AVFormatContext* ifmt  = nullptr;
    AVFormatContext* ofmt  = nullptr;
    AVCodecContext*  decCtx = nullptr;
    AVCodecContext*  encCtx = nullptr;
    AVPacket* rpkt  = nullptr;   // read packet
    AVPacket* wpkt  = nullptr;   // write/encoded packet
    AVFrame*  frame = nullptr;
    int vsIdx = -1;

    // Ring buffer: refBuf[0] = previous frame (post-edit), refBuf[1] = two back, etc.
    // Frames are stored AFTER pixel edits are applied so the corrupted content
    // is available to cascade entries via ghostBlend/mvDrift in subsequent frames.
    static const int REF_BUF_SIZE = 8;
    QVector<AVFrame*> refBuf(REF_BUF_SIZE, nullptr);

    // ── Open input ─────────────────────────────────────────────────────────
    if (avformat_open_input(&ifmt, m_videoPath.toUtf8().constData(),
                            nullptr, nullptr) < 0) {
        errorMsg = "Cannot open input: " + m_videoPath;
        goto cleanup;
    }
    if (avformat_find_stream_info(ifmt, nullptr) < 0) {
        errorMsg = "avformat_find_stream_info failed";
        goto cleanup;
    }

    for (unsigned i = 0; i < ifmt->nb_streams; i++) {
        if (ifmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vsIdx = (int)i; break;
        }
    }
    if (vsIdx < 0) { errorMsg = "No video stream"; goto cleanup; }

    // ── Open decoder ───────────────────────────────────────────────────────
    {
        AVStream* inStream = ifmt->streams[vsIdx];
        const AVCodec* dec = avcodec_find_decoder(inStream->codecpar->codec_id);
        if (!dec) { errorMsg = "No decoder"; goto cleanup; }
        decCtx = avcodec_alloc_context3(dec);
        if (avcodec_parameters_to_context(decCtx, inStream->codecpar) < 0) goto cleanup;
        if (avcodec_open2(decCtx, dec, nullptr) < 0) {
            errorMsg = "Cannot open decoder"; goto cleanup;
        }
    }

    // ── Open output ────────────────────────────────────────────────────────
    if (avformat_alloc_output_context2(&ofmt, nullptr, nullptr,
                                       tempPath.toUtf8().constData()) < 0) {
        errorMsg = "Cannot alloc output context"; goto cleanup;
    }

    // ── Setup H.264 encoder (same quality as DecodePipeline) ──────────────
    {
        AVStream* inStream = ifmt->streams[vsIdx];
        const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!enc) { errorMsg = "H.264 encoder not found"; goto cleanup; }

        encCtx = avcodec_alloc_context3(enc);
        encCtx->width              = decCtx->width;
        encCtx->height             = decCtx->height;
        encCtx->pix_fmt            = AV_PIX_FMT_YUV420P;
        encCtx->sample_aspect_ratio = decCtx->sample_aspect_ratio;

        // Match the input framerate/timebase
        encCtx->framerate  = inStream->avg_frame_rate;
        encCtx->time_base  = inStream->time_base;
        if (encCtx->framerate.num <= 0 || encCtx->framerate.den <= 0) {
            encCtx->framerate = AVRational{24, 1};
            encCtx->time_base = AVRational{1, 24};
        }

        // ── Rate control ──────────────────────────────────────────────────────
        // CRF 18 = near-lossless by default.  Per-MB QP delta (ROI) works best
        // in CRF mode because the rate controller doesn't compensate elsewhere.
        // If qpOverride >= 0, switch to fixed-CQP mode (no rate control at all).
        encCtx->bit_rate     = 0;
        // When killing I-frames for datamoshing, B-frames must be disabled.
        // x264's B-frame adaptive lookahead (b-adapt=2) can insert I-frames to
        // anchor the B-frame reference chain, even when every individual frame is
        // hinted as AV_PICTURE_TYPE_P.  With bframes=0 x264 encodes in strict
        // decode order with no reordering, so pict_type hints are applied exactly.
        const bool killI = m_globalParams.killIFrames;
        encCtx->max_b_frames = killI ? 0
                             : (m_globalParams.bFrames >= 0) ? m_globalParams.bFrames : 3;

        if (m_globalParams.qpOverride >= 0) {
            av_opt_set(encCtx->priv_data, "qp",
                       QString::number(m_globalParams.qpOverride).toUtf8().constData(), 0);
        } else {
            av_opt_set(encCtx->priv_data, "crf", "18", 0);
        }
        if (m_globalParams.qpMin >= 0)
            encCtx->qmin = m_globalParams.qpMin;
        if (m_globalParams.qpMax >= 0)
            encCtx->qmax = m_globalParams.qpMax;

        // GOP size: default 250, override if requested.
        // gopSize=0 → effective infinite GOP (keyint=9999, scenecut off).
        const int gopSize = (m_globalParams.gopSize == 0) ? 9999
                          : (m_globalParams.gopSize  >  0) ? m_globalParams.gopSize
                          : 250;
        encCtx->gop_size = gopSize;

        if (ofmt->oformat->flags & AVFMT_GLOBALHEADER)
            encCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        // Disable deblocking if requested (preserves block-boundary artifacts).
        if (m_globalParams.noDeblock)
            encCtx->flags &= ~AV_CODEC_FLAG_LOOP_FILTER;

        // ── Build x264-params string from GlobalEncodeParams ─────────────────
        QString x264p;
        // Frame structure
        // min-keyint must match keyint — if min-keyint < keyint, x264 is free to
        // insert keyframes early (on scene transitions etc.) even with scenecut=0.
        // For datamoshing this silently resets the smear wherever x264 decides
        // to place an early keyframe.
        x264p += QString("keyint=%1:min-keyint=%1:scenecut=0:").arg(gopSize);
        {
            // When killing I-frames: force bframes=0, b-adapt=0.
            // x264's B-frame lookahead (b-adapt=2) inserts I-frames to anchor
            // the B-frame reference chain even with scenecut=0 and forced P
            // pict_type hints.  No B-frames = no lookahead = no surprise I-frames.
            int bf = killI ? 0 : (m_globalParams.bFrames >= 0) ? m_globalParams.bFrames : 3;
            int ba = killI ? 0 : (m_globalParams.bAdapt  >= 0) ? m_globalParams.bAdapt  : 2;
            x264p += QString("bframes=%1:b-adapt=%2:").arg(bf).arg(ba);
        }
        if (m_globalParams.refFrames > 0)
            x264p += QString("ref=%1:").arg(m_globalParams.refFrames);
        // Motion estimation
        if (m_globalParams.meMethod >= 0) {
            const char* meNames[] = {"dia","hex","umh","esa","tesa"};
            int mi = qBound(0, m_globalParams.meMethod, 4);
            x264p += QString("me=%1:").arg(meNames[mi]);
        }
        if (m_globalParams.meRange > 0)
            x264p += QString("merange=%1:").arg(m_globalParams.meRange);
        if (m_globalParams.subpelRef >= 0)
            x264p += QString("subme=%1:").arg(m_globalParams.subpelRef);
        // Partitions
        if (m_globalParams.partitionMode >= 0) {
            const char* parts[] = {"none","p8x8","all","all,p4x4,b8x8,i8x8,i4x4"};
            int pi = qBound(0, m_globalParams.partitionMode, 3);
            x264p += QString("partitions=%1:").arg(parts[pi]);
        }
        x264p += QString("8x8dct=%1:").arg(m_globalParams.use8x8DCT ? 1 : 0);
        // B-frame prediction
        if (m_globalParams.directMode >= 0) {
            const char* dmodes[] = {"none","temporal","spatial","auto"};
            int di = qBound(0, m_globalParams.directMode, 3);
            x264p += QString("direct=%1:").arg(dmodes[di]);
        }
        x264p += QString("weightb=%1:").arg(m_globalParams.weightedPredB ? 1 : 0);
        if (m_globalParams.weightedPredP >= 0)
            x264p += QString("weightp=%1:").arg(m_globalParams.weightedPredP);
        // Quantization
        if (m_globalParams.trellis >= 0)
            x264p += QString("trellis=%1:").arg(m_globalParams.trellis);
        if (m_globalParams.noFastPSkip)   x264p += "no-fast-pskip=1:";
        if (m_globalParams.noDctDecimate) x264p += "no-dct-decimate=1:";
        if (m_globalParams.cabacDisable)  x264p += "no-cabac=1:";
        // Deblocking offsets (even when filter is on)
        if (!m_globalParams.noDeblock) {
            int da = qBound(-6, m_globalParams.deblockAlpha, 6);
            int db = qBound(-6, m_globalParams.deblockBeta,  6);
            if (da != 0 || db != 0)
                x264p += QString("deblock=%1,%2:").arg(da).arg(db);
        }
        // Psychovisual
        {
            float psy  = (m_globalParams.psyRD      >= 0.0f) ? m_globalParams.psyRD      : 1.0f;
            float psyt = (m_globalParams.psyTrellis >= 0.0f) ? m_globalParams.psyTrellis : 0.0f;
            x264p += QString("psy-rd=%1,%2:").arg(psy, 0, 'f', 2).arg(psyt, 0, 'f', 2);
        }
        if (m_globalParams.aqMode >= 0)
            x264p += QString("aq-mode=%1:").arg(m_globalParams.aqMode);
        if (m_globalParams.aqStrength >= 0.0f)
            x264p += QString("aq-strength=%1:").arg(m_globalParams.aqStrength, 0, 'f', 2);
        if (m_globalParams.mbTreeDisable) x264p += "no-mbtree=1:";
        if (m_globalParams.rcLookahead >= 0)
            x264p += QString("rc-lookahead=%1:").arg(m_globalParams.rcLookahead);

        // Strip trailing colon
        while (x264p.endsWith(':')) x264p.chop(1);

        av_opt_set(encCtx->priv_data, "x264-params",
                   x264p.toUtf8().constData(), 0);

        if (avcodec_open2(encCtx, enc, nullptr) < 0) {
            errorMsg = "Cannot open encoder"; goto cleanup;
        }

        // Output stream
        AVStream* outStream = avformat_new_stream(ofmt, nullptr);
        avcodec_parameters_from_context(outStream->codecpar, encCtx);
        outStream->time_base     = encCtx->time_base;
        outStream->avg_frame_rate = encCtx->framerate;
    }

    // ── Open output file and write header ──────────────────────────────────
    if (!(ofmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt->pb, tempPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
            errorMsg = "Cannot open output file"; goto cleanup;
        }
    }
    if (avformat_write_header(ofmt, nullptr) < 0) {
        errorMsg = "avformat_write_header failed"; goto cleanup;
    }

    // ── Process frames ─────────────────────────────────────────────────────
    frame = av_frame_alloc();
    rpkt  = av_packet_alloc();
    wpkt  = av_packet_alloc();

    {
        // Compute macroblock grid dimensions for ROI / pixel-drift helpers
        const int mbCols = (encCtx->width  + 15) / 16;
        const int mbRows = (encCtx->height + 15) / 16;

        // ── Temporal cascade pre-expansion ──────────────────────────────────
        // For each edited frame N with cascadeLen > 0, inject synthetic entries
        // for frames N+1 … N+cascadeLen.  Each entry uses ghostBlend pointing at
        // refDepth=1 (the immediately-preceding, now-corrupted, frame) so the
        // damage propagates frame-by-frame rather than healing after one frame.
        //
        // ghostBlend=100 forces the selected MBs to be exact copies of the
        // previous (corrupted) reconstruction.  The encoder finds zero residual
        // and emits skip macroblocks — this is how professional datamoshing
        // achieves indefinite temporal smear.
        //
        // cascadeDecay interpretation (linear fade over cascadeLen frames):
        //   cascadeDecay=0   → constant full intensity throughout cascade
        //   cascadeDecay=50  → effect fades to 50 % intensity at the final frame
        //   cascadeDecay=100 → effect fades linearly to zero at the final frame
        // This means cascadeLen actually controls duration — the cascade runs for
        // exactly cascadeLen frames regardless of the decay setting, which matches
        // user intuition when they set a large cascadeLen expecting a long smear.
        MBEditMap activeEdits = m_mbEdits;
        for (auto it = m_mbEdits.constBegin(); it != m_mbEdits.constEnd(); ++it) {
            const FrameMBParams& src = it.value();
            if (src.cascadeLen <= 0) continue;
            for (int k = 1; k <= src.cascadeLen; ++k) {
                int ci = it.key() + k;
                if (ci >= m_totalFrames) break;
                if (activeEdits.contains(ci)) continue; // honour explicit edits
                // Linear fade: at k=1 intensity ≈ 1.0, at k=cascadeLen it is
                // (1 - cascadeDecay/100).  cascadeDecay=0 → constant smear.
                float f = 1.0f - (src.cascadeDecay / 100.0f)
                               * ((float)k / (float)src.cascadeLen);
                f = qBound(0.0f, f, 1.0f);
                if (f < 0.01f) break; // absolute floor — only hits at cascadeDecay=100

                FrameMBParams c;
                c.selectedMBs  = src.selectedMBs;
                c.spillRadius  = src.spillRadius;
                // Core cascade: copy from previous (corrupted) frame at same position.
                // ghostBlend=100% → zero residual → encoder propagates corruption.
                c.refDepth     = 1;
                c.ghostBlend   = qMin(100, (int)(100.0f * f));
                // Force maximum quantization on every cascade frame.
                // With qpDelta=51 the encoder is forced to the coarsest quantiser
                // and all DCT residuals quantize to zero — the encoder CANNOT add
                // correction residuals, so only the (corrupted) inter-prediction
                // survives in the bitstream.  This is what makes the datamosh
                // smear persist across multiple P-frames instead of healing after
                // one frame.  We still carry the seed's decay-scaled corruption
                // for the other pixel-manipulation passes, but the QP is pinned.
                c.qpDelta      = 51;
                c.noiseLevel   = (int)(src.noiseLevel    * f);
                c.pixelOffset  = (int)(src.pixelOffset   * f);
                c.invertLuma   = (int)(src.invertLuma    * f);
                c.chromaDriftX = (int)(src.chromaDriftX  * f);
                c.chromaDriftY = (int)(src.chromaDriftY  * f);
                c.chromaOffset = (int)(src.chromaOffset  * f);
                // No additional MV drift in cascade (the ghost-copied content
                // already carries the seed frame's drift; adding more compounds
                // the shift unpredictably relative to what the encoder expects).
                c.mvDriftX    = 0;
                c.mvDriftY    = 0;
                c.mvAmplify   = 1;
                c.cascadeLen  = 0;
                c.cascadeDecay = 0;
                activeEdits[ci] = c;
            }
        }

        int frameIdx = 0;
        AVStream* outStream = ofmt->streams[0];

        // Sequential PTS counter — ensures delete/duplicate produce evenly-spaced
        // timestamps.  Without this, deleted frames leave PTS gaps and the player
        // stalls on the last pre-gap frame; duplicated frames share identical PTS
        // values and are collapsed by the player.
        int64_t outFrameCount = 0;
        int64_t frameDuration = 1;
        if (encCtx->framerate.num > 0 && encCtx->framerate.den > 0) {
            frameDuration = av_rescale_q(1,
                AVRational{encCtx->framerate.den, encCtx->framerate.num},
                encCtx->time_base);
            if (frameDuration <= 0) frameDuration = 1;
        }

        // Helper: stamp sequential PTS, encode one frame, drain any output packets.
        // If a spatial mask is set in globalParams, inject a QP ROI for those MBs
        // on every single encoded frame so the global mask acts as a persistent
        // "damage zone" across the entire timeline.
        auto encodeAndDrain = [&](AVFrame* f) {
            if (!m_globalParams.spatialMaskMBs.isEmpty())
                applyQPROI(f, m_globalParams.spatialMaskMBs,
                           m_globalParams.spatialMaskQP, mbCols);
            f->pts = outFrameCount * frameDuration;
            outFrameCount++;
            if (avcodec_send_frame(encCtx, f) == 0) {
                while (avcodec_receive_packet(encCtx, wpkt) == 0) {
                    wpkt->stream_index = 0;
                    av_packet_rescale_ts(wpkt, encCtx->time_base, outStream->time_base);
                    av_interleaved_write_frame(ofmt, wpkt);
                    av_packet_unref(wpkt);
                }
            }
        };

        while (av_read_frame(ifmt, rpkt) >= 0) {
            if (rpkt->stream_index == vsIdx) {
                if (avcodec_send_packet(decCtx, rpkt) == 0) {
                    while (avcodec_receive_frame(decCtx, frame) == 0) {
                        bool inSel = m_frameIndices.contains(frameIdx);

                        // Frame 0 is the stream IDR — no reference exists before it,
                        // so the encoder physically cannot make it P or B.
                        if (inSel && frameIdx == 0 &&
                            (m_targetType == ForceP || m_targetType == ForceB)) {
                            emit warning("Frame 0 is the stream IDR keyframe — it cannot "
                                         "be converted to P or B (no reference frame exists "
                                         "before it). It was left as-is.");
                            inSel = false;
                        }

                        // ── Kill I-frames ────────────────────────────────────
                        // If requested, silently convert every I-frame after frame 0
                        // to a P-frame so the encoder uses inter-prediction everywhere.
                        // Frame count stays identical — only the reference structure changes.
                        if (m_globalParams.killIFrames && frameIdx > 0) {
                            if (frame->pict_type == AV_PICTURE_TYPE_I ||
                                (frame->flags & AV_FRAME_FLAG_KEY)) {
                                frame->pict_type = AV_PICTURE_TYPE_P;
                                frame->flags    &= ~AV_FRAME_FLAG_KEY;
                            }
                        }

                        if (m_targetType <= ForceB) {
                            // ── Force I/P/B ──────────────────────────────
                            if (inSel)
                                applyForceType(frame, m_targetType);
                            else
                                frame->pict_type = AV_PICTURE_TYPE_NONE;
                            applyMBEdits(frame, frameIdx, mbCols, mbRows, refBuf, activeEdits);
                            encodeAndDrain(frame);

                        } else if (m_targetType == DeleteFrames) {
                            // ── Delete ───────────────────────────────────
                            // Selected frames are skipped entirely.
                            // For remaining frames, enforce their original type
                            // from the analysis roster so the encoder recalculates
                            // motion vectors with broken references (datamosh).
                            if (!inSel) {
                                if (frameIdx < m_origFrameTypes.size())
                                    frame->pict_type = pictTypeFromChar(
                                                           m_origFrameTypes[frameIdx]);
                                else
                                    frame->pict_type = AV_PICTURE_TYPE_NONE;
                                applyMBEdits(frame, frameIdx, mbCols, mbRows, refBuf, activeEdits);
                                encodeAndDrain(frame);
                            }
                            // selected frames are simply skipped

                        } else if (m_targetType == MBEditOnly) {
                            // ── MB Edits Only ────────────────────────────
                            // When killIFrames is active, force every non-IDR frame
                            // to AV_PICTURE_TYPE_P so x264 uses inter-prediction
                            // throughout.  Setting NONE here would let x264 insert
                            // I-frames at its own schedule, overriding the killIFrames
                            // request and resetting the datamosh effect at every GOP.
                            // Frame 0 is always left as NONE — it is the mandatory
                            // stream IDR and x264 cannot encode it as P or B.
                            if (m_globalParams.killIFrames && frameIdx > 0)
                                frame->pict_type = AV_PICTURE_TYPE_P;
                            else
                                frame->pict_type = AV_PICTURE_TYPE_NONE;
                            applyMBEdits(frame, frameIdx, mbCols, mbRows, refBuf, activeEdits);
                            encodeAndDrain(frame);

                        } else {
                            // ── Duplicate Left / Right ───────────────────
                            // Apply MB edits BEFORE cloning so the duplicate
                            // inherits both the pixel modifications and ROI
                            // side data.
                            if (!inSel) {
                                frame->pict_type = AV_PICTURE_TYPE_NONE;
                            }
                            applyMBEdits(frame, frameIdx, mbCols, mbRows, refBuf, activeEdits);

                            if (inSel) {
                                AVFrame* dup = av_frame_clone(frame);
                                if (m_targetType == DuplicateLeft) {
                                    encodeAndDrain(dup);    // copy before
                                    encodeAndDrain(frame);  // original
                                } else {
                                    encodeAndDrain(frame);  // original
                                    encodeAndDrain(dup);    // copy after
                                }
                                av_frame_free(&dup);
                            } else {
                                encodeAndDrain(frame);
                            }
                        }

                        // ── Shift ring buffer — store POST-EDIT frame ────────
                        // Cloning AFTER all pixel modifications means refBuf
                        // carries the corrupted content.  Cascade entries that
                        // use ghostBlend(refDepth=1) will therefore blend toward
                        // this corrupted frame, propagating damage forward instead
                        // of healing back to the clean source.
                        AVFrame* bufFrame = av_frame_clone(frame);
                        if (refBuf[REF_BUF_SIZE - 1])
                            av_frame_free(&refBuf[REF_BUF_SIZE - 1]);
                        for (int ri = REF_BUF_SIZE - 1; ri > 0; --ri)
                            refBuf[ri] = refBuf[ri - 1];
                        refBuf[0] = bufFrame;

                        frameIdx++;
                        if (m_totalFrames > 0)
                            emit progress(frameIdx, m_totalFrames);

                        av_frame_unref(frame);
                    }
                }
            }
            av_packet_unref(rpkt);
        }

        // Flush encoder
        avcodec_send_frame(encCtx, nullptr);
        while (avcodec_receive_packet(encCtx, wpkt) == 0) {
            wpkt->stream_index = 0;
            av_packet_rescale_ts(wpkt, encCtx->time_base, outStream->time_base);
            av_interleaved_write_frame(ofmt, wpkt);
            av_packet_unref(wpkt);
        }
    }

    av_write_trailer(ofmt);
    ok = true;

cleanup:
    // Free ring buffer frames
    for (int i = 0; i < refBuf.size(); ++i)
        if (refBuf[i]) av_frame_free(&refBuf[i]);

    if (wpkt)   av_packet_free(&wpkt);
    if (rpkt)   av_packet_free(&rpkt);
    if (frame)  av_frame_free(&frame);
    if (decCtx) avcodec_free_context(&decCtx);
    if (encCtx) avcodec_free_context(&encCtx);
    if (ofmt && !(ofmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt->pb);
    if (ofmt)   avformat_free_context(ofmt);
    if (ifmt)   avformat_close_input(&ifmt);

    if (ok) {
        // Atomic replace: remove original, rename temp
        if (!QFile::remove(m_videoPath)) {
            ok = false;
            errorMsg = "Cannot remove original file for replacement";
            QFile::remove(tempPath);
        } else if (!QFile::rename(tempPath, m_videoPath)) {
            ok = false;
            errorMsg = "Cannot rename temp file to original path";
        }
    } else {
        QFile::remove(tempPath);
    }

    ControlLogger::instance().logApplyCompleted(ok);
    emit done(ok, errorMsg);
}
