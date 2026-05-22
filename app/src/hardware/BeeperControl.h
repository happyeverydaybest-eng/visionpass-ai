/*
 * VisionPass 蜂鸣器控制
 *
 * 通过写入 /sys/class/leds/beep/brightness 控制板载蜂鸣器
 * 提供三种反馈模式：
 * - beepSuccess(): 短鸣一声（100ms），表示操作成功
 * - beepError(): 短鸣两声（100ms+100ms间隔+100ms），表示操作失败
 * - beepAlarm(): 长鸣（500ms），表示安全告警
 *
 * 使用QTimer非阻塞方式控制时序，不影响主线程事件循环
 */

#ifndef BEEPERCONTROL_H
#define BEEPERCONTROL_H

#include <QObject>
#include <QTimer>
#include <QList>
#include <QFile>

class BeeperControl : public QObject
{
	Q_OBJECT

public:
	explicit BeeperControl(QObject *parent = nullptr);
	~BeeperControl();

	/* 短鸣一声（成功反馈） */
	void beepSuccess();
	/* 短鸣两声（错误反馈） */
	void beepError();
	/* 长鸣（告警） */
	void beepAlarm();

private slots:
	/* 定时器回调：执行蜂鸣序列中的下一步 */
	void nextStep();

private:
	/* 单次蜂鸣（durationMs毫秒后自动关闭） */
	void beepOnce(int durationMs);
	/* 开启蜂鸣器 */
	void beepOn();
	/* 关闭蜂鸣器 */
	void beepOff();
	/* 启动蜂鸣序列 */
	void startSequence(const QList<int> &durations);

	QTimer *m_beepTimer;        /* 蜂鸣时序定时器 */
	QList<int> m_sequence;      /* 当前蜂鸣序列（毫秒） */
	int m_currentStep;          /* 当前步骤索引 */
	bool m_beepAvailable;       /* 蜂鸣器设备是否可用 */
	QFile *m_beepFile;          /* sysfs文件句柄（保持打开以避免重复open/close） */
};

#endif // BEEPERCONTROL_H