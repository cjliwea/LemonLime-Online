/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class SessionManager : public QObject {
	Q_OBJECT
  public:
	explicit SessionManager(QObject *parent = nullptr);

	QString createSession(const QString &username);
	bool validate(const QString &token, QString *username = nullptr);
	void destroy(const QString &token);
	void destroyAllForUser(const QString &username);

	void setTtl(int seconds) { ttlSeconds_ = seconds; }

	// ---- 在线状态（登录会话数，按用户名去重） ----
	int onlineCount();
	QStringList onlineUsernames();
	// 某用户最近一次请求时间；从未见过则返回无效 QDateTime
	QDateTime lastSeenOf(const QString &username) const;
	// 某用户最近一次登录（创建会话）时间；从未登录过则返回无效 QDateTime
	QDateTime lastLoginOf(const QString &username) const;

  signals:
	// 在线人数（去重用户数）发生变化；仅增减时发出，纯活动刷新不发出
	void onlineChanged(int count);

  private:
	struct Session {
		QString username;
		QDateTime expiresAt;
	};
	QHash<QString, Session> sessions_;
	QHash<QString, QDateTime> lastSeen_;
	QHash<QString, QDateTime> lastLogin_;
	int ttlSeconds_ = 6 * 60 * 60; // 6 hours

	void purgeExpired();
	int emitIfChanged(int before, int after);
};
