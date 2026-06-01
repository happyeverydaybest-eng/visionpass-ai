/*
 * VisionPass 触摸屏软键盘实现
 *
 * 布局：3行字母键 + 1行数字/符号 + 功能键（Shift/空格/退格/确认）
 * 深色科技风，按键大且间距适中，适合触摸操作
 */

#include "TouchKeyboard.h"
#include <QVBoxLayout>
#include <QDebug>

/* 按键尺寸 */
static const int KEY_HEIGHT = 42;
static const int KEY_FONT_SIZE = 16;

TouchKeyboard::TouchKeyboard(QWidget *parent)
	: QWidget(parent),
	  m_targetEdit(nullptr),
	  m_shiftActive(false)
{
	initLayout();
}

TouchKeyboard::~TouchKeyboard()
{
}

void TouchKeyboard::setTargetEdit(QLineEdit *edit)
{
	/* 移除旧输入框的事件过滤器 */
	if (m_targetEdit) {
		m_targetEdit->removeEventFilter(this);
	}

	m_targetEdit = edit;

	/* 在新输入框上安装事件过滤器，监听焦点事件 */
	if (m_targetEdit) {
		m_targetEdit->installEventFilter(this);
	}
}

/*
 * 事件过滤器：监听目标输入框的焦点事件
 * 获得焦点 → 显示键盘
 * 失去焦点 → 不隐藏（用户可能在点键盘上的按钮）
 */
bool TouchKeyboard::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == m_targetEdit) {
		if (event->type() == QEvent::FocusIn) {
			/* 输入框获得焦点，显示键盘 */
			this->show();
		}
	}
	return QWidget::eventFilter(obj, event);
}

/*
 * 创建按键按钮
 * 参数 text：按键文字
 * 参数 width：按键宽度倍数（1=标准，2=双倍）
 */
QPushButton *TouchKeyboard::createKeyButton(const QString &text, int width)
{
	QPushButton *btn = new QPushButton(text, this);
	btn->setFixedHeight(KEY_HEIGHT);
	btn->setMinimumWidth(40 * width);
	btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	btn->setStyleSheet(
		"QPushButton {"
		"  background-color: #34495e;"
		"  color: #ecf0f1;"
		"  border: 1px solid #2c3e50;"
		"  border-radius: 6px;"
		"  font-size: " + QString::number(KEY_FONT_SIZE) + "px;"
		"  font-weight: bold;"
		"}"
		"QPushButton:pressed {"
		"  background-color: #1abc9c;"
		"}"
	);

	connect(btn, &QPushButton::clicked, this, &TouchKeyboard::onKeyClicked);
	return btn;
}

void TouchKeyboard::initLayout()
{
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->setSpacing(3);

	/* ===== 第1行：数字行 1234567890 ===== */
	QHBoxLayout *row1 = new QHBoxLayout();
	row1->setSpacing(3);
	QStringList nums = {"1","2","3","4","5","6","7","8","9","0"};
	for (const QString &n : nums) {
		row1->addWidget(createKeyButton(n));
	}
	mainLayout->addLayout(row1);

	/* ===== 第2行：QWERTYUIOP ===== */
	QHBoxLayout *row2 = new QHBoxLayout();
	row2->setSpacing(3);
	QStringList row2keys = {"q","w","e","r","t","y","u","i","o","p"};
	for (const QString &k : row2keys) {
		row2->addWidget(createKeyButton(k));
	}
	mainLayout->addLayout(row2);

	/* ===== 第3行：ASDFGHJKL ===== */
	QHBoxLayout *row3 = new QHBoxLayout();
	row3->setSpacing(3);
	row3->addStretch();  /* 左侧缩进 */
	QStringList row3keys = {"a","s","d","f","g","h","j","k","l"};
	for (const QString &k : row3keys) {
		row3->addWidget(createKeyButton(k));
	}
	row3->addStretch();  /* 右侧缩进 */
	mainLayout->addLayout(row3);

	/* ===== 第4行：ZXCVBNM + 退格 ===== */
	QHBoxLayout *row4 = new QHBoxLayout();
	row4->setSpacing(3);

	/* Shift按钮 */
	m_shiftButton = createKeyButton("Shift", 2);
	m_shiftButton->setStyleSheet(
		"QPushButton {"
		"  background-color: #7f8c8d;"
		"  color: #ecf0f1;"
		"  border: 1px solid #2c3e50;"
		"  border-radius: 6px;"
		"  font-size: 14px;"
		"  font-weight: bold;"
		"}"
		"QPushButton:pressed { background-color: #1abc9c; }"
	);
	row4->addWidget(m_shiftButton);

	QStringList row4keys = {"z","x","c","v","b","n","m"};
	for (const QString &k : row4keys) {
		row4->addWidget(createKeyButton(k));
	}

	/* 退格按钮 */
	QPushButton *backspaceBtn = createKeyButton("←", 2);
	backspaceBtn->setStyleSheet(
		"QPushButton {"
		"  background-color: #e74c3c;"
		"  color: white;"
		"  border: 1px solid #c0392b;"
		"  border-radius: 6px;"
		"  font-size: 16px;"
		"  font-weight: bold;"
		"}"
		"QPushButton:pressed { background-color: #c0392b; }"
	);
	row4->addWidget(backspaceBtn);

	mainLayout->addLayout(row4);

	/* ===== 第5行：符号 + 空格 + 确认 ===== */
	QHBoxLayout *row5 = new QHBoxLayout();
	row5->setSpacing(3);

	/* 常用符号 */
	QStringList syms = {".",",","@","-","_","/"};
	for (const QString &s : syms) {
		row5->addWidget(createKeyButton(s));
	}

	/* 空格按钮（占3倍宽度） */
	m_spaceButton = createKeyButton("空格", 3);
	row5->addWidget(m_spaceButton);

	/* 确认按钮（关闭键盘） */
	QPushButton *doneBtn = createKeyButton("完成", 2);
	doneBtn->setStyleSheet(
		"QPushButton {"
		"  background-color: #27ae60;"
		"  color: white;"
		"  border: 1px solid #1e8449;"
		"  border-radius: 6px;"
		"  font-size: 14px;"
		"  font-weight: bold;"
		"}"
		"QPushButton:pressed { background-color: #1e8449; }"
	);
	row5->addWidget(doneBtn);

	mainLayout->addLayout(row5);
}

/*
 * 按键点击处理
 * 通过 sender() 判断是哪个按钮被点击
 */
void TouchKeyboard::onKeyClicked()
{
	if (!m_targetEdit)
		return;

	QPushButton *btn = qobject_cast<QPushButton*>(sender());
	if (!btn)
		return;

	QString key = btn->text();

	/* 功能键处理 */
	if (key == "←") {
		/* 退格：删除最后一个字符 */
		m_targetEdit->backspace();
		return;
	}

	if (key == "空格") {
		m_targetEdit->insert(" ");
		return;
	}

	if (key == "完成") {
		/* 隐藏键盘，并清除输入框焦点 */
		this->hide();
		if (m_targetEdit) {
			m_targetEdit->clearFocus();
		}
		return;
	}

	if (key == "Shift") {
		m_shiftActive = !m_shiftActive;
		/* Shift按钮视觉反馈 */
		if (m_shiftActive) {
			m_shiftButton->setStyleSheet(
				"QPushButton {"
				"  background-color: #1abc9c;"
				"  color: white;"
				"  border: 1px solid #16a085;"
				"  border-radius: 6px;"
				"  font-size: 14px;"
				"  font-weight: bold;"
				"}"
				"QPushButton:pressed { background-color: #16a085; }"
			);
		} else {
			m_shiftButton->setStyleSheet(
				"QPushButton {"
				"  background-color: #7f8c8d;"
				"  color: #ecf0f1;"
				"  border: 1px solid #2c3e50;"
				"  border-radius: 6px;"
				"  font-size: 14px;"
				"  font-weight: bold;"
				"}"
				"QPushButton:pressed { background-color: #1abc9c; }"
			);
		}
		return;
	}

	/* 普通字符：插入到输入框 */
	if (m_shiftActive && key.length() == 1 && key[0].isLetter()) {
		key = key.toUpper();
		m_shiftActive = false;
		/* 重置Shift按钮样式 */
		m_shiftButton->setStyleSheet(
			"QPushButton {"
			"  background-color: #7f8c8d;"
			"  color: #ecf0f1;"
			"  border: 1px solid #2c3e50;"
			"  border-radius: 6px;"
			"  font-size: 14px;"
			"  font-weight: bold;"
			"}"
			"QPushButton:pressed { background-color: #1abc9c; }"
		);
	}

	m_targetEdit->insert(key);
}
