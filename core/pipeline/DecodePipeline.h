#pragma once
#include <QString>
#include <functional>

class DecodePipeline {
public:
    // Takes any input video and creates a deterministic, silent H.264 MP4.
    // Optional progress callback receives (currentFrame, estimatedTotalFrames).
    using ProgressCallback = std::function<void(int, int)>;
    static bool standardizeVideo(const QString& inputFile, const QString& outputFile,
                                 ProgressCallback progress = nullptr);
};