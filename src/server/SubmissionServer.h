/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QHttpServer;
class QTcpServer;
class Contest;
class UserStore;
class SessionManager;
class QJsonArray;
class QHttpServerRequest;
class QHttpServerResponse;

class SubmissionServer : public QObject {
	Q_OBJECT
  public:
	explicit SubmissionServer(QObject *parent = nullptr);
	~SubmissionServer() override;

	void bindContest(Contest *contest, const QString &contestDir);
	UserStore *userStore() const { return userStore_.data(); }
	QString contestDir() const { return contestDir_; }

	// 当前下发给学生的题面及样例文件路径（statements/ 下的第一个文件，或旧的
	// statement.pdf）；未设定时返回空串。
	static QString findStatementFile(const QString &contestDir);

	bool start(const QHostAddress &addr, quint16 port, QString *errorOut = nullptr);
	void stop();
	bool isRunning() const { return running_; }
	quint16 port() const { return boundPort_; }
	QHostAddress address() const { return boundAddress_; }

	void setMaxSourceBytes(int n) { maxSourceBytes_ = n; }

	bool windowEnabled() const { return windowEnabled_; }
	QDateTime startTime() const { return startTime_; }
	QDateTime endTime() const { return endTime_; }
	void setContestWindow(bool enabled, const QDateTime &start, const QDateTime &end);

	bool autoJudge() const { return autoJudge_; }
	void setAutoJudge(bool on);

	// ---- 公告与须知（主窗口面板直接下发；学生端经 /api/announce 轮询） ----
	QString announcement() const { return announcement_; }
	void setAnnouncement(const QString &text);
	QString notice() const { return notice_; }
	void setNotice(const QString &text);

	// 默认答题界面："editor" / "upload" / "both"
	QString defaultUiMode() const { return defaultUiMode_; }
	void setDefaultUiMode(const QString &mode);

	// ---- 实时状况 ----
	int submissionCount() const { return submissionCount_; }
	int onlineCount() const;
	QStringList onlineUsernames() const;
	QDateTime lastSeenOf(const QString &username) const;
	// 强制某用户下线（销毁其全部会话）
	void forceLogout(const QString &username);

	bool loadConfig();
	bool saveConfig() const;

  signals:
	void started(QHostAddress addr, quint16 port);
	void stopped();
	void submissionReceived(QString username, QString taskName, int bytes);
	void logMessage(QString msg);
	// 在线人数（去重用户数）变化，由 SessionManager 转发
	void onlineCountChanged(int count);

  private:
	void setupRoutes();
	QHttpServerResponse handleStatic(const QString &resourcePath, const QString &contentType);
	QHttpServerResponse handleLoginPage();
	QHttpServerResponse handleLoginPost(const QHttpServerRequest &req);
	QHttpServerResponse handleLogout(const QHttpServerRequest &req);
	QHttpServerResponse handleIndex(const QHttpServerRequest &req);
	QHttpServerResponse handleSubmitPage(qint32 taskId, const QHttpServerRequest &req);
	QHttpServerResponse handleApiTasks(const QHttpServerRequest &req);
	QHttpServerResponse handleApiSubmit(qint32 taskId, const QHttpServerRequest &req);
	QHttpServerResponse handleApiUploadFolder(const QHttpServerRequest &req);
	QHttpServerResponse handleApiUploadSource(qint32 taskId, const QHttpServerRequest &req);
	QHttpServerResponse handleApiAnnounce(const QHttpServerRequest &req);
	QHttpServerResponse handleStatement(const QHttpServerRequest &req);

	QString sessionUser(const QHttpServerRequest &req) const;
	bool requireSession(const QHttpServerRequest &req, QString *user) const;
	QHttpServerResponse redirect(const QString &location, const QString &setCookie = {}) const;
	QHttpServerResponse jsonError(int httpStatus, const QString &message) const;

	bool writeSubmission(const QString &username, int taskIndex, const QByteArray &source,
	                     const QString &extension, QString *errOut);
	void appendAuditLog(const QString &username, int taskIndex, qint64 bytes, const QString &sha256);

	// 整包上传：把选手文件夹原样镜像到 <contestDir>/source/<contestant>/
	bool writeContestantFolder(const QString &contestant, const QJsonArray &files,
	                           QJsonArray *resultsOut, QString *errOut);
	void appendFolderAuditLog(const QString &loginUser, const QString &contestant, int fileCount,
	                          qint64 bytes);
	void triggerJudgeContestant(const QString &contestant);

	QHttpServer *http_ = nullptr;
	QTcpServer *tcp_ = nullptr;
	QPointer<Contest> contest_;
	QString contestDir_;
	QPointer<UserStore> userStore_;
	QPointer<SessionManager> sessions_;
	QHostAddress boundAddress_;
	quint16 boundPort_ = 0;
	bool running_ = false;
	int maxSourceBytes_ = 64 * 1024;
	int maxFolderFiles_ = 512;
	qint64 maxFolderBytes_ = 8 * 1024 * 1024;

	bool windowEnabled_ = false;
	QDateTime startTime_;
	QDateTime endTime_;

	bool autoJudge_ = true;

	QString announcement_;
	QString notice_;
	QString defaultUiMode_ = QStringLiteral("both");
	int submissionCount_ = 0;
};
