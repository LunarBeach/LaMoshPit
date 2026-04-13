#pragma once
#include <QSet>

// =============================================================================
// GlobalEncodeParams — Encoder-level parameters applied to the ENTIRE encode.
//
// These reach inside libx264 and reshape how H.264 prediction works globally —
// not just per-MB pixel manipulation, but the fundamental decisions the encoder
// makes about block sizes, reference frames, motion search, residual coding, and
// perceptual optimization.
//
// Convention: value == -1 (or -1.0f) means "keep default".  Only set fields
// you want to override.
// =============================================================================
struct GlobalEncodeParams {

    // ── Frame structure ───────────────────────────────────────────────────────
    //  gopSize = 0 → effective infinite GOP (keyint=9999, only explicit IDRs).
    //  Large values maximise the temporal prediction chain = maximum datamosh.
    int  gopSize      = -1;   // -1=default(250); 0=infinite; >0=exact keyint
    int  bFrames      = -1;   // -1=default(3);  0..16
    int  bAdapt       = -1;   // -1=default(2);  0=off, 1=fast, 2=trellis BA
    int  refFrames    = -1;   // -1=default(3);  1..16 reference frames kept

    // ── Rate control ──────────────────────────────────────────────────────────
    //  qpOverride ≥ 0  →  switch to fixed-CQP mode (all frames same QP).
    //  qpMin/qpMax constrain the quantiser range in CRF mode.
    int  qpOverride   = -1;   // -1=CRF18; 0..51=force this QP
    int  qpMin        = -1;   // -1=unchanged; 0..51
    int  qpMax        = -1;   // -1=unchanged; 0..51

    // ── Motion estimation ─────────────────────────────────────────────────────
    //  meMethod=0 (dia) is the least accurate → most wrong MVs → most artifacts.
    //  meRange:  wider search = encoder finds more accurate matches = less artifact.
    //            narrower search = wrong predictions = more smear.
    //  subpelRef: 0=no subpel → integer-pixel MVs only → step-like artifacts.
    int  meMethod     = -1;   // -1=default; 0=dia,1=hex,2=umh,3=esa,4=tesa
    int  meRange      = -1;   // -1=default(16); 0=default; 4..512px search window
    int  subpelRef    = -1;   // -1=default; 0..10 subpixel refinement passes

    // ── Partition modes ───────────────────────────────────────────────────────
    //  Restricting to 16×16 only → crude predictions → big block artifacts.
    //  All partitions (p4×4 etc.) → very accurate → few artifacts (for research).
    int  partitionMode = -1;  // -1=default; 0=16x16_only; 1=p8x8; 2=all; 3=all+4x4
    bool use8x8DCT    = true; // false → 4×4 DCT only (lower freq capture)

    // ── B-frame prediction ────────────────────────────────────────────────────
    int  directMode   = -1;   // -1=default; 0=none,1=temporal,2=spatial,3=auto
    bool weightedPredB = true;// weighted prediction for B-frames
    int  weightedPredP = -1;  // -1=default; 0=off,1=blind,2=smart

    // ── Quantization ─────────────────────────────────────────────────────────
    //  trellis=0 → no RD-optimal coefficient zeroing → residuals noisier.
    //  noFastPSkip → encoder must fully evaluate every MB residual (no bail-out).
    //  noDctDecimate → near-zero DCT coefficients are kept, not zeroed out.
    int  trellis        = -1;  // -1=default; 0=off,1=final-MB,2=all-MBs
    bool noFastPSkip    = false;
    bool noDctDecimate  = false;

    // ── Entropy coding ────────────────────────────────────────────────────────
    bool cabacDisable   = false; // CAVLC instead of CABAC → less efficient → different artifacts

    // ── Deblocking loop filter ────────────────────────────────────────────────
    //  Disabling deblock preserves block boundaries → visible 16×16 grid.
    bool noDeblock      = false;
    int  deblockAlpha   = 0;   // -6..+6
    int  deblockBeta    = 0;   // -6..+6

    // ── Psychovisual optimization ─────────────────────────────────────────────
    //  psyRD=0  → no perceptual optimization → flat residual distribution.
    //  psyRD>1  → encoder adds grain to textures (high-frequency injection).
    float psyRD         = -1.0f; // -1=default; 0.0..5.0
    float psyTrellis    = -1.0f; // -1=default; 0.0..1.0
    int   aqMode        = -1;   // -1=default; 0=off,1=var,2=auto-var,3=auto-var+edge
    float aqStrength    = -1.0f; // -1=default; 0.0..3.0
    bool  mbTreeDisable = false; // disable macroblock-tree lookahead RC

    // ── I-frame elimination ───────────────────────────────────────────────────
    //  killIFrames  Force every decoded I-frame (except the mandatory stream IDR
    //               at frame 0) to AV_PICTURE_TYPE_P before encoding.  The frame
    //               count never changes; only the reference structure does.
    //               Combined with a large GOP size this creates an unbroken chain
    //               of P-frames — classic datamosh territory.
    bool killIFrames    = false;

    // ── Scene-cut detection ──────────────────────────────────────────────────
    //  scenecut  When ON, x264 detects scene transitions and automatically
    //            inserts I-frames at cuts.  OFF by default (hardcoded scenecut=0)
    //            to prevent surprise I-frames during datamoshing.
    bool scenecut       = false;

    // ── Lookahead ─────────────────────────────────────────────────────────────
    int  rcLookahead    = -1;  // -1=default; 0..250 frames

    // ── Rate-control fidelity ────────────────────────────────────────────────
    //  These parameters further inhibit x264's ability to override MB-level
    //  user edits.  Each has an explicit enable flag — when disabled (default),
    //  x264 uses its own defaults and the parameter is not emitted.
    //
    //  qcomp       QP curve compression: 0.0 = constant bitrate (QP varies
    //              greatly), 1.0 = constant QP (no variation).  Default 0.6.
    //  ipratio     I-frame QP ratio vs P-frames.  Default 1.4 (I gets 40 %
    //              lower QP).  1.0 = treat I same as P.
    //  pbratio     B-frame QP ratio vs P-frames.  Default 1.3 (B gets 30 %
    //              higher QP).  1.0 = treat B same as P.
    //  deadzoneInter  Coefficient deadzone for inter MBs.  Default 21.
    //                 0 = keep ALL coefficients (maximum fidelity).
    //  deadzoneIntra  Coefficient deadzone for intra MBs.  Default 11.
    //                 0 = keep ALL coefficients.
    //  qblur       Temporal QP smoothing radius.  Default 0.5.
    //              0.0 = no smoothing (frame-by-frame QP honoured exactly).
    bool  qcompEnabled        = false;
    float qcomp               = 0.6f;   // 0.0 .. 1.0

    bool  ipratioEnabled      = false;
    float ipratio             = 1.4f;   // 1.0 .. 2.0

    bool  pbratioEnabled      = false;
    float pbratio             = 1.3f;   // 1.0 .. 2.0

    bool  deadzoneInterEnabled = false;
    int   deadzoneInter       = 21;     // 0 .. 32

    bool  deadzoneIntraEnabled = false;
    int   deadzoneIntra       = 11;     // 0 .. 32

    bool  qblurEnabled        = false;
    float qblur               = 0.5f;   // 0.0 .. 10.0

    // ── Spatial mask (from MB painter) ────────────────────────────────────────
    //  If non-empty, an additional QP ROI is applied to these MBs on EVERY frame.
    //  Bridges the MB painter's spatial selection into the global encode pass.
    QSet<int> spatialMaskMBs;  // MB indices (row*mbCols+col); empty = no mask
    int       spatialMaskQP = 51; // QP offset applied to masked MBs each frame
};

// =============================================================================
// EncodePreset — named configurations targeting specific glitch aesthetics.
// =============================================================================
enum class EncodePreset {
    Default = 0,
    InfiniteGOP,      // pure P-chain, no auto IDRs
    MaxDatamosh,      // infinite GOP + max corruption flags + no psy + max QP
    SmearHeavy,       // ref=16, long GOP, no deblock, inaccurate ME → time smear
    GlitchWave,       // dia ME, wide search, all partitions, high psy → wave distort
    BlockMosaic,      // 16×16 only, no deblock, no subpel → giant block artifacts
    ChromaFever,      // high AQ strength, edge-AQ mode → chroma saturation bleed
    QuantumResidue,   // full trellis, no-decimate, all partitions → micro-noise
    TemporalBleed,    // many B-frames, ref=16, temporal direct → time bleed
    DataCorrupt,      // high qpMin/qpMax, no deblock, no RC optimisations
};

inline GlobalEncodeParams presetParams(EncodePreset preset)
{
    GlobalEncodeParams p;
    switch (preset) {

    case EncodePreset::Default:
        break;

    case EncodePreset::InfiniteGOP:
        p.gopSize    = 0;
        p.bFrames    = 0;
        p.bAdapt     = 0;
        p.refFrames  = 4;
        p.killIFrames = true;
        break;

    case EncodePreset::MaxDatamosh:
        p.gopSize       = 0;
        p.bFrames       = 0;
        p.bAdapt        = 0;
        p.refFrames     = 8;
        p.killIFrames   = true;
        p.noDeblock     = true;
        p.noFastPSkip   = true;
        p.noDctDecimate = true;
        p.psyRD         = 0.0f;
        p.psyTrellis    = 0.0f;
        p.aqMode        = 0;
        p.mbTreeDisable = true;
        p.qpMax         = 51;
        p.subpelRef     = 0;
        p.trellis       = 0;
        p.meMethod      = 0;
        break;

    case EncodePreset::SmearHeavy:
        p.gopSize    = 0;
        p.killIFrames = true;
        p.bFrames    = 0;
        p.refFrames  = 16;
        p.noDeblock = true;
        p.subpelRef = 1;
        p.trellis   = 0;
        p.meMethod  = 0;
        p.meRange   = 64;
        p.mbTreeDisable = true;
        break;

    case EncodePreset::GlitchWave:
        p.meMethod      = 0;
        p.meRange       = 128;
        p.partitionMode = 2;
        p.psyRD         = 3.0f;
        p.trellis       = 2;
        p.noFastPSkip   = true;
        p.subpelRef     = 4;
        break;

    case EncodePreset::BlockMosaic:
        p.partitionMode = 0;
        p.subpelRef     = 0;
        p.trellis       = 0;
        p.noDeblock     = true;
        p.use8x8DCT     = false;
        p.meMethod      = 0;
        p.meRange       = 8;
        p.gopSize       = 0;
        break;

    case EncodePreset::ChromaFever:
        p.aqMode        = 3;
        p.aqStrength    = 3.0f;
        p.psyTrellis    = 0.0f;
        p.psyRD         = 1.5f;
        p.noDeblock     = true;
        p.refFrames     = 8;
        break;

    case EncodePreset::QuantumResidue:
        p.noDctDecimate = true;
        p.noFastPSkip   = true;
        p.trellis       = 2;
        p.use8x8DCT     = true;
        p.partitionMode = 3;
        p.subpelRef     = 10;
        p.psyRD         = 0.5f;
        break;

    case EncodePreset::TemporalBleed:
        p.gopSize        = 0;
        p.bFrames        = 8;
        p.bAdapt         = 2;
        p.refFrames      = 16;
        p.weightedPredB  = true;
        p.weightedPredP  = 2;
        p.directMode     = 1;
        p.noDeblock      = false;
        break;

    case EncodePreset::DataCorrupt:
        p.gopSize       = 0;
        p.qpMin         = 36;
        p.qpMax         = 51;
        p.noDeblock     = true;
        p.noFastPSkip   = true;
        p.noDctDecimate = true;
        p.mbTreeDisable = true;
        p.psyRD         = 0.0f;
        p.aqMode        = 0;
        p.meMethod      = 0;
        break;
    }
    return p;
}
