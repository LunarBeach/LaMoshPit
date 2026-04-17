#pragma once

// =============================================================================
// NleLauncher — spawns + supervises LaMoshPit_NLE.exe from the MSVC host.
//
// Responsibilities:
//   1. Locate LaMoshPit_NLE.exe (searches known dev paths first, then
//      the deployed install layout).
//   2. Spawn it via QProcess with `--ipc-pipe <name>` on the command line.
//   3. Wrap the child process handle in a Windows Job Object with
//      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE so a Task-Manager kill of
//      LaMoshPit.exe (or a crash) takes the NLE with it automatically.
//      This is the canonical way on Windows to prevent orphan children.
//   4. Report abnormal exits so MainWindow can offer "Relaunch NLE?".
//
// The Job Object handle lives for the lifetime of this object.  When
// LaMoshPit.exe's last handle to the job closes (normally: app exit),
// Windows kernel terminates every process in the job — including the
// NLE — with no grace period.
//
// See docs/Implementation_Plan_NLE_Rebuild_2026-04-17.md Step 3 Gotcha
// G3.6 for the full rationale.
// =============================================================================

#include <QObject>
#include <QString>

class QProcess;

namespace lamosh {

class NleLauncher : public QObject {
    Q_OBJECT
public:
    explicit NleLauncher(QObject* parent = nullptr);
    ~NleLauncher() override;

    // Spawn the NLE process, passing `ipcPipeName` on the command line.
    // Returns true if QProcess::start() was accepted (NOT if the child
    // has successfully booted — that arrives later via the IPC channel).
    bool launch(const QString& ipcPipeName);

    // True if the QProcess thinks the child is alive.  Does NOT mean the
    // IPC handshake has happened — check NleControlChannel for that.
    bool isRunning() const;

signals:
    // Emitted when the NLE process exits, for any reason (clean quit or
    // crash).  `exitCode` is the process exit code; `normal` is true if
    // the process exited normally, false if it was killed / crashed.
    void nleExited(int exitCode, bool normal);

private slots:
    void onProcessFinished(int exitCode, int exitStatus);
    void onProcessErrorOccurred();

private:
    QString resolveNleExePath() const;

    QProcess* m_process { nullptr };

    // Windows Job Object handle.  void* to keep <windows.h> out of this
    // header.  Opaque HANDLE value stashed by the .cpp side.
    void*     m_jobHandle { nullptr };
};

} // namespace lamosh
