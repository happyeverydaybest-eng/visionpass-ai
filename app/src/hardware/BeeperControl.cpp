/*
 * VisionPass 蜂鸣器控制实现
 *
 * 通过写入sysfs接口控制蜂鸣器：
 *   echo 1 > /sys/class/leds/beep/brightness  → 开启
 *   echo 0 > /sys/class/leds/beep/brightness  → 关闭
 *
 * 如果sysfs路径不存在（驱动未加载），降级为日志输出
 */

#include "BeeperControl.h"
#include <QFile>
#include <QDebug>

/* 蜂鸣器sysfs路径（由内核LED子系统驱动提供） */
static const QString BEEP_PATH = "/sys/class/leds/beep/brightness";

BeeperControl::BeeperControl(QObject *parent)
	: QObject(parent),
	  m_beepTimer(nullptr),
	  m_currentStep(0),
	  m_beepAvailable(false)
{
	/* 检测蜂鸣器设备是否存在 */
	QFile beepFile(BEEP_PATH);
	if (beepFile.exists()) {
		m_beepAvailable = true;
		qInfo() << "BeeperControl: Beeper available at" << BEEP_PATH;
	} else {
		qWarning() << "BeeperControl: Beeper not found at" << BEEP_PATH;
		qWarning() << "  Falling back to log output only";
	}

	/* 创建单次触发定时器（用于蜂鸣时序控制） */
	m_beepTimer = new QTimer(this);
	m_beepTimer->setSingleShot(true);
	connect(m_beepTimer, &QTimer::timeout, this, &BeeperControl::nextStep);
}

BeeperControl::~BeeperControl()
{
	/* 确保蜂鸣器关闭 */
	beepOff();

	if (m_beepTimer) {
		m_beepTimer->stop();
	}
}

void BeeperControl::beepSuccess()
{
	/* 成功反馈：短鸣一声（100ms） */
	QList<int> seq;
	seq << 100;  /* 100ms开 */
	startSequence(seq);
}

void BeeperControl::beepError()
{
	/* 错误反馈：短鸣两声（100ms开 + 100ms关 + 100ms开） */
	QList<int> seq;
	seq << 100 << 100 << 100;  /* 开100ms, 关100ms, 开100ms */
	startSequence(seq);
}

void BeeperControl::beepAlarm()
{
	/* 告警：长鸣（500ms） */
	QList<int> seq;
	seq << 500;  /* 500ms开 */
	startSequence(seq);
}

/*
 * 启动蜂鸣序列
 *
 * 序列是一个整数列表，表示每次操作的持续时间（毫秒）
 * 奇数步骤=开启，偶数步骤=关闭（交替进行）
 *
 * 例如 [100, 100, 100] 表示：
 *   步骤0: 开启100ms
 *   步骤1: 关闭100ms
 *   步骤2: 开启100ms
 */
void BeeperControl::startSequence(const QList<int> &durations)
{
	/* 停止之前的序列（如果有） */
	m_beepTimer->stop();

	m_sequence = durations;
	m_currentStep = 0;

	/* 执行第一步 */
	nextStep();
}

/*
 * 定时器回调：执行序列中的下一步
 */
void BeeperControl::nextStep()
{
	if (m_currentStep >= m_sequence.size()) {
		/* 序列执行完毕，确保蜂鸣器关闭 */
		beepOff();
		return;
	}

	int durationMs = m_sequence[m_currentStep];

	/* 偶数步骤=开启，奇数步骤=关闭 */
	if (m_currentStep % 2 == 0) {
		beepOn();
	} else {
		beepOff();
	}

	m_currentStep++;

	/* 安排下一步 */
	if (m_currentStep < m_sequence.size()) {
		m_beepTimer->start(durationMs);
	} else {
		/* 最后一步：开启后延迟关闭 */
		m_beepTimer->start(durationMs);
	}
}

void BeeperControl::beepOnce(int durationMs)
{
	QList<int> seq;
	seq << durationMs;
	startSequence(seq);
}

void BeeperControl::beepOn()
{
	if (m_beepAvailable) {
		QFile file(BEEP_PATH);
		if (file.open(QIODevice::WriteOnly)) {
			file.write("1");
			file.close();
		}
	}
	qInfo() << "BeeperControl: BEEP ON";
}

void BeeperControl::beepOff()
{
	if (m_beepAvailable) {
		QFile file(BEEP_PATH);
		if (file.open(QIODevice::WriteOnly)) {
			file.write("0");
			file.close();
		}
	}
}