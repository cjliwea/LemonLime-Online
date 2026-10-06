/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "onlinepanel.h"

#include "SubmissionServer.h"
#include "UserStore.h"
#include "core/contest.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSet>
#include <QSpinBox>
#include <QStringConverter>
#include <QStyle>
#include <QTableWidget>
#include <QTcpServer>
#include <QTime>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {
const char *kPanelQss =
    "QGroupBox { border: 1px solid #E5E7EB; border-radius: 10px; "
    "  margin-top: 4px; padding: 24px 12px 8px 12px; background: #FFFFFF; }"
    "QGroupBox::title { subcontrol-origin: border; subcontrol-position: top left; "
    "  left: 12px; top: 4px; padding: 0 4px; color: #475569; font-weight: 600; }"
    "QPushButton { padding: 6px 14px; border: 1px solid #D4D4D8; "
    "  border-radius: 6px; background: #FFFFFF; }"
    "QPushButton:hover { background: #F4F4F5; }"
    "QPushButton:disabled { color: #A1A1AA; background: #FAFAFA; }"
    "QPushButton#PrimaryBtn { background: #84CC16; color: #1A2E05; "
    "  border: 1px solid #65A30D; font-weight: 600; }"
    "QPushButton#PrimaryBtn:hover { background: #65A30D; color: #FFFFFF; }"
    "QPushButton#DangerBtn { background: #FEE2E2; color: #991B1B; "
    "  border: 1px solid #FCA5A5; font-weight: 600; }"
    "QPushButton#DangerBtn:hover { background: #FECACA; }"
    "QLineEdit, QComboBox, QSpinBox, QDateTimeEdit, QPlainTextEdit { "
    "  padding: 5px 8px; border: 1px solid #D4D4D8; border-radius: 6px; "
    "  background: #FFFFFF; }"
    "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDateTimeEdit:focus { "
    "  border-color: #84CC16; }"
    "QTableWidget, QListWidget { border: 1px solid #E5E7EB; border-radius: 6px; "
    "  gridline-color: #F1F5F9; background: #FFFFFF; }"
    "QHeaderView::section { background: #F8FAFC; padding: 6px; "
    "  border: 0; border-bottom: 1px solid #E5E7EB; color: #475569; }"
    "QLabel#MetricValue { font-size: 21px; font-weight: 600; color: #0F172A; }"
    "QLabel#MetricLabel { font-size: 11px; color: #94A3B8; }"
    "QLabel#Hint { color: #94A3B8; font-size: 11px; }";
} // namespace

OnlinePanel::OnlinePanel(QWidget *settingsPage, QWidget *accountsPage, QWidget *noticePage,
                         QWidget *livePage, QWidget *logsPage, QObject *parent)
    : QObject(parent), settingsPage_(settingsPage), accountsPage_(accountsPage),
      noticePage_(noticePage), livePage_(livePage), logsPage_(logsPage) {
	server_ = new SubmissionServer(this);
	connect(server_, &SubmissionServer::logMessage, this, &OnlinePanel::appendLog);
	connect(server_, &SubmissionServer::submissionReceived, this,
	        [this](const QString &user, const QString &task, int bytes) {
		        if (feedList_) {
			        auto *item = new QListWidgetItem(
			            QTime::currentTime().toString("hh:mm:ss") + "  " + user + " 提交 " + task +
			            "（" + QString::number(bytes) + " 字节）");
			        feedList_->insertItem(0, item);
			        while (feedList_->count() > 100)
				        delete feedList_->takeItem(feedList_->count() - 1);
		        }
		        refreshUsersTable();
		        refreshLive();
	        });
	connect(server_, &SubmissionServer::onlineCountChanged, this, [this](int) {
		refreshLive();
		refreshOnlineTable();
		refreshUsersTable(); // 账号表的「状态」列随登录/下线刷新
	});

	buildSettingsPage(settingsPage_);
	buildAccountsPage(accountsPage_);
	buildNoticePage(noticePage_);
	buildLivePage(livePage_);
	buildLogsPage(logsPage_);
}

OnlinePanel::~OnlinePanel() { server_->stop(); }

void OnlinePanel::bindContest(Contest *contest, const QString &contestDir) {
	contest_ = contest;
	contestDir_ = contestDir;
	server_->bindContest(contest, contestDir);
	refreshUsersTable();
	refreshStatementHint();
	refreshContestWindow();
	if (contestTitleLabel_)
		contestTitleLabel_->setText(contest ? contest->getContestTitle()
		                                    : tr("（尚未打开比赛）"));
	// 已持久化的开考须知回填到编辑框
	if (noticeEdit_)
		noticeEdit_->setPlainText(server_->notice());
	refreshLive();
	refreshOnlineTable();
}

// ---------------------------------------------------------------- settings page

void OnlinePanel::buildSettingsPage(QWidget *page) {
	page->setStyleSheet(QLatin1String(kPanelQss));
	// 页面在 .ui 里已带 QVBoxLayout：直接 setLayout 会被 Qt 静默拒绝、内容全部不可见，
	// 必须把内容装进容器再挂到页面已有布局
	auto *host = new QWidget(page);
	auto *layout = new QGridLayout(host);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(12);

	layout->addWidget(buildContestInfoGroup(page), 0, 0);
	layout->addWidget(buildListenGroup(page), 0, 1);
	layout->addWidget(buildUiModeGroup(page), 1, 0);
	layout->addWidget(buildJudgeGroup(page), 1, 1);
	layout->addWidget(buildStatementGroup(page), 2, 0, 1, 2);
	layout->setRowStretch(2, 1);
	layout->setColumnStretch(0, 1);
	layout->setColumnStretch(1, 1);
	if (auto *box = qobject_cast<QBoxLayout *>(page->layout()))
		box->addWidget(host, 1);
}

QWidget *OnlinePanel::buildListenGroup(QWidget *parent) {
	auto *listenBox = new QGroupBox(tr("在线服务"), parent);
	auto *listenForm = new QFormLayout(listenBox);
	listenForm->setHorizontalSpacing(12);
	listenForm->setVerticalSpacing(8);

	bindCombo_ = new QComboBox(listenBox);
	bindCombo_->addItem(tr("所有网卡 (0.0.0.0)"), QStringLiteral("0.0.0.0"));
	bindCombo_->addItem(tr("仅本机 (127.0.0.1)"), QStringLiteral("127.0.0.1"));
	const auto addrs = QNetworkInterface::allAddresses();
	for (const auto &a : addrs) {
		if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
			bindCombo_->addItem(a.toString(), a.toString());
	}
	portSpin_ = new QSpinBox(listenBox);
	portSpin_->setRange(1024, 65535);
	portSpin_->setValue(8080);
	startStopBtn_ = new QPushButton(tr("启动服务"), listenBox);
	startStopBtn_->setObjectName(QStringLiteral("PrimaryBtn"));
	connect(startStopBtn_, &QPushButton::clicked, this, &OnlinePanel::toggleServer);

	listenForm->addRow(tr("监听网卡"), bindCombo_);
	listenForm->addRow(tr("端口"), portSpin_);
	auto *actionRow = new QHBoxLayout();
	actionRow->addWidget(startStopBtn_);
	actionRow->addStretch();
	listenForm->addRow(QString(), actionRow);

	statusLabel_ = new QLabel(tr("已停止"), listenBox);
	listenForm->addRow(tr("状态"), statusLabel_);

	urlLabel_ = new QLabel(QStringLiteral("—"), listenBox);
	urlLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

	copyUrlBtn_ = new QPushButton(tr("复制"), listenBox);
	openBrowserBtn_ = new QPushButton(tr("用浏览器打开"), listenBox);
	copyUrlBtn_->setEnabled(false);
	openBrowserBtn_->setEnabled(false);
	connect(copyUrlBtn_, &QPushButton::clicked, this, [this]() {
		QGuiApplication::clipboard()->setText(serviceUrl_);
		appendLog(tr("已复制访问地址：%1").arg(serviceUrl_));
	});
	connect(openBrowserBtn_, &QPushButton::clicked, this,
	        [this]() { QDesktopServices::openUrl(QUrl(serviceUrl_)); });

	auto *urlRow = new QHBoxLayout();
	urlRow->addWidget(urlLabel_, 1);
	urlRow->addWidget(copyUrlBtn_);
	urlRow->addWidget(openBrowserBtn_);
	listenForm->addRow(tr("访问地址"), urlRow);

	return listenBox;
}

QWidget *OnlinePanel::buildContestInfoGroup(QWidget *parent) {
	auto *infoBox = new QGroupBox(tr("比赛信息"), parent);
	auto *form = new QFormLayout(infoBox);
	form->setHorizontalSpacing(12);
	form->setVerticalSpacing(8);

	contestTitleLabel_ = new QLabel(tr("（尚未打开比赛）"), infoBox);
	form->addRow(tr("比赛标题"), contestTitleLabel_);

	windowEnableBox_ = new QCheckBox(tr("启用比赛时间限制（窗口外不允许提交）"), infoBox);
	form->addRow(QString(), windowEnableBox_);

	startEdit_ = new QDateTimeEdit(QDateTime::currentDateTime(), infoBox);
	startEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
	startEdit_->setCalendarPopup(true);
	endEdit_ = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3 * 3600), infoBox);
	endEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
	endEdit_->setCalendarPopup(true);
	form->addRow(tr("开始时间"), startEdit_);
	form->addRow(tr("结束时间"), endEdit_);

	applyWindowBtn_ = new QPushButton(tr("保存比赛时间"), infoBox);
	applyWindowBtn_->setObjectName(QStringLiteral("PrimaryBtn"));
	connect(applyWindowBtn_, &QPushButton::clicked, this, &OnlinePanel::onApplyContestWindow);
	connect(windowEnableBox_, &QCheckBox::toggled, this, [this](bool on) {
		startEdit_->setEnabled(on);
		endEdit_->setEnabled(on);
	});
	auto *applyRow = new QHBoxLayout();
	applyRow->addWidget(applyWindowBtn_);
	applyRow->addStretch();
	form->addRow(QString(), applyRow);

	windowStatusLabel_ = new QLabel(infoBox);
	form->addRow(tr("当前"), windowStatusLabel_);

	return infoBox;
}

QWidget *OnlinePanel::buildJudgeGroup(QWidget *parent) {
	auto *judgeBox = new QGroupBox(tr("评测"), parent);
	auto *judgeLayout = new QVBoxLayout(judgeBox);
	autoJudgeBox_ = new QCheckBox(tr("学生提交后立即在本机评测"), judgeBox);
	autoJudgeBox_->setChecked(true);
	connect(autoJudgeBox_, &QCheckBox::toggled, this, [this](bool on) {
		if (server_) {
			server_->setAutoJudge(on);
			appendLog(on ? tr("已开启提交后自动评测") : tr("已关闭提交后自动评测"));
		}
	});
	judgeLayout->addWidget(autoJudgeBox_);
	auto *hint = new QLabel(
	    tr("关闭时为手动评测：学生提交只写入 source 目录，老师在主窗口手动点评测。"),
	    judgeBox);
	hint->setObjectName(QStringLiteral("Hint"));
	hint->setWordWrap(true);
	judgeLayout->addWidget(hint);
	judgeLayout->addStretch(1);
	return judgeBox;
}

QWidget *OnlinePanel::buildUiModeGroup(QWidget *parent) {
	auto *modeBox = new QGroupBox(tr("默认答题界面"), parent);
	auto *modeLayout = new QVBoxLayout(modeBox);

	const QString hintBase = tr("学生进入提交页时默认停留的界面；选双模式时学生可自行切换。");
	uiModeEditor_ = new QCheckBox(tr("代码编辑器"), modeBox);
	uiModeUpload_ = new QCheckBox(tr("上传文件"), modeBox);
	uiModeBoth_ = new QCheckBox(tr("双模式（学生自选）"), modeBox);
	uiModeBoth_->setChecked(true);

	auto sync = [this]() {
		QString mode = QStringLiteral("both");
		if (uiModeEditor_->isChecked())
			mode = QStringLiteral("editor");
		else if (uiModeUpload_->isChecked())
			mode = QStringLiteral("upload");
		server_->setDefaultUiMode(mode);
		appendLog(tr("默认答题界面已设为：%1").arg(mode));
	};
	// 三选一互斥
	connect(uiModeEditor_, &QCheckBox::toggled, this, [this, sync](bool on) {
		if (on) {
			uiModeUpload_->blockSignals(true);
			uiModeBoth_->blockSignals(true);
			uiModeUpload_->setChecked(false);
			uiModeBoth_->setChecked(false);
			uiModeUpload_->blockSignals(false);
			uiModeBoth_->blockSignals(false);
			sync();
		}
	});
	connect(uiModeUpload_, &QCheckBox::toggled, this, [this, sync](bool on) {
		if (on) {
			uiModeEditor_->blockSignals(true);
			uiModeBoth_->blockSignals(true);
			uiModeEditor_->setChecked(false);
			uiModeBoth_->setChecked(false);
			uiModeEditor_->blockSignals(false);
			uiModeBoth_->blockSignals(false);
			sync();
		}
	});
	connect(uiModeBoth_, &QCheckBox::toggled, this, [this, sync](bool on) {
		if (on) {
			uiModeEditor_->blockSignals(true);
			uiModeUpload_->blockSignals(true);
			uiModeEditor_->setChecked(false);
			uiModeUpload_->setChecked(false);
			uiModeEditor_->blockSignals(false);
			uiModeUpload_->blockSignals(false);
			sync();
		}
	});

	modeLayout->addWidget(uiModeEditor_);
	modeLayout->addWidget(uiModeUpload_);
	modeLayout->addWidget(uiModeBoth_);
	auto *hint = new QLabel(hintBase, modeBox);
	hint->setObjectName(QStringLiteral("Hint"));
	hint->setWordWrap(true);
	modeLayout->addWidget(hint);
	modeLayout->addStretch(1);
	return modeBox;
}

QWidget *OnlinePanel::buildStatementGroup(QWidget *parent) {
	auto *pdfBox = new QGroupBox(tr("题面及样例"), parent);
	auto *pdfLayout = new QVBoxLayout(pdfBox);
	pdfLayout->setSpacing(8);

	statementLabel_ = new QLabel(pdfBox);
	statementLabel_->setWordWrap(true);
	statementLabel_->setStyleSheet(QStringLiteral("color: #475569;"));

	setStatementBtn_ = new QPushButton(tr("选择文件..."), pdfBox);
	clearStatementBtn_ = new QPushButton(tr("清除"), pdfBox);
	connect(setStatementBtn_, &QPushButton::clicked, this, &OnlinePanel::onSetStatement);
	connect(clearStatementBtn_, &QPushButton::clicked, this, &OnlinePanel::onClearStatement);

	auto *pdfRow = new QHBoxLayout();
	pdfRow->addWidget(statementLabel_, 1);
	pdfRow->addWidget(setStatementBtn_);
	pdfRow->addWidget(clearStatementBtn_);
	pdfLayout->addLayout(pdfRow);
	auto *hint = new QLabel(
	    tr("题面 PDF、大样例压缩包或任意单个文件均可在此下发，会原样提供给学生在“题面下载”处获取；重复选择会替换已下发的文件。"),
	    pdfBox);
	hint->setObjectName(QStringLiteral("Hint"));
	pdfLayout->addWidget(hint);

	return pdfBox;
}

// ---------------------------------------------------------------- accounts page

void OnlinePanel::buildAccountsPage(QWidget *page) {
	page->setStyleSheet(QLatin1String(kPanelQss));
	auto *host = new QWidget(page); // 同 settings：挂进页面已有布局，勿直接 setLayout
	auto *layout = new QVBoxLayout(host);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(12);

	// --- Generate
	auto *genBox = new QGroupBox(tr("批量生成"), page);
	auto *genLayout = new QHBoxLayout(genBox);
	genCountSpin_ = new QSpinBox(genBox);
	genCountSpin_->setRange(1, 500);
	genCountSpin_->setValue(30);
	genPrefixEdit_ = new QLineEdit(genBox);
	genPrefixEdit_->setPlaceholderText(tr("用户名前缀，如 s2026_"));
	genPrefixEdit_->setText(QStringLiteral("s_"));
	genBtn_ = new QPushButton(tr("批量生成"), genBox);
	genBtn_->setObjectName(QStringLiteral("PrimaryBtn"));
	connect(genBtn_, &QPushButton::clicked, this, &OnlinePanel::onGenerateUsers);
	genLayout->addWidget(new QLabel(tr("人数")));
	genLayout->addWidget(genCountSpin_);
	genLayout->addSpacing(12);
	genLayout->addWidget(new QLabel(tr("前缀")));
	genLayout->addWidget(genPrefixEdit_, 1);
	genLayout->addWidget(genBtn_);

	// --- Import / Export / Single add row
	auto *toolBox = new QGroupBox(tr("导入 / 导出 / 添加 / 删除"), page);
	auto *toolLayout = new QVBoxLayout(toolBox);
	toolLayout->setSpacing(8);

	auto *ioRow = new QHBoxLayout();
	auto *importBtn = new QPushButton(tr("从 CSV 导入..."), toolBox);
	auto *exportBtn = new QPushButton(tr("导出 CSV"), toolBox);
	connect(importBtn, &QPushButton::clicked, this, &OnlinePanel::onImportCsv);
	connect(exportBtn, &QPushButton::clicked, this, &OnlinePanel::onExportCsv);
	ioRow->addWidget(importBtn);
	ioRow->addWidget(exportBtn);
	ioRow->addStretch();
	toolLayout->addLayout(ioRow);

	auto *addRow = new QHBoxLayout();
	addNameEdit_ = new QLineEdit(toolBox);
	addNameEdit_->setPlaceholderText(tr("用户名"));
	addPwdEdit_ = new QLineEdit(toolBox);
	addPwdEdit_->setPlaceholderText(tr("密码"));
	auto *addBtn = new QPushButton(tr("添加"), toolBox);
	auto *removeBtn = new QPushButton(tr("删除所选"), toolBox);
	connect(addBtn, &QPushButton::clicked, this, &OnlinePanel::onAddUser);
	connect(removeBtn, &QPushButton::clicked, this, &OnlinePanel::onRemoveUser);
	addRow->addWidget(addNameEdit_, 1);
	addRow->addWidget(addPwdEdit_, 1);
	addRow->addWidget(addBtn);
	addRow->addWidget(removeBtn);
	toolLayout->addLayout(addRow);

	// --- Table
	usersTable_ = new QTableWidget(0, 5, page);
	usersTable_->setHorizontalHeaderLabels(
	    {tr("用户名"), tr("显示名"), tr("密码"), tr("状态"), tr("上次登录")});
	usersTable_->horizontalHeader()->setStretchLastSection(true);
	usersTable_->verticalHeader()->setVisible(false);
	usersTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
	usersTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	usersTable_->setAlternatingRowColors(true);
	usersTable_->setStyleSheet(QStringLiteral("alternate-background-color: #FAFAFA;"));

	layout->addWidget(genBox);
	layout->addWidget(toolBox);
	layout->addWidget(usersTable_, 1);
	if (auto *box = qobject_cast<QBoxLayout *>(page->layout()))
		box->addWidget(host, 1);
}

// ---------------------------------------------------------------- notice page

void OnlinePanel::buildNoticePage(QWidget *page) {
	page->setStyleSheet(QLatin1String(kPanelQss));
	auto *host = new QWidget(page); // 同 settings：挂进页面已有布局
	auto *layout = new QGridLayout(host);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(12);

	// --- 公告（即时广播，学生端 15 秒内可见）
	auto *annBox = new QGroupBox(tr("公告（即时广播）"), page);
	auto *annLayout = new QVBoxLayout(annBox);
	annText_ = new QPlainTextEdit(annBox);
	annText_->setPlaceholderText(tr("输入公告内容，例如：距离比赛结束还有 30 分钟…"));
	annText_->setFixedHeight(96);
	annLayout->addWidget(annText_);
	auto *annBtnRow = new QHBoxLayout();
	auto *broadcastBtn = new QPushButton(tr("广播公告"), annBox);
	broadcastBtn->setObjectName(QStringLiteral("PrimaryBtn"));
	connect(broadcastBtn, &QPushButton::clicked, this, &OnlinePanel::onBroadcastAnnouncement);
	auto *clearAnnBtn = new QPushButton(tr("清除公告"), annBox);
	connect(clearAnnBtn, &QPushButton::clicked, this, &OnlinePanel::onClearAnnouncement);
	annBtnRow->addWidget(broadcastBtn);
	annBtnRow->addWidget(clearAnnBtn);
	annBtnRow->addStretch();
	annLayout->addLayout(annBtnRow);
	auto *annHint = new QLabel(
	    tr("将以横幅显示在所有已登录学生的页面顶部（15 秒内可见）。"),
	    annBox);
	annHint->setObjectName(QStringLiteral("Hint"));
	annHint->setWordWrap(true);
	annLayout->addWidget(annHint);

	// --- 历史
	auto *historyBox = new QGroupBox(tr("历史公告"), page);
	auto *historyLayout = new QVBoxLayout(historyBox);
	annHistory_ = new QListWidget(historyBox);
	historyLayout->addWidget(annHistory_);

	// --- 开考须知（常驻）
	auto *noticeBox = new QGroupBox(tr("开考须知（常驻显示）"), page);
	auto *noticeLayout = new QVBoxLayout(noticeBox);
	noticeEdit_ = new QPlainTextEdit(noticeBox);
	noticeEdit_->setPlaceholderText(
	    tr("常驻显示在所有学生页面的规则说明，例如：\"保持安静，独立作答…\""));
	noticeLayout->addWidget(noticeEdit_);
	auto *noticeBtnRow = new QHBoxLayout();
	auto *saveNoticeBtn = new QPushButton(tr("保存须知"), noticeBox);
	saveNoticeBtn->setObjectName(QStringLiteral("PrimaryBtn"));
	connect(saveNoticeBtn, &QPushButton::clicked, this, &OnlinePanel::onSaveNotice);
	auto *clearNoticeBtn = new QPushButton(tr("清除须知"), noticeBox);
	connect(clearNoticeBtn, &QPushButton::clicked, this, [this]() {
		noticeEdit_->clear();
		onSaveNotice();
	});
	noticeBtnRow->addWidget(saveNoticeBtn);
	noticeBtnRow->addWidget(clearNoticeBtn);
	noticeBtnRow->addStretch();
	noticeLayout->addLayout(noticeBtnRow);

	layout->addWidget(annBox, 0, 0);
	layout->addWidget(historyBox, 1, 0);
	layout->addWidget(noticeBox, 0, 1, 2, 1);
	layout->setColumnStretch(0, 1);
	layout->setColumnStretch(1, 1);
	layout->setRowStretch(1, 1);
	if (auto *box = qobject_cast<QBoxLayout *>(page->layout()))
		box->addWidget(host, 1);
}

// ---------------------------------------------------------------- live page

void OnlinePanel::buildLivePage(QWidget *page) {
	page->setStyleSheet(QLatin1String(kPanelQss));
	auto *host = new QWidget(page); // 同 settings：挂进页面已有布局
	auto *layout = new QVBoxLayout(host);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(12);

	// --- metrics
	auto *metricRow = new QHBoxLayout();
	metricRow->setSpacing(12);
	auto makeMetric = [this, metricRow](QLabel **value, const QString &title) {
		auto *frame = new QFrame(this->livePage_);
		frame->setObjectName(QStringLiteral("metricFrame"));
		frame->setStyleSheet(
		    QStringLiteral("QFrame#metricFrame { background: #FFFFFF; border: 1px solid #E5E7EB; "
		                   "border-radius: 10px; }"));
		auto *v = new QVBoxLayout(frame);
		auto *l = new QLabel(title, frame);
		l->setObjectName(QStringLiteral("MetricLabel"));
		*value = new QLabel(QStringLiteral("0"), frame);
		(*value)->setObjectName(QStringLiteral("MetricValue"));
		v->addWidget(l);
		v->addWidget(*value);
		metricRow->addWidget(frame, 1);
	};
	makeMetric(&metricOnline_, tr("当前在线"));
	makeMetric(&metricSubmissions_, tr("提交总数"));
	makeMetric(&metricState_, tr("服务状态"));
	layout->addLayout(metricRow);

	// --- online table + feed
	auto *splitRow = new QHBoxLayout();
	splitRow->setSpacing(12);

	auto *onlineBox = new QGroupBox(tr("在线学生"), page);
	auto *onlineLayout = new QVBoxLayout(onlineBox);
	onlineTable_ = new QTableWidget(0, 3, onlineBox);
	onlineTable_->setHorizontalHeaderLabels(
	    {tr("用户名"), tr("显示名"), tr("最近活跃")});
	onlineTable_->horizontalHeader()->setStretchLastSection(true);
	onlineTable_->verticalHeader()->setVisible(false);
	onlineTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
	onlineTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	onlineTable_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(onlineTable_, &QTableWidget::customContextMenuRequested, this,
	        [this](const QPoint &pos) {
		        const auto rows = onlineTable_->selectionModel()->selectedRows();
		        if (rows.isEmpty())
			        return;
		        const auto name = onlineTable_->item(rows.first().row(), 0)->text();
		        QMenu menu(onlineTable_);
		        QAction *kick = menu.addAction(tr("强制下线"));
		        QAction *chosen = menu.exec(onlineTable_->viewport()->mapToGlobal(pos));
		        if (chosen == kick) {
			        // 注销该用户全部会话；学生下次请求回到登录页
			        server_->forceLogout(name);
			        appendLog(tr("用户 %1 已被强制下线").arg(name));
		        }
	        });
	onlineLayout->addWidget(onlineTable_);
	splitRow->addWidget(onlineBox, 3);

	auto *feedBox = new QGroupBox(tr("提交动态"), page);
	auto *feedLayout = new QVBoxLayout(feedBox);
	feedList_ = new QListWidget(feedBox);
	feedLayout->addWidget(feedList_);
	splitRow->addWidget(feedBox, 2);

	layout->addLayout(splitRow, 1);
	if (auto *box = qobject_cast<QBoxLayout *>(page->layout()))
		box->addWidget(host, 1);
}

// ---------------------------------------------------------------- logs page

void OnlinePanel::buildLogsPage(QWidget *page) {
	page->setStyleSheet(QLatin1String(kPanelQss));
	auto *host = new QWidget(page); // 同 settings：挂进页面已有布局
	auto *layout = new QVBoxLayout(host);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);
	logView_ = new QPlainTextEdit(page);
	logView_->setReadOnly(true);
	logView_->setMaximumBlockCount(2000);
	layout->addWidget(logView_);
	auto *clearBtn = new QPushButton(tr("清空日志"), page);
	connect(clearBtn, &QPushButton::clicked, logView_, &QPlainTextEdit::clear);
	auto *btnRow = new QHBoxLayout();
	btnRow->addStretch();
	btnRow->addWidget(clearBtn);
	layout->addLayout(btnRow);
	if (auto *box = qobject_cast<QBoxLayout *>(page->layout()))
		box->addWidget(host, 1);
}

// ---------------------------------------------------------------- server control

// 探测 addr:port 当前是否可以监听（未被占用）
static bool portAvailable(const QHostAddress &addr, quint16 port) {
	QTcpServer probe;
	const bool ok = probe.listen(addr, port);
	if (ok)
		probe.close();
	return ok;
}

void OnlinePanel::toggleServer() {
	if (server_->isRunning()) {
		server_->stop();
		refreshServerUi();
		return;
	}
	const auto addrStr = bindCombo_->currentData().toString();
	QHostAddress addr(addrStr);
	auto port = static_cast<quint16>(portSpin_->value());
	QString err;
	if (!server_->start(addr, port, &err)) {
		if (portAvailable(addr, port)) {
			// 端口本身空闲，是其他原因（权限/网卡等）
			QMessageBox::critical(nullptr, tr("错误"), tr("启动失败：%1").arg(err));
			return;
		}
		// 端口被占用：给出可操作的出口——自动换一个空闲端口重试
		quint16 alt = 0;
		for (int p = port + 1; p <= qMin<int>(port + 100, 65535); ++p) {
			if (portAvailable(addr, static_cast<quint16>(p))) {
				alt = static_cast<quint16>(p);
				break;
			}
		}
		QMessageBox box(QMessageBox::Warning, tr("端口被占用"),
		                tr("端口 %1 已被占用（可能是上一个 LemonLime 实例仍在运行，"
		                   "或其他程序占用了该端口）。\n关闭占用程序后重试，或改用其他端口。")
		                    .arg(port),
		                QMessageBox::Cancel);
		QPushButton *retryBtn = nullptr;
		if (alt != 0)
			retryBtn = box.addButton(tr("改用端口 %1 重试").arg(alt), QMessageBox::AcceptRole);
		box.exec();
		if (!retryBtn || box.clickedButton() != retryBtn)
			return;
		err.clear();
		if (!server_->start(addr, alt, &err)) {
			QMessageBox::critical(nullptr, tr("错误"), tr("启动失败：%1").arg(err));
			return;
		}
		port = alt;
		portSpin_->setValue(alt);
	}
	const auto ip = (addrStr == QStringLiteral("0.0.0.0")) ? detectLocalIp() : addrStr;
	serviceUrl_ = QStringLiteral("http://%1:%2").arg(ip).arg(server_->port());
	refreshServerUi();
}

void OnlinePanel::refreshServerUi() {
	const bool running = server_ && server_->isRunning();
	startStopBtn_->setText(running ? tr("停止服务") : tr("启动服务"));
	startStopBtn_->setObjectName(running ? QStringLiteral("DangerBtn")
	                                     : QStringLiteral("PrimaryBtn"));
	startStopBtn_->style()->unpolish(startStopBtn_);
	startStopBtn_->style()->polish(startStopBtn_);
	bindCombo_->setEnabled(!running);
	portSpin_->setEnabled(!running);
	statusLabel_->setText(running ? tr("正在运行") : tr("已停止"));
	urlLabel_->setText(running ? serviceUrl_ : QStringLiteral("—"));
	copyUrlBtn_->setEnabled(running);
	openBrowserBtn_->setEnabled(running);
	refreshLive();
}

void OnlinePanel::appendLog(const QString &msg) {
	if (logView_)
		logView_->appendPlainText(QDateTime::currentDateTime().toString("hh:mm:ss") + "  " + msg);
}

QString OnlinePanel::detectLocalIp() const {
	for (const auto &a : QNetworkInterface::allAddresses()) {
		if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
			return a.toString();
	}
	return QStringLiteral("127.0.0.1");
}

// ---------------------------------------------------------------- users

void OnlinePanel::onGenerateUsers() {
	if (!server_ || contestDir_.isEmpty()) {
		QMessageBox::warning(nullptr, tr("提示"), tr("请先打开一场比赛。"));
		return;
	}
	auto *store = server_->userStore();
	if (!store)
		return;
	const auto batch = store->generateBatch(genCountSpin_->value(), genPrefixEdit_->text(), 8);
	if (!store->saveToContestDir(contestDir_)) {
		QMessageBox::critical(nullptr, tr("错误"), tr("无法保存 online_users.json"));
		return;
	}
	refreshUsersTable();
	appendLog(tr("已批量生成 %1 个账号").arg(batch.size()));
	onSavePlaintextList();
}

void OnlinePanel::onAddUser() {
	if (!server_ || contestDir_.isEmpty())
		return;
	auto *store = server_->userStore();
	if (!store)
		return;
	const auto name = addNameEdit_->text().trimmed();
	const auto pwd = addPwdEdit_->text();
	if (name.isEmpty() || pwd.isEmpty()) {
		QMessageBox::warning(nullptr, tr("提示"), tr("用户名和密码都不能为空。"));
		return;
	}
	if (store->exists(name)) {
		if (QMessageBox::question(nullptr, tr("确认"),
		                          tr("用户 '%1' 已存在，是否覆盖其密码？").arg(name)) !=
		    QMessageBox::Yes)
			return;
	}
	store->addUser(name, name, pwd);
	store->saveToContestDir(contestDir_);
	addNameEdit_->clear();
	addPwdEdit_->clear();
	refreshUsersTable();
	appendLog(tr("已添加用户 %1").arg(name));
}

void OnlinePanel::onRemoveUser() {
	if (!server_ || contestDir_.isEmpty())
		return;
	auto *store = server_->userStore();
	if (!store)
		return;
	const auto rows = usersTable_->selectionModel()->selectedRows();
	if (rows.isEmpty())
		return;
	if (QMessageBox::question(nullptr, tr("确认"),
	                          tr("确定删除选中的 %1 个用户？").arg(rows.size())) !=
	    QMessageBox::Yes)
		return;
	for (const auto &idx : rows) {
		const auto name = usersTable_->item(idx.row(), 0)->text();
		store->removeUser(name);
	}
	store->saveToContestDir(contestDir_);
	refreshUsersTable();
}

void OnlinePanel::onExportCsv() {
	if (!server_)
		return;
	auto *store = server_->userStore();
	if (!store || store->count() == 0) {
		QMessageBox::information(nullptr, tr("提示"), tr("当前没有可导出的账号。"));
		return;
	}
	const auto path = QFileDialog::getSaveFileName(
	    nullptr, tr("导出 CSV"),
	    QDir(contestDir_).filePath(QStringLiteral("online_users_passwords.csv")),
	    tr("CSV 文件 (*.csv)"));
	if (path.isEmpty())
		return;
	QFile f(path);
	if (!f.open(QFile::WriteOnly | QFile::Text)) {
		QMessageBox::critical(nullptr, tr("错误"), f.errorString());
		return;
	}
	QTextStream ts(&f);
	ts.setEncoding(QStringConverter::Utf8);
	ts << "username,display_name,password\n";
	for (const auto &name : store->allUsernames())
		ts << name << "," << store->displayNameOf(name) << "," << store->plaintextOf(name) << "\n";
	appendLog(tr("已导出 CSV 至 %1").arg(path));
}

// Tolerant single-line CSV parser: handles quoted fields and embedded commas.
static QStringList parseCsvLine(const QString &line) {
	QStringList out;
	QString cur;
	bool inQuote = false;
	for (int i = 0; i < line.size(); ++i) {
		const QChar c = line.at(i);
		if (inQuote) {
			if (c == '"') {
				if (i + 1 < line.size() && line.at(i + 1) == '"') {
					cur.append('"');
					++i;
				} else {
					inQuote = false;
				}
			} else {
				cur.append(c);
			}
		} else {
			if (c == ',') {
				out.append(cur);
				cur.clear();
			} else if (c == '"' && cur.isEmpty()) {
				inQuote = true;
			} else {
				cur.append(c);
			}
		}
	}
	out.append(cur);
	return out;
}

void OnlinePanel::onImportCsv() {
	if (contestDir_.isEmpty() || !server_) {
		QMessageBox::warning(nullptr, tr("提示"), tr("请先打开一场比赛。"));
		return;
	}
	auto *store = server_->userStore();
	if (!store)
		return;

	const auto path = QFileDialog::getOpenFileName(nullptr, tr("选择 CSV 文件"), contestDir_,
	                                               tr("CSV 文件 (*.csv *.txt)"));
	if (path.isEmpty())
		return;

	QFile f(path);
	if (!f.open(QFile::ReadOnly | QFile::Text)) {
		QMessageBox::critical(nullptr, tr("错误"), f.errorString());
		return;
	}
	QTextStream ts(&f);
	ts.setEncoding(QStringConverter::Utf8);

	int added = 0, overwritten = 0, skipped = 0;
	int lineNum = 0;
	bool overwriteAll = false;
	bool skipAllConflicts = false;

	while (!ts.atEnd()) {
		++lineNum;
		QString line = ts.readLine();
		if (lineNum == 1 && line.startsWith(QChar(0xFEFF)))
			line.remove(0, 1); // strip UTF-8 BOM
		const auto trimmed = line.trimmed();
		if (trimmed.isEmpty() || trimmed.startsWith('#'))
			continue;

		// header detection: first line containing 'username' / '用户名' is treated as header
		if (lineNum == 1) {
			const auto lower = trimmed.toLower();
			if (lower.contains("username") || lower.contains("用户名") ||
			    lower.contains("user_name") || lower.contains("name,"))
				continue;
		}

		const auto cols = parseCsvLine(line);
		QString username, displayName, password;
		if (cols.size() >= 3) {
			username = cols.at(0).trimmed();
			displayName = cols.at(1).trimmed();
			password = cols.at(2).trimmed();
		} else if (cols.size() == 2) {
			username = cols.at(0).trimmed();
			password = cols.at(1).trimmed();
		} else if (cols.size() == 1) {
			username = cols.at(0).trimmed();
		}

		if (username.isEmpty()) {
			++skipped;
			continue;
		}
		if (displayName.isEmpty())
			displayName = username;
		if (password.isEmpty()) {
			// no password column in this row → auto-generate one
			QByteArray buf(8, Qt::Uninitialized);
			auto *gen = QRandomGenerator::system();
			static const char alphabet[] =
			    "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
			for (int i = 0; i < buf.size(); ++i)
				buf[i] = alphabet[gen->bounded(int(sizeof(alphabet) - 1))];
			password = QString::fromLatin1(buf);
		}

		if (store->exists(username)) {
			if (skipAllConflicts) {
				++skipped;
				continue;
			}
			if (!overwriteAll) {
				QMessageBox box;
				box.setIcon(QMessageBox::Question);
				box.setWindowTitle(tr("用户已存在"));
				box.setText(tr("用户 '%1' 已存在，是否覆盖其密码？").arg(username));
				auto *yes = box.addButton(tr("覆盖"), QMessageBox::YesRole);
				auto *yesAll = box.addButton(tr("全部覆盖"), QMessageBox::AcceptRole);
				auto *no = box.addButton(tr("跳过"), QMessageBox::NoRole);
				auto *noAll = box.addButton(tr("全部跳过"), QMessageBox::RejectRole);
				box.exec();
				if (box.clickedButton() == yesAll) {
					overwriteAll = true;
				} else if (box.clickedButton() == no) {
					++skipped;
					continue;
				} else if (box.clickedButton() == noAll) {
					skipAllConflicts = true;
					++skipped;
					continue;
				}
				Q_UNUSED(yes);
			}
			store->addUser(username, displayName, password);
			++overwritten;
		} else {
			store->addUser(username, displayName, password);
			++added;
		}
	}

	store->saveToContestDir(contestDir_);
	refreshUsersTable();
	appendLog(tr("CSV 导入完成：新增 %1，覆盖 %2，跳过 %3")
	              .arg(added)
	              .arg(overwritten)
	              .arg(skipped));
	QMessageBox::information(
	    nullptr, tr("导入结果"),
	    tr("新增 %1 个账号，覆盖 %2 个已有账号，跳过 %3 行。").arg(added).arg(overwritten).arg(skipped));
}

void OnlinePanel::onSavePlaintextList() {
	if (contestDir_.isEmpty() || !server_)
		return;
	auto *store = server_->userStore();
	if (!store || store->count() == 0)
		return;
	const auto path = QDir(contestDir_).filePath(QStringLiteral("online_users_passwords.csv"));
	QFile f(path);
	if (!f.open(QFile::WriteOnly | QFile::Text))
		return;
	QTextStream ts(&f);
	ts.setEncoding(QStringConverter::Utf8);
	ts << "username,display_name,password\n";
	for (const auto &name : store->allUsernames())
		ts << name << "," << store->displayNameOf(name) << "," << store->plaintextOf(name) << "\n";
	appendLog(tr("已自动保存明文清单至 %1").arg(path));
}

void OnlinePanel::refreshUsersTable() {
	if (!server_ || !usersTable_)
		return;
	auto *store = server_->userStore();
	if (!store)
		return;
	const auto names = store->allUsernames();
	const auto onlineList = server_->onlineUsernames();
	const QSet<QString> onlineSet(onlineList.cbegin(), onlineList.cend());
	usersTable_->setRowCount(names.size());
	for (int i = 0; i < names.size(); ++i) {
		const auto &n = names.at(i);
		usersTable_->setItem(i, 0, new QTableWidgetItem(n));
		usersTable_->setItem(i, 1, new QTableWidgetItem(store->displayNameOf(n)));
		const auto pw = store->plaintextOf(n);
		auto *pwItem = new QTableWidgetItem(pw.isEmpty() ? QStringLiteral("●●●") : pw);
		if (pw.isEmpty())
			pwItem->setForeground(QBrush(QColor("#94A3B8")));
		pwItem->setFont(QFont(QStringLiteral("Consolas")));
		usersTable_->setItem(i, 2, pwItem);
		// 状态：是否在线
		const bool online = onlineSet.contains(n);
		auto *stItem = new QTableWidgetItem(online ? tr("在线") : tr("离线"));
		stItem->setForeground(QBrush(QColor(online ? "#65A30D" : "#94A3B8")));
		stItem->setTextAlignment(Qt::AlignCenter);
		usersTable_->setItem(i, 3, stItem);
		// 上次登录时间
		const auto login = server_->lastLoginOf(n);
		auto *loginItem = new QTableWidgetItem(login.isValid() ? login.toString("MM-dd hh:mm:ss")
		                                                       : QStringLiteral("—"));
		loginItem->setForeground(QBrush(QColor("#64748B")));
		usersTable_->setItem(i, 4, loginItem);
	}
}

// ---------------------------------------------------------------- live

void OnlinePanel::refreshLive() {
	if (metricOnline_)
		metricOnline_->setText(QString::number(server_ ? server_->onlineCount() : 0));
	if (metricSubmissions_)
		metricSubmissions_->setText(QString::number(server_ ? server_->submissionCount() : 0));
	if (metricState_)
		metricState_->setText(server_ && server_->isRunning() ? tr("正在运行") : tr("已停止"));
}

void OnlinePanel::refreshOnlineTable() {
	if (!onlineTable_ || !server_)
		return;
	const auto names = server_->onlineUsernames();
	auto *store = server_->userStore();
	onlineTable_->setRowCount(names.size());
	for (int i = 0; i < names.size(); ++i) {
		const auto &n = names.at(i);
		onlineTable_->setItem(i, 0, new QTableWidgetItem(n));
		onlineTable_->setItem(i, 1, new QTableWidgetItem(store ? store->displayNameOf(n) : n));
		const auto seen = server_->lastSeenOf(n);
		onlineTable_->setItem(i, 2, new QTableWidgetItem(seen.isValid()
		                                                     ? seen.toString("hh:mm:ss")
		                                                     : QStringLiteral("—")));
	}
}

// ---------------------------------------------------------------- notice

void OnlinePanel::onBroadcastAnnouncement() {
	const auto text = annText_->toPlainText().trimmed();
	server_->setAnnouncement(text);
	if (!text.isEmpty()) {
		annHistory_->insertItem(0, new QListWidgetItem(
		                               QDateTime::currentDateTime().toString("hh:mm") + "  " + text));
		while (annHistory_->count() > 50)
			delete annHistory_->takeItem(annHistory_->count() - 1);
		annText_->clear();
	}
}

void OnlinePanel::onClearAnnouncement() {
	annText_->clear();
	server_->setAnnouncement(QString());
}

void OnlinePanel::onSaveNotice() { server_->setNotice(noticeEdit_->toPlainText().trimmed()); }

// ---------------------------------------------------------------- statement

void OnlinePanel::onSetStatement() {
	if (contestDir_.isEmpty()) {
		QMessageBox::warning(nullptr, tr("提示"), tr("请先打开一场比赛。"));
		return;
	}
	const auto src = QFileDialog::getOpenFileName(
	    nullptr, tr("选择题面及样例"), QString(),
	    tr("题面与样例 (*.pdf *.zip *.7z *.rar *.tar.gz);;所有文件 (*)"));
	if (src.isEmpty())
		return;
	const QFileInfo srcInfo(src);
	QDir contestDir(contestDir_);
	const QDir stmtDir(contestDir.filePath(QStringLiteral("statements")));
	const auto dst = stmtDir.filePath(srcInfo.fileName());
	if (srcInfo.canonicalFilePath() == QFileInfo(dst).canonicalFilePath()) {
		appendLog(tr("题面及样例已是该文件，无需替换"));
		refreshStatementHint();
		return;
	}
	// 单槽位：新选择的文件会替换当前下发的文件
	const auto current = SubmissionServer::findStatementFile(contestDir_);
	if (!current.isEmpty() &&
	    QMessageBox::question(nullptr, tr("确认"),
	                          tr("已下发 %1，是否替换为 %2？")
	                              .arg(QFileInfo(current).fileName(), srcInfo.fileName())) !=
	        QMessageBox::Yes)
		return;
	if (!stmtDir.exists() && !contestDir.mkpath(QStringLiteral("statements"))) {
		QMessageBox::critical(nullptr, tr("错误"),
		                      tr("无法创建目录：%1").arg(stmtDir.absolutePath()));
		return;
	}
	// 先清掉旧的（statements/ 下的残留文件与旧版 statement.pdf），再放入新文件
	const auto srcCanonical = srcInfo.canonicalFilePath();
	for (const auto &fi : stmtDir.entryInfoList(QDir::Files)) {
		if (!srcCanonical.isEmpty() && fi.canonicalFilePath() == srcCanonical)
			continue; // 选中的就是该文件本身，别删了源文件
		QFile::remove(fi.absoluteFilePath());
	}
	const auto legacy = contestDir.filePath(QStringLiteral("statement.pdf"));
	if (QFile::exists(legacy))
		QFile::remove(legacy);
	if (!QFile::copy(src, dst)) {
		QMessageBox::critical(nullptr, tr("错误"), tr("复制失败：%1").arg(src));
		return;
	}
	appendLog(tr("已设定题面及样例：%1").arg(dst));
	refreshStatementHint();
}

void OnlinePanel::onClearStatement() {
	if (contestDir_.isEmpty())
		return;
	const auto current = SubmissionServer::findStatementFile(contestDir_);
	if (current.isEmpty()) {
		appendLog(tr("当前没有题面及样例，无需清除"));
		return;
	}
	if (QMessageBox::question(nullptr, tr("确认"), tr("是否删除 %1？").arg(current)) !=
	    QMessageBox::Yes)
		return;
	if (!QFile::remove(current)) {
		QMessageBox::critical(nullptr, tr("错误"), tr("删除失败：%1").arg(current));
		return;
	}
	// 清理 statements/ 下的残留文件与旧版 statement.pdf
	const QDir stmtDir(QDir(contestDir_).filePath(QStringLiteral("statements")));
	for (const auto &fi : stmtDir.entryInfoList(QDir::Files))
		QFile::remove(fi.absoluteFilePath());
	const auto legacy = QDir(contestDir_).filePath(QStringLiteral("statement.pdf"));
	if (QFile::exists(legacy))
		QFile::remove(legacy);
	appendLog(tr("已清除题面及样例"));
	refreshStatementHint();
}

void OnlinePanel::refreshStatementHint() {
	if (!statementLabel_)
		return;
	if (contestDir_.isEmpty()) {
		statementLabel_->setText(tr("（尚未绑定比赛）"));
		if (setStatementBtn_)
			setStatementBtn_->setEnabled(false);
		if (clearStatementBtn_)
			clearStatementBtn_->setEnabled(false);
		return;
	}
	const auto p = SubmissionServer::findStatementFile(contestDir_);
	const bool exists = !p.isEmpty();
	if (exists) {
		const QFileInfo fi(p);
		statementLabel_->setText(
		    tr("已设定：%1（%2 KB）").arg(p).arg(fi.size() / 1024.0, 0, 'f', 1));
	} else {
		statementLabel_->setText(tr("未设定。点击右侧按钮选择文件。"));
	}
	if (setStatementBtn_)
		setStatementBtn_->setEnabled(true);
	if (clearStatementBtn_)
		clearStatementBtn_->setEnabled(exists);
}

// ---------------------------------------------------------------- contest window

void OnlinePanel::onApplyContestWindow() {
	if (!server_)
		return;
	const auto enabled = windowEnableBox_->isChecked();
	const auto start = startEdit_->dateTime();
	const auto end = endEdit_->dateTime();
	if (enabled && start >= end) {
		QMessageBox::warning(nullptr, tr("提示"),
		                     tr("结束时间必须晚于开始时间。"));
		return;
	}
	server_->setContestWindow(enabled, start, end);
	refreshContestWindow();
	if (enabled)
		appendLog(tr("已设定比赛时间：%1 ~ %2")
		              .arg(start.toString("yyyy-MM-dd HH:mm"), end.toString("yyyy-MM-dd HH:mm")));
	else
		appendLog(tr("已关闭比赛时间限制"));
}

void OnlinePanel::refreshContestWindow() {
	if (!server_ || !windowEnableBox_)
		return;
	if (autoJudgeBox_)
		autoJudgeBox_->setChecked(server_->autoJudge());
	const auto en = server_->windowEnabled();
	windowEnableBox_->setChecked(en);
	startEdit_->setEnabled(en);
	endEdit_->setEnabled(en);
	if (server_->startTime().isValid())
		startEdit_->setDateTime(server_->startTime());
	if (server_->endTime().isValid())
		endEdit_->setDateTime(server_->endTime());
	if (!en) {
		windowStatusLabel_->setText(tr("当前：未启用，任何时间都允许提交"));
		return;
	}
	const auto now = QDateTime::currentDateTime();
	if (server_->startTime().isValid() && now < server_->startTime())
		windowStatusLabel_->setText(tr("当前：未开始（距离开始 %1 分钟）")
		                                .arg(QString::number(now.secsTo(server_->startTime()) / 60)));
	else if (server_->endTime().isValid() && now > server_->endTime())
		windowStatusLabel_->setText(tr("当前：已结束"));
	else
		windowStatusLabel_->setText(tr("正在运行"));
}
