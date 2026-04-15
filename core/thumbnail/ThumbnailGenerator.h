#pragma once

// =============================================================================
// ThumbnailGenerator — single-frame PNG preview extraction for the Media Bin.
//
// One static entry point: generateMidFrameThumbnail(videoPath, outPngPath,
// w, h).  Opens the video via libavformat, decodes until roughly half-way
// through, scales the decoded frame to w×h, and writes it as a PNG.
//
// Called from MainWindow on import completion (for root imports) and on
// render completion (for vNN iterations).  Runs synchronously on the caller's
// thread — thumbnails for short clips take a few hundred ms, which is fine
// for a post-render call at the end of a multi-second transform.  If this
// ever feels sluggish the obvious upgrade is QtConcurrent::run with a
// completion signal; not worth the complexity yet.
// =============================================================================

#include <QString>

class ThumbnailGenerator {
public:
    // Decode a frame near the middle of the video and save it as a PNG of
    // the requested size.  Returns true on success.  On failure, errorMsg
    // is populated and no file is written.  If outPngPath already exists
    // it is overwritten.
    //
    // Default 160×90 matches the Media Bin icon size at a comfortable scale
    // that reads well without eating row height.
    static bool generateMidFrameThumbnail(const QString& videoPath,
                                          const QString& outPngPath,
                                          QString& errorMsg,
                                          int outWidth  = 160,
                                          int outHeight = 90);
};
