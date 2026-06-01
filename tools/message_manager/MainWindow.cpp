/*
 * VisionPass 消息管理程序主窗口实现
 *
 * 支持文字消息和语音消息
 * 录音/播放使用 arecord/aplay 命令行工具
 */

#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QEvent>
#include <QMouseEvent>

static const int REC_MAX_MS = 15000;

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent),
	  m_recordTimer(nullptr),
	  m_recordElapsed(0),
	  m_recording(false),
	  m_recordRunning(false)
{
	setWindowTitle("VisionPass 消息管理");
	setFixedSize(500, 500);

	initLayout();
	initConnections();

	m_recordTimer = new QTimer(this);
	m_recordTimer->setInterval(100);
	connect(m_recordTimer, &QTimer::timeout, this, &MainWindow::onRecordTick);

	m_server = new MessageServer(this);
	if (m_server->start(9500)) {
		m_statusLabel->setText("状态: 监听中 (端口 9500)");
		m_statusLabel->setStyleSheet("color: #27ae60; font-size: 13px;");
	} else {
		m_statusLabel->setText("状态: 启动失败");
		m_statusLabel->setStyleSheet("color: #e74c3c; font-size: 13px;");
	}

	connect(m_server, &MessageServer::messageReceived,
		this, &MainWindow::onMessageReceived);
	connect(m_server, &MessageServer::voiceMessageReceived,
		this, &MainWindow::onVoiceReceived);
	connect(m_server, &MessageServer::clientConnected,
		this, &MainWindow::onClientConnected);
	connect(m_server, &MessageServer::clientDisconnected,
		this, &MainWindow::onClientDisconnected);
}

MainWindow::~MainWindow()
{
	if (m_recording) {
		m_recordProcess->kill();
		m_recordProcess->waitForFinished(2000);
	}
}

void MainWindow::initLayout()
{
	setStyleSheet("QMainWindow { background-color: #2c3e50; }");

	QWidget *central = new QWidget(this);
	setCentralWidget(central);

	QVBoxLayout *mainLayout = new QVBoxLayout(central);
	mainLayout->setContentsMargins(15, 15, 15, 15);
	mainLayout->setSpacing(10);

	QLabel *title = new QLabel("VisionPass 消息管理", this);
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet("color: #ecf0f1; font-size: 20px; font-weight: bold; padding: 5px;");
	mainLayout->addWidget(title);

	m_historyDisplay = new QTextEdit(this);
	m_historyDisplay->setReadOnly(true);
	m_historyDisplay->setStyleSheet(
		"QTextEdit { background-color: #1a252f; border: 2px solid #34495e;"
		"  border-radius: 8px; color: #ecf0f1; font-size: 14px; padding: 8px; }"
	);
	m_historyDisplay->setPlaceholderText("等待门禁端连接...");
	mainLayout->addWidget(m_historyDisplay, 1);

	QHBoxLayout *inputLayout = new QHBoxLayout();
	inputLayout->setSpacing(10);

	m_voiceButton = new QPushButton("按住说话", this);
	m_voiceButton->setFixedSize(90, 40);
	m_voiceButton->setStyleSheet(
		"QPushButton { background-color: #3498db; color: white;"
		"  border: none; border-radius: 8px; font-size: 14px; font-weight: bold; }"
		"QPushButton:pressed { background-color: #e74c3c; }"
	);
	m_voiceButton->installEventFilter(this);

	m_inputEdit = new QLineEdit(this);
	m_inputEdit->setFixedHeight(40);
	m_inputEdit->setPlaceholderText("输入消息...");
	m_inputEdit->setStyleSheet(
		"QLineEdit { background-color: #1a252f; border: 2px solid #34495e;"
		"  border-radius: 8px; color: #ecf0f1; font-size: 14px; padding: 8px; }"
	);

	m_sendButton = new QPushButton("发送", this);
	m_sendButton->setFixedSize(80, 40);
	m_sendButton->setStyleSheet(
		"QPushButton { background-color: #1abc9c; color: white;"
		"  border: none; border-radius: 8px; font-size: 14px; font-weight: bold; }"
		"QPushButton:pressed { background-color: #16a085; }"
	);

	inputLayout->addWidget(m_voiceButton);
	inputLayout->addWidget(m_inputEdit, 1);
	inputLayout->addWidget(m_sendButton);
	mainLayout->addLayout(inputLayout);

	m_statusLabel = new QLabel("状态: 启动中...", this);
	m_statusLabel->setStyleSheet("color: #95a5a6; font-size: 13px;");
	mainLayout->addWidget(m_statusLabel);
}

void MainWindow::initConnections()
{
	connect(m_sendButton, &QPushButton::clicked,
		this, &MainWindow::onSendClicked);
	connect(m_inputEdit, &QLineEdit::returnPressed,
		this, &MainWindow::onSendClicked);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == m_voiceButton) {
		if (event->type() == QEvent::MouseButtonPress && !m_recording) {
			onVoicePressed();
			return true;
		} else if (event->type() == QEvent::MouseButtonRelease && m_recording) {
			onVoiceReleased();
			return true;
		}
	}
	return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onSendClicked()
{
	QString text = m_inputEdit->text().trimmed();
	if (text.isEmpty())
		return;

	if (!m_server->hasClient()) {
		m_historyDisplay->append("<div style='color: #e74c3c;'>[系统] 没有门禁端连接</div>");
		return;
	}

	m_historyDisplay->append(
		QString("<div style='color: #1abc9c; text-align: right;'>"
			"<b>管理端:</b> %1</div>").arg(text));
	m_server->sendMessage(text);
	m_inputEdit->clear();
}

void MainWindow::onVoicePressed()
{
	/* 使用 arecord 录音，输出到临时文件 */
	m_recordFile = "/tmp/vp_voice_record.raw";
	m_recordProcess = new QProcess(this);
	m_recordProcess->start("arecord",
		QStringList() << "-f" << "S16_LE" << "-r" << "16000" << "-c" << "1"
			      << "-t" << "raw" << "-d" << "15" << m_recordFile);

	if (!m_recordProcess->waitForStarted(2000)) {
		qWarning() << "arecord failed to start";
		delete m_recordProcess;
		m_recordProcess = nullptr;
		return;
	}

	m_recording = true;
	m_recordElapsed = 0;

	m_voiceButton->setStyleSheet(
		"QPushButton { background-color: #e74c3c; color: white;"
		"  border: none; border-radius: 8px; font-size: 14px; font-weight: bold; }"
	);
	m_voiceButton->setText("松手发送");
	m_recordTimer->start();
}

void MainWindow::onVoiceReleased()
{
	m_recording = false;
	m_recordTimer->stop();

	if (m_recordProcess) {
		/* 发送SIGINT让arecord优雅停止 */
		m_recordProcess->terminate();
		m_recordProcess->waitForFinished(2000);
		delete m_recordProcess;
		m_recordProcess = nullptr;
	}

	m_voiceButton->setStyleSheet(
		"QPushButton { background-color: #3498db; color: white;"
		"  border: none; border-radius: 8px; font-size: 14px; font-weight: bold; }"
		"QPushButton:pressed { background-color: #e74c3c; }"
	);
	m_voiceButton->setText("按住说话");

	if (m_recordElapsed < 1000) {
		m_historyDisplay->append("<div style='color: #95a5a6;'>录音太短，已取消</div>");
		return;
	}

	if (!m_server->hasClient()) {
		m_historyDisplay->append("<div style='color: #e74c3c;'>[系统] 没有门禁端连接</div>");
		return;
	}

	/* 读取录音文件 */
	QFile file(m_recordFile);
	if (!file.open(QIODevice::ReadOnly)) {
		m_historyDisplay->append("<div style='color: #e74c3c;'>录音文件读取失败</div>");
		return;
	}
	QByteArray audioData = file.readAll();
	file.close();

	/* 软件增益 */
	const float GAIN = 5.0f;
	int16_t *samples = reinterpret_cast<int16_t*>(audioData.data());
	int numSamples = audioData.size() / sizeof(int16_t);
	for (int i = 0; i < numSamples; i++) {
		float s = samples[i] * GAIN;
		if (s > 32767) s = 32767;
		if (s < -32768) s = -32768;
		samples[i] = (int16_t)s;
	}

	int duration = m_recordElapsed / 1000;
	m_historyDisplay->append(
		QString("<div style='color: #e67e22; text-align: right;'>"
			"<b>管理端:</b> [语音 %1秒]</div>").arg(duration));
	m_server->sendVoiceMessage(audioData, duration);

	/* 清理临时文件 */
	QFile::remove(m_recordFile);
}

void MainWindow::onRecordTick()
{
	m_recordElapsed += 100;
	m_voiceButton->setText(QString("录音 %1s").arg(m_recordElapsed / 1000));
	if (m_recordElapsed >= REC_MAX_MS) {
		onVoiceReleased();
	}
}

void MainWindow::onMessageReceived(const QString &text, const QString &from)
{
	QString time = QDateTime::currentDateTime().toString("HH:mm:ss");
	m_historyDisplay->append(
		QString("<div style='color: #3498db;'>"
			"<b>[%1] 门禁端:</b> %2</div>").arg(time, text));
}

void MainWindow::onVoiceReceived(const QByteArray &pcmData, int duration, const QString &from)
{
	Q_UNUSED(from);

	/* 保存到临时文件 */
	QString tmpFile = "/tmp/vp_voice_recv.raw";
	QFile file(tmpFile);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(pcmData);
		file.close();
	} else {
		qWarning() << "Cannot write voice file:" << tmpFile;
		return;
	}

	/* 不做处理，直接写入原始数据 */
	QFile file2(tmpFile);
	if (file2.open(QIODevice::WriteOnly)) {
		file2.write(pcmData);
		file2.close();
	}

	m_historyDisplay->append(
		QString("<div style='color: #e67e22;'>"
			"<b>门禁端:</b> [语音 %1秒] ▶ 播放中...</div>").arg(duration));

	/* 使用QProcess同步播放（阻塞直到播放完成） */
	QProcess playProc;
	playProc.start("aplay",
		QStringList() << "-f" << "S16_LE" << "-r" << "16000" << "-c" << "1"
			      << "-t" << "raw" << tmpFile);
	playProc.waitForFinished(-1);

	if (playProc.exitCode() != 0) {
		qWarning() << "aplay failed:" << playProc.readAllStandardError();
	}
}

void MainWindow::playAudio(const QByteArray &pcmData)
{
	Q_UNUSED(pcmData);
	/* 已在onVoiceReceived中直接调用aplay */
}

void MainWindow::onClientConnected(const QString &address)
{
	m_historyDisplay->append(
		QString("<div style='color: #27ae60;'>[系统] 门禁端已连接: %1</div>").arg(address));
	m_statusLabel->setText("状态: 已连接 " + address);
	m_statusLabel->setStyleSheet("color: #27ae60; font-size: 13px;");
}

void MainWindow::onClientDisconnected(const QString &address)
{
	m_historyDisplay->append(
		QString("<div style='color: #e74c3c;'>[系统] 门禁端已断开: %1</div>").arg(address));
	m_statusLabel->setText("状态: 等待连接...");
	m_statusLabel->setStyleSheet("color: #e67e22; font-size: 13px;");
}
