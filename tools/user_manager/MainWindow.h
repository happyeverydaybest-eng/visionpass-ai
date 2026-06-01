/*
 * VisionPass 用户管理工具 - 主窗口
 *
 * 功能说明：
 * PC端用户管理工具，用于管理门禁系统的用户数据。
 * 包含4个标签页：用户管理、人脸注册、RFID卡管理、密码管理。
 * 数据库使用SQLite，与开发板共享同一个数据库文件。
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QComboBox>
#include <QStatusBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QTimer>

/* 前向声明 OpenCV 和 NCNN 类型 */
namespace cv {
    class CascadeClassifier;
}
namespace ncnn {
    class Net;
}

/*
 * 添加用户对话框
 *
 * 点击"添加用户"按钮弹出此对话框，
 * 填写用户ID、用户名后点击"确定"。
 * 注意：密码已改为系统级密码（所有用户共用），不在此处设置。
 */
class AddUserDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddUserDialog(QWidget *parent = nullptr);

    QString userId() const;
    QString userName() const;

private:
    QLineEdit *m_idEdit;
    QLineEdit *m_nameEdit;
};

/*
 * 主窗口类
 *
 * 管理4个标签页的用户界面和数据库操作。
 * 数据库路径：程序所在目录/users.db
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    /* 用户管理 */
    void addUser();          /* 弹出添加用户对话框 */
    void deleteUser();       /* 删除选中的用户（含级联删除） */
    void previewFace();      /* 预览选中用户的人脸照片 */
    void refreshUserList();  /* 刷新用户列表 */

    /* 人脸注册 */
    void selectPhoto();      /* 选择照片并提取人脸特征 */
    void saveFeature();      /* 保存人脸特征到数据库 */
    void testFaceRecognition(); /* 测试识别（仅提示在开发板运行） */

    /* RFID卡管理 */
    void registerCard();     /* 注册RFID卡片 */
    void deleteCard();       /* 删除RFID卡片 */
    void refreshCardList();  /* 刷新卡片列表 */

    /* 系统密码管理 */
    void changeSystemPassword();   /* 修改系统密码 */

    /* 同步 */
    void syncToDevBoard();   /* 同步数据库到开发板 */
    void discoverBoard();    /* UDP发现开发板IP */
    void onDiscoveryReply(); /* 收到开发板回复 */

private:
    /* 初始化各标签页UI */
    void setupUI();
    void setupUserTab(QWidget *tab);
    void setupFaceTab(QWidget *tab);
    void setupCardTab(QWidget *tab);
    void setupPasswordTab(QWidget *tab);

    /* 数据库和模型初始化 */
    void connectDatabase();
    void initFaceModels();

    /* ===== 数据库 ===== */
    QSqlDatabase db;

    /* ===== AI模型 ===== */
    ncnn::Net *faceNet;
    cv::CascadeClassifier *faceDetector;
    bool modelsLoaded;

    /* ===== 当前提取的人脸特征和照片（未保存到数据库） ===== */
    QByteArray currentFeatureBlob;
    QByteArray currentPhotoBlob;    /* 裁剪后的人脸照片JPEG */
    QString currentFaceUserId;

    /* ===== 用户管理标签页 ===== */
    QTableWidget *userTable;

    /* ===== 人脸注册标签页 ===== */
    QLabel *faceImageLabel;
    QLineEdit *faceUserIdEdit;
    QPushButton *selectPhotoBtn;
    QPushButton *saveFeatureBtn;
    QComboBox *faceModeCombo;     /* 追加特征 / 覆盖特征 */

    /* ===== RFID卡管理标签页 ===== */
    QTableWidget *cardTable;
    QLineEdit *cardIdEdit;
    QLineEdit *cardUserIdEdit;

    /* ===== 系统密码标签页 ===== */
    QLineEdit *sysPasswordEdit;        /* 新密码输入 */
    QLineEdit *sysConfirmPasswordEdit; /* 确认密码输入 */
    QLabel *sysPasswordStatusLabel;     /* 当前状态显示 */

    /* ===== 状态栏 ===== */
    QLabel *statusLabel;

    /* ===== UDP自动发现 ===== */
    QUdpSocket *m_discoverSocket;
    QTimer *m_discoverTimer;
    QString m_boardIp;       /* 发现的开发板IP */
    int m_discoverRetry;     /* 发现重试计数 */
};

#endif // MAINWINDOW_H
