#include "ipc/CoreControlClient.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocalSocket>

namespace lamosh::nle {

CoreControlClient::CoreControlClient(QObject* parent)
    : QObject(parent)
    , m_sock(new QLocalSocket(this))
{
    connect(m_sock, &QLocalSocket::connected,
            this,   &CoreControlClient::onConnected);
    connect(m_sock, &QLocalSocket::disconnected,
            this,   &CoreControlClient::onDisconnected);
    connect(m_sock, &QLocalSocket::readyRead,
            this,   &CoreControlClient::onReadyRead);
    connect(m_sock, &QLocalSocket::errorOccurred,
            this,   &CoreControlClient::onErrorOccurred);
}

CoreControlClient::~CoreControlClient() = default;

bool CoreControlClient::isConnected() const
{
    return m_sock && m_sock->state() == QLocalSocket::ConnectedState;
}

void CoreControlClient::connectToCore(const QString& pipeName)
{
    if (isConnected() || m_sock->state() == QLocalSocket::ConnectingState) return;
    m_pipeName = pipeName;
    m_sock->connectToServer(pipeName);
}

void CoreControlClient::onConnected()
{
    // Handshake: announce ourselves with a version so the host knows it's
    // talking to a compatible NLE build.
    QJsonObject ready {
        { QStringLiteral("evt"),     QStringLiteral("ready") },
        { QStringLiteral("version"), QStringLiteral("1.0")  },
    };
    sendEvent(ready);
    emit connected();
}

void CoreControlClient::onDisconnected()
{
    emit disconnected();
}

void CoreControlClient::onErrorOccurred()
{
    // QLocalSocket errors during handshake or normal operation are logged
    // for diagnostics but otherwise handled the same as a disconnect.  If
    // the host is gone, the Job Object will kill us anyway.
    qWarning() << "[ipc] QLocalSocket error:" << m_sock->errorString();
    if (m_sock->state() != QLocalSocket::ConnectedState) {
        emit disconnected();
    }
}

void CoreControlClient::onReadyRead()
{
    m_readBuf.append(m_sock->readAll());
    // Split on newlines.  Any trailing partial line stays in the buffer
    // for the next readyRead.
    int idx;
    while ((idx = m_readBuf.indexOf('\n')) != -1) {
        const QByteArray line = m_readBuf.left(idx);
        m_readBuf.remove(0, idx + 1);
        if (!line.isEmpty()) dispatchLine(line);
    }
}

void CoreControlClient::sendEvent(const QJsonObject& evt)
{
    if (!isConnected()) return;
    const QJsonDocument doc(evt);
    const QByteArray bytes = doc.toJson(QJsonDocument::Compact) + '\n';
    m_sock->write(bytes);
    m_sock->flush();
}

void CoreControlClient::dispatchLine(const QByteArray& line)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[ipc] malformed incoming JSON:"
                   << err.errorString() << "line:" << line;
        return;
    }
    emit commandReceived(doc.object());
}

} // namespace lamosh::nle
