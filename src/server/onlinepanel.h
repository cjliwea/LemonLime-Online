/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QObject>
#include <QPointer>

class Contest;
class SubmissionServer;
class QComboBox;
class QCheckBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QWidget;

// 在线服务的「面板化」载体：把原 OnlineServerDialog 的服务/比赛/账号/日志四块内容
// 直接构建进主窗口的标签页（比赛设置 / 账号 / 公告须知 / 实时状况 / 日志），
// 并新增公告广播、开考须知、实时状况与默认答题界面设置。
// SubmissionServer 由本类持有，主窗口经 server() 访问。
class OnlinePanel : public QObject {
	Q_OBJECT
  public:
	explicit OnlinePanel(QWidget *settingsPage, QWidget *accountsPage, QWidget *noticePage,
	                     QWidget *livePage, QWidget *logsPage, QObject *parent = nullptr);
	~OnlinePanel() override;

	SubmissionServer *server() const { return server_; }

	void bindContest(Contest *contest, const QString &contestDir);

	// 供主窗口状态栏使用
	QString serviceUrl() const { return serviceUrl_; }

  public slots:
	void toggleServer();
	void refreshLive();

  private:
	void buildSettingsPage(QWidget *page);
	void buildAccountsPage(QWidget *page);
	void buildNoticePage(QWidget *page);
	void buildLivePage(QWidget *page);
	void buildLogsPage(QWidget *page);

	QWidget *buildListenGroup(QWidget *parent);
	QWidget *buildContestInfoGroup(QWidget *parent);
	QWidget *buildJudgeGroup(QWidget *parent);
	QWidget *buildStatementGroup(QWidget *parent);
	QWidget *buildUiModeGroup(QWidget *parent);

	void onStartStop();
	void refreshServerUi();
	void appendLog(const QString &msg);
	QString detectLocalIp() const;

	// users
	void onGenerateUsers();
	void onAddUser();
	void onRemoveUser();
	void onExportCsv();
	void onImportCsv();
	void onSavePlaintextList();
	void refreshUsersTable();
	void refreshOnlineTable();

	// statement
	void onSetStatement();
	void onClearStatement();
	void refreshStatementHint();

	// contest window
	void onApplyContestWindow();
	void refreshContestWindow();

	// notice
	void onBroadcastAnnouncement();
	void onClearAnnouncement();
	void onSaveNotice();

	SubmissionServer *server_ = nullptr;

	QWidget *settingsPage_{};
	QWidget *accountsPage_{};
	QWidget *noticePage_{};
	QWidget *livePage_{};
	QWidget *logsPage_{};

	QPointer<Contest> contest_;
	QString contestDir_;
	QString serviceUrl_;

	// listen group
	QComboBox *bindCombo_{};
	QSpinBox *portSpin_{};
	QPushButton *startStopBtn_{};
	QLabel *statusLabel_{};
	QLabel *urlLabel_{};
	QPushButton *copyUrlBtn_{};
	QPushButton *openBrowserBtn_{};

	// contest info group
	QLabel *contestTitleLabel_{};
	QCheckBox *windowEnableBox_{};
	QDateTimeEdit *startEdit_{};
	QDateTimeEdit *endEdit_{};
	QPushButton *applyWindowBtn_{};
	QLabel *windowStatusLabel_{};

	// judge group
	QCheckBox *autoJudgeBox_{};

	// statement group
	QLabel *statementLabel_{};
	QPushButton *setStatementBtn_{};
	QPushButton *clearStatementBtn_{};

	// ui mode group
	QCheckBox *uiModeEditor_{};
	QCheckBox *uiModeUpload_{};
	QCheckBox *uiModeBoth_{};

	// accounts page
	QSpinBox *genCountSpin_{};
	QLineEdit *genPrefixEdit_{};
	QPushButton *genBtn_{};
	QLineEdit *addNameEdit_{};
	QLineEdit *addPwdEdit_{};
	QTableWidget *usersTable_{};

	// notice page
	QPlainTextEdit *annText_{};
	QListWidget *annHistory_{};
	QPlainTextEdit *noticeEdit_{};

	// live page
	QLabel *metricOnline_{};
	QLabel *metricSubmissions_{};
	QLabel *metricState_{};
	QTableWidget *onlineTable_{};
	QListWidget *feedList_{};

	// logs page
	QPlainTextEdit *logView_{};
};
