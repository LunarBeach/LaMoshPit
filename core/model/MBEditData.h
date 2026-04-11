#pragma once
#include <QSet>
#include <QMap>

// =============================================================================
// Per-frame macroblock editing parameters.
//
// All pixel-level operations are applied to the raw YUV frame *before* it is
// sent to the H.264 encoder, so the encoder sees "corrupted" reference content
// and is forced to produce destructive predictions.
//
// ── Quantisation ─────────────────────────────────────────────────────────────
//  qpDelta      QP offset via AVRegionOfInterest side data.
//               Positive = higher quantisation (more loss); negative = better.
//               Range: -51 .. +51.
//
// ── Motion / Temporal reference ──────────────────────────────────────────────
//  mvDriftX/Y   Pixel-shift applied to the Y+UV planes for selected MBs.
//               Reference content from refBuf[refDepth-1] is pasted in at
//               offset (driftX, driftY); the encoder finds perfect prediction
//               there and assigns those motion vectors.  Range: -128 .. +128.
//
//  refDepth     Which ring-buffer slot to use as the pixel source.
//               0 = disabled, 1 = 1 frame back … 7 = 7 frames back.
//
//  ghostBlend   0..100 %: blend selected-MB pixels toward the refDepth frame
//               at the *same* spatial position (temporal echo / ghost).
//               Applied independently of mvDrift.
//
// ── Luma (Y-plane) corruption ────────────────────────────────────────────────
//  noiseLevel   0..255: magnitude of per-pixel uniform random noise added to Y.
//  pixelOffset  -128..+127: signed DC shift added to every selected Y pixel.
//  invertLuma   0..100 %: blend percentage toward 255-Y (solarise / negative).
//
// ── Chroma (U+V planes) corruption ───────────────────────────────────────────
//  chromaDriftX/Y  Independent spatial shift of U+V planes from refDepth frame.
//                  Creates colour aberration separate from the luma drift.
//                  Range: -128 .. +128.
//  chromaOffset    Signed DC offset applied to both U and V pixels.  ±127.
//
// ── Spatial influence ────────────────────────────────────────────────────────
//  spillRadius  0..8: expand the effect region by N MB-widths in every
//               direction.  Creates a "blast radius" around each painted MB.
// =============================================================================
struct FrameMBParams {
    QSet<int> selectedMBs;

    // Quantisation
    int  qpDelta      = 0;   // -51 .. +51

    // Motion / temporal reference
    int  mvDriftX     = 0;   // -128 .. +128 px
    int  mvDriftY     = 0;   // -128 .. +128 px
    int  refDepth     = 0;   // 0 .. 7  (0 = disabled)
    int  ghostBlend   = 0;   // 0 .. 100 %

    // Luma corruption
    int  noiseLevel   = 0;   // 0 .. 255
    int  pixelOffset  = 0;   // -128 .. +127
    int  invertLuma   = 0;   // 0 .. 100 %

    // Chroma corruption
    int  chromaDriftX = 0;   // -128 .. +128 px
    int  chromaDriftY = 0;   // -128 .. +128 px
    int  chromaOffset = 0;   // -128 .. +127

    // Spatial influence
    int  spillRadius  = 0;   // 0 .. 8 MBs  (outward — this MB infects neighbours)
    int  sampleRadius = 0;   // 0 .. 8 MBs  (inward  — ghost blend samples a blurred
                             //              neighbourhood of the reference, not just
                             //              the exactly-aligned pixel; larger values
                             //              create a diffuse temporal smear)

    // Motion vector amplification
    //  mvAmplify   Multiplier applied to mvDriftX/Y before the pixel-shift.
    //              1 = no scaling, 8 = 8× the drift distance.  Acts like a
    //              gain stage on top of the raw drift values.  Range: 1 .. 16.
    int  mvAmplify    = 1;   // 1 .. 16

    // Temporal cascade
    //  cascadeLen   How many subsequent frames inherit the corruption.
    //               Each cascade frame uses ghostBlend toward the previous
    //               (already-corrupted) frame, propagating damage forward.
    //               0 = single-frame only.  Range: 0 .. 60.
    //
    //  cascadeDecay Per-frame strength reduction expressed as a percentage.
    //               0  = no decay (every cascade frame gets full effect).
    //               100 = each frame retains ~50 % of the previous frame's
    //               intensity (exponential halving).
    //               Maps to retainPerFrame = 1.0 − decay/200, range 0.5..1.0.
    int  cascadeLen   = 0;   // 0 .. 60 frames
    int  cascadeDecay = 0;   // 0 .. 100

    // ── Additional pixel-domain corruption ───────────────────────────────────
    //
    //  blockFlatten  Blend each MB's pixels toward the MB's own spatial average.
    //                100 % = perfectly flat block (uniform luma).  Forces the
    //                encoder toward skip-MBs or very large inter residuals.
    //                Range 0..100 %.
    //
    //  refScatter    Randomly offset the reference-frame sample position by up
    //                to ±refScatter pixels for each pixel in the MB.  Creates a
    //                "shattered glass" temporal artifact.  Range 0..32 px.
    //
    //  colorTwistU   Independent signed DC offset applied only to the U (Cb)
    //  colorTwistV   chroma plane.  Lets you steer hue separately from saturation.
    //                Range −127 .. +127 each.
    int  blockFlatten = 0;   // 0 .. 100 %
    int  refScatter   = 0;   // 0 .. 32 px
    int  colorTwistU  = 0;   // −127 .. +127
    int  colorTwistV  = 0;   // −127 .. +127
};

// Frame display-order index → per-MB edit parameters.
using MBEditMap = QMap<int, FrameMBParams>;
