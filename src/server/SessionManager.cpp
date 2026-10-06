/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SessionManager.h"

#include <QRandomGenerator>
#include <QSet>

SessionManager::SessionManager(QObject *parent) : QObject(parent) {}

QString SessionManager::createSession(const QString &username) {
	purgeExpired();
	const int before = onlineCount();
	QByteArray buf(32, Qt::Uninitialized);
	auto *gen = QRandomGenerator::system();
	for (int i = 0; i < buf.size(); ++i)
		buf[i] = static_cast<char>(gen->bounded(0, 256));
	const auto token = QString::fromLatin1(buf.toHex());
	Session s{username, QDateTime::currentDateTimeUtc().addSecs(ttlSeconds_)};
	sessions_.insert(token, s);
	emitIfChanged(before, onlineCount());
	return token;
}

bool SessionManager::validate(const QString &token, QString *username) {
	const auto it = sessions_.find(token);
	if (it == sessions_.end())
		return false;
	if (it->expiresAt < QDateTime::currentDateTimeUtc()) {
		const int before = onlineCount();
		sessions_.erase(it);
		emitIfChanged(before, onlineCount());
		return false;
	}
	// sliding window: extend on access
	it->expiresAt = QDateTime::currentDateTimeUtc().addSecs(ttlSeconds_);
	if (username) {
		*username = it->username;
		lastSeen_[it->username] = QDateTime::currentDateTime();
	}
	return true;
}

void SessionManager::destroy(const QString &token) {
	// 先查用户名再删，便于比较在线数
	QString user;
	const auto it = sessions_.find(token);
	if (it == sessions_.end())
		return;
	user = it->username;
	const int before = onlineCount();
	sessions_.erase(it);
	emitIfChanged(before, onlineCount());
}

void SessionManager::destroyAllForUser(const QString &username) {
	const int before = onlineCount();
	for (auto it = sessions_.begin(); it != sessions_.end();) {
		if (it->username == username)
			it = sessions_.erase(it);
		else
			++it;
	}
	emitIfChanged(before, onlineCount());
}

int SessionManager::onlineCount() { return onlineUsernames().size(); }

QStringList SessionManager::onlineUsernames() {
	purgeExpired();
	QSet<QString> set;
	for (const auto &s : sessions_)
		set.insert(s.username);
	QStringList out = set.values();
	out.sort();
	return out;
}

QDateTime SessionManager::lastSeenOf(const QString &username) const {
	return lastSeen_.value(username);
}

int SessionManager::emitIfChanged(int before, int after) {
	if (before != after)
		emit onlineChanged(after);
	return after;
}

void SessionManager::purgeExpired() {
	const auto now = QDateTime::currentDateTimeUtc();
	for (auto it = sessions_.begin(); it != sessions_.end();) {
		if (it->expiresAt < now)
			it = sessions_.erase(it);
		else
			++it;
	}
}
