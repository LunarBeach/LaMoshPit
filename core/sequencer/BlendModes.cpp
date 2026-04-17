#include "core/sequencer/BlendModes.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace sequencer {

namespace {

// All helpers are per-channel 8-bit integer ops with /255 rounded nearest
// (classic trick: (x*y + 127) / 255).  Cheap and accurate enough for
// video-domain blending; floating-point per pixel adds noticeable cost
// without perceptible quality gain at 8-bit output depth.

inline uint8_t lerpU8(uint8_t a, uint8_t b, float t) {
    // Branchless: v is guaranteed in [0, 256) for a,b ∈ [0,255] and t ∈ [0,1]
    // (applyBlend clamps alpha to [0,1] on entry), so the explicit saturation
    // branches that used to live here prevented auto-vectorisation without
    // catching any real cases.  uint8_t truncation clamps 255.5f → 255 safely.
    return uint8_t(float(a) + (float(b) - float(a)) * t + 0.5f);
}

inline uint8_t mulU8(uint8_t a, uint8_t b) {
    return uint8_t((int(a) * int(b) + 127) / 255);
}

inline uint8_t screenU8(uint8_t a, uint8_t b) {
    const int ia = 255 - int(a);
    const int ib = 255 - int(b);
    return uint8_t(255 - ((ia * ib + 127) / 255));
}

inline uint8_t addU8(uint8_t a, uint8_t b) {
    const int s = int(a) + int(b);
    return uint8_t(s > 255 ? 255 : s);
}

inline uint8_t overlayU8(uint8_t dst, uint8_t src) {
    // Overlay = (dst < 128) ? 2*dst*src/255 : 255 - 2*(255-dst)*(255-src)/255
    // dst is the "background" term — layer is blended onto it.
    if (dst < 128) {
        return uint8_t((2 * int(dst) * int(src) + 127) / 255);
    } else {
        const int id = 255 - int(dst);
        const int is = 255 - int(src);
        return uint8_t(255 - ((2 * id * is + 127) / 255));
    }
}

// Per-mode blend kernel used by blendLoop below.  Each returns the pre-
// alpha "blended source" channel value for one channel of one pixel.
// Templating the loop over these keeps the pixel-loop body branch-free,
// which is what lets the compiler vectorise it.
struct KNormal   { static inline uint8_t c(uint8_t /*d*/, uint8_t s) { return s; } };
struct KMultiply { static inline uint8_t c(uint8_t d, uint8_t s) { return mulU8(d, s); } };
struct KScreen   { static inline uint8_t c(uint8_t d, uint8_t s) { return screenU8(d, s); } };
struct KAdd      { static inline uint8_t c(uint8_t d, uint8_t s) { return addU8(d, s); } };
struct KOverlay  { static inline uint8_t c(uint8_t d, uint8_t s) { return overlayU8(d, s); } };

template <class K>
inline void blendLoop(QImage& dst, const QImage& src, float alpha)
{
    const int w = dst.width();
    const int h = dst.height();
    for (int y = 0; y < h; ++y) {
        uint8_t* dRow = dst.scanLine(y);
        const uint8_t* sRow = src.constScanLine(y);
        for (int x = 0; x < w * 4; x += 4) {
            // BGRA byte order on little-endian (Qt's ARGB32 in memory).
            const uint8_t dB = dRow[x + 0];
            const uint8_t dG = dRow[x + 1];
            const uint8_t dR = dRow[x + 2];
            const uint8_t sB = sRow[x + 0];
            const uint8_t sG = sRow[x + 1];
            const uint8_t sR = sRow[x + 2];

            const uint8_t bB = K::c(dB, sB);
            const uint8_t bG = K::c(dG, sG);
            const uint8_t bR = K::c(dR, sR);

            dRow[x + 0] = lerpU8(dB, bB, alpha);
            dRow[x + 1] = lerpU8(dG, bG, alpha);
            dRow[x + 2] = lerpU8(dR, bR, alpha);
            dRow[x + 3] = 255;
        }
    }
}

} // namespace

void applyBlend(QImage& dst, const QImage& src, float alpha, BlendMode mode)
{
    if (alpha <= 0.0f) return;
    if (alpha > 1.0f)  alpha = 1.0f;

    const int w = dst.width();
    const int h = dst.height();
    if (w <= 0 || h <= 0) return;
    if (src.width() != w || src.height() != h) return;

    // Fast path — Normal @ alpha == 1 is a straight copy.  This is the hot
    // path: every clip with default properties (opacity=1, blendMode=Normal
    // and not mid-fade) hits this branch, which is the vast majority of
    // compositing work during playback.  memcpy vectorises down to
    // SIMD-width loads/stores; the pixel-loop form below does not because
    // the lerpU8 call keeps a float dependency on the critical path.
    if (mode == BlendMode::Normal && alpha >= 1.0f) {
        const size_t rowBytes = size_t(w) * 4;
        for (int y = 0; y < h; ++y) {
            std::memcpy(dst.scanLine(y), src.constScanLine(y), rowBytes);
        }
        return;
    }

    // Second fast path — Normal @ 0 < alpha < 1 is a pure alpha-over blend
    // (dst' = dst*(1-a) + src*a).  Integer-only math so the compiler can
    // auto-vectorise the inner loop.  The /255 compiles down to a magic-
    // number multiply on x86_64.
    //
    // (We tried delegating to QPainter::drawImage with setOpacity, which
    // SHOULD be faster via Qt's SSE-optimised raster kernels — but
    // QPainter on a QImage from a non-GUI worker thread appears to block
    // indefinitely in this Qt build.  The router's event loop stalls for
    // tens of seconds on the first alpha-blend frame.  Keep this pure-
    // integer path until that's diagnosed in Qt.)
    if (mode == BlendMode::Normal) {
        const int a8    = int(alpha * 255.0f + 0.5f);   // 0 .. 255
        const int invA8 = 255 - a8;
        for (int y = 0; y < h; ++y) {
            uint8_t* dRow = dst.scanLine(y);
            const uint8_t* sRow = src.constScanLine(y);
            for (int x = 0; x < w * 4; x += 4) {
                dRow[x+0] = uint8_t((dRow[x+0] * invA8 + sRow[x+0] * a8 + 127) / 255);
                dRow[x+1] = uint8_t((dRow[x+1] * invA8 + sRow[x+1] * a8 + 127) / 255);
                dRow[x+2] = uint8_t((dRow[x+2] * invA8 + sRow[x+2] * a8 + 127) / 255);
                dRow[x+3] = 255;
            }
        }
        return;
    }

    // Slow path — hoist the blend-mode dispatch out of the pixel loop so
    // the inner body is straight-line and vectorisable.  The generic
    // template inlines K::c at each call site.
    switch (mode) {
    case BlendMode::Normal:   blendLoop<KNormal>  (dst, src, alpha); break;
    case BlendMode::Multiply: blendLoop<KMultiply>(dst, src, alpha); break;
    case BlendMode::Screen:   blendLoop<KScreen>  (dst, src, alpha); break;
    case BlendMode::Add:      blendLoop<KAdd>     (dst, src, alpha); break;
    case BlendMode::Overlay:  blendLoop<KOverlay> (dst, src, alpha); break;
    }
}

} // namespace sequencer
