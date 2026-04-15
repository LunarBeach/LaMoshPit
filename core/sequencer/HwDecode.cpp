#include "core/sequencer/HwDecode.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
}

#include <QDebug>

namespace sequencer {

// =============================================================================
// get_format callback — picks D3D11 if the decoder offers it.
//
// FFmpeg calls this during decoder initialisation with a list of acceptable
// pixel formats for the current stream.  Returning the HW format tells the
// decoder "please produce frames as D3D11 textures."  Returning one of the
// SW formats silently downgrades to CPU decoding — which is fine fallback
// behaviour but means our HW decode attempt failed.
// =============================================================================

static enum AVPixelFormat pickD3D11Format(AVCodecContext* /*ctx*/,
                                          const enum AVPixelFormat* fmts)
{
    for (const enum AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_D3D11) return AV_PIX_FMT_D3D11;
    }
    // HW format wasn't offered by the decoder — surface an SW format so the
    // codec opens cleanly in CPU mode.  The caller's avcodec_open2 still
    // succeeds; decoded frames just land on the CPU.
    return fmts[0];
}

// =============================================================================
// tryAttachHwDeviceContext
// =============================================================================

bool tryAttachHwDeviceContext(AVCodecContext* codecCtx)
{
    if (!codecCtx) return false;

    AVBufferRef* hwDeviceCtx = nullptr;
    const int err = av_hwdevice_ctx_create(&hwDeviceCtx,
                                           AV_HWDEVICE_TYPE_D3D11VA,
                                           /*device=*/nullptr,
                                           /*opts=*/nullptr, 0);
    if (err < 0 || !hwDeviceCtx) {
        // No D3D11 available (unusual on Win11 but possible under remote
        // desktop, no GPU driver, virtual machine without passthrough, etc.).
        char buf[128];
        av_strerror(err, buf, sizeof(buf));
        qWarning() << "[HwDecode] D3D11VA hw device create failed:" << buf;
        return false;
    }

    codecCtx->hw_device_ctx = hwDeviceCtx;   // codec takes ownership on open
    codecCtx->get_format    = pickD3D11Format;
    return true;
}

// =============================================================================
// transferHwFrameToSw
// =============================================================================

bool transferHwFrameToSw(AVFrame* hwFrame, AVFrame* swFrame)
{
    if (!hwFrame || !swFrame) return false;

    // Already a CPU-side frame — just ref into swFrame.  Caller treats
    // swFrame as the canonical output regardless of the source path.
    if (hwFrame->format != AV_PIX_FMT_D3D11) {
        av_frame_unref(swFrame);
        return av_frame_ref(swFrame, hwFrame) == 0;
    }

    av_frame_unref(swFrame);
    const int err = av_hwframe_transfer_data(swFrame, hwFrame, 0);
    if (err < 0) {
        char buf[128];
        av_strerror(err, buf, sizeof(buf));
        qWarning() << "[HwDecode] hwframe_transfer_data failed:" << buf;
        return false;
    }
    // Preserve PTS / best_effort_timestamp across the transfer — the util
    // copies buffer data but not every metadata field on older FFmpeg.
    swFrame->pts                   = hwFrame->pts;
    swFrame->pkt_dts               = hwFrame->pkt_dts;
    swFrame->best_effort_timestamp = hwFrame->best_effort_timestamp;
    return true;
}

} // namespace sequencer
