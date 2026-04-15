#pragma once

// =============================================================================
// HwEncoder — find the best-available hardware H.264 encoder at runtime.
//
// Shared between SequencerRenderer (NLE export) and DecodePipeline (import
// standardisation) so the preference order stays consistent in one place.
//
// Preference order (first match wins):
//   h264_nvenc  — NVIDIA (Turing+ best; Maxwell+ supported)
//   h264_amf    — AMD (GCN 4th gen / RDNA)
//   h264_qsv    — Intel Quick Sync (CPUs with iGPU)
//   h264_mf     — Windows Media Foundation (generic; works with any GPU
//                 that has a vendor driver, as a catch-all)
//
// Returns nullptr if none of the above is available in the linked FFmpeg
// build OR if none can be initialised on this machine.  Caller should fall
// back to libx264 in that case.
//
// NOTE: availability-at-link-time is only half the story.  An encoder that
// `avcodec_find_encoder_by_name` returns non-null can still fail inside
// `avcodec_open2` if the user lacks drivers / a compatible GPU.  Callers
// must handle that failure and fall back to software encode.
// =============================================================================

#include <QString>

struct AVCodec;

namespace hwenc {

// Returns the AVCodec*; writes the chosen name into *outName if non-null.
const AVCodec* findBestH264Encoder(QString* outName = nullptr);

} // namespace hwenc
