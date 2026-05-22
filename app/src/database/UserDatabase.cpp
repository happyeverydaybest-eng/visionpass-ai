/*
 * VisionPass 用户数据库实现
 *
 * 使用Qt SQL模块操作SQLite数据库
 * Qt SQL文档：https://doc.qt.io/qt-5/qtsql-module.html
 */

#include "UserDatabase.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>

UserDatabase::UserDatabase(QObject *parent)
	: QObject(parent), m_opened(false)
{
}

UserDatabase::~UserDatabase()
{
	close();
}

/*
 * 打开数据库并创建表（如果不存在）
 *
 * SQLite数据库是一个文件，Qt使用QSqlDatabase管理连接。
 * addDatabase("QSQLITE")表示使用SQLite驱动。
 */
bool UserDatabase::open(const QString &dbPath)
{
	if (m_opened)
		return true;

	/*
	 * 确保数据库目录存在
	 * QDir::mkpath会递归创建目录（类似mkdir -p）
	 */
	QFileInfo fi(dbPath);
	QDir dir = fi.absoluteDir();
	if (!dir.exists()) {
		if (!dir.mkpath(".")) {
			qWarning() << "UserDatabase: Cannot create directory" << dir.absolutePath();
			emit databaseError("无法创建数据库目录");
			return false;
		}
	}

	/*
	 * 添加SQLite数据库连接
	 * "QSQLITE"是Qt内置的SQLite驱动名称
	 * "visionpass_db"是连接名（用于多数据库场景区分）
	 */
	m_db = QSqlDatabase::addDatabase("QSQLITE", "visionpass_db");
	m_db.setDatabaseName(dbPath);

	if (!m_db.open()) {
		qWarning() << "UserDatabase: Cannot open" << dbPath << ":" << m_db.lastError().text();
		emit databaseError("无法打开数据库: " + m_db.lastError().text());
		return false;
	}

	/* 创建表结构 */
	if (!createTables()) {
		close();
		return false;
	}

	m_opened = true;
	qInfo() << "UserDatabase: Opened" << dbPath;
	return true;
}

void UserDatabase::close()
{
	if (m_db.isOpen()) {
		m_db.close();
	}
	/*
	 * 移除数据库连接（Qt要求先close再removeDatabase）
	 * 必须先销毁所有使用该连接的QSqlQuery对象
	 */
	QSqlDatabase::removeDatabase("visionpass_db");
	m_opened = false;
}

bool UserDatabase::isOpen() const
{
	return m_opened;
}

/*
 * 创建数据库表
 *
 * CREATE TABLE IF NOT EXISTS：如果表已存在则不报错
 * BLOB类型：二进制大对象，用于存储float数组
 */
bool UserDatabase::createTables()
{
	QSqlQuery query(m_db);
	bool ok = query.exec(
		"CREATE TABLE IF NOT EXISTS users ("
		"  id TEXT PRIMARY KEY,"
		"  name TEXT NOT NULL,"
		"  face_feature BLOB,"
		"  card_uid TEXT,"
		"  password_hash TEXT,"
		"  created_at TEXT"
		")"
	);

	if (!ok) {
		qWarning() << "UserDatabase: CREATE TABLE failed:" << query.lastError().text();
		emit databaseError("创建表失败: " + query.lastError().text());
	}

	return ok;
}

/* ===== FaceFeature ↔ QByteArray 转换 ===== */

/*
 * 将128维float向量转换为QByteArray（二进制）
 * 128 × 4字节 = 512字节
 */
QByteArray UserDatabase::featureToBlob(const FaceFeature &feature)
{
	QByteArray blob;
	blob.resize(feature.size() * sizeof(float));
	char *data = blob.data();
	for (int i = 0; i < feature.size(); i++) {
		memcpy(data + i * sizeof(float), &feature[i], sizeof(float));
	}
	return blob;
}

/*
 * 将QByteArray（二进制）转换回128维float向量
 */
FaceFeature UserDatabase::blobToFeature(const QByteArray &blob)
{
	FaceFeature feature;
	int count = blob.size() / sizeof(float);
	const char *data = blob.constData();
	for (int i = 0; i < count; i++) {
		float val;
		memcpy(&val, data + i * sizeof(float), sizeof(float));
		feature.append(val);
	}
	return feature;
}

/* ===== 用户CRUD操作 ===== */

bool UserDatabase::addUser(const QString &id, const QString &name,
			   const FaceFeature &faceFeature,
			   const QString &cardUid,
			   const QString &passwordHash)
{
	QSqlQuery query(m_db);
	query.prepare(
		"INSERT INTO users (id, name, face_feature, card_uid, password_hash, created_at) "
		"VALUES (:id, :name, :face_feature, :card_uid, :password_hash, :created_at)"
	);
	query.bindValue(":id", id);
	query.bindValue(":name", name);
	query.bindValue(":face_feature", featureToBlob(faceFeature));
	query.bindValue(":card_uid", cardUid);
	query.bindValue(":password_hash", passwordHash);
	query.bindValue(":created_at", QDateTime::currentDateTime().toString(Qt::ISODate));

	if (!query.exec()) {
		qWarning() << "UserDatabase: addUser failed:" << query.lastError().text();
		emit databaseError("添加用户失败: " + query.lastError().text());
		return false;
	}

	qInfo() << "UserDatabase: Added user" << id << name;
	return true;
}

bool UserDatabase::removeUser(const QString &id)
{
	QSqlQuery query(m_db);
	query.prepare("DELETE FROM users WHERE id = :id");
	query.bindValue(":id", id);

	if (!query.exec()) {
		qWarning() << "UserDatabase: removeUser failed:" << query.lastError().text();
		return false;
	}

	qInfo() << "UserDatabase: Removed user" << id;
	return true;
}

UserInfo UserDatabase::getUserById(const QString &id)
{
	UserInfo user;

	QSqlQuery query(m_db);
	query.prepare("SELECT * FROM users WHERE id = :id");
	query.bindValue(":id", id);

	if (query.exec() && query.next()) {
		user.id = query.value("id").toString();
		user.name = query.value("name").toString();
		user.faceFeature = blobToFeature(query.value("face_feature").toByteArray());
		user.cardUid = query.value("card_uid").toString();
		user.passwordHash = query.value("password_hash").toString();
	}

	return user;
}

QVector<UserInfo> UserDatabase::getAllUsers()
{
	QVector<UserInfo> users;

	QSqlQuery query(m_db);
	if (query.exec("SELECT * FROM users ORDER BY created_at")) {
		while (query.next()) {
			UserInfo user;
			user.id = query.value("id").toString();
			user.name = query.value("name").toString();
			user.faceFeature = blobToFeature(query.value("face_feature").toByteArray());
			user.cardUid = query.value("card_uid").toString();
			user.passwordHash = query.value("password_hash").toString();
			users.append(user);
		}
	}

	return users;
}

/* ===== 特征更新 ===== */

bool UserDatabase::updateFaceFeature(const QString &id, const FaceFeature &feature)
{
	QSqlQuery query(m_db);
	query.prepare("UPDATE users SET face_feature = :feature WHERE id = :id");
	query.bindValue(":feature", featureToBlob(feature));
	query.bindValue(":id", id);

	if (!query.exec()) {
		qWarning() << "UserDatabase: updateFaceFeature failed:" << query.lastError().text();
		return false;
	}

	qInfo() << "UserDatabase: Updated face feature for" << id;
	return true;
}

bool UserDatabase::updateCardUid(const QString &id, const QString &cardUid)
{
	QSqlQuery query(m_db);
	query.prepare("UPDATE users SET card_uid = :card_uid WHERE id = :id");
	query.bindValue(":card_uid", cardUid);
	query.bindValue(":id", id);

	if (!query.exec()) {
		qWarning() << "UserDatabase: updateCardUid failed:" << query.lastError().text();
		return false;
	}

	qInfo() << "UserDatabase: Updated card UID for" << id;
	return true;
}

bool UserDatabase::updatePassword(const QString &id, const QString &passwordHash)
{
	QSqlQuery query(m_db);
	query.prepare("UPDATE users SET password_hash = :hash WHERE id = :id");
	query.bindValue(":hash", passwordHash);
	query.bindValue(":id", id);

	if (!query.exec()) {
		qWarning() << "UserDatabase: updatePassword failed:" << query.lastError().text();
		return false;
	}

	return true;
}

/* ===== 查询匹配 ===== */

QString UserDatabase::findUserByCardUid(const QString &cardUid)
{
	QSqlQuery query(m_db);
	query.prepare("SELECT id FROM users WHERE card_uid = :card_uid");
	query.bindValue(":card_uid", cardUid);

	if (query.exec() && query.next()) {
		return query.value("id").toString();
	}

	return QString();  /* 未找到 */
}

QMap<QString, FaceFeature> UserDatabase::getAllFaceFeatures()
{
	QMap<QString, FaceFeature> features;

	QSqlQuery query(m_db);
	if (query.exec("SELECT id, face_feature FROM users WHERE face_feature IS NOT NULL")) {
		while (query.next()) {
			QString id = query.value("id").toString();
			FaceFeature feature = blobToFeature(query.value("face_feature").toByteArray());
			if (!feature.isEmpty()) {
				features[id] = feature;
			}
		}
	}

	return features;
}

QMap<QString, QString> UserDatabase::getAllUserNames()
{
	QMap<QString, QString> names;

	QSqlQuery query(m_db);
	if (query.exec("SELECT id, name FROM users")) {
		while (query.next()) {
			names[query.value("id").toString()] = query.value("name").toString();
		}
	}

	return names;
}

/*
 * 验证密码：计算输入的SHA-256并与存储的哈希比对
 */
bool UserDatabase::verifyPassword(const QString &inputPassword, const QString &storedHash)
{
	return hashPassword(inputPassword) == storedHash;
}

/*
 * 计算密码的SHA-256哈希
 *
 * SHA-256是一种单向哈希函数：
 * - 输入任意长度字符串 → 输出64字符hex字符串
 * - 相同输入总是产生相同输出
 * - 无法从哈希值反推原始密码
 *
 * 使用Qt的QCryptographicHash类，无需外部依赖
 */
QString UserDatabase::hashPassword(const QString &password)
{
	QByteArray hash = QCryptographicHash::hash(
		password.toUtf8(), QCryptographicHash::Sha256);
	return hash.toHex();  /* 转为64字符hex字符串 */
}