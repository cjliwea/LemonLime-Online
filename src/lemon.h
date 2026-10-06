/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include "contestscanner.h"

#include <QMainWindow>
#include <QtCore>

namespace Ui {
	class LemonLime;
}

class Contest;
class Settings;
class OptionsDialog;
class OnlineServerDialog;
class QLabel;

class LemonLime : public QMainWindow {
	Q_OBJECT

  public:
	explicit LemonLime(QWidget *parent = nullptr);
	~LemonLime();
	void changeEvent(QEvent *);
	void closeEvent(QCloseEvent *);
	int getSplashTime();
	void welcome();

  private:
	Ui::LemonLime *ui;
	Contest *curContest;
	Settings *settings;
	OnlineServerDialog *onlineServerDialog{};
	QLabel *onlineSvcLabel{}; // 状态栏右侧的在线服务状态标签
	QFileSystemWatcher *dataDirWatcher;
	QString curFile;
	QSignalMapper *signalMapper;
	QMenu *TaskMenu;
	QList<QAction *> TaskList;
	QTimer autoSaveTimer;
	void judgeExtButtonFlip(bool);
	void loadUiLanguage();
	void insertWatchPath(const QString &, QFileSystemWatcher *);
	void newContest(const QString &, const QString &, const QString &);
	void saveContest(const QString &);
	void loadContest(const QString &);
	// 主窗口骨架：顶部比赛卡片 + 主区堆栈（比赛页 / 无比赛空状态页）
	// 有比赛 -> 卡片填充（标题/元信息）+ 显示标签页；无比赛 -> 隐藏卡片 + 空状态页（创建/打开入口）
	void updateContestCard();
	static void getFiles(const QString &, const QStringList &, QMap<QString, QString> &);
	void addTask(const QString &, const QList<std::pair<QString, QString>> &, int, int, int);
	void addTaskWithScoreScale(const QString &, const QList<std::pair<QString, QString>> &, int, int, int);
	static bool compareFileName(const std::pair<QString, QString> &, const std::pair<QString, QString> &);
	// 把向导确认过的题目统一落盘 + 挂到当前比赛（新建比赛与追加题目共用同一条路径）
	void applyImportedProblems(const QList<PlannedProblem> &);

  private slots:
	void summarySelectionChanged();
	void refreshSummary();
	void resetDataWatcher();
	void showOptionsDialog();
	void showOnlineServerDialog();
	void refreshButtonClicked();
	void cleanupButtonClicked();
	void tabIndexChanged(int);
	void moveUpTask();
	void moveDownTask();
	void viewerSelectionChanged();
	void contestantDeleted();
	void newAction();
	void saveAction();
	static void openFolderAction();
	void closeAction();
	void loadAction();
	void addTasksAction();
	void exportResult();
	void exportStatistics();
	void changeContestName();
	void aboutLemon();
	void actionManual();
	static void actionMore();

  signals:
	void dataPathChanged();
};
