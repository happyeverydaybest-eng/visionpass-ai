/*
 * VisionPass 密码输入对话框
 *
 * 360x450 模态对话框，提供数字键盘输入密码。
 * 布局：
 *   [  * * * *  ]  <- 密码显示区（掩码）
 *   [7] [8] [9]
 *   [4] [5] [6]
 *   [1] [2] [3]
 *   [0] [DEL][OK]
 *        [Cancel]
 *
 * 设计原则：
 * - 纯Qt Widgets实现，不使用QML
 * - 深色科技风配色，与主窗口一致（#2c3e50背景）
 * - 最大8位数字密码
 * - 用户按确认后发射 passwordConfirmed 信号
 */

#ifndef PASSWORDDIALOG_H
#define PASSWORDDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>

class PasswordDialog : public QDialog
{
	Q_OBJECT

public:
	explicit PasswordDialog(QWidget *parent = nullptr);
	~PasswordDialog();

signals:
	/* 用户按下确认按钮时发射，携带输入的密码字符串 */
	void passwordConfirmed(const QString &password);

private slots:
	/* 数字按钮0-9点击处理 */
	void onDigitClicked();
	/* 删除最后一位 */
	void onDeleteClicked();
	/* 确认密码 */
	void onConfirmClicked();
	/* 取消输入 */
	void onCancelClicked();

private:
	/* 初始化UI布局和样式 */
	void initLayout();
	/* 初始化信号连接 */
	void initConnections();
	/* 更新密码显示标签（掩码*号） */
	void updateDisplay();
	/* 创建带统一样式的数字按钮 */
	QPushButton *createDigitButton(int digit);
	/* 创建带统一样式的功能按钮 */
	QPushButton *createFunctionButton(const QString &text, const QString &bgColor);

	/* ===== UI元素 ===== */

	/* 密码显示标签（显示掩码*号） */
	QLabel *m_displayLabel;

	/* 数字按钮0-9 */
	QPushButton *m_digitButtons[10];

	/* 功能按钮 */
	QPushButton *m_deleteButton;
	QPushButton *m_confirmButton;
	QPushButton *m_cancelButton;

	/* 当前输入的密码（明文存储，显示时用*掩码） */
	QString m_password;

	/* 最大密码位数 */
	static const int MAX_PASSWORD_LENGTH = 8;
};

#endif // PASSWORDDIALOG_H