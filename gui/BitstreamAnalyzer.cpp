#include "BitstreamAnalyzer.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVector>
#include <cmath>
#include <cstdint>
#include <climits>
#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/motion_vector.h>
#include "h264_stream.h"
}

// =============================================================================
// Internal helpers
// =============================================================================

static QString sliceTypeName(int slice_type)
{
    switch (slice_type % 5) {
        case 0: return "P";
        case 1: return "B";
        case 2: return "I";
        case 3: return "SP";
        case 4: return "SI";
        default: return "?";
    }
}

static QString profileName(int profile_idc)
{
    switch (profile_idc) {
        case 66:  return "Baseline";
        case 77:  return "Main";
        case 88:  return "Extended";
        case 100: return "High";
        case 110: return "High 10";
        case 122: return "High 4:2:2";
        case 244: return "High 4:4:4 Predictive";
        default:  return QString("Unknown (%1)").arg(profile_idc);
    }
}

static QString chromaFormatName(int chroma_format_idc)
{
    switch (chroma_format_idc) {
        case 0: return "Monochrome (4:0:0)";
        case 1: return "4:2:0";
        case 2: return "4:2:2";
        case 3: return "4:4:4";
        default: return "Unknown";
    }
}

static QString videoFormatName(int fmt)
{
    switch (fmt) {
        case 0: return "Component";
        case 1: return "PAL";
        case 2: return "NTSC";
        case 3: return "SECAM";
        case 4: return "MAC";
        case 5: return "Unspecified";
        default: return "Reserved";
    }
}

static QString deblockyName(int idc)
{
    switch (idc) {
        case 0: return "ENABLED (filter ON across all boundaries)";
        case 1: return "DISABLED (filter OFF — block edges visible)";
        case 2: return "ENABLED within slice only (no cross-slice filtering)";
        default: return "Unknown";
    }
}

static QString weightedBipredName(int idc)
{
    switch (idc) {
        case 0: return "No weighted prediction";
        case 1: return "Explicit weighted prediction";
        case 2: return "Implicit weighted prediction";
        default: return "Unknown";
    }
}

// Fill SPSInfo from h264bitstream sps_t struct
static void fillSPSInfo(sps_t* sps, SPSInfo& out)
{
    out.profile_idc = sps->profile_idc;
    out.level_idc   = sps->level_idc;
    out.constraint_flags =
        (sps->constraint_set0_flag << 7) | (sps->constraint_set1_flag << 6) |
        (sps->constraint_set2_flag << 5) | (sps->constraint_set3_flag << 4) |
        (sps->constraint_set4_flag << 3) | (sps->constraint_set5_flag << 2);

    out.chroma_format_idc = sps->chroma_format_idc;
    out.bit_depth_luma    = sps->bit_depth_luma_minus8 + 8;
    out.bit_depth_chroma  = sps->bit_depth_chroma_minus8 + 8;

    out.num_ref_frames             = sps->num_ref_frames;
    out.log2_max_frame_num         = sps->log2_max_frame_num_minus4 + 4;
    out.pic_order_cnt_type         = sps->pic_order_cnt_type;
    out.log2_max_pic_order_cnt_lsb = sps->log2_max_pic_order_cnt_lsb_minus4 + 4;

    out.mb_width  = sps->pic_width_in_mbs_minus1 + 1;
    out.mb_height = sps->pic_height_in_map_units_minus1 + 1;
    out.total_mbs = out.mb_width * out.mb_height;

    // Pixel dimensions after cropping (chroma_format_idc=1 → multiply offsets by 2)
    int cropUnitX = (sps->chroma_format_idc == 0) ? 1 : 2;
    int cropUnitY = (sps->chroma_format_idc == 0) ? 1 : 2;
    out.frame_width_px  = out.mb_width  * 16
                          - cropUnitX * (sps->frame_crop_left_offset + sps->frame_crop_right_offset);
    out.frame_height_px = out.mb_height * 16
                          - cropUnitY * (sps->frame_crop_top_offset  + sps->frame_crop_bottom_offset);

    out.frame_mbs_only          = (sps->frame_mbs_only_flag          != 0);
    out.mb_adaptive_frame_field = (sps->mb_adaptive_frame_field_flag != 0);
    out.direct_8x8_inference    = (sps->direct_8x8_inference_flag    != 0);

    out.vui_present = (sps->vui_parameters_present_flag != 0);
    if (out.vui_present) {
        out.timing_info_present = (sps->vui.timing_info_present_flag != 0);
        if (out.timing_info_present) {
            out.num_units_in_tick = sps->vui.num_units_in_tick;
            out.time_scale        = sps->vui.time_scale;
            out.fixed_frame_rate  = (sps->vui.fixed_frame_rate_flag != 0);
            if (out.num_units_in_tick > 0)
                out.frame_rate_fps = (double)out.time_scale / (2.0 * out.num_units_in_tick);
        }
        out.aspect_ratio_info_present = (sps->vui.aspect_ratio_info_present_flag != 0);
        if (out.aspect_ratio_info_present) {
            out.aspect_ratio_idc = sps->vui.aspect_ratio_idc;
            out.sar_width        = sps->vui.sar_width;
            out.sar_height       = sps->vui.sar_height;
        }
        out.video_signal_type_present = (sps->vui.video_signal_type_present_flag != 0);
        if (out.video_signal_type_present) {
            out.video_format             = sps->vui.video_format;
            out.video_full_range         = (sps->vui.video_full_range_flag != 0);
            out.colour_primaries         = sps->vui.colour_primaries;
            out.transfer_characteristics = sps->vui.transfer_characteristics;
            out.matrix_coefficients      = sps->vui.matrix_coefficients;
        }
        out.bitstream_restriction_present = (sps->vui.bitstream_restriction_flag != 0);
        if (out.bitstream_restriction_present) {
            out.max_dec_frame_buffering = sps->vui.max_dec_frame_buffering;
            out.num_reorder_frames      = sps->vui.num_reorder_frames;
        }
    }
    out.parsed = true;
}

// Fill PPSInfo from h264bitstream pps_t struct
static void fillPPSInfo(pps_t* pps, PPSInfo& out)
{
    out.pps_id = pps->pic_parameter_set_id;
    out.sps_id = pps->seq_parameter_set_id;

    out.entropy_coding_mode           = (pps->entropy_coding_mode_flag       != 0);
    out.pic_init_qp                   =  pps->pic_init_qp_minus26 + 26;
    out.pic_init_qs                   =  pps->pic_init_qs_minus26 + 26;
    out.chroma_qp_index_offset        =  pps->chroma_qp_index_offset;
    out.second_chroma_qp_index_offset =  pps->second_chroma_qp_index_offset;

    out.deblocking_filter_control_present = (pps->deblocking_filter_control_present_flag != 0);
    out.constrained_intra_pred            = (pps->constrained_intra_pred_flag            != 0);
    out.redundant_pic_cnt_present         = (pps->redundant_pic_cnt_present_flag          != 0);
    out.transform_8x8_mode                = (pps->transform_8x8_mode_flag                != 0);
    out.weighted_pred                     = (pps->weighted_pred_flag                      != 0);
    out.weighted_bipred_idc               =  pps->weighted_bipred_idc;
    out.num_ref_idx_l0_default            =  pps->num_ref_idx_l0_active_minus1 + 1;
    out.num_ref_idx_l1_default            =  pps->num_ref_idx_l1_active_minus1 + 1;
    out.parsed = true;
}

// Parse a raw NALU (no start code) from AVCC extradata.
// Uses the SHARED h264_stream_t so that SPS/PPS tables are populated for later
// slice-header parsing by the same stream object.
static void parseNaluFromExtradata(h264_stream_t* h, const uint8_t* naluData, int naluSize,
                                   SPSInfo& sps, PPSInfo& pps)
{
    if (naluSize <= 0) return;

    // Prepend 4-byte Annex B start code so find_nal_unit can locate it.
    QByteArray buf(4 + naluSize, '\0');
    buf[3] = '\x01';   // → 00 00 00 01
    memcpy(buf.data() + 4, naluData, naluSize);

    int nal_start = 0, nal_end = 0;
    uint8_t* p = reinterpret_cast<uint8_t*>(buf.data());
    int sz = buf.size();

    // find_nal_unit returns >0 when a terminating start code is found,
    // -1 when the NAL runs to end-of-buffer (no next start code) — that
    // is the normal case for a single-NALU buffer.  Both mean "found NAL".
    int ret = find_nal_unit(p, sz, &nal_start, &nal_end);
    if (ret == 0) return; // no start code found at all

    QByteArray nalCopy(reinterpret_cast<const char*>(p + nal_start), nal_end - nal_start);
    read_nal_unit(h,
                  reinterpret_cast<uint8_t*>(nalCopy.data()),
                  nalCopy.size());

    if (h->nal->nal_unit_type == NAL_UNIT_TYPE_SPS && h->sps && !sps.parsed)
        fillSPSInfo(h->sps, sps);
    else if (h->nal->nal_unit_type == NAL_UNIT_TYPE_PPS && h->pps && !pps.parsed)
        fillPPSInfo(h->pps, pps);
}

// Extract SPS + PPS from AVCC AVCDecoderConfigurationRecord in codec extradata.
// Parses into the SHARED h264_stream_t so its SPS/PPS tables are ready for
// slice-header parsing later in the same pass.
// Returns the naluLengthSize (usually 4).
static int parseExtradataForSPSPPS(const uint8_t* extra, int extraSize,
                                   SPSInfo& sps, PPSInfo& pps,
                                   h264_stream_t* h)
{
    int naluLengthSize = 4;
    if (!extra || extraSize < 7 || extra[0] != 1)
        return naluLengthSize;

    naluLengthSize = (extra[4] & 0x03) + 1;

    int offset = 5;
    int numSPS = extra[offset++] & 0x1F;
    for (int i = 0; i < numSPS && offset + 2 <= extraSize; i++) {
        int len = ((uint16_t)extra[offset] << 8) | extra[offset + 1];
        offset += 2;
        if (offset + len > extraSize) break;
        parseNaluFromExtradata(h, extra + offset, len, sps, pps);
        offset += len;
    }

    if (offset < extraSize) {
        int numPPS = extra[offset++];
        for (int i = 0; i < numPPS && offset + 2 <= extraSize; i++) {
            int len = ((uint16_t)extra[offset] << 8) | extra[offset + 1];
            offset += 2;
            if (offset + len > extraSize) break;
            parseNaluFromExtradata(h, extra + offset, len, sps, pps);
            offset += len;
        }
    }

    return naluLengthSize;
}

// Convert one AVCC packet to Annex B and parse every slice header NAL it contains.
static void parsePacketSliceHeaders(const uint8_t* pktData, int pktSize,
                                    int naluLengthSize, h264_stream_t* h,
                                    const PPSInfo& pps, int sliceIndex,
                                    QVector<SliceInfo>& slices)
{
    // 1. Build Annex B version of this packet
    QByteArray annexB;
    annexB.reserve(pktSize + 64);

    int offset = 0;
    while (offset + naluLengthSize <= pktSize) {
        int naluSize = 0;
        for (int b = 0; b < naluLengthSize; b++)
            naluSize = (naluSize << 8) | pktData[offset + b];
        offset += naluLengthSize;
        if (naluSize <= 0 || offset + naluSize > pktSize) break;

        // Annex B start code
        annexB.append('\x00'); annexB.append('\x00');
        annexB.append('\x00'); annexB.append('\x01');
        annexB.append(reinterpret_cast<const char*>(pktData + offset), naluSize);
        offset += naluSize;
    }

    // 2. Walk through all NAL units in the Annex B buffer.
    //    find_nal_unit returns:  >0 = found NAL with terminating start code
    //                            -1 = found NAL that runs to end of buffer (last NAL)
    //                             0 = no start code found
    //    Both >0 and -1 mean a valid NAL was located; only 0 is a real failure.
    uint8_t* base = reinterpret_cast<uint8_t*>(annexB.data());
    int remaining  = annexB.size();
    int nal_start  = 0, nal_end = 0;

    while (remaining > 4) {
        int ret = find_nal_unit(base, remaining, &nal_start, &nal_end);
        if (ret == 0) break; // no start code found anywhere in remainder

        // Copy NAL bytes — read_nal_unit may modify the buffer during RBSP extraction
        QByteArray nalCopy(reinterpret_cast<const char*>(base + nal_start),
                           nal_end - nal_start);
        read_nal_unit(h,
                      reinterpret_cast<uint8_t*>(nalCopy.data()),
                      nalCopy.size());

        int nalType = h->nal->nal_unit_type;
        bool isSlice = (nalType == NAL_UNIT_TYPE_CODED_SLICE_NON_IDR ||
                        nalType == NAL_UNIT_TYPE_CODED_SLICE_IDR);

        if (isSlice && h->sh) {
            SliceInfo si;
            si.sliceIndex   = sliceIndex;
            si.nalType      = nalType;
            si.nal_ref_idc  = h->nal->nal_ref_idc;
            si.isIDR        = (nalType == NAL_UNIT_TYPE_CODED_SLICE_IDR);
            si.slice_type   = h->sh->slice_type;
            si.sliceTypeStr = sliceTypeName(h->sh->slice_type);
            si.frame_num            = h->sh->frame_num;
            si.pic_order_cnt_lsb    = h->sh->pic_order_cnt_lsb;
            si.idr_pic_id           = h->sh->idr_pic_id;
            si.first_mb_in_slice    = h->sh->first_mb_in_slice;

            si.slice_qp_delta = h->sh->slice_qp_delta;
            si.effective_qp   = pps.pic_init_qp + h->sh->slice_qp_delta;
            si.slice_qs_delta = h->sh->slice_qs_delta;

            if (h->sh->num_ref_idx_active_override_flag) {
                si.num_ref_idx_l0 = h->sh->num_ref_idx_l0_active_minus1 + 1;
                si.num_ref_idx_l1 = h->sh->num_ref_idx_l1_active_minus1 + 1;
            } else {
                si.num_ref_idx_l0 = pps.num_ref_idx_l0_default;
                si.num_ref_idx_l1 = pps.num_ref_idx_l1_default;
            }

            si.disable_deblocking    = h->sh->disable_deblocking_filter_idc;
            si.slice_alpha_c0_offset = h->sh->slice_alpha_c0_offset_div2 * 2;
            si.slice_beta_offset     = h->sh->slice_beta_offset_div2     * 2;
            si.cabac_init_idc        = h->sh->cabac_init_idc;

            si.ref_reorder_l0 = (h->sh->rplr.ref_pic_list_reordering_flag_l0 != 0);
            si.ref_reorder_l1 = (h->sh->rplr.ref_pic_list_reordering_flag_l1 != 0);
            si.adaptive_ref_pic_marking = (h->sh->drpm.adaptive_ref_pic_marking_mode_flag != 0);
            si.long_term_ref            = (h->sh->drpm.long_term_reference_flag            != 0);
            si.pktByteSize  = pktSize;

            slices.append(si);
        }

        if (ret < 0) break; // last NAL — nothing left to search
        // Advance past this NAL; nal_end is the offset from base to the NAL data end
        base      += nal_end;
        remaining -= nal_end;
    }
}

// =============================================================================
// analyzeVideo — main entry point
// Two passes through the file:
//   Pass 1: demux packets → h264bitstream slice header parse
//   Pass 2: decode frames → FFmpeg motion vector export
// Both use the same AVFormatContext session opened only once, rewound between.
// =============================================================================
AnalysisReport BitstreamAnalyzer::analyzeVideo(const QString& videoPath)
{
    AnalysisReport report;
    report.filePath = videoPath;

    QFileInfo fi(videoPath);
    if (!fi.exists()) {
        report.errorMessage = "File not found: " + videoPath;
        return report;
    }
    report.fileSizeBytes = fi.size();

    const QByteArray pathBytes = videoPath.toUtf8();
    const char* path = pathBytes.constData();

    // ─────────────────────────────────────────────────────────────────────
    // PASS 1 — Demux + h264bitstream structural parse
    // ─────────────────────────────────────────────────────────────────────
    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, path, nullptr, nullptr) < 0) {
        report.errorMessage = "avformat_open_input failed: " + videoPath;
        return report;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        report.errorMessage = "avformat_find_stream_info failed";
        return report;
    }

    int videoStreamIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = (int)i;
            break;
        }
    }
    if (videoStreamIdx < 0) {
        avformat_close_input(&fmtCtx);
        report.errorMessage = "No video stream found";
        return report;
    }

    AVStream* vs = fmtCtx->streams[videoStreamIdx];
    AVCodecParameters* par = vs->codecpar;

    // Create one shared h264_stream_t for the entire pass.
    // Extradata SPS/PPS are parsed into it first so the stream has the correct
    // parameter context when we parse slice headers from packets.
    h264_stream_t* h = h264_new();

    // Parse SPS / PPS from AVCC extradata into the shared stream
    int naluLengthSize = parseExtradataForSPSPPS(par->extradata,
                                                  par->extradata_size,
                                                  report.sps, report.pps, h);
    qDebug() << "BitstreamAnalyzer: extradata parsed — SPS:" << report.sps.parsed
             << " PPS:" << report.pps.parsed
             << " naluLengthSize:" << naluLengthSize;

    AVPacket* pkt = av_packet_alloc();
    int sliceCounter = 0;

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == videoStreamIdx) {
            parsePacketSliceHeaders(pkt->data, pkt->size,
                                    naluLengthSize, h, report.pps,
                                    sliceCounter++, report.slices);
        }
        av_packet_unref(pkt);
    }

    h264_free(h);
    av_packet_free(&pkt);
    avformat_close_input(&fmtCtx);
    qDebug() << "BitstreamAnalyzer: pass 1 complete —" << sliceCounter
             << "packets read," << report.slices.size() << "slices parsed";

    // ─────────────────────────────────────────────────────────────────────
    // PASS 2 — Decode frames for motion vector export + frame stats
    // ─────────────────────────────────────────────────────────────────────
    if (avformat_open_input(&fmtCtx, path, nullptr, nullptr) >= 0 &&
        avformat_find_stream_info(fmtCtx, nullptr) >= 0)
    {
        // Re-find video stream index (could differ in theory, be safe)
        int vsIdx2 = -1;
        for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
            if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                vsIdx2 = (int)i; break;
            }
        }

        if (vsIdx2 >= 0) {
            AVCodecParameters* par2 = fmtCtx->streams[vsIdx2]->codecpar;
            const AVCodec* codec    = avcodec_find_decoder(par2->codec_id);
            AVCodecContext* decCtx  = avcodec_alloc_context3(codec);

            if (decCtx && codec &&
                avcodec_parameters_to_context(decCtx, par2) >= 0)
            {
                // Enable motion vector export
                decCtx->flags2 |= AV_CODEC_FLAG2_EXPORT_MVS;

                if (avcodec_open2(decCtx, codec, nullptr) >= 0) {
                    AVPacket* pkt2  = av_packet_alloc();
                    AVFrame*  frame = av_frame_alloc();
                    int frameIdx    = 0;

                    double mvMagSum       = 0.0;
                    double frameMVMagMax  = 0.0;

                    while (av_read_frame(fmtCtx, pkt2) >= 0) {
                        if (pkt2->stream_index == vsIdx2) {
                            int64_t thisPktSize = pkt2->size;
                            if (avcodec_send_packet(decCtx, pkt2) == 0) {
                                while (avcodec_receive_frame(decCtx, frame) == 0) {
                                    FrameInfo fi2;
                                    fi2.frameIndex    = frameIdx++;
                                    fi2.codedPicNum   = frameIdx - 1; // decode order
                                    fi2.displayPicNum = frameIdx - 1; // approx presentation order
                                    fi2.keyFrame      = (frame->flags & AV_FRAME_FLAG_KEY) != 0;
                                    fi2.pktSize       = thisPktSize;
                                    fi2.pts           = frame->pts;

                                    switch (frame->pict_type) {
                                        case AV_PICTURE_TYPE_I: fi2.pictType = 'I'; break;
                                        case AV_PICTURE_TYPE_P: fi2.pictType = 'P'; break;
                                        case AV_PICTURE_TYPE_B: fi2.pictType = 'B'; break;
                                        default:                fi2.pictType = '?'; break;
                                    }

                                    // Motion vector side data
                                    frameMVMagMax = 0.0;
                                    AVFrameSideData* sd =
                                        av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS);
                                    if (sd) {
                                        const AVMotionVector* mvs =
                                            reinterpret_cast<const AVMotionVector*>(sd->data);
                                        int nbMVs = (int)(sd->size / sizeof(AVMotionVector));
                                        fi2.mvCount = nbMVs;
                                        report.totalMVs += nbMVs;

                                        for (int m = 0; m < nbMVs; m++) {
                                            const AVMotionVector& mv = mvs[m];

                                            // Convert from 1/4-pixel units to pixels
                                            double dx = mv.motion_x / (double)mv.motion_scale;
                                            double dy = mv.motion_y / (double)mv.motion_scale;
                                            double mag = std::sqrt(dx * dx + dy * dy);

                                            mvMagSum += mag;
                                            if (mag > frameMVMagMax) frameMVMagMax = mag;
                                            if (mag > report.maxMVMagnitude) {
                                                report.maxMVMagnitude = mag;
                                                report.maxMVFrame     = fi2.frameIndex;
                                            }

                                            if (mag < 0.5)       { report.zeroMVs++;    report.mvBucket[0]++; }
                                            else if (mag < 4.0)  { report.mvBucket[0]++; }
                                            else if (mag < 16.0) { report.mvBucket[1]++; }
                                            else if (mag < 64.0) { report.mvBucket[2]++; }
                                            else                 { report.mvBucket[3]++; }

                                            if (mv.source < 0)
                                                report.forwardMVs++;
                                            else
                                                report.backwardMVs++;
                                        }
                                    }

                                    report.frames.append(fi2);
                                    av_frame_unref(frame);
                                }
                            }
                        }
                        av_packet_unref(pkt2);
                    }

                    // Flush decoder
                    avcodec_send_packet(decCtx, nullptr);
                    while (avcodec_receive_frame(decCtx, frame) == 0) {
                        FrameInfo fi2;
                        fi2.frameIndex  = frameIdx++;
                        fi2.codedPicNum = frameIdx - 1;
                        fi2.pktSize     = 0;
                        switch (frame->pict_type) {
                            case AV_PICTURE_TYPE_I: fi2.pictType = 'I'; break;
                            case AV_PICTURE_TYPE_P: fi2.pictType = 'P'; break;
                            case AV_PICTURE_TYPE_B: fi2.pictType = 'B'; break;
                            default:                fi2.pictType = '?'; break;
                        }
                        fi2.keyFrame = (frame->flags & AV_FRAME_FLAG_KEY) != 0;
                        report.frames.append(fi2);
                        av_frame_unref(frame);
                    }

                    if (report.totalMVs > 0)
                        report.avgMVMagnitude = mvMagSum / report.totalMVs;

                    av_frame_free(&frame);
                    av_packet_free(&pkt2);
                }
            }
            if (decCtx) avcodec_free_context(&decCtx);
        }
        avformat_close_input(&fmtCtx);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Aggregate statistics
    // ─────────────────────────────────────────────────────────────────────
    report.totalFrames = report.slices.size();
    report.baseQP      = report.pps.pic_init_qp;
    report.minEffQP    = 51;
    report.maxEffQP    = 0;

    double qpSum          = 0.0;
    double iSizeSum = 0.0, pSizeSum = 0.0, bSizeSum = 0.0;
    int64_t minSz = INT64_MAX, maxSz = 0;
    int minIdx = -1, maxIdx = -1;

    for (int i = 0; i < report.slices.size(); i++) {
        const SliceInfo& s = report.slices[i];

        // Frame type counts
        QString t = s.sliceTypeStr;
        if      (t == "I") report.iFrameCount++;
        else if (t == "P") report.pFrameCount++;
        else if (t == "B") report.bFrameCount++;

        // IDR positions
        if (s.isIDR) report.idrPositions.append(i);

        // QP
        qpSum += s.effective_qp;
        if (s.effective_qp < report.minEffQP) report.minEffQP = s.effective_qp;
        if (s.effective_qp > report.maxEffQP) report.maxEffQP = s.effective_qp;

        // Size
        report.totalEncodedBytes += s.pktByteSize;
        if (s.pktByteSize > maxSz) { maxSz = s.pktByteSize; maxIdx = i; }
        if (s.pktByteSize < minSz) { minSz = s.pktByteSize; minIdx = i; }

        if      (t == "I") iSizeSum += s.pktByteSize;
        else if (t == "P") pSizeSum += s.pktByteSize;
        else if (t == "B") bSizeSum += s.pktByteSize;
    }

    if (report.totalFrames > 0) {
        report.avgEffQP      = qpSum / report.totalFrames;
        report.avgFrameBytes = (double)report.totalEncodedBytes / report.totalFrames;
        report.maxFrameBytes = maxSz;
        report.maxFrameIndex = maxIdx;
        report.minFrameBytes = (minSz == INT64_MAX) ? 0 : minSz;
        report.minFrameIndex = minIdx;
    }
    if (report.iFrameCount > 0) report.avgIFrameBytes = iSizeSum / report.iFrameCount;
    if (report.pFrameCount > 0) report.avgPFrameBytes = pSizeSum / report.pFrameCount;
    if (report.bFrameCount > 0) report.avgBFrameBytes = bSizeSum / report.bFrameCount;

    // Average GOP size
    if (report.idrPositions.size() > 1) {
        double gapSum = 0;
        for (int g = 1; g < report.idrPositions.size(); g++)
            gapSum += report.idrPositions[g] - report.idrPositions[g - 1];
        report.avgGopSize = gapSum / (report.idrPositions.size() - 1);
    }

    report.success = !report.slices.isEmpty();
    if (!report.success)
        report.errorMessage = "No slice headers could be parsed — file may not be H.264 or may be corrupt.";

    return report;
}

// =============================================================================
// printAnalysis — quick console summary
// =============================================================================
void BitstreamAnalyzer::printAnalysis(const AnalysisReport& r)
{
    if (!r.success) { qDebug() << "Analysis failed:" << r.errorMessage; return; }
    qDebug() << "=== BitstreamAnalyzer ===";
    qDebug() << "  File :" << r.filePath;
    qDebug() << "  Size :" << r.fileSizeBytes << "bytes";
    qDebug() << "  Frames:" << r.totalFrames
             << " I:" << r.iFrameCount << " P:" << r.pFrameCount << " B:" << r.bFrameCount;
    if (r.sps.parsed)
        qDebug() << "  Res  :" << r.sps.frame_width_px << "x" << r.sps.frame_height_px
                 << " Profile:" << profileName(r.sps.profile_idc)
                 << " Level:" << (r.sps.level_idc / 10) << "." << (r.sps.level_idc % 10);
    if (r.pps.parsed)
        qDebug() << "  Entropy:" << (r.pps.entropy_coding_mode ? "CABAC" : "CAVLC")
                 << " BaseQP:" << r.pps.pic_init_qp;
}

// =============================================================================
// saveAnalysisToFile — write the full structured report
// =============================================================================
void BitstreamAnalyzer::saveAnalysisToFile(const AnalysisReport& r, const QString& videoPath)
{
    QString outPath = QFileInfo(videoPath).absolutePath() + "/"
                    + QFileInfo(videoPath).baseName() + "_analysis.txt";

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "BitstreamAnalyzer: cannot write" << outPath;
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    auto line = [&](const QString& s = "") { out << s << "\n"; };
    auto section = [&](const QString& title) {
        line();
        QString bar(title.length() + 4, '=');
        line(bar);
        line("  " + title);
        line(bar);
    };
    auto f = [&](const QString& label, const QString& value) {
        out << QString("  %1  %2\n").arg(label, -42).arg(value);
    };
    auto fi = [&](const QString& label, int value) {
        f(label, QString::number(value));
    };
    auto fb = [&](const QString& label, bool value) {
        f(label, value ? "YES" : "NO");
    };

    // ── Header ────────────────────────────────────────────────────────────
    line("================================================================================");
    line("  LaMoshPit — H.264 Bitstream Analysis Report");
    line("================================================================================");
    f("Analyzed",       QDateTime::currentDateTime().toString("yyyy-MM-dd  hh:mm:ss"));
    f("Source file",    r.filePath);
    f("File size",      QString("%1 bytes (%2 MB)")
                          .arg(r.fileSizeBytes)
                          .arg(r.fileSizeBytes / 1048576.0, 0, 'f', 2));
    if (!r.success) {
        line();
        line("  ERROR: " + r.errorMessage);
        file.close();
        return;
    }

    // ── Section 1: SPS ─────────────────────────────────────────────────
    section("SECTION 1 — SEQUENCE PARAMETERS (SPS)");
    if (!r.sps.parsed) {
        line("  [SPS not found in extradata]");
    } else {
        f("Profile",
          QString("%1 (%2)").arg(profileName(r.sps.profile_idc)).arg(r.sps.profile_idc));
        f("Level",
          QString("%1.%2  (level_idc = %3)")
              .arg(r.sps.level_idc / 10).arg(r.sps.level_idc % 10).arg(r.sps.level_idc));
        f("Constraint flags byte",
          QString("0x%1").arg(r.sps.constraint_flags, 2, 16, QChar('0')));
        line();
        f("Frame dimensions (pixels)",
          QString("%1 x %2").arg(r.sps.frame_width_px).arg(r.sps.frame_height_px));
        f("Frame dimensions (macroblocks)",
          QString("%1 x %2  (%3 MBs total, each 16x16 px)")
              .arg(r.sps.mb_width).arg(r.sps.mb_height).arg(r.sps.total_mbs));
        f("Chroma format",
          QString("%1  (chroma_format_idc = %2)")
              .arg(chromaFormatName(r.sps.chroma_format_idc)).arg(r.sps.chroma_format_idc));
        f("Bit depth luma / chroma",
          QString("%1 / %2  bits").arg(r.sps.bit_depth_luma).arg(r.sps.bit_depth_chroma));
        line();
        fi("Max reference frames (num_ref_frames)", r.sps.num_ref_frames);
        f("log2_max_frame_num",
          QString("%1  (frame_num range: 0 to %2)")
              .arg(r.sps.log2_max_frame_num)
              .arg((1 << r.sps.log2_max_frame_num) - 1));
        fi("POC type (pic_order_cnt_type)", r.sps.pic_order_cnt_type);
        if (r.sps.pic_order_cnt_type == 0)
            f("log2_max_pic_order_cnt_lsb",
              QString("%1  (range: 0 to %2)")
                  .arg(r.sps.log2_max_pic_order_cnt_lsb)
                  .arg((1 << r.sps.log2_max_pic_order_cnt_lsb) - 1));
        fb("frame_mbs_only_flag  (progressive)", r.sps.frame_mbs_only);
        fb("mb_adaptive_frame_field_flag",        r.sps.mb_adaptive_frame_field);
        fb("direct_8x8_inference_flag",           r.sps.direct_8x8_inference);
        line();
        fb("VUI parameters present", r.sps.vui_present);
        if (r.sps.vui_present) {
            fb("  Timing info present", r.sps.timing_info_present);
            if (r.sps.timing_info_present) {
                fi("  num_units_in_tick", r.sps.num_units_in_tick);
                fi("  time_scale",        r.sps.time_scale);
                fb("  fixed_frame_rate",  r.sps.fixed_frame_rate);
                f("  Derived frame rate",
                  r.sps.frame_rate_fps > 0
                    ? QString("%1 fps").arg(r.sps.frame_rate_fps, 0, 'f', 4)
                    : "N/A");
            }
            if (r.sps.aspect_ratio_info_present) {
                f("  aspect_ratio_idc", QString::number(r.sps.aspect_ratio_idc));
                if (r.sps.aspect_ratio_idc == 255)
                    f("  SAR (extended)",
                      QString("%1:%2").arg(r.sps.sar_width).arg(r.sps.sar_height));
            }
            if (r.sps.video_signal_type_present) {
                f("  video_format",            videoFormatName(r.sps.video_format));
                fb("  video_full_range_flag",   r.sps.video_full_range);
                fi("  colour_primaries",        r.sps.colour_primaries);
                fi("  transfer_characteristics",r.sps.transfer_characteristics);
                fi("  matrix_coefficients",     r.sps.matrix_coefficients);
            }
            if (r.sps.bitstream_restriction_present) {
                fi("  max_dec_frame_buffering", r.sps.max_dec_frame_buffering);
                fi("  num_reorder_frames",      r.sps.num_reorder_frames);
            }
        }
    }

    // ── Section 2: PPS ─────────────────────────────────────────────────
    section("SECTION 2 — PICTURE PARAMETERS (PPS)");
    if (!r.pps.parsed) {
        line("  [PPS not found in extradata]");
    } else {
        fi("PPS ID / SPS ID", r.pps.pps_id);
        f("Entropy coding mode",
          r.pps.entropy_coding_mode
            ? "CABAC  (Context-Adaptive Binary Arithmetic Coding)"
            : "CAVLC  (Context-Adaptive Variable-Length Coding)");
        f("Base QP  (pic_init_qp)",
          QString("%1  (raw pic_init_qp_minus26 = %2)")
              .arg(r.pps.pic_init_qp).arg(r.pps.pic_init_qp - 26));
        f("Base QS  (pic_init_qs)",
          QString("%1  (SP/SI frames only)").arg(r.pps.pic_init_qs));
        fi("chroma_qp_index_offset",         r.pps.chroma_qp_index_offset);
        fi("second_chroma_qp_index_offset",  r.pps.second_chroma_qp_index_offset);
        fb("deblocking_filter_control_present", r.pps.deblocking_filter_control_present);
        fb("constrained_intra_pred_flag",       r.pps.constrained_intra_pred);
        fb("redundant_pic_cnt_present_flag",    r.pps.redundant_pic_cnt_present);
        fb("transform_8x8_mode_flag  (8x8 DCT)", r.pps.transform_8x8_mode);
        fb("weighted_pred_flag",                 r.pps.weighted_pred);
        f("weighted_bipred_idc",
          QString("%1  — %2").arg(r.pps.weighted_bipred_idc)
                              .arg(weightedBipredName(r.pps.weighted_bipred_idc)));
        fi("num_ref_idx_l0_active (default)",   r.pps.num_ref_idx_l0_default);
        fi("num_ref_idx_l1_active (default)",   r.pps.num_ref_idx_l1_default);
    }

    // ── Section 3: GOP structure ────────────────────────────────────────
    section("SECTION 3 — GOP STRUCTURE & FRAME TYPE DISTRIBUTION");
    fi("Total frames parsed",    r.totalFrames);
    fi("I-frames",               r.iFrameCount);
    fi("P-frames",               r.pFrameCount);
    fi("B-frames",               r.bFrameCount);
    if (r.totalFrames > 0) {
        f("I-frame percentage",
          QString("%1 %").arg(100.0 * r.iFrameCount / r.totalFrames, 0, 'f', 1));
        f("P-frame percentage",
          QString("%1 %").arg(100.0 * r.pFrameCount / r.totalFrames, 0, 'f', 1));
        f("B-frame percentage",
          QString("%1 %").arg(100.0 * r.bFrameCount / r.totalFrames, 0, 'f', 1));
    }
    line();
    fi("IDR frame count",        r.idrPositions.size());
    if (r.avgGopSize > 0)
        f("Average GOP size",    QString("%1 frames").arg(r.avgGopSize, 0, 'f', 1));
    line("  IDR frame positions (slice index):");
    {
        QString idrs = "    ";
        for (int i = 0; i < r.idrPositions.size(); i++) {
            idrs += QString::number(r.idrPositions[i]);
            if (i < r.idrPositions.size() - 1) idrs += ", ";
            if ((i + 1) % 20 == 0) { line(idrs); idrs = "    "; }
        }
        if (!idrs.trimmed().isEmpty()) line(idrs);
    }

    // ── Section 4: Compression / size stats ────────────────────────────
    section("SECTION 4 — COMPRESSION & FRAME SIZE STATISTICS");
    f("Total encoded video bytes",
      QString("%1 bytes  (%2 MB)")
          .arg(r.totalEncodedBytes)
          .arg(r.totalEncodedBytes / 1048576.0, 0, 'f', 2));
    if (r.sps.vui_present && r.sps.timing_info_present && r.sps.frame_rate_fps > 0 && r.totalFrames > 0) {
        double durationSec = r.totalFrames / r.sps.frame_rate_fps;
        double avgBitrateMbps = (r.totalEncodedBytes * 8.0) / durationSec / 1e6;
        f("Estimated average bitrate",
          QString("%1 Mbps  (over %2 frames @ %3 fps)")
              .arg(avgBitrateMbps, 0, 'f', 2)
              .arg(r.totalFrames)
              .arg(r.sps.frame_rate_fps, 0, 'f', 4));
    }
    line();
    f("Average frame size (all)",
      QString("%1 bytes").arg(r.avgFrameBytes, 0, 'f', 0));
    f("Average I-frame size",
      r.iFrameCount > 0
        ? QString("%1 bytes").arg(r.avgIFrameBytes, 0, 'f', 0)
        : "N/A");
    f("Average P-frame size",
      r.pFrameCount > 0
        ? QString("%1 bytes").arg(r.avgPFrameBytes, 0, 'f', 0)
        : "N/A");
    f("Average B-frame size",
      r.bFrameCount > 0
        ? QString("%1 bytes").arg(r.avgBFrameBytes, 0, 'f', 0)
        : "N/A");
    line();
    f("Largest frame",
      r.maxFrameIndex >= 0
        ? QString("frame %1  (%2 bytes, type=%3, IDR=%4)")
              .arg(r.maxFrameIndex).arg(r.maxFrameBytes)
              .arg(r.maxFrameIndex < r.slices.size()
                   ? r.slices[r.maxFrameIndex].sliceTypeStr : "?")
              .arg(r.maxFrameIndex < r.slices.size()
                   ? (r.slices[r.maxFrameIndex].isIDR ? "YES" : "NO") : "?")
        : "N/A");
    f("Smallest frame",
      r.minFrameIndex >= 0
        ? QString("frame %1  (%2 bytes, type=%3)")
              .arg(r.minFrameIndex).arg(r.minFrameBytes)
              .arg(r.minFrameIndex < r.slices.size()
                   ? r.slices[r.minFrameIndex].sliceTypeStr : "?")
        : "N/A");

    // ── Section 5: QP stats ─────────────────────────────────────────────
    section("SECTION 5 — QUANTISATION PARAMETER (QP) STATISTICS");
    fi("Base QP from PPS (pic_init_qp)",    r.baseQP);
    fi("Minimum effective slice QP",        r.minEffQP);
    fi("Maximum effective slice QP",        r.maxEffQP);
    f("Average effective slice QP",
      QString("%1").arg(r.avgEffQP, 0, 'f', 2));
    line();
    line("  NOTE: QP range is 0 (lossless) to 51 (maximum compression/worst quality).");
    line("  Higher QP → larger quantisation step → more blocking and smearing artifacts.");
    line("  slice_qp_delta in each frame = (effective_qp - base_qp).  Manipulate this");
    line("  field to shift the quantisation of individual frames during live playback.");

    // QP distribution
    QMap<int, int> qpHistogram;
    for (const SliceInfo& s : r.slices)
        qpHistogram[s.effective_qp]++;
    line();
    line("  QP Distribution:");
    for (auto it = qpHistogram.begin(); it != qpHistogram.end(); ++it) {
        int pct = r.totalFrames > 0 ? (it.value() * 100 / r.totalFrames) : 0;
        QString bar(std::min(it.value() * 40 / std::max(r.totalFrames, 1) + 1, 40), '#');
        out << QString("    QP %1 : %2 frames (%3%)  %4\n")
               .arg(it.key(), 3).arg(it.value(), 5).arg(pct, 3).arg(bar);
    }

    // ── Section 6: Motion vectors ───────────────────────────────────────
    section("SECTION 6 — MOTION VECTOR STATISTICS  (from FFmpeg decode pass)");
    fi("Total motion vectors exported",     r.totalMVs);
    fi("  Forward  (L0 / past reference)",  r.forwardMVs);
    fi("  Backward (L1 / future reference)",r.backwardMVs);
    fi("  Zero or near-zero (< 0.5 px)",    r.zeroMVs);
    line();
    if (r.totalMVs > 0) {
        f("Average MV magnitude",
          QString("%1 pixels").arg(r.avgMVMagnitude, 0, 'f', 2));
        f("Maximum MV magnitude",
          QString("%1 pixels  (frame %2)")
              .arg(r.maxMVMagnitude, 0, 'f', 2).arg(r.maxMVFrame));
        line();
        line("  Magnitude distribution:");
        auto bucket = [&](const QString& label, int val) {
            int pct = r.totalMVs > 0 ? (val * 100 / r.totalMVs) : 0;
            QString bar(std::min(val * 40 / std::max(r.totalMVs, 1) + 1, 40), '#');
            out << QString("    %-22s  %6 vectors  (%3%)  %4\n")
                   .arg(label).arg(val).arg(pct, 3).arg(bar);
        };
        bucket("0 – 4 px   (fine motion)",    r.mvBucket[0]);
        bucket("4 – 16 px  (normal motion)",  r.mvBucket[1]);
        bucket("16 – 64 px (fast motion)",    r.mvBucket[2]);
        bucket("64+ px     (extreme motion)", r.mvBucket[3]);
    } else {
        line("  No motion vectors exported (video may be all I-frames, or decode pass failed).");
    }

    // ── Section 7: Per-frame slice header table ─────────────────────────
    section("SECTION 7 — PER-FRAME SLICE HEADER TABLE");
    line("  Columns: Idx | Type | IDR | RefI | FrameNum | POC | QP_delta | EffQP | "
         "L0refs | L1refs | Deblock | CABAC_idc | Size(bytes)");
    line("  " + QString(130, '-'));

    for (const SliceInfo& s : r.slices) {
        out << QString("  %1  %2  %3  %4  %5  %6  %7  %8  %9  %10  %11  %12  %13\n")
               .arg(s.sliceIndex,      5)
               .arg(s.sliceTypeStr,    4)
               .arg(s.isIDR ? "IDR" : "   ", 3)
               .arg(s.nal_ref_idc,     3)
               .arg(s.frame_num,       8)
               .arg(s.pic_order_cnt_lsb, 5)
               .arg(s.slice_qp_delta,  8)
               .arg(s.effective_qp,    5)
               .arg(s.num_ref_idx_l0,  6)
               .arg(s.num_ref_idx_l1,  6)
               .arg(s.disable_deblocking, 7)
               .arg(s.cabac_init_idc,  9)
               .arg(s.pktByteSize,    12);
    }

    // ── Section 8: FFmpeg decode frame table ────────────────────────────
    if (!r.frames.isEmpty()) {
        section("SECTION 8 — FFMPEG DECODED FRAME TABLE");
        line("  Columns: Idx | CodedPic | DisplayPic | Type | Keyframe | MVs | Size(bytes)");
        line("  " + QString(80, '-'));
        for (const FrameInfo& fr : r.frames) {
            out << QString("  %1  %2  %3  %4  %5  %6  %7\n")
                   .arg(fr.frameIndex,    5)
                   .arg(fr.codedPicNum,   8)
                   .arg(fr.displayPicNum, 10)
                   .arg(fr.pictType,      4)
                   .arg(fr.keyFrame ? "KEY" : "   ", 7)
                   .arg(fr.mvCount,       8)
                   .arg(fr.pktSize,      12);
        }
    }

    // ── Section 9: Manipulation guide ───────────────────────────────────
    section("SECTION 9 — MANIPULATION TARGETS FOR GLITCH ALGORITHMS");
    line();
    line("  ┌─ SLICE-LEVEL FIELDS (one value per 16x16-row macroblock strip / frame)");
    line("  │");
    line("  │  slice_type  (SliceInfo::slice_type)");
    line("  │    Current spread: " + [&]() {
        int counts[3] = {r.iFrameCount, r.pFrameCount, r.bFrameCount};
        return QString("I=%1  P=%2  B=%3").arg(counts[0]).arg(counts[1]).arg(counts[2]);
    }());
    line("  │    Changing P→I forces the decoder to treat the slice as intra-coded.");
    line("  │    Changing I→P without valid reference data causes prediction errors");
    line("  │    that cascade forward until the next IDR — the mosh-pit core effect.");
    line("  │");
    line("  │  slice_qp_delta  (SliceInfo::slice_qp_delta)");
    out << QString("  │    Current range: %1 to %2  (base QP=%3, effective range %4–%5)\n")
           .arg(r.minEffQP - r.baseQP).arg(r.maxEffQP - r.baseQP)
           .arg(r.baseQP).arg(r.minEffQP).arg(r.maxEffQP);
    line("  │    Clamped to [-(26 + pic_init_qp), 25] by spec.");
    line("  │    Increasing delta → bigger quantisation steps → blocking/smearing.");
    line("  │    Decreasing to large negative values → near-lossless, inflated bitstream.");
    line("  │");
    line("  │  disable_deblocking_filter_idc  (SliceInfo::disable_deblocking)");
    line(QString("  │    Current value in all frames: %1  — %2")
           .arg(r.slices.isEmpty() ? 0 : r.slices[0].disable_deblocking)
           .arg(deblockyName(r.slices.isEmpty() ? 0 : r.slices[0].disable_deblocking)));
    line("  │    Set to 1 during decode to expose raw 8x8/16x16 DCT block boundaries.");
    line("  │");
    line("  │  cabac_init_idc  (SliceInfo::cabac_init_idc)");
    out << QString("  │    Current value: %1  (valid: 0-2 for P/B slices, always 0 for I)\n")
           .arg(r.slices.isEmpty() ? 0 : r.slices[0].cabac_init_idc);
    line("  │    Mismatching this between write and decode misaligns all CABAC symbol");
    line("  │    probabilities → cascading corruption for the rest of the slice.");
    line("  │");
    line("  └─ MACROBLOCK-LEVEL FIELDS (require per-MB CABAC/CAVLC entropy decode)");
    line("     These are NOT in the slice header; they live inside the slice data RBSP.");
    line("     h264bitstream does NOT parse them — a full decoder is needed.");
    line();
    line("     mb_type           — intra/inter prediction mode per 16x16 block");
    line("                         (I_16x16, I_4x4, P_16x16, P_8x8, B_Direct, Skip, etc.)");
    line("     sub_mb_type       — prediction partition inside an 8x8 sub-MB");
    line("     ref_idx_l0/l1     — which reference frame (0 to num_ref_idx-1) to use");
    line("     mvd_l0/l1         — motion vector delta per block (in 1/4-pixel units)");
    line("     coded_block_pattern (CBP) — which 8x8 luma/chroma blocks carry residuals");
    line("     residual coefficients     — transform coefficients after quantisation");
    line();
    out << QString("     This video has %1 MBs per frame (%2 x %3).\n")
           .arg(r.sps.total_mbs).arg(r.sps.mb_width).arg(r.sps.mb_height);
    line("     Use FFmpeg's FF_DEBUG_MB_TYPE / FF_DEBUG_QP decoder debug flags, or");
    line("     implement a CABAC/CAVLC slice-data parser on top of h264bitstream's bs_t");
    line("     to get per-MB data for the HackedMacroblock manipulation layer.");

    line();
    line("================================================================================");
    line("  END OF REPORT");
    line("================================================================================");

    file.close();
    qDebug() << "BitstreamAnalyzer: report saved to" << outPath;
}
