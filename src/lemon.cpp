/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lemon.h"
#include "ui_lemon.h"
//
#include "addcompilerwizard.h"
#include "addproblemwizard.h"
#include "addtaskdialog.h"
#include "base/LemonBase.hpp"
#include "base/LemonLog.hpp"
#include "base/LemonTranslator.hpp"
#include "base/compiler.h"
#include "base/settings.h"
#include "component/exportutil/exportutil.h"
#include "core/contest.h"
#include "core/contestant.h"
#include "core/task.h"
#include "core/testcase.h"
#include "detaildialog.h"
#include "newcontestdialog.h"
#include "newcontestwizard.h"
#include "opencontestdialog.h"
#include "optionsdialog.h"
#include "probleminstaller.h"
#include "server/SubmissionServer.h"
#include "server/UserStore.h" // SubmissionServer.h 内联函数用到 QPointer<UserStore>::data()，MSVC 需要完整类型
#include "server/onlinepanel.h"
#include "titlebar.h"
#include "statisticsbrowser.h"
#include "welcomedialog.h"
//
#include <QByteArrayView>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScreen>
#include <QButtonGroup>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QProcess>
#include <QPushButton>
#include <QSizeGrip>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <algorithm>
#include <chrono>
//
#define LEMON_MODULE_NAME "Lemon"

LemonLime::LemonLime(QWidget *parent) : QMainWindow(parent), ui(new Ui::LemonLime) {
	ui->setupUi(this);
	curContest = nullptr;
	settings = new Settings();
	ui->tabWidget->setVisible(false);
	ui->contestCard->setVisible(false);
	ui->mainStack->setCurrentIndex(1); // 空状态页：在试题区衔接创建/打开比赛
	connect(ui->emptyNewBtn, &QPushButton::clicked, this, &LemonLime::newAction);
	connect(ui->emptyOpenBtn, &QPushButton::clicked, this, &LemonLime::loadAction);
	connect(ui->cardOpenFolderBtn, &QPushButton::clicked, this, &LemonLime::openFolderAction);
	connect(ui->cardRenameBtn, &QPushButton::clicked, this, &LemonLime::changeContestName);
	connect(ui->cardSettingsBtn, &QPushButton::clicked, this, [this]() {
		if (ui->mainStack->currentIndex() == 0)
			ui->tabWidget->setCurrentWidget(ui->contestSettingsTab);
	});
	connect(ui->cardCloseBtn, &QPushButton::clicked, ui->closeAction, &QAction::trigger);
	ui->closeAction->setEnabled(false);
	ui->saveAction->setEnabled(false);
	ui->openFolderAction->setEnabled(false);
	ui->actionChangeContestName->setEnabled(false);
	dataDirWatcher = nullptr;
	settings->loadSettings();
	TaskMenu = new QMenu();
	signalMapper = new QSignalMapper();
	ui->summary->setSettings(settings);
	ui->taskEdit->setSettings(settings);
	ui->testCaseEdit->setSettings(settings);
	connect(this, &LemonLime::dataPathChanged, ui->taskEdit, &TaskEditWidget::dataPathChanged);
	connect(this, &LemonLime::dataPathChanged, ui->testCaseEdit, &TestCaseEditWidget::dataPathChanged);
	connect(ui->summary, &SummaryTree::currentItemChanged, this, &LemonLime::summarySelectionChanged);
	connect(ui->optionsAction, &QAction::triggered, this, &LemonLime::showOptionsDialog);
	connect(ui->actionOnlineServer, &QAction::triggered, this, &LemonLime::showOnlineServerDialog);
	connect(ui->cleanupButton, &QPushButton::clicked, this, &LemonLime::cleanupButtonClicked);
	connect(ui->refreshButton, &QPushButton::clicked, this, &LemonLime::refreshButtonClicked);
	connect(ui->judgeButton, &QPushButton::clicked, ui->resultViewer, &ResultViewer::judgeSelected);
	connect(ui->judgeAllButton, &QPushButton::clicked, ui->resultViewer, &ResultViewer::judgeAll);
	connect(ui->judgeUnjudgedButton, &QPushButton::clicked, ui->resultViewer, &ResultViewer::judgeUnjudged);
	connect(ui->judgeAction, &QAction::triggered, ui->resultViewer, &ResultViewer::judgeSelected);
	connect(ui->judgeAllAction, &QAction::triggered, ui->resultViewer, &ResultViewer::judgeAll);
	connect(ui->judgeUnjudgedAction, &QAction::triggered, ui->resultViewer, &ResultViewer::judgeUnjudged);
	connect(ui->cleanupAction, &QAction::triggered, this, &LemonLime::cleanupButtonClicked);
	connect(ui->refreshAction, &QAction::triggered, this, &LemonLime::refreshButtonClicked);
	connect(ui->judgeGreyAction, &QAction::triggered, ui->resultViewer, &ResultViewer::judgeGrey);
	connect(ui->judgeMagentaAction, &QAction::triggered, ui->resultViewer, &ResultViewer::judgeMagenta);
	connect(ui->tabWidget, &QTabWidget::currentChanged, this, &LemonLime::tabIndexChanged);
	connect(ui->moveUpButton, &QToolButton::clicked, this, &LemonLime::moveUpTask);
	connect(ui->moveDownButton, &QToolButton::clicked, this, &LemonLime::moveDownTask);
	connect(ui->resultViewer, &ResultViewer::itemSelectionChanged, this, &LemonLime::viewerSelectionChanged);
	connect(ui->resultViewer, &ResultViewer::contestantDeleted, this, &LemonLime::contestantDeleted);
	connect(ui->newAction, &QAction::triggered, this, &LemonLime::newAction);
	connect(ui->openAction, &QAction::triggered, this, &LemonLime::loadAction);
	connect(ui->saveAction, &QAction::triggered, this, &LemonLime::saveAction);
	connect(ui->openFolderAction, &QAction::triggered, this, &LemonLime::openFolderAction);
	connect(ui->closeAction, &QAction::triggered, this, &LemonLime::closeAction);
	connect(ui->addTasksAction, &QAction::triggered, this, &LemonLime::addTasksAction);
	connect(ui->exportAction, &QAction::triggered, this, &LemonLime::exportResult);
	connect(ui->actionExportStatistics, &QAction::triggered, this, &LemonLime::exportStatistics);
	connect(ui->aboutAction, &QAction::triggered, this, &LemonLime::aboutLemon);
	connect(ui->actionManual, &QAction::triggered, this, &LemonLime::actionManual);
	connect(ui->actionMore, &QAction::triggered, this, &LemonLime::actionMore);
	connect(ui->actionChangeContestName, &QAction::triggered, this, &LemonLime::changeContestName);
	connect(ui->exitAction, &QAction::triggered, this, &LemonLime::close);

	// 顶部居中导航条（复刻预览版）：藏起 QTabBar，用自绘按钮行实现真正的居中。
	// QTabWidget/QTabBar 原生不支持居中，setExpanding 只是均布，不是居中。
	ui->tabWidget->tabBar()->hide();
	auto *navBar = new QWidget(this);
	navBar->setObjectName(QStringLiteral("navBar"));
	auto *navLayout = new QHBoxLayout(navBar);
	navLayout->setContentsMargins(0, 0, 0, 0);
	navLayout->setSpacing(4);
	navLayout->addStretch(1);
	auto *navGroup = new QButtonGroup(navBar);
	for (int i = 0; i < ui->tabWidget->count(); ++i) {
		auto *tabBtn = new QPushButton(ui->tabWidget->tabText(i), navBar);
		tabBtn->setObjectName(QStringLiteral("navTab"));
		tabBtn->setCheckable(true);
		tabBtn->setCursor(Qt::PointingHandCursor);
		tabBtn->setChecked(i == 0);
		navGroup->addButton(tabBtn, i);
		navLayout->addWidget(tabBtn);
	}
	navLayout->addStretch(1);
	// 点击 -> 切页；切页（含程序性 setCurrentIndex）-> 同步选中态
	connect(navGroup, &QButtonGroup::idClicked, ui->tabWidget, &QTabWidget::setCurrentIndex);
	connect(ui->tabWidget, &QTabWidget::currentChanged, navGroup, [navGroup](int idx) {
		for (auto *b : navGroup->buttons())
			b->setChecked(navGroup->id(b) == idx);
	});
	// 插到 mainPage 布局里 tabWidget 上方（空状态页不显示导航，与预览版一致）
	if (auto *pageBox = qobject_cast<QBoxLayout *>(ui->tabWidget->parentWidget()->layout()))
		pageBox->insertWidget(0, navBar);

	// 在线服务面板：内容直接构建进各标签页（比赛设置/账号/公告须知/实时状况/日志）。
	// 必须先于页脚按钮创建：面板容器先进比赛设置页布局，页脚才能垫底
	onlinePanel = new OnlinePanel(ui->contestSettingsTab, ui->accountsTab, ui->noticeTab,
	                              ui->liveTab, ui->logsTab, this);
	setupOnlineStatusBar();

	// 页内动作按钮：原菜单全部移除后，各动作收进对应标签页（复用 QAction 文案与快捷键）
	auto pageBtn = [this](QAction *act) {
		auto *btn = new QToolButton(this);
		btn->setDefaultAction(act);
		btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
		btn->setObjectName(QStringLiteral("pageActionBtn"));
		btn->setAutoRaise(true);
		return btn;
	};
	// 试题页：题目列表底行 [添加题目]（原 Control 菜单）
	ui->horizontalLayout_7->insertWidget(0, pageBtn(ui->addTasksAction));
	// 成绩页：按钮行加 [导出成绩]（原 Control 菜单）
	ui->horizontalLayout_4->insertWidget(1, pageBtn(ui->exportAction));
	// 统计页：右上 [导出统计] 单独占一行靠右；浏览器占满整宽。
	// 注意不要再往浏览器所在的横向布局里 insertStretch，否则整页内容会被挤到右侧、
	// 左侧留出一大片空白（v1.8.8 的问题）。
	ui->statisticsTopRow->insertWidget(1, pageBtn(ui->actionExportStatistics));
	ui->statisticsTabLayout->setStretchFactor(ui->statisticsBrowser, 1);
	// 比赛设置页底部：[选项设置] [使用手册] [更多指南] [关于]（原 Tools / Help 菜单）
	{
		auto *footer = new QHBoxLayout();
		footer->addStretch(1);
		footer->addWidget(pageBtn(ui->optionsAction));
		footer->addWidget(pageBtn(ui->actionManual));
		footer->addWidget(pageBtn(ui->actionMore));
		footer->addWidget(pageBtn(ui->aboutAction));
		ui->contestSettingsTabLayout->addLayout(footer);
	}
	// 无边框窗口：装自绘标题栏（含最小化 / 最大化）
	installTitleBar(this, true);

	// 还原上次退出时的窗口位置与尺寸。
	// 关键：还原出来的尺寸必须小于屏幕可用区域，否则「正常尺寸」本身就等于整屏，
	// 最大化 / 还原按钮点下去在视觉上毫无变化（看起来就像失效）。
	QSettings windowSettings("LemonLime", "lemon");
	QRect savedGeometry = windowSettings.value("WindowGeometry").toRect();

	if (! savedGeometry.isValid() || savedGeometry.width() < 400 || savedGeometry.height() < 300) {
		const QSize savedSize = windowSettings.value("WindowSize", size()).toSize();
		savedGeometry = QRect(QPoint(0, 0), savedSize);
	}

	const QScreen *curScreen = screen();

	if (! curScreen)
		curScreen = QGuiApplication::primaryScreen();

	if (curScreen) {
		const QRect available = curScreen->availableGeometry();
		savedGeometry.setWidth(qMin(savedGeometry.width(), qMax(800, int(available.width() * 0.86))));
		savedGeometry.setHeight(qMin(savedGeometry.height(), qMax(560, int(available.height() * 0.86))));

		if (! available.intersects(savedGeometry))
			savedGeometry.moveCenter(available.center());
	}

	setGeometry(savedGeometry);

	if (windowSettings.value("WindowMaximized", false).toBool())
		setWindowState(windowState() | Qt::WindowMaximized);

	autoSaveTimer.callOnTimeout([this]() {
		if (curContest)
			saveAction();
	});
	using namespace std::chrono_literals;
	autoSaveTimer.start(30s);
}

LemonLime::~LemonLime() {
	delete TaskMenu;
	delete ui;
}

void LemonLime::changeEvent(QEvent *event) {
	if (event->type() == QEvent::LanguageChange) {
		ui->retranslateUi(this);
		ui->resultViewer->refreshViewer();
		ui->statisticsBrowser->refresh();
	}
}

void LemonLime::closeEvent(QCloseEvent * /*event*/) {
	if (onlinePanel)
		onlinePanel->server()->stop(); // 退出前停掉 HTTP 服务
	if (curContest)
		saveContest(curFile);

	settings->saveSettings();
	QSettings windowSettings("LemonLime", "lemon");
	const QRect normal = normalGeometry();
	// 最大化时 size() 就是整屏，必须存 normalGeometry()，否则下次启动会「正常即整屏」
	windowSettings.setValue("WindowGeometry",
	                        normal.isValid() && normal.width() > 0 ? normal : geometry());
	windowSettings.setValue("WindowMaximized", isMaximized() || bool(windowState() & Qt::WindowMaximized));
	windowSettings.setValue("WindowSize", size()); // 兼容旧版本配置键
}

auto LemonLime::getSplashTime() -> int { return settings->getSplashTime(); }

void LemonLime::welcome() {
	if (settings->getCompilerList().empty()) {
		auto *wizard = new AddCompilerWizard(this);

		if (wizard->exec() == QDialog::Accepted) {
			QList<Compiler *> compilerList = wizard->getCompilerList();

			for (auto &i : compilerList)
				settings->addCompiler(i);
		}

		delete wizard;
	}

	auto *dialog = new WelcomeDialog(this);
	dialog->setRecentContest(settings->getRecentContest());

	if (dialog->exec() == QDialog::Accepted) {
		settings->setRecentContest(dialog->getRecentContest());

		if (dialog->getCurrentTab() == 0) {
			loadContest(dialog->getSelectedContest());
		} else {
			newContest(dialog->getContestTitle(), dialog->getSavingName(), dialog->getContestPath());
		}
	} else {
		settings->setRecentContest(dialog->getRecentContest());
	}

	delete dialog;
}

void LemonLime::insertWatchPath(const QString &curDir, QFileSystemWatcher *watcher) {
	watcher->addPath(curDir);
	QDir dir(curDir);
	QStringList list = dir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);

	for (int i = 0; i < list.size(); i++) {
		insertWatchPath(curDir + list[i] + QDir::separator(), watcher);
	}
}

void LemonLime::resetDataWatcher() {
	delete dataDirWatcher;
	dataDirWatcher = new QFileSystemWatcher(this);
	insertWatchPath(Settings::dataPath(), dataDirWatcher);
	connect(dataDirWatcher, &QFileSystemWatcher::directoryChanged, this, &LemonLime::resetDataWatcher);
	connect(dataDirWatcher, &QFileSystemWatcher::fileChanged, this, &LemonLime::dataPathChanged);
	connect(dataDirWatcher, &QFileSystemWatcher::directoryChanged, this, &LemonLime::dataPathChanged);
	emit dataPathChanged();
}

void LemonLime::refreshSummary() {
	if (! ui->summary->isEnabled())
		return;

	ui->summary->setContest(curContest);
}

void LemonLime::summarySelectionChanged() {
	if (! ui->summary->isEnabled())
		return;

	QTreeWidgetItem *curItem = ui->summary->currentItem();

	if (! curItem) {
		ui->taskEdit->setEditTask(nullptr);
		ui->editWidget->setCurrentIndex(0);
		return;
	}

	int index = ui->summary->indexOfTopLevelItem(curItem);

	if (index != -1) {
		ui->taskEdit->setEditTask(curContest->getTask(index));
		ui->editWidget->setCurrentIndex(1);
	} else {
		QTreeWidgetItem *parentItem = curItem->parent();
		int taskIndex = ui->summary->indexOfTopLevelItem(parentItem);
		int testCaseIndex = parentItem->indexOfChild(curItem);
		Task *curTask = curContest->getTask(taskIndex);
		TestCase *curTestCase = curTask->getTestCase(testCaseIndex);
		ui->testCaseEdit->setEditTestCase(curTestCase, curTask->getTaskType() == Task::Traditional ||
		                                                   curTask->getTaskType() == Task::Interaction ||
		                                                   curTask->getTaskType() == Task::Communication ||
		                                                   curTask->getTaskType() == Task::CommunicationExec);
		ui->editWidget->setCurrentIndex(2);
	}
}

void LemonLime::showOptionsDialog() {
	auto *dialog = new OptionsDialog(this);
	dialog->resetEditSettings(settings);

	if (dialog->exec() == QDialog::Accepted) {
		settings->copyFrom(dialog->getEditSettings());
		LemonLimeTranslator->InstallTranslation(settings->getUiLanguage());
		ui->testCaseEdit->setSettings(settings);

		if (curContest) {
			const QList<Task *> &taskList = curContest->getTaskList();

			for (auto *i : taskList)
				i->refreshCompilerConfiguration(settings);
		}
	}

	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	delete dialog;
}

void LemonLime::showOnlineServerDialog() {
	// 在线服务已面板化：跳到「比赛设置」标签页即可操作服务
	if (curContest)
		onlinePanel->bindContest(curContest, QDir::currentPath());
	ui->tabWidget->setCurrentWidget(ui->contestSettingsTab);
}

// 底部常驻服务状态条：● 状态 · 地址 · 复制链接 · 浏览器打开 · 放行防火墙 · 在线 N
void LemonLime::setupOnlineStatusBar() {
	statusDotLabel = new QLabel(tr("● HTTP 已停止"), this);
	statusDotLabel->setStyleSheet(QStringLiteral("color: #94A3B8;"));
	statusAddrLabel = new QLabel(this);
	statusAddrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	statusCopyBtn = new QPushButton(tr("复制链接"), this);
	statusBrowserBtn = new QPushButton(tr("浏览器打开"), this);
	statusFirewallBtn = new QPushButton(tr("放行防火墙"), this);
	statusOnlineLabel = new QLabel(tr("在线 0"), this);
	for (auto *btn : {statusCopyBtn, statusBrowserBtn, statusFirewallBtn}) {
		btn->setObjectName(QStringLiteral("statusBarBtn"));
		btn->setFlat(true);
		btn->setEnabled(false);
	}
	ui->statusBar->addPermanentWidget(statusDotLabel);
	ui->statusBar->addPermanentWidget(statusAddrLabel);
	ui->statusBar->addPermanentWidget(statusCopyBtn);
	ui->statusBar->addPermanentWidget(statusBrowserBtn);
	ui->statusBar->addPermanentWidget(statusFirewallBtn);
	ui->statusBar->addPermanentWidget(statusOnlineLabel);
	// 无边框窗口后靠右下角拉伸柄调整大小（放在最右）
	ui->statusBar->addPermanentWidget(new QSizeGrip(this));

	connect(onlinePanel->server(), &SubmissionServer::started, this, [this](const QHostAddress &,
	                                                                       quint16 port) {
		// 面板在 start() 返回后才算出完整 URL，这里延迟到事件循环再取
		QTimer::singleShot(0, this, [this, port]() {
			statusDotLabel->setText(tr("● HTTP 运行中"));
			statusDotLabel->setStyleSheet(QStringLiteral("color: #65A30D; font-weight: 600;"));
			const auto url = onlinePanel->serviceUrl();
			statusAddrLabel->setText(url.isEmpty() ? QStringLiteral(":%1").arg(port) : url);
			statusCopyBtn->setEnabled(true);
			statusBrowserBtn->setEnabled(true);
			statusFirewallBtn->setEnabled(true);
		});
	});
	connect(onlinePanel->server(), &SubmissionServer::stopped, this, [this]() {
		statusDotLabel->setText(tr("● HTTP 已停止"));
		statusDotLabel->setStyleSheet(QStringLiteral("color: #94A3B8;"));
		statusAddrLabel->setText(QString());
		statusCopyBtn->setEnabled(false);
		statusBrowserBtn->setEnabled(false);
		statusFirewallBtn->setEnabled(false);
	});
	connect(statusCopyBtn, &QPushButton::clicked, this, [this]() {
		QGuiApplication::clipboard()->setText(statusAddrLabel->text());
		ui->statusBar->showMessage(tr("已复制访问地址"), 2000);
	});
	connect(statusBrowserBtn, &QPushButton::clicked, this, [this]() {
		QDesktopServices::openUrl(QUrl(onlinePanel->serviceUrl()));
	});
	connect(statusFirewallBtn, &QPushButton::clicked, this, [this]() {
		// netsh 需要管理员权限：经 ShellExecute RunAs 拉起 UAC 授权窗
		const auto port = QString::number(onlinePanel->server()->port());
		const auto args = QStringLiteral(
		                      "advfirewall firewall add rule name=LemonLimeOnline dir=in action=allow "
		                      "protocol=TCP localport=%1")
		                      .arg(port);
		const auto cmd = QStringLiteral(
		    "Start-Process netsh -ArgumentList '%1' -Verb RunAs").arg(args);
		QProcess::startDetached(QStringLiteral("powershell.exe"),
		                        {QStringLiteral("-NoProfile"), QStringLiteral("-Command"), cmd});
		ui->statusBar->showMessage(tr("已请求放行 TCP %1（请在弹出的授权窗口中确认）").arg(port), 6000);
	});
}

// 成绩标签页直接使用 .ui 里的 resultViewer（原生成绩表，支持选中评测/右键菜单），
// 不再额外自建得分表——之前两表并排显示属于重复。

void LemonLime::judgeExtButtonFlip(bool stat) {
	ui->judgeAllButton->setEnabled(stat);
	ui->judgeAllAction->setEnabled(stat);
	ui->judgeUnjudgedButton->setEnabled(stat);
	ui->judgeUnjudgedAction->setEnabled(stat);
	ui->judgeGreyAction->setEnabled(stat);
	ui->judgeMagentaAction->setEnabled(stat);
}

void LemonLime::refreshButtonClicked() {
	curContest->refreshContestantList();
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	judgeExtButtonFlip(ui->resultViewer->rowCount() > 0);
	ui->cleanupAction->setEnabled(true);
	ui->refreshAction->setEnabled(true);
}

void removePath(const QString &path) {
	if (path.isEmpty())
		return;

	QDir dir(path);

	if (! dir.exists())
		return;

	dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

	for (const auto &fi : dir.entryInfoList()) {
		if (fi.isFile() || fi.isSymLink())
			fi.dir().remove(fi.fileName());
		else
			removePath(fi.absoluteFilePath());
	}

	dir.rmpath(dir.absolutePath());
}

void copyPath(const QString &fromPath, const QString &toPath) {
	QDir dir(fromPath);

	if (! dir.exists())
		return;

	QString fpath = fromPath + QDir::separator();
	QString tpath = toPath + QDir::separator();
	dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

	for (const auto &fi : dir.entryInfoList()) {
		QString fn = fpath + fi.fileName();
		QString tn = tpath + fi.fileName();

		if (fi.isFile() || fi.isSymLink())
			QFile::copy(fn, tn);
		else {
			QDir toDir(toPath);
			toDir.mkpath(fi.fileName());
			copyPath(fn, tn);
		}
	}
}

void LemonLime::cleanupButtonClicked() {
	QString text;
	text += tr("Are you sure to Clean up Files?") + "<br>";
	text += tr("Reading guide are recommended.") + "<br>";
	QMessageBox::StandardButton res =
	    QMessageBox::warning(this, tr("Clean up Files"), text,
	                         QMessageBox::Yes | QMessageBox::No | QMessageBox::Abort, QMessageBox::No);

	if (res == QMessageBox::Yes) {
		QDir basDir(Settings::sourcePath());
		basDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
		QFileInfoList basDirLis = basDir.entryInfoList();
		int tarcnt = basDirLis.size();
		QString backupFolder = "source_bak_%1";
		int backupNum = 0;
		QDir tempBackupLoca;

		while (tempBackupLoca.exists(backupFolder.arg(backupNum)))
			backupNum++;

		backupFolder = backupFolder.arg(backupNum);
		text = tr("Making backup files to dir <br> `%1'?").arg(backupFolder) + "<br>";
		QMessageBox::StandardButton doBackup = QMessageBox::information(
		    this, tr("Clean up Files"), text, QMessageBox::Yes | QMessageBox::No | QMessageBox::Abort,
		    QMessageBox::Yes);

		if (doBackup == QMessageBox::Abort) {
			QMessageBox::information(this, tr("Clean up Files"), tr("Aborted."));
			return;
		}

		if (doBackup == QMessageBox::Yes) {
			QDir bkLoca;

			if (bkLoca.exists(backupFolder)) {
				QMessageBox::information(this, tr("Clean up Files"),
				                         tr("Aborted: `%1' already exist.").arg(backupFolder));
				return;
			}

			if (! bkLoca.mkpath(backupFolder)) {
				QMessageBox::information(this, tr("Clean up Files"),
				                         tr("Aborted: Cannot make dir `%1'.").arg(backupFolder));
				return;
			}

			bkLoca = QDir(backupFolder);
			auto *bkProcess = new QProgressDialog(tr("Making Backup..."), "", 0, 0, this);
			bkProcess->setWindowModality(Qt::WindowModal);
			bkProcess->setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
			bkProcess->setMinimumDuration(0);
			bkProcess->setCancelButton(nullptr);
			bkProcess->setRange(0, tarcnt);
			bkProcess->setValue(0);
			QCoreApplication::processEvents();
			basDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

			for (const auto &conDirWho : basDir.entryInfoList()) {
				bkLoca.mkpath(conDirWho.fileName());
				copyPath(conDirWho.path() + QDir::separator() + conDirWho.fileName(),
				         bkLoca.path() + QDir::separator() + conDirWho.fileName());
				bkProcess->setValue(bkProcess->value() + 1);
				QCoreApplication::processEvents();
			}

			delete bkProcess;
		}

		auto *process = new QProgressDialog(tr("Cleaning"), "", 0, 0, this);
		process->setWindowModality(Qt::WindowModal);
		process->setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
		process->setMinimumDuration(0);
		process->setCancelButton(nullptr);
		process->setRange(0, 5);
		process->setValue(0);
		process->setLabelText(tr("Working on it..."));
		QCoreApplication::processEvents();
		process->setRange(0, tarcnt + 5);
		process->setValue(0);
		process->setModal(true);
		process->setLabelText(tr("Fetching Data..."));
		QCoreApplication::processEvents();
		QSet<QString> tarNameSet;
		QSet<QString> nameSet;
		QMap<QString, int> typeSet;
		QMap<QString, QString> origSet;
		QList<Task *> taskList = curContest->getTaskList();
		process->setValue(1);
		process->setLabelText(tr("Initing..."));
		QCoreApplication::processEvents();

		for (int i = 0; i < taskList.size(); i++) {
			QString taskName = taskList[i]->getSourceFileName();
			typeSet[taskName] = i;
			nameSet.insert(taskName);

			if (taskList[i]->getTaskType() == Task::AnswersOnly) {
				for (auto *j : taskList[i]->getTestCaseList()) {
					for (const auto &k : j->getInputFiles()) {
						QString temp = QFileInfo(k).completeBaseName();
						tarNameSet.insert(temp);
						origSet[temp] = taskName;
					}
				}
			} else if (taskList[i]->getTaskType() == Task::Communication ||
			           taskList[i]->getTaskType() == Task::CommunicationExec) {
				QStringList sourcePaths = taskList[i]->getSourceFilesPath();

				for (const auto &j : sourcePaths) {
					QString temp = QFileInfo(j).completeBaseName();
					tarNameSet.insert(temp);
					origSet[temp] = taskName;
				}
			} else {
				tarNameSet.insert(taskName);
				origSet[taskName] = taskName;
			}
		}

		process->setValue(5);
		process->setLabelText(tr("Now Cleaning..."));
		QCoreApplication::processEvents();
		basDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

		for (const auto &conDirWho : basDir.entryInfoList()) {
			QDir conDir(conDirWho.filePath());
			conDir.setFilter(QDir::Files | QDir::Hidden);

			for (const auto &proFilWho : conDir.entryInfoList()) {
				if (proFilWho.suffix().length() <= 0 || proFilWho.suffix().toUpper() == "EXE")
					QFile::remove(proFilWho.absoluteFilePath());
			}

			conDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

			for (const auto &proDirWho : conDir.entryInfoList()) {
				if (nameSet.contains(proDirWho.fileName())) {
					QDir proDir(proDirWho.filePath());
					proDir.setFilter(QDir::Files | QDir::Hidden);

					for (const auto &sorFilWho : proDir.entryInfoList()) {
						if (sorFilWho.suffix().length() > 0 && sorFilWho.suffix().toUpper() != "EXE")
							QFile::copy(sorFilWho.filePath(),
							            conDirWho.filePath() + QDir::separator() + sorFilWho.fileName());
					}
				}

				removePath(proDirWho.absoluteFilePath());
			}

			for (const auto &proName : nameSet) {
				conDir.mkpath(proName);
			}

			conDir.setFilter(QDir::Files | QDir::Hidden);

			for (const auto &proFilWho : conDir.entryInfoList()) {
				QString proFilName = proFilWho.fileName();
				QString proName = proFilName;
				proName.truncate(proName.lastIndexOf("."));

				if (tarNameSet.contains(proName)) {
					QString taskName = origSet[proName];
					int who = typeSet[taskName];
					int types = taskList[who]->getTaskType();

					if (types == Task::Traditional || types == Task::Interaction ||
					    types == Task::Communication || types == Task::CommunicationExec) {
						if (proFilName != taskList[who]->getInputFileName() &&
						    proFilName != taskList[who]->getOutputFileName())
							QFile::copy(proFilWho.filePath(), conDirWho.filePath() + QDir::separator() +
							                                      taskName + QDir::separator() + proFilName);
					} else if (types == Task::AnswersOnly) {
						if (proFilWho.suffix() == taskList[who]->getAnswerFileExtension())
							QFile::copy(proFilWho.filePath(), conDirWho.filePath() + QDir::separator() +
							                                      taskName + QDir::separator() + proFilName);
					}
				}

				QFile::remove(proFilWho.absoluteFilePath());
			}

			conDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

			for (const auto &proDirWho : conDir.entryInfoList()) {
				QDir proDir(proDirWho.filePath());
				proDir.setFilter(QDir::Files | QDir::Hidden);

				for (const auto &sorFilWho : proDir.entryInfoList())
					QFile::copy(sorFilWho.filePath(),
					            conDirWho.filePath() + QDir::separator() + sorFilWho.fileName());
			}

			process->setValue(process->value() + 1);
			QCoreApplication::processEvents();
		}

		delete process;
		text = tr("Finished.") + "<br>";
		QMessageBox::information(this, tr("Clean up Files"), text);
	} else {
		QMessageBox::information(this, tr("Clean up Files"), tr("Aborted"));
	}
}

void LemonLime::tabIndexChanged(int index) {
	Q_UNUSED(index)
	if (ui->tabWidget->currentWidget() != ui->scoreTab) {
		judgeExtButtonFlip(false);
		ui->judgeAction->setEnabled(false);
		ui->judgeButton->setEnabled(false);
		ui->cleanupAction->setEnabled(false);
		ui->refreshAction->setEnabled(false);

		// 标签重排后不能再依赖索引：按控件判断
		if (ui->tabWidget->currentWidget() == ui->statisticsTab)
			ui->statisticsBrowser->refresh();
	} else {
		QList<QTableWidgetSelectionRange> selectionRange = ui->resultViewer->selectedRanges();

		if (! selectionRange.empty()) {
			ui->judgeAction->setEnabled(true);
			ui->judgeButton->setEnabled(true);
		} else {
			ui->judgeAction->setEnabled(false);
			ui->judgeButton->setEnabled(false);
		}

		judgeExtButtonFlip(ui->resultViewer->rowCount() > 0);
		ui->cleanupAction->setEnabled(true);
		ui->refreshAction->setEnabled(true);
	}
}

void LemonLime::moveUpTask() {
	QTreeWidgetItem *curItem = ui->summary->currentItem();

	if (! curItem)
		return;

	int index = ui->summary->indexOfTopLevelItem(curItem);
	curContest->swapTask(index - 1, index);
	ui->summary->setContest(curContest);
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	curItem = ui->summary->topLevelItem(index - 1);

	if (! curItem)
		curItem = ui->summary->topLevelItem(index);

	if (curItem)
		ui->summary->setCurrentItem(curItem);
}

void LemonLime::moveDownTask() {
	QTreeWidgetItem *curItem = ui->summary->currentItem();

	if (! curItem)
		return;

	int index = ui->summary->indexOfTopLevelItem(curItem);
	curContest->swapTask(index + 1, index);
	ui->summary->setContest(curContest);
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	curItem = ui->summary->topLevelItem(index + 1);

	if (! curItem)
		curItem = ui->summary->topLevelItem(index);

	if (curItem)
		ui->summary->setCurrentItem(curItem);
}

void LemonLime::viewerSelectionChanged() {
	QList<QTableWidgetSelectionRange> selectionRange = ui->resultViewer->selectedRanges();

	if (! selectionRange.empty()) {
		ui->judgeButton->setEnabled(true);
		ui->judgeAction->setEnabled(true);
	} else {
		ui->judgeButton->setEnabled(false);
		ui->judgeAction->setEnabled(false);
	}
}

void LemonLime::contestantDeleted() {
	judgeExtButtonFlip(ui->resultViewer->rowCount() > 0);
	ui->cleanupAction->setEnabled(true);
	ui->refreshAction->setEnabled(true);
}

void LemonLime::saveContest(const QString &fileName) {
	QFile file(fileName);

	if (! file.open(QFile::WriteOnly)) {
		QMessageBox::warning(this, tr("Error"), tr("Cannot open file %1").arg(fileName), QMessageBox::Close);
		ui->statusBar->showMessage(tr("Save Failed"), 1000);
		WARN(fileName, "Save Failed");
		return;
	}

	QApplication::setOverrideCursor(Qt::WaitCursor);
	QJsonObject out;
	curContest->writeToJson(out);
	file.write(QJsonDocument(out).toJson(QJsonDocument::Compact));
	/* QByteArray data;
	QDataStream _out(&data, QIODevice::WriteOnly);
	curContest->writeToStream(_out);
	data = qCompress(data);
	QDataStream out(&file);
	out << unsigned(MagicNumber) << qChecksum(QByteArrayView(data))
	    << static_cast<int>(data.length()); // Qt 6+ uses qsizetype for length
	out.writeRawData(data.data(), data.length()); */
	QApplication::restoreOverrideCursor();
	ui->statusBar->showMessage(tr("Saved"), 1000);
}

void LemonLime::loadContest(const QString &filePath) {
	if (curContest)
		closeAction();

	curContest = new Contest(this);

	QFile file(filePath);

	if (! file.open(QFile::ReadOnly)) {
		QMessageBox::warning(this, tr("Error"), tr("Cannot open file %1").arg(QFileInfo(filePath).fileName()),
		                     QMessageBox::Close);
		return;
	}
	char firstChar;
	file.peek(&firstChar, 1);
	// Don't support RFC 7159, but support RFC 4627
	if (firstChar == '[' || firstChar == '{') {
		QJsonParseError parseError;
		QJsonObject inObj(QJsonDocument::fromJson(file.readAll(), &parseError).object());
		if (parseError.error != 0) {
			QMessageBox::warning(this, tr("Error"),
			                     tr("File %1 is broken").arg(QFileInfo(filePath).fileName()) + "\n" +
			                         parseError.errorString() + "at position" +
			                         QString("%1").arg(parseError.offset),
			                     QMessageBox::Close);
			return;
		}
		QApplication::setOverrideCursor(Qt::WaitCursor);
		curContest->setSettings(settings);
		if (curContest->readFromJson(inObj) == -1) {
			QMessageBox::warning(this, tr("Error"),
			                     tr("File %1 is broken").arg(QFileInfo(filePath).fileName()),
			                     QMessageBox::Close);
			QApplication::restoreOverrideCursor();
			return;
		}
	} else {
		QDataStream _in(&file);
		unsigned checkNumber = 0;
		_in >> checkNumber;

		if (checkNumber != unsigned(MagicNumber)) {
			QMessageBox::warning(this, tr("Error"),
			                     tr("File %1 is broken").arg(QFileInfo(filePath).fileName()),
			                     QMessageBox::Close);
			return;
		}

		quint16 checksum = 0;
		int len = 0;
		_in >> checksum >> len;
		char *raw = new char[len];
		_in.readRawData(raw, len);

		if (qChecksum(QByteArrayView(raw, static_cast<uint>(len))) != checksum) {
			QMessageBox::warning(this, tr("Error"),
			                     tr("File %1 is broken").arg(QFileInfo(filePath).fileName()),
			                     QMessageBox::Close);
			delete[] raw;
			return;
		}

		QByteArray data(raw, len);
		delete[] raw;
		data = qUncompress(data);
		QDataStream in(data);
		QApplication::setOverrideCursor(Qt::WaitCursor);
		curContest->setSettings(settings);
		curContest->readFromStream(in);
	}
	curFile = QFileInfo(filePath).fileName();
	QDir::setCurrent(QFileInfo(filePath).path());
	QDir().mkdir(Settings::dataPath());
	QDir().mkdir(Settings::sourcePath());
	ui->summary->setContest(curContest);
	ui->resultViewer->setContest(curContest);
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->setContest(curContest);
	ui->statisticsBrowser->refresh();
	ui->tabWidget->setVisible(true);
	updateContestCard();
	resetDataWatcher();
	ui->closeAction->setEnabled(true);
	ui->openFolderAction->setEnabled(true);
	ui->saveAction->setEnabled(true);
	ui->addTasksAction->setEnabled(true);
	ui->exportAction->setEnabled(true);
	ui->actionExportStatistics->setEnabled(true);
	ui->actionChangeContestName->setEnabled(true);
	ui->cleanupAction->setEnabled(false);
	ui->refreshAction->setEnabled(false);
	setWindowTitle(tr("LemonLime - %1").arg(curContest->getContestTitle()));
	ui->tabWidget->setCurrentIndex(0);
	QApplication::restoreOverrideCursor();
	LOG("Contest -", curContest->getContestTitle(), "loaded successfully");
}

void LemonLime::newContest(const QString &title, const QString &savingName, const QString &path) {
	if (! QDir(path).exists() && ! QDir().mkpath(path)) {
		QMessageBox::warning(this, tr("Error"), tr("Cannot make contest path"), QMessageBox::Close);
		return;
	}

	if (curContest)
		closeAction();

	curContest = new Contest(this);
	curContest->setSettings(settings);
	curContest->setContestTitle(title);
	setWindowTitle(tr("LemonLime - %1").arg(title));
	QDir::setCurrent(path);
	QDir().mkdir(Settings::dataPath());
	QDir().mkdir(Settings::sourcePath());
	curFile = savingName + ".cdf";
	saveContest(curFile);
	ui->summary->setContest(curContest);
	ui->resultViewer->setContest(curContest);
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->setContest(curContest);
	ui->statisticsBrowser->refresh();
	ui->tabWidget->setVisible(true);
	updateContestCard();
	resetDataWatcher();
	ui->closeAction->setEnabled(true);
	ui->openFolderAction->setEnabled(true);
	ui->saveAction->setEnabled(true);
	ui->addTasksAction->setEnabled(true);
	ui->exportAction->setEnabled(true);
	ui->actionExportStatistics->setEnabled(true);
	ui->actionChangeContestName->setEnabled(true);
	ui->cleanupAction->setEnabled(false);
	ui->refreshAction->setEnabled(false);
	QStringList recentContest = settings->getRecentContest();
	recentContest.append(QDir::toNativeSeparators((QDir().absoluteFilePath(curFile))));
	settings->setRecentContest(recentContest);
	ui->tabWidget->setCurrentIndex(0);
	LOG("New Contest -", title);
}

void LemonLime::newAction() {
	// 新版向导：比赛信息 → 拖入数据自动识别并装配题目 → 创建。
	// 设与题在同一次向导里完成，所以不再需要「先手工建好 data/ 与题目文件夹」。
	auto *wizard = new NewContestWizard(settings, this);

	if (wizard->exec() != QDialog::Accepted) {
		delete wizard;
		return;
	}

	const QString title = wizard->contestTitle();
	const QString savingName = wizard->savingName();
	const QString path = wizard->contestDir();
	const QList<PlannedProblem> problems = wizard->plannedProblems();
	delete wizard;

	newContest(title, savingName, path);

	if (! curContest)
		return;

	applyImportedProblems(problems);
	updateContestCard(); // 题目在 newContest 之后装配，卡片元信息需再刷一次
}

// 主窗口骨架联动：有比赛 -> 顶部卡片填充 + 比赛页；无比赛 -> 隐藏卡片 + 空状态页（创建/打开入口）。
// 打开另一场比赛会覆盖当前比赛（loadContest/newContest 开头先 closeAction），不设独立的“关闭比赛”按钮。
void LemonLime::updateContestCard() {
	if (! curContest) {
		ui->contestCard->setVisible(false);
		ui->mainStack->setCurrentIndex(1);
		return;
	}

	ui->contestTitle->setText(curContest->getContestTitle());
	ui->contestMeta->setText(tr("%1 task(s) · %2 contestant(s) · %3")
	                             .arg(curContest->getTaskList().size())
	                             .arg(curContest->getContestantList().size())
	                             .arg(QDir::toNativeSeparators(QDir::currentPath())));
	ui->contestCard->setVisible(true);
	ui->mainStack->setCurrentIndex(0);
	// 比赛变化后同步：服务绑定比赛
	if (onlinePanel)
		onlinePanel->bindContest(curContest, QDir::currentPath());
}

void LemonLime::closeAction() {
	saveContest(curFile);
	ui->summary->setContest(nullptr);
	ui->taskEdit->setEditTask(nullptr);
	ui->resultViewer->setContest(nullptr);
	ui->statisticsBrowser->setContest(nullptr);
	delete curContest;
	curContest = nullptr;
	ui->tabWidget->setCurrentIndex(0);
	ui->tabWidget->setVisible(false);
	updateContestCard();
	ui->closeAction->setEnabled(false);
	ui->openFolderAction->setEnabled(false);
	ui->saveAction->setEnabled(false);
	ui->addTasksAction->setEnabled(false);
	ui->exportAction->setEnabled(false);
	ui->actionExportStatistics->setEnabled(false);
	ui->actionChangeContestName->setEnabled(false);
	ui->cleanupAction->setEnabled(false);
	ui->refreshAction->setEnabled(false);
	setWindowTitle(tr("LemonLime"));
}

void LemonLime::saveAction() { saveContest(curFile); }

void LemonLime::openFolderAction() { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::currentPath())); }

void LemonLime::loadAction() {
	auto *dialog = new OpenContestDialog(this);
	dialog->setRecentContest(settings->getRecentContest());
	QStringList recentContest = dialog->getRecentContest();

	if (dialog->exec() == QDialog::Accepted) {
		QString selectedContest = dialog->getSelectedContest();

		for (int i = 0; i < recentContest.size(); i++) {
			if (recentContest[i] == selectedContest) {
				recentContest.removeAt(i);
				break;
			}
		}

		recentContest.prepend(selectedContest);
		loadContest(selectedContest);
	}

	settings->setRecentContest(recentContest);
	delete dialog;
}

void LemonLime::getFiles(const QString &path, const QStringList &filters, QMap<QString, QString> &files) {
	QDir dir(path);

	if (! filters.isEmpty())
		dir.setNameFilters(filters);

	QFileInfoList list = dir.entryInfoList(QDir::Files);

	for (auto &i : list) {
		files.insert(i.completeBaseName(), i.fileName());
	}
}

void LemonLime::addTask(const QString &title, const QList<std::pair<QString, QString>> &testCases,
                        int fullScore, int timeLimit, int memoryLimit) {
	Task *newTask = new Task;
	newTask->setProblemTitle(title);
	newTask->setSourceFileName(title);
	newTask->setInputFileName(title + ".in");
	newTask->setOutputFileName(title + ".out");
	newTask->refreshCompilerConfiguration(settings);
	newTask->setAnswerFileExtension(settings->getDefaultOutputFileExtension());
	curContest->addTask(newTask);

	for (const auto &testCase : testCases) {
		auto *newTestCase = new TestCase;
		newTestCase->setFullScore(fullScore);
		newTestCase->setTimeLimit(timeLimit);
		newTestCase->setMemoryLimit(memoryLimit);
		newTestCase->addSingleCase(title + QDir::separator() + testCase.first,
		                           title + QDir::separator() + testCase.second);
		newTask->addTestCase(newTestCase);
	}
}

void LemonLime::addTaskWithScoreScale(const QString &title,
                                      const QList<std::pair<QString, QString>> &testCases, int sumScore,
                                      int timeLimit, int memoryLimit) {
	Task *newTask = new Task;
	newTask->setProblemTitle(title);
	newTask->setSourceFileName(title);
	newTask->setInputFileName(title + ".in");
	newTask->setOutputFileName(title + ".out");
	newTask->refreshCompilerConfiguration(settings);
	newTask->setAnswerFileExtension(settings->getDefaultOutputFileExtension());
	curContest->addTask(newTask);
	int scorePer = sumScore / testCases.size();
	int scoreLos = sumScore - scorePer * testCases.size();

	for (int i = 0; i < testCases.size(); i++) {
		auto *newTestCase = new TestCase;
		newTestCase->setFullScore(scorePer + static_cast<int>(i < scoreLos));
		newTestCase->setTimeLimit(timeLimit);
		newTestCase->setMemoryLimit(memoryLimit);
		newTestCase->addSingleCase(title + QDir::separator() + testCases[i].first,
		                           title + QDir::separator() + testCases[i].second);
		newTask->addTestCase(newTestCase);
	}
}

auto LemonLime::compareFileName(const std::pair<QString, QString> &a,
                                const std::pair<QString, QString> &b) -> bool {
	return (a.first.length() < b.first.length()) ||
	       (a.first.length() == b.first.length() && QString::localeAwareCompare(a.first, b.first) < 0);
}

void LemonLime::addTasksAction() {
	if (! curContest) {
		QMessageBox::warning(this, tr("LemonLime"), tr("请先新建或打开一场比赛。"), QMessageBox::Ok);
		return;
	}

	// 新版向导：先选题型，再按题型提示需要填什么、传什么。
	// 打开时会先把当前比赛 data/ 里尚未导入的题目扫出来列好，保持原来的便利性。
	auto *wizard = new AddProblemWizard(settings, this);

	QStringList existingNames;

	for (Task *task : curContest->getTaskList())
		existingNames.append(task->getSourceFileName());

	wizard->preloadFromDataDir(existingNames);

	if (wizard->exec() != QDialog::Accepted) {
		delete wizard;
		return;
	}

	const QList<PlannedProblem> plans = wizard->plans();
	delete wizard;

	applyImportedProblems(plans);
}

void LemonLime::applyImportedProblems(const QList<PlannedProblem> &problems) {
	if (! curContest || problems.isEmpty())
		return;

	// 当前工作目录就是比赛目录（newContest / loadContest 都做过 setCurrent）
	ProblemInstaller installer;
	installer.setContestDir(QDir::currentPath());
	installer.setOverwrite(ProblemInstaller::Skip);

	int imported = 0;
	int copiedFiles = 0;
	int skippedFiles = 0;
	QStringList failures;

	for (const PlannedProblem &plan : problems) {
		ProblemInstaller::Report report;
		QString error;

		if (! installer.install(plan.scan, &report, &error)) {
			failures.append(QStringLiteral("%1：%2")
			                    .arg(plan.scan.englishName.isEmpty() ? plan.scan.title
			                                                         : plan.scan.englishName,
			                         error));
			continue;
		}

		copiedFiles += report.written;
		skippedFiles += report.skipped;

		auto *newTask = new Task;
		newTask->setProblemTitle(plan.scan.title);
		newTask->setSourceFileName(plan.scan.englishName);
		newTask->setInputFileName(plan.scan.englishName + QStringLiteral(".in"));
		newTask->setOutputFileName(plan.scan.englishName + QStringLiteral(".out"));
		newTask->setSubFolderCheck(plan.subFolderCheck);
		newTask->setTaskType(plan.taskType);
		newTask->setComparisonMode(plan.comparisonMode);
		newTask->setRealPrecision(plan.realPrecision);

		if (! plan.diffArguments.isEmpty())
			newTask->setDiffArguments(plan.diffArguments);

		if (! plan.specialJudge.isEmpty())
			newTask->setSpecialJudge(plan.specialJudge);

		if (! plan.interactor.isEmpty()) {
			newTask->setInteractor(plan.interactor);
			newTask->setInteractorName(plan.interactorName.isEmpty()
			                               ? QFileInfo(plan.interactor).fileName()
			                               : plan.interactorName);
		}

		if (! plan.sourceFilesPath.isEmpty()) {
			newTask->setSourceFilesPath(plan.sourceFilesPath);
			newTask->setSourceFilesName(plan.sourceFilesName);
		}

		if (! plan.graderFilesPath.isEmpty()) {
			newTask->setGraderFilesPath(plan.graderFilesPath);
			newTask->setGraderFilesName(plan.graderFilesName);
		}

		newTask->refreshCompilerConfiguration(settings);
		newTask->setAnswerFileExtension(plan.answerFileExtension.isEmpty()
		                                    ? settings->getDefaultOutputFileExtension()
		                                    : plan.answerFileExtension);

		const int caseCount = static_cast<int>(plan.scan.cases.size());
		const int scorePerCase = caseCount > 0 ? plan.fullScore / caseCount : 0;
		const int scoreRemainder = caseCount > 0 ? plan.fullScore - scorePerCase * caseCount : 0;

		for (int i = 0; i < caseCount; i++) {
			const ScannedCase &one = plan.scan.cases.at(i);
			auto *newTestCase = new TestCase;
			newTestCase->setFullScore(scorePerCase + (i < scoreRemainder ? 1 : 0));
			newTestCase->setTimeLimit(plan.timeLimit);
			newTestCase->setMemoryLimit(plan.memoryLimit);
			newTestCase->addSingleCase(plan.scan.englishName + QDir::separator() + one.inputRel,
			                           plan.scan.englishName + QDir::separator() + one.outputRel);
			newTask->addTestCase(newTestCase);
		}

		curContest->addTask(newTask);
		++imported;
	}

	saveContest(curFile);
	ui->summary->setContest(curContest);
	ui->resultViewer->setContest(curContest);
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->setContest(curContest);
	ui->statisticsBrowser->refresh();
	resetDataWatcher();
	ui->cleanupAction->setEnabled(true);
	ui->refreshAction->setEnabled(true);

	if (! failures.isEmpty()) {
		QMessageBox::warning(this, tr("导入结果"),
		                     tr("有 %1 道题目没有导入成功：").arg(failures.size()) +
		                         QStringLiteral("\n\n") + failures.join(QStringLiteral("\n")));
	} else if (imported > 0) {
		QString text = tr("已导入 %1 道题目，写入 %2 个测试点文件。").arg(imported).arg(copiedFiles);

		if (skippedFiles > 0)
			text += QLatin1Char('\n') + tr("另有 %1 个文件因已存在而跳过。").arg(skippedFiles);

		QMessageBox::information(this, tr("导入完成"), text);
	}
}

void LemonLime::exportResult() { ExportUtil::exportResult(this, curContest); }

void LemonLime::exportStatistics() { StatisticsBrowser::exportStatistics(this, curContest); }

void LemonLime::changeContestName() {
	if (! curContest) {
		QMessageBox::warning(this, tr("Rename Contest"), tr("No Contest Yet"));
		return;
	}

	bool confirmed = false;
	QString newName = QInputDialog::getText(this, tr("Rename Contest"), tr("Write the name you want."),
	                                        QLineEdit::Normal, tr("New Name"), &confirmed);

	if (! confirmed) {
		QMessageBox::warning(this, tr("Rename Contest"), tr("The name did not changes."));
		return;
	}

	curContest->setContestTitle(newName);
	setWindowTitle(tr("LemonLime - %1").arg(curContest->getContestTitle()));
	// 重命名后立刻刷新顶部比赛卡片标题与在线服务面板里的比赛标题，
	// 免去「关掉比赛再打开才更新」的绕路（updateContestCard 内部会重新 bindContest）。
	updateContestCard();
	ui->resultViewer->refreshViewer();
	ui->statisticsBrowser->refresh();
	saveContest(curFile);
}

void LemonLime::aboutLemon() {
	QString text;
	text += "<h2>Project LemonLime</h2>";
	text +=
	    "<h3>" +
	    tr("Version: %1").arg(QString(LEMON_VERSION_STRING) + QString(":") + QString(LEMON_VERSION_BUILD)) +
	    "</h3>";
	text += tr("This is a tiny judging environment for OI contest based on Project LemonPlus.") + "<br>";
	text += tr("Based on Project Lemon version 1.2 Beta by Zhipeng Jia, 2011") + "<br>";
	text += tr("Based on Project LemonPlus by Dust1404, 2019") + "<br>";
	text += tr("Update by iotang and Coelacanthus") + "<br><br>";
	text += tr("Build Info: %1").arg(QString(LEMON_BUILD_INFO_STR)) + "<br>";
	text += tr("Build Extra Info: %1").arg(QString(LEMON_BUILD_EXTRA_INFO_STR)) + "<br>";
	text += tr("Build Date: %1").arg(QString(__DATE__) + QString(", ") + QString(__TIME__)) + "<br>";
	text += tr("This program is under the <a href=\"http://www.gnu.org/licenses/gpl-3.0.html\">GPLv3</a> "
	           "license") +
	        "<br>";
	QMessageBox::about(this, tr("About LemonLime"), text);
}

void LemonLime::actionManual() {
#ifndef LEMON_EMBED_DOCS
	QString fileName("/usr/share/doc/lemon-lime/llmanual.pdf");
#else
	QString fileName = QFileDialog::getSaveFileName(this, tr("Manual"), "llmanual.pdf");

	if (fileName.isEmpty())
		return;

	QFile::copy(":/manual/llmanual.pdf", fileName);
#endif
	QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
}

void LemonLime::actionMore() {
	QDesktopServices::openUrl(QUrl(QString("https://github.com/Project-LemonLime/Project_LemonLime")));
}
