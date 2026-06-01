/*
 * VisionPass 消息客户端实现
 *
 * TCP连接管理：连接、断线自动重连（5秒间隔）
 * 消息协议：JSON文本 + 换行符分隔
 * 接收缓冲区：处理TCP粘包/拆包
 */

#include "MessageClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>
#include <QNetworkInterface>

MessageClient::MessageClient(QObject *parent)
	: QObject(parent),
	  m_socket(nullptr),
	  m_port(0),
	  m_manualDisconnect(false),
	  m_discoverSocket(nullptr),
	  m_discoverSocket2(nullptr),
	  m_discoverTimer(nullptr)
{
	/* 创建重连定时器 */
	m_reconnectTimer = new QTimer(this);
	m_reconnectTimer->setInterval(5000);  /* 5秒重连间隔 */
	connect(m_reconnectTimer, &QTimer::timeout,
		this, &MessageClient::onReconnectTimer);

	/* 始终监听发现请求（让user_manager能找到本设备） */
	startDiscoverListener();
}

MessageClient::~MessageClient()
{
	m_manualDisconnect = true;
	m_reconnectTimer->stop();
	if (m_socket) {
		m_socket->disconnectFromHost();
		m_socket->deleteLater();
	}
}

/*
 * 连接到管理程序服务器
 * 参数 host：服务器IP地址
 * 参数 port：服务器端口号
 */
void MessageClient::connectToServer(const QString &host, quint16 port)
{
	m_host = host;
	m_port = port;
	m_manualDisconnect = false;

	/* 如果已有socket，先清理 */
	if (m_socket) {
		m_socket->disconnectFromHost();
		m_socket->deleteLater();
	}

	/* 创建新的TCP socket */
	m_socket = new QTcpSocket(this);

	connect(m_socket, &QTcpSocket::connected,
		this, &MessageClient::onConnected);
	connect(m_socket, &QTcpSocket::disconnected,
		this, &MessageClient::onDisconnected);
	connect(m_socket, &QTcpSocket::readyRead,
		this, &MessageClient::onReadyRead);
	connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
		this, &MessageClient::onSocketError);

	qInfo() << "MessageClient: Connecting to" << host << ":" << port;
	m_socket->connectToHost(host, port);
}

/*
 * 断开连接（手动断开不触发自动重连）
 */
void MessageClient::disconnectFromServer()
{
	m_manualDisconnect = true;
	m_reconnectTimer->stop();

	if (m_socket) {
		m_socket->disconnectFromHost();
	}

	qInfo() << "MessageClient: Disconnected";
}

/*
 * 启动发现请求监听
 * 始终运行，让user_manager等工具能找到本设备
 */
void MessageClient::startDiscoverListener()
{
	/* 监听9501（管理端发现） */
	if (!m_discoverSocket) {
		m_discoverSocket = new QUdpSocket(this);
		m_discoverSocket->bind(QHostAddress::Any, 9501,
				       QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
		connect(m_discoverSocket, &QUdpSocket::readyRead,
			this, &MessageClient::onDiscoveryReply);
	}
	/* 监听9503（user_manager发现） */
	if (!m_discoverSocket2) {
		m_discoverSocket2 = new QUdpSocket(this);
		m_discoverSocket2->bind(QHostAddress::Any, 9503,
					QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
		connect(m_discoverSocket2, &QUdpSocket::readyRead,
			this, &MessageClient::onDiscoveryReply);
	}
	qInfo() << "MessageClient: Discovery listeners started on ports 9501, 9503";
}

/*
 * 启动UDP自动发现（主动寻找管理端）
 * 每隔3秒发送一次广播，直到收到管理端回复
 */
void MessageClient::startDiscovery(quint16 discoverPort, quint16 tcpPort)
{
	m_discoverPort = discoverPort;
	m_tcpPort = tcpPort;

	/* 创建UDP socket */
	if (!m_discoverSocket) {
		m_discoverSocket = new QUdpSocket(this);
		m_discoverSocket->bind(QHostAddress::Any, discoverPort,
				       QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
		connect(m_discoverSocket, &QUdpSocket::readyRead,
			this, &MessageClient::onDiscoveryReply);
	}

	/* 创建发现定时器 */
	if (!m_discoverTimer) {
		m_discoverTimer = new QTimer(this);
		m_discoverTimer->setInterval(3000);
		connect(m_discoverTimer, &QTimer::timeout,
			this, &MessageClient::sendDiscoveryBroadcast);
	}

	qInfo() << "MessageClient: Starting UDP discovery on port" << discoverPort;
	m_discoverTimer->start();
	sendDiscoveryBroadcast();  /* 立即发送第一次 */
}

/*
 * 发送UDP广播发现请求
 */
void MessageClient::sendDiscoveryBroadcast()
{
	QByteArray msg = "VisionPass_DISCOVER";
	m_discoverSocket->writeDatagram(msg, QHostAddress::Broadcast, m_discoverPort);
	qInfo() << "MessageClient: Discovery broadcast sent";
}

/*
 * 处理UDP发现消息（双向）
 * 1. 收到 "VisionPass_MANAGER:ip:port" → 管理端回复，连接之
 * 2. 收到 "VisionPass_DISCOVER" → 其他工具在找本设备，回复自己的IP
 */
void MessageClient::onDiscoveryReply()
{
	/* 支持两个socket（9501和9503） */
	QUdpSocket *socket = qobject_cast<QUdpSocket*>(sender());
	if (!socket) socket = m_discoverSocket;
	if (!socket) return;

	while (socket->hasPendingDatagrams()) {
		QByteArray data;
		data.resize(socket->pendingDatagramSize());
		QHostAddress senderAddr;
		quint16 senderPort;
		socket->readDatagram(data.data(), data.size(),
				     &senderAddr, &senderPort);
		QString msg = QString::fromUtf8(data);

		if (msg.startsWith("VisionPass_MANAGER:")) {
			/* 管理端回复 → 连接 */
			QStringList parts = msg.mid(19).split(":");
			if (parts.size() >= 1) {
				QString ip = parts[0];
				quint16 port = (parts.size() >= 2) ?
					parts[1].toUShort() : m_tcpPort;
				qInfo() << "MessageClient: Discovered manager at" << ip << ":" << port;
				if (m_discoverTimer) m_discoverTimer->stop();
				connectToServer(ip, port);
			}
		} else if (msg == "VisionPass_DISCOVER") {
			/* 其他工具在找本设备 → 回复自己的IP */
			QString myIp;
			for (const QHostAddress &addr : QNetworkInterface::allAddresses()) {
				if (addr != QHostAddress::LocalHost &&
				    addr.protocol() == QAbstractSocket::IPv4Protocol) {
					myIp = addr.toString();
					break;
				}
			}
			if (!myIp.isEmpty()) {
				QByteArray reply = QString("VisionPass_MANAGER:%1:9500")
					.arg(myIp).toUtf8();
				socket->writeDatagram(reply, senderAddr, senderPort);
				qInfo() << "MessageClient: Discovery reply sent to"
					<< senderAddr.toString() << "(my IP:" << myIp << ")";
			}
		}
	}
}

/*
 * 发送文字消息
 * 消息格式：{"type":"text","from":"doorlock","content":"消息内容","time":"时间"}
 */
void MessageClient::sendMessage(const QString &text)
{
	if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
		qWarning() << "MessageClient: Cannot send, not connected";
		emit connectionError("未连接到管理程序");
		return;
	}

	/* 构建JSON消息 */
	QJsonObject json;
	json["type"] = "text";
	json["from"] = "doorlock";
	json["content"] = text;
	json["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

	QJsonDocument doc(json);
	QByteArray data = doc.toJson(QJsonDocument::Compact);
	data.append('\n');  /* 换行符作为消息分隔符 */

	m_socket->write(data);
	m_socket->flush();

	qInfo() << "MessageClient: Sent message:" << text;
}

/*
 * 发送语音消息
 * 消息格式：{"type":"voice","from":"doorlock","duration":5,"audio":"base64...","time":"..."}
 */
void MessageClient::sendVoiceMessage(const QByteArray &pcmData, int durationSec)
{
	if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
		qWarning() << "MessageClient: Cannot send voice, not connected";
		emit connectionError("未连接到管理程序");
		return;
	}

	QJsonObject json;
	json["type"] = "voice";
	json["from"] = "doorlock";
	json["duration"] = durationSec;
	json["audio"] = QString::fromLatin1(pcmData.toBase64());
	json["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

	QJsonDocument doc(json);
	QByteArray data = doc.toJson(QJsonDocument::Compact);
	data.append('\n');

	m_socket->write(data);
	m_socket->flush();

	qInfo() << "MessageClient: Sent voice message," << durationSec << "s,"
		<< pcmData.size() << "bytes";
}

bool MessageClient::isConnected() const
{
	return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

/*
 * TCP连接成功
 */
void MessageClient::onConnected()
{
	qInfo() << "MessageClient: Connected to" << m_host << ":" << m_port;
	m_reconnectTimer->stop();
	m_recvBuffer.clear();
	emit connectionStateChanged(true);
}

/*
 * TCP断开
 */
void MessageClient::onDisconnected()
{
	qInfo() << "MessageClient: Disconnected from server";
	emit connectionStateChanged(false);

	/* 非手动断开 → 启动自动重连 */
	if (!m_manualDisconnect) {
		qInfo() << "MessageClient: Will reconnect in 5 seconds";
		m_reconnectTimer->start();
	}
}

/*
 * 收到数据
 * 处理TCP粘包：按换行符分割完整消息
 */
void MessageClient::onReadyRead()
{
	QByteArray data = m_socket->readAll();
	m_recvBuffer.append(QString::fromUtf8(data));

	/* 按换行符分割消息 */
	while (m_recvBuffer.contains('\n')) {
		int idx = m_recvBuffer.indexOf('\n');
		QString line = m_recvBuffer.left(idx).trimmed();
		m_recvBuffer.remove(0, idx + 1);

		if (line.isEmpty())
			continue;

		/* 解析JSON */
		QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
		if (!doc.isObject()) {
			qWarning() << "MessageClient: Invalid JSON:" << line;
			continue;
		}

		QJsonObject json = doc.object();
		QString type = json["type"].toString();
		QString content = json["content"].toString();
		QString sender = json["from"].toString();
		QString time = json["time"].toString();

		if (type == "text" && !content.isEmpty()) {
			qInfo() << "MessageClient: Received from" << sender
				<< ":" << content;
			emit messageReceived(content, sender);
		} else if (type == "voice") {
			int duration = json["duration"].toInt();
			QString audioBase64 = json["audio"].toString();
			QByteArray pcmData = QByteArray::fromBase64(audioBase64.toLatin1());
			qInfo() << "MessageClient: Received voice from" << sender
				<< "," << duration << "s," << pcmData.size() << "bytes";
			emit voiceMessageReceived(pcmData, duration, sender);
		}
	}
}

/*
 * 连接错误处理
 */
void MessageClient::onSocketError(QAbstractSocket::SocketError error)
{
	Q_UNUSED(error);
	QString errMsg = m_socket->errorString();
	qWarning() << "MessageClient: Error:" << errMsg;
	emit connectionError(errMsg);

	/* 连接失败 → 启动自动重连 */
	if (!m_manualDisconnect && !m_reconnectTimer->isActive()) {
		qInfo() << "MessageClient: Will reconnect in 5 seconds";
		m_reconnectTimer->start();
	}
}

/*
 * 自动重连
 */
void MessageClient::onReconnectTimer()
{
	if (!m_socket || m_socket->state() == QAbstractSocket::UnconnectedState) {
		qInfo() << "MessageClient: Reconnecting to" << m_host << ":" << m_port;
		m_socket->connectToHost(m_host, m_port);
	}
}
