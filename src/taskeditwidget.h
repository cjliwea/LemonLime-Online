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

#include <QShowEvent>
#include <QWidget>

namespace Ui {
	class TaskEditWidget;
}

class Settings;
class Task;

class TaskEditWidget : public QWidget {
	Q_OBJECT

  public:
	explicit TaskEditWidget(QWidget *parent = nullptr);
	~TaskEditWidget();
	void changeEvent(QEvent *);
	void showEvent(QShowEvent *);
	void setEditTask(Task *);
	void setSettings(Settings *);

  private:
	Ui::TaskEditWidget *ui;
	Settings *settings{};
	Task *editTask;
	void refreshWidgetState();
	void refreshTaskInfo(); // 刷新顶部信息条（测试点 / 时限 / 内存 / 满分）
	void applyBulkTimeLimit();  // 信息条时限框编辑完成 -> 应用到全部测试点
	void applyBulkMemoryLimit(); // 信息条内存框编辑完成 -> 应用到全部测试点
	void applyBulkFullScore();  // 信息条满分框编辑完成 -> 按测试点数均分
	void addSourceFiles(const QString &, const QString &);
	void addGraderFiles(const QString &, const QString &);
	void rmSourceFilesAt(int);
	void rmGraderFilesAt(int);
	void multiFilesRefresh();

  private slots:
	void problemTitleChanged(const QString &);
	void setToTraditional(bool);
	void setToAnswersOnly(bool);
	void setToInteraction(bool);
	void setToCommunication(bool);
	void setToCommunicationExec(bool);
	void sourceFileNameChanged(const QString &);
	void subFolderCheckChanged();
	void inputFileNameChanged(const QString &);
	void outputFileNameChanged(const QString &);
	void standardInputCheckChanged();
	void standardOutputCheckChanged();
	void comparisonModeChanged();
	void diffArgumentsChanged(const QString &);
	void realPrecisionChanged(int);
	void specialJudgeChanged(const QString &);
	void interactorChanged(const QString &);
	void interactorNameChanged(const QString &);
	void graderChanged(const QString &);
	void refreshProblemTitle(const QString &);
	void refreshCompilerConfiguration();
	void compilerSelectionChanged();
	void configurationSelectionChanged();
	void answerFileExtensionChanged(const QString &);
	void addSourceFileClicked();
	void addGraderFileClicked();
	void rmSourceFileClicked();
	void rmGraderFileClicked();

  signals:
	void dataPathChanged();
};
