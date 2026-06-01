/*
 * VisionPass 用户管理工具 - 主窗口实现
 *
 * 操作手册对应的4个标签页：
 * 1. 用户管理 - 添加/删除用户（含级联删除人脸和RFID记录）
 * 2. 人脸注册 - 选择照片 → 检测人脸 → 提取特征 → 保存到数据库
 * 3. RFID卡管理 - 手动输入卡号注册/删除
 * 4. 系统密码管理 - 修改系统密码（SHA-256加密，保存到system.json）
 *
 * 数据库使用归一化schema（3个表），与开发板共享。
 */

#include "MainWindow.h"
#include <QApplication>
#include <QFileDialog>
#include <QSqlError>
#include <QCryptographicHash>
#include <QHeaderView>
#include <QProcess>
#include <QDir>
#include <QNetworkInterface>
#include <QThread>
#include <cstring>
#include <opencv2/opencv.hpp>
#include <net.h>
#include <mat.h>

/* ============================================================
 * AddUserDialog 实现
 *
 * 点击"添加用户"按钮后弹出对话框，
 * 填写用户ID、用户名，点击"确定"保存。
 * 密码已改为系统级（所有用户共用），不在此处设置。
 * ============================================================ */

AddUserDialog::AddUserDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("添加用户");
    setMinimumWidth(360);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    /* 表单区域 */
    QFormLayout *form = new QFormLayout();

    m_idEdit = new QLineEdit();
    m_idEdit->setPlaceholderText("例如：user001");
    form->addRow("用户ID:", m_idEdit);

    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("例如：张三");
    form->addRow("用户名:", m_nameEdit);

    mainLayout->addLayout(form);

    /* 确定/取消按钮 */
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText("确定");
    buttonBox->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

QString AddUserDialog::userId() const { return m_idEdit->text().trimmed(); }
QString AddUserDialog::userName() const { return m_nameEdit->text().trimmed(); }

/* ============================================================
 * MainWindow 构造/析构
 * ============================================================ */

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      faceNet(nullptr),
      faceDetector(nullptr),
      modelsLoaded(false),
      m_discoverSocket(nullptr),
      m_discoverTimer(nullptr),
      m_discoverRetry(0)
{
    setupUI();
    connectDatabase();
    initFaceModels();
}

MainWindow::~MainWindow()
{
    if (db.isOpen()) {
        db.close();
    }
    if (faceNet) {
        delete faceNet;
        faceNet = nullptr;
    }
    if (faceDetector) {
        delete faceDetector;
        faceDetector = nullptr;
    }
}

/* ============================================================
 * UI 搭建
 *
 * 主窗口包含4个标签页，对应操作手册中的4个功能模块。
 * ============================================================ */

void MainWindow::setupUI()
{
    setWindowTitle("VisionPass 用户管理工具");
    setMinimumSize(1000, 700);

    /* 状态栏 */
    statusLabel = new QLabel("就绪");
    statusBar()->addWidget(statusLabel, 1);

    /* 搜索开发板按钮 */
    QPushButton *discoverBtn = new QPushButton("搜索开发板");
    discoverBtn->setMaximumWidth(100);
    discoverBtn->setToolTip("UDP广播发现局域网中的开发板");
    connect(discoverBtn, &QPushButton::clicked, this, &MainWindow::discoverBoard);
    statusBar()->addPermanentWidget(discoverBtn);

    /* 同步到开发板按钮 */
    QPushButton *syncBtn = new QPushButton("同步到开发板");
    syncBtn->setMaximumWidth(120);
    syncBtn->setToolTip("自动发现开发板并同步数据库（SCP）");
    connect(syncBtn, &QPushButton::clicked, this, &MainWindow::syncToDevBoard);
    statusBar()->addPermanentWidget(syncBtn);

    /* 标签页 */
    QTabWidget *tabWidget = new QTabWidget(this);
    setCentralWidget(tabWidget);

    QWidget *userTab = new QWidget();
    QWidget *faceTab = new QWidget();
    QWidget *cardTab = new QWidget();
    QWidget *passwordTab = new QWidget();

    setupUserTab(userTab);
    setupFaceTab(faceTab);
    setupCardTab(cardTab);
    setupPasswordTab(passwordTab);

    tabWidget->addTab(userTab, "用户管理");
    tabWidget->addTab(faceTab, "人脸注册");
    tabWidget->addTab(cardTab, "RFID卡管理");
    tabWidget->addTab(passwordTab, "密码管理");
}

/* ============================================================
 * 用户管理标签页
 *
 * 操作手册步骤：
 * - 点击"添加用户"按钮 → 弹出对话框填写信息 → 确定
 * - 在列表中选择用户 → 点击"删除"按钮 → 确认删除
 * - 列表显示：用户ID、用户名、创建时间、操作
 * ============================================================ */

void MainWindow::setupUserTab(QWidget *tab)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(tab);

    /* 用户列表表格 */
    userTable = new QTableWidget(tab);
    userTable->setColumnCount(4);
    userTable->setHorizontalHeaderLabels({"用户ID", "用户名", "创建时间", "操作"});
    userTable->horizontalHeader()->setStretchLastSection(true);
    userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(userTable);

    /* 底部按钮栏 */
    QHBoxLayout *btnLayout = new QHBoxLayout();

    QPushButton *addBtn = new QPushButton("添加用户");
    addBtn->setMinimumWidth(100);
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addUser);
    btnLayout->addWidget(addBtn);

    QPushButton *refreshBtn = new QPushButton("刷新列表");
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshUserList);
    btnLayout->addWidget(refreshBtn);

    btnLayout->addStretch();

    mainLayout->addLayout(btnLayout);
}

/* ============================================================
 * 人脸注册标签页
 *
 * 操作手册步骤：
 * 1. 输入用户ID
 * 2. 点击"选择照片"按钮，选择人脸照片
 * 3. 程序自动检测人脸、裁剪、缩放、提取128维特征
 * 4. 点击"保存特征"保存到数据库
 * 5. 可选择"追加特征"或"覆盖特征"模式
 * ============================================================ */

void MainWindow::setupFaceTab(QWidget *tab)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(tab);

    /* 照片要求说明 */
    QLabel *infoLabel = new QLabel(
        "照片要求：\n"
        "  ✅ 正面照：脸部朝向正前方\n"
        "  ✅ 光线均匀：避免强光或阴影\n"
        "  ✅ 清晰度：分辨率建议 640×480 以上\n"
        "  ✅ 人脸占比：人脸占照片的 1/3 以上\n"
        "  ❌ 避免：侧脸、遮挡、模糊、逆光");
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("QLabel { color: #2c3e50; padding: 8px; "
                             "background-color: #ecf0f1; border-radius: 4px; }");
    mainLayout->addWidget(infoLabel);

    /* 图片预览区域（显示选中的照片和人脸检测框） */
    faceImageLabel = new QLabel(tab);
    faceImageLabel->setFixedSize(640, 480);
    faceImageLabel->setStyleSheet(
        "QLabel { background-color: #2c3e50; border: 2px solid #34495e; }");
    faceImageLabel->setAlignment(Qt::AlignCenter);
    faceImageLabel->setText("选择照片后预览");
    mainLayout->addWidget(faceImageLabel, 0, Qt::AlignCenter);

    /* 操作栏：用户ID + 模式 + 按钮 */
    QHBoxLayout *inputLayout = new QHBoxLayout();

    inputLayout->addWidget(new QLabel("用户ID:"));
    faceUserIdEdit = new QLineEdit();
    faceUserIdEdit->setPlaceholderText("输入用户ID（须先在[用户管理]中添加）");
    faceUserIdEdit->setMaximumWidth(200);
    inputLayout->addWidget(faceUserIdEdit);

    /* 追加/覆盖模式选择 */
    faceModeCombo = new QComboBox();
    faceModeCombo->addItem("追加特征");
    faceModeCombo->addItem("覆盖特征");
    faceModeCombo->setToolTip(
        "追加特征：保留已有特征，添加新特征（建议多角度注册3-5张）\n"
        "覆盖特征：删除已有特征，用新特征替换");
    inputLayout->addWidget(faceModeCombo);

    /* 选择照片按钮 - 第一步：选照片并提取特征 */
    selectPhotoBtn = new QPushButton("选择照片");
    selectPhotoBtn->setMinimumWidth(100);
    selectPhotoBtn->setStyleSheet(
        "QPushButton { background-color: #3498db; color: white; "
        "padding: 6px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #2980b9; }");
    connect(selectPhotoBtn, &QPushButton::clicked, this, &MainWindow::selectPhoto);
    inputLayout->addWidget(selectPhotoBtn);

    /* 保存特征按钮 - 第二步：将提取的特征保存到数据库 */
    saveFeatureBtn = new QPushButton("保存特征");
    saveFeatureBtn->setMinimumWidth(100);
    saveFeatureBtn->setEnabled(false);  /* 选择照片后才启用 */
    saveFeatureBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; "
        "padding: 6px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #219a52; }"
        "QPushButton:disabled { background-color: #95a5a6; }");
    connect(saveFeatureBtn, &QPushButton::clicked, this, &MainWindow::saveFeature);
    inputLayout->addWidget(saveFeatureBtn);

    /* 测试识别按钮 */
    QPushButton *testBtn = new QPushButton("测试识别");
    connect(testBtn, &QPushButton::clicked, this, &MainWindow::testFaceRecognition);
    inputLayout->addWidget(testBtn);

    mainLayout->addLayout(inputLayout);
}

/* ============================================================
 * RFID卡管理标签页
 *
 * 操作手册步骤：
 * - 手动输入卡号（8位十六进制）和用户ID → 点击"注册卡片"
 * - 在列表中选择卡 → 点击"删除"按钮 → 确认删除
 * - 列表显示：卡号、用户ID、用户名、操作
 * ============================================================ */

void MainWindow::setupCardTab(QWidget *tab)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(tab);

    /* RFID卡列表表格 */
    cardTable = new QTableWidget(tab);
    cardTable->setColumnCount(4);
    cardTable->setHorizontalHeaderLabels({"卡号", "用户ID", "用户名", "操作"});
    cardTable->horizontalHeader()->setStretchLastSection(true);
    cardTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(cardTable);

    /* 注册卡表单 */
    QHBoxLayout *formLayout = new QHBoxLayout();

    formLayout->addWidget(new QLabel("卡号:"));
    cardIdEdit = new QLineEdit();
    cardIdEdit->setPlaceholderText("8位十六进制，例如: AABBCCDD");
    cardIdEdit->setMaximumWidth(200);
    formLayout->addWidget(cardIdEdit);

    formLayout->addWidget(new QLabel("用户ID:"));
    cardUserIdEdit = new QLineEdit();
    cardUserIdEdit->setMaximumWidth(150);
    formLayout->addWidget(cardUserIdEdit);

    QPushButton *addCardBtn = new QPushButton("注册卡片");
    addCardBtn->setStyleSheet(
        "QPushButton { background-color: #3498db; color: white; "
        "padding: 6px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #2980b9; }");
    connect(addCardBtn, &QPushButton::clicked, this, &MainWindow::registerCard);
    formLayout->addWidget(addCardBtn);

    QPushButton *refreshCardBtn = new QPushButton("刷新列表");
    connect(refreshCardBtn, &QPushButton::clicked, this, &MainWindow::refreshCardList);
    formLayout->addWidget(refreshCardBtn);

    formLayout->addStretch();

    mainLayout->addLayout(formLayout);
}

/* ============================================================
 * 密码管理标签页
 *
 * 操作手册步骤：
 * 1. 输入用户ID
 * 2. 输入新密码
 * 3. 点击"修改密码"
 * ============================================================ */

void MainWindow::setupPasswordTab(QWidget *tab)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(tab);

    /* 说明文字 */
    QLabel *infoLabel = new QLabel(
        "系统密码管理说明：\n"
        "门禁系统使用一个全局密码进行密码开锁（所有用户共用）。\n"
        "密码将使用 SHA-256 算法加密存储到 system.json 文件中。\n"
        "修改后需要点击[同步到开发板]按钮将配置同步到开发板。");
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("QLabel { color: #2c3e50; padding: 8px; "
                             "background-color: #ecf0f1; border-radius: 4px; }");
    mainLayout->addWidget(infoLabel);

    /* 当前状态显示 */
    sysPasswordStatusLabel = new QLabel("当前状态：检测中...");
    sysPasswordStatusLabel->setStyleSheet("QLabel { color: #16a085; padding: 8px; "
                                          "background-color: #d5f4e6; border-radius: 4px; "
                                          "font-weight: bold; }");
    mainLayout->addWidget(sysPasswordStatusLabel);

    mainLayout->addStretch();

    /* 密码修改表单（居中显示） */
    QVBoxLayout *formLayout = new QVBoxLayout();

    QHBoxLayout *row1 = new QHBoxLayout();
    row1->addWidget(new QLabel("新密码:"));
    sysPasswordEdit = new QLineEdit();
    sysPasswordEdit->setEchoMode(QLineEdit::Password);
    sysPasswordEdit->setPlaceholderText("输入新密码");
    sysPasswordEdit->setMaximumWidth(300);
    row1->addWidget(sysPasswordEdit);
    row1->addStretch();
    formLayout->addLayout(row1);

    QHBoxLayout *row2 = new QHBoxLayout();
    row2->addWidget(new QLabel("确认密码:"));
    sysConfirmPasswordEdit = new QLineEdit();
    sysConfirmPasswordEdit->setEchoMode(QLineEdit::Password);
    sysConfirmPasswordEdit->setPlaceholderText("再次输入新密码");
    sysConfirmPasswordEdit->setMaximumWidth(300);
    row2->addWidget(sysConfirmPasswordEdit);
    row2->addStretch();
    formLayout->addLayout(row2);

    QPushButton *changeBtn = new QPushButton("修改系统密码");
    changeBtn->setMinimumWidth(120);
    changeBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; "
        "padding: 8px 20px; border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background-color: #d35400; }");
    connect(changeBtn, &QPushButton::clicked, this, &MainWindow::changeSystemPassword);
    formLayout->addWidget(changeBtn, 0, Qt::AlignCenter);

    mainLayout->addLayout(formLayout);
    mainLayout->addStretch();
}

/* ============================================================
 * 数据库连接和初始化
 *
 * 数据库路径：程序所在目录/users.db
 * 与开发板共享同一个数据库文件（通过SCP同步）
 *
 * 归一化schema（3个表）：
 * - users: 用户基本信息（id, name, password_hash）
 * - face_features: 人脸特征（支持每用户多个）
 * - rfid_cards: RFID卡片（支持每用户多个）
 * ============================================================ */

void MainWindow::connectDatabase()
{
    db = QSqlDatabase::addDatabase("QSQLITE");
    QString dbPath = QCoreApplication::applicationDirPath() + "/users.db";
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        QMessageBox::critical(this, "数据库错误",
                              "无法打开数据库: " + dbPath + "\n" + db.lastError().text());
        return;
    }

    QSqlQuery query;

    /*
     * 检测旧版schema并自动迁移
     * 旧版users表使用 user_id 列名，新版使用 id 列名
     */
    bool needsMigration = false;
    if (query.exec("PRAGMA table_info(users)")) {
        while (query.next()) {
            if (query.value("name").toString() == "user_id") {
                needsMigration = true;
                break;
            }
        }
    }

    if (needsMigration) {
        statusLabel->setText("检测到旧版数据库，正在迁移...");

        /* 重命名旧表 */
        query.exec("ALTER TABLE users RENAME TO users_old");
        query.exec("ALTER TABLE face_features RENAME TO face_features_old");
        query.exec("ALTER TABLE rfid_cards RENAME TO rfid_cards_old");

        /* 创建新schema */
        query.exec(
            "CREATE TABLE users ("
            "  id TEXT PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  password_hash TEXT,"
            "  created_at TEXT"
            ")");

        query.exec(
            "CREATE TABLE face_features ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  user_id TEXT NOT NULL,"
            "  feature BLOB NOT NULL,"
            "  created_at TEXT,"
            "  FOREIGN KEY(user_id) REFERENCES users(id)"
            ")");

        query.exec(
            "CREATE TABLE rfid_cards ("
            "  card_id TEXT PRIMARY KEY,"
            "  user_id TEXT NOT NULL,"
            "  created_at TEXT,"
            "  FOREIGN KEY(user_id) REFERENCES users(id)"
            ")");

        /* 迁移用户数据（user_id → id） */
        query.exec(
            "INSERT INTO users (id, name, password_hash, created_at) "
            "SELECT user_id, name, password_hash, created_at FROM users_old");
        int userCount = query.numRowsAffected();

        /* 迁移人脸特征 */
        query.exec(
            "INSERT INTO face_features (user_id, feature, created_at) "
            "SELECT user_id, feature, created_at FROM face_features_old");
        int faceCount = query.numRowsAffected();

        /* 迁移RFID卡 */
        query.exec(
            "INSERT INTO rfid_cards (card_id, user_id, created_at) "
            "SELECT card_id, user_id, created_at FROM rfid_cards_old");
        int cardCount = query.numRowsAffected();

        /* 删除旧表 */
        query.exec("DROP TABLE IF EXISTS face_features_old");
        query.exec("DROP TABLE IF EXISTS rfid_cards_old");
        query.exec("DROP TABLE IF EXISTS users_old");

        statusLabel->setText(QString("数据库迁移完成：用户%1个，人脸%2条，卡片%3张")
                             .arg(userCount).arg(faceCount).arg(cardCount));
    } else {
        /* 新建表（如果不存在） */

        /* 用户表（与开发板app/src/database/UserDatabase.cpp保持一致） */
        query.exec(
            "CREATE TABLE IF NOT EXISTS users ("
            "  id TEXT PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  password_hash TEXT,"
            "  created_at TEXT"
            ")");

        /* 人脸特征表（支持每用户多张照片，对应操作手册的"追加特征"功能） */
        query.exec(
            "CREATE TABLE IF NOT EXISTS face_features ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  user_id TEXT NOT NULL,"
            "  feature BLOB NOT NULL,"
            "  photo BLOB,"
            "  created_at TEXT,"
            "  FOREIGN KEY(user_id) REFERENCES users(id)"
            ")");

        /* 兼容旧表：添加photo列（如果不存在） */
        query.exec("ALTER TABLE face_features ADD COLUMN photo BLOB");

        /* RFID卡表（支持每用户多张卡） */
        query.exec(
            "CREATE TABLE IF NOT EXISTS rfid_cards ("
            "  card_id TEXT PRIMARY KEY,"
            "  user_id TEXT NOT NULL,"
            "  created_at TEXT,"
            "  FOREIGN KEY(user_id) REFERENCES users(id)"
            ")");
    }

    /* 更新状态栏显示数据库路径 */
    statusLabel->setText("数据库: " + dbPath);

    refreshUserList();
    refreshCardList();

    /* 加载系统密码状态 */
    QString configPath = QCoreApplication::applicationDirPath() + "/system.json";
    if (QFile::exists(configPath)) {
        QFile configFile(configPath);
        if (configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = configFile.readAll();
            configFile.close();

            /* 简单解析JSON获取密码哈希（检查是否有效） */
            if (content.contains("password_hash")) {
                sysPasswordStatusLabel->setText("当前状态：✅ 系统密码已设置");
                sysPasswordStatusLabel->setStyleSheet("QLabel { color: #27ae60; padding: 8px; "
                                                      "background-color: #d5f4e6; border-radius: 4px; "
                                                      "font-weight: bold; }");
            } else {
                sysPasswordStatusLabel->setText("当前状态：⚠️ 配置文件格式错误");
                sysPasswordStatusLabel->setStyleSheet("QLabel { color: #e67e22; padding: 8px; "
                                                      "background-color: #fef5e7; border-radius: 4px; "
                                                      "font-weight: bold; }");
            }
        }
    } else {
        sysPasswordStatusLabel->setText("当前状态：⚠️ 系统密码未设置（将使用默认密码 123456）");
        sysPasswordStatusLabel->setStyleSheet("QLabel { color: #e67e22; padding: 8px; "
                                              "background-color: #fef5e7; border-radius: 4px; "
                                              "font-weight: bold; }");
    }
}

/* ============================================================
 * 人脸识别模型初始化
 *
 * 加载模型文件：
 * - haarcascade_frontalface_alt2.xml (OpenCV人脸检测)
 * - MobileFaceNet.param + .bin (NCNN人脸识别，128维特征)
 * ============================================================ */

void MainWindow::initFaceModels()
{
    QString modelPath = QCoreApplication::applicationDirPath() + "/model/";
    QString paramFile = modelPath + "MobileFaceNet.param";
    QString binFile = modelPath + "MobileFaceNet.bin";
    QString cascadeFile = modelPath + "haarcascade_frontalface_alt2.xml";

    /* 检查模型文件是否存在 */
    if (!QFile::exists(paramFile) || !QFile::exists(binFile)) {
        QMessageBox::warning(this, "模型文件缺失",
            "请将以下文件复制到 " + modelPath + " 目录:\n"
            "- MobileFaceNet.param\n"
            "- MobileFaceNet.bin\n\n"
            "这些文件可以在开发板的 /opt/visionpass/model/ 目录中找到");
        return;
    }

    if (!QFile::exists(cascadeFile)) {
        QMessageBox::warning(this, "模型文件缺失",
            "请将以下文件复制到 " + modelPath + " 目录:\n"
            "- haarcascade_frontalface_alt2.xml\n\n"
            "这个文件可以在开发板的 /opt/visionpass/model/ 目录中找到");
        return;
    }

    /* 加载OpenCV Haar级联人脸检测器 */
    faceDetector = new cv::CascadeClassifier();
    if (!faceDetector->load(cascadeFile.toStdString())) {
        QMessageBox::critical(this, "错误", "无法加载人脸检测模型");
        delete faceDetector;
        faceDetector = nullptr;
        return;
    }

    /* 加载NCNN MobileFaceNet人脸识别模型 */
    faceNet = new ncnn::Net();
    if (faceNet->load_param(paramFile.toStdString().c_str()) != 0 ||
        faceNet->load_model(binFile.toStdString().c_str()) != 0) {
        QMessageBox::critical(this, "错误", "无法加载人脸识别模型");
        delete faceNet;
        faceNet = nullptr;
        return;
    }

    modelsLoaded = true;
    statusLabel->setText(statusLabel->text() + " | 人脸识别模型已加载");
}

/* ============================================================
 * 用户管理功能
 * ============================================================ */

/*
 * 添加用户
 *
 * 弹出AddUserDialog对话框，填写用户ID/用户名/密码，
 * 密码使用SHA-256哈希后存储到数据库。
 */
void MainWindow::addUser()
{
    AddUserDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString userId = dialog.userId();
    QString userName = dialog.userName();

    if (userId.isEmpty() || userName.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请填写用户ID和用户名");
        return;
    }

    /* 检查用户ID是否已存在 */
    QSqlQuery checkQuery;
    checkQuery.prepare("SELECT id FROM users WHERE id = ?");
    checkQuery.addBindValue(userId);
    checkQuery.exec();
    if (checkQuery.next()) {
        QMessageBox::warning(this, "错误", "用户ID已存在: " + userId);
        return;
    }

    QSqlQuery query;
    query.prepare("INSERT INTO users (id, name, created_at) "
                  "VALUES (?, ?, datetime('now','localtime'))");
    query.addBindValue(userId);
    query.addBindValue(userName);

    if (!query.exec()) {
        QMessageBox::critical(this, "错误",
                              "添加用户失败: " + query.lastError().text());
        return;
    }

    QMessageBox::information(this, "成功",
                             QString("用户添加成功！\n"
                                     "用户ID: %1\n"
                                     "用户名: %2").arg(userId, userName));
    refreshUserList();
}

/*
 * 删除用户
 *
 * 级联删除：同时删除该用户的人脸特征和RFID卡记录。
 * 操作手册说明："删除用户会同时删除该用户的人脸特征和RFID卡记录"
 */
void MainWindow::deleteUser()
{
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QString userId = btn->property("userId").toString();

    if (QMessageBox::question(this, "确认删除",
                              QString("确定要删除用户 \"%1\" 吗？\n\n"
                                      "这将同时删除该用户的：\n"
                                      "• 所有人脸特征记录\n"
                                      "• 所有RFID卡记录").arg(userId),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QSqlQuery query;

    /* 删除关联的人脸特征 */
    query.prepare("DELETE FROM face_features WHERE user_id = ?");
    query.addBindValue(userId);
    query.exec();

    /* 删除关联的RFID卡 */
    query.prepare("DELETE FROM rfid_cards WHERE user_id = ?");
    query.addBindValue(userId);
    query.exec();

    /* 删除用户 */
    query.prepare("DELETE FROM users WHERE id = ?");
    query.addBindValue(userId);

    if (!query.exec()) {
        QMessageBox::critical(this, "错误",
                              "删除用户失败: " + query.lastError().text());
        return;
    }

    QMessageBox::information(this, "成功", "用户删除成功");
    refreshUserList();
    refreshCardList();
}

/*
 * 预览用户的人脸照片
 *
 * 从face_features表查询该用户最新一条记录的photo字段，
 * 解码JPEG并显示在对话框中。
 */
void MainWindow::previewFace()
{
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn)
        return;

    QString userId = btn->property("userId").toString();
    if (userId.isEmpty())
        return;

    /* 查询最新的人脸照片 */
    QSqlQuery query;
    query.prepare("SELECT photo FROM face_features WHERE user_id = ? AND photo IS NOT NULL "
                  "ORDER BY id DESC LIMIT 1");
    query.addBindValue(userId);

    if (!query.exec() || !query.next()) {
        /* 没有照片，也查一下有多少条特征记录 */
        QSqlQuery countQuery;
        countQuery.prepare("SELECT COUNT(*) FROM face_features WHERE user_id = ?");
        countQuery.addBindValue(userId);
        countQuery.exec();
        int count = 0;
        if (countQuery.next())
            count = countQuery.value(0).toInt();

        if (count > 0) {
            QMessageBox::information(this, "预览",
                QString("用户 %1 有 %2 条人脸特征记录，\n"
                        "但没有存储照片（旧版注册的数据）。\n\n"
                        "请重新注册人脸照片以启用预览功能。").arg(userId).arg(count));
        } else {
            QMessageBox::warning(this, "预览",
                QString("用户 %1 还没有人脸特征记录。\n"
                        "请先在[人脸注册]标签页中注册照片。").arg(userId));
        }
        return;
    }

    /* 读取JPEG数据 */
    QByteArray photoBlob = query.value(0).toByteArray();
    QImage photo;
    if (!photo.loadFromData(photoBlob, "JPG")) {
        QMessageBox::warning(this, "预览", "照片数据损坏");
        return;
    }

    /* 查询用户名 */
    QSqlQuery nameQuery;
    nameQuery.prepare("SELECT name FROM users WHERE id = ?");
    nameQuery.addBindValue(userId);
    nameQuery.exec();
    QString userName = userId;
    if (nameQuery.next())
        userName = nameQuery.value(0).toString();

    /* 统计特征数量 */
    QSqlQuery countQuery;
    countQuery.prepare("SELECT COUNT(*) FROM face_features WHERE user_id = ?");
    countQuery.addBindValue(userId);
    countQuery.exec();
    int featureCount = 0;
    if (countQuery.next())
        featureCount = countQuery.value(0).toInt();

    /* 显示预览对话框 */
    QDialog dialog(this);
    dialog.setWindowTitle(QString("人脸预览 - %1 (%2)").arg(userName, userId));
    dialog.setMinimumWidth(300);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *photoLabel = new QLabel();
    QPixmap pixmap = QPixmap::fromImage(photo).scaled(
        QSize(224, 224), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    photoLabel->setPixmap(pixmap);
    photoLabel->setAlignment(Qt::AlignCenter);
    photoLabel->setStyleSheet("border: 2px solid #34495e; border-radius: 8px; padding: 4px;");
    layout->addWidget(photoLabel);

    QLabel *infoLabel = new QLabel(
        QString("用户: %1 (%2)\n已注册特征: %3 条")
            .arg(userName, userId).arg(featureCount));
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setStyleSheet("color: #ecf0f1; font-size: 14px; padding: 8px;");
    layout->addWidget(infoLabel);

    QPushButton *closeBtn = new QPushButton("关闭");
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(closeBtn);

    dialog.setStyleSheet("QDialog { background-color: #2c3e50; }");
    dialog.exec();
}

/*
 * 刷新用户列表
 *
 * 从数据库读取所有用户，显示在表格中。
 * 每行末尾添加"删除"按钮。
 */
void MainWindow::refreshUserList()
{
    userTable->setRowCount(0);

    QSqlQuery query("SELECT id, name, created_at FROM users ORDER BY created_at DESC");

    while (query.next()) {
        int row = userTable->rowCount();
        userTable->insertRow(row);

        userTable->setItem(row, 0, new QTableWidgetItem(query.value(0).toString()));
        userTable->setItem(row, 1, new QTableWidgetItem(query.value(1).toString()));
        userTable->setItem(row, 2, new QTableWidgetItem(query.value(2).toString()));

        /* 操作列：预览 + 删除按钮 */
        QWidget *opWidget = new QWidget();
        QHBoxLayout *opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(2, 2, 2, 2);
        opLayout->setSpacing(4);

        QPushButton *previewBtn = new QPushButton("预览");
        previewBtn->setProperty("userId", query.value(0).toString());
        previewBtn->setStyleSheet(
            "QPushButton { color: #3498db; }"
            "QPushButton:hover { color: #2980b9; font-weight: bold; }");
        connect(previewBtn, &QPushButton::clicked, this, &MainWindow::previewFace);
        opLayout->addWidget(previewBtn);

        QPushButton *deleteBtn = new QPushButton("删除");
        deleteBtn->setProperty("userId", query.value(0).toString());
        deleteBtn->setStyleSheet(
            "QPushButton { color: #e74c3c; }"
            "QPushButton:hover { color: #c0392b; font-weight: bold; }");
        connect(deleteBtn, &QPushButton::clicked, this, &MainWindow::deleteUser);
        opLayout->addWidget(deleteBtn);

        userTable->setCellWidget(row, 3, opWidget);
    }
}

/* ============================================================
 * 人脸注册功能
 *
 * 两步流程（对应操作手册）：
 * 第一步："选择照片" → 打开文件选择器 → 检测人脸 → 提取特征 → 预览
 * 第二步："保存特征" → 将特征保存到数据库（追加或覆盖模式）
 * ============================================================ */

/*
 * 第一步：选择照片并提取人脸特征
 *
 * 操作手册步骤：
 * 1. 输入用户ID
 * 2. 点击"选择照片"按钮
 * 3. 选择照片后程序自动：检测人脸 → 裁剪 → 缩放到112×112 → 提取128维特征
 */
void MainWindow::selectPhoto()
{
    if (!modelsLoaded) {
        QMessageBox::warning(this, "错误",
                             "人脸识别模型未加载，请先检查模型文件");
        return;
    }

    QString userId = faceUserIdEdit->text().trimmed();
    if (userId.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请先输入用户ID");
        return;
    }

    /* 检查用户是否存在 */
    QSqlQuery checkQuery;
    checkQuery.prepare("SELECT id FROM users WHERE id = ?");
    checkQuery.addBindValue(userId);
    checkQuery.exec();
    if (!checkQuery.next()) {
        QMessageBox::warning(this, "错误",
                             "用户ID \"" + userId + "\" 不存在，"
                             "请先在[用户管理]标签页中添加该用户");
        return;
    }

    /* 选择照片文件 */
    QString imagePath = QFileDialog::getOpenFileName(this,
        "选择人脸照片",
        QDir::homePath(),
        "图片文件 (*.jpg *.jpeg *.png *.bmp)");

    if (imagePath.isEmpty()) {
        return;
    }

    /* 加载图片 */
    cv::Mat img = cv::imread(imagePath.toStdString());
    if (img.empty()) {
        QMessageBox::critical(this, "错误", "无法加载图片: " + imagePath);
        return;
    }

    /* 转换为灰度图并直方图均衡化（提高检测率） */
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    /* Haar级联人脸检测 */
    std::vector<cv::Rect> faces;
    faceDetector->detectMultiScale(gray, faces, 1.1, 5, 0, cv::Size(30, 30));

    if (faces.empty()) {
        QMessageBox::warning(this, "检测失败",
                             "图片中未检测到人脸，请更换照片。\n\n"
                             "照片要求：正面照、光线均匀、无遮挡");
        return;
    }

    if (faces.size() > 1) {
        QMessageBox::warning(this, "检测失败",
            QString("检测到 %1 张人脸，请选择只有一张人脸的照片").arg(faces.size()));
        return;
    }

    /* 裁剪人脸区域 */
    cv::Rect faceRect = faces[0];
    cv::Mat faceROI = img(faceRect);

    /* 缩放到MobileFaceNet需要的尺寸 112×112 */
    cv::Mat faceResized;
    cv::resize(faceROI, faceResized, cv::Size(112, 112));

    /* 保存裁剪后的人脸照片为JPEG（用于预览） */
    std::vector<uchar> jpegBuf;
    cv::imencode(".jpg", faceResized, jpegBuf);
    currentPhotoBlob = QByteArray(reinterpret_cast<const char*>(jpegBuf.data()),
				   jpegBuf.size());

    /* 转换为RGB（NCNN需要RGB格式） */
    cv::Mat faceRGB;
    cv::cvtColor(faceResized, faceRGB, cv::COLOR_BGR2RGB);

    /* 转换为NCNN Mat格式 */
    ncnn::Mat in = ncnn::Mat::from_pixels(faceRGB.data, ncnn::Mat::PIXEL_RGB, 112, 112);

    /*
     * 归一化到[-1, 1]范围
     * 与开发板上FaceRecognizer的预处理参数一致
     */
    const float mean_vals[3] = {127.5f, 127.5f, 127.5f};
    const float norm_vals[3] = {1.0f/127.5f, 1.0f/127.5f, 1.0f/127.5f};
    in.substract_mean_normalize(mean_vals, norm_vals);

    /* MobileFaceNet推理，提取128维特征向量 */
    ncnn::Mat out;
    ncnn::Extractor ex = faceNet->create_extractor();
    ex.input("data", in);
    ex.extract("fc1", out);

    /* 将特征向量转换为QByteArray（128×4=512字节） */
    currentFeatureBlob.clear();
    currentFeatureBlob.resize(out.w * sizeof(float));
    memcpy(currentFeatureBlob.data(), out.data, out.w * sizeof(float));
    currentFaceUserId = userId;

    /* 在原图上画人脸检测框（绿色）用于预览 */
    cv::rectangle(img, faceRect, cv::Scalar(0, 255, 0), 2);

    /* 转换OpenCV Mat到QImage用于显示 */
    cv::Mat imgRGB;
    cv::cvtColor(img, imgRGB, cv::COLOR_BGR2RGB);
    QImage qimg(imgRGB.data, imgRGB.cols, imgRGB.rows,
                imgRGB.step, QImage::Format_RGB888);

    /* 缩放显示到预览区域 */
    QPixmap pixmap = QPixmap::fromImage(qimg).scaled(
        faceImageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    faceImageLabel->setPixmap(pixmap);

    /* 启用"保存特征"按钮 */
    saveFeatureBtn->setEnabled(true);

    QMessageBox::information(this, "检测成功",
        QString("人脸特征提取成功！\n\n"
                "用户ID: %1\n"
                "特征维度: %2\n"
                "检测到人脸位置: (%3, %4), 大小: %5×%6\n\n"
                "请点击[保存特征]按钮保存到数据库。").arg(
                    userId).arg(out.w)
                    .arg(faceRect.x).arg(faceRect.y)
                    .arg(faceRect.width).arg(faceRect.height));
}

/*
 * 第二步：保存人脸特征到数据库
 *
 * 操作手册步骤：
 * - "追加特征"模式：保留已有特征，添加新特征（建议多角度注册3-5张）
 * - "覆盖特征"模式：删除已有特征，用新特征替换
 */
void MainWindow::saveFeature()
{
    if (currentFeatureBlob.isEmpty() || currentFaceUserId.isEmpty()) {
        QMessageBox::warning(this, "错误", "请先选择照片提取特征");
        return;
    }

    bool overwrite = (faceModeCombo->currentIndex() == 1);

    if (overwrite) {
        /* 覆盖模式：先删除该用户的所有已有特征 */
        QSqlQuery deleteQuery;
        deleteQuery.prepare("DELETE FROM face_features WHERE user_id = ?");
        deleteQuery.addBindValue(currentFaceUserId);
        deleteQuery.exec();
    }

    /* 插入新特征和照片到face_features表 */
    QSqlQuery insertQuery;
    insertQuery.prepare(
        "INSERT INTO face_features (user_id, feature, photo, created_at) "
        "VALUES (?, ?, ?, datetime('now','localtime'))");
    insertQuery.addBindValue(currentFaceUserId);
    insertQuery.addBindValue(currentFeatureBlob);
    insertQuery.addBindValue(currentPhotoBlob);

    if (!insertQuery.exec()) {
        QMessageBox::critical(this, "错误",
                              "保存人脸特征失败: " + insertQuery.lastError().text());
        return;
    }

    /* 统计该用户当前有多少条特征记录 */
    QSqlQuery countQuery;
    countQuery.prepare("SELECT COUNT(*) FROM face_features WHERE user_id = ?");
    countQuery.addBindValue(currentFaceUserId);
    countQuery.exec();
    int featureCount = 0;
    if (countQuery.next()) {
        featureCount = countQuery.value(0).toInt();
    }

    QString modeStr = overwrite ? "覆盖" : "追加";
    QMessageBox::information(this, "保存成功",
        QString("人脸特征保存成功！\n\n"
                "用户ID: %1\n"
                "模式: %2\n"
                "当前特征数量: %3 条\n\n"
                "%4").arg(
                    currentFaceUserId, modeStr).arg(featureCount)
                    .arg(featureCount < 3
                         ? "建议继续注册更多角度的照片（正面、左侧、右侧）以提高识别率"
                         : "已注册足够多的特征，可以同步数据库到开发板测试"));

    /* 重置状态 */
    currentFeatureBlob.clear();
    currentFaceUserId.clear();
    saveFeatureBtn->setEnabled(false);
}

/*
 * 测试人脸识别（仅提示在开发板上运行）
 */
void MainWindow::testFaceRecognition()
{
    QMessageBox::information(this, "功能说明",
                             "人脸识别测试需要在开发板上运行，\n"
                             "因为需要使用开发板的摄像头。\n\n"
                             "测试步骤：\n"
                             "1. 点击[同步到开发板]按钮同步数据库\n"
                             "2. SSH登录开发板: ssh root@192.168.0.102\n"
                             "3. 重启程序: pkill -9 visionpass && "
                             "/opt/visionpass/bin/visionpass\n"
                             "4. 站到摄像头前观察识别结果");
}

/* ============================================================
 * RFID卡管理功能
 * ============================================================ */

/*
 * 注册RFID卡片
 *
 * 操作手册步骤：手动输入卡号（8位十六进制）和用户ID → 注册卡片
 */
void MainWindow::registerCard()
{
    QString cardId = cardIdEdit->text().trimmed().toUpper();
    QString userId = cardUserIdEdit->text().trimmed();

    if (cardId.isEmpty() || userId.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请填写卡号和用户ID");
        return;
    }

    /* 验证卡号格式（8位十六进制） */
    QRegExp hexPattern("[0-9A-F]{8}");
    if (!hexPattern.exactMatch(cardId)) {
        QMessageBox::warning(this, "格式错误",
                             "卡号必须是8位十六进制字符（0-9, A-F）\n"
                             "例如: AABBCCDD");
        return;
    }

    /* 检查用户是否存在 */
    QSqlQuery checkQuery;
    checkQuery.prepare("SELECT id FROM users WHERE id = ?");
    checkQuery.addBindValue(userId);
    checkQuery.exec();
    if (!checkQuery.next()) {
        QMessageBox::warning(this, "错误",
                             "用户ID \"" + userId + "\" 不存在，"
                             "请先在[用户管理]标签页中添加该用户");
        return;
    }

    /* 检查卡号是否已注册 */
    checkQuery.prepare("SELECT card_id FROM rfid_cards WHERE card_id = ?");
    checkQuery.addBindValue(cardId);
    checkQuery.exec();
    if (checkQuery.next()) {
        QMessageBox::warning(this, "错误",
                             "卡号 " + cardId + " 已被注册");
        return;
    }

    /* 注册卡片 */
    QSqlQuery query;
    query.prepare("INSERT INTO rfid_cards (card_id, user_id, created_at) "
                  "VALUES (?, ?, datetime('now','localtime'))");
    query.addBindValue(cardId);
    query.addBindValue(userId);

    if (!query.exec()) {
        QMessageBox::critical(this, "错误",
                              "注册卡片失败: " + query.lastError().text());
        return;
    }

    QMessageBox::information(this, "成功",
                             QString("RFID卡注册成功！\n"
                                     "卡号: %1\n"
                                     "绑定用户: %2").arg(cardId, userId));
    cardIdEdit->clear();
    cardUserIdEdit->clear();
    refreshCardList();
}

/*
 * 删除RFID卡片
 */
void MainWindow::deleteCard()
{
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QString cardId = btn->property("cardId").toString();

    if (QMessageBox::question(this, "确认删除",
                              QString("确定要删除卡 \"%1\" 吗？").arg(cardId),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QSqlQuery query;
    query.prepare("DELETE FROM rfid_cards WHERE card_id = ?");
    query.addBindValue(cardId);

    if (!query.exec()) {
        QMessageBox::critical(this, "错误",
                              "删除卡失败: " + query.lastError().text());
        return;
    }

    QMessageBox::information(this, "成功", "卡删除成功");
    refreshCardList();
}

/*
 * 刷新RFID卡列表
 *
 * JOIN查询获取卡号、用户ID和用户名。
 */
void MainWindow::refreshCardList()
{
    cardTable->setRowCount(0);

    QSqlQuery query(
        "SELECT r.card_id, r.user_id, u.name, r.created_at "
        "FROM rfid_cards r "
        "LEFT JOIN users u ON r.user_id = u.id "
        "ORDER BY r.created_at DESC");

    while (query.next()) {
        int row = cardTable->rowCount();
        cardTable->insertRow(row);

        cardTable->setItem(row, 0, new QTableWidgetItem(query.value(0).toString()));
        cardTable->setItem(row, 1, new QTableWidgetItem(query.value(1).toString()));
        cardTable->setItem(row, 2, new QTableWidgetItem(query.value(2).toString()));

        /* 删除按钮 */
        QPushButton *deleteBtn = new QPushButton("删除");
        deleteBtn->setProperty("cardId", query.value(0).toString());
        deleteBtn->setStyleSheet(
            "QPushButton { color: #e74c3c; }"
            "QPushButton:hover { color: #c0392b; font-weight: bold; }");
        connect(deleteBtn, &QPushButton::clicked, this, &MainWindow::deleteCard);
        cardTable->setCellWidget(row, 3, deleteBtn);
    }
}

/* ============================================================
 * 密码管理功能
 * ============================================================ */

/*
 * 修改密码
 *
 * 操作手册步骤：
 * 1. 输入用户ID
 * 2. 输入新密码
 * 3. 点击"修改密码"
 */
/* ============================================================
 * 修改系统密码
 *
 * 将新密码的SHA-256哈希保存到 system.json 文件中。
 * 开发板端读取此文件进行密码验证。
 * ============================================================ */

void MainWindow::changeSystemPassword()
{
    QString newPassword = sysPasswordEdit->text();
    QString confirmPassword = sysConfirmPasswordEdit->text();

    if (newPassword.isEmpty()) {
        QMessageBox::warning(this, "输入错误", "请输入新密码");
        return;
    }

    if (newPassword != confirmPassword) {
        QMessageBox::warning(this, "输入错误", "两次输入的密码不一致");
        return;
    }

    /* SHA-256加密新密码 */
    QString passwordHash = QString(QCryptographicHash::hash(
        newPassword.toUtf8(), QCryptographicHash::Sha256).toHex());

    /* 写入 system.json */
    QString configPath = QCoreApplication::applicationDirPath() + "/system.json";
    QFile file(configPath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误",
                              "无法创建配置文件: " + configPath);
        return;
    }

    /* 写入JSON格式 */
    QString jsonContent = QString("{\n    \"password_hash\": \"%1\"\n}\n").arg(passwordHash);
    file.write(jsonContent.toUtf8());
    file.close();

    QMessageBox::information(this, "成功",
                             QString("系统密码修改成功！\n\n"
                                     "配置文件: %1\n\n"
                                     "请点击[同步到开发板]按钮将配置同步到开发板。").arg(configPath));

    sysPasswordEdit->clear();
    sysConfirmPasswordEdit->clear();

    /* 更新状态标签 */
    sysPasswordStatusLabel->setText("当前状态：✅ 系统密码已修改，请同步到开发板");
    sysPasswordStatusLabel->setStyleSheet("QLabel { color: #27ae60; padding: 8px; "
                                          "background-color: #d5f4e6; border-radius: 4px; "
                                          "font-weight: bold; }");
}

/* ============================================================
 * 同步数据库到开发板
 *
 * 使用SCP命令将本地数据库复制到开发板。
 * 操作手册"同步数据库到开发板"章节描述的方法1。
 * ============================================================ */

/*
 * UDP自动发现开发板
 *
 * 广播 "VisionPass_DISCOVER" 到局域网，等待开发板回复。
 * 开发板的MessageClient会监听9501端口并回复IP。
 */
void MainWindow::discoverBoard()
{
    /* 用QInputDialog获取IP */
    bool ok;
    QString ip = QInputDialog::getText(this, "连接开发板",
        "输入开发板IP地址:", QLineEdit::Normal,
        m_boardIp.isEmpty() ? "192.168.0.102" : m_boardIp, &ok);

    if (!ok || ip.trimmed().isEmpty())
        return;

    ip = ip.trimmed();
    statusLabel->setText("正在测试 " + ip + "...");
    statusLabel->setStyleSheet("color: #f39c12;");
    QCoreApplication::processEvents();

    /* 测试SSH连接（SCP同步需要SSH） */
    QProcess sshTest;
    sshTest.start("ssh", QStringList()
        << "-o" << "ConnectTimeout=3"
        << "-o" << "StrictHostKeyChecking=no"
        << "-o" << "HostKeyAlgorithms=+ssh-rsa"
        << "-o" << "PubkeyAcceptedAlgorithms=+ssh-rsa"
        << QString("root@%1").arg(ip)
        << "echo ok");
    sshTest.waitForFinished(5000);

    if (sshTest.exitCode() == 0) {
        m_boardIp = ip;
        statusLabel->setText("已连接开发板: " + m_boardIp);
        statusLabel->setStyleSheet("color: #27ae60;");
        QMessageBox::information(this, "连接成功",
            "已成功连接到开发板 " + ip);
    } else {
        statusLabel->setText("连接失败");
        statusLabel->setStyleSheet("color: #e74c3c;");
        QMessageBox::warning(this, "连接失败",
            "无法SSH连接到 " + ip + "\n\n"
            "请检查:\n1. 开发板是否开机\n"
            "2. IP地址是否正确\n"
            "3. SSH服务是否运行");
    }
}

void MainWindow::onDiscoveryReply()
{
    /* 已改用手动输入IP，此方法保留为空 */
}

/* ============================================================
 * 同步数据库和配置到开发板
 *
 * 使用FTP或SCP将数据库同步到开发板。
 * 自动发现开发板IP，无需手动配置。
 * ============================================================ */

void MainWindow::syncToDevBoard()
{
    QString localDb = QCoreApplication::applicationDirPath() + "/users.db";
    QString localConfig = QCoreApplication::applicationDirPath() + "/system.json";

    if (!QFile::exists(localDb)) {
        QMessageBox::warning(this, "错误", "本地数据库文件不存在: " + localDb);
        return;
    }

    /* 自动发现开发板IP */
    if (m_boardIp.isEmpty()) {
        discoverBoard();
        /* 等待发现（最多20秒） */
        for (int i = 0; i < 40 && m_boardIp.isEmpty(); i++) {
            QCoreApplication::processEvents();
            QThread::msleep(500);
        }
        if (m_boardIp.isEmpty()) {
            QMessageBox::warning(this, "错误",
                                 "未找到开发板，请检查：\n"
                                 "1. 开发板是否开机\n"
                                 "2. 是否在同一局域网\n"
                                 "3. VisionPass是否在运行");
            return;
        }
    }

    bool hasConfig = QFile::exists(localConfig);

    QString syncInfo = QString("同步到开发板 %1？\n\n"
                               "数据库: %2\n")
                               .arg(m_boardIp, localDb);
    if (hasConfig)
        syncInfo += "系统密码: " + localConfig + "\n";
    syncInfo += "\n同步后需要在开发板上重启程序。";

    if (QMessageBox::question(this, "同步到开发板", syncInfo,
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    statusLabel->setText("正在同步...");
    QString target = QString("root@%1").arg(m_boardIp);

    /* 同步数据库 */
    QProcess process;
    process.start("scp", QStringList()
        << "-o" << "StrictHostKeyChecking=no"
        << "-o" << "HostKeyAlgorithms=+ssh-rsa"
        << "-o" << "PubkeyAcceptedKeyTypes=+ssh-rsa"
        << localDb
        << QString("%1:/opt/visionpass/data/users.db").arg(target));
    process.waitForFinished(30000);

    if (process.exitCode() != 0) {
        statusLabel->setText("同步失败");
        QMessageBox::critical(this, "同步失败",
            "数据库同步失败:\n" + process.readAllStandardError());
        return;
    }

    /* 同步配置 */
    if (hasConfig) {
        QProcess mkdirP;
        mkdirP.start("ssh", QStringList()
            << "-o" << "StrictHostKeyChecking=no"
            << "-o" << "HostKeyAlgorithms=+ssh-rsa"
            << target << "mkdir -p /opt/visionpass/config");
        mkdirP.waitForFinished(15000);

        QProcess cfgP;
        cfgP.start("scp", QStringList()
            << "-o" << "StrictHostKeyChecking=no"
            << "-o" << "HostKeyAlgorithms=+ssh-rsa"
            << localConfig
            << QString("%1:/opt/visionpass/config/system.json").arg(target));
        cfgP.waitForFinished(30000);
    }

    statusLabel->setText("同步成功！请在开发板上重启程序。");
    QMessageBox::information(this, "同步成功",
        QString("文件已同步到 %1！\n\n"
                "请在开发板上重启程序。").arg(m_boardIp));
}
