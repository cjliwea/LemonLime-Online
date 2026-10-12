/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SubmissionServer.h"

#include "SessionManager.h"
#include "UserStore.h"
#include "core/contest.h"
#include "core/task.h"
#include "core/testcase.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QTcpServer>
#include <QUrlQuery>

#include <algorithm>

namespace {
	constexpr auto kCookieName = "lemon_sid";
	constexpr auto kStatementDirName = "statements";
	constexpr auto kLegacyStatementName = "statement.pdf";
	constexpr auto kAuditLogName = "online_submissions.log";
	constexpr auto kConfigName = "online_config.json";

	QString readResource(const QString &path) {
		QFile f(path);
		if (! f.open(QFile::ReadOnly))
			return {};
		return QString::fromUtf8(f.readAll());
	}

	QByteArray readResourceBytes(const QString &path) {
		QFile f(path);
		if (! f.open(QFile::ReadOnly))
			return {};
		return f.readAll();
	}

	QString cookieValue(const QHttpServerRequest &req, const QString &name) {
		const auto headers = req.headers();
		const auto cookies = headers.values(QHttpHeaders::WellKnownHeader::Cookie);
		for (const auto &cookieHeader : cookies) {
			const auto parts = QString::fromUtf8(cookieHeader).split(';', Qt::SkipEmptyParts);
			for (const auto &p : parts) {
				const auto trimmed = p.trimmed();
				const int eq = trimmed.indexOf('=');
				if (eq <= 0)
					continue;
				if (trimmed.left(eq) == name)
					return trimmed.mid(eq + 1);
			}
		}
		return {};
	}

	QMap<QString, QString> parseFormUrlEncoded(const QByteArray &body) {
		QMap<QString, QString> out;
		const QUrlQuery q(QString::fromUtf8(body));
		for (const auto &kv : q.queryItems(QUrl::FullyDecoded))
			out.insert(kv.first, kv.second);
		return out;
	}

	QString extensionFor(const QString &lang) {
		const auto l = lang.toLower();
		if (l == "c")
			return QStringLiteral("c");
		if (l == "python" || l == "py")
			return QStringLiteral("py");
		if (l == "pascal" || l == "pas")
			return QStringLiteral("pas");
		return QStringLiteral("cpp");
	}

	// 由上传的文件名推断源码扩展名；不支持的类型返回空串。
	QString extensionForFileName(const QString &name) {
		const auto suffix = QFileInfo(name).suffix().toLower();
		if (suffix == "cpp" || suffix == "cc" || suffix == "cxx" || suffix == "c++")
			return QStringLiteral("cpp");
		if (suffix == "c")
			return QStringLiteral("c");
		if (suffix == "py")
			return QStringLiteral("py");
		if (suffix == "pas" || suffix == "pp")
			return QStringLiteral("pas");
		return QString();
	}

	QString encodeRfc5987(const QString &name) {
		const auto utf8 = name.toUtf8();
		QString out;
		for (char c : utf8) {
			const auto u = static_cast<unsigned char>(c);
			const bool unreserved = (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') ||
			                        (u >= 'a' && u <= 'z') || u == '-' || u == '_' || u == '.' || u == '~';
			if (unreserved)
				out.append(QChar(c));
			else
				out.append(QString::asprintf("%%%02X", u));
		}
		return out;
	}

	// Content-Disposition 的 ASCII 回退文件名：非 ASCII 或特殊字符替换为下划线
	QString asciiFallbackName(const QString &name) {
		QString out;
		for (const QChar c : name) {
			if (c.unicode() >= 0x20 && c.unicode() < 0x80 && c != QLatin1Char('"') && c != QLatin1Char('\\'))
				out.append(c);
			else
				out.append(QLatin1Char('_'));
		}
		return out.isEmpty() ? QStringLiteral("statement") : out;
	}

	// 单层目录名/文件名合法性：禁止路径分隔符、保留字符、"."、".."、Windows 保留名。
	bool isSafePathComponent(const QString &s) {
		if (s.isEmpty() || s.size() > 128)
			return false;
		if (s == QLatin1String(".") || s == QLatin1String(".."))
			return false;
		for (const QChar c : s) {
			if (c.unicode() < 0x20 || QStringLiteral("\\/:*?\"<>|").contains(c))
				return false;
		}
		if (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' ')))
			return false;
		static const QStringList reserved = {
		    QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),  QStringLiteral("NUL"),
		    QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
		    QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
		    QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
		    QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
		    QStringLiteral("LPT8"), QStringLiteral("LPT9")};
		return ! reserved.contains(s.section(QLatin1Char('.'), 0, 0).toUpper());
	}

	constexpr int kMaxFolderDepth = 8;

	// 学生桌面工作区
	constexpr auto kDesktopDirName = "desktop";
	constexpr auto kFsTrashDirName = ".trash";
	constexpr auto kFsTrashIndexName = ".trash/trash_index.json";
	constexpr int kMaxFsFileBytes = 512 * 1024;
	constexpr int kMaxFsSegmentLen = 60;

	// 单层名字净化：替换非法字符、去掉首尾空白与结尾点，兜底名 + 截断
	QString sanitizeSegment(const QString &raw, const QString &fallback) {
		QString s = raw.trimmed();
		for (int i = 0; i < s.size(); ++i) {
			const QChar c = s.at(i);
			if (c.unicode() < 0x20 || QStringLiteral("\\/:*?\"<>|").contains(c))
				s[i] = QLatin1Char('_');
		}
		while (! s.isEmpty() && (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' '))))
			s.chop(1);
		if (s.isEmpty())
			s = fallback;
		if (s.size() > kMaxFsSegmentLen)
			s = s.left(kMaxFsSegmentLen);
		return s;
	}

	QJsonArray readTrashIndex(const QString &root) {
		QFile f(QDir(root).filePath(QLatin1String(kFsTrashIndexName)));
		if (! f.open(QFile::ReadOnly))
			return {};
		return QJsonDocument::fromJson(f.readAll()).array();
	}

	void writeTrashIndex(const QString &root, const QJsonArray &arr) {
		const auto path = QDir(root).filePath(QLatin1String(kFsTrashIndexName));
		QDir().mkpath(QFileInfo(path).absolutePath());
		QSaveFile f(path);
		if (! f.open(QFile::WriteOnly))
			return;
		f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
		f.commit();
	}
} // namespace

// 题面及样例只保留一个槽位：statements/ 下按文件名排序取第一个；
// 老比赛没有该目录时回退到比赛目录下的 statement.pdf。
QString SubmissionServer::findStatementFile(const QString &contestDir) {
	if (contestDir.isEmpty())
		return {};
	const QDir stmtDir(QDir(contestDir).filePath(QLatin1String(kStatementDirName)));
	const auto files = stmtDir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
	if (! files.isEmpty())
		return files.first().absoluteFilePath();
	const auto legacy = QDir(contestDir).filePath(QLatin1String(kLegacyStatementName));
	return QFile::exists(legacy) ? legacy : QString{};
}

SubmissionServer::SubmissionServer(QObject *parent) : QObject(parent) {
	userStore_ = new UserStore(this);
	sessions_ = new SessionManager(this);
	// 在线人数变化转发给主窗口（状态栏 / 实时状况页）
	connect(sessions_, &SessionManager::onlineChanged, this, &SubmissionServer::onlineCountChanged);
}

SubmissionServer::~SubmissionServer() { stop(); }

void SubmissionServer::bindContest(Contest *contest, const QString &contestDir) {
	contest_ = contest;
	contestDir_ = contestDir;
	if (userStore_)
		userStore_->loadFromContestDir(contestDir);
	loadConfig();
}

void SubmissionServer::setContestWindow(bool enabled, const QDateTime &start, const QDateTime &end) {
	windowEnabled_ = enabled;
	startTime_ = start;
	endTime_ = end;
	saveConfig();
}

void SubmissionServer::setAutoJudge(bool on) {
	autoJudge_ = on;
	saveConfig();
}

void SubmissionServer::setAnnouncement(const QString &text) {
	announcement_ = text;
	saveConfig();
	emit logMessage(text.isEmpty() ? tr("已清除公告") : tr("公告已广播：%1").arg(text));
}

void SubmissionServer::setNotice(const QString &text) {
	notice_ = text;
	saveConfig();
	emit logMessage(text.isEmpty() ? tr("已清除开考须知") : tr("开考须知已更新"));
}

void SubmissionServer::setDefaultUiMode(const QString &mode) {
	defaultUiMode_ = (mode == QStringLiteral("editor") || mode == QStringLiteral("upload"))
	                     ? mode
	                     : QStringLiteral("both");
	saveConfig();
}

void SubmissionServer::setUiTheme(const QString &theme) {
	uiTheme_ = (theme == QStringLiteral("noilinux")) ? theme : QStringLiteral("online");
	saveConfig();
	emit logMessage(uiTheme_ == QStringLiteral("noilinux") ? tr("学生端界面已切换为 NOI Linux 桌面")
	                                                       : tr("学生端界面已切换为在线（默认）"));
}

int SubmissionServer::onlineCount() const { return sessions_ ? sessions_->onlineCount() : 0; }

QString SubmissionServer::effectiveUiMode(const QString &username) const {
	if (userStore_ && ! username.isEmpty()) {
		const auto mode = userStore_->uiModeOf(username);
		if (mode == QStringLiteral("noilinux") || mode == QStringLiteral("online"))
			return mode;
	}
	return uiTheme_;
}

QStringList SubmissionServer::onlineUsernames() const {
	return sessions_ ? sessions_->onlineUsernames() : QStringList{};
}

QDateTime SubmissionServer::lastSeenOf(const QString &username) const {
	return sessions_ ? sessions_->lastSeenOf(username) : QDateTime{};
}

QDateTime SubmissionServer::lastLoginOf(const QString &username) const {
	return sessions_ ? sessions_->lastLoginOf(username) : QDateTime{};
}

void SubmissionServer::forceLogout(const QString &username) {
	if (sessions_)
		sessions_->destroyAllForUser(username);
	emit logMessage(tr("已注销用户 %1 的全部会话").arg(username));
}

void SubmissionServer::sendDirectMessage(const QString &username, const QString &text) {
	if (username.isEmpty() || text.isEmpty())
		return;
	QJsonObject msg;
	msg.insert("text", text);
	msg.insert("ts", QDateTime::currentDateTime().toString(Qt::ISODate));
	pendingMessages_[username].append(msg);
	emit logMessage(tr("私信已发送给 %1：%2").arg(username, text));
}

bool SubmissionServer::loadConfig() {
	if (contestDir_.isEmpty())
		return false;
	QFile f(QDir(contestDir_).filePath(kConfigName));
	if (! f.exists())
		return true;
	if (! f.open(QFile::ReadOnly))
		return false;
	const auto obj = QJsonDocument::fromJson(f.readAll()).object();
	windowEnabled_ = obj.value("windowEnabled").toBool(false);
	startTime_ = QDateTime::fromString(obj.value("startTime").toString(), Qt::ISODate);
	endTime_ = QDateTime::fromString(obj.value("endTime").toString(), Qt::ISODate);
	autoJudge_ = obj.value("autoJudge").toBool(true);
	announcement_ = obj.value("announcement").toString();
	notice_ = obj.value("notice").toString();
	defaultUiMode_ = obj.value("defaultUiMode").toString(QStringLiteral("both"));
	uiTheme_ = obj.value("uiTheme").toString(QStringLiteral("online"));
	return true;
}

bool SubmissionServer::saveConfig() const {
	if (contestDir_.isEmpty())
		return false;
	QJsonObject obj;
	obj.insert("version", 1);
	obj.insert("windowEnabled", windowEnabled_);
	if (startTime_.isValid())
		obj.insert("startTime", startTime_.toString(Qt::ISODate));
	if (endTime_.isValid())
		obj.insert("endTime", endTime_.toString(Qt::ISODate));
	obj.insert("autoJudge", autoJudge_);
	if (! announcement_.isEmpty())
		obj.insert("announcement", announcement_);
	if (! notice_.isEmpty())
		obj.insert("notice", notice_);
	obj.insert("defaultUiMode", defaultUiMode_);
	obj.insert("uiTheme", uiTheme_);
	QSaveFile f(QDir(contestDir_).filePath(kConfigName));
	if (! f.open(QFile::WriteOnly))
		return false;
	f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
	return f.commit();
}

bool SubmissionServer::start(const QHostAddress &addr, quint16 port, QString *errorOut) {
	if (running_)
		return true;
	if (! contest_ || contestDir_.isEmpty()) {
		if (errorOut)
			*errorOut = tr("No contest is currently bound to the server.");
		return false;
	}
	http_ = new QHttpServer(this);
	tcp_ = new QTcpServer(this);
	setupRoutes();
	if (! tcp_->listen(addr, port)) {
		if (errorOut)
			*errorOut = tcp_->errorString();
		delete http_;
		http_ = nullptr;
		delete tcp_;
		tcp_ = nullptr;
		return false;
	}
	if (! http_->bind(tcp_)) {
		if (errorOut)
			*errorOut = tr("Failed to bind HTTP server to TCP socket.");
		tcp_->close();
		delete http_;
		http_ = nullptr;
		delete tcp_;
		tcp_ = nullptr;
		return false;
	}
	boundAddress_ = addr;
	boundPort_ = tcp_->serverPort();
	running_ = true;
	emit started(boundAddress_, boundPort_);
	emit logMessage(tr("Server started on %1:%2").arg(boundAddress_.toString()).arg(boundPort_));
	return true;
}

void SubmissionServer::stop() {
	if (! running_)
		return;
	if (tcp_) {
		tcp_->close();
		tcp_->deleteLater();
		tcp_ = nullptr;
	}
	if (http_) {
		http_->deleteLater();
		http_ = nullptr;
	}
	running_ = false;
	emit stopped();
	emit logMessage(tr("Server stopped"));
}

void SubmissionServer::setupRoutes() {
	using namespace Qt::StringLiterals;

	http_->route("/", QHttpServerRequest::Method::Get,
	             [this](const QHttpServerRequest &req) { return handleIndex(req); });

	http_->route("/login", QHttpServerRequest::Method::Get, [this]() { return handleLoginPage(); });
	http_->route("/login", QHttpServerRequest::Method::Post,
	             [this](const QHttpServerRequest &req) { return handleLoginPost(req); });
	http_->route("/logout", QHttpServerRequest::Method::Post,
	             [this](const QHttpServerRequest &req) { return handleLogout(req); });

	http_->route(
	    "/submit/<arg>", QHttpServerRequest::Method::Get,
	    [this](qint32 taskId, const QHttpServerRequest &req) { return handleSubmitPage(taskId, req); });

	http_->route("/api/tasks", QHttpServerRequest::Method::Get,
	             [this](const QHttpServerRequest &req) { return handleApiTasks(req); });

	http_->route("/api/announce", QHttpServerRequest::Method::Get,
	             [this](const QHttpServerRequest &req) { return handleApiAnnounce(req); });

	http_->route(
	    "/api/submit/<arg>", QHttpServerRequest::Method::Post,
	    [this](qint32 taskId, const QHttpServerRequest &req) { return handleApiSubmit(taskId, req); });

	http_->route("/api/upload-folder", QHttpServerRequest::Method::Post,
	             [this](const QHttpServerRequest &req) { return handleApiUploadFolder(req); });

	http_->route(
	    "/api/upload-source/<arg>", QHttpServerRequest::Method::Post,
	    [this](qint32 taskId, const QHttpServerRequest &req) { return handleApiUploadSource(taskId, req); });

	http_->route("/statement", QHttpServerRequest::Method::Get,
	             [this](const QHttpServerRequest &req) { return handleStatement(req); });

	// 学生桌面工作区（服务端文件树）
	http_->route("/api/fs", QHttpServerRequest::Method::Get,
	             [this](const QHttpServerRequest &req) { return handleApiFs(req); });
	http_->route("/api/fs", QHttpServerRequest::Method::Post,
	             [this](const QHttpServerRequest &req) { return handleApiFsMutate(req); });
	http_->route(
	    "/api/fs/sample/<arg>", QHttpServerRequest::Method::Get,
	    [this](const QString &name, const QHttpServerRequest &req) { return handleApiFsSample(name, req); });

	http_->route("/desktop", QHttpServerRequest::Method::Get,
	             [this](const QHttpServerRequest &req) { return handleDesktopPage(req); });

	// static assets
	http_->route("/assets/css/app.css", QHttpServerRequest::Method::Get,
	             [this]() { return handleStatic(":/online/css/app.css", "text/css; charset=utf-8"); });
	http_->route("/assets/css/desktop.css", QHttpServerRequest::Method::Get,
	             [this]() { return handleStatic(":/online/css/desktop.css", "text/css; charset=utf-8"); });
	http_->route("/assets/js/app.js", QHttpServerRequest::Method::Get, [this]() {
		return handleStatic(":/online/js/app.js", "application/javascript; charset=utf-8");
	});
	http_->route("/assets/js/editor.js", QHttpServerRequest::Method::Get, [this]() {
		return handleStatic(":/online/js/editor.js", "application/javascript; charset=utf-8");
	});
	http_->route("/assets/js/desktop.js", QHttpServerRequest::Method::Get, [this]() {
		return handleStatic(":/online/js/desktop.js", "application/javascript; charset=utf-8");
	});
}

QString SubmissionServer::sessionUser(const QHttpServerRequest &req) const {
	const auto sid = cookieValue(req, kCookieName);
	if (sid.isEmpty() || ! sessions_)
		return {};
	QString user;
	return sessions_->validate(sid, &user) ? user : QString{};
}

bool SubmissionServer::requireSession(const QHttpServerRequest &req, QString *user) const {
	const auto u = sessionUser(req);
	if (u.isEmpty())
		return false;
	if (user)
		*user = u;
	return true;
}

QHttpServerResponse SubmissionServer::redirect(const QString &location, const QString &setCookie) const {
	QHttpServerResponse resp(QHttpServerResponder::StatusCode::SeeOther);
	QHttpHeaders h;
	h.append(QHttpHeaders::WellKnownHeader::Location, location);
	if (! setCookie.isEmpty())
		h.append(QHttpHeaders::WellKnownHeader::SetCookie, setCookie);
	h.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-store");
	resp.setHeaders(h);
	return resp;
}

QHttpServerResponse SubmissionServer::jsonError(int httpStatus, const QString &message) const {
	QJsonObject obj{{"error", message}};
	QHttpServerResponse resp("application/json", QJsonDocument(obj).toJson(QJsonDocument::Compact),
	                         static_cast<QHttpServerResponder::StatusCode>(httpStatus));
	return resp;
}

QHttpServerResponse SubmissionServer::handleStatic(const QString &resourcePath, const QString &contentType) {
	const auto bytes = readResourceBytes(resourcePath);
	if (bytes.isEmpty())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
	QHttpServerResponse resp(contentType.toLatin1(), bytes);
	QHttpHeaders h = resp.headers();
	h.append(QHttpHeaders::WellKnownHeader::CacheControl, "private, max-age=300");
	resp.setHeaders(h);
	return resp;
}

QHttpServerResponse SubmissionServer::handleLoginPage() {
	auto html = readResource(":/online/login.html");
	if (html.isEmpty())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::InternalServerError);
	return QHttpServerResponse("text/html; charset=utf-8", html.toUtf8());
}

QHttpServerResponse SubmissionServer::handleLoginPost(const QHttpServerRequest &req) {
	const auto form = parseFormUrlEncoded(req.body());
	const auto username = form.value("username").trimmed();
	const auto password = form.value("password");
	if (username.isEmpty() || password.isEmpty())
		return redirect("/login?err=1");
	if (! userStore_ || ! userStore_->verify(username, password))
		return redirect("/login?err=1");
	const auto token = sessions_->createSession(username);
	const auto cookie = QStringLiteral("%1=%2; Path=/; HttpOnly; SameSite=Lax").arg(kCookieName, token);
	emit logMessage(tr("Login OK: %1").arg(username));
	return redirect(effectiveUiMode(username) == QStringLiteral("noilinux") ? QStringLiteral("/desktop")
	                                                                        : QStringLiteral("/"),
	                cookie);
}

QHttpServerResponse SubmissionServer::handleLogout(const QHttpServerRequest &req) {
	const auto sid = cookieValue(req, kCookieName);
	if (! sid.isEmpty())
		sessions_->destroy(sid);
	const auto cookie = QStringLiteral("%1=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0").arg(kCookieName);
	return redirect("/login", cookie);
}

QHttpServerResponse SubmissionServer::handleIndex(const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return redirect("/login");
	// NOI Linux 桌面模式下，访问根路径一律落到桌面
	if (effectiveUiMode(user) == QStringLiteral("noilinux"))
		return redirect("/desktop");
	auto html = readResource(":/online/index.html");
	if (html.isEmpty())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::InternalServerError);
	return QHttpServerResponse("text/html; charset=utf-8", html.toUtf8());
}

QHttpServerResponse SubmissionServer::handleDesktopPage(const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return redirect("/login");
	// 账号设定为在线界面时，桌面入口回落到题目列表页
	if (effectiveUiMode(user) != QStringLiteral("noilinux"))
		return redirect("/");
	auto html = readResource(":/online/desktop.html");
	if (html.isEmpty())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::InternalServerError);
	return QHttpServerResponse("text/html; charset=utf-8", html.toUtf8());
}

QHttpServerResponse SubmissionServer::handleSubmitPage(qint32 taskId, const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return redirect("/login");
	if (! contest_ || taskId < 0 || taskId >= contest_->getTaskList().size())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
	auto html = readResource(":/online/submit.html");
	if (html.isEmpty())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::InternalServerError);
	return QHttpServerResponse("text/html; charset=utf-8", html.toUtf8());
}

QHttpServerResponse SubmissionServer::handleApiTasks(const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return jsonError(401, tr("Not authenticated"));
	if (! contest_)
		return jsonError(500, tr("No contest bound"));

	const auto taskList = contest_->getTaskList();
	QJsonArray arr;
	for (int i = 0; i < taskList.size(); ++i) {
		const auto *t = taskList.at(i);
		QJsonObject obj;
		obj.insert("id", i);
		obj.insert("title", t->getProblemTitle());
		obj.insert("totalScore", t->getTotalScore());
		// 时限展示单个测试点的限制；getTotalTimeLimit() 是全场评测总时长（各点时限之和），
		// 给学生看会造成误解（33 个 1s 测试点会显示成 33.0s）。各点时限不同则附带最大值。
		QList<int> timeLimits;
		for (const auto *testCase : t->getTestCaseList())
			timeLimits.append(testCase->getTimeLimit());
		if (! timeLimits.isEmpty()) {
			const auto minMax = std::minmax_element(timeLimits.cbegin(), timeLimits.cend());
			obj.insert("timeLimitMs", *minMax.first);
			if (*minMax.first != *minMax.second)
				obj.insert("timeLimitMaxMs", *minMax.second);
		} else {
			obj.insert("timeLimitMs", 0);
		}
		obj.insert("sourceFileName", t->getSourceFileName());

		// determine last submission time across known extensions
		const auto srcDir = QDir(QDir(contestDir_).filePath(QStringLiteral("source/%1").arg(user)));
		const auto base = t->getSourceFileName();
		const QString folder = t->getSubFolderCheck() ? base + QChar('/') : QString();
		QDateTime latest;
		for (const auto *ext : {"cpp", "c", "py", "pas"}) {
			const QFileInfo fi(srcDir, folder + base + QChar('.') + QString::fromLatin1(ext));
			if (fi.exists() && (! latest.isValid() || fi.lastModified() > latest))
				latest = fi.lastModified();
		}
		obj.insert("submittedAt", latest.isValid() ? latest.toString(Qt::ISODate) : QString());
		arr.append(obj);
	}

	QJsonObject root;
	root.insert("contestTitle", contest_->getContestTitle());
	root.insert("user", user);
	root.insert("displayName", userStore_ ? userStore_->displayNameOf(user) : QString());
	root.insert("hasStatement", ! findStatementFile(contestDir_).isEmpty());
	root.insert("tasks", arr);
	root.insert("serverNow", QDateTime::currentDateTime().toString(Qt::ISODate));
	root.insert("announcement", announcement_);
	root.insert("notice", notice_);
	root.insert("defaultUiMode", defaultUiMode_);
	root.insert("windowEnabled", windowEnabled_);
	if (windowEnabled_ && startTime_.isValid())
		root.insert("startTime", startTime_.toString(Qt::ISODate));
	if (windowEnabled_ && endTime_.isValid())
		root.insert("endTime", endTime_.toString(Qt::ISODate));
	return QHttpServerResponse("application/json", QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QHttpServerResponse SubmissionServer::handleApiAnnounce(const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return jsonError(401, tr("Not authenticated"));
	QJsonObject root;
	root.insert("username", user);
	root.insert("announcement", announcement_);
	root.insert("notice", notice_);
	root.insert("serverNow", QDateTime::currentDateTime().toString(Qt::ISODate));
	// 比赛时间窗（NOI Linux 桌面顶栏倒计时用；列表页忽略这些字段）
	root.insert("windowEnabled", windowEnabled_ && startTime_.isValid() && endTime_.isValid());
	if (startTime_.isValid())
		root.insert("startTime", startTime_.toString(Qt::ISODate));
	if (endTime_.isValid())
		root.insert("endTime", endTime_.toString(Qt::ISODate));
	// 定向私信：随公告轮询下发，取走即清（未在线的学生在下次轮询时补收）
	const auto it = pendingMessages_.constFind(user);
	if (it != pendingMessages_.constEnd()) {
		if (! it.value().isEmpty())
			root.insert("messages", it.value());
		pendingMessages_.erase(it);
	}
	return QHttpServerResponse("application/json", QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QHttpServerResponse SubmissionServer::handleApiSubmit(qint32 taskId, const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return jsonError(401, tr("Not authenticated"));
	if (! contest_)
		return jsonError(500, tr("No contest bound"));
	if (taskId < 0 || taskId >= contest_->getTaskList().size())
		return jsonError(404, tr("Unknown task"));

	if (windowEnabled_) {
		const auto now = QDateTime::currentDateTime();
		if (startTime_.isValid() && now < startTime_)
			return jsonError(403, tr("比赛尚未开始"));
		if (endTime_.isValid() && now > endTime_)
			return jsonError(403, tr("比赛已结束"));
	}

	const auto body = req.body();
	if (body.isEmpty())
		return jsonError(400, tr("Empty body"));
	if (body.size() > maxSourceBytes_)
		return jsonError(413, tr("Source too large (max %1 KB)").arg(maxSourceBytes_ / 1024));

	const auto contentType =
	    QString::fromUtf8(req.headers().combinedValue(QHttpHeaders::WellKnownHeader::ContentType));

	QByteArray source;
	QString language = QStringLiteral("cpp");
	if (contentType.contains("application/json", Qt::CaseInsensitive)) {
		QJsonParseError err;
		const auto doc = QJsonDocument::fromJson(body, &err);
		if (err.error != QJsonParseError::NoError)
			return jsonError(400, tr("Bad JSON: %1").arg(err.errorString()));
		const auto obj = doc.object();
		source = obj.value("source").toString().toUtf8();
		const auto lang = obj.value("language").toString();
		if (! lang.isEmpty())
			language = lang;
	} else {
		source = body;
	}
	if (source.trimmed().isEmpty())
		return jsonError(400, tr("Empty source"));

	QString writeErr;
	if (! writeSubmission(user, taskId, source, extensionFor(language), &writeErr))
		return jsonError(500, writeErr);

	const auto sha =
	    QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
	appendAuditLog(user, taskId, source.size(), sha);
	++submissionCount_;
	emit submissionReceived(user, contest_->getTaskList().at(taskId)->getProblemTitle(), source.size());

	bool willJudge = false;
	if (autoJudge_ && contest_) {
		willJudge = true;
		auto contestPtr = contest_.data();
		const QString u = user;
		const int t = taskId;
		// Run on main thread on the next event loop tick so the HTTP response goes
		// out first; then refresh selectors and trigger judge.
		QMetaObject::invokeMethod(
		    contestPtr,
		    [contestPtr, u, t]() {
			    contestPtr->refreshContestantList();
			    QList<std::pair<QString, QVector<int>>> work;
			    work.append({u, QVector<int>{t}});
			    contestPtr->judge(work);
		    },
		    Qt::QueuedConnection);
	}

	QJsonObject ok{
	    {"ok", true},
	    {"size", source.size()},
	    {"submittedAt", QDateTime::currentDateTime().toString(Qt::ISODate)},
	    {"willJudge", willJudge},
	};
	return QHttpServerResponse("application/json", QJsonDocument(ok).toJson(QJsonDocument::Compact));
}

// 单题上传源码文件：把文件原始字节按该题的标准文件名写入 source/<用户名>/。
// 原始字节直传，避免机房 GBK 源码被当成 UTF-8 转坏。
QHttpServerResponse SubmissionServer::handleApiUploadSource(qint32 taskId, const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return jsonError(401, tr("Not authenticated"));
	if (! contest_)
		return jsonError(500, tr("No contest bound"));
	if (taskId < 0 || taskId >= contest_->getTaskList().size())
		return jsonError(404, tr("Unknown task"));

	if (windowEnabled_) {
		const auto now = QDateTime::currentDateTime();
		if (startTime_.isValid() && now < startTime_)
			return jsonError(403, tr("比赛尚未开始"));
		if (endTime_.isValid() && now > endTime_)
			return jsonError(403, tr("比赛已结束"));
	}

	const auto body = req.body();
	if (body.isEmpty())
		return jsonError(400, tr("Empty body"));
	// base64 体积约为原始字节的 4/3，body 上限相应放宽，解码后再按源码上限校验
	const qint64 bodyLimit = static_cast<qint64>(maxSourceBytes_) * 4 / 3 + 4096;
	if (body.size() > bodyLimit)
		return jsonError(413, tr("文件过大（上限 %1 KB）").arg(maxSourceBytes_ / 1024));

	QJsonParseError err;
	const auto doc = QJsonDocument::fromJson(body, &err);
	if (err.error != QJsonParseError::NoError)
		return jsonError(400, tr("Bad JSON: %1").arg(err.errorString()));
	const auto obj = doc.object();

	const auto name = obj.value("name").toString().trimmed();
	if (! isSafePathComponent(name))
		return jsonError(400, tr("文件名不合法"));
	const auto extension = extensionForFileName(name);
	if (extension.isEmpty())
		return jsonError(400, tr("仅支持 .cpp / .cc / .cxx / .c / .pas / .py"));

	const auto source = QByteArray::fromBase64(obj.value("content").toString().toLatin1());
	if (source.isEmpty())
		return jsonError(400, tr("文件内容为空"));
	if (source.size() > maxSourceBytes_)
		return jsonError(413, tr("文件过大（上限 %1 KB）").arg(maxSourceBytes_ / 1024));

	QString writeErr;
	if (! writeSubmission(user, taskId, source, extension, &writeErr))
		return jsonError(500, writeErr);

	const auto sha =
	    QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
	appendAuditLog(user, taskId, source.size(), sha);
	++submissionCount_;
	emit submissionReceived(user, contest_->getTaskList().at(taskId)->getProblemTitle(), source.size());

	bool willJudge = false;
	if (autoJudge_) {
		willJudge = true;
		auto contestPtr = contest_.data();
		const QString u = user;
		const int t = taskId;
		QMetaObject::invokeMethod(
		    contestPtr,
		    [contestPtr, u, t]() {
			    contestPtr->refreshContestantList();
			    QList<std::pair<QString, QVector<int>>> work;
			    work.append({u, QVector<int>{t}});
			    contestPtr->judge(work);
		    },
		    Qt::QueuedConnection);
	}

	QJsonObject ok{
	    {"ok", true},
	    {"name", name},
	    {"size", source.size()},
	    {"submittedAt", QDateTime::currentDateTime().toString(Qt::ISODate)},
	    {"willJudge", willJudge},
	};
	return QHttpServerResponse("application/json", QJsonDocument(ok).toJson(QJsonDocument::Compact));
}

QHttpServerResponse SubmissionServer::handleApiUploadFolder(const QHttpServerRequest &req) {
	QString loginUser;
	if (! requireSession(req, &loginUser))
		return jsonError(401, tr("Not authenticated"));
	if (! contest_)
		return jsonError(500, tr("No contest bound"));

	if (windowEnabled_) {
		const auto now = QDateTime::currentDateTime();
		if (startTime_.isValid() && now < startTime_)
			return jsonError(403, tr("比赛尚未开始"));
		if (endTime_.isValid() && now > endTime_)
			return jsonError(403, tr("比赛已结束"));
	}

	const auto body = req.body();
	if (body.isEmpty())
		return jsonError(400, tr("Empty body"));
	if (body.size() > maxFolderBytes_)
		return jsonError(413, tr("文件夹过大（上限 %1 MB）").arg(maxFolderBytes_ / (1024 * 1024)));

	QJsonParseError err;
	const auto doc = QJsonDocument::fromJson(body, &err);
	if (err.error != QJsonParseError::NoError)
		return jsonError(400, tr("Bad JSON: %1").arg(err.errorString()));
	const auto obj = doc.object();

	const auto contestant = obj.value("contestant").toString().trimmed();
	if (! isSafePathComponent(contestant))
		return jsonError(400, tr("选手文件夹名不合法"));

	const auto files = obj.value("files").toArray();
	if (files.isEmpty())
		return jsonError(400, tr("文件列表为空"));
	if (files.size() > maxFolderFiles_)
		return jsonError(413, tr("文件数量过多（上限 %1 个）").arg(maxFolderFiles_));

	QJsonArray results;
	QString writeErr;
	if (! writeContestantFolder(contestant, files, &results, &writeErr))
		return jsonError(500, writeErr);

	qint64 totalBytes = 0;
	int written = 0;
	for (const auto &r : results) {
		const auto o = r.toObject();
		totalBytes += static_cast<qint64>(o.value("size").toDouble());
		if (o.value("ok").toBool())
			++written;
	}

	appendFolderAuditLog(loginUser, contestant, written, totalBytes);
	++submissionCount_;
	emit submissionReceived(contestant, tr("整包上传"), static_cast<int>(totalBytes));

	bool willJudge = false;
	if (autoJudge_ && written > 0) {
		willJudge = true;
		triggerJudgeContestant(contestant);
	}

	QJsonObject ok{
	    {"ok", true},
	    {"contestant", contestant},
	    {"written", written},
	    {"total", static_cast<int>(files.size())},
	    {"bytes", static_cast<double>(totalBytes)},
	    {"willJudge", willJudge},
	    {"files", results},
	    {"submittedAt", QDateTime::currentDateTime().toString(Qt::ISODate)},
	};
	return QHttpServerResponse("application/json", QJsonDocument(ok).toJson(QJsonDocument::Compact));
}

// 把选手文件夹按原样（含子目录）镜像到 <contestDir>/source/<contestant>/。
// 只做路径安全校验，不检查文件名是否符合题目要求 —— 那属于评测内容。
bool SubmissionServer::writeContestantFolder(const QString &contestant, const QJsonArray &files,
                                             QJsonArray *resultsOut, QString *errOut) {
	const QString root = QDir(contestDir_).filePath(QStringLiteral("source/%1").arg(contestant));
	if (! QDir().mkpath(root)) {
		if (errOut)
			*errOut = tr("无法创建选手目录：%1").arg(root);
		return false;
	}
	const QString rootAbs = QDir(root).absolutePath();

	for (const auto &v : files) {
		const auto o = v.toObject();
		const auto rawPath = o.value("path").toString();
		const auto bytes = QByteArray::fromBase64(o.value("content").toString().toLatin1());

		QJsonObject res;
		res.insert("path", rawPath);
		res.insert("size", static_cast<double>(bytes.size()));

		auto fail = [&res, resultsOut](const QString &msg) {
			res.insert("ok", false);
			res.insert("message", msg);
			if (resultsOut)
				resultsOut->append(res);
		};

		const auto rel = QDir::cleanPath(rawPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
		if (rel.isEmpty() || rel.startsWith(QLatin1String("../")) || rel == QLatin1String("..") ||
		    QDir::isAbsolutePath(rel)) {
			fail(tr("非法路径"));
			continue;
		}

		auto segs = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
		// 客户端提交的路径形如 <contestant>/<题目>/<源文件>，去掉首层选手文件夹
		if (! segs.isEmpty() && segs.first() == contestant)
			segs.removeFirst();
		if (segs.isEmpty() || segs.size() > kMaxFolderDepth) {
			fail(tr("路径层级不合法"));
			continue;
		}
		bool bad = false;
		for (const auto &s : segs) {
			if (! isSafePathComponent(s)) {
				bad = true;
				break;
			}
		}
		if (bad) {
			fail(tr("路径含非法字符"));
			continue;
		}

		const QString outFile = QDir(rootAbs).filePath(segs.join(QLatin1Char('/')));
		if (! outFile.startsWith(rootAbs + QLatin1Char('/'))) {
			fail(tr("路径越界"));
			continue;
		}
		QDir().mkpath(QFileInfo(outFile).absolutePath());

		QSaveFile f(outFile);
		if (! f.open(QFile::WriteOnly | QFile::Truncate)) {
			fail(f.errorString());
			continue;
		}
		if (f.write(bytes) != bytes.size() || ! f.commit()) {
			fail(f.errorString());
			continue;
		}

		res.insert("ok", true);
		res.insert("target", QStringLiteral("source/%1/%2").arg(contestant, segs.join(QLatin1Char('/'))));
		if (resultsOut)
			resultsOut->append(res);
	}
	return true;
}

void SubmissionServer::appendFolderAuditLog(const QString &loginUser, const QString &contestant,
                                            int fileCount, qint64 bytes) {
	QFile f(QDir(contestDir_).filePath(kAuditLogName));
	if (! f.open(QFile::Append | QFile::Text))
		return;
	const auto line = QStringLiteral("%1\t%2\tfolder=%3\tfiles=%4\tbytes=%5\n")
	                      .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
	                      .arg(loginUser)
	                      .arg(contestant)
	                      .arg(fileCount)
	                      .arg(bytes);
	f.write(line.toUtf8());
}

void SubmissionServer::triggerJudgeContestant(const QString &contestant) {
	auto contestPtr = contest_.data();
	if (! contestPtr)
		return;
	// 与单题提交一致：先让 HTTP 响应发出去，再刷新选手列表并整包评测
	QMetaObject::invokeMethod(
	    contestPtr,
	    [contestPtr, contestant]() {
		    contestPtr->refreshContestantList();
		    QVector<int> allTasks;
		    for (int i = 0; i < contestPtr->getTaskList().size(); ++i)
			    allTasks.append(i);
		    if (allTasks.isEmpty())
			    return;
		    QList<std::pair<QString, QVector<int>>> work;
		    work.append({contestant, allTasks});
		    contestPtr->judge(work);
	    },
	    Qt::QueuedConnection);
}

QHttpServerResponse SubmissionServer::handleStatement(const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return redirect("/login");
	const auto path = findStatementFile(contestDir_);
	if (path.isEmpty())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
	QFile f(path);
	if (! f.open(QFile::ReadOnly))
		return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
	const auto bytes = f.readAll();
	const auto name = QFileInfo(path).fileName();
	const auto mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension);
	// PDF 可直接在浏览器内预览，压缩包等其它形式一律作为附件下载
	const bool inlineDisplay = mime.name() == QLatin1String("application/pdf");
	QHttpServerResponse resp(mime.name().toUtf8(), bytes);
	QHttpHeaders h = resp.headers();
	h.append(QHttpHeaders::WellKnownHeader::ContentDisposition,
	         QStringLiteral("%1; filename=\"%2\"; filename*=UTF-8''%3")
	             .arg(inlineDisplay ? QStringLiteral("inline") : QStringLiteral("attachment"),
	                  asciiFallbackName(name), encodeRfc5987(name)));
	h.append(QHttpHeaders::WellKnownHeader::CacheControl, "private, max-age=60");
	resp.setHeaders(h);
	return resp;
}

// ---------------------------------------------------------------- 学生桌面工作区

QString SubmissionServer::fsRootFor(const QString &user) const {
	return QDir(contestDir_).filePath(QStringLiteral("%1/%2").arg(QLatin1String(kDesktopDirName), user));
}

QStringList SubmissionServer::fsSlotNames() const {
	QStringList slotNames;
	QSet<QString> used;
	if (contest_) {
		const auto list = contest_->getTaskList();
		for (int i = 0; i < list.size(); ++i) {
			auto base = list.at(i)->getSourceFileName();
			if (base.isEmpty())
				base = QStringLiteral("problem%1").arg(i + 1);
			base = sanitizeSegment(base, QStringLiteral("problem%1").arg(i + 1));
			QString name = base;
			int n = 2;
			while (used.contains(name.toLower()))
				name = QStringLiteral("%1_%2").arg(base).arg(n++);
			used.insert(name.toLower());
			slotNames << name;
		}
	}
	return slotNames;
}

void SubmissionServer::ensureFsScaffold(const QString &root) const {
	QDir().mkpath(root);
	QDir().mkpath(QDir(root).filePath(QLatin1String(kFsTrashDirName)));
	const auto slotNames = fsSlotNames();
	const auto tasks = contest_ ? contest_->getTaskList() : QList<Task *>{};
	for (int i = 0; i < slotNames.size(); ++i) {
		const auto dir = QDir(root).filePath(slotNames.at(i));
		QDir().mkpath(dir);
		const auto base = i < tasks.size() ? tasks.at(i)->getSourceFileName() : QString();
		const auto fileBase = base.isEmpty() ? slotNames.at(i) : sanitizeSegment(base, slotNames.at(i));
		const auto path = QDir(dir).filePath(fileBase + QStringLiteral(".cpp"));
		if (! QFile::exists(path)) {
			QFile f(path);
			if (f.open(QFile::WriteOnly | QFile::Truncate))
				f.write(QByteArray());
		}
	}
}

QString SubmissionServer::resolveFsPath(const QString &root, const QString &rel) const {
	const auto clean = QDir::fromNativeSeparators(rel.trimmed());
	if (clean.isEmpty())
		return root;
	QStringList parts = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
	if (parts.size() > kMaxFolderDepth)
		return {};
	for (const auto &p : parts) {
		if (p == QLatin1String(".") || p == QLatin1String(".."))
			return {};
		if (! isSafePathComponent(p))
			return {};
	}
	const auto rel2 = parts.join(QLatin1Char('/'));
	const auto abs = QDir(root).absoluteFilePath(rel2);
	// 规范化后再确认仍在 root 之内，挡住符号链接/怪路径逃逸
	const auto canonRoot = QDir::cleanPath(QDir(root).absolutePath());
	const auto canonAbs = QDir::cleanPath(abs);
	if (canonAbs != canonRoot && ! canonAbs.startsWith(canonRoot + QLatin1Char('/')))
		return {};
	return canonAbs;
}

QHttpServerResponse SubmissionServer::handleApiFs(const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return jsonError(401, tr("Not authenticated"));
	if (! contest_ || contestDir_.isEmpty())
		return jsonError(500, tr("No contest bound"));

	const auto root = fsRootFor(user);
	ensureFsScaffold(root);

	const auto query = QUrlQuery(req.url());
	// 读取单个文件内容
	if (query.hasQueryItem(QStringLiteral("path"))) {
		const auto abs =
		    resolveFsPath(root, query.queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded));
		if (abs.isEmpty())
			return jsonError(400, tr("Invalid path"));
		QFileInfo fi(abs);
		if (! fi.isFile() || fi.isSymLink())
			return jsonError(404, tr("Not a file"));
		if (fi.size() > kMaxFsFileBytes)
			return jsonError(413, tr("File too large"));
		QFile f(abs);
		if (! f.open(QFile::ReadOnly))
			return jsonError(500, tr("Cannot read file"));
		QJsonObject out;
		out.insert("path", QDir(root).relativeFilePath(abs));
		out.insert("content", QString::fromUtf8(f.readAll()));
		return QHttpServerResponse("application/json", QJsonDocument(out).toJson(QJsonDocument::Compact));
	}

	// 整棵树
	QJsonArray items;
	const auto slotNames = fsSlotNames();
	QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		it.next();
		const auto fi = it.fileInfo();
		const auto rel = QDir(root).relativeFilePath(fi.absoluteFilePath());
		if (rel.startsWith(QLatin1String(kFsTrashDirName)))
			continue; // 回收站不进工作区视图
		QJsonObject obj;
		obj.insert("path", rel);
		obj.insert("name", fi.fileName());
		const auto parentRel = QDir(root).relativeFilePath(fi.absolutePath());
		obj.insert("parent", parentRel == QLatin1String(".") ? QString() : parentRel);
		obj.insert("type", fi.isDir() ? "folder" : "file");
		if (fi.isFile())
			obj.insert("size", static_cast<double>(fi.size()));
		obj.insert("mtime", fi.lastModified().toString(Qt::ISODate));
		obj.insert("slot", slotNames.contains(fi.fileName()) || slotNames.contains(parentRel));
		items.append(obj);
	}

	// 回收站条目（保存在 .trash 下的实际文件 + 索引）
	QJsonArray trash = readTrashIndex(root);
	QJsonArray trashAlive;
	for (const auto &v : trash) {
		const auto obj = v.toObject();
		const auto stored = obj.value("stored").toString();
		if (QFileInfo::exists(
		        QDir(root).filePath(QLatin1String(kFsTrashDirName) + QLatin1Char('/') + stored)))
			trashAlive.append(obj);
	}
	if (trashAlive.size() != trash.size())
		writeTrashIndex(root, trashAlive);

	// 只读样例区：题面及样例下发的文件
	QJsonArray samples;
	const QDir stmtDir(QDir(contestDir_).filePath(QLatin1String(kStatementDirName)));
	for (const auto &fi : stmtDir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
		QJsonObject obj;
		obj.insert("name", fi.fileName());
		obj.insert("size", static_cast<double>(fi.size()));
		samples.append(obj);
	}
	if (samples.isEmpty()) {
		const auto legacy = findStatementFile(contestDir_);
		if (! legacy.isEmpty()) {
			const auto fi = QFileInfo(legacy);
			QJsonObject obj;
			obj.insert("name", fi.fileName());
			obj.insert("size", static_cast<double>(fi.size()));
			samples.append(obj);
		}
	}

	QJsonObject root2;
	root2.insert("user", user);
	root2.insert("slots", QJsonArray::fromStringList(slotNames));
	root2.insert("items", items);
	root2.insert("trash", trashAlive);
	root2.insert("samples", samples);
	return QHttpServerResponse("application/json", QJsonDocument(root2).toJson(QJsonDocument::Compact));
}

QHttpServerResponse SubmissionServer::handleApiFsMutate(const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return jsonError(401, tr("Not authenticated"));
	if (! contest_ || contestDir_.isEmpty())
		return jsonError(500, tr("No contest bound"));

	QJsonParseError err;
	const auto doc = QJsonDocument::fromJson(req.body(), &err);
	if (err.error != QJsonParseError::NoError)
		return jsonError(400, tr("Bad JSON: %1").arg(err.errorString()));
	const auto obj = doc.object();
	const auto op = obj.value("op").toString();
	const auto root = fsRootFor(user);
	ensureFsScaffold(root);

	const auto okJson = [](const QJsonObject &extra = {}) {
		QJsonObject out = extra;
		out.insert("ok", true);
		return QHttpServerResponse("application/json", QJsonDocument(out).toJson(QJsonDocument::Compact));
	};

	if (op == QLatin1String("mkdir") || op == QLatin1String("newfile") || op == QLatin1String("upload")) {
		const auto parentAbs = resolveFsPath(root, obj.value("parent").toString());
		if (parentAbs.isEmpty() || ! QFileInfo(parentAbs).isDir())
			return jsonError(400, tr("Invalid parent folder"));
		auto name = sanitizeSegment(obj.value("name").toString(),
		                            op == QLatin1String("mkdir") ? tr("新建文件夹") : tr("新建空白文件.txt"));
		// 同名自动加 (2)
		QString finalName = name;
		int n = 2;
		while (QFileInfo::exists(QDir(parentAbs).filePath(finalName)))
			finalName = QStringLiteral("%1 (%2)").arg(name).arg(n++);
		const auto target = QDir(parentAbs).filePath(finalName);
		const auto rel = QDir(root).relativeFilePath(target);
		if (resolveFsPath(root, rel).isEmpty())
			return jsonError(400, tr("Invalid name"));
		if (op == QLatin1String("mkdir")) {
			if (! QDir().mkpath(target))
				return jsonError(500, tr("Cannot create folder"));
		} else {
			const auto content = obj.value("content").toString();
			if (content.toUtf8().size() > kMaxFsFileBytes)
				return jsonError(413, tr("File too large"));
			QFile f(target);
			if (! f.open(QFile::WriteOnly | QFile::Truncate))
				return jsonError(500, tr("Cannot write file"));
			f.write(content.toUtf8());
		}
		return okJson({{"path", rel}, {"name", finalName}});
	}

	if (op == QLatin1String("write")) {
		const auto abs = resolveFsPath(root, obj.value("path").toString());
		if (abs.isEmpty() || ! QFileInfo(abs).isFile())
			return jsonError(404, tr("File not found"));
		const auto content = obj.value("content").toString();
		if (content.toUtf8().size() > kMaxFsFileBytes)
			return jsonError(413, tr("File too large"));
		QSaveFile f(abs);
		if (! f.open(QFile::WriteOnly | QFile::Truncate))
			return jsonError(500, tr("Cannot write file"));
		f.write(content.toUtf8());
		if (! f.commit())
			return jsonError(500, tr("Cannot save file"));
		return okJson();
	}

	if (op == QLatin1String("rename")) {
		const auto abs = resolveFsPath(root, obj.value("path").toString());
		if (abs.isEmpty() || ! QFileInfo::exists(abs))
			return jsonError(404, tr("Not found"));
		const auto parentAbs = QFileInfo(abs).absolutePath();
		const bool isDir = QFileInfo(abs).isDir();
		auto name = sanitizeSegment(obj.value("name").toString(), isDir ? tr("新建文件夹") : tr("未命名"));
		const auto target = QDir(parentAbs).filePath(name);
		const auto rel = QDir(root).relativeFilePath(target);
		if (resolveFsPath(root, rel).isEmpty())
			return jsonError(400, tr("Invalid name"));
		if (target != abs && QFileInfo::exists(target))
			return jsonError(409, tr("名称已存在"));
		if (! QDir().rename(abs, target))
			return jsonError(500, tr("Cannot rename"));
		return okJson({{"path", rel}, {"name", name}});
	}

	if (op == QLatin1String("delete")) {
		const auto abs = resolveFsPath(root, obj.value("path").toString());
		if (abs.isEmpty() || ! QFileInfo::exists(abs))
			return jsonError(404, tr("Not found"));
		const auto rel = QDir(root).relativeFilePath(abs);
		if (rel.isEmpty() || rel.startsWith(QLatin1String(kFsTrashDirName)))
			return jsonError(400, tr("Invalid path"));
		const auto trashDir = QDir(root).filePath(QLatin1String(kFsTrashDirName));
		QDir().mkpath(trashDir);
		const auto id = QStringLiteral("t%1").arg(QDateTime::currentMSecsSinceEpoch());
		const auto stored = QStringLiteral("%1_%2").arg(id, QFileInfo(abs).fileName());
		const auto storedAbs = QDir(trashDir).filePath(stored);
		const bool isDir = QFileInfo(abs).isDir();
		bool moved = isDir ? QDir().rename(abs, storedAbs) : QFile::rename(abs, storedAbs);
		if (! moved) {
			// 跨设备等情况：退化为递归复制 + 删除
			auto copyRec = [](const QString &from, const QString &to) {
				auto self = [](const QString &f, const QString &t, auto &fn) -> bool {
					if (QFileInfo(f).isDir()) {
						if (! QDir().mkpath(t))
							return false;
						const auto subs =
						    QDir(f).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
						for (const auto &s : subs)
							if (! fn(s.absoluteFilePath(), QDir(t).filePath(s.fileName()), fn))
								return false;
						return true;
					}
					return QFile::copy(f, t);
				};
				return self(from, to, self);
			};
			if (! copyRec(abs, storedAbs))
				return jsonError(500, tr("Cannot move to trash"));
			if (isDir)
				QDir(abs).removeRecursively();
			else
				QFile::remove(abs);
		}
		auto index = readTrashIndex(root);
		QJsonObject entry;
		entry.insert("id", id);
		entry.insert("name", QFileInfo(abs).fileName());
		entry.insert("stored", stored);
		entry.insert("type", isDir ? "folder" : "file");
		entry.insert("deletedAt", QDateTime::currentDateTime().toString(Qt::ISODate));
		index.append(entry);
		writeTrashIndex(root, index);
		return okJson({{"id", id}});
	}

	if (op == QLatin1String("restore")) {
		const auto id = obj.value("id").toString();
		auto index = readTrashIndex(root);
		QJsonArray kept;
		QJsonObject found;
		for (const auto &v : index) {
			const auto e = v.toObject();
			if (e.value("id").toString() == id && found.isEmpty())
				found = e;
			else
				kept.append(e);
		}
		if (found.isEmpty())
			return jsonError(404, tr("Trash entry not found"));
		const auto trashDir = QDir(root).filePath(QLatin1String(kFsTrashDirName));
		const auto storedAbs = QDir(trashDir).filePath(found.value("stored").toString());
		auto name = sanitizeSegment(found.value("name").toString(), tr("未命名"));
		QString finalName = name;
		int n = 2;
		while (QFileInfo::exists(QDir(root).filePath(finalName)))
			finalName = QStringLiteral("%1 (%2)").arg(name).arg(n++);
		const auto target = QDir(root).filePath(finalName);
		if (! QFileInfo::exists(storedAbs))
			return jsonError(404, tr("Trash file missing"));
		if (! QFile::rename(storedAbs, target) && ! QDir().rename(storedAbs, target))
			return jsonError(500, tr("Cannot restore"));
		writeTrashIndex(root, kept);
		return okJson({{"path", finalName}, {"name", finalName}});
	}

	if (op == QLatin1String("emptytrash")) {
		QDir(QDir(root).filePath(QLatin1String(kFsTrashDirName))).removeRecursively();
		QDir().mkpath(QDir(root).filePath(QLatin1String(kFsTrashDirName)));
		writeTrashIndex(root, QJsonArray());
		return okJson();
	}

	return jsonError(400, tr("Unknown op"));
}

QHttpServerResponse SubmissionServer::handleApiFsSample(const QString &name, const QHttpServerRequest &req) {
	QString user;
	if (! requireSession(req, &user))
		return redirect("/login");
	if (! isSafePathComponent(name))
		return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
	const QDir stmtDir(QDir(contestDir_).filePath(QLatin1String(kStatementDirName)));
	auto path = stmtDir.filePath(name);
	if (! QFile::exists(path)) {
		const auto legacy = findStatementFile(contestDir_);
		if (legacy.isEmpty() || QFileInfo(legacy).fileName() != name)
			return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
		path = legacy;
	}
	QFile f(path);
	if (! f.open(QFile::ReadOnly))
		return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
	const auto mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension);
	const bool inlineDisplay = mime.name() == QLatin1String("application/pdf");
	QHttpServerResponse resp(mime.name().toUtf8(), f.readAll());
	QHttpHeaders h = resp.headers();
	h.append(QHttpHeaders::WellKnownHeader::ContentDisposition,
	         QStringLiteral("%1; filename=\"%2\"; filename*=UTF-8''%3")
	             .arg(inlineDisplay ? QStringLiteral("inline") : QStringLiteral("attachment"),
	                  asciiFallbackName(name), encodeRfc5987(name)));
	h.append(QHttpHeaders::WellKnownHeader::CacheControl, "private, max-age=60");
	resp.setHeaders(h);
	return resp;
}

bool SubmissionServer::writeSubmission(const QString &username, int taskIndex, const QByteArray &source,
                                       const QString &extension, QString *errOut) {
	if (! contest_)
		return false;
	const auto *task = contest_->getTaskList().at(taskIndex);
	const auto base = task->getSourceFileName();
	if (base.isEmpty()) {
		if (errOut)
			*errOut = tr("Task %1 has empty source file name").arg(taskIndex);
		return false;
	}

	// directory layout: <contestDir>/source/<username>/[<base>/]<base>.<ext>
	const QString sourceRoot = QDir(contestDir_).filePath(QStringLiteral("source/%1").arg(username));
	QDir().mkpath(sourceRoot);

	QString outFile;
	if (task->getSubFolderCheck()) {
		const QString sub = QDir(sourceRoot).filePath(base);
		QDir().mkpath(sub);
		outFile = QDir(sub).filePath(base + QStringLiteral(".") + extension);
	} else {
		outFile = QDir(sourceRoot).filePath(base + QStringLiteral(".") + extension);
	}

	QSaveFile f(outFile);
	if (! f.open(QFile::WriteOnly | QFile::Truncate)) {
		if (errOut)
			*errOut = f.errorString();
		return false;
	}
	if (f.write(source) != source.size()) {
		if (errOut)
			*errOut = f.errorString();
		return false;
	}
	if (! f.commit()) {
		if (errOut)
			*errOut = f.errorString();
		return false;
	}
	return true;
}

void SubmissionServer::appendAuditLog(const QString &username, int taskIndex, qint64 bytes,
                                      const QString &sha256) {
	QFile f(QDir(contestDir_).filePath(kAuditLogName));
	if (! f.open(QFile::Append | QFile::Text))
		return;
	const auto line = QStringLiteral("%1\t%2\ttask=%3\tbytes=%4\tsha256=%5\n")
	                      .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
	                      .arg(username)
	                      .arg(taskIndex)
	                      .arg(bytes)
	                      .arg(sha256);
	f.write(line.toUtf8());
}
