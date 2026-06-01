/*
 * VisionPass 消息管理程序主窗口
 *
 * 支持文字消息和语音消息（长按录音）
 * 使用 arecord/aplay 命令行工具处理音频
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include "MessageServer.h"

class QProcess;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow();

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
	void onSendClicked();
	void onVoicePressed();
	void onVoiceReleased();
	void onRecordTick();
	void onMessageReceived(const QString &text, const QString &from);
	void onVoiceReceived(const QByteArray &pcmData, int duration, const QString &from);
	void onClientConnected(const QString &address);
	void onClientDisconnected(const QString &address);

private:
	void initLayout();
	void initConnections();
	void playAudio(const QByteArray &pcmData);

	QTextEdit *m_historyDisplay;
	QLineEdit *m_inputEdit;
	QPushButton *m_sendButton;
	QPushButton *m_voiceButton;
	QLabel *m_statusLabel;

	MessageServer *m_server;

	/* 录音相关（使用arecord命令行） */
	QProcess *m_recordProcess;
	QString m_recordFile;
	QTimer *m_recordTimer;
	int m_recordElapsed;
	bool m_recording;
	bool m_recordRunning;
};

#endif // MAINWINDOW_H
