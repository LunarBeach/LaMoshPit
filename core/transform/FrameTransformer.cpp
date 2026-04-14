#include "FrameTransformer.h"

#include "core/logger/ControlLogger.h"

#include <QFile>
#include <QDebug>
#include <QSet>
#include <QByteArray>
#include <cstdlib>   // std::rand(), malloc/free
#include <cstring>   // memcpy, memset
#include <cmath>     // sin, cos, round

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <x264.h>
}

FrameTransformerWorker::FrameTransformerWorker(const QString& videoPath,
                                               const QVector<int>& frameIndices,
                                               TargetType targetType,
                                               int totalFrameCount,
                                               const QVector<char>& origFrameTypes,
                                               const MBEditMap&     mbEdits,
                                               const GlobalEncodeParams& globalParams,
                                               int  interpolateCount,
                                               QObject* parent)
    : QObject(parent)
    , m_videoPath(videoPath)
    , m_frameIndices(frameIndices)
    , m_targetType(targetType)
    , m_totalFrames(totalFrameCount)
    , m_origFrameTypes(origFrameTypes)
    , m_mbEdits(mbEdits)
    , m_globalParams(globalParams)
    , m_interpolateCount(interpolateCount)
{}

// ── Helper: does any frame's FrameMBParams have a bitstream-surgery knob set? ─
// Used by run() to dispatch to runBitstreamEdit() (the parallel libx264-direct
// render path) instead of the FFmpeg-wrapped pixel path.  Only the bs* fields
// count: pixel-domain knobs (qpDelta, noiseLevel, ghostBlend, etc.) are handled
// fine by the existing FFmpeg path without our x264 fork hooks.
static bool anyBitstreamEdits(const MBEditMap& edits)
{
    for (auto it = edits.constBegin(); it != edits.constEnd(); ++it) {
        const FrameMBParams& p = it.value();
        if (p.bsCbpZero    > 0)   return true;
        if (p.bsForceSkip  > 0)   return true;
        // bsMbType no longer gates the bitstream path — control moved to the
        // global --partitions parameter (GlobalEncodeParams::partitionMode)
        // which is applied to both render paths at encoder-setup time, not
        // per-MB.  Legacy presets that still carry bsMbType != -1 have no
        // effect; the field remains in FrameMBParams for JSON backward compat.
        if (p.bsIntraMode  >= 0)  return true;
        if (p.bsMvdX != 0 || p.bsMvdY != 0) return true;
        if (p.bsDctScale != 100)  return true;
    }
    return false;
}

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

// ── Helper: flip a YUV420P frame in-place ──────────────────────────────────
// vertical=true  → mirror rows (upside-down)
// vertical=false → mirror columns (left-right)
static void flipFrameInPlace(AVFrame* frame, bool vertical)
{
    if (!frame || frame->format != AV_PIX_FMT_YUV420P) return;
    int w = frame->width, h = frame->height;

    if (vertical) {
        // Swap rows top↔bottom for each plane
        for (int plane = 0; plane < 3; plane++) {
            int pH = (plane == 0) ? h : h / 2;
            int linesize = frame->linesize[plane];
            int pW = (plane == 0) ? w : w / 2;
            for (int r = 0; r < pH / 2; r++) {
                uint8_t* top = frame->data[plane] + r * linesize;
                uint8_t* bot = frame->data[plane] + (pH - 1 - r) * linesize;
                for (int c = 0; c < pW; c++)
                    std::swap(top[c], bot[c]);
            }
        }
    } else {
        // Swap columns left↔right for each plane
        for (int plane = 0; plane < 3; plane++) {
            int pH = (plane == 0) ? h : h / 2;
            int pW = (plane == 0) ? w : w / 2;
            int linesize = frame->linesize[plane];
            for (int r = 0; r < pH; r++) {
                uint8_t* row = frame->data[plane] + r * linesize;
                for (int c = 0; c < pW / 2; c++)
                    std::swap(row[c], row[pW - 1 - c]);
            }
        }
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

// ── Posterize: reduce colour depth per MB ────────────────────────────────────
static void applyPosterize(AVFrame* frame, const QSet<int>& mbs,
                            int bits, int mbCols)
{
    if (bits >= 8 || bits < 1) return; // 8 = no effect
    if (av_frame_make_writable(frame) < 0) return;
    const int shift = 8 - bits;
    const int mask  = 0xFF << shift;
    for (int mbIdx : mbs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int x0 = mbCol * 16, y0 = mbRow * 16;
        for (int y = 0; y < 16 && y0+y < frame->height; ++y)
            for (int x = 0; x < 16 && x0+x < frame->width; ++x)
                frame->data[0][(y0+y)*frame->linesize[0] + (x0+x)] &= mask;
        // Chroma planes (8x8)
        int cx0 = mbCol * 8, cy0 = mbRow * 8;
        for (int y = 0; y < 8 && cy0+y < frame->height/2; ++y)
            for (int x = 0; x < 8 && cx0+x < frame->width/2; ++x) {
                frame->data[1][(cy0+y)*frame->linesize[1] + (cx0+x)] &= mask;
                frame->data[2][(cy0+y)*frame->linesize[2] + (cx0+x)] &= mask;
            }
    }
}

// ── Pixel Shuffle: randomly permute pixel positions within each MB ───────────
static void applyPixelShuffle(AVFrame* frame, const QSet<int>& mbs,
                               int radius, int mbCols)
{
    if (radius <= 0) return;
    if (av_frame_make_writable(frame) < 0) return;
    for (int mbIdx : mbs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int x0 = mbCol * 16, y0 = mbRow * 16;
        for (int y = 0; y < 16 && y0+y < frame->height; ++y)
            for (int x = 0; x < 16 && x0+x < frame->width; ++x) {
                int dx = (std::rand() % (2*radius+1)) - radius;
                int dy = (std::rand() % (2*radius+1)) - radius;
                int sx = qBound(0, x0+x+dx, frame->width-1);
                int sy = qBound(0, y0+y+dy, frame->height-1);
                std::swap(frame->data[0][(y0+y)*frame->linesize[0]+(x0+x)],
                          frame->data[0][sy*frame->linesize[0]+sx]);
            }
    }
}

// ── Sharpen / Blur: per-MB convolution ───────────────────────────────────────
static void applySharpen(AVFrame* frame, const QSet<int>& mbs,
                          int strength, int mbCols)
{
    if (strength == 0) return;
    if (av_frame_make_writable(frame) < 0) return;
    // Work on Y plane. strength > 0 = sharpen, < 0 = blur.
    const float alpha = qBound(-1.0f, strength / 100.0f, 1.0f);
    const int W = frame->width, H = frame->height;
    // Temp buffer for one MB row of luma
    uint8_t tmp[16*16];
    for (int mbIdx : mbs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int x0 = mbCol*16, y0 = mbRow*16;
        int bw = qMin(16, W-x0), bh = qMin(16, H-y0);
        // Copy original MB luma
        for (int y = 0; y < bh; ++y)
            for (int x = 0; x < bw; ++x)
                tmp[y*16+x] = frame->data[0][(y0+y)*frame->linesize[0]+(x0+x)];
        // Apply unsharp mask: out = orig + alpha * (orig - blur)
        for (int y = 0; y < bh; ++y)
            for (int x = 0; x < bw; ++x) {
                // 3x3 box blur (clamped to frame bounds)
                int sum = 0, cnt = 0;
                for (int ky = -1; ky <= 1; ++ky)
                    for (int kx = -1; kx <= 1; ++kx) {
                        int sy = y0+y+ky, sx = x0+x+kx;
                        if (sy >= 0 && sy < H && sx >= 0 && sx < W) {
                            sum += frame->data[0][sy*frame->linesize[0]+sx];
                            cnt++;
                        }
                    }
                int blur = sum / cnt;
                int orig = tmp[y*16+x];
                int val = orig + (int)(alpha * (float)(orig - blur));
                frame->data[0][(y0+y)*frame->linesize[0]+(x0+x)] =
                    (uint8_t)qBound(0, val, 255);
            }
    }
}

// ── Temporal Difference Amplify ──────────────────────────────────────────────
static void applyTempDiffAmp(AVFrame* frame, const QSet<int>& mbs,
                              int amplify, int mbCols, const AVFrame* ref)
{
    if (!ref || amplify <= 0) return;
    if (av_frame_make_writable(frame) < 0) return;
    const float gain = amplify / 100.0f; // 100 = 1× extra, 200 = 2× extra
    for (int mbIdx : mbs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int x0 = mbCol*16, y0 = mbRow*16;
        for (int y = 0; y < 16 && y0+y < frame->height; ++y)
            for (int x = 0; x < 16 && x0+x < frame->width; ++x) {
                int pos = (y0+y)*frame->linesize[0]+(x0+x);
                int cur = frame->data[0][pos];
                int rp  = (y0+y < ref->height && x0+x < ref->width)
                          ? ref->data[0][(y0+y)*ref->linesize[0]+(x0+x)] : cur;
                int diff = cur - rp;
                int val = cur + (int)(gain * diff);
                frame->data[0][pos] = (uint8_t)qBound(0, val, 255);
            }
        // UV planes
        int cx0 = mbCol*8, cy0 = mbRow*8;
        for (int pl = 1; pl <= 2; ++pl)
            for (int y = 0; y < 8 && cy0+y < frame->height/2; ++y)
                for (int x = 0; x < 8 && cx0+x < frame->width/2; ++x) {
                    int pos = (cy0+y)*frame->linesize[pl]+(cx0+x);
                    int cur = frame->data[pl][pos];
                    int rp  = (cy0+y < ref->height/2 && cx0+x < ref->width/2)
                              ? ref->data[pl][(cy0+y)*ref->linesize[pl]+(cx0+x)] : cur;
                    int diff = cur - rp;
                    int val = cur + (int)(gain * diff);
                    frame->data[pl][pos] = (uint8_t)qBound(0, val, 255);
                }
    }
}

// ── Hue Rotate: continuous UV-plane rotation ─────────────────────────────────
static void applyHueRotate(AVFrame* frame, const QSet<int>& mbs,
                            int degrees, int mbCols)
{
    if (degrees <= 0 || degrees >= 360) return;
    if (av_frame_make_writable(frame) < 0) return;
    const float rad = degrees * 3.14159265f / 180.0f;
    const float cosA = cosf(rad), sinA = sinf(rad);
    for (int mbIdx : mbs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int cx0 = mbCol*8, cy0 = mbRow*8;
        for (int y = 0; y < 8 && cy0+y < frame->height/2; ++y)
            for (int x = 0; x < 8 && cx0+x < frame->width/2; ++x) {
                int posU = (cy0+y)*frame->linesize[1]+(cx0+x);
                int posV = (cy0+y)*frame->linesize[2]+(cx0+x);
                float u = (float)frame->data[1][posU] - 128.0f;
                float v = (float)frame->data[2][posV] - 128.0f;
                float ru = u * cosA - v * sinA;
                float rv = u * sinA + v * cosA;
                frame->data[1][posU] = (uint8_t)qBound(0, (int)(ru + 128.5f), 255);
                frame->data[2][posV] = (uint8_t)qBound(0, (int)(rv + 128.5f), 255);
            }
    }
}

// ── Bitstream Surgery — pixel-domain approximations ──────────────────────────
// These approximate bitstream-level effects by manipulating pixels so the
// encoder produces the desired compressed-domain result.

// Force Skip: copy reference pixels exactly → encoder emits skip MB
static void applyForceSkip(AVFrame* frame, const QSet<int>& mbs,
                            int pct, int mbCols, const AVFrame* ref)
{
    if (!ref || pct <= 0) return;
    if (av_frame_make_writable(frame) < 0) return;
    for (int mbIdx : mbs) {
        if (pct < 100 && (std::rand() % 100) >= pct) continue;
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int x0 = mbCol*16, y0 = mbRow*16;
        for (int y = 0; y < 16 && y0+y < frame->height; ++y)
            for (int x = 0; x < 16 && x0+x < frame->width; ++x) {
                int pos = (y0+y)*frame->linesize[0]+(x0+x);
                if (y0+y < ref->height && x0+x < ref->width)
                    frame->data[0][pos] = ref->data[0][(y0+y)*ref->linesize[0]+(x0+x)];
            }
        int cx0 = mbCol*8, cy0 = mbRow*8;
        for (int pl = 1; pl <= 2; ++pl)
            for (int y = 0; y < 8 && cy0+y < frame->height/2; ++y)
                for (int x = 0; x < 8 && cx0+x < frame->width/2; ++x) {
                    int pos = (cy0+y)*frame->linesize[pl]+(cx0+x);
                    if (cy0+y < ref->height/2 && cx0+x < ref->width/2)
                        frame->data[pl][pos] = ref->data[pl][(cy0+y)*ref->linesize[pl]+(cx0+x)];
                }
    }
}

// Direct MVD injection: shift reference pixels by exact (mvdX, mvdY)
// Similar to mvDrift but applied as a separate bitstream-surgery knob.
static void applyBsMvd(AVFrame* frame, const QSet<int>& mbs,
                        int mvdX, int mvdY, int mbCols, const AVFrame* ref)
{
    if (!ref || (mvdX == 0 && mvdY == 0)) return;
    if (av_frame_make_writable(frame) < 0) return;
    for (int mbIdx : mbs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int x0 = mbCol*16, y0 = mbRow*16;
        for (int y = 0; y < 16 && y0+y < frame->height; ++y)
            for (int x = 0; x < 16 && x0+x < frame->width; ++x) {
                int sx = qBound(0, x0+x+mvdX, ref->width-1);
                int sy = qBound(0, y0+y+mvdY, ref->height-1);
                frame->data[0][(y0+y)*frame->linesize[0]+(x0+x)] =
                    ref->data[0][sy*ref->linesize[0]+sx];
            }
        int cx0 = mbCol*8, cy0 = mbRow*8;
        int cmx = mvdX/2, cmy = mvdY/2;
        for (int pl = 1; pl <= 2; ++pl)
            for (int y = 0; y < 8 && cy0+y < frame->height/2; ++y)
                for (int x = 0; x < 8 && cx0+x < frame->width/2; ++x) {
                    int sx = qBound(0, cx0+x+cmx, ref->width/2-1);
                    int sy = qBound(0, cy0+y+cmy, ref->height/2-1);
                    frame->data[pl][(cy0+y)*frame->linesize[pl]+(cx0+x)] =
                        ref->data[pl][sy*ref->linesize[pl]+sx];
                }
    }
}

// Intra Mode: structure pixels to favour a specific intra prediction pattern
static void applyBsIntraMode(AVFrame* frame, const QSet<int>& mbs,
                              int mode, int mbCols)
{
    if (mode < 0) return; // -1 = default, do nothing
    if (av_frame_make_writable(frame) < 0) return;
    for (int mbIdx : mbs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int x0 = mbCol*16, y0 = mbRow*16;
        // Get the MB's average luma as a base value
        int sum = 0, cnt = 0;
        for (int y = 0; y < 16 && y0+y < frame->height; ++y)
            for (int x = 0; x < 16 && x0+x < frame->width; ++x) {
                sum += frame->data[0][(y0+y)*frame->linesize[0]+(x0+x)]; cnt++;
            }
        int avg = cnt > 0 ? sum/cnt : 128;
        // Apply prediction-favouring pixel patterns
        for (int y = 0; y < 16 && y0+y < frame->height; ++y)
            for (int x = 0; x < 16 && x0+x < frame->width; ++x) {
                int val = avg;
                switch (mode) {
                case 0: val = avg + (y - 8) * 4; break; // Vertical gradient
                case 1: val = avg + (x - 8) * 4; break; // Horizontal gradient
                case 2: val = avg;                break; // DC (flat)
                case 3: val = avg + (x+y-16) * 3; break; // Plane (diagonal gradient)
                }
                frame->data[0][(y0+y)*frame->linesize[0]+(x0+x)] =
                    (uint8_t)qBound(0, val, 255);
            }
    }
}

// MB Type: restructure pixels to favour specific partition decisions
static void applyBsMbType(AVFrame* frame, const QSet<int>& mbs,
                           int mbType, int mbCols, const AVFrame* ref)
{
    if (mbType < 0) return; // -1 = default
    if (av_frame_make_writable(frame) < 0) return;
    for (int mbIdx : mbs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int x0 = mbCol*16, y0 = mbRow*16;
        switch (mbType) {
        case 0: // Skip — copy from reference exactly
            if (ref) {
                for (int y = 0; y < 16 && y0+y < frame->height; ++y)
                    for (int x = 0; x < 16 && x0+x < frame->width; ++x) {
                        if (y0+y < ref->height && x0+x < ref->width)
                            frame->data[0][(y0+y)*frame->linesize[0]+(x0+x)] =
                                ref->data[0][(y0+y)*ref->linesize[0]+(x0+x)];
                    }
            }
            break;
        case 1: // I16x16 — flatten to single average (forces intra 16x16)
        {
            int sum=0, cnt=0;
            for (int y=0; y<16 && y0+y<frame->height; ++y)
                for (int x=0; x<16 && x0+x<frame->width; ++x) {
                    sum += frame->data[0][(y0+y)*frame->linesize[0]+(x0+x)]; cnt++;
                }
            uint8_t avg = cnt > 0 ? (uint8_t)(sum/cnt) : 128;
            for (int y=0; y<16 && y0+y<frame->height; ++y)
                for (int x=0; x<16 && x0+x<frame->width; ++x)
                    frame->data[0][(y0+y)*frame->linesize[0]+(x0+x)] = avg;
            break;
        }
        case 2: // I4x4 — add per-4x4 block noise to force small partitions
            for (int by=0; by<4; ++by)
                for (int bx=0; bx<4; ++bx) {
                    int off = (std::rand() % 40) - 20;
                    for (int y=0; y<4 && y0+by*4+y<frame->height; ++y)
                        for (int x=0; x<4 && x0+bx*4+x<frame->width; ++x) {
                            int pos = (y0+by*4+y)*frame->linesize[0]+(x0+bx*4+x);
                            frame->data[0][pos] = (uint8_t)qBound(0, (int)frame->data[0][pos]+off, 255);
                        }
                }
            break;
        case 3: // P16x16 — slight uniform shift to force single MV
            if (ref) {
                for (int y=0; y<16 && y0+y<frame->height; ++y)
                    for (int x=0; x<16 && x0+x<frame->width; ++x) {
                        int sx = qBound(0, x0+x+2, ref->width-1);
                        if (y0+y < ref->height)
                            frame->data[0][(y0+y)*frame->linesize[0]+(x0+x)] =
                                ref->data[0][(y0+y)*ref->linesize[0]+sx];
                    }
            }
            break;
        case 4: // P8x8 — different shifts per 8x8 sub-block
            if (ref) {
                for (int by=0; by<2; ++by)
                    for (int bx=0; bx<2; ++bx) {
                        int dx = (std::rand() % 8) - 4;
                        int dy = (std::rand() % 8) - 4;
                        for (int y=0; y<8 && y0+by*8+y<frame->height; ++y)
                            for (int x=0; x<8 && x0+bx*8+x<frame->width; ++x) {
                                int sx = qBound(0, x0+bx*8+x+dx, ref->width-1);
                                int sy = qBound(0, y0+by*8+y+dy, ref->height-1);
                                frame->data[0][(y0+by*8+y)*frame->linesize[0]+(x0+bx*8+x)] =
                                    ref->data[0][sy*ref->linesize[0]+sx];
                            }
                    }
            }
            break;
        }
    }
}

// DCT Scale: scale residual by transforming to frequency domain and back
static void applyDctScale(AVFrame* frame, const QSet<int>& mbs,
                           int scalePct, int mbCols, const AVFrame* ref)
{
    if (scalePct == 100 || !ref) return; // 100% = no change
    if (av_frame_make_writable(frame) < 0) return;
    const float scale = scalePct / 100.0f;
    // Approximate: residual = current - reference, scale it, add back to reference
    for (int mbIdx : mbs) {
        int mbRow = mbIdx / mbCols, mbCol = mbIdx % mbCols;
        int x0 = mbCol*16, y0 = mbRow*16;
        for (int y = 0; y < 16 && y0+y < frame->height; ++y)
            for (int x = 0; x < 16 && x0+x < frame->width; ++x) {
                int pos = (y0+y)*frame->linesize[0]+(x0+x);
                int cur = frame->data[0][pos];
                int rp  = (y0+y < ref->height && x0+x < ref->width)
                          ? ref->data[0][(y0+y)*ref->linesize[0]+(x0+x)] : cur;
                int residual = cur - rp;
                int val = rp + (int)(scale * residual);
                frame->data[0][pos] = (uint8_t)qBound(0, val, 255);
            }
        int cx0 = mbCol*8, cy0 = mbRow*8;
        for (int pl = 1; pl <= 2; ++pl)
            for (int y = 0; y < 8 && cy0+y < frame->height/2; ++y)
                for (int x = 0; x < 8 && cx0+x < frame->width/2; ++x) {
                    int pos = (cy0+y)*frame->linesize[pl]+(cx0+x);
                    int cur = frame->data[pl][pos];
                    int rp  = (cy0+y < ref->height/2 && cx0+x < ref->width/2)
                              ? ref->data[pl][(cy0+y)*ref->linesize[pl]+(cx0+x)] : cur;
                    int residual = cur - rp;
                    int val = rp + (int)(scale * residual);
                    frame->data[pl][pos] = (uint8_t)qBound(0, val, 255);
                }
    }
}

// CBP Zero: zero out residual → pixel becomes pure prediction
static void applyCbpZero(AVFrame* frame, const QSet<int>& mbs,
                          int pct, int mbCols, const AVFrame* ref)
{
    if (!ref || pct <= 0) return;
    // Same as ForceSkip but specifically targets residual elimination
    applyForceSkip(frame, mbs, pct, mbCols, ref);
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

    // 9. Posterize — reduce colour depth
    if (p.posterize < 8)
        applyPosterize(frame, mbs, p.posterize, mbCols);

    // 10. Pixel Shuffle — random pixel displacement within MBs
    if (p.pixelShuffle > 0)
        applyPixelShuffle(frame, mbs, p.pixelShuffle, mbCols);

    // 11. Sharpen / Blur — per-MB convolution
    if (p.sharpen != 0)
        applySharpen(frame, mbs, p.sharpen, mbCols);

    // 12. Temporal Difference Amplify — exaggerate motion delta
    if (ref && p.tempDiffAmp > 0)
        applyTempDiffAmp(frame, mbs, p.tempDiffAmp, mbCols, ref);

    // 13. Hue Rotate — continuous UV rotation
    if (p.hueRotate > 0 && p.hueRotate < 360)
        applyHueRotate(frame, mbs, p.hueRotate, mbCols);

    // ── Bitstream-domain approximations ──────────────────────────────────────

    // 14. Direct MVD injection
    if (ref && (p.bsMvdX != 0 || p.bsMvdY != 0))
        applyBsMvd(frame, mbs, p.bsMvdX, p.bsMvdY, mbCols, ref);

    // 15. Force Skip — copy reference exactly
    if (ref && p.bsForceSkip > 0)
        applyForceSkip(frame, mbs, p.bsForceSkip, mbCols, ref);

    // 16. Intra prediction mode structuring
    if (p.bsIntraMode >= 0)
        applyBsIntraMode(frame, mbs, p.bsIntraMode, mbCols);

    // 17. MB Type forcing
    if (p.bsMbType >= 0)
        applyBsMbType(frame, mbs, p.bsMbType, mbCols, ref);

    // 18. DCT coefficient scaling
    if (ref && p.bsDctScale != 100)
        applyDctScale(frame, mbs, p.bsDctScale, mbCols, ref);

    // 19. CBP Zero — drop residual
    if (ref && p.bsCbpZero > 0)
        applyCbpZero(frame, mbs, p.bsCbpZero, mbCols, ref);
}

// ── Frame blending helper (for Interpolate Left / Right) ─────────────────────
// Allocates a new AVFrame whose pixels are a weighted mix of 'a' and 'b':
//   pixel = round( a*(1-alpha) + b*alpha )
// alpha = 0.0 → identical to 'a';  alpha = 1.0 → identical to 'b'.
// Both frames are assumed to be YUV 4:2:0 planar.  Returns nullptr on failure.
static AVFrame* blendFramesWeighted(const AVFrame* a, const AVFrame* b, float alpha)
{
    if (!a || !b) return nullptr;
    AVFrame* out = av_frame_alloc();
    if (!out) return nullptr;

    out->format = AV_PIX_FMT_YUV420P;
    out->width  = a->width;
    out->height = a->height;
    if (av_frame_get_buffer(out, 0) < 0) {
        av_frame_free(&out);
        return nullptr;
    }
    av_frame_copy_props(out, a);
    out->pict_type = AV_PICTURE_TYPE_P;
    out->flags    &= ~AV_FRAME_FLAG_KEY;

    const float wa = 1.0f - alpha;   // weight for frame a
    const float wb =        alpha;   // weight for frame b

    // Blend Y (full resolution) then U and V (half resolution each axis).
    for (int plane = 0; plane < 3; ++plane) {
        const int w = (plane == 0) ? a->width  : (a->width  + 1) / 2;
        const int h = (plane == 0) ? a->height : (a->height + 1) / 2;
        for (int y = 0; y < h; ++y) {
            const uint8_t* pa = a->data[plane] + y * a->linesize[plane];
            const uint8_t* pb = b->data[plane] + y * b->linesize[plane];
            uint8_t*       po = out->data[plane] + y * out->linesize[plane];
            for (int x = 0; x < w; ++x)
                po[x] = (uint8_t)(wa * (float)pa[x] + wb * (float)pb[x] + 0.5f);
        }
    }
    return out;
}

// ── Bitstream splice for Delete ────────────────────────────────────────────
// Copies compressed H.264 packets directly to the output, skipping selected
// frames.  No decode or re-encode takes place, so the original motion vectors
// in the surviving P-frames are preserved intact.  After deletion those
// vectors reference content from a different shot → datamosh smear.
//
// Prerequisites: the video should be an I/P-only stream (use Force → P with
// Kill I-Frames first).  B-frame videos have packet DTS order ≠ display order,
// so the packet counter would not map correctly to timeline frame indices.
void FrameTransformerWorker::runBitstreamSplice()
{
    ControlLogger::instance().logApplyStarted(m_totalFrames, 0);
    ControlLogger::instance().logRenderPath(
        "BITSTREAM SPLICE PATH (packet copy, no re-encode — DeleteFrames)");

    const QString tempPath = m_videoPath + ".xform_tmp.mp4";
    QString errorMsg;
    bool    ok   = false;

    AVFormatContext* ifmt = nullptr;
    AVFormatContext* ofmt = nullptr;
    AVPacket*        pkt  = nullptr;
    int vsIdx = -1;

    // Warn when B-frames are present — packet order won't match display order.
    if (m_origFrameTypes.contains('B')) {
        emit warning(
            "Delete datamosh requires an I/P-only stream. "
            "Select all frames \u2192 Force \u2192 P (with Kill I-Frames ON) first, "
            "then Delete. Proceeding, but frame mapping may be incorrect.");
    }

    // ── Open input (no decoder needed) ─────────────────────────────────────
    if (avformat_open_input(&ifmt, m_videoPath.toUtf8().constData(),
                            nullptr, nullptr) < 0) {
        errorMsg = "Cannot open input: " + m_videoPath;
        goto splice_cleanup;
    }
    if (avformat_find_stream_info(ifmt, nullptr) < 0) {
        errorMsg = "avformat_find_stream_info failed";
        goto splice_cleanup;
    }
    for (unsigned i = 0; i < ifmt->nb_streams; i++) {
        if (ifmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vsIdx = (int)i; break;
        }
    }
    if (vsIdx < 0) { errorMsg = "No video stream"; goto splice_cleanup; }

    // ── Open output ────────────────────────────────────────────────────────
    if (avformat_alloc_output_context2(&ofmt, nullptr, nullptr,
                                       tempPath.toUtf8().constData()) < 0) {
        errorMsg = "Cannot alloc output context"; goto splice_cleanup;
    }

    // Mirror every input stream into the output (video + any audio/data).
    for (unsigned i = 0; i < ifmt->nb_streams; i++) {
        AVStream* in  = ifmt->streams[i];
        AVStream* out = avformat_new_stream(ofmt, nullptr);
        if (!out) { errorMsg = "Cannot create output stream"; goto splice_cleanup; }
        if (avcodec_parameters_copy(out->codecpar, in->codecpar) < 0) {
            errorMsg = "Cannot copy codec parameters"; goto splice_cleanup;
        }
        out->codecpar->codec_tag = 0;  // let muxer assign the correct tag
        out->time_base           = in->time_base;
    }

    if (!(ofmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt->pb, tempPath.toUtf8().constData(),
                      AVIO_FLAG_WRITE) < 0) {
            errorMsg = "Cannot open output file"; goto splice_cleanup;
        }
    }
    if (avformat_write_header(ofmt, nullptr) < 0) {
        errorMsg = "avformat_write_header failed"; goto splice_cleanup;
    }

    // ── Copy packets, skipping deleted frames ──────────────────────────────
    pkt = av_packet_alloc();
    {
        AVStream* vs = ifmt->streams[vsIdx];

        // Frame duration in the input video's time_base — used to write
        // sequential, gap-free output timestamps while leaving the compressed
        // payload (motion vectors) completely untouched.
        int64_t frameDur = 0;
        if (vs->avg_frame_rate.num > 0 && vs->avg_frame_rate.den > 0)
            frameDur = av_rescale_q(
                1,
                AVRational{ vs->avg_frame_rate.den, vs->avg_frame_rate.num },
                vs->time_base);
        if (frameDur <= 0) frameDur = 1;

        const QSet<int> delSet(m_frameIndices.begin(), m_frameIndices.end());
        int     videoFrameIdx = 0;   // packet read-order counter = frame index
        int64_t outVideoPts   = 0;   // monotonically increasing output PTS

        while (av_read_frame(ifmt, pkt) >= 0) {
            if (pkt->stream_index == vsIdx) {
                if (!delSet.contains(videoFrameIdx)) {
                    // Rewrite only the container timestamps so the muxer gets
                    // valid monotonic values; the H.264 NAL payload is copied
                    // byte-for-byte — motion vectors remain intact.
                    pkt->pts      = outVideoPts;
                    pkt->dts      = outVideoPts;
                    pkt->duration = frameDur;
                    pkt->pos      = -1;
                    outVideoPts  += frameDur;

                    av_interleaved_write_frame(ofmt, pkt);
                    emit progress(videoFrameIdx + 1, m_totalFrames);
                }
                videoFrameIdx++;
            } else {
                // Non-video streams (audio, subtitles): rescale and pass through.
                AVStream* inS  = ifmt->streams[pkt->stream_index];
                AVStream* outS = ofmt->streams[pkt->stream_index];
                av_packet_rescale_ts(pkt, inS->time_base, outS->time_base);
                pkt->pos = -1;
                av_interleaved_write_frame(ofmt, pkt);
            }
            av_packet_unref(pkt);
        }
    }
    av_write_trailer(ofmt);
    ok = true;

splice_cleanup:
    if (pkt)  av_packet_free(&pkt);
    if (ofmt && !(ofmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt->pb);
    if (ofmt) avformat_free_context(ofmt);
    if (ifmt) avformat_close_input(&ifmt);

    if (ok) {
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

// =============================================================================
// runBitstreamEdit — parallel render path for true bitstream-level MB edits.
// =============================================================================
//
// This is the Option X2 render path (see BITSTREAM_SURGERY_ARCHITECTURE.md):
//
//     libavformat (demux)
//          │
//          ▼
//     libavcodec (decode)
//          │
//          ▼
//     applyMBEdits(...)        ← pixel-domain corruption on decoded YUV
//          │
//          ▼
//     libx264 direct C API    ← our forked x264 with override hooks
//          │   pic.prop.cbp_override      (uint8_t*, 1 = force CBP=0)
//          │   pic.prop.mb_skip_override  (uint8_t*, 1 = force P_SKIP/B_SKIP)
//          │   pic.prop.mb_type_override  (uint8_t*, 1..5)
//          │   pic.prop.intra_mode_override (int8_t*, -1..3)
//          │   pic.prop.mvd_x/y/active_override  (MVD injection)
//          │   pic.prop.dct_scale_override (uint8_t*, 255=none, 0..200=%)
//          ▼
//     Annex-B → AVCC conversion
//          │
//          ▼
//     libavformat (MP4 mux)
//          ▼
//     output.mp4
//
// Ownership: each override array is heap-allocated per frame with calloc/malloc
// and passed to x264 via pic.prop.*_override_free = std::free.  x264 takes
// ownership at x264_encoder_encode() time, uses the array during encoding, and
// calls the free callback from the encoder thread once fdec is released.
// The caller must NOT free these arrays — doing so would double-free.
// =============================================================================
void FrameTransformerWorker::runBitstreamEdit()
{
    // Loud marker so Debug-console output makes the render path unambiguous —
    // the ControlLogger doesn't distinguish pixel-domain vs bitstream path on
    // its own (both funnel through the same logApplyStarted / per-frame calls).
    qDebug() << "[FrameTransformer] runBitstreamEdit() — direct libx264 path ("
             << m_mbEdits.size() << "frames carry edits,"
             << m_totalFrames << "total frames)";
    ControlLogger::instance().logApplyStarted(m_totalFrames, m_mbEdits.size());
    ControlLogger::instance().logRenderPath(
        "BITSTREAM-SURGERY PATH (direct libx264 via x264-lamoshpit fork)");

    const QString tempPath = m_videoPath + ".xform_tmp.mp4";
    QString errorMsg;
    bool    ok   = false;

    AVFormatContext* ifmt   = nullptr;
    AVFormatContext* ofmt   = nullptr;
    AVCodecContext*  decCtx = nullptr;
    x264_t*          x264Enc = nullptr;
    AVPacket*        rpkt   = nullptr;
    AVFrame*         frame  = nullptr;
    int              vsIdx  = -1;

    x264_param_t   x264Param;
    x264_picture_t picIn;
    x264_picture_t picOut;
    std::memset(&picIn,  0, sizeof(picIn));
    std::memset(&picOut, 0, sizeof(picOut));

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
        if (avcodec_parameters_to_context(decCtx, inStream->codecpar) < 0) {
            errorMsg = "avcodec_parameters_to_context failed"; goto cleanup;
        }
        if (avcodec_open2(decCtx, dec, nullptr) < 0) {
            errorMsg = "Cannot open decoder"; goto cleanup;
        }
    }

    // ── Configure & open x264 encoder ─────────────────────────────────────
    {
        AVStream* inStream = ifmt->streams[vsIdx];
        int width  = decCtx->width;
        int height = decCtx->height;
        int fpsNum = inStream->avg_frame_rate.num;
        int fpsDen = inStream->avg_frame_rate.den;
        if (fpsNum <= 0 || fpsDen <= 0) { fpsNum = 24; fpsDen = 1; }

        if (x264_param_default_preset(&x264Param, "medium", nullptr) < 0) {
            errorMsg = "x264_param_default_preset failed"; goto cleanup;
        }
        x264Param.i_width       = width;
        x264Param.i_height      = height;
        x264Param.i_csp         = X264_CSP_I420;
        x264Param.i_fps_num     = fpsNum;
        x264Param.i_fps_den     = fpsDen;
        x264Param.i_timebase_num = fpsDen;
        x264Param.i_timebase_den = fpsNum;
        x264Param.b_vfr_input   = 0;
        x264Param.b_repeat_headers = 0;   // we embed SPS/PPS via extradata
        x264Param.b_annexb      = 1;      // default: start codes

        // Rate control — mirror pixel path: CRF 18 default, CQP when qpOverride set.
        x264Param.rc.i_rc_method = X264_RC_CRF;
        x264Param.rc.f_rf_constant = 18.0f;
        if (m_globalParams.qpOverride >= 0) {
            x264Param.rc.i_rc_method  = X264_RC_CQP;
            x264Param.rc.i_qp_constant = m_globalParams.qpOverride;
        }
        if (m_globalParams.qpMin >= 0) x264Param.rc.i_qp_min = m_globalParams.qpMin;
        if (m_globalParams.qpMax >= 0) x264Param.rc.i_qp_max = m_globalParams.qpMax;

        // Frame structure / GOP (match pixel path exactly so A/B comparisons work)
        const bool killI = m_globalParams.killIFrames;
        x264Param.i_bframe = killI ? 0
                           : (m_globalParams.bFrames >= 0) ? m_globalParams.bFrames : 3;
        const int gopSize = (killI || m_globalParams.gopSize == 0) ? 9999
                          : (m_globalParams.gopSize > 0) ? m_globalParams.gopSize : 250;
        x264Param.i_keyint_max = gopSize;
        x264Param.i_keyint_min = gopSize;
        x264Param.i_scenecut_threshold = m_globalParams.scenecut ? 40 : 0;
        if (killI) x264Param.i_bframe_adaptive = 0;

        if (m_globalParams.refFrames > 0)
            x264Param.i_frame_reference = m_globalParams.refFrames;
        if (m_globalParams.cabacDisable)
            x264Param.b_cabac = 0;
        if (m_globalParams.noDeblock)
            x264Param.b_deblocking_filter = 0;
        if (m_globalParams.mbTreeDisable)
            x264Param.rc.b_mb_tree = 0;
        if (m_globalParams.aqMode >= 0)
            x264Param.rc.i_aq_mode = m_globalParams.aqMode;
        if (m_globalParams.aqStrength >= 0.0f)
            x264Param.rc.f_aq_strength = m_globalParams.aqStrength;
        if (m_globalParams.trellis >= 0)
            x264Param.analyse.i_trellis = m_globalParams.trellis;
        if (m_globalParams.psyRD >= 0.0f)
            x264Param.analyse.f_psy_rd = m_globalParams.psyRD;
        if (m_globalParams.psyTrellis >= 0.0f)
            x264Param.analyse.f_psy_trellis = m_globalParams.psyTrellis;
        if (m_globalParams.noFastPSkip)
            x264Param.analyse.b_fast_pskip = 0;
        if (m_globalParams.noDctDecimate)
            x264Param.analyse.b_dct_decimate = 0;
        if (m_globalParams.rcLookahead >= 0)
            x264Param.rc.i_lookahead = m_globalParams.rcLookahead;
        if (m_globalParams.qcompEnabled)
            x264Param.rc.f_qcompress = m_globalParams.qcomp;
        if (m_globalParams.ipratioEnabled)
            x264Param.rc.f_ip_factor = m_globalParams.ipratio;
        if (m_globalParams.pbratioEnabled)
            x264Param.rc.f_pb_factor = m_globalParams.pbratio;
        if (m_globalParams.qblurEnabled)
            x264Param.rc.f_qblur = m_globalParams.qblur;

        // Partition Mode — per-frame-type MB Type dropdowns map to subsets
        // of x264_param_t.analyse.inter bit flags:
        //   X264_ANALYSE_I4x4       — intra 4×4 (from I-frame dropdown)
        //   X264_ANALYSE_I8x8       — intra 8×8 (from I-frame dropdown;
        //                             requires b_transform_8x8)
        //   X264_ANALYSE_PSUB16x16  — P 8×8 sub-partitions (from P-frame dropdown)
        //   X264_ANALYSE_PSUB8x8    — P 4×4 inside P 8×8 (from P-frame dropdown)
        //   X264_ANALYSE_BSUB16x16  — B 8×8 sub-partitions (from B-frame dropdown)
        //
        // Build only when any dropdown is non-default.  -1 dropdown values
        // default to x264's natural per-slice-type default, so a single
        // non-default dropdown doesn't unintentionally restrict the others.
        // Mirrors the FFmpeg-pixel-path construction in runTransform().
        if (m_globalParams.iFrameMbType >= 0 ||
            m_globalParams.pFrameMbType >= 0 ||
            m_globalParams.bFrameMbType >= 0)
        {
            uint32_t flags = 0;
            // Intra — default: i8x8 + i4x4
            int iEff = (m_globalParams.iFrameMbType < 0) ? 2 : m_globalParams.iFrameMbType;
            if (iEff >= 1) flags |= X264_ANALYSE_I8x8;
            if (iEff >= 2) flags |= X264_ANALYSE_I4x4;
            // P inter — default: PSUB16x16 (p8x8)
            int pEff = (m_globalParams.pFrameMbType < 0) ? 1 : m_globalParams.pFrameMbType;
            if (pEff >= 1) flags |= X264_ANALYSE_PSUB16x16;
            if (pEff >= 2) flags |= X264_ANALYSE_PSUB8x8;
            // B inter — default: BSUB16x16 (b8x8)
            int bEff = (m_globalParams.bFrameMbType < 0) ? 1 : m_globalParams.bFrameMbType;
            if (bEff >= 1) flags |= X264_ANALYSE_BSUB16x16;
            x264Param.analyse.inter = flags;
            // i8x8 requires 8×8 DCT transform
            if ((flags & X264_ANALYSE_I8x8) || m_globalParams.use8x8DCT)
                x264Param.analyse.b_transform_8x8 = 1;
        }

        x264_param_apply_profile(&x264Param, "high");

        x264Enc = x264_encoder_open(&x264Param);
        if (!x264Enc) { errorMsg = "x264_encoder_open failed"; goto cleanup; }
    }

    // ── Open output container & build stream from x264 SPS/PPS headers ────
    if (avformat_alloc_output_context2(&ofmt, nullptr, "mp4",
                                       tempPath.toUtf8().constData()) < 0) {
        errorMsg = "Cannot alloc output context"; goto cleanup;
    }
    {
        AVStream* outStream = avformat_new_stream(ofmt, nullptr);
        outStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        outStream->codecpar->codec_id   = AV_CODEC_ID_H264;
        outStream->codecpar->format     = AV_PIX_FMT_YUV420P;
        outStream->codecpar->width      = decCtx->width;
        outStream->codecpar->height     = decCtx->height;
        // x264's i_fps_* / i_timebase_* are uint32_t; AVRational uses int.
        outStream->time_base            = AVRational{(int)x264Param.i_timebase_num,
                                                     (int)x264Param.i_timebase_den};
        outStream->avg_frame_rate       = AVRational{(int)x264Param.i_fps_num,
                                                     (int)x264Param.i_fps_den};

        // Pull SPS/PPS out of x264 and build avcC extradata for MP4 muxer.
        x264_nal_t* hdrs  = nullptr;
        int         iNal  = 0;
        int headerSize = x264_encoder_headers(x264Enc, &hdrs, &iNal);
        if (headerSize <= 0 || !hdrs) {
            errorMsg = "x264_encoder_headers failed"; goto cleanup;
        }

        // Find SPS and PPS NALs (type 7 and 8, respectively).  Strip Annex-B
        // start codes and build an avcC box as required by movenc.
        const uint8_t* spsPayload = nullptr; int spsSize = 0;
        const uint8_t* ppsPayload = nullptr; int ppsSize = 0;
        for (int i = 0; i < iNal; i++) {
            const uint8_t* p = hdrs[i].p_payload;
            int            n = hdrs[i].i_payload;
            // Skip Annex-B start code
            int sc = 0;
            if (n >= 4 && p[0]==0 && p[1]==0 && p[2]==0 && p[3]==1) sc = 4;
            else if (n >= 3 && p[0]==0 && p[1]==0 && p[2]==1) sc = 3;
            const uint8_t* nalBody = p + sc;
            int nalBodySize = n - sc;
            int nalType = nalBody[0] & 0x1F;
            if (nalType == 7) { spsPayload = nalBody; spsSize = nalBodySize; }
            else if (nalType == 8) { ppsPayload = nalBody; ppsSize = nalBodySize; }
        }
        if (!spsPayload || !ppsPayload || spsSize < 4) {
            errorMsg = "Missing SPS/PPS from x264 headers"; goto cleanup;
        }

        QByteArray avcC;
        avcC.append((char)0x01);                    // configurationVersion
        avcC.append((char)spsPayload[1]);           // AVCProfileIndication
        avcC.append((char)spsPayload[2]);           // profile_compatibility
        avcC.append((char)spsPayload[3]);           // AVCLevelIndication
        avcC.append((char)0xFF);                    // 6 reserved bits + NALU len-1 = 3
        avcC.append((char)0xE1);                    // 3 reserved bits + numOfSPS = 1
        avcC.append((char)((spsSize >> 8) & 0xFF));
        avcC.append((char)(spsSize & 0xFF));
        avcC.append(reinterpret_cast<const char*>(spsPayload), spsSize);
        avcC.append((char)0x01);                    // numOfPPS = 1
        avcC.append((char)((ppsSize >> 8) & 0xFF));
        avcC.append((char)(ppsSize & 0xFF));
        avcC.append(reinterpret_cast<const char*>(ppsPayload), ppsSize);

        outStream->codecpar->extradata = (uint8_t*)av_mallocz(
            avcC.size() + AV_INPUT_BUFFER_PADDING_SIZE);
        outStream->codecpar->extradata_size = avcC.size();
        std::memcpy(outStream->codecpar->extradata, avcC.constData(), avcC.size());
    }

    if (!(ofmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt->pb, tempPath.toUtf8().constData(),
                      AVIO_FLAG_WRITE) < 0) {
            errorMsg = "Cannot open output file"; goto cleanup;
        }
    }
    if (avformat_write_header(ofmt, nullptr) < 0) {
        errorMsg = "avformat_write_header failed"; goto cleanup;
    }

    // ── Initialize x264 picture (i_csp + i_plane; planes/strides per frame) ─
    x264_picture_init(&picIn);
    picIn.img.i_csp   = X264_CSP_I420;
    picIn.img.i_plane = 3;

    frame = av_frame_alloc();
    rpkt  = av_packet_alloc();

    {
        const int width   = decCtx->width;
        const int height  = decCtx->height;
        const int mbCols  = (width  + 15) / 16;
        const int mbRows  = (height + 15) / 16;
        const int mbCount = mbCols * mbRows;

        // Cascade pre-expansion — identical logic to runTransform() so
        // bitstream edits respect cascadeLen / cascadeDecay too.
        MBEditMap activeEdits = m_mbEdits;
        for (auto it = m_mbEdits.constBegin(); it != m_mbEdits.constEnd(); ++it) {
            const FrameMBParams& src = it.value();
            if (src.cascadeLen <= 0) continue;
            for (int k = 1; k <= src.cascadeLen; ++k) {
                int ci = it.key() + k;
                if (ci >= m_totalFrames) break;
                if (activeEdits.contains(ci)) continue;
                float f = 1.0f - (src.cascadeDecay / 100.0f)
                               * ((float)k / (float)src.cascadeLen);
                f = qBound(0.0f, f, 1.0f);
                if (f < 0.01f) break;
                FrameMBParams c;
                c.selectedMBs  = src.selectedMBs;
                c.spillRadius  = src.spillRadius;
                c.refDepth     = 1;
                c.ghostBlend   = qMin(100, (int)(100.0f * f));
                c.qpDelta      = 51;
                // Propagate bs* seed values at decayed intensity (probability-like
                // knobs scale by f; direct-value knobs pass through unchanged on
                // the cascade seed frame, off on downstream cascade frames).
                c.bsCbpZero    = (int)(src.bsCbpZero   * f);
                c.bsForceSkip  = (int)(src.bsForceSkip * f);
                activeEdits[ci] = c;
            }
        }

        AVStream* outStream = ofmt->streams[0];
        int frameIdx = 0;

        // Helper: encode one x264_picture_t and mux the resulting NAL as an
        // AVCC-format AVPacket (replacing Annex-B start codes with 4-byte
        // big-endian length prefixes).  Works for both real frames and flush.
        auto encodeAndMux = [&](x264_picture_t* inPic) {
            x264_nal_t* nal  = nullptr;
            int         iNal = 0;
            int frameSize = x264_encoder_encode(x264Enc, &nal, &iNal, inPic, &picOut);
            if (frameSize <= 0 || !nal) return;

            // Convert Annex-B byte stream → AVCC length-prefixed.  x264 emits all
            // output NALs back-to-back into nal[0].p_payload..nal[iNal-1].p_payload
            // as one contiguous buffer of size frameSize.  We walk it by NAL.
            QByteArray avccPkt;
            avccPkt.reserve(frameSize + iNal * 4);
            for (int i = 0; i < iNal; i++) {
                const uint8_t* p = nal[i].p_payload;
                int n = nal[i].i_payload;
                int sc = 0;
                if (n >= 4 && p[0]==0 && p[1]==0 && p[2]==0 && p[3]==1) sc = 4;
                else if (n >= 3 && p[0]==0 && p[1]==0 && p[2]==1) sc = 3;
                int body = n - sc;
                uint8_t len4[4] = {
                    (uint8_t)((body >> 24) & 0xFF),
                    (uint8_t)((body >> 16) & 0xFF),
                    (uint8_t)((body >>  8) & 0xFF),
                    (uint8_t)( body        & 0xFF)
                };
                avccPkt.append(reinterpret_cast<const char*>(len4), 4);
                avccPkt.append(reinterpret_cast<const char*>(p + sc), body);
            }
            AVPacket* wpkt = av_packet_alloc();
            if (av_new_packet(wpkt, avccPkt.size()) < 0) {
                av_packet_free(&wpkt); return;
            }
            std::memcpy(wpkt->data, avccPkt.constData(), avccPkt.size());
            wpkt->stream_index = 0;
            wpkt->pts = picOut.i_pts;
            wpkt->dts = picOut.i_dts;
            if (picOut.b_keyframe) wpkt->flags |= AV_PKT_FLAG_KEY;
            av_packet_rescale_ts(wpkt,
                AVRational{(int)x264Param.i_timebase_num,
                           (int)x264Param.i_timebase_den},
                outStream->time_base);
            av_interleaved_write_frame(ofmt, wpkt);
            av_packet_free(&wpkt);
        };

        // Helper: fill pic.prop.*_override arrays from this frame's FrameMBParams.
        // Every array is heap-allocated with calloc/malloc and x264 takes
        // ownership via the *_free = std::free callback.  After
        // x264_encoder_encode returns, we reset the picIn.prop pointers to
        // nullptr before the next frame so we never double-assign.
        auto setupOverrides = [&](const FrameMBParams* p) {
            // Reset all pointers first (safe default for frames with no edits).
            picIn.prop.cbp_override         = nullptr;
            picIn.prop.cbp_override_free    = nullptr;
            picIn.prop.mb_skip_override     = nullptr;
            picIn.prop.mb_skip_override_free= nullptr;
            picIn.prop.mb_type_override     = nullptr;
            picIn.prop.mb_type_override_free= nullptr;
            picIn.prop.intra_mode_override  = nullptr;
            picIn.prop.intra_mode_override_free = nullptr;
            picIn.prop.mvd_x_override       = nullptr;
            picIn.prop.mvd_y_override       = nullptr;
            picIn.prop.mvd_active_override  = nullptr;
            picIn.prop.mvd_x_override_free  = nullptr;
            picIn.prop.mvd_y_override_free  = nullptr;
            picIn.prop.mvd_active_override_free = nullptr;
            picIn.prop.dct_scale_override   = nullptr;
            picIn.prop.dct_scale_override_free = nullptr;
            if (!p) return;

            // Resolve selection set: empty = global (whole frame), else expand
            // by spillRadius so the bitstream edit matches the visual footprint.
            QSet<int> fullFrame;
            const QSet<int>* base = &p->selectedMBs;
            if (p->selectedMBs.isEmpty()) {
                fullFrame.reserve(mbCount);
                for (int i = 0; i < mbCount; ++i) fullFrame.insert(i);
                base = &fullFrame;
            }
            const QSet<int>& mbs = (p->spillRadius > 0)
                ? expandedMBs(*base, p->spillRadius, mbCols, mbRows)
                : *base;

            // CBP Zero — probability-per-MB (0..100).
            if (p->bsCbpZero > 0) {
                uint8_t* arr = (uint8_t*)std::calloc(mbCount, 1);
                for (int mbi : mbs)
                    if (mbi >= 0 && mbi < mbCount &&
                        (std::rand() % 100) < p->bsCbpZero) arr[mbi] = 1;
                picIn.prop.cbp_override      = arr;
                picIn.prop.cbp_override_free = std::free;
            }
            // Force Skip — probability-per-MB (0..100).
            if (p->bsForceSkip > 0) {
                uint8_t* arr = (uint8_t*)std::calloc(mbCount, 1);
                for (int mbi : mbs)
                    if (mbi >= 0 && mbi < mbCount &&
                        (std::rand() % 100) < p->bsForceSkip) arr[mbi] = 1;
                picIn.prop.mb_skip_override      = arr;
                picIn.prop.mb_skip_override_free = std::free;
            }
            // Force MB Type (per-MB) — REMOVED.  Previously wrote the
            // x264 mb_type_override array here based on p->bsMbType.  Control
            // migrated to the encoder-wide --partitions parameter
            // (GlobalEncodeParams::partitionMode), which is set on the x264
            // param struct at encoder-open time.  The x264 mb_type_override
            // hook in our fork remains as dead code; we simply never populate
            // the array.  p->bsMbType is left at its -1 default and ignored.
            // Force Intra Mode — 0..3 (V/H/DC/Plane) for I_16x16 only.
            if (p->bsIntraMode >= 0 && p->bsIntraMode <= 3) {
                int8_t* arr = (int8_t*)std::malloc(mbCount);
                std::memset(arr, 0xFF /* -1 */, mbCount);
                for (int mbi : mbs)
                    if (mbi >= 0 && mbi < mbCount) arr[mbi] = (int8_t)p->bsIntraMode;
                picIn.prop.intra_mode_override      = arr;
                picIn.prop.intra_mode_override_free = std::free;
            }
            // MVD Injection — UI values are whole pixels; x264 expects q-pel.
            if (p->bsMvdX != 0 || p->bsMvdY != 0) {
                int16_t* xArr = (int16_t*)std::calloc(mbCount, sizeof(int16_t));
                int16_t* yArr = (int16_t*)std::calloc(mbCount, sizeof(int16_t));
                uint8_t* act  = (uint8_t*)std::calloc(mbCount, 1);
                int16_t qx = (int16_t)(p->bsMvdX * 4);
                int16_t qy = (int16_t)(p->bsMvdY * 4);
                for (int mbi : mbs) {
                    if (mbi < 0 || mbi >= mbCount) continue;
                    xArr[mbi] = qx;
                    yArr[mbi] = qy;
                    act [mbi] = 1;
                }
                picIn.prop.mvd_x_override           = xArr;
                picIn.prop.mvd_y_override           = yArr;
                picIn.prop.mvd_active_override      = act;
                picIn.prop.mvd_x_override_free      = std::free;
                picIn.prop.mvd_y_override_free      = std::free;
                picIn.prop.mvd_active_override_free = std::free;
            }
            // DCT Scale — 100 = no-op (sentinel-coded as 255).
            if (p->bsDctScale != 100) {
                uint8_t* arr = (uint8_t*)std::malloc(mbCount);
                std::memset(arr, 255, mbCount);  // 255 = no override
                uint8_t scale = (uint8_t)qBound(0, p->bsDctScale, 200);
                for (int mbi : mbs)
                    if (mbi >= 0 && mbi < mbCount) arr[mbi] = scale;
                picIn.prop.dct_scale_override      = arr;
                picIn.prop.dct_scale_override_free = std::free;
            }
        };

        while (av_read_frame(ifmt, rpkt) >= 0) {
            if (rpkt->stream_index == vsIdx) {
                if (avcodec_send_packet(decCtx, rpkt) == 0) {
                    while (avcodec_receive_frame(decCtx, frame) == 0) {
                        // Pixel-domain corruption on the decoded YUV first —
                        // bitstream overrides then shape how x264 encodes it.
                        applyMBEdits(frame, frameIdx, mbCols, mbRows,
                                     refBuf, activeEdits);

                        // Point picIn at the decoded frame's planes.
                        for (int p = 0; p < 3; p++) {
                            picIn.img.plane[p]    = frame->data[p];
                            picIn.img.i_stride[p] = frame->linesize[p];
                        }
                        picIn.i_pts = frameIdx;

                        // Populate per-MB override arrays for this frame.
                        const FrameMBParams* eParams =
                            activeEdits.contains(frameIdx)
                                ? &activeEdits[frameIdx] : nullptr;
                        setupOverrides(eParams);

                        encodeAndMux(&picIn);

                        // Ring buffer — store the decoded (post-edit) frame so
                        // subsequent frames can ghost-blend / mv-drift against it.
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

        // Flush x264 — pump nulls until no delayed frames remain.
        while (x264_encoder_delayed_frames(x264Enc))
            encodeAndMux(nullptr);
    }

    av_write_trailer(ofmt);
    ok = true;

cleanup:
    for (int i = 0; i < refBuf.size(); ++i)
        if (refBuf[i]) av_frame_free(&refBuf[i]);

    if (rpkt)    av_packet_free(&rpkt);
    if (frame)   av_frame_free(&frame);
    if (decCtx)  avcodec_free_context(&decCtx);
    if (x264Enc) x264_encoder_close(x264Enc);
    if (ofmt && !(ofmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt->pb);
    if (ofmt)    avformat_free_context(ofmt);
    if (ifmt)    avformat_close_input(&ifmt);

    if (ok) {
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


// ── Reorder frames ────────────────────────────────────────────────────────
// Decodes all frames into RAM, re-encodes them in the requested display order.
// m_frameIndices must be [sourceIdx, insertBeforeIdx].
// "insertBeforeIdx" is in original (pre-move) coordinates: the moved frame is
// placed just before the frame that currently has that index.
void FrameTransformerWorker::runReorderFrames()
{
    ControlLogger::instance().logApplyStarted(m_totalFrames, 0);
    ControlLogger::instance().logRenderPath(
        "REORDER PATH (full decode → re-encode with new frame order)");

    if (m_frameIndices.size() < 2) {
        emit done(false, "ReorderFrames: frameIndices must be [source, insertBefore]");
        return;
    }
    const int srcIdx    = m_frameIndices[0];
    const int insertBef = m_frameIndices[1];

    // No-op: dropping at the same position or immediately after is a no-op.
    if (insertBef == srcIdx || insertBef == srcIdx + 1) {
        ControlLogger::instance().logApplyCompleted(true);
        emit done(true, "");
        return;
    }

    const QString tempPath = m_videoPath + ".xform_tmp.mp4";
    QString errorMsg;
    bool    ok = false;

    AVFormatContext* ifmt   = nullptr;
    AVCodecContext*  decCtx = nullptr;
    AVFormatContext* ofmt   = nullptr;
    AVCodecContext*  encCtx = nullptr;
    AVPacket* rpkt  = nullptr;
    AVPacket* wpkt  = nullptr;
    AVFrame*  frame = nullptr;
    int vsIdx = -1;
    QVector<AVFrame*> allFrames;

    // ── Open input + decoder ───────────────────────────────────────────────
    if (avformat_open_input(&ifmt, m_videoPath.toUtf8().constData(),
                            nullptr, nullptr) < 0) {
        errorMsg = "Cannot open input: " + m_videoPath; goto reorder_cleanup;
    }
    if (avformat_find_stream_info(ifmt, nullptr) < 0) {
        errorMsg = "avformat_find_stream_info failed"; goto reorder_cleanup;
    }
    for (unsigned i = 0; i < ifmt->nb_streams; i++) {
        if (ifmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vsIdx = (int)i; break;
        }
    }
    if (vsIdx < 0) { errorMsg = "No video stream"; goto reorder_cleanup; }

    {
        AVStream* inS = ifmt->streams[vsIdx];
        const AVCodec* dec = avcodec_find_decoder(inS->codecpar->codec_id);
        if (!dec) { errorMsg = "No decoder"; goto reorder_cleanup; }
        decCtx = avcodec_alloc_context3(dec);
        if (avcodec_parameters_to_context(decCtx, inS->codecpar) < 0) goto reorder_cleanup;
        if (avcodec_open2(decCtx, dec, nullptr) < 0) {
            errorMsg = "Cannot open decoder"; goto reorder_cleanup;
        }
    }

    // ── Decode all frames into a RAM buffer ────────────────────────────────
    frame = av_frame_alloc();
    rpkt  = av_packet_alloc();
    {
        while (av_read_frame(ifmt, rpkt) >= 0) {
            if (rpkt->stream_index == vsIdx) {
                if (avcodec_send_packet(decCtx, rpkt) == 0) {
                    while (avcodec_receive_frame(decCtx, frame) == 0) {
                        AVFrame* stored = av_frame_clone(frame);
                        stored->pict_type = AV_PICTURE_TYPE_NONE;
                        allFrames.append(stored);
                        av_frame_unref(frame);
                    }
                }
            }
            av_packet_unref(rpkt);
        }
        avcodec_send_packet(decCtx, nullptr);   // flush
        while (avcodec_receive_frame(decCtx, frame) == 0) {
            AVFrame* stored = av_frame_clone(frame);
            stored->pict_type = AV_PICTURE_TYPE_NONE;
            allFrames.append(stored);
            av_frame_unref(frame);
        }
    }

    if (allFrames.isEmpty()) {
        errorMsg = "No frames decoded"; goto reorder_cleanup;
    }

    // ── Build new display order ────────────────────────────────────────────
    // For each original index i (0..N-1):
    //   • if i == insertBef  → emit srcIdx first, then i (if i != srcIdx)
    //   • else if i == srcIdx → skip (it was already emitted above)
    //   • else                → emit i
    // If insertBef >= N, srcIdx is appended at the end.
    {
        const int N = allFrames.size();
        QVector<int> newOrder;
        newOrder.reserve(N);
        for (int i = 0; i < N; i++) {
            if (i == insertBef) newOrder.append(srcIdx);
            if (i != srcIdx)    newOrder.append(i);
        }
        if (insertBef >= N) newOrder.append(srcIdx);

        // ── Open encoder + output ──────────────────────────────────────────
        {
            AVStream* inS = ifmt->streams[vsIdx];

            if (avformat_alloc_output_context2(&ofmt, nullptr, nullptr,
                                               tempPath.toUtf8().constData()) < 0) {
                errorMsg = "Cannot alloc output context"; goto reorder_cleanup;
            }
            const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
            if (!enc) { errorMsg = "H.264 encoder not found"; goto reorder_cleanup; }

            encCtx = avcodec_alloc_context3(enc);
            encCtx->width               = decCtx->width;
            encCtx->height              = decCtx->height;
            encCtx->pix_fmt             = AV_PIX_FMT_YUV420P;
            encCtx->sample_aspect_ratio = decCtx->sample_aspect_ratio;
            encCtx->framerate           = inS->avg_frame_rate;
            encCtx->time_base           = inS->time_base;
            if (encCtx->framerate.num <= 0 || encCtx->framerate.den <= 0) {
                encCtx->framerate = AVRational{24, 1};
                encCtx->time_base = AVRational{1, 24};
            }
            encCtx->bit_rate     = 0;
            encCtx->max_b_frames = 0;   // no B-frames keeps encode order == display order
            encCtx->gop_size     = 250;
            av_opt_set(encCtx->priv_data, "crf", "18", 0);
            av_opt_set(encCtx->priv_data, "x264-params",
                       "keyint=250:min-keyint=250:scenecut=0:bframes=0:b-adapt=0", 0);

            if (ofmt->oformat->flags & AVFMT_GLOBALHEADER)
                encCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            if (avcodec_open2(encCtx, enc, nullptr) < 0) {
                errorMsg = "Cannot open encoder"; goto reorder_cleanup;
            }

            AVStream* outS = avformat_new_stream(ofmt, nullptr);
            avcodec_parameters_from_context(outS->codecpar, encCtx);
            outS->time_base      = encCtx->time_base;
            outS->avg_frame_rate = encCtx->framerate;

            if (!(ofmt->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&ofmt->pb, tempPath.toUtf8().constData(),
                              AVIO_FLAG_WRITE) < 0) {
                    errorMsg = "Cannot open output file"; goto reorder_cleanup;
                }
            }
            if (avformat_write_header(ofmt, nullptr) < 0) {
                errorMsg = "avformat_write_header failed"; goto reorder_cleanup;
            }
        }

        // ── Encode in new order ────────────────────────────────────────────
        wpkt = av_packet_alloc();
        {
            int64_t outFrameCount = 0;
            int64_t frameDuration = 1;
            if (encCtx->framerate.num > 0 && encCtx->framerate.den > 0) {
                frameDuration = av_rescale_q(
                    1, AVRational{encCtx->framerate.den, encCtx->framerate.num},
                    encCtx->time_base);
                if (frameDuration <= 0) frameDuration = 1;
            }
            AVStream* outS = ofmt->streams[0];

            for (int i = 0; i < newOrder.size(); i++) {
                AVFrame* f = allFrames[newOrder[i]];
                f->pts = outFrameCount * frameDuration;
                outFrameCount++;
                if (avcodec_send_frame(encCtx, f) == 0) {
                    while (avcodec_receive_packet(encCtx, wpkt) == 0) {
                        wpkt->stream_index = 0;
                        av_packet_rescale_ts(wpkt, encCtx->time_base, outS->time_base);
                        av_interleaved_write_frame(ofmt, wpkt);
                        av_packet_unref(wpkt);
                    }
                }
                emit progress(i + 1, N);
            }

            // Flush encoder
            avcodec_send_frame(encCtx, nullptr);
            while (avcodec_receive_packet(encCtx, wpkt) == 0) {
                wpkt->stream_index = 0;
                av_packet_rescale_ts(wpkt, encCtx->time_base, outS->time_base);
                av_interleaved_write_frame(ofmt, wpkt);
                av_packet_unref(wpkt);
            }
        }
        av_write_trailer(ofmt);
        ok = true;
    }

reorder_cleanup:
    for (AVFrame* f : allFrames) av_frame_free(&f);
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

// ── Main re-encode work ────────────────────────────────────────────────────
void FrameTransformerWorker::run()
{
    // Delete is handled by bitstream splice — no decode/re-encode required.
    if (m_targetType == DeleteFrames) {
        runBitstreamSplice();
        return;
    }

    // Reorder decodes all frames into memory then re-encodes in the new order.
    if (m_targetType == ReorderFrames) {
        runReorderFrames();
        return;
    }

    // LaMoshPit-Edge: bitstream-surgery render path.
    // When the user has set any per-MB bitstream knob (CBP Zero, Force Skip,
    // Force MB Type, Force Intra Mode, MVD Injection, DCT Scale), route to
    // runBitstreamEdit() which calls our forked libx264 directly so the
    // override arrays in x264_picture_t.prop actually reach the encoder.
    // Pixel-domain knobs on the same frames are still applied (they act on
    // the decoded YUV before it is fed to x264).  For now this path is
    // scoped to MBEditOnly; combining with force-type or splice operations
    // is future work.
    if (m_targetType == MBEditOnly && anyBitstreamEdits(m_mbEdits)) {
        runBitstreamEdit();
        return;
    }

    // Log Apply start — records total frames and how many carry edits.
    ControlLogger::instance().logApplyStarted(m_totalFrames, m_mbEdits.size());
    ControlLogger::instance().logRenderPath(
        "PIXEL-DOMAIN PATH (FFmpeg-wrapped libx264)");

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

        // GOP size.
        // When killIFrames is active we must use an infinite GOP (keyint=9999)
        // regardless of the gopSize setting.  x264 WILL insert a forced IDR at
        // every keyint boundary even when every frame is hinted as
        // AV_PICTURE_TYPE_P — the keyint schedule is applied after frame-type
        // decisions and cannot be overridden by pict_type alone.  With a 335-
        // frame clip and the default gopSize=250 this produces an I-frame at
        // frame 250 every single encode, which is exactly what the user sees.
        // gopSize=0 is the user-visible "infinite GOP" option; killIFrames
        // also forces it internally.
        const int gopSize = (killI || m_globalParams.gopSize == 0) ? 9999
                          : (m_globalParams.gopSize > 0) ? m_globalParams.gopSize
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
        x264p += QString("keyint=%1:min-keyint=%1:scenecut=%2:")
                     .arg(gopSize)
                     .arg(m_globalParams.scenecut ? 40 : 0);
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
        // Partitions — build the x264 --partitions string from the three
        // per-frame-type MB Type dropdowns (iFrameMbType / pFrameMbType /
        // bFrameMbType).  Each is -1 "default" or 0..N for specific
        // subdivision restriction levels.  When ALL three are default, we
        // don't emit --partitions at all and x264 uses its natural default
        // (p8x8,b8x8,i8x8,i4x4 in the medium preset).  Otherwise, each -1
        // defaults to the x264-natural value for that slice type, so
        // changing just one dropdown doesn't inadvertently restrict others.
        if (m_globalParams.iFrameMbType >= 0 ||
            m_globalParams.pFrameMbType >= 0 ||
            m_globalParams.bFrameMbType >= 0)
        {
            QStringList parts;
            // Intra — x264 default is i8x8 + i4x4
            int iEff = (m_globalParams.iFrameMbType < 0) ? 2 : m_globalParams.iFrameMbType;
            if (iEff >= 1) parts << "i8x8";
            if (iEff >= 2) parts << "i4x4";
            // P inter — x264 default is p8x8 (no p4x4)
            int pEff = (m_globalParams.pFrameMbType < 0) ? 1 : m_globalParams.pFrameMbType;
            if (pEff >= 1) parts << "p8x8";
            if (pEff >= 2) parts << "p4x4";
            // B inter — x264 default is b8x8
            int bEff = (m_globalParams.bFrameMbType < 0) ? 1 : m_globalParams.bFrameMbType;
            if (bEff >= 1) parts << "b8x8";
            x264p += QString("partitions=%1:")
                .arg(parts.isEmpty() ? QString("none") : parts.join(','));
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
        // Rate-control fidelity (only emitted when user explicitly enables)
        if (m_globalParams.qcompEnabled)
            x264p += QString("qcomp=%1:").arg(m_globalParams.qcomp, 0, 'f', 2);
        if (m_globalParams.ipratioEnabled)
            x264p += QString("ipratio=%1:").arg(m_globalParams.ipratio, 0, 'f', 2);
        if (m_globalParams.pbratioEnabled)
            x264p += QString("pbratio=%1:").arg(m_globalParams.pbratio, 0, 'f', 2);
        if (m_globalParams.deadzoneInterEnabled)
            x264p += QString("deadzone-inter=%1:").arg(m_globalParams.deadzoneInter);
        if (m_globalParams.deadzoneIntraEnabled)
            x264p += QString("deadzone-intra=%1:").arg(m_globalParams.deadzoneIntra);
        if (m_globalParams.qblurEnabled)
            x264p += QString("qblur=%1:").arg(m_globalParams.qblur, 0, 'f', 2);

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

        // Pre-compute selection bounds for InterpolateLeft / InterpolateRight.
        // minSel = earliest selected frame; maxSel = rightmost selected frame.
        int minSel = -1, maxSel = -1;
        if (m_targetType == InterpolateLeft || m_targetType == InterpolateRight) {
            for (int idx : m_frameIndices) {
                if (minSel < 0 || idx < minSel) minSel = idx;
                if (maxSel < 0 || idx > maxSel) maxSel = idx;
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
                            if (inSel) {
                                applyForceType(frame, m_targetType);
                            } else if (m_globalParams.killIFrames && frameIdx > 0) {
                                // Non-selected frames: still honour killIFrames so
                                // x264 doesn't auto-upgrade them to I at any boundary.
                                frame->pict_type = AV_PICTURE_TYPE_P;
                                frame->flags    &= ~AV_FRAME_FLAG_KEY;
                            } else {
                                frame->pict_type = AV_PICTURE_TYPE_NONE;
                            }
                            applyMBEdits(frame, frameIdx, mbCols, mbRows, refBuf, activeEdits);
                            encodeAndDrain(frame);

                        } else if (m_targetType == MBEditOnly) {
                            // ── MB Edits Only ────────────────────────────
                            // When killIFrames is active, force every non-IDR frame
                            // to AV_PICTURE_TYPE_P so x264 uses inter-prediction
                            // throughout.  Setting NONE here would let x264 insert
                            // I-frames at its own schedule, overriding the killIFrames
                            // request and resetting the datamosh effect at every GOP.
                            // Frame 0 is always left as NONE — it is the mandatory
                            // stream IDR and x264 cannot encode it as P or B.
                            if (m_globalParams.killIFrames && frameIdx > 0) {
                                frame->pict_type = AV_PICTURE_TYPE_P;
                                frame->flags    &= ~AV_FRAME_FLAG_KEY;
                            } else {
                                frame->pict_type = AV_PICTURE_TYPE_NONE;
                            }
                            applyMBEdits(frame, frameIdx, mbCols, mbRows, refBuf, activeEdits);
                            encodeAndDrain(frame);

                        } else if (m_targetType == FlipVertical || m_targetType == FlipHorizontal) {
                            // ── Flip / Flop ──────────────────────────────
                            if (inSel)
                                flipFrameInPlace(frame, m_targetType == FlipVertical);
                            frame->pict_type = AV_PICTURE_TYPE_NONE;
                            encodeAndDrain(frame);

                        } else if (m_targetType == DuplicateLeft || m_targetType == DuplicateRight) {
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

                        } else {
                            // ── Interpolate Left / Right ──────────────────
                            // All frames pass through unmodified.  At the
                            // trigger point m_interpolateCount new frames are
                            // inserted, each a linearly-weighted blend stepping
                            // from 'a' toward 'b' in equal increments:
                            //   InterpolateLeft:  trigger = frameIdx == minSel
                            //     a = refBuf[0] (left neighbour)
                            //     b = current frame (minSel)
                            //   InterpolateRight: trigger = frameIdx == maxSel+1
                            //     a = refBuf[0] (rightmost selected frame)
                            //     b = current frame (maxSel+1)
                            //
                            // For N inserted frames the alphas are:
                            //   k/(N+1)  for k = 1 … N
                            // so the sequence becomes:
                            //   [..., a, interp_1, interp_2, …, interp_N, b, ...]
                            frame->pict_type = AV_PICTURE_TYPE_NONE;

                            bool doBlend = false;
                            if (m_targetType == InterpolateLeft &&
                                frameIdx == minSel && frameIdx > 0 && refBuf[0])
                                doBlend = true;
                            else if (m_targetType == InterpolateRight &&
                                     maxSel >= 0 && frameIdx == maxSel + 1 && refBuf[0])
                                doBlend = true;

                            if (doBlend) {
                                const int N = qMax(1, m_interpolateCount);
                                for (int k = 1; k <= N; ++k) {
                                    const float alpha = (float)k / (float)(N + 1);
                                    AVFrame* blend = blendFramesWeighted(refBuf[0], frame, alpha);
                                    if (blend) {
                                        encodeAndDrain(blend);
                                        av_frame_free(&blend);
                                    }
                                }
                            }
                            encodeAndDrain(frame);
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
