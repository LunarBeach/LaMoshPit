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
// =============================================================================
class FrameTransformerWorker : public QObject {
    Q_OBJECT
public:
    enum TargetType { ForceI = 0, ForceP = 1, ForceB = 2,
                      DeleteFrames = 3, DuplicateLeft = 4, DuplicateRight = 5,
                      MBEditOnly = 6   // re-encode preserving frame types, apply MB edits only
                    };

    // origFrameTypes: display-order frame type chars from the last analysis
    // ('I','P','B'). When provided, Delete operations enforce the original type
    // on every remaining frame so the encoder recalculates predictions with
    // broken references (datamosh effect) rather than freely re-typing frames.
    //
    // mbEdits: per-frame macroblock edit parameters from MacroblockWidget.
    FrameTransformerWorker(const QString& videoPath,
                           const QVector<int>& frameIndices,
                           TargetType targetType,
                           int totalFrameCount,
                           const QVector<char>& origFrameTypes  = QVector<char>(),
                           const MBEditMap&     mbEdits          = MBEditMap(),
                           const GlobalEncodeParams& globalParams = GlobalEncodeParams(),
                           QObject* parent = nullptr);

public slots:
    void run();

signals:
    void progress(int current, int total);
    void warning(QString message);
    void done(bool success, QString errorMessage);

private:
    QString            m_videoPath;
    QVector<int>       m_frameIndices;
    TargetType         m_targetType;
    int                m_totalFrames;
    QVector<char>      m_origFrameTypes;
    MBEditMap          m_mbEdits;
    GlobalEncodeParams m_globalParams;
};
