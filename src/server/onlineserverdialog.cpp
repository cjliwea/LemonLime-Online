/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "onlineserverdialog.h"

#include "SubmissionServer.h"
#include "UserStore.h"
#include "core/contest.h"
#include "titlebar.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QHostInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QDesktopServices>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

OnlineServerDialog::OnlineServerDialog(QWidget *parent) : QDialog(parent) {
	setWindowTitle(tr("在线提交服务"));
	resize(880, 640);
	server_ = new SubmissionServer(this);
	connect(server_, &SubmissionServer::logMessage, this, &OnlineServerDialog::appendLog);
	connect(server_, &SubmissionServer::submissionReceived, this,
	        [this](const QString &user, const QString &task, int bytes) {
		        appendLog(tr("收到提交：%1 → %2（%3 字节）").arg(user, task).arg(bytes));
		        refreshUsersTable();
	        });
	buildUi();
	// 无边框窗口：装自绘标题栏（含最小化 / 最大化）
	installTitleBar(this, true);
}

void OnlineServerDialog::bindContest(Contest *contest, const QString &contestDir) {
	contest_ = contest;
	contestDir_ = contestDir;
	server_->bindContest(contest, contestDir);
	refreshUsersTable();
	refreshStatementHint();
	refreshContestWindow();
}

void OnlineServerDialog::buildUi() {
	setStyleSheet(QStringLiteral(
	    "QGroupBox { border: 1px solid #E5E7EB; border-radius: 6px; "
	    "  margin-top: 12px; padding: 10px 12px 8px 12px; }"
	    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; "
	    "  color: #475569; font-weight: 600; }"
	    "QPushButton { padding: 6px 14px; border: 1px solid #D4D4D8; "
	    "  border-radius: 6px; background: #FFFFFF; }"
	    "QPushButton:hover { background: #F4F4F5; }"
	    "QPushButton:disabled { color: #A1A1AA; background: #FAFAFA; }"
	    "QPushButton#PrimaryBtn { background: #84CC16; color: #1A2E05; "
	    "  border: 1px solid #65A30D; font-weight: 600; }"
	    "QPushButton#PrimaryBtn:hover { background: #65A30D; color: #FFFFFF; }"
	    "QPushButton#StopBtn { background: #FEE2E2; color: #991B1B; "
	    "  border: 1px solid #FCA5A5; font-weight: 600; }"
	    "QPushButton#StopBtn:hover { background: #FECACA; }"
	    "QTabWidget::pane { border: 1px solid #E5E7EB; border-radius: 6px; "
	    "  background: #FFFFFF; top: -1px; }"
	    "QTabBar::tab { padding: 8px 18px; margin-right: 2px; "
	    "  border: 1px solid transparent; border-bottom: 0; "
	    "  border-top-left-radius: 6px; border-top-right-radius: 6px; "
	    "  color: #64748B; }"
	    "QTabBar::tab:selected { background: #FFFFFF; color: #0F172A; "
	    "  border-color: #E5E7EB; font-weight: 600; }"
	    "QTabBar::tab:!selected:hover { color: #0F172A; }"
	    "QLineEdit, QComboBox, QSpinBox, QDateTimeEdit { "
	    "  padding: 5px 8px; border: 1px solid #D4D4D8; border-radius: 4px; "
	    "  background: #FFFFFF; }"
	    "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDateTimeEdit:focus { "
	    "  border-color: #84CC16; }"
	    "QTableWidget { border: 1px solid #E5E7EB; border-radius: 4px; "
	    "  gridline-color: #F1F5F9; background: #FFFFFF; }"
	    "QHeaderView::section { background: #F8FAFC; padding: 6px; "
	    "  border: 0; border-bottom: 1px solid #E5E7EB; color: #475569; }"
	    "QPlainTextEdit { border: 1px solid #E5E7EB; border-radius: 4px; "
	    "  background: #FAFAFA; font-family: Menlo, Consolas, monospace; }"
	    "QLabel#StatusBadge { padding: 3px 10px; border-radius: 999px; "
	    "  background: #F1F5F9; color: #64748B; font-weight: 600; }"
	    "QLabel#StatusBadgeRunning { padding: 3px 10px; border-radius: 999px; "
	    "  background: #DCFCE7; color: #15803D; font-weight: 600; }"));

	auto *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(16, 16, 16, 12);
	mainLayout->setSpacing(12);

	// Top tabs
	tabs_ = new QTabWidget(this);
	tabs_->setDocumentMode(true);
	tabs_->addTab(buildServerTab(), tr("服务"));
	tabs_->addTab(buildContestTab(), tr("比赛"));
	tabs_->addTab(buildUsersTab(), tr("账号"));
	tabs_->addTab(buildLogTab(), tr("日志"));
	mainLayout->addWidget(tabs_, 1);

	// Bottom status strip — always visible
	auto *strip = new QWidget(this);
	strip->setObjectName(QStringLiteral("StatusStrip"));
	strip->setStyleSheet(QStringLiteral(
	    "QWidget#StatusStrip { background: #F8FAFC; border: 1px solid #E5E7EB; "
	    "  border-radius: 6px; }"));
	auto *stripLayout = new QHBoxLayout(strip);
	stripLayout->setContentsMargins(12, 8, 12, 8);
	stripStatusBadge_ = new QLabel(tr("已停止"), strip);
	stripStatusBadge_->setObjectName(QStringLiteral("StatusBadge"));
	stripStatusText_ = new QLabel(tr("点击右侧按钮启动服务"), strip);
	stripStatusText_->setStyleSheet(QStringLiteral("color: #475569;"));
	stripStartStopBtn_ = new QPushButton(tr("启动"), strip);
	stripStartStopBtn_->setObjectName(QStringLiteral("PrimaryBtn"));
	stripStartStopBtn_->setMinimumWidth(96);
	connect(stripStartStopBtn_, &QPushButton::clicked, this, &OnlineServerDialog::onStartStop);
	stripLayout->addWidget(stripStatusBadge_);
	stripLayout->addWidget(stripStatusText_, 1);
	stripLayout->addWidget(stripStartStopBtn_);
	mainLayout->addWidget(strip);

	auto *closeBtn = new QPushButton(tr("关闭"), this);
	connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
	auto *bottomBtnRow = new QHBoxLayout();
	bottomBtnRow->addStretch();
	bottomBtnRow->addWidget(closeBtn);
	mainLayout->addLayout(bottomBtnRow);

	resize(960, 660);
	refreshStatusStrip();
}

QWidget *OnlineServerDialog::buildServerTab() {
	auto *page = new QWidget();
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(14);

	// --- Listen group
	auto *listenBox = new QGroupBox(tr("监听设置"), page);
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
	connect(startStopBtn_, &QPushButton::clicked, this, &OnlineServerDialog::onStartStop);

	listenForm->addRow(tr("监听网卡"), bindCombo_);
	listenForm->addRow(tr("端口"), portSpin_);
	auto *actionRow = new QHBoxLayout();
	actionRow->addWidget(startStopBtn_);
	actionRow->addStretch();
	listenForm->addRow(QString(), actionRow);

	statusLabel_ = new QLabel(tr("已停止"), listenBox);
	statusLabel_->setStyleSheet(QStringLiteral("color: #475569;"));
	listenForm->addRow(tr("状态"), statusLabel_);

	urlLabel_ = new QLabel(QStringLiteral("—"), listenBox);
	urlLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	urlLabel_->setStyleSheet(QStringLiteral("font-family: Menlo, Consolas, monospace;"));

	copyUrlBtn_ = new QPushButton(tr("复制"), listenBox);
	openBrowserBtn_ = new QPushButton(tr("用浏览器打开"), listenBox);
	copyUrlBtn_->setEnabled(false);
	openBrowserBtn_->setEnabled(false);
	connect(copyUrlBtn_, &QPushButton::clicked, this, [this]() {
		QGuiApplication::clipboard()->setText(urlLabel_->text());
		appendLog(tr("已复制访问地址：%1").arg(urlLabel_->text()));
	});
	connect(openBrowserBtn_, &QPushButton::clicked, this,
	        [this]() { QDesktopServices::openUrl(QUrl(urlLabel_->text())); });

	auto *urlRow = new QHBoxLayout();
	urlRow->addWidget(urlLabel_, 1);
	urlRow->addWidget(copyUrlBtn_);
	urlRow->addWidget(openBrowserBtn_);
	listenForm->addRow(tr("访问地址"), urlRow);

	layout->addWidget(listenBox);

	// --- Statement & samples group（单槽位：题面 PDF 或样例压缩包/任意单文件）
	auto *pdfBox = new QGroupBox(tr("题面及样例"), page);
	auto *pdfLayout = new QVBoxLayout(pdfBox);
	pdfLayout->setSpacing(8);

	statementLabel_ = new QLabel(pdfBox);
	statementLabel_->setOpenExternalLinks(false);
	statementLabel_->setTextFormat(Qt::RichText);
	statementLabel_->setWordWrap(true);
	statementLabel_->setStyleSheet(QStringLiteral("color: #475569;"));

	setStatementBtn_ = new QPushButton(tr("选择文件..."), pdfBox);
	clearStatementBtn_ = new QPushButton(tr("清除"), pdfBox);
	connect(setStatementBtn_, &QPushButton::clicked, this, &OnlineServerDialog::onSetStatement);
	connect(clearStatementBtn_, &QPushButton::clicked, this, &OnlineServerDialog::onClearStatement);

	auto *pdfRow = new QHBoxLayout();
	pdfRow->addWidget(statementLabel_, 1);
	pdfRow->addWidget(setStatementBtn_);
	pdfRow->addWidget(clearStatementBtn_);
	pdfLayout->addLayout(pdfRow);
	pdfLayout->addWidget(new QLabel(
	    tr("提示：题面 PDF、大样例压缩包或任意单个文件均可在此下发，会原样提供给学生在"
	       "“题面下载”处获取；重复选择会替换已下发的文件。"),
	    pdfBox));
	pdfLayout->itemAt(1)->widget()->setStyleSheet(QStringLiteral("color: #94A3B8;"));

	layout->addWidget(pdfBox);
	layout->addStretch(1);
	return page;
}

QWidget *OnlineServerDialog::buildContestTab() {
	auto *page = new QWidget();
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(14);

	// --- Time window
	auto *windowBox = new QGroupBox(tr("比赛时间窗口"), page);
	auto *windowLayout = new QVBoxLayout(windowBox);
	windowLayout->setSpacing(10);

	windowEnableBox_ = new QCheckBox(tr("启用比赛时间限制（窗口外不允许提交）"), windowBox);
	windowLayout->addWidget(windowEnableBox_);

	auto *grid = new QFormLayout();
	grid->setHorizontalSpacing(12);
	grid->setVerticalSpacing(8);
	startEdit_ = new QDateTimeEdit(QDateTime::currentDateTime(), windowBox);
	startEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
	startEdit_->setCalendarPopup(true);
	endEdit_ = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3 * 3600), windowBox);
	endEdit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
	endEdit_->setCalendarPopup(true);
	grid->addRow(tr("开始时间"), startEdit_);
	grid->addRow(tr("结束时间"), endEdit_);
	windowLayout->addLayout(grid);

	applyWindowBtn_ = new QPushButton(tr("保存比赛时间"), windowBox);
	applyWindowBtn_->setObjectName(QStringLiteral("PrimaryBtn"));
	connect(applyWindowBtn_, &QPushButton::clicked, this, &OnlineServerDialog::onApplyContestWindow);
	connect(windowEnableBox_, &QCheckBox::toggled, this, [this](bool on) {
		startEdit_->setEnabled(on);
		endEdit_->setEnabled(on);
	});
	auto *applyRow = new QHBoxLayout();
	applyRow->addWidget(applyWindowBtn_);
	applyRow->addStretch();
	windowLayout->addLayout(applyRow);

	windowStatusLabel_ = new QLabel(windowBox);
	windowStatusLabel_->setStyleSheet(QStringLiteral("color: #475569;"));
	windowLayout->addWidget(windowStatusLabel_);

	layout->addWidget(windowBox);

	// --- Auto judge
	auto *judgeBox = new QGroupBox(tr("评测"), page);
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
	    tr("关闭时为手动评测：学生提交只写入 source 目录，老师在主窗口手动点评测。"), judgeBox);
	hint->setStyleSheet(QStringLiteral("color: #94A3B8;"));
	hint->setWordWrap(true);
	judgeLayout->addWidget(hint);

	layout->addWidget(judgeBox);
	layout->addStretch(1);
	return page;
}

QWidget *OnlineServerDialog::buildUsersTab() {
	auto *page = new QWidget();
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(16, 16, 16, 16);
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
	connect(genBtn_, &QPushButton::clicked, this, &OnlineServerDialog::onGenerateUsers);
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
	importBtn_ = new QPushButton(tr("从 CSV 导入..."), toolBox);
	exportBtn_ = new QPushButton(tr("导出 CSV"), toolBox);
	connect(importBtn_, &QPushButton::clicked, this, &OnlineServerDialog::onImportCsv);
	connect(exportBtn_, &QPushButton::clicked, this, &OnlineServerDialog::onExportCsv);
	ioRow->addWidget(importBtn_);
	ioRow->addWidget(exportBtn_);
	ioRow->addStretch();
	toolLayout->addLayout(ioRow);

	auto *addRow = new QHBoxLayout();
	addNameEdit_ = new QLineEdit(toolBox);
	addNameEdit_->setPlaceholderText(tr("用户名"));
	addPwdEdit_ = new QLineEdit(toolBox);
	addPwdEdit_->setPlaceholderText(tr("密码"));
	addBtn_ = new QPushButton(tr("添加"), toolBox);
	removeBtn_ = new QPushButton(tr("删除所选"), toolBox);
	connect(addBtn_, &QPushButton::clicked, this, &OnlineServerDialog::onAddUser);
	connect(removeBtn_, &QPushButton::clicked, this, &OnlineServerDialog::onRemoveUser);
	addRow->addWidget(addNameEdit_, 1);
	addRow->addWidget(addPwdEdit_, 1);
	addRow->addWidget(addBtn_);
	addRow->addWidget(removeBtn_);
	toolLayout->addLayout(addRow);

	// --- Table
	usersTable_ = new QTableWidget(0, 3, page);
	usersTable_->setHorizontalHeaderLabels({tr("用户名"), tr("显示名"), tr("密码")});
	usersTable_->horizontalHeader()->setStretchLastSection(true);
	usersTable_->verticalHeader()->setVisible(false);
	usersTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
	usersTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	usersTable_->setAlternatingRowColors(true);
	usersTable_->setStyleSheet(QStringLiteral("alternate-background-color: #FAFAFA;"));

	layout->addWidget(genBox);
	layout->addWidget(toolBox);
	layout->addWidget(usersTable_, 1);
	return page;
}

QWidget *OnlineServerDialog::buildLogTab() {
	auto *page = new QWidget();
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(16, 16, 16, 16);
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
	return page;
}

void OnlineServerDialog::refreshStatusStrip() {
	if (!stripStatusBadge_)
		return;
	if (server_ && server_->isRunning()) {
		stripStatusBadge_->setObjectName(QStringLiteral("StatusBadgeRunning"));
		stripStatusBadge_->setText(tr("● 运行中"));
		const auto url = urlLabel_ ? urlLabel_->text() : QString();
		stripStatusText_->setText(url.isEmpty() ? tr("服务正在运行") : url);
		stripStartStopBtn_->setText(tr("停止"));
		stripStartStopBtn_->setObjectName(QStringLiteral("StopBtn"));
	} else {
		stripStatusBadge_->setObjectName(QStringLiteral("StatusBadge"));
		stripStatusBadge_->setText(tr("○ 已停止"));
		stripStatusText_->setText(tr("点击右侧按钮启动服务"));
		stripStartStopBtn_->setText(tr("启动"));
		stripStartStopBtn_->setObjectName(QStringLiteral("PrimaryBtn"));
	}
	// re-apply the cosmetic stylesheet so objectName-based rules kick in
	stripStatusBadge_->style()->unpolish(stripStatusBadge_);
	stripStatusBadge_->style()->polish(stripStatusBadge_);
	stripStartStopBtn_->style()->unpolish(stripStartStopBtn_);
	stripStartStopBtn_->style()->polish(stripStartStopBtn_);
}

void OnlineServerDialog::onStartStop() {
	if (server_->isRunning()) {
		server_->stop();
		startStopBtn_->setText(tr("启动服务"));
		startStopBtn_->setObjectName(QStringLiteral("PrimaryBtn"));
		startStopBtn_->style()->unpolish(startStopBtn_);
		startStopBtn_->style()->polish(startStopBtn_);
		statusLabel_->setText(tr("已停止"));
		urlLabel_->setText(QStringLiteral("—"));
		copyUrlBtn_->setEnabled(false);
		openBrowserBtn_->setEnabled(false);
		bindCombo_->setEnabled(true);
		portSpin_->setEnabled(true);
		refreshStatusStrip();
		return;
	}
	const auto addrStr = bindCombo_->currentData().toString();
	QHostAddress addr(addrStr);
	QString err;
	if (!server_->start(addr, static_cast<quint16>(portSpin_->value()), &err)) {
		QMessageBox::critical(this, tr("错误"), tr("启动失败：%1").arg(err));
		return;
	}
	startStopBtn_->setText(tr("停止服务"));
	startStopBtn_->setObjectName(QStringLiteral("StopBtn"));
	startStopBtn_->style()->unpolish(startStopBtn_);
	startStopBtn_->style()->polish(startStopBtn_);
	bindCombo_->setEnabled(false);
	portSpin_->setEnabled(false);
	const auto ip = (addrStr == QStringLiteral("0.0.0.0")) ? detectLocalIp() : addrStr;
	const auto url = QStringLiteral("http://%1:%2").arg(ip).arg(server_->port());
	statusLabel_->setText(tr("正在运行"));
	urlLabel_->setText(url);
	copyUrlBtn_->setEnabled(true);
	openBrowserBtn_->setEnabled(true);
	refreshStatusStrip();
	appendLog(tr("正在监听 %1").arg(url));
}

void OnlineServerDialog::onGenerateUsers() {
	if (!server_ || contestDir_.isEmpty()) {
		QMessageBox::warning(this, tr("提示"), tr("请先打开一场比赛。"));
		return;
	}
	auto *store = server_->userStore();
	if (!store)
		return;
	const auto batch = store->generateBatch(genCountSpin_->value(), genPrefixEdit_->text(), 8);
	if (!store->saveToContestDir(contestDir_)) {
		QMessageBox::critical(this, tr("错误"),
		                      tr("无法保存 online_users.json"));
		return;
	}
	lastGeneratedRows_.clear();
	for (const auto &g : batch)
		lastGeneratedRows_.append({g.username, g.displayName, g.plaintextPassword});

	refreshUsersTable();
	appendLog(tr("已批量生成 %1 个账号").arg(batch.size()));

	// always write a fresh plaintext file alongside, so the teacher won't lose passwords
	onSavePlaintextList();
}

void OnlineServerDialog::onAddUser() {
	if (!server_ || contestDir_.isEmpty())
		return;
	auto *store = server_->userStore();
	if (!store)
		return;
	const auto name = addNameEdit_->text().trimmed();
	const auto pwd = addPwdEdit_->text();
	if (name.isEmpty() || pwd.isEmpty()) {
		QMessageBox::warning(this, tr("提示"), tr("用户名和密码都不能为空。"));
		return;
	}
	if (store->exists(name)) {
		if (QMessageBox::question(this, tr("确认"),
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

void OnlineServerDialog::onRemoveUser() {
	if (!server_ || contestDir_.isEmpty())
		return;
	auto *store = server_->userStore();
	if (!store)
		return;
	const auto rows = usersTable_->selectionModel()->selectedRows();
	if (rows.isEmpty())
		return;
	if (QMessageBox::question(this, tr("确认"),
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

void OnlineServerDialog::onExportCsv() {
	// Build rows from current store (covers users created earlier too)
	if (!server_)
		return;
	auto *store = server_->userStore();
	if (!store || store->count() == 0) {
		QMessageBox::information(this, tr("提示"), tr("当前没有可导出的账号。"));
		return;
	}
	const auto path = QFileDialog::getSaveFileName(
	    this, tr("导出 CSV"),
	    QDir(contestDir_).filePath(QStringLiteral("online_users_passwords.csv")),
	    tr("CSV 文件 (*.csv)"));
	if (path.isEmpty())
		return;
	QFile f(path);
	if (!f.open(QFile::WriteOnly | QFile::Text)) {
		QMessageBox::critical(this, tr("错误"), f.errorString());
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

void OnlineServerDialog::onImportCsv() {
	if (contestDir_.isEmpty() || !server_) {
		QMessageBox::warning(this, tr("提示"), tr("请先打开一场比赛。"));
		return;
	}
	auto *store = server_->userStore();
	if (!store)
		return;

	const auto path = QFileDialog::getOpenFileName(this, tr("选择 CSV 文件"),
	                                               contestDir_, tr("CSV 文件 (*.csv *.txt)"));
	if (path.isEmpty())
		return;

	QFile f(path);
	if (!f.open(QFile::ReadOnly | QFile::Text)) {
		QMessageBox::critical(this, tr("错误"), f.errorString());
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
				QMessageBox box(this);
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
	              .arg(added).arg(overwritten).arg(skipped));
	QMessageBox::information(
	    this, tr("导入结果"),
	    tr("新增 %1 个账号，覆盖 %2 个已有账号，跳过 %3 行。").arg(added).arg(overwritten).arg(skipped));
}

void OnlineServerDialog::onSavePlaintextList() {
	if (contestDir_.isEmpty() || !server_)
		return;
	auto *store = server_->userStore();
	if (!store || store->count() == 0)
		return;
	const auto path =
	    QDir(contestDir_).filePath(QStringLiteral("online_users_passwords.csv"));
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

void OnlineServerDialog::refreshUsersTable() {
	if (!server_)
		return;
	auto *store = server_->userStore();
	if (!store)
		return;
	const auto names = store->allUsernames();
	usersTable_->setRowCount(names.size());
	for (int i = 0; i < names.size(); ++i) {
		const auto &n = names.at(i);
		usersTable_->setItem(i, 0, new QTableWidgetItem(n));
		usersTable_->setItem(i, 1, new QTableWidgetItem(store->displayNameOf(n)));
		const auto pw = store->plaintextOf(n);
		auto *pwItem = new QTableWidgetItem(pw.isEmpty() ? QStringLiteral("（已加密保存）") : pw);
		if (pw.isEmpty())
			pwItem->setForeground(QBrush(QColor("#94A3B8")));
		else
			pwItem->setFont(QFont(QStringLiteral("Menlo")));
		usersTable_->setItem(i, 2, pwItem);
	}
}

void OnlineServerDialog::onSetStatement() {
	if (contestDir_.isEmpty()) {
		QMessageBox::warning(this, tr("提示"), tr("请先打开一场比赛。"));
		return;
	}
	const auto src = QFileDialog::getOpenFileName(
	    this, tr("选择题面及样例"), QString(),
	    tr("题面与样例 (*.pdf *.zip *.7z *.rar *.tar.gz);;所有文件 (*)"));
	if (src.isEmpty())
		return;
	const QFileInfo srcInfo(src);
	QDir contestDir(contestDir_);
	const QDir stmtDir(contestDir.filePath(QStringLiteral("statements")));
	const auto dst = stmtDir.filePath(srcInfo.fileName());
	if (srcInfo.canonicalFilePath() == QFileInfo(dst).canonicalFilePath()) {
		appendLog(tr("题面及样例已是该文件，无需复制"));
		refreshStatementHint();
		return;
	}
	// 单槽位：新选择的文件会替换当前下发的文件
	const auto current = SubmissionServer::findStatementFile(contestDir_);
	if (!current.isEmpty() &&
	    QMessageBox::question(this, tr("确认"),
	                          tr("已下发 %1，是否替换为 %2？")
	                              .arg(QFileInfo(current).fileName(), srcInfo.fileName())) !=
	        QMessageBox::Yes)
		return;
	if (!stmtDir.exists() && !contestDir.mkpath(QStringLiteral("statements"))) {
		QMessageBox::critical(this, tr("错误"),
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
		QMessageBox::critical(this, tr("错误"), tr("复制失败：%1").arg(src));
		return;
	}
	appendLog(tr("已设定题面及样例：%1").arg(dst));
	refreshStatementHint();
}

void OnlineServerDialog::onClearStatement() {
	if (contestDir_.isEmpty())
		return;
	const auto current = SubmissionServer::findStatementFile(contestDir_);
	if (current.isEmpty()) {
		appendLog(tr("当前没有题面及样例，无需清除"));
		return;
	}
	if (QMessageBox::question(this, tr("确认"),
	                          tr("是否删除 %1？").arg(current)) != QMessageBox::Yes)
		return;
	if (!QFile::remove(current)) {
		QMessageBox::critical(this, tr("错误"), tr("删除失败：%1").arg(current));
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

void OnlineServerDialog::onApplyContestWindow() {
	if (!server_)
		return;
	const auto enabled = windowEnableBox_->isChecked();
	const auto start = startEdit_->dateTime();
	const auto end = endEdit_->dateTime();
	if (enabled && start >= end) {
		QMessageBox::warning(this, tr("提示"), tr("结束时间必须晚于开始时间。"));
		return;
	}
	server_->setContestWindow(enabled, start, end);
	refreshContestWindow();
	if (enabled)
		appendLog(tr("已设定比赛时间：%1 ~ %2")
		              .arg(start.toString("yyyy-MM-dd HH:mm"),
		                   end.toString("yyyy-MM-dd HH:mm")));
	else
		appendLog(tr("已关闭比赛时间限制"));
}

void OnlineServerDialog::refreshContestWindow() {
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
		windowStatusLabel_->setText(tr("当前：未开始（距离开始 %1）")
		                                .arg(QString::number(now.secsTo(server_->startTime()) / 60) +
		                                     tr(" 分钟")));
	else if (server_->endTime().isValid() && now > server_->endTime())
		windowStatusLabel_->setText(tr("当前：已结束"));
	else
		windowStatusLabel_->setText(tr("当前：进行中"));
}

void OnlineServerDialog::refreshStatementHint() {
	if (contestDir_.isEmpty()) {
		statementLabel_->setText(tr("（尚未绑定比赛）"));
		if (setStatementBtn_) setStatementBtn_->setEnabled(false);
		if (clearStatementBtn_) clearStatementBtn_->setEnabled(false);
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
	if (setStatementBtn_) setStatementBtn_->setEnabled(true);
	if (clearStatementBtn_) clearStatementBtn_->setEnabled(exists);
}

void OnlineServerDialog::appendLog(const QString &msg) {
	logView_->appendPlainText(QDateTime::currentDateTime().toString("hh:mm:ss") + "  " + msg);
}

QString OnlineServerDialog::detectLocalIp() const {
	for (const auto &a : QNetworkInterface::allAddresses()) {
		if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
			return a.toString();
	}
	return QStringLiteral("127.0.0.1");
}
