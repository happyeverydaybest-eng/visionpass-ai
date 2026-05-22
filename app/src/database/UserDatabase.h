/*
 * VisionPass 用户数据库
 *
 * 功能说明（初学者必读）：
 * =========================
 * 使用SQLite存储门禁系统的用户数据，包括：
 * - 用户基本信息（ID、姓名）
 * - 人脸特征向量（128维float，来自NCNN MobileFaceNet）
 * - RFID卡片UID（4字节hex字符串，如"AABBCCDD"）
 * - 密码（SHA-256哈希存储）
 *
 * 数据库结构：
 * =========================
 * 表 users:
 *   id          TEXT PRIMARY KEY  -- 用户唯一ID（如 "user_001"）
 *   name        TEXT NOT NULL     -- 用户姓名
 *   face_feature BLOB             -- 人脸特征（128×4=512字节）
 *   card_uid    TEXT              -- RFID卡片UID
 *   password_hash TEXT            -- 密码SHA-256哈希
 *   created_at  TEXT              -- 创建时间
 *
 * 为什么用SQLite而不是文件？
 * =========================
 * - SQLite是Qt自带的数据库引擎，无需额外依赖
 * - 支持SQL查询，比JSON文件更灵活
 * - 适合嵌入式场景（单文件，无需数据库服务器）
 */

#ifndef USERDATABASE_H
#define USERDATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <QMap>
#include "src/face/FaceRecognizer.h"  /* FaceFeature typedef */

/*
 * 用户信息结构体
 */
struct UserInfo {
	QString id;           /* 用户唯一ID */
	QString name;         /* 用户姓名 */
	FaceFeature faceFeature;  /* 人脸特征向量（128维float） */
	QString cardUid;      /* RFID卡片UID */
	QString passwordHash; /* 密码SHA-256哈希 */
};

/*
 * 用户数据库管理类
 *
 * 使用方法：
 *   UserDatabase db;
 *   if (db.open()) {
 *       db.addUser("user_001", "张三", feature, "AABBCCDD", "");
 *       UserInfo user = db.getUserById("user_001");
 *       db.close();
 *   }
 */
class UserDatabase : public QObject
{
	Q_OBJECT

public:
	explicit UserDatabase(QObject *parent = nullptr);
	~UserDatabase();

	/*
	 * 打开数据库
	 * 参数 dbPath：数据库文件路径（默认 /opt/visionpass/data/users.db）
	 * 返回值：true=成功
	 */
	bool open(const QString &dbPath = "/opt/visionpass/data/users.db");

	/* 关闭数据库 */
	void close();

	/* 数据库是否已打开 */
	bool isOpen() const;

	/* ===== 用户CRUD操作 ===== */

	/*
	 * 添加用户
	 * 返回值：true=成功，false=用户ID已存在或数据库错误
	 */
	bool addUser(const QString &id, const QString &name,
		     const FaceFeature &faceFeature = FaceFeature(),
		     const QString &cardUid = QString(),
		     const QString &passwordHash = QString());

	/*
	 * 删除用户
	 * 返回值：true=成功
	 */
	bool removeUser(const QString &id);

	/*
	 * 获取用户信息
	 * 返回值：用户信息结构体（如果不存在，id为空字符串）
	 */
	UserInfo getUserById(const QString &id);

	/*
	 * 获取所有用户列表
	 */
	QVector<UserInfo> getAllUsers();

	/* ===== 特征更新 ===== */

	/*
	 * 更新用户的人脸特征
	 * 返回值：true=成功
	 */
	bool updateFaceFeature(const QString &id, const FaceFeature &feature);

	/*
	 * 更新用户的RFID卡片UID
	 * 返回值：true=成功
	 */
	bool updateCardUid(const QString &id, const QString &cardUid);

	/*
	 * 更新用户的密码哈希
	 * 返回值：true=成功
	 */
	bool updatePassword(const QString &id, const QString &passwordHash);

	/* ===== 查询匹配 ===== */

	/*
	 * 根据RFID卡片UID查找用户
	 * 返回值：用户ID（如果未找到，返回空字符串）
	 */
	QString findUserByCardUid(const QString &cardUid);

	/*
	 * 根据密码哈希查找用户（用于密码验证）
	 * 返回值：用户信息结构体（如果未找到，id为空字符串）
	 * 注意：此方法只查询id, name, password_hash字段，不加载人脸特征BLOB
	 */
	UserInfo findUserByPasswordHash(const QString &passwordHash);

	/*
	 * 获取所有已注册人脸特征的用户（用于人脸识别比对）
	 * 返回值：map<用户ID, 特征向量>
	 */
	QMap<QString, FaceFeature> getAllFaceFeatures();

	/*
	 * 获取所有用户的姓名映射（用于UI显示）
	 * 返回值：map<用户ID, 用户姓名>
	 */
	QMap<QString, QString> getAllUserNames();

	/*
	 * 验证密码
	 * 返回值：true=密码正确
	 */
	bool verifyPassword(const QString &inputPassword, const QString &storedHash);

	/*
	 * 计算密码的SHA-256哈希
	 * 参数 password：明文密码
	 * 返回值：64字符hex字符串
	 */
	static QString hashPassword(const QString &password);

signals:
	/* 数据库错误 */
	void databaseError(const QString &error);

private:
	/* 创建数据库表（如果不存在） */
	bool createTables();

	/* FaceFeature ↔ QByteArray 转换 */
	static QByteArray featureToBlob(const FaceFeature &feature);
	static FaceFeature blobToFeature(const QByteArray &blob);

	QSqlDatabase m_db;
	bool m_opened;
};

#endif // USERDATABASE_H