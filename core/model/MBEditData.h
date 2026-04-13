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

    // ── Pixel-domain additions ──────────────────────────────────────────────
    //
    //  posterize     Reduce colour depth per MB.  Snaps pixel values to
    //                (2^posterize) levels.  8 = full precision (no effect),
    //                1 = binary (extreme banding).  Range 1..8.
    //
    //  pixelShuffle  Randomly permute pixel positions within each MB.
    //                The encoder sees spatially incoherent content.
    //                0 = off; higher = larger shuffle radius.  Range 0..32 px.
    //
    //  sharpen       Per-MB convolution: positive sharpens (amplifies edges),
    //                negative blurs (destroys detail).  0 = off.  Range −100..+100.
    //
    //  tempDiffAmp   Amplify the temporal difference (current − reference).
    //                Static areas stay clean; motion areas explode.
    //                0 = off, 100 = double the difference, 200 = triple.
    //                Requires refDepth > 0.  Range 0..200.
    //
    //  hueRotate     Continuous rotation in the UV colour plane (degrees).
    //                Preserves saturation while rotating the hue wheel.
    //                0 = off.  Range 0..359.
    int  posterize    = 8;   // 1 .. 8  (8 = off)
    int  pixelShuffle = 0;   // 0 .. 32 px
    int  sharpen      = 0;   // −100 .. +100
    int  tempDiffAmp  = 0;   // 0 .. 200
    int  hueRotate    = 0;   // 0 .. 359 degrees

    // ── Bitstream-domain controls ───────────────────────────────────────────
    //  These manipulate compressed H.264 macroblock syntax fields directly
    //  via the h264bitstream library (read→modify→write on NAL units).
    //
    //  bsMvdX/Y      Direct motion-vector-difference injection.  Overwrites
    //                the MVD in the compressed bitstream.  Range −128..+128.
    //
    //  bsForceSkip   Force the MB skip flag.  A skipped MB copies its content
    //                from the reference with zero residual.  0=off, 100=all.
    //                Range 0..100 (probability percentage per MB).
    //
    //  bsIntraMode   Override intra prediction mode for I-MBs.
    //                −1 = default (encoder chooses).
    //                0=Vertical, 1=Horizontal, 2=DC, 3=Plane.  Range −1..3.
    //
    //  bsMbType      Force macroblock partition type.
    //                −1 = default.  0=Skip, 1=I16x16, 2=I4x4,
    //                3=P16x16, 4=P8x8.  Range −1..4.
    //
    //  bsDctScale    Scale all DCT residual coefficients.
    //                100 = unchanged, 0 = zero all residual (flat prediction),
    //                200 = double all coefficients.  Range 0..200.
    //
    //  bsCbpZero     Zero the coded-block-pattern — drops all residual data.
    //                0 = off, 100 = zero CBP on all selected MBs.
    //                Range 0..100 (probability percentage per MB).
    int  bsMvdX       = 0;   // −128 .. +128
    int  bsMvdY       = 0;   // −128 .. +128
    int  bsForceSkip  = 0;   // 0 .. 100
    int  bsIntraMode  = -1;  // −1 .. 3
    int  bsMbType     = -1;  // −1 .. 4
    int  bsDctScale   = 100; // 0 .. 200
    int  bsCbpZero    = 0;   // 0 .. 100
};

// Frame display-order index → per-MB edit parameters.
using MBEditMap = QMap<int, FrameMBParams>;
