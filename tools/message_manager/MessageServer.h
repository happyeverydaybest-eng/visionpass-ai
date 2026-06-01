/*
 * VisionPass 消息服务器（Ubuntu管理端）
 *
 * 功能：监听TCP端口，接收门禁端连接，收发消息
 * 端口：9500
 *
 * TCP粘包处理：每个客户端维护独立的接收缓冲区
 */

#ifndef MESSAGESERVER_H
#define MESSAGESERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QList>
#include <QMap>

class MessageServer : public QObject
{
	Q_OBJECT

public:
	explicit MessageServer(QObject *parent = nullptr);
	~MessageServer();

	bool start(quint16 port = 9500);
	void stop();
	bool hasClient() const;

	void sendMessage(const QString &text);
	void sendVoiceMessage(const QByteArray &pcmData, int duration);

signals:
	void messageReceived(const QString &text, const QString &from);
	void voiceMessageReceived(const QByteArray &pcmData, int duration, const QString &from);
	void clientConnected(const QString &address);
	void clientDisconnected(const QString &address);
	void serverError(const QString &error);

private slots:
	void onNewConnection();
	void onReadyRead();
	void onClientDisconnected();
	void onDiscoveryRequest();  /* UDP发现请求 */

private:
	QTcpServer *m_server;
	QUdpSocket *m_discoverSocket;  /* UDP发现 */
	QList<QTcpSocket*> m_clients;
	/* 每个客户端的接收缓冲区（处理TCP拆包） */
	QMap<QTcpSocket*, QString> m_recvBuffers;
};

#endif // MESSAGESERVER_H
