#pragma once

#include <QObject>
#include <QVector>
#include <QString>

#include "core/model/MBEditData.h"
#include "core/model/GlobalEncodeParams.h"

// =============================================================================
// FrameTransformerWorker — runs on a QThread.
// Decodes the source MP4, forces the selected frames to the target H.264 slice
// type by setting AVFrame::pict_type before re-encoding, then atomically
// replaces the original file.
//
// Additionally, per-macroblock edits supplied via mbEdits are applied on top
// of the frame-type transforms:
//   • qpDelta  → AVRegionOfInterest side data (per-MB QP offset)
//   • mvDrift  → pixel substitution from a ring-buffered reference frame
//                so the encoder assigns the desired motion vectors
//   • refDepth → which buffered reference frame to source pixels from
//
// DeleteFrames is handled separately via runBitstreamSplice(), which copies
// compressed packets directly without decode/re-encode.  This preserves the
// original H.264 motion vectors so they reference content from the wrong shot
// after the cut — the classic datamosh smear effect.
// =============================================================================
class FrameTransformerWorker : public QObject {
    Q_OBJECT
public:
    enum TargetType { ForceI = 0, ForceP = 1, ForceB = 2,
                      DeleteFrames = 3, DuplicateLeft = 4, DuplicateRight = 5,
                      MBEditOnly = 6,   // re-encode preserving frame types, apply MB edits only
                      InterpolateLeft  = 7,  // insert blended frame between left-neighbour and earliest selected
                      InterpolateRight = 8,  // insert blended frame between rightmost selected and right-neighbour
                      ReorderFrames    = 9,  // drag-reorder: frameIndices[0]=src, frameIndices[1]=insertBefore
                      FlipVertical     = 10, // mirror selected frames upside-down (Y axis)
                      FlipHorizontal   = 11  // mirror selected frames left-right (X axis)
                    };

    // origFrameTypes: display-order frame type chars from the last analysis
    // ('I','P','B').  Used by the splice path to detect B-frame content, and
    // by Force I/P/B re-encode to restore frame types on unselected frames.
    //
    // mbEdits: per-frame macroblock edit parameters from MacroblockWidget.
    FrameTransformerWorker(const QString& videoPath,
                           const QVector<int>& frameIndices,
                           TargetType targetType,
                           int totalFrameCount,
                           const QVector<char>& origFrameTypes  = QVector<char>(),
                           const MBEditMap&     mbEdits          = MBEditMap(),
                           const GlobalEncodeParams& globalParams = GlobalEncodeParams(),
                           int  interpolateCount = 1,
                           QObject* parent = nullptr);

public slots:
    void run();

signals:
    void progress(int current, int total);
    void warning(QString message);
    void done(bool success, QString errorMessage);

private:
    // Bitstream splice for DeleteFrames: copies compressed packets without
    // decode/re-encode so original motion vectors are preserved (datamosh).
    void runBitstreamSplice();

    // Full decode → re-encode with frames emitted in a user-specified new order.
    // frameIndices must be [sourceIdx, insertBeforeIdx].
    void runReorderFrames();

    // LaMoshPit-Edge: parallel render path for true bitstream-level MB edits.
    // Bypasses FFmpeg's libx264 wrapper and calls our forked libx264 (x264-
    // lamoshpit) directly so per-MB overrides in pic.prop (cbp_override,
    // mb_skip_override, mb_type_override, intra_mode_override, mvd_x/y/active_
    // override, dct_scale_override) reach the encoder.  Decodes via libavcodec,
    // encodes via libx264 C API, muxes via libavformat.  Invoked from run()
    // when m_targetType == MBEditOnly AND any frame has a bitstream knob set
    // (bsCbpZero, bsForceSkip, bsMbType, bsIntraMode, bsMvdX|Y, bsDctScale).
    // Returns via the same done(bool, QString) signal as the pixel path.
    void runBitstreamEdit();

    QString            m_videoPath;
    QVector<int>       m_frameIndices;
    TargetType         m_targetType;
    int                m_totalFrames;
    QVector<char>      m_origFrameTypes;
    MBEditMap          m_mbEdits;
    GlobalEncodeParams m_globalParams;
    int                m_interpolateCount = 1;  // only used by InterpolateLeft/Right
};
