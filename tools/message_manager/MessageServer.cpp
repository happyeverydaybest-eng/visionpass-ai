/*
 * VisionPass 消息服务器实现
 *
 * TCP服务器：监听端口，管理客户端连接，收发JSON消息
 */

#include "MessageServer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>
#include <QNetworkInterface>

MessageServer::MessageServer(QObject *parent)
	: QObject(parent),
	  m_server(nullptr),
	  m_discoverSocket(nullptr)
{
}

MessageServer::~MessageServer()
{
	stop();
}

bool MessageServer::start(quint16 port)
{
	if (m_server) {
		stop();
	}

	m_server = new QTcpServer(this);
	connect(m_server, &QTcpServer::newConnection,
		this, &MessageServer::onNewConnection);

	if (!m_server->listen(QHostAddress::Any, port)) {
		QString err = m_server->errorString();
		qWarning() << "MessageServer: Listen failed:" << err;
		emit serverError(err);
		return false;
	}

	qInfo() << "MessageServer: TCP listening on port" << port;

	/* 启动UDP发现监听（9501端口） */
	m_discoverSocket = new QUdpSocket(this);
	m_discoverSocket->bind(QHostAddress::Any, 9501,
			       QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
	connect(m_discoverSocket, &QUdpSocket::readyRead,
		this, &MessageServer::onDiscoveryRequest);
	qInfo() << "MessageServer: UDP discovery listening on port 9501";

	return true;
}

void MessageServer::stop()
{
	for (QTcpSocket *client : m_clients) {
		client->disconnectFromHost();
		client->deleteLater();
	}
	m_clients.clear();

	if (m_server) {
		m_server->close();
		m_server->deleteLater();
		m_server = nullptr;
	}
}

bool MessageServer::hasClient() const
{
	return !m_clients.isEmpty();
}

void MessageServer::sendMessage(const QString &text)
{
	QJsonObject json;
	json["type"] = "text";
	json["from"] = "manager";
	json["content"] = text;
	json["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

	QJsonDocument doc(json);
	QByteArray data = doc.toJson(QJsonDocument::Compact);
	data.append('\n');

	for (QTcpSocket *client : m_clients) {
		if (client->state() == QAbstractSocket::ConnectedState) {
			client->write(data);
			client->flush();
		}
	}

	qInfo() << "MessageServer: Sent:" << text;
}

void MessageServer::sendVoiceMessage(const QByteArray &pcmData, int duration)
{
	QJsonObject json;
	json["type"] = "voice";
	json["from"] = "manager";
	json["duration"] = duration;
	json["audio"] = QString::fromLatin1(pcmData.toBase64());
	json["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

	QJsonDocument doc(json);
	QByteArray data = doc.toJson(QJsonDocument::Compact);
	data.append('\n');

	for (QTcpSocket *client : m_clients) {
		if (client->state() == QAbstractSocket::ConnectedState) {
			client->write(data);
			client->flush();
		}
	}

	qInfo() << "MessageServer: Sent voice," << duration << "s";
}

void MessageServer::onNewConnection()
{
	while (m_server->hasPendingConnections()) {
		QTcpSocket *client = m_server->nextPendingConnection();
		QString address = client->peerAddress().toString();

		qInfo() << "MessageServer: Client connected from" << address;

		connect(client, &QTcpSocket::readyRead,
			this, &MessageServer::onReadyRead);
		connect(client, &QTcpSocket::disconnected,
			this, &MessageServer::onClientDisconnected);

		m_clients.append(client);
		emit clientConnected(address);
	}
}

void MessageServer::onReadyRead()
{
	QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
	if (!client)
		return;

	/* 追加到该客户端的缓冲区（处理TCP拆包） */
	m_recvBuffers[client].append(QString::fromUtf8(client->readAll()));

	/* 按换行符分割完整消息 */
	while (m_recvBuffers[client].contains('\n')) {
		int idx = m_recvBuffers[client].indexOf('\n');
		QString line = m_recvBuffers[client].left(idx).trimmed();
		m_recvBuffers[client].remove(0, idx + 1);

		if (line.isEmpty())
			continue;

		QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
		if (!doc.isObject()) {
			qWarning() << "MessageServer: Invalid JSON, length=" << line.length();
			continue;
		}

		QJsonObject json = doc.object();
		QString type = json["type"].toString();
		QString from = json["from"].toString();

		if (type == "text") {
			QString content = json["content"].toString();
			if (!content.isEmpty()) {
				qInfo() << "MessageServer: Received text from" << from
					<< ":" << content;
				emit messageReceived(content, from);
			}
		} else if (type == "voice") {
			int duration = json["duration"].toInt();
			QString audioBase64 = json["audio"].toString();
			QByteArray pcmData = QByteArray::fromBase64(audioBase64.toLatin1());
			qInfo() << "MessageServer: Received voice from" << from
				<< "," << duration << "s," << pcmData.size() << "bytes";
			emit voiceMessageReceived(pcmData, duration, from);
		}
	}
}

/*
 * 收到UDP发现请求
 * 设备端广播: "VisionPass_DISCOVER"
 * 回复: "VisionPass_MANAGER:<myIP>:9500"
 */
void MessageServer::onDiscoveryRequest()
{
	while (m_discoverSocket->hasPendingDatagrams()) {
		QByteArray data;
		data.resize(m_discoverSocket->pendingDatagramSize());
		QHostAddress senderAddr;
		quint16 senderPort;
		m_discoverSocket->readDatagram(data.data(), data.size(),
						&senderAddr, &senderPort);

		if (data == "VisionPass_DISCOVER") {
			/* 获取本机IP（取第一个非回环IPv4地址） */
			QString myIp;
			for (const QHostAddress &addr : QNetworkInterface::allAddresses()) {
				if (addr != QHostAddress::LocalHost &&
				    addr.protocol() == QAbstractSocket::IPv4Protocol) {
					myIp = addr.toString();
					break;
				}
			}

			if (myIp.isEmpty())
				myIp = "127.0.0.1";

			QByteArray reply = QString("VisionPass_MANAGER:%1:9500").arg(myIp).toUtf8();
			m_discoverSocket->writeDatagram(reply, senderAddr, senderPort);
			qInfo() << "MessageServer: Discovery reply sent to"
				<< senderAddr.toString() << ":" << myIp;
		}
	}
}

void MessageServer::onClientDisconnected()
{
	QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
	if (!client)
		return;

	QString address = client->peerAddress().toString();
	qInfo() << "MessageServer: Client disconnected:" << address;

	m_clients.removeOne(client);
	m_recvBuffers.remove(client);
	client->deleteLater();
	emit clientDisconnected(address);
}
