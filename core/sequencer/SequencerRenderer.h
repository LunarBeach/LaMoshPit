#pragma once

// =============================================================================
// SequencerRenderer — bakes one SequencerProject track to a clean H.264 MP4.
//
// Runs on a worker QThread (MainWindow spawns + owns the thread).  Emits
// progress() every few frames and done() once at the end.  Never touches
// Qt GUI objects directly.
//
// Decode path: reuses SequencerClipDecoder, which handles D3D11VA HW
// decode with SW fallback.  Each clip in the chosen track is walked in
// sequence.  The decoded BGRA frame is swscale-converted to YUV420P (or
// NV12 for HW encoders), stamped with an output-timebase PTS, and handed
// to the encoder.
//
// Encoder:
//   - Libx264Default      → libx264 with CRF 18 + sensible defaults.
//   - Libx264FromGlobal   → libx264 honouring GlobalEncodeParams (gopSize,
//                            bFrames, refFrames, qpOverride, qpMin/Max,
//                            killIFrames, and a selection of common flags).
//   - Hardware            → h264_nvenc → h264_amf → h264_qsv (first
//                            available).  Falls back to libx264 if none
//                            are usable on the current system.
//
// Output: one MP4 file at the user-chosen path with video-only stream at
// project.outputFrameRate().
// =============================================================================

#include "core/sequencer/Tick.h"
#include "core/model/GlobalEncodeParams.h"
#include <QObject>
#include <QString>

namespace sequencer {

class SequencerProject;

class SequencerRenderer : public QObject {
    Q_OBJECT
public:
    enum class EncoderMode {
        Libx264Default,
        Libx264FromGlobal,
        Hardware,
    };

    struct Params {
        QString            outputPath;
        int                sourceTrackIndex { 0 };
        Tick               rangeStartTicks  { 0 };
        Tick               rangeEndTicks    { 0 };   // 0 = use full track duration
        EncoderMode        encoderMode      { EncoderMode::Libx264Default };
        GlobalEncodeParams globalParams;              // consulted only for Libx264FromGlobal
    };

    SequencerRenderer(const SequencerProject* project, Params params,
                      QObject* parent = nullptr);
    ~SequencerRenderer() override;

public slots:
    // Invoked from the QThread's started() signal.  Does all the heavy
    // lifting on this thread.  Emits done() exactly once at completion or
    // failure.
    void run();

signals:
    // Progress update: current encoded-frame count vs. the estimated total.
    // Total can be 0 until first-frame counting finishes, so UIs should
    // guard against that.
    void progress(int currentFrames, int totalFrames);

    // Completion signal — always fires exactly once, on this (worker)
    // thread.  success=false indicates render failure; errorMsg is
    // populated.  outputPath is the file actually written (on success).
    void done(bool success, QString errorMsg, QString outputPath);

private:
    const SequencerProject* m_project { nullptr };
    Params                  m_params;
};

} // namespace sequencer
