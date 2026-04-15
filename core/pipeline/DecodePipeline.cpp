#include "DecodePipeline.h"
#include "core/util/HwEncoder.h"
#include <QDebug>
#include <cmath>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/rational.h>
#include <libavutil/display.h>
#include <libswscale/swscale.h>
}

bool DecodePipeline::standardizeVideo(const QString& inputFile, const QString& outputFile,
                                      ProgressCallback progress,
                                      bool useHwEncode)
{
    AVFormatContext *ifmt_ctx = nullptr, *ofmt_ctx = nullptr;
    AVCodecContext *dec_ctx = nullptr, *enc_ctx = nullptr;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    SwsContext *sws_to_nv12 = nullptr;   // active only for HW-encode path
    AVFrame *nv12_frame = nullptr;       // reused per-frame scratch buffer
    bool usingHw = false;
    int video_stream_idx = -1;
    bool success = false;

    // 1. Open Input
    if (avformat_open_input(&ifmt_ctx, inputFile.toUtf8().constData(), nullptr, nullptr) < 0) {
        qDebug() << "Could not open input file";
        goto cleanup;
    }
    
    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) {
        qDebug() << "Could not find stream info";
        goto cleanup;
    }

    // 2. Find Video Stream & Decoder
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    if (video_stream_idx == -1) {
        qDebug() << "No video stream found";
        goto cleanup;
    }

    {
        AVStream *in_stream = ifmt_ctx->streams[video_stream_idx];
        const AVCodec *decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
        if (!decoder) {
            qDebug() << "Could not find decoder";
            goto cleanup;
        }
        
        dec_ctx = avcodec_alloc_context3(decoder);
        if (avcodec_parameters_to_context(dec_ctx, in_stream->codecpar) < 0) goto cleanup;
        if (avcodec_open2(dec_ctx, decoder, nullptr) < 0) goto cleanup;

        // Print input info
        double input_fps = av_q2d(in_stream->avg_frame_rate);
        qDebug() << "Input video info - FPS:" << input_fps << "Duration:" << ifmt_ctx->duration;

        // Estimate total frame count for progress reporting
        int estimatedTotal = 0;
        if (in_stream->nb_frames > 0) {
            estimatedTotal = (int)in_stream->nb_frames;
        } else if (ifmt_ctx->duration > 0 && input_fps > 0) {
            estimatedTotal = (int)(ifmt_ctx->duration / (double)AV_TIME_BASE * input_fps);
        }
        int frameCounter = 0;

        // Detect display-matrix rotation so portrait videos stay upright.
        // Try multiple metadata locations — different containers / muxers
        // store rotation in different places.
        int rotation = 0;
        {
            bool found = false;

            // Method 1: codecpar->coded_side_data (FFmpeg 6.1+ / 7.x API)
            {
                const AVPacketSideData* sd = av_packet_side_data_get(
                    in_stream->codecpar->coded_side_data,
                    in_stream->codecpar->nb_coded_side_data,
                    AV_PKT_DATA_DISPLAYMATRIX);
                if (sd && sd->size >= (size_t)(9 * sizeof(int32_t))) {
                    double deg = av_display_rotation_get((const int32_t*)sd->data);
                    if (deg == deg) {  // NaN check
                        int angle = (int)round(deg);
                        angle = ((angle % 360) + 360) % 360;
                        if (angle == 90 || angle == 180 || angle == 270) {
                            rotation = angle;
                            found = true;
                        }
                        qDebug() << "Method 1 (coded_side_data) rotation:" << angle << "deg (raw:" << deg << ")";
                    }
                }
            }

            // Method 2: stream metadata "rotate" key (common in MP4/MOV from phones)
            if (!found) {
                const AVDictionaryEntry* rotEntry = av_dict_get(
                    in_stream->metadata, "rotate", nullptr, 0);
                if (rotEntry) {
                    int angle = atoi(rotEntry->value);
                    angle = ((angle % 360) + 360) % 360;
                    if (angle == 90 || angle == 180 || angle == 270) {
                        rotation = angle;
                        found = true;
                    }
                    qDebug() << "Method 2 (stream metadata 'rotate') rotation:" << angle << "deg";
                }
            }

            // Method 3: container-level metadata "rotate" key
            if (!found && ifmt_ctx->metadata) {
                const AVDictionaryEntry* rotEntry = av_dict_get(
                    ifmt_ctx->metadata, "rotate", nullptr, 0);
                if (rotEntry) {
                    int angle = atoi(rotEntry->value);
                    angle = ((angle % 360) + 360) % 360;
                    if (angle == 90 || angle == 180 || angle == 270) {
                        rotation = angle;
                        found = true;
                    }
                    qDebug() << "Method 3 (container metadata 'rotate') rotation:" << angle << "deg";
                }
            }

            if (rotation != 0)
                qDebug() << "Final rotation to apply:" << rotation << "deg";
            else
                qDebug() << "No rotation metadata found — assuming upright";
        }

        // 3. Setup Output Context
        avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, outputFile.toUtf8().constData());
        if (!ofmt_ctx) goto cleanup;

        // 4. Setup H.264 Encoder — optional HW path with libx264 fallback.
        //
        // When useHwEncode is set, try vendor-specific HW encoders first
        // (nvenc / amf / qsv / h264_mf).  HW encoders can fail at either
        // avcodec_find_encoder_by_name (not compiled in) or avcodec_open2
        // (no compatible GPU / drivers).  Either failure silently falls
        // back to libx264 so imports always succeed.
        const AVCodec *encoder = nullptr;
        if (useHwEncode) {
            QString hwName;
            encoder = hwenc::findBestH264Encoder(&hwName);
            if (encoder) {
                qInfo() << "[DecodePipeline] Trying HW encoder:" << hwName;
                usingHw = true;
            }
        }
        if (!encoder) {
            encoder = avcodec_find_encoder(AV_CODEC_ID_H264);   // libx264
            usingHw = false;
        }
        enc_ctx = avcodec_alloc_context3(encoder);

        // Copy basic properties; swap width/height for 90°/270° rotated sources.
        if (rotation == 90 || rotation == 270) {
            enc_ctx->width  = dec_ctx->height;
            enc_ctx->height = dec_ctx->width;
            qDebug() << "Swapping encoder dims for rotation:" << enc_ctx->width << "x" << enc_ctx->height;
        } else {
            enc_ctx->height = dec_ctx->height;
            enc_ctx->width  = dec_ctx->width;
        }
        enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
        // HW encoders accept NV12 universally; libx264 wants YUV420P.
        enc_ctx->pix_fmt = usingHw ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
        
        // SET FRAMERATE AND TIMEBASE CORRECTLY - KEY FIX
        enc_ctx->framerate = in_stream->avg_frame_rate;
        enc_ctx->time_base = in_stream->time_base;
        
        // If we don't have valid framerate, use common defaults
        if (enc_ctx->framerate.num <= 0 || enc_ctx->framerate.den <= 0) {
            enc_ctx->framerate = AVRational{24, 1};
            enc_ctx->time_base = AVRational{1, 24};
        }
        
        qDebug() << "Using framerate:" << enc_ctx->framerate.num << "/" << enc_ctx->framerate.den;
        qDebug() << "Using time_base:" << enc_ctx->time_base.num << "/" << enc_ctx->time_base.den;

        // Set quality parameters
        enc_ctx->bit_rate = 6000000; // 6 Mbps

        // Single-GOP encoding: one IDR at frame 0, no automatic keyframes thereafter.
        // This ensures every frame in the imported video is freely transformable —
        // no GOP boundaries will block Force-P / Force-B operations.
        enc_ctx->gop_size    = 9999;
        enc_ctx->max_b_frames = 3;

        // x264-specific params — only meaningful on the libx264 path.
        // HW encoders ignore priv_data "x264-params" and have their own
        // defaults that are fine for import standardisation.
        if (!usingHw) {
            av_opt_set(enc_ctx->priv_data, "x264-params",
                       "keyint=9999:min-keyint=1:scenecut=0:bframes=3:b-adapt=2",
                       0);
        }

        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(enc_ctx, encoder, nullptr) < 0) {
            // HW open failed (no compatible driver / runtime) — fall back
            // to libx264 with a fresh codec context so we don't ship
            // broken files on systems without HW support.
            if (usingHw) {
                qWarning() << "[DecodePipeline] HW encoder open failed, falling back to libx264";
                avcodec_free_context(&enc_ctx);
                usingHw = false;
                encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
                enc_ctx = avcodec_alloc_context3(encoder);
                if (rotation == 90 || rotation == 270) {
                    enc_ctx->width  = dec_ctx->height;
                    enc_ctx->height = dec_ctx->width;
                } else {
                    enc_ctx->width  = dec_ctx->width;
                    enc_ctx->height = dec_ctx->height;
                }
                enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
                enc_ctx->pix_fmt     = AV_PIX_FMT_YUV420P;
                enc_ctx->framerate   = in_stream->avg_frame_rate;
                enc_ctx->time_base   = in_stream->time_base;
                if (enc_ctx->framerate.num <= 0 || enc_ctx->framerate.den <= 0) {
                    enc_ctx->framerate = AVRational{24, 1};
                    enc_ctx->time_base = AVRational{1, 24};
                }
                enc_ctx->bit_rate    = 6000000;
                enc_ctx->gop_size    = 9999;
                enc_ctx->max_b_frames = 3;
                av_opt_set(enc_ctx->priv_data, "x264-params",
                           "keyint=9999:min-keyint=1:scenecut=0:bframes=3:b-adapt=2",
                           0);
                if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
                    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                if (avcodec_open2(enc_ctx, encoder, nullptr) < 0) {
                    qDebug() << "Could not open libx264 fallback";
                    goto cleanup;
                }
            } else {
                qDebug() << "Could not open encoder";
                goto cleanup;
            }
        }

        // If HW encode survived open(), prepare the YUV420P → NV12 sws
        // context + a reusable target frame.  Size matches enc_ctx (post-
        // -rotation) since the rotation path already produces YUV420P at
        // the encoder's dimensions.
        if (usingHw) {
            sws_to_nv12 = sws_getContext(
                enc_ctx->width, enc_ctx->height, AV_PIX_FMT_YUV420P,
                enc_ctx->width, enc_ctx->height, AV_PIX_FMT_NV12,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws_to_nv12) {
                qDebug() << "Could not allocate NV12 converter";
                goto cleanup;
            }
            nv12_frame = av_frame_alloc();
            nv12_frame->format = AV_PIX_FMT_NV12;
            nv12_frame->width  = enc_ctx->width;
            nv12_frame->height = enc_ctx->height;
            if (av_frame_get_buffer(nv12_frame, 32) < 0) {
                qDebug() << "Could not allocate NV12 frame buffer";
                goto cleanup;
            }
        }

        // 5. Create Output Stream
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
        out_stream->time_base = enc_ctx->time_base;
        out_stream->avg_frame_rate = enc_ctx->framerate;

        // 6. Open Output File & Write Header
        if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&ofmt_ctx->pb, outputFile.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) goto cleanup;
        }
        if (avformat_write_header(ofmt_ctx, nullptr) < 0) goto cleanup;

        // 7. Process frames
        while (av_read_frame(ifmt_ctx, pkt) >= 0) {
            if (pkt->stream_index == video_stream_idx) {
                // Send packet to decoder
                if (avcodec_send_packet(dec_ctx, pkt) == 0) {
                    // Receive decoded frames
                    while (avcodec_receive_frame(dec_ctx, frame) == 0) {
                        // If the source has a 90° or 270° display-matrix rotation,
                        // physically transpose the YUV420P frame planes so the
                        // encoded output has the correct portrait orientation.
                        AVFrame* sendFrame = frame;
                        AVFrame* rotated   = nullptr;
                        if (rotation == 90 || rotation == 180 || rotation == 270) {
                            rotated = av_frame_alloc();
                            rotated->format = AV_PIX_FMT_YUV420P;
                            rotated->width  = enc_ctx->width;
                            rotated->height = enc_ctx->height;
                            av_frame_get_buffer(rotated, 0);
                            av_frame_copy_props(rotated, frame);
                            int srcW = frame->width, srcH = frame->height;
                            if (rotation == 90) {
                                // 90° CW: src(row,col) → dst(col, srcH-1-row)
                                for (int r = 0; r < srcH; r++)
                                    for (int c = 0; c < srcW; c++)
                                        rotated->data[0][c * rotated->linesize[0] + (srcH - 1 - r)] =
                                            frame->data[0][r * frame->linesize[0] + c];
                                for (int r = 0; r < srcH / 2; r++)
                                    for (int c = 0; c < srcW / 2; c++) {
                                        rotated->data[1][c * rotated->linesize[1] + (srcH/2 - 1 - r)] =
                                            frame->data[1][r * frame->linesize[1] + c];
                                        rotated->data[2][c * rotated->linesize[2] + (srcH/2 - 1 - r)] =
                                            frame->data[2][r * frame->linesize[2] + c];
                                    }
                            } else if (rotation == 180) {
                                // 180°: src(row,col) → dst(srcH-1-row, srcW-1-col)
                                for (int r = 0; r < srcH; r++)
                                    for (int c = 0; c < srcW; c++)
                                        rotated->data[0][(srcH - 1 - r) * rotated->linesize[0] + (srcW - 1 - c)] =
                                            frame->data[0][r * frame->linesize[0] + c];
                                for (int r = 0; r < srcH / 2; r++)
                                    for (int c = 0; c < srcW / 2; c++) {
                                        rotated->data[1][(srcH/2 - 1 - r) * rotated->linesize[1] + (srcW/2 - 1 - c)] =
                                            frame->data[1][r * frame->linesize[1] + c];
                                        rotated->data[2][(srcH/2 - 1 - r) * rotated->linesize[2] + (srcW/2 - 1 - c)] =
                                            frame->data[2][r * frame->linesize[2] + c];
                                    }
                            } else { // 270° CW = 90° CCW
                                // 270° CW: src(row,col) → dst(srcW-1-col, row)
                                for (int r = 0; r < srcH; r++)
                                    for (int c = 0; c < srcW; c++)
                                        rotated->data[0][(srcW - 1 - c) * rotated->linesize[0] + r] =
                                            frame->data[0][r * frame->linesize[0] + c];
                                for (int r = 0; r < srcH / 2; r++)
                                    for (int c = 0; c < srcW / 2; c++) {
                                        rotated->data[1][(srcW/2 - 1 - c) * rotated->linesize[1] + r] =
                                            frame->data[1][r * frame->linesize[1] + c];
                                        rotated->data[2][(srcW/2 - 1 - c) * rotated->linesize[2] + r] =
                                            frame->data[2][r * frame->linesize[2] + c];
                                    }
                            }
                            sendFrame = rotated;
                        }

                        // For HW encode, convert the YUV420P sendFrame to
                        // NV12 before handing to the encoder.
                        AVFrame* encodeInput = sendFrame;
                        if (usingHw && sws_to_nv12 && nv12_frame) {
                            av_frame_make_writable(nv12_frame);
                            sws_scale(sws_to_nv12,
                                      sendFrame->data, sendFrame->linesize,
                                      0, sendFrame->height,
                                      nv12_frame->data, nv12_frame->linesize);
                            av_frame_copy_props(nv12_frame, sendFrame);
                            nv12_frame->pts = sendFrame->pts;
                            encodeInput = nv12_frame;
                        }

                        // Send frame to encoder with preserved timing
                        if (avcodec_send_frame(enc_ctx, encodeInput) == 0) {
                            AVPacket *enc_pkt = av_packet_alloc();
                            // Receive encoded packets
                            while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
                                // Preserve timing from input
                                enc_pkt->stream_index = out_stream->index;
                                av_interleaved_write_frame(ofmt_ctx, enc_pkt);
                                av_packet_unref(enc_pkt);
                            }
                            av_packet_free(&enc_pkt);
                        }
                        if (rotated) av_frame_free(&rotated);
                        frameCounter++;
                        if (progress && estimatedTotal > 0)
                            progress(frameCounter, estimatedTotal);
                    }
                }
            }
            av_packet_unref(pkt);
        }

        // 8. Flush encoder
        avcodec_send_frame(enc_ctx, nullptr);
        AVPacket *flush_pkt = av_packet_alloc();
        while (avcodec_receive_packet(enc_ctx, flush_pkt) == 0) {
            flush_pkt->stream_index = out_stream->index;
            av_interleaved_write_frame(ofmt_ctx, flush_pkt);
            av_packet_unref(flush_pkt);
        }
        av_packet_free(&flush_pkt);

        av_write_trailer(ofmt_ctx);
        success = true;
        qDebug() << "Encoding completed successfully";
    }

cleanup:
    av_packet_free(&pkt);
    av_frame_free(&frame);
    if (nv12_frame)  av_frame_free(&nv12_frame);
    if (sws_to_nv12) sws_freeContext(sws_to_nv12);
    if (dec_ctx) avcodec_free_context(&dec_ctx);
    if (enc_ctx) avcodec_free_context(&enc_ctx);
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&ofmt_ctx->pb);
    if (ofmt_ctx) avformat_free_context(ofmt_ctx);
    if (ifmt_ctx) avformat_close_input(&ifmt_ctx);

    return success;
}
