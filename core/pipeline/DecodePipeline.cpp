#include "DecodePipeline.h"
#include <QDebug>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/rational.h>
}

bool DecodePipeline::standardizeVideo(const QString& inputFile, const QString& outputFile)
{
    AVFormatContext *ifmt_ctx = nullptr, *ofmt_ctx = nullptr;
    AVCodecContext *dec_ctx = nullptr, *enc_ctx = nullptr;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
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

        // 3. Setup Output Context
        avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, outputFile.toUtf8().constData());
        if (!ofmt_ctx) goto cleanup;

        // 4. Setup H.264 Encoder - SIMPLIFIED
        const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        enc_ctx = avcodec_alloc_context3(encoder);
        
        // Copy basic properties
        enc_ctx->height = dec_ctx->height;
        enc_ctx->width = dec_ctx->width;
        enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
        enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        
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

        // x264-params: keyint=9999 prevents automatic IDR insertions,
        // scenecut=0 disables scene-cut IDR detection,
        // bframes=3 + b-adapt=2 for rich B-frame prediction structure,
        // min-keyint=1 still allows user-forced I-frames anywhere.
        av_opt_set(enc_ctx->priv_data, "x264-params",
                   "keyint=9999:min-keyint=1:scenecut=0:bframes=3:b-adapt=2",
                   0);

        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(enc_ctx, encoder, nullptr) < 0) {
            qDebug() << "Could not open encoder";
            goto cleanup;
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
                        // Send frame to encoder with preserved timing
                        if (avcodec_send_frame(enc_ctx, frame) == 0) {
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
    if (dec_ctx) avcodec_free_context(&dec_ctx);
    if (enc_ctx) avcodec_free_context(&enc_ctx);
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&ofmt_ctx->pb);
    if (ofmt_ctx) avformat_free_context(ofmt_ctx);
    if (ifmt_ctx) avformat_close_input(&ifmt_ctx);

    return success;
}
