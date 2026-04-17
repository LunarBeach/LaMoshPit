#pragma once

// =============================================================================
// CoreControlClient — NLE-side endpoint of the IPC channel with LaMoshPit.exe.
//
// The MSVC host (LaMoshPit.exe) creates a QLocalServer with a unique pipe
// name, spawns LaMoshPit_NLE.exe with `--ipc-pipe <name>`, and waits for
// the NLE to connect.  We (the NLE) are the client: we open a QLocalSocket
// to that named pipe, handshake with `{"evt":"ready","version":"1.0"}`,
// then dispatch inbound commands to the rest of the NLE app.
//
// Protocol: one JSON object per line, LF-terminated, UTF-8.  Compact
// serialisation (no embedded newlines), newline-delimited framing.
//
// Reconnect on disconnect: if the socket drops unexpectedly (e.g. the host
// crashed) we don't attempt to reconnect.  LaMoshPit's Job Object setup
// will kill us anyway when it dies; reconnect would just be theatre.
//
// Same-file as a sibling QObject in nle/src/ipc/ so Qt's AUTOMOC picks it
// up without any nle/CMakeLists.txt change beyond adding the .cpp.
// =============================================================================

#include <QObject>
#include <QString>
#include <QJsonObject>

class QLocalSocket;

namespace lamosh::nle {

class CoreControlClient : public QObject {
    Q_OBJECT
public:
    explicit CoreControlClient(QObject* parent = nullptr);
    ~CoreControlClient() override;

    // Connects to the named-pipe server at `pipeName`.  Does not block;
    // success/failure arrives via signals.  Idempotent — calling while
    // already connected is a no-op.
    void connectToCore(const QString& pipeName);

    bool isConnected() const;

signals:
    // Fired when the socket is open and the "ready" handshake has been sent.
    void connected();

    // Fired on unexpected disconnect or any socket error.
    void disconnected();

    // Fired for every inbound command object from LaMoshPit.exe.  The
    // object's "cmd" field identifies the command; see
    // docs/Implementation_Plan_NLE_Rebuild_2026-04-17.md Step 3 for the
    // documented command set.
    void commandReceived(const QJsonObject& msg);

public slots:
    // Send an event object to LaMoshPit.exe.  The object must be JSON-
    // serialisable and have an "evt" field.  No-op if not connected.
    void sendEvent(const QJsonObject& evt);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred();

private:
    void dispatchLine(const QByteArray& line);

    QLocalSocket* m_sock { nullptr };
    QString       m_pipeName;
    QByteArray    m_readBuf;   // accumulates partial lines across reads
};

} // namespace lamosh::nle
