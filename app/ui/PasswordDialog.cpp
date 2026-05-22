/*
 * VisionPass 密码输入对话框实现
 *
 * 嵌入式ARM开发板（I.MX6ULL, 528MHz）上运行的密码输入对话框。
 * 使用QGridLayout排列数字键盘，QSS实现深色科技风界面。
 *
 * 使用说明（嵌入式Linux初学者）：
 * - QDialog::exec() 以模态方式显示对话框，阻塞父窗口
 * - 用户输入密码后按"确认"，发射 passwordConfirmed 信号
 * - 外部通过 connect() 接收信号获取密码
 */

#include "PasswordDialog.h"
#include <QVBoxLayout>
#include <QFont>

/* ===== 尺寸常量（像素） ===== */
static const int DIALOG_WIDTH = 360;
static const int DIALOG_HEIGHT = 450;
static const int BUTTON_WIDTH = 80;
static const int BUTTON_HEIGHT = 55;
static const int DISPLAY_HEIGHT = 60;

PasswordDialog::PasswordDialog(QWidget *parent)
	: QDialog(parent)
{
	/* 固定对话框大小（嵌入式LCD不支持调整窗口大小） */
	setFixedSize(DIALOG_WIDTH, DIALOG_HEIGHT);
	setWindowTitle("密码开锁");

	/* 去掉窗口装饰（标题栏），在linuxfb下更美观 */
	setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

	/* 模态对话框：阻塞父窗口输入 */
	setModal(true);

	initLayout();
	initConnections();
}

PasswordDialog::~PasswordDialog()
{
}

/* ===== 初始化UI布局 ===== */
void PasswordDialog::initLayout()
{
	/*
	 * 对话框整体背景色：与主窗口一致的深色科技风
	 * #2c3e50 是项目统一的深色背景色
	 */
	setStyleSheet(
		"PasswordDialog { background-color: #2c3e50; }"
	);

	/* 主布局：垂直排列（显示区 + 键盘区 + 取消按钮） */
	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(20, 15, 20, 15);
	mainLayout->setSpacing(10);

	/* ===== 密码显示区 ===== */
	m_displayLabel = new QLabel(this);
	m_displayLabel->setFixedHeight(DISPLAY_HEIGHT);
	m_displayLabel->setAlignment(Qt::AlignCenter);

	/*
	 * 显示区样式：深色背景 + 浅色边框 + 圆角
	 * 与MainWindow的视频区边框风格一致
	 */
	m_displayLabel->setStyleSheet(
		"QLabel {"
		"  background-color: #1a252f;"
		"  border: 2px solid #34495e;"
		"  border-radius: 8px;"
		"  color: #ecf0f1;"
		"  font-size: 28px;"
		"  font-weight: bold;"
		"  letter-spacing: 8px;"
		"}"
	);
	m_displayLabel->setText("请输入密码");

	mainLayout->addWidget(m_displayLabel);

	/* ===== 数字键盘区（QGridLayout，4行3列） ===== */
	QGridLayout *keypadLayout = new QGridLayout();
	keypadLayout->setSpacing(8);

	/*
	 * 键盘布局（4行3列）：
	 *   行0: [7] [8] [9]
	 *   行1: [4] [5] [6]
	 *   行2: [1] [2] [3]
	 *   行3: [0] [DEL] [OK]
	 *
	 * 注意：QGridLayout的行列索引从0开始
	 */

	/* 创建数字按钮1-9并放入网格（前3行） */
	for (int i = 1; i <= 9; i++) {
		int row = (i - 1) / 3;    /* 行：0,0,0,1,1,1,2,2,2 */
		int col = (i - 1) % 3;    /* 列：0,1,2,0,1,2,0,1,2 */
		m_digitButtons[i] = createDigitButton(i);
		keypadLayout->addWidget(m_digitButtons[i], row, col);
	}

	/* 第4行：[0] [DEL] [OK] */
	m_digitButtons[0] = createDigitButton(0);
	keypadLayout->addWidget(m_digitButtons[0], 3, 0);

	/* 删除按钮：橙色背景，与主窗口"刷卡"按钮配色一致 */
	m_deleteButton = createFunctionButton("DEL", "#e67e22");
	keypadLayout->addWidget(m_deleteButton, 3, 1);

	/* 确认按钮：绿色背景，表示"确认/成功" */
	m_confirmButton = createFunctionButton("OK", "#27ae60");
	keypadLayout->addWidget(m_confirmButton, 3, 2);

	mainLayout->addLayout(keypadLayout);

	/* ===== 取消按钮（独占一行，居中显示） ===== */
	m_cancelButton = new QPushButton("取消", this);
	m_cancelButton->setFixedHeight(40);

	/*
	 * 取消按钮样式：红色背景
	 * 红色在UI中通常表示"取消/危险操作"
	 */
	m_cancelButton->setStyleSheet(
		"QPushButton {"
		"  background-color: #c0392b;"
		"  color: white;"
		"  border: none;"
		"  border-radius: 8px;"
		"  font-size: 16px;"
		"  font-weight: bold;"
		"}"
		"QPushButton:pressed {"
		"  background-color: #96281b;"
		"}"
	);

	mainLayout->addWidget(m_cancelButton);
}

/* ===== 初始化信号连接 ===== */
void PasswordDialog::initConnections()
{
	/*
	 * 使用Qt5的新式信号/槽语法（编译期检查类型安全）
	 * 每个数字按钮的 clicked 信号连接到 onDigitClicked 槽
	 *
	 * sender() 机制：在槽函数中通过 sender() 获取发送信号的对象
	 * 这样10个按钮可以共用一个槽函数
	 */
	for (int i = 0; i <= 9; i++) {
		connect(m_digitButtons[i], &QPushButton::clicked,
			this, &PasswordDialog::onDigitClicked);
	}

	connect(m_deleteButton, &QPushButton::clicked,
		this, &PasswordDialog::onDeleteClicked);
	connect(m_confirmButton, &QPushButton::clicked,
		this, &PasswordDialog::onConfirmClicked);
	connect(m_cancelButton, &QPushButton::clicked,
		this, &PasswordDialog::onCancelClicked);
}

/* ===== 数字按钮点击处理 ===== */
void PasswordDialog::onDigitClicked()
{
	/*
	 * 通过 sender() 判断是哪个按钮被点击
	 * 遍历所有数字按钮，找到发送信号的那个
	 */
	int digit = -1;
	for (int i = 0; i <= 9; i++) {
		if (sender() == m_digitButtons[i]) {
			digit = i;
			break;
		}
	}

	if (digit < 0) {
		return;
	}

	/* 输入验证：最大8位数字密码 */
	if (m_password.length() >= MAX_PASSWORD_LENGTH) {
		/* 已达最大长度，忽略输入 */
		return;
	}

	/* 追加数字到密码字符串 */
	m_password.append(QString::number(digit));

	/* 更新显示区（掩码） */
	updateDisplay();
}

/* ===== 删除最后一位 ===== */
void PasswordDialog::onDeleteClicked()
{
	if (!m_password.isEmpty()) {
		/* 移除最后一个字符 */
		m_password.chop(1);
		updateDisplay();
	}
}

/* ===== 确认密码 ===== */
void PasswordDialog::onConfirmClicked()
{
	/* 密码不能为空 */
	if (m_password.isEmpty()) {
		m_displayLabel->setText("请输入密码");
		return;
	}

	/*
	 * 发射信号，携带用户输入的密码明文
	 * 外部通过 connect(passwordDialog, &PasswordDialog::passwordConfirmed, ...)
	 * 接收密码并验证
	 */
	emit passwordConfirmed(m_password);

	/* 清空密码（安全考虑，不在内存中保留密码） */
	m_password.clear();
	updateDisplay();

	/* 关闭对话框（QDialog::accept() 设置结果为Accepted） */
	accept();
}

/* ===== 取消输入 ===== */
void PasswordDialog::onCancelClicked()
{
	/* 清空已输入的密码 */
	m_password.clear();
	updateDisplay();

	/* 关闭对话框（QDialog::reject() 设置结果为Rejected） */
	reject();
}

/* ===== 更新密码显示标签 ===== */
void PasswordDialog::updateDisplay()
{
	if (m_password.isEmpty()) {
		/* 密码为空时显示提示文字 */
		m_displayLabel->setText("请输入密码");
	} else {
		/*
		 * 用星号掩码显示已输入的位数
		 * 例如输入了3位数字，显示 "***"
		 * 这样旁人无法看到实际密码
		 */
		m_displayLabel->setText(QString(m_password.length(), '*'));
	}
}

/* ===== 创建数字按钮（统一样式） ===== */
QPushButton *PasswordDialog::createDigitButton(int digit)
{
	QPushButton *btn = new QPushButton(QString::number(digit), this);
	btn->setFixedSize(BUTTON_WIDTH, BUTTON_HEIGHT);

	/*
	 * 数字按钮样式：
	 * - 深蓝灰色背景（#34495e），与项目整体配色一致
	 * - 白色大号字体，方便触摸操作
	 * - 圆角8px，现代化外观
	 * - pressed状态变暗（#2c3e50），提供触摸反馈
	 */
	btn->setStyleSheet(
		"QPushButton {"
		"  background-color: #34495e;"
		"  color: #ecf0f1;"
		"  border: none;"
		"  border-radius: 8px;"
		"  font-size: 22px;"
		"  font-weight: bold;"
		"}"
		"QPushButton:pressed {"
		"  background-color: #2c3e50;"
		"}"
	);

	return btn;
}

/* ===== 创建功能按钮（统一样式） ===== */
QPushButton *PasswordDialog::createFunctionButton(const QString &text,
						   const QString &bgColor)
{
	QPushButton *btn = new QPushButton(text, this);
	btn->setFixedSize(BUTTON_WIDTH, BUTTON_HEIGHT);

	/*
	 * 功能按钮样式：
	 * - 背景色由参数指定（DEL=橙色，OK=绿色）
	 * - 按下时添加白色边框，提供触摸反馈
	 */
	btn->setStyleSheet(QString(
		"QPushButton {"
		"  background-color: %1;"
		"  color: white;"
		"  border: none;"
		"  border-radius: 8px;"
		"  font-size: 16px;"
		"  font-weight: bold;"
		"}"
		"QPushButton:pressed {"
		"  background-color: %1;"
		"  border: 2px solid #ecf0f1;"
		"}"
	).arg(bgColor));

	return btn;
}