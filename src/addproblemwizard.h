/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "contestscanner.h"

#include <QWizard>

class ProblemTable;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class Settings;

// 「新建题目」向导：先选题型，再按题型提示需要填什么、传什么、配哪些参数。
// 同一个向导服务三个入口：比赛内「添加试题」、新建比赛后补题、编辑已有题目。
class AddProblemWizard : public QWizard {
	Q_OBJECT

  public:
	explicit AddProblemWizard(Settings *settings, QWidget *parent = nullptr);

	// 取出确认后的题目（可多道，批量导入时共用同一题型与参数）
	auto plans() -> QList<PlannedProblem>;

	// 打开时先把当前比赛 data/ 里还没导入的题目扫出来列好
	void preloadFromDataDir(const QStringList &existingSourceNames);

  protected:
	bool validateCurrentPage() override;
	void initializePage(int id) override;
	void dragEnterEvent(QDragEnterEvent *) override;
	void dragMoveEvent(QDragMoveEvent *) override;
	void dropEvent(QDropEvent *) override;
	void accept() override;

  private:
	enum PageId { PageType = 0, PageConfig = 1, PageSummary = 2 };

	auto buildTypePage() -> QWizardPage *;
	auto buildConfigPage() -> QWizardPage *;
	auto buildSummaryPage() -> QWizardPage *;

	void collect();
	void applyTypeVisibility();
	void refreshSummary();
	void chooseInteractor();
	void chooseGraderSource();
	void chooseGraderExecutable();

	Settings *settings_{};
	QListWidget *typeList_{};
	QLabel *typeTitle_{};
	QLabel *typeDescription_{};
	ProblemTable *table_{};
	QWidget *extraCard_{};
	QFormLayout *extraForm_{};
	QWidget *specialJudgeRow_{};
	QLineEdit *specialJudgeEdit_{};
	QWidget *interactorRow_{};
	QLineEdit *interactorEdit_{};
	QWidget *graderSourceRow_{};
	QLineEdit *graderSourceEdit_{};
	QWidget *graderExecRow_{};
	QLineEdit *graderExecEdit_{};
	QLineEdit *answerExtensionEdit_{};
	QLabel *summaryLabel_{};
	Task::TaskType currentType_ = Task::Traditional;
	QStringList graderSourcePaths_;
	QStringList graderExecPaths_;
	QList<PlannedProblem> collected_;
};
