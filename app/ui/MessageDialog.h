/*
 * VisionPass 消息输入对话框
 *
 * 功能：用户输入文字消息并发送给Ubuntu管理程序
 * 布局：消息历史（只读）+ 文字输入 + 发送/取消按钮
 *
 * 使用方式（与PasswordDialog一致）：
 *   MessageDialog *dialog = new MessageDialog(this);
 *   connect(dialog, &MessageDialog::messageToSend, ...);
 *   dialog->exec();
 *   dialog->deleteLater();
 */

#ifndef MESSAGEDIALOG_H
#define MESSAGEDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMap>

class TouchKeyboard;
class VoiceRecordButton;

class MessageDialog : public QDialog
{
	Q_OBJECT

public:
	explicit MessageDialog(QWidget *parent = nullptr);
	~MessageDialog();

	/* 添加一条收到的消息到历史区 */
	void appendReceivedMessage(const QString &text);
	/* 添加一条历史消息（自动判断方向） */
	void appendHistoryMessage(const QString &text);
	/* 添加一条语音消息到历史区 */
	void appendVoiceMessage(const QString &text, int duration);
	/* 添加一条可播放的语音消息（带PCM数据） */
	void appendVoiceWithData(const QByteArray &pcmData, int duration, bool isMine = false);

signals:
	/* 用户点击发送，携带消息文本 */
	void messageToSend(const QString &text);
	/* 用户发送语音消息 */
	void voiceToSend(const QByteArray &pcmData, int durationSec);
	/* 请求播放语音消息 */
	void playVoiceRequested(const QByteArray &pcmData);

private slots:
	void onSendClicked();
	void onCancelClicked();

private:
	void initLayout();
	void initConnections();

	QTextBrowser *m_historyDisplay; /* 消息历史（只读，支持链接点击） */
	QLineEdit *m_inputEdit;        /* 文字输入框 */
	QPushButton *m_sendButton;     /* 发送按钮 */
	QPushButton *m_cancelButton;   /* 取消按钮 */
	VoiceRecordButton *m_voiceButton; /* 语音录音按钮 */
	TouchKeyboard *m_keyboard;     /* 触摸屏软键盘 */

	/* 语音消息存储（ID → PCM数据） */
	QMap<int, QByteArray> m_voiceDataMap;
	int m_voiceIdCounter;
};

#endif // MESSAGEDIALOG_H
