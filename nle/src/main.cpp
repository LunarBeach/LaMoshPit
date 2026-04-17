// =============================================================================
// LaMoshPit_NLE.exe — entry point.
//
// Spawned as a child process by LaMoshPit.exe (the MSVC host) with
// `--ipc-pipe <name>` on the command line.  The <name> identifies a
// QLocalServer named pipe the host is listening on; we connect as client
// and exchange JSON messages for visibility, project-path, VJ-mode, etc.
//
// Running without `--ipc-pipe` is allowed (for manual testing / dev
// builds from the MINGW64 shell); the NLE just opens its window without
// the IPC channel and runs standalone.
// =============================================================================

#include <QApplication>
#include <QCommandLineParser>
#include <QMainWindow>
#include <QStatusBar>
#include <QDebug>
#include <QJsonObject>

#include <Mlt.h>

#include "ipc/CoreControlClient.h"

using lamosh::nle::CoreControlClient;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("LaMoshPit NLE");
    QCoreApplication::setOrganizationName("LaMoshPit");

    // ── CLI parsing ─────────────────────────────────────────────────────────
    QCommandLineParser cli;
    cli.setApplicationDescription("LaMoshPit NLE Sequencer (MLT-based)");
    cli.addHelpOption();
    QCommandLineOption pipeOpt(
        QStringLiteral("ipc-pipe"),
        QStringLiteral("Connect to LaMoshPit.exe's IPC server on the named pipe <name>."),
        QStringLiteral("name"));
    cli.addOption(pipeOpt);
    cli.process(app);

    // ── MLT init ────────────────────────────────────────────────────────────
    Mlt::Repository* repo = Mlt::Factory::init();
    if (!repo) {
        qWarning() << "Mlt::Factory::init() returned null — modules not found.";
        return 2;
    }
    qInfo() << "MLT initialised, version:" << mlt_version_get_string();

    // ── Main window stub ────────────────────────────────────────────────────
    QMainWindow win;
    win.setWindowTitle("LaMoshPit NLE");
    win.resize(1280, 800);
    win.statusBar()->showMessage(
        QString("Ready — MLT %1").arg(mlt_version_get_string()));
    win.show();

    // ── IPC client (only if --ipc-pipe was passed) ─────────────────────────
    CoreControlClient ipc;
    if (cli.isSet(pipeOpt)) {
        const QString pipeName = cli.value(pipeOpt);
        qInfo() << "[ipc] connecting to host on pipe:" << pipeName;

        QObject::connect(&ipc, &CoreControlClient::connected, [&]() {
            qInfo() << "[ipc] connected + ready sent";
            win.statusBar()->showMessage(
                QString("Ready — MLT %1 — connected to LaMoshPit core")
                    .arg(mlt_version_get_string()));
        });
        QObject::connect(&ipc, &CoreControlClient::disconnected, [&]() {
            qInfo() << "[ipc] disconnected";
            win.statusBar()->showMessage(
                QString("Ready — MLT %1 — IPC DISCONNECTED")
                    .arg(mlt_version_get_string()));
        });
        QObject::connect(&ipc, &CoreControlClient::commandReceived,
                         [&](const QJsonObject& msg) {
            const QString cmd = msg.value(QStringLiteral("cmd")).toString();
            qInfo() << "[ipc] cmd received:" << cmd;
            // Step 4 will dispatch show/hide/shutdown/etc. here.  For Step 3,
            // we only care that the handshake + command delivery works.
            if (cmd == QLatin1String("shutdown")) {
                QCoreApplication::quit();
            }
        });

        ipc.connectToCore(pipeName);
    } else {
        qInfo() << "[ipc] no --ipc-pipe given; running standalone";
    }

    const int rc = app.exec();
    Mlt::Factory::close();
    return rc;
}
