#pragma once

#include <QString>
#include <QSet>
#include <QMap>
#include <QVector>

// =============================================================================
// MapFrameSampler
//
// Decodes a black-and-white "selection map" video and produces a per-MB
// selection set for each requested frame.
//
// Rule: each 16×16 macroblock is SELECTED iff the mean luma of the block's
// pixels in the corresponding map frame is strictly greater than 127.
//
// Decode is done in a single sequential pass over the map video (no per-
// frame re-scan), which keeps the cost O(N_decoded_frames) rather than
// O(frames_in_video × frames_requested).
// =============================================================================
class MapFrameSampler {
public:
    // Decode the selected frames of the map video at native resolution and
    // return a map of {frameIndex → QSet<int> selectedMBs} for each frame
    // in `targetFrames`.  Frame indices are 0-based, presentation-order.
    //
    // mbCols/mbRows describe the MB grid of the *target* clip — since map
    // and target clip are metadata-compatible (same width/height/fps/count),
    // they also have the same MB grid.
    //
    // Frames not present in the video are silently skipped.  Returns empty
    // map on fatal error (errorMsg populated).
    static QMap<int, QSet<int>> sampleFrames(
        const QString& mapVideoPath,
        const QVector<int>& targetFrames,
        int mbCols,
        int mbRows,
        QString& errorMsg);
};
