#pragma once

#include <QString>

// =============================================================================
// VideoMetaProbe
//
// Lightweight video-metadata probe.  Opens the container and reads stream
// headers only — no full decode pass — so it's orders of magnitude cheaper
// than BitstreamAnalyzer::analyzeVideo().  Used to validate compatibility
// between an imported selection-map video and candidate target clips.
//
// Frame count is computed by iterating through compressed packets in the
// video stream; this reads packet headers only (no decode), which is fast
// even for long files and gives an accurate count for H.264.
// =============================================================================
struct VideoMeta {
    bool    ok          = false;
    int     width       = 0;
    int     height      = 0;
    double  fps         = 0.0;   // frames per second (display rate)
    int     totalFrames = 0;
    QString errorMsg;
};

class VideoMetaProbe {
public:
    static VideoMeta probe(const QString& videoPath);

    // True when two metas match on width, height, frame count, and fps
    // within the given tolerance.
    static bool compatible(const VideoMeta& a, const VideoMeta& b,
                           double fpsTolerance = 0.01);
};
