#pragma once

// =============================================================================
// ControlLogger — Debug logging for LaMoshPit control-knob verification.
//
// Toggle via the "Enable Control Debug Logging" checkbox in the Global Encode
// Parameters panel.  When disabled (the default), every log call returns
// immediately — zero branches taken, zero allocations, zero I/O.
//
// When enabled, output goes to TWO destinations simultaneously:
//   • qDebug()  →  VS Code Debug Console / Output panel (live, while running)
//   • logs/LaMoshPit_ControlTest_Log.txt  (appended each run; survives restarts)
//
// Thread-safety: FrameTransformer calls logFrameEditApplied() from a worker
// thread.  All public methods acquire m_mutex before touching shared state.
//
// To remove the entire system: delete this file and ControlLogger.cpp, remove
// the #include from the three callers, and remove the source from CMakeLists.
// =============================================================================

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>

#include "core/model/MBEditData.h"

class ControlLogger {
public:
    // Process-wide singleton.
    static ControlLogger& instance();

    // Toggle logging on/off.  Connected directly to the UI checkbox.
    // Enabling lazily creates the log file (first call only).
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // ── Per-control change events ─────────────────────────────────────────────
    // Called each time the user moves a dial or spinbox.
    // Skipped silently when oldVal == newVal (suppresses initialisation noise).
    void logKnobChange(const QString& knobName, int frameIdx,
                       int oldVal, int newVal);

    // Called when the painted MB selection on the canvas changes.
    void logMBSelectionChange(int frameIdx, int oldCount, int newCount);

    // ── Apply pipeline events ─────────────────────────────────────────────────
    // Called at the top of applyMBEdits() — dumps the full FrameMBParams for
    // the frame that is about to be rendered.  Only non-default fields printed.
    void logFrameEditApplied(int frameIdx, const FrameMBParams& p,
                              int mbCols, int mbRows);

    // Called once at the start of FrameTransformerWorker::run().
    void logApplyStarted(int totalFrames, int editedFrameCount);

    // Called immediately after logApplyStarted() by whichever runXxx() method
    // actually services this render.  The string ends up in the log as
    // "APPLY VIA <path>" — critical for distinguishing pixel-domain (FFmpeg
    // wrapped libx264) from bitstream-surgery (direct libx264 via the
    // LaMoshPit-Edge fork with override hooks) runs.
    void logRenderPath(const QString& pathName);

    // Called just before emit done() in FrameTransformerWorker::run().
    void logApplyCompleted(bool success);

    // Free-form diagnostic line — writes a single timestamped entry to both
    // the console and the log file.  Used to trace code paths when we need
    // finer-grained than "APPLY STARTED / COMPLETED" but don't want to invent
    // a new log category for each breadcrumb.
    void logNote(const QString& message);

    // ── Session lifecycle ─────────────────────────────────────────────────────
    // Called when a new video is loaded.  Writes a header block to the log.
    void beginSession(const QString& videoPath);

private:
    ControlLogger();
    ~ControlLogger();

    // write() does the actual output — caller MUST hold m_mutex.
    void write(const QString& line);

    QString timestamp() const;

    bool        m_enabled = false;
    QFile       m_logFile;
    QTextStream m_stream;
    QMutex      m_mutex;
};
