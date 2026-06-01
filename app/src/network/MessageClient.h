/*
 * VisionPass 消息客户端（TCP）
 *
 * 功能：连接Ubuntu管理程序，收发文字消息
 * 协议：JSON文本，每条消息以换行符(\n)分隔
 * 消息格式：{"type":"text","from":"doorlock","content":"你好","time":"2024-12-04 10:30:00"}
 *
 * 使用方式：
 *   MessageClient *client = new MessageClient(this);
 *   client->connectToServer("192.168.0.100", 9500);
 *   connect(client, &MessageClient::messageReceived, this, &MyClass::onMessage);
 *   client->sendMessage("你好");
 */

#ifndef MESSAGECLIENT_H
#define MESSAGECLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QString>

class MessageClient : public QObject
{
	Q_OBJECT

public:
	explicit MessageClient(QObject *parent = nullptr);
	~MessageClient();

	/* 连接到管理程序服务器（固定IP） */
	void connectToServer(const QString &host, quint16 port);

	/* 启动UDP自动发现（不固定IP） */
	void startDiscovery(quint16 discoverPort = 9501, quint16 tcpPort = 9500);

	/* 断开连接 */
	void disconnectFromServer();

	/* 发送文字消息 */
	void sendMessage(const QString &text);

	/* 发送语音消息（PCM数据 + 时长秒数） */
	void sendVoiceMessage(const QByteArray &pcmData, int durationSec);

	/* 是否已连接 */
	bool isConnected() const;

signals:
	/* 收到对方发来的文字消息 */
	void messageReceived(const QString &text, const QString &sender);
	/* 收到对方发来的语音消息 */
	void voiceMessageReceived(const QByteArray &pcmData, int duration, const QString &sender);
	/* 连接状态变化 */
	void connectionStateChanged(bool connected);
	/* 连接错误 */
	void connectionError(const QString &error);

private slots:
	/* TCP连接成功 */
	void onConnected();
	/* TCP断开 */
	void onDisconnected();
	/* 收到数据 */
	void onReadyRead();
	/* 连接错误 */
	void onSocketError(QAbstractSocket::SocketError error);
	/* 自动重连定时器 */
	void onReconnectTimer();
	/* UDP发现：发送广播 */
	void sendDiscoveryBroadcast();
	/* UDP发现：收到回复 */
	void onDiscoveryReply();

private:
	QTcpSocket *m_socket;
	QTimer *m_reconnectTimer;
	QString m_host;
	quint16 m_port;
	QString m_recvBuffer;  /* 接收缓冲区（处理粘包） */
	bool m_manualDisconnect; /* 是否手动断开（手动断开不重连） */

	/* UDP自动发现 */
	QUdpSocket *m_discoverSocket;    /* 管理端发现（9501） */
	QUdpSocket *m_discoverSocket2;   /* user_manager发现（9503） */
	QTimer *m_discoverTimer;
	quint16 m_discoverPort;
	quint16 m_tcpPort;
	void startDiscoverListener(); /* 启动发现请求监听（回复别人的发现） */
};

#endif // MESSAGECLIENT_H
