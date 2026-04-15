#include "ControlLogger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDebug>

// =============================================================================
// Singleton
// =============================================================================

ControlLogger& ControlLogger::instance()
{
    static ControlLogger s;
    return s;
}

ControlLogger::ControlLogger() = default;

ControlLogger::~ControlLogger()
{
    if (m_logFile.isOpen()) {
        m_stream << "[" << timestamp() << "] === SESSION ENDED ===\n\n";
        m_stream.flush();
        m_logFile.close();
    }
}

// =============================================================================
// Enable / disable
// =============================================================================

void ControlLogger::setEnabled(bool enabled)
{
    QMutexLocker lock(&m_mutex);

    if (enabled == m_enabled) return;
    m_enabled = enabled;

    if (enabled) {
        if (!m_logFile.isOpen()) {
            // Create the logs/ directory next to the executable and open the file.
            QString dir = QCoreApplication::applicationDirPath() + "/logs";
            QDir().mkpath(dir);
            m_logFile.setFileName(dir + "/LaMoshPit_ControlTest_Log.txt");
            m_logFile.open(QIODevice::Append | QIODevice::Text);
            if (m_logFile.isOpen())
                m_stream.setDevice(&m_logFile);
        }
        write("\n" + QString(72, '='));
        write("[" + timestamp() + "] CONTROL DEBUG LOGGING ENABLED");
        write(QString(72, '='));
    } else {
        write("[" + timestamp() + "] Logging disabled by user.");
    }
}

bool ControlLogger::isEnabled() const
{
    return m_enabled;
}

// =============================================================================
// Internal write helper — caller must hold m_mutex
// =============================================================================

void ControlLogger::write(const QString& line)
{
    qDebug().noquote() << line;
    if (m_logFile.isOpen()) {
        m_stream << line << "\n";
        m_stream.flush();
    }
}

QString ControlLogger::timestamp() const
{
    return QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
}

// =============================================================================
// Session lifecycle
// =============================================================================

void ControlLogger::beginSession(const QString& videoPath)
{
    QMutexLocker lock(&m_mutex);
    if (!m_enabled) return;
    write("");
    write("[" + timestamp() + "] -- VIDEO LOADED " + QString(54, '-'));
    write("    " + videoPath);
}

// =============================================================================
// Per-control change events
// =============================================================================

void ControlLogger::logKnobChange(const QString& knobName, int frameIdx,
                                   int oldVal, int newVal)
{
    if (!m_enabled) return;
    if (oldVal == newVal) return;   // no-op changes (e.g. initialisation loads)

    QMutexLocker lock(&m_mutex);
    write(QString("[%1] KNOB  frame=%2  %3 %4 -> %5  [written to edit map]")
          .arg(timestamp())
          .arg(frameIdx, 3)
          .arg(knobName.leftJustified(20))
          .arg(oldVal, 5)
          .arg(newVal, 5));
}

// =============================================================================

void ControlLogger::logMBSelectionChange(int frameIdx, int oldCount, int newCount)
{
    if (!m_enabled) return;
    if (oldCount == newCount) return;

    QMutexLocker lock(&m_mutex);
    write(QString("[%1] PAINT frame=%2  MB selection: %3 -> %4 MBs selected")
          .arg(timestamp())
          .arg(frameIdx, 3)
          .arg(oldCount, 4)
          .arg(newCount, 4));
}

// =============================================================================
// Apply pipeline events
// =============================================================================

void ControlLogger::logFrameEditApplied(int frameIdx, const FrameMBParams& p,
                                         int mbCols, int mbRows)
{
    if (!m_enabled) return;
    QMutexLocker lock(&m_mutex);

    write(QString("[%1] APPLY frame=%2  mbGrid=%3x%4  selectedMBs=%5")
          .arg(timestamp())
          .arg(frameIdx, 3)
          .arg(mbCols).arg(mbRows)
          .arg(p.selectedMBs.size()));

    // Print only the non-default / active parameters so the log stays readable.
    // Default int fields are 0; mvAmplify default is 1.
    auto chk = [&](const QString& name, int val, int dflt = 0) {
        if (val != dflt)
            write(QString("         |  %1 = %2").arg(name.leftJustified(20)).arg(val));
    };

    chk("qpDelta",      p.qpDelta);
    chk("refDepth",     p.refDepth);
    chk("ghostBlend",   p.ghostBlend);
    chk("mvDriftX",     p.mvDriftX);
    chk("mvDriftY",     p.mvDriftY);
    chk("mvAmplify",    p.mvAmplify, 1);  // skip when at default of 1
    chk("noiseLevel",   p.noiseLevel);
    chk("pixelOffset",  p.pixelOffset);
    chk("invertLuma",   p.invertLuma);
    chk("chromaDriftX", p.chromaDriftX);
    chk("chromaDriftY", p.chromaDriftY);
    chk("chromaOffset", p.chromaOffset);
    chk("spillRadius",  p.spillRadius);
    chk("sampleRadius", p.sampleRadius);
    chk("cascadeLen",   p.cascadeLen);
    chk("cascadeDecay", p.cascadeDecay);
    chk("blockFlatten", p.blockFlatten);
    chk("refScatter",   p.refScatter);
    chk("colorTwistU",  p.colorTwistU);
    chk("colorTwistV",  p.colorTwistV);
    // Newer pixel-domain knobs (posterize default is 8 = off; others default 0).
    chk("posterize",    p.posterize,    8);
    chk("pixelShuffle", p.pixelShuffle);
    chk("sharpen",      p.sharpen);
    chk("tempDiffAmp",  p.tempDiffAmp);
    chk("hueRotate",    p.hueRotate);
    // Bitstream-surgery knobs — critical for validating the runBitstreamEdit()
    // render path.  bsDctScale default is 100 (unchanged); bsIntraMode/bsMbType
    // default −1 (off); all others default 0.
    chk("bsMvdX",             p.bsMvdX);
    chk("bsMvdY",             p.bsMvdY);
    chk("bsSuppressResOnMvd", p.bsSuppressResOnMvd, 1);
    chk("bsForceSkip",        p.bsForceSkip);
    chk("bsIntraMode",        p.bsIntraMode,  -1);
    /* bsMbType not logged — feature migrated to Partition Mode (encoder-wide). */
    chk("bsDctScale",         p.bsDctScale,   100);
    chk("bsCbpZero",          p.bsCbpZero);
    chk("bsCbpZeroLuma",      p.bsCbpZeroLuma,    -1);
    chk("bsCbpZeroChroma",    p.bsCbpZeroChroma,  -1);

    write(QString("         \\_ end frame %1").arg(frameIdx));
}

// =============================================================================

void ControlLogger::logApplyStarted(int totalFrames, int editedFrameCount)
{
    if (!m_enabled) return;
    QMutexLocker lock(&m_mutex);
    write("");
    write("[" + timestamp() + "] -- APPLY STARTED " + QString(52, '-'));
    write(QString("    totalFrames=%1  framesWithEdits=%2")
          .arg(totalFrames).arg(editedFrameCount));
}

void ControlLogger::logRenderPath(const QString& pathName)
{
    if (!m_enabled) return;
    QMutexLocker lock(&m_mutex);
    write("[" + timestamp() + "]    APPLY VIA " + pathName);
}

void ControlLogger::logNote(const QString& message)
{
    if (!m_enabled) return;
    QMutexLocker lock(&m_mutex);
    write("[" + timestamp() + "] NOTE  " + message);
}

void ControlLogger::logApplyCompleted(bool success)
{
    if (!m_enabled) return;
    QMutexLocker lock(&m_mutex);
    write("[" + timestamp() + "] -- APPLY " +
          (success ? "COMPLETED OK" : "FAILED      ") + " " + QString(48, '-'));
    write("");
}
