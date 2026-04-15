#pragma once
#include <QString>
#include <functional>

class DecodePipeline {
public:
    // Takes any input video and creates a deterministic, silent H.264 MP4.
    // Optional progress callback receives (currentFrame, estimatedTotalFrames).
    //
    // useHwEncode: if true, attempts hardware-accelerated H.264 encode via
    // nvenc / amf / qsv / h264_mf (in that order), falling back to libx264
    // if none are usable on this machine.  The HW path produces NV12 pixels;
    // the CPU path sticks with YUV420P (libx264 native).
    //
    // Controlled application-wide by the "Use hardware acceleration for new
    // file imports" checkbox in File → Settings.  Independent of the
    // NLE-render and mosh-editor paths.
    using ProgressCallback = std::function<void(int, int)>;
    static bool standardizeVideo(const QString& inputFile, const QString& outputFile,
                                 ProgressCallback progress = nullptr,
                                 bool useHwEncode = false);
};