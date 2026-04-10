#pragma once
#include <QString>

class DecodePipeline {
public:
    // Takes any input video and creates a deterministic, silent H.264 MP4
    static bool standardizeVideo(const QString& inputFile, const QString& outputFile);
};