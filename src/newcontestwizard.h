/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "contestscanner.h"

#include <QWizard>

class DropArea;
class ProblemTable;
class QLabel;
class QLineEdit;
class Settings;

// 「新建比赛」向导：比赛信息 → 拖入数据自动识别并批量装配题目 → 确认创建。
// 设和题在同一次向导里完成，因此「先建目录后建比赛」的顺序约束不再存在。
class NewContestWizard : public QWizard {
	Q_OBJECT

  public:
	explicit NewContestWizard(Settings *settings, QWidget *parent = nullptr);

	auto contestTitle() const -> QString;
	auto savingName() const -> QString;
	auto parentDir() const -> QString;
	auto contestDir() const -> QString;

	auto plannedProblems() -> QList<PlannedProblem>;

  protected:
	bool validateCurrentPage() override;
	void initializePage(int id) override;
	void dragEnterEvent(QDragEnterEvent *) override;
	void dragMoveEvent(QDragMoveEvent *) override;
	void dropEvent(QDropEvent *) override;

  private:
	enum PageId { PageBasic = 0, PageImport = 1, PageFinish = 2 };

	auto buildBasicPage() -> QWizardPage *;
	auto buildImportPage() -> QWizardPage *;
	auto buildFinishPage() -> QWizardPage *;

	void chooseParentDir();
	void updatePathPreview();
	void refreshSummary();

	Settings *settings_{};
	QLineEdit *titleEdit_{};
	QLineEdit *nameEdit_{};
	QLineEdit *parentEdit_{};
	QLabel *pathPreview_{};
	QLabel *summaryLabel_{};
	ProblemTable *importPanel_{};
};
