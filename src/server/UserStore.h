/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

class UserStore : public QObject {
	Q_OBJECT
  public:
	struct User {
		QString username;
		QString displayName;
		QByteArray salt;
		QByteArray passwordHash;
		int iterations = 100000;
		QString plaintext; // kept for teacher convenience; classroom threat model
		QString uiMode;    // 答题界面：空串表示跟随全局设置，否则 "online" / "noilinux"
		QString player;    // 关联选手（监考用，暂作占位信息）
	};

	struct GeneratedUser {
		QString username;
		QString displayName;
		QString plaintextPassword;
	};

	explicit UserStore(QObject *parent = nullptr);

	bool loadFromContestDir(const QString &contestDir);
	bool saveToContestDir(const QString &contestDir) const;

	bool verify(const QString &username, const QString &password) const;
	bool exists(const QString &username) const;
	QString displayNameOf(const QString &username) const;
	QString plaintextOf(const QString &username) const;
	QString uiModeOf(const QString &username) const;
	QString playerOf(const QString &username) const;
	QList<QString> allUsernames() const;
	int count() const;

	QList<GeneratedUser> generateBatch(int count, const QString &prefix, int passwordLen = 8);
	bool addUser(const QString &username, const QString &displayName, const QString &plaintextPassword);
	bool removeUser(const QString &username);
	// 编辑账号：可改准考证号（重命名）、姓名、关联选手、答题界面；newUsername 冲突时返回 false。
	bool updateUser(const QString &username, const QString &newUsername, const QString &displayName,
	                const QString &player, const QString &uiMode);
	// 重置密码：重新生成盐与哈希，并更新明文留存。
	bool setPassword(const QString &username, const QString &plaintextPassword);
	void clear();

  private:
	QMap<QString, User> users_;
	mutable QString lastError_;

	static QByteArray makeSalt(int len = 16);
	static QString makePassword(int len);
	static QByteArray pbkdf2(const QString &password, const QByteArray &salt, int iterations);
};
