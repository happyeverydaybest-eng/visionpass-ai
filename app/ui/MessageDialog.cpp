/*
 * VisionPass 消息输入对话框实现
 *
 * 全屏对话框（1024x600 LCD）
 * 上半部分：消息历史 + 输入行（取消 + 输入框 + 语音按钮 + 发送）
 * 下半部分：触摸屏软键盘
 */

#include "MessageDialog.h"
#include "TouchKeyboard.h"
#include "VoiceRecordButton.h"
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QUrl>

static const int DIALOG_WIDTH = 1014;
static const int DIALOG_HEIGHT = 590;

MessageDialog::MessageDialog(QWidget *parent)
	: QDialog(parent),
	  m_voiceIdCounter(0)
{
	setFixedSize(DIALOG_WIDTH, DIALOG_HEIGHT);
	setWindowTitle("发送消息");
	setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
	setModal(true);

	initLayout();
	initConnections();
}

MessageDialog::~MessageDialog()
{
}

void MessageDialog::initLayout()
{
	setStyleSheet("MessageDialog { background-color: #2c3e50; }");

	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(10, 10, 10, 10);
	mainLayout->setSpacing(6);

	/* ===== 消息历史（QTextBrowser支持链接点击） ===== */
	m_historyDisplay = new QTextBrowser(this);
	m_historyDisplay->setReadOnly(true);
	m_historyDisplay->setMinimumHeight(100);
	m_historyDisplay->setOpenExternalLinks(false);
	m_historyDisplay->setStyleSheet(
		"QTextBrowser {"
		"  background-color: #1a252f;"
		"  border: 2px solid #34495e;"
		"  border-radius: 8px;"
		"  color: #ecf0f1;"
		"  font-size: 14px;"
		"  padding: 8px;"
		"}"
	);
	m_historyDisplay->setPlaceholderText("暂无消息记录");
	connect(m_historyDisplay, &QTextBrowser::anchorClicked,
		this, [this](const QUrl &url) {
			/* 处理语音播放链接：vpvoice://ID */
			if (url.scheme() == "vpvoice") {
				int id = url.host().toInt();
				if (m_voiceDataMap.contains(id))
					emit playVoiceRequested(m_voiceDataMap[id]);
			}
		});
	mainLayout->addWidget(m_historyDisplay, 1);

	/* ===== 输入行：取消 + 语音按钮 + 输入框 + 发送 ===== */
	QHBoxLayout *inputRow = new QHBoxLayout();
	inputRow->setSpacing(8);

	m_cancelButton = new QPushButton("取消", this);
	m_cancelButton->setFixedSize(70, 40);
	m_cancelButton->setStyleSheet(
		"QPushButton { background-color: #c0392b; color: white;"
		"  border: none; border-radius: 6px; font-size: 14px; font-weight: bold; }"
		"QPushButton:pressed { background-color: #96281b; }"
	);

	/* 语音录音按钮（长按录音，松手发送） */
	m_voiceButton = new VoiceRecordButton(this);

	m_inputEdit = new QLineEdit(this);
	m_inputEdit->setFixedHeight(40);
	m_inputEdit->setPlaceholderText("点击输入框后用下方键盘输入...");
	m_inputEdit->setStyleSheet(
		"QLineEdit { background-color: #1a252f; border: 2px solid #34495e;"
		"  border-radius: 6px; color: #ecf0f1; font-size: 16px; padding: 6px 10px; }"
	);

	m_sendButton = new QPushButton("发送", this);
	m_sendButton->setFixedSize(70, 40);
	m_sendButton->setStyleSheet(
		"QPushButton { background-color: #1abc9c; color: white;"
		"  border: none; border-radius: 6px; font-size: 14px; font-weight: bold; }"
		"QPushButton:pressed { background-color: #16a085; }"
	);

	inputRow->addWidget(m_cancelButton);
	inputRow->addWidget(m_voiceButton);
	inputRow->addWidget(m_inputEdit, 1);
	inputRow->addWidget(m_sendButton);
	mainLayout->addLayout(inputRow);

	/* ===== 触摸键盘 ===== */
	m_keyboard = new TouchKeyboard(this);
	m_keyboard->setTargetEdit(m_inputEdit);
	m_keyboard->setFixedHeight(230);
	mainLayout->addWidget(m_keyboard);
}

void MessageDialog::initConnections()
{
	connect(m_sendButton, &QPushButton::clicked,
		this, &MessageDialog::onSendClicked);
	connect(m_cancelButton, &QPushButton::clicked,
		this, &MessageDialog::onCancelClicked);

	/* 语音录音完成 → 发射语音信号 */
	connect(m_voiceButton, &VoiceRecordButton::voiceRecorded,
		this, [this](const QByteArray &audioData, int durationSec) {
			m_historyDisplay->append(
				QString("<div style='color: #e67e22; text-align: right;'>"
					"<b>我:</b> [语音 %1秒]</div>").arg(durationSec));
			emit voiceToSend(audioData, durationSec);
		});
	connect(m_voiceButton, &VoiceRecordButton::recordingCancelled,
		this, [this]() {
			m_historyDisplay->append(
				"<div style='color: #95a5a6;'>录音太短，已取消</div>");
		});
}

void MessageDialog::onSendClicked()
{
	QString text = m_inputEdit->text().trimmed();
	if (text.isEmpty())
		return;

	m_historyDisplay->append(
		QString("<div style='color: #1abc9c; text-align: right;'>"
			"<b>我:</b> %1</div>").arg(text));
	emit messageToSend(text);
	m_inputEdit->clear();
}

void MessageDialog::onCancelClicked()
{
	reject();
}

void MessageDialog::appendReceivedMessage(const QString &text)
{
	m_historyDisplay->append(
		QString("<div style='color: #3498db;'>"
			"<b>管理端:</b> %1</div>").arg(text));
}

void MessageDialog::appendHistoryMessage(const QString &text)
{
	if (text.startsWith("[我]")) {
		QString content = text.mid(4);
		m_historyDisplay->append(
			QString("<div style='color: #1abc9c; text-align: right;'>"
				"<b>我:</b> %1</div>").arg(content));
	} else if (text.startsWith("[管理端]")) {
		QString content = text.mid(5);
		m_historyDisplay->append(
			QString("<div style='color: #3498db;'>"
				"<b>管理端:</b> %1</div>").arg(content));
	} else {
		m_historyDisplay->append(
			QString("<div style='color: #95a5a6;'>%1</div>").arg(text));
	}
}

void MessageDialog::appendVoiceMessage(const QString &sender, int duration)
{
	QString color = (sender == "doorlock") ? "#e67e22" : "#3498db";
	QString label = (sender == "doorlock") ? "我" : "管理端";
	/* 语音消息不在此处显示，由appendVoiceWithData调用 */
	Q_UNUSED(duration);
	Q_UNUSED(sender);
	Q_UNUSED(color);
	Q_UNUSED(label);
}

/*
 * 添加语音消息（带数据，可点击播放）
 */
void MessageDialog::appendVoiceWithData(const QByteArray &pcmData, int duration, bool isMine)
{
	/* 存储语音数据（供将来重播功能使用） */
	int id = m_voiceIdCounter++;
	m_voiceDataMap[id] = pcmData;

	QString color = isMine ? "#e67e22" : "#3498db";
	QString label = isMine ? "我" : "管理端";
	QString align = isMine ? "text-align: right;" : "";

	m_historyDisplay->append(
		QString("<div style='color: %1; %2'>"
			"<b>%3:</b> 🎤 语音 %4秒</div>")
			.arg(color, align, label).arg(duration));
}
