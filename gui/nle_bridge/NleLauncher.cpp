#include "gui/nle_bridge/NleLauncher.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace lamosh {

NleLauncher::NleLauncher(QObject* parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &NleLauncher::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &NleLauncher::onProcessErrorOccurred);

    // Forward child stdout/stderr to our own output for diagnostic
    // visibility during development.  In a shipping build we'd silence
    // these or pipe them into the IPC log event stream.
    m_process->setProcessChannelMode(QProcess::ForwardedChannels);
}

NleLauncher::~NleLauncher()
{
#ifdef _WIN32
    if (m_jobHandle) {
        // Closing the job handle terminates every process assigned to it
        // (KILL_ON_JOB_CLOSE below).  This is the shutdown path when
        // LaMoshPit.exe exits cleanly — the NLE dies synchronously.
        ::CloseHandle(static_cast<HANDLE>(m_jobHandle));
        m_jobHandle = nullptr;
    }
#endif
}

QString NleLauncher::resolveNleExePath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString exeName = QStringLiteral("LaMoshPit_NLE.exe");

    // Search order:
    //   1) <appDir>/nle/LaMoshPit_NLE.exe         (shipping layout, Step 12)
    //   2) <appDir>/../nle/build/LaMoshPit_NLE.exe  (dev layout — exe is
    //      <repoRoot>/build/{Release,Debug}/LaMoshPit.exe, NLE is at
    //      <repoRoot>/nle/build/LaMoshPit_NLE.exe)
    //   3) <appDir>/../../nle/build/LaMoshPit_NLE.exe  (same as 2 but one
    //      more level up for the case of nested CMake build trees)
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("nle/") + exeName),
        QDir(appDir).filePath(QStringLiteral("../nle/build/") + exeName),
        QDir(appDir).filePath(QStringLiteral("../../nle/build/") + exeName),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).canonicalFilePath();
        }
    }
    return {};
}

bool NleLauncher::launch(const QString& ipcPipeName)
{
    if (isRunning()) {
        qWarning() << "[launcher] NLE already running, not spawning again";
        return true;
    }

    const QString exePath = resolveNleExePath();
    if (exePath.isEmpty()) {
        qWarning() << "[launcher] LaMoshPit_NLE.exe not found in known "
                      "locations (app dir or ../nle/build). Have you built "
                      "the NLE with bash scripts/build-nle-msys2.sh ?";
        return false;
    }

    qInfo() << "[launcher] spawning NLE at" << exePath;

#ifdef _WIN32
    // Create the Job Object BEFORE spawning.  We'll assign the child
    // process to it right after creation.  KILL_ON_JOB_CLOSE means when
    // our last handle to the job closes (or our process dies), all
    // processes in the job are terminated by the kernel.
    if (!m_jobHandle) {
        HANDLE hJob = ::CreateJobObjectW(nullptr, nullptr);
        if (!hJob) {
            qWarning() << "[launcher] CreateJobObjectW failed:" << ::GetLastError();
            return false;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
        jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!::SetInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                        &jobInfo, sizeof(jobInfo))) {
            qWarning() << "[launcher] SetInformationJobObject failed:"
                       << ::GetLastError();
            ::CloseHandle(hJob);
            return false;
        }
        m_jobHandle = hJob;
    }
#endif

    m_process->setProgram(exePath);
    m_process->setArguments({
        QStringLiteral("--ipc-pipe"), ipcPipeName,
    });

    // On Windows, set the working directory to the NLE's own directory
    // so its relative DLL/runtime-asset searches resolve correctly.
    m_process->setWorkingDirectory(QFileInfo(exePath).absolutePath());

    m_process->start();
    if (!m_process->waitForStarted(3000)) {
        qWarning() << "[launcher] QProcess::waitForStarted failed:"
                   << m_process->errorString();
        return false;
    }

#ifdef _WIN32
    // Assign the running child to our Job.  Must happen AFTER start()
    // gives us a valid process handle.
    const qint64 pid = m_process->processId();
    HANDLE hChild = ::OpenProcess(
        PROCESS_SET_QUOTA | PROCESS_TERMINATE,
        FALSE, static_cast<DWORD>(pid));
    if (!hChild) {
        qWarning() << "[launcher] OpenProcess failed for NLE pid" << pid
                   << "err:" << ::GetLastError();
        // Not fatal — the NLE will still run, just without auto-kill on
        // host exit.  User can manually kill it via Task Manager.
    } else {
        if (!::AssignProcessToJobObject(static_cast<HANDLE>(m_jobHandle), hChild)) {
            qWarning() << "[launcher] AssignProcessToJobObject failed:"
                       << ::GetLastError();
        } else {
            qInfo() << "[launcher] NLE pid" << pid
                    << "assigned to Job Object for auto-cleanup";
        }
        ::CloseHandle(hChild);
    }
#endif

    return true;
}

bool NleLauncher::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void NleLauncher::onProcessFinished(int exitCode, int exitStatus)
{
    const bool normal = (exitStatus == QProcess::NormalExit);
    qInfo() << "[launcher] NLE exited"
            << "code=" << exitCode
            << (normal ? "(normal)" : "(crashed)");
    emit nleExited(exitCode, normal);
}

void NleLauncher::onProcessErrorOccurred()
{
    qWarning() << "[launcher] QProcess error:" << m_process->errorString();
}

} // namespace lamosh
