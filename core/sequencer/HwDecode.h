#pragma once

// =============================================================================
// HwDecode — helpers for setting up D3D11VA hardware-accelerated decoding.
//
// Windows 11 ships D3D11VA support in the driver for every GPU LaMoshPit is
// likely to run on (Intel iGPU / AMD / NVIDIA).  H.264 decode via D3D11VA
// is essentially free in CPU time, which matters for Phase 3 multi-track
// parallel decode (up to 9 tracks at once).
//
// The public surface is two free functions:
//
//   tryAttachHwDeviceContext(codecCtx)
//       Creates (or reuses a cached) D3D11VA hardware device context and
//       attaches it to codecCtx->hw_device_ctx.  Also installs a get_format
//       callback so the decoder selects the D3D11 pixel format.  Returns
//       true on success.  On failure the codec context is left pristine
//       and the caller should proceed with CPU decoding.
//
//   transferHwFrameToSw(hwFrame, swFrame)
//       Copies a decoded AV_PIX_FMT_D3D11 frame into a CPU-side frame
//       (typically AV_PIX_FMT_NV12) via av_hwframe_transfer_data.  No-op
//       if the frame is already a software frame.  Returns true on success.
//
// Phase-1 note: decoded frames ultimately land on the CPU via transfer +
// swscale → BGRA for display in QWidget paintEvent.  The true zero-copy
// GPU-to-display path lands in Phase 4 alongside Spout, which needs the
// frame to stay on a D3D11 texture anyway.
// =============================================================================

struct AVCodecContext;
struct AVFrame;

namespace sequencer {

// Attach a D3D11VA hw_device_ctx to the codec context and install the
// get_format callback that selects AV_PIX_FMT_D3D11.  Call BEFORE
// avcodec_open2.  Returns true on success; false if HW init failed (caller
// should fall back to software decoding).
bool tryAttachHwDeviceContext(AVCodecContext* codecCtx);

// Copy a HW frame (AV_PIX_FMT_D3D11) into a SW frame (typically NV12).
// If the input is already SW, the function is a no-op that returns true
// after an av_frame_ref — so callers can always call this unconditionally.
//
// swFrame must be allocated by the caller; its fields will be populated.
// Returns true on success.
bool transferHwFrameToSw(AVFrame* hwFrame, AVFrame* swFrame);

} // namespace sequencer
