#pragma once

// =============================================================================
// NleControlChannel — host (LaMoshPit.exe) side of the IPC channel with
// LaMoshPit_NLE.exe.
//
// Owns a QLocalServer.  At construction we listen on a unique pipe name
// (PID-derived to avoid clashes if two copies of LaMoshPit run at once).
// NleLauncher spawns the NLE process with `--ipc-pipe <name>`, the NLE
// connects back, and from then on this class is the single pump for
// bidirectional JSON messages.
//
// Protocol: one JSON object per line, LF-terminated, UTF-8 compact.
//
// See docs/Implementation_Plan_NLE_Rebuild_2026-04-17.md Step 3 for the
// command and event taxonomy.  For Step 3 itself, the only requirement
// is that connection + handshake + shutdown work end-to-end.
// =============================================================================

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QByteArray>

class QLocalServer;
class QLocalSocket;

namespace lamosh {

class NleControlChannel : public QObject {
    Q_OBJECT
public:
    explicit NleControlChannel(QObject* parent = nullptr);
    ~NleControlChannel() override;

    // Start listening on a unique local pipe.  Returns the pipe name on
    // success (pass this to NleLauncher as `--ipc-pipe <name>`) or an
    // empty string on failure.  The name is of the form
    // "lamosh-nle-<pid>-<random>" on Windows, mapped to
    // \\.\pipe\lamosh-nle-<pid>-<random> by Qt.
    QString listen();

    bool isNleConnected() const;

signals:
    void nleConnected();
    void nleDisconnected();
    void eventReceived(const QJsonObject& evt);

public slots:
    // Send a command object to the NLE.  Object should have a "cmd"
    // field.  Queued until the NLE connects if we're not ready yet
    // (prevents the race where MainWindow tries to send before the
    // client socket is up).
    void sendCommand(const QJsonObject& cmd);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onClientReadyRead();

private:
    void dispatchLine(const QByteArray& line);

    QLocalServer* m_server { nullptr };
    QLocalSocket* m_client { nullptr };
    QByteArray    m_readBuf;
    QList<QJsonObject> m_pendingCommands;  // queued before NLE connects
};

} // namespace lamosh
