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
 * 创建数据库表（归一化schema，与user_manager工具共享）
 *
 * 3个表：
 * - users: 用户基本信息（id, name, password_hash, created_at）
 * - face_features: 人脸特征向量（支持每用户多张照片）
 * - rfid_cards: RFID卡片（支持每用户多张卡）
 *
 * CREATE TABLE IF NOT EXISTS：如果表已存在则不报错
 * BLOB类型：二进制大对象，用于存储float数组
 */
bool UserDatabase::createTables()
{
	QSqlQuery query(m_db);

	/* 用户基本信息表 */
	bool ok = query.exec(
		"CREATE TABLE IF NOT EXISTS users ("
		"  id TEXT PRIMARY KEY,"
		"  name TEXT NOT NULL,"
		"  password_hash TEXT,"
		"  created_at TEXT"
		")"
	);

	if (!ok) {
		qWarning() << "UserDatabase: CREATE TABLE users failed:" << query.lastError().text();
		emit databaseError("创建用户表失败: " + query.lastError().text());
		return false;
	}

	/* 人脸特征表（支持每用户多条特征，对应多角度注册） */
	ok = query.exec(
		"CREATE TABLE IF NOT EXISTS face_features ("
		"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"  user_id TEXT NOT NULL,"
		"  feature BLOB NOT NULL,"
		"  photo BLOB,"
		"  created_at TEXT,"
		"  FOREIGN KEY(user_id) REFERENCES users(id)"
		")"
	);

	if (!ok) {
		qWarning() << "UserDatabase: CREATE TABLE face_features failed:" << query.lastError().text();
		emit databaseError("创建人脸特征表失败: " + query.lastError().text());
		return false;
	}

	/* RFID卡表（支持每用户多张卡） */
	ok = query.exec(
		"CREATE TABLE IF NOT EXISTS rfid_cards ("
		"  card_id TEXT PRIMARY KEY,"
		"  user_id TEXT NOT NULL,"
		"  created_at TEXT,"
		"  FOREIGN KEY(user_id) REFERENCES users(id)"
		")"
	);

	if (!ok) {
		qWarning() << "UserDatabase: CREATE TABLE rfid_cards failed:" << query.lastError().text();
		emit databaseError("创建RFID卡表失败: " + query.lastError().text());
		return false;
	}

	/*
	 * 创建索引以加速查询
	 * card_id索引：加速RFID卡片查找（避免全表扫描）
	 * password_hash索引：加速密码验证查询
	 */
	query.exec("CREATE INDEX IF NOT EXISTS idx_rfid_card_id ON rfid_cards(card_id)");
	query.exec("CREATE INDEX IF NOT EXISTS idx_password_hash ON users(password_hash)");
	query.exec("CREATE INDEX IF NOT EXISTS idx_face_user_id ON face_features(user_id)");

	/* ===== 旧schema迁移检测 ===== */
	/*
	 * 旧版users表包含 face_feature(BLOB) 和 card_uid(TEXT) 列，
	 * 新版将其拆分到 face_features 和 rfid_cards 表。
	 * 检测旧列是否存在，如果存在则迁移数据。
	 */
	bool hasOldFaceCol = false;
	bool hasOldCardCol = false;

	/* PRAGMA table_info 返回表的列信息 */
	if (query.exec("PRAGMA table_info(users)")) {
		while (query.next()) {
			QString colName = query.value("name").toString();
			if (colName == "face_feature") hasOldFaceCol = true;
			if (colName == "card_uid") hasOldCardCol = true;
		}
	}

	if (hasOldFaceCol || hasOldCardCol) {
		qInfo() << "UserDatabase: Old schema detected, migrating data...";

		/* 迁移旧的人脸特征数据 */
		if (hasOldFaceCol) {
			QSqlQuery migrateQuery(m_db);
			if (migrateQuery.exec(
				"INSERT INTO face_features (user_id, feature, created_at) "
				"SELECT id, face_feature, created_at FROM users "
				"WHERE face_feature IS NOT NULL")) {
				int count = migrateQuery.numRowsAffected();
				qInfo() << "UserDatabase: Migrated" << count << "face features";
			} else {
				qWarning() << "UserDatabase: Face feature migration failed:"
					<< migrateQuery.lastError().text();
			}
		}

		/* 迁移旧的RFID卡数据 */
		if (hasOldCardCol) {
			QSqlQuery migrateQuery(m_db);
			if (migrateQuery.exec(
				"INSERT OR IGNORE INTO rfid_cards (card_id, user_id, created_at) "
				"SELECT card_uid, id, created_at FROM users "
				"WHERE card_uid IS NOT NULL AND card_uid != ''")) {
				int count = migrateQuery.numRowsAffected();
				qInfo() << "UserDatabase: Migrated" << count << "RFID cards";
			} else {
				qWarning() << "UserDatabase: RFID card migration failed:"
					<< migrateQuery.lastError().text();
			}
		}

		/* 创建新users表（不含旧列），替换旧表 */
		m_db.transaction();
		query.exec("ALTER TABLE users RENAME TO users_old");
		query.exec(
			"CREATE TABLE users ("
			"  id TEXT PRIMARY KEY,"
			"  name TEXT NOT NULL,"
			"  password_hash TEXT,"
			"  created_at TEXT"
			")");
		query.exec(
			"INSERT INTO users (id, name, password_hash, created_at) "
			"SELECT id, name, password_hash, created_at FROM users_old");
		query.exec("DROP TABLE users_old");
		m_db.commit();

		/* 重建索引 */
		query.exec("CREATE INDEX IF NOT EXISTS idx_password_hash ON users(password_hash)");

		qInfo() << "UserDatabase: Schema migration complete";
	}

	/* 兼容旧表：添加photo列（如果不存在，静默忽略错误） */
	query.exec("ALTER TABLE face_features ADD COLUMN photo BLOB");

	return true;
}

/* ===== FaceFeature ↔ QByteArray 转换 ===== */

/*
 * 将128维float向量转换为QByteArray（二进制）
 * 128 × 4字节 = 512字节
 * 使用单次bulk memcpy而非逐元素拷贝（性能提升~10倍）
 */
QByteArray UserDatabase::featureToBlob(const FaceFeature &feature)
{
	int bytes = feature.size() * sizeof(float);
	QByteArray blob(bytes, Qt::Uninitialized);
	memcpy(blob.data(), feature.constData(), bytes);
	return blob;
}

/*
 * 将QByteArray（二进制）转换回128维float向量
 * 使用单次bulk memcpy而非逐元素append（性能提升~10倍）
 */
FaceFeature UserDatabase::blobToFeature(const QByteArray &blob)
{
	int count = blob.size() / sizeof(float);
	FaceFeature feature(count, 0.0f);
	memcpy(feature.data(), blob.constData(), count * sizeof(float));
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
		"INSERT INTO users (id, name, password_hash, created_at) "
		"VALUES (:id, :name, :password_hash, :created_at)"
	);
	query.bindValue(":id", id);
	query.bindValue(":name", name);
	query.bindValue(":password_hash", passwordHash);
	query.bindValue(":created_at", QDateTime::currentDateTime().toString(Qt::ISODate));

	if (!query.exec()) {
		qWarning() << "UserDatabase: addUser failed:" << query.lastError().text();
		emit databaseError("添加用户失败: " + query.lastError().text());
		return false;
	}

	/* 如果提供了人脸特征，保存到face_features表 */
	if (!faceFeature.isEmpty()) {
		updateFaceFeature(id, faceFeature);
	}

	/* 如果提供了RFID卡号，保存到rfid_cards表 */
	if (!cardUid.isEmpty()) {
		updateCardUid(id, cardUid);
	}

	qInfo() << "UserDatabase: Added user" << id << name;
	return true;
}

bool UserDatabase::removeUser(const QString &id)
{
	QSqlQuery query(m_db);

	/* 级联删除人脸特征 */
	query.prepare("DELETE FROM face_features WHERE user_id = :id");
	query.bindValue(":id", id);
	query.exec();

	/* 级联删除RFID卡 */
	query.prepare("DELETE FROM rfid_cards WHERE user_id = :id");
	query.bindValue(":id", id);
	query.exec();

	/* 删除用户 */
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
	query.prepare("SELECT id, name, password_hash FROM users WHERE id = :id");
	query.bindValue(":id", id);

	if (query.exec() && query.next()) {
		user.id = query.value("id").toString();
		user.name = query.value("name").toString();
		user.passwordHash = query.value("password_hash").toString();

		/* 从face_features表加载最新的人脸特征 */
		QSqlQuery featureQuery(m_db);
		featureQuery.prepare("SELECT feature FROM face_features WHERE user_id = :id ORDER BY id DESC LIMIT 1");
		featureQuery.bindValue(":id", id);
		if (featureQuery.exec() && featureQuery.next()) {
			user.faceFeature = blobToFeature(featureQuery.value("feature").toByteArray());
		}

		/* 从rfid_cards表加载最新的卡号 */
		QSqlQuery cardQuery(m_db);
		cardQuery.prepare("SELECT card_id FROM rfid_cards WHERE user_id = :id LIMIT 1");
		cardQuery.bindValue(":id", id);
		if (cardQuery.exec() && cardQuery.next()) {
			user.cardUid = cardQuery.value("card_id").toString();
		}
	}

	return user;
}

QVector<UserInfo> UserDatabase::getAllUsers()
{
	QVector<UserInfo> users;

	QSqlQuery query(m_db);
	if (query.exec("SELECT id, name, password_hash FROM users ORDER BY created_at")) {
		while (query.next()) {
			UserInfo user;
			user.id = query.value("id").toString();
			user.name = query.value("name").toString();
			user.passwordHash = query.value("password_hash").toString();

			/* 加载最新人脸特征 */
			QSqlQuery featureQuery(m_db);
			featureQuery.prepare("SELECT feature FROM face_features WHERE user_id = :id ORDER BY id DESC LIMIT 1");
			featureQuery.bindValue(":id", user.id);
			if (featureQuery.exec() && featureQuery.next()) {
				user.faceFeature = blobToFeature(featureQuery.value("feature").toByteArray());
			}

			/* 加载最新RFID卡号 */
			QSqlQuery cardQuery(m_db);
			cardQuery.prepare("SELECT card_id FROM rfid_cards WHERE user_id = :id LIMIT 1");
			cardQuery.bindValue(":id", user.id);
			if (cardQuery.exec() && cardQuery.next()) {
				user.cardUid = cardQuery.value("card_id").toString();
			}

			users.append(user);
		}
	}

	return users;
}

/* ===== 特征更新 ===== */

bool UserDatabase::updateFaceFeature(const QString &id, const FaceFeature &feature)
{
	QSqlQuery query(m_db);
	/* 插入新特征到face_features表（支持多条记录） */
	query.prepare(
		"INSERT INTO face_features (user_id, feature, created_at) "
		"VALUES (:id, :feature, :created_at)"
	);
	query.bindValue(":id", id);
	query.bindValue(":feature", featureToBlob(feature));
	query.bindValue(":created_at", QDateTime::currentDateTime().toString(Qt::ISODate));

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
	/* 插入或替换到rfid_cards表 */
	query.prepare(
		"INSERT OR REPLACE INTO rfid_cards (card_id, user_id, created_at) "
		"VALUES (:card_id, :user_id, :created_at)"
	);
	query.bindValue(":card_id", cardUid);
	query.bindValue(":user_id", id);
	query.bindValue(":created_at", QDateTime::currentDateTime().toString(Qt::ISODate));

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
	query.prepare("SELECT user_id FROM rfid_cards WHERE card_id = :card_uid");
	query.bindValue(":card_uid", cardUid);

	if (query.exec() && query.next()) {
		return query.value("user_id").toString();
	}

	return QString();  /* 未找到 */
}

/*
 * 根据密码哈希查找用户
 * 只查询id, name, password_hash字段，避免加载人脸特征BLOB（节省~500ms）
 */
UserInfo UserDatabase::findUserByPasswordHash(const QString &passwordHash)
{
	UserInfo user;  /* 默认id为空字符串 */

	QSqlQuery query(m_db);
	query.prepare("SELECT id, name, password_hash FROM users WHERE password_hash = :hash LIMIT 1");
	query.bindValue(":hash", passwordHash);

	if (query.exec() && query.next()) {
		user.id = query.value("id").toString();
		user.name = query.value("name").toString();
		user.passwordHash = query.value("password_hash").toString();
		/* 不加载faceFeature和cardUid，节省内存和时间 */
	}

	return user;
}

QMap<QString, FaceFeature> UserDatabase::getAllFaceFeatures()
{
	QMap<QString, FaceFeature> features;

	QSqlQuery query(m_db);
	/* 从face_features表加载特征，与users表JOIN获取user_id */
	if (query.exec("SELECT user_id, feature FROM face_features")) {
		while (query.next()) {
			QString userId = query.value("user_id").toString();
			FaceFeature feature = blobToFeature(query.value("feature").toByteArray());
			if (!feature.isEmpty()) {
				features[userId] = feature;
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