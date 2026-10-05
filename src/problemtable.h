/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "contestscanner.h"

#include <QWidget>

class DropArea;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class Settings;

// 「拖入数据 → 自动识别 → 表格化确认」的可复用面板。
// 新建比赛向导与新建题目向导共用它，避免两处识别/落盘参数逻辑漂移。
class ProblemTable : public QWidget {
	Q_OBJECT

  public:
	explicit ProblemTable(Settings *settings, QWidget *parent = nullptr);

	void setDropHint(const QString &title, const QString &subtitle);

	// 追加识别（可多次调用，重复目录会自动去重）
	void addPaths(const QStringList &paths);
	void setProblems(const QList<ScannedProblem> &problems);
	void clear();

	// 把表格里的编辑同步回 planned_
	void commitEdits();

	// 已经存在的题目源文件名（再导入时不再重复列出）
	void setExcludedNames(const QStringList &names);

	const QList<PlannedProblem> &plannedProblems() const { return planned_; }
	QStringList warnings() const { return warnings_; }
	int containerCount() const { return containerCount_; }

	// 单题导入时可以用它指定表头的输入/输出列名
	void setSingleProblemMode(bool single);

  signals:
	void problemsChanged();

  private:
	void pickFolder();
	void pickFiles();
	void removeSelected();
	void applyBatch();
	void refreshTable();
	void refreshWarnings();
	void refreshDropHint();
	void appendScanResult(const ScanResult &result);

	Settings *settings_{};
	DropArea *dropArea_{};
	QLabel *warnLabel_{};
	QTableWidget *table_{};
	QComboBox *structureBox_{};
	QLineEdit *batchScore_{};
	QLineEdit *batchTime_{};
	QLineEdit *batchMemory_{};

	ContestScanner scanner_;
	QList<PlannedProblem> planned_;
	QStringList warnings_;
	QStringList excludedNames_;
	int containerCount_{};
	bool singleProblemMode_{};
};
