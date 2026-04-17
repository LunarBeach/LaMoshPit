#include "gui/nle_bridge/NleControlChannel.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDebug>
#include <QRandomGenerator>
#include <QString>

namespace lamosh {

NleControlChannel::NleControlChannel(QObject* parent)
    : QObject(parent)
{
}

NleControlChannel::~NleControlChannel() = default;

QString NleControlChannel::listen()
{
    if (m_server) {
        return m_server->fullServerName();   // already listening
    }

    const qint64 pid = QCoreApplication::applicationPid();
    const quint32 rnd = QRandomGenerator::global()->generate();
    const QString pipeName = QStringLiteral("lamosh-nle-%1-%2")
        .arg(pid)
        .arg(rnd, 8, 16, QLatin1Char('0'));

    // A dangling pipe from a previous crashed run would cause listen() to
    // fail with EADDRINUSE.  removeServer() silently no-ops if the name
    // isn't registered, so it's safe to call unconditionally.
    QLocalServer::removeServer(pipeName);

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(pipeName)) {
        qWarning() << "[ipc] QLocalServer::listen failed:"
                   << m_server->errorString();
        m_server->deleteLater();
        m_server = nullptr;
        return {};
    }
    connect(m_server, &QLocalServer::newConnection,
            this,     &NleControlChannel::onNewConnection);

    qInfo() << "[ipc] host listening on pipe:" << pipeName;
    return pipeName;
}

bool NleControlChannel::isNleConnected() const
{
    return m_client && m_client->state() == QLocalSocket::ConnectedState;
}

void NleControlChannel::onNewConnection()
{
    if (!m_server) return;
    QLocalSocket* incoming = m_server->nextPendingConnection();
    if (!incoming) return;

    // Only accept the first connection.  If one is already active (NLE
    // re-spawned somehow?), close the newcomer — the launcher is
    // authoritative over NLE lifecycle.
    if (isNleConnected()) {
        qWarning() << "[ipc] rejecting second NLE connection";
        incoming->disconnectFromServer();
        incoming->deleteLater();
        return;
    }

    m_client = incoming;
    m_client->setParent(this);
    connect(m_client, &QLocalSocket::disconnected,
            this,     &NleControlChannel::onClientDisconnected);
    connect(m_client, &QLocalSocket::readyRead,
            this,     &NleControlChannel::onClientReadyRead);

    qInfo() << "[ipc] NLE client connected";

    // Drain any commands queued before connection arrived.
    for (const QJsonObject& cmd : std::as_const(m_pendingCommands)) {
        const QByteArray bytes =
            QJsonDocument(cmd).toJson(QJsonDocument::Compact) + '\n';
        m_client->write(bytes);
    }
    m_pendingCommands.clear();
    m_client->flush();

    emit nleConnected();
}

void NleControlChannel::onClientDisconnected()
{
    qInfo() << "[ipc] NLE client disconnected";
    if (m_client) {
        m_client->deleteLater();
        m_client = nullptr;
    }
    m_readBuf.clear();
    emit nleDisconnected();
}

void NleControlChannel::onClientReadyRead()
{
    if (!m_client) return;
    m_readBuf.append(m_client->readAll());
    int idx;
    while ((idx = m_readBuf.indexOf('\n')) != -1) {
        const QByteArray line = m_readBuf.left(idx);
        m_readBuf.remove(0, idx + 1);
        if (!line.isEmpty()) dispatchLine(line);
    }
}

void NleControlChannel::sendCommand(const QJsonObject& cmd)
{
    if (!isNleConnected()) {
        m_pendingCommands.append(cmd);
        return;
    }
    const QByteArray bytes =
        QJsonDocument(cmd).toJson(QJsonDocument::Compact) + '\n';
    m_client->write(bytes);
    m_client->flush();
}

void NleControlChannel::dispatchLine(const QByteArray& line)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[ipc] malformed incoming JSON:"
                   << err.errorString() << "line:" << line;
        return;
    }
    emit eventReceived(doc.object());
}

} // namespace lamosh
