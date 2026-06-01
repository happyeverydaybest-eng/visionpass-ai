/*
 * VisionPass 触摸屏软键盘
 *
 * 功能：在触摸屏上输入英文、数字、常用符号
 * 使用：嵌入到对话框底部，点击按键自动向目标QLineEdit输入字符
 *
 * 用法：
 *   TouchKeyboard *kbd = new TouchKeyboard(this);
 *   kbd->setTargetEdit(m_inputEdit);
 */

#ifndef TOUCHKEYBOARD_H
#define TOUCHKEYBOARD_H

#include <QWidget>
#include <QGridLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QEvent>

class TouchKeyboard : public QWidget
{
	Q_OBJECT

public:
	explicit TouchKeyboard(QWidget *parent = nullptr);
	~TouchKeyboard();

	/* 设置目标输入框（键盘输入的字符会写入这个QLineEdit） */
	void setTargetEdit(QLineEdit *edit);

protected:
	/* 监听输入框焦点事件，自动显示/隐藏键盘 */
	bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
	void onKeyClicked();

private:
	void initLayout();
	QPushButton *createKeyButton(const QString &text, int width = 1);

	QLineEdit *m_targetEdit;   /* 目标输入框 */
	QPushButton *m_shiftButton;
	QPushButton *m_spaceButton;
	bool m_shiftActive;        /* Shift状态 */
};

#endif // TOUCHKEYBOARD_H
