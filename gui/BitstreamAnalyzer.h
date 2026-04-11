#pragma once

#include <QString>
#include <QVector>

// =============================================================================
// SPS — Sequence Parameter Set
// Everything that defines the structure of the encoded sequence.
// These fields are fixed for the lifetime of the video (our standardized file
// always uses the same encoder settings, so this is the master reference).
// =============================================================================
struct SPSInfo {
    bool parsed = false;

    // Profile / Level
    int  profile_idc      = 0;   // 66=Baseline 77=Main 100=High
    int  level_idc        = 0;   // e.g. 41 = Level 4.1
    int  constraint_flags = 0;   // combined constraint_set0..5 byte

    // Decoded frame dimensions
    int  mb_width         = 0;   // frame width  in 16x16 macroblocks
    int  mb_height        = 0;   // frame height in 16x16 macroblocks
    int  frame_width_px   = 0;   // pixel width  after crop
    int  frame_height_px  = 0;   // pixel height after crop
    int  total_mbs        = 0;   // mb_width * mb_height

    // Chroma / bit depth
    int  chroma_format_idc  = 1; // 1=4:2:0  2=4:2:2  3=4:4:4
    int  bit_depth_luma     = 8;
    int  bit_depth_chroma   = 8;

    // Reference / DPB
    int  num_ref_frames             = 0;
    int  log2_max_frame_num         = 0; // frame_num wraps at 2^this
    int  pic_order_cnt_type         = 0;
    int  log2_max_pic_order_cnt_lsb = 0;

    // Frame structure
    bool frame_mbs_only          = true;  // false = interlaced
    bool mb_adaptive_frame_field = false;
    bool direct_8x8_inference    = false;

    // VUI — timing / colour
    bool   vui_present         = false;
    bool   timing_info_present = false;
    int    num_units_in_tick   = 0;
    int    time_scale          = 0;
    bool   fixed_frame_rate    = false;
    double frame_rate_fps      = 0.0;   // derived: time_scale / (2 * num_units_in_tick)

    bool aspect_ratio_info_present = false;
    int  aspect_ratio_idc          = 0;
    int  sar_width                 = 0;
    int  sar_height                = 0;

    bool video_signal_type_present = false;
    int  video_format              = 5;  // 5 = Unspecified
    bool video_full_range          = false;
    int  colour_primaries          = 0;
    int  transfer_characteristics  = 0;
    int  matrix_coefficients       = 0;

    bool bitstream_restriction_present = false;
    int  max_dec_frame_buffering       = 0;
    int  num_reorder_frames            = 0;
};

// =============================================================================
// PPS — Picture Parameter Set
// Encoding settings that govern every slice in the sequence.
// =============================================================================
struct PPSInfo {
    bool parsed = false;

    int  pps_id = 0;
    int  sps_id = 0;

    bool entropy_coding_mode           = false; // true=CABAC false=CAVLC
    int  pic_init_qp                   = 26;    // base QP (pic_init_qp_minus26 + 26)
    int  pic_init_qs                   = 26;
    int  chroma_qp_index_offset        = 0;
    int  second_chroma_qp_index_offset = 0;

    bool deblocking_filter_control_present = true;
    bool constrained_intra_pred            = false;
    bool redundant_pic_cnt_present         = false;
    bool transform_8x8_mode                = false;

    bool weighted_pred          = false;
    int  weighted_bipred_idc    = 0; // 0=none 1=explicit 2=implicit
    int  num_ref_idx_l0_default = 0;
    int  num_ref_idx_l1_default = 0;
};

// =============================================================================
// SliceInfo — per-slice data from h264bitstream slice-header parse
// For our single-slice-per-frame video this is effectively per-frame.
// These are the fields that glitch algorithms will directly modify.
// =============================================================================
struct SliceInfo {
    int  sliceIndex      = 0;

    // NAL header
    int  nalType         = 0; // 1=non-IDR coded slice  5=IDR coded slice
    int  nal_ref_idc     = 0; // 3=highest ref priority  0=disposable
    bool isIDR           = false;

    // Slice type (spec 7.4.3 Table 7-6)
    int     slice_type   = 0; // 0=P 1=B 2=I 3=SP 4=SI  (+5 for *_ONLY variants)
    QString sliceTypeStr;     // "I" "P" "B" "SP" "SI"

    // Timing / ordering
    int frame_num          = 0;
    int pic_order_cnt_lsb  = 0;
    int idr_pic_id         = 0;
    int first_mb_in_slice  = 0; // 0 for first (and usually only) slice in frame

    // Quantisation  ← PRIMARY MANIPULATION TARGET
    int slice_qp_delta = 0;  // applied on top of pps.pic_init_qp
    int effective_qp   = 0;  // actual QP = pic_init_qp + slice_qp_delta
    int slice_qs_delta = 0;  // SP/SI frames only

    // Reference lists  ← how many reference frames this slice can use
    int num_ref_idx_l0 = 0;  // L0 (past / forward)
    int num_ref_idx_l1 = 0;  // L1 (future / backward, B-frames only)

    // Deblocking filter  ← ON/OFF manipulation target
    int disable_deblocking    = 0; // 0=filter ON  1=filter OFF  2=on but no cross-slice
    int slice_alpha_c0_offset = 0; // deblock alpha threshold (actual = 2 * _div2)
    int slice_beta_offset     = 0; // deblock beta  threshold (actual = 2 * _div2)

    // CABAC context  ← mismatching this cascades corruption across the frame
    int cabac_init_idc = 0; // context init table index: 0, 1, or 2

    // Reference picture reordering
    bool ref_reorder_l0 = false;
    bool ref_reorder_l1 = false;

    // Decoded ref pic marking (DRPM)
    bool adaptive_ref_pic_marking = false;
    bool long_term_ref            = false;

    // Raw compressed size for this NAL unit
    int pktByteSize = 0;
};

// =============================================================================
// FrameInfo — per-frame data from FFmpeg decode pass
// Gives presentation-order stats including motion vector counts.
// =============================================================================
struct FrameInfo {
    int  frameIndex     = 0;
    int  codedPicNum    = 0;
    int  displayPicNum  = 0;
    char pictType       = '?'; // 'I' 'P' 'B'
    bool keyFrame       = false;
    int  mvCount        = 0;   // number of motion vectors exported by FFmpeg
    int64_t pktSize     = 0;   // compressed packet size in bytes
    int64_t pts         = 0;
};

// =============================================================================
// AnalysisReport — the complete result of one analysis run
// =============================================================================
struct AnalysisReport {
    bool    success      = false;
    QString errorMessage;

    QString filePath;
    qint64  fileSizeBytes = 0;

    SPSInfo sps;
    PPSInfo pps;

    QVector<SliceInfo> slices;  // one entry per NAL slice unit (= one per frame for our video)
    QVector<FrameInfo> frames;  // one entry per decoded frame (presentation order)

    // ── GOP / frame type summary ──────────────────────────────────────────
    int          totalFrames = 0;
    int          iFrameCount = 0;
    int          pFrameCount = 0;
    int          bFrameCount = 0;
    QVector<int> idrPositions;   // slice indices that are IDR
    double       avgGopSize  = 0.0;

    // ── Compressed frame size stats ──────────────────────────────────────
    qint64  totalEncodedBytes = 0;
    double  avgFrameBytes     = 0.0;
    double  avgIFrameBytes    = 0.0;
    double  avgPFrameBytes    = 0.0;
    double  avgBFrameBytes    = 0.0;
    int64_t maxFrameBytes     = 0;
    int     maxFrameIndex     = -1;
    int64_t minFrameBytes     = INT64_MAX;
    int     minFrameIndex     = -1;

    // ── QP statistics (from slice headers) ───────────────────────────────
    int    baseQP    = 26;
    int    minEffQP  = 51;
    int    maxEffQP  = 0;
    double avgEffQP  = 0.0;

    // ── Motion vector statistics (from FFmpeg MV export) ─────────────────
    int    totalMVs       = 0;
    int    forwardMVs     = 0;  // L0 / past-reference
    int    backwardMVs    = 0;  // L1 / future-reference
    int    zeroMVs        = 0;  // magnitude < 0.5 pixels
    double avgMVMagnitude = 0.0;
    double maxMVMagnitude = 0.0;
    int    maxMVFrame     = -1;
    // Magnitude histogram buckets: [0-4), [4-16), [16-64), [64+) pixels
    int    mvBucket[4]    = {0, 0, 0, 0};
};

// =============================================================================
class BitstreamAnalyzer {
public:
    // Backward-compatibility alias so MainWindow.cpp compiles unchanged
    using AnalysisResult = AnalysisReport;

    static AnalysisReport analyzeVideo(const QString &videoPath);
    static void printAnalysis(const AnalysisReport &result);
    static void saveAnalysisToFile(const AnalysisReport &result, const QString &videoPath);
};
