/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "problemtable.h"

#include "base/settings.h"
#include "droparea.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

const int columnTitle = 0;
const int columnEnglish = 1;
const int columnCases = 2;
const int columnScore = 3;
const int columnTime = 4;
const int columnMemory = 5;
const int columnSource = 6;

auto readOnlyItem(const QString &text) -> QTableWidgetItem * {
	auto *item = new QTableWidgetItem(text);
	item->setFlags(item->flags() & ~Qt::ItemIsEditable);
	return item;
}

} // namespace

ProblemTable::ProblemTable(Settings *settings, QWidget *parent) : QWidget(parent), settings_(settings) {
	if (settings_)
		scanner_.setExtensions(settings_->getInputFileExtensions(), settings_->getOutputFileExtensions());

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(10);

	warnLabel_ = new QLabel(this);
	warnLabel_->setObjectName(QStringLiteral("WarnBanner"));
	warnLabel_->setWordWrap(true);
	warnLabel_->setVisible(false);
	outer->addWidget(warnLabel_);

	dropArea_ = new DropArea(this);
	outer->addWidget(dropArea_);

	auto *buttonRow = new QHBoxLayout;
	buttonRow->setSpacing(8);

	auto *pickFolderButton = new QPushButton(tr("选择文件夹…"), this);
	auto *pickFilesButton = new QPushButton(tr("选择测试点文件…"), this);
	auto *removeButton = new QPushButton(tr("删除选中行"), this);
	auto *clearButton = new QPushButton(tr("清空列表"), this);

	buttonRow->addWidget(pickFolderButton);
	buttonRow->addWidget(pickFilesButton);
	buttonRow->addWidget(removeButton);
	buttonRow->addStretch();
	buttonRow->addWidget(clearButton);
	outer->addLayout(buttonRow);

	table_ = new QTableWidget(this);
	table_->setColumnCount(7);
	table_->setHorizontalHeaderLabels({tr("题目名"), tr("英文名 / 源文件名"), tr("测试点"), tr("总分"),
	                                   tr("时限 (ms)"), tr("内存 (MiB)"), tr("来源目录")});
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked |
	                        QAbstractItemView::EditKeyPressed);
	table_->verticalHeader()->setVisible(false);
	table_->horizontalHeader()->setSectionResizeMode(columnTitle, QHeaderView::Interactive);
	table_->horizontalHeader()->setSectionResizeMode(columnEnglish, QHeaderView::Interactive);
	table_->horizontalHeader()->setSectionResizeMode(columnCases, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(columnScore, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(columnTime, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(columnMemory, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setStretchLastSection(true);
	table_->setMinimumHeight(190);
	outer->addWidget(table_, 1);

	auto *batchCard = new QFrame(this);
	batchCard->setObjectName(QStringLiteral("Card"));
	auto *batchRow = new QHBoxLayout(batchCard);
	batchRow->setContentsMargins(14, 10, 14, 10);
	batchRow->setSpacing(8);

	batchScore_ = new QLineEdit(batchCard);
	batchScore_->setPlaceholderText(tr("总分"));
	batchScore_->setMaximumWidth(84);
	batchTime_ = new QLineEdit(batchCard);
	batchTime_->setPlaceholderText(tr("时限"));
	batchTime_->setMaximumWidth(84);
	batchMemory_ = new QLineEdit(batchCard);
	batchMemory_->setPlaceholderText(tr("内存"));
	batchMemory_->setMaximumWidth(84);

	auto *applyButton = new QPushButton(tr("应用到全部"), batchCard);

	batchRow->addWidget(new QLabel(tr("批量设置："), batchCard));
	batchRow->addWidget(batchScore_);
	batchRow->addWidget(batchTime_);
	batchRow->addWidget(batchMemory_);
	batchRow->addWidget(applyButton);
	batchRow->addSpacing(12);

	structureBox_ = new QComboBox(batchCard);
	structureBox_->addItem(tr("次级文件夹（OI 赛制：准考证号/题目/题目.cpp）"));
	structureBox_->addItem(tr("扁平结构（准考证号/题目.cpp）"));
	structureBox_->setCurrentIndex(0);
	batchRow->addWidget(new QLabel(tr("选手提交结构："), batchCard));
	batchRow->addWidget(structureBox_, 1);
	outer->addWidget(batchCard);

	connect(dropArea_, &DropArea::pathsDropped, this, &ProblemTable::addPaths);
	connect(dropArea_, &DropArea::activated, this, &ProblemTable::pickFolder);
	connect(pickFolderButton, &QPushButton::clicked, this, &ProblemTable::pickFolder);
	connect(pickFilesButton, &QPushButton::clicked, this, &ProblemTable::pickFiles);
	connect(removeButton, &QPushButton::clicked, this, &ProblemTable::removeSelected);
	connect(clearButton, &QPushButton::clicked, this, &ProblemTable::clear);
	connect(applyButton, &QPushButton::clicked, this, &ProblemTable::applyBatch);

	refreshDropHint();
	refreshWarnings();
}

void ProblemTable::setDropHint(const QString &title, const QString &subtitle) {
	if (dropArea_)
		dropArea_->setHint(title, subtitle);
}

void ProblemTable::setSingleProblemMode(bool single) {
	singleProblemMode_ = single;

	if (single)
		setDropHint(tr("把这道题目的数据文件夹拖到这里"),
		            tr("也可以点这里选择文件夹，或一次性拖入多个 .in / .out 文件"));
	else
		refreshDropHint();
}

void ProblemTable::addPaths(const QStringList &paths) {
	if (paths.isEmpty())
		return;

	QApplication::setOverrideCursor(Qt::WaitCursor);
	const ScanResult result = scanner_.scan(paths);
	QApplication::restoreOverrideCursor();

	appendScanResult(result);
}

void ProblemTable::appendScanResult(const ScanResult &result) {
	for (const QString &one : result.warnings) {
		if (! warnings_.contains(one))
			warnings_.append(one);
	}

	if (result.kind == ScanResult::Unknown || result.problems.isEmpty()) {
		refreshWarnings();
		refreshDropHint();
		emit problemsChanged();
		return;
	}

	QSet<QString> existing;

	for (const PlannedProblem &one : planned_)
		existing.insert(one.scan.dirPath);

	containerCount_ += result.containerCount;

	const int defaultTime = settings_ ? settings_->getDefaultTimeLimit() : 1000;
	const int defaultMemory = settings_ ? settings_->getDefaultMemoryLimit() : 512;

	for (const ScannedProblem &problem : result.problems) {
		if (existing.contains(problem.dirPath))
			continue;

		existing.insert(problem.dirPath);

		// 已经是比赛里某道题的源文件名 → 说明这道题早就导入过了，不再重复列出
		if (! problem.englishName.isEmpty() && excludedNames_.contains(problem.englishName))
			continue;

		PlannedProblem plan;
		plan.scan = problem;
		plan.timeLimit = defaultTime;
		plan.memoryLimit = defaultMemory;
		plan.fullScore = qMax(10, static_cast<int>(problem.cases.size()) * 5);
		plan.subFolderCheck = ! structureBox_ || structureBox_->currentIndex() == 0;
		planned_.append(plan);
	}

	refreshTable();
	refreshWarnings();
	refreshDropHint();
	emit problemsChanged();
}

void ProblemTable::setProblems(const QList<ScannedProblem> &problems) {
	const int defaultTime = settings_ ? settings_->getDefaultTimeLimit() : 1000;
	const int defaultMemory = settings_ ? settings_->getDefaultMemoryLimit() : 512;

	for (const ScannedProblem &problem : problems) {
		PlannedProblem plan;
		plan.scan = problem;
		plan.timeLimit = defaultTime;
		plan.memoryLimit = defaultMemory;
		plan.fullScore = qMax(10, static_cast<int>(problem.cases.size()) * 5);
		plan.subFolderCheck = ! structureBox_ || structureBox_->currentIndex() == 0;
		planned_.append(plan);
	}

	refreshTable();
	refreshDropHint();
	emit problemsChanged();
}

void ProblemTable::clear() {
	planned_.clear();
	warnings_.clear();
	containerCount_ = 0;
	refreshTable();
	refreshWarnings();
	refreshDropHint();
	emit problemsChanged();
}

void ProblemTable::commitEdits() {
	if (! table_)
		return;

	const bool subFolder = ! structureBox_ || structureBox_->currentIndex() == 0;

	for (int i = 0; i < planned_.size() && i < table_->rowCount(); i++) {
		PlannedProblem &plan = planned_[i];

		if (QTableWidgetItem *item = table_->item(i, columnTitle); item && ! item->text().trimmed().isEmpty())
			plan.scan.title = item->text().trimmed();

		if (QTableWidgetItem *item = table_->item(i, columnEnglish))
			plan.scan.englishName = item->text().trimmed();

		if (QTableWidgetItem *item = table_->item(i, columnScore))
			plan.fullScore = qMax(1, item->text().toInt());

		if (QTableWidgetItem *item = table_->item(i, columnTime))
			plan.timeLimit = qMax(1, item->text().toInt());

		if (QTableWidgetItem *item = table_->item(i, columnMemory))
			plan.memoryLimit = qMax(1, item->text().toInt());

		plan.subFolderCheck = subFolder;
	}
}

void ProblemTable::setExcludedNames(const QStringList &names) { excludedNames_ = names; }

void ProblemTable::pickFolder() {
	const QString dir =
	    QFileDialog::getExistingDirectory(this, tr("选择题目的数据文件夹"), QDir::homePath());

	if (! dir.isEmpty())
		addPaths({dir});
}

void ProblemTable::pickFiles() {
	const QStringList files = QFileDialog::getOpenFileNames(
	    this, tr("选择测试点文件"), QDir::homePath(),
	    tr("测试点文件 (*.in *.out *.ans);;所有文件 (*.*)"));

	if (! files.isEmpty())
		addPaths(files);
}

void ProblemTable::removeSelected() {
	const QList<QTableWidgetItem *> selected = table_->selectedItems();

	if (selected.isEmpty())
		return;

	QList<int> rows;

	for (QTableWidgetItem *item : selected) {
		if (! rows.contains(item->row()))
			rows.append(item->row());
	}

	std::sort(rows.begin(), rows.end());

	for (int i = rows.size() - 1; i >= 0; --i) {
		const int row = rows.at(i);

		if (0 <= row && row < planned_.size())
			planned_.removeAt(row);
	}

	refreshTable();
	refreshDropHint();
	emit problemsChanged();
}

void ProblemTable::applyBatch() {
	const QString scoreText = batchScore_->text().trimmed();
	const QString timeText = batchTime_->text().trimmed();
	const QString memoryText = batchMemory_->text().trimmed();

	if (scoreText.isEmpty() && timeText.isEmpty() && memoryText.isEmpty())
		return;

	for (PlannedProblem &plan : planned_) {
		if (! scoreText.isEmpty())
			plan.fullScore = qMax(1, scoreText.toInt());

		if (! timeText.isEmpty())
			plan.timeLimit = qMax(1, timeText.toInt());

		if (! memoryText.isEmpty())
			plan.memoryLimit = qMax(1, memoryText.toInt());
	}

	refreshTable();
	emit problemsChanged();
}

void ProblemTable::refreshTable() {
	if (! table_)
		return;

	const QSignalBlocker blocker(table_);
	table_->setRowCount(planned_.size());

	for (int i = 0; i < planned_.size(); i++) {
		const PlannedProblem &plan = planned_.at(i);

		table_->setItem(i, columnTitle, new QTableWidgetItem(plan.scan.title));
		table_->setItem(i, columnEnglish, new QTableWidgetItem(plan.scan.englishName));
		table_->setItem(i, columnCases,
		                readOnlyItem(tr("%1 组").arg(plan.scan.cases.size())));
		table_->setItem(i, columnScore, new QTableWidgetItem(QString::number(plan.fullScore)));
		table_->setItem(i, columnTime, new QTableWidgetItem(QString::number(plan.timeLimit)));
		table_->setItem(i, columnMemory, new QTableWidgetItem(QString::number(plan.memoryLimit)));

		auto *sourceItem = readOnlyItem(QDir::toNativeSeparators(plan.scan.dirPath));

		if (! plan.scan.notes.isEmpty())
			sourceItem->setToolTip(plan.scan.notes.join(QStringLiteral("\n")));

		table_->setItem(i, columnSource, sourceItem);
	}

	table_->resizeColumnsToContents();
	table_->horizontalHeader()->setStretchLastSection(true);
}

void ProblemTable::refreshWarnings() {
	if (! warnLabel_)
		return;

	if (warnings_.isEmpty()) {
		warnLabel_->clear();
		warnLabel_->setVisible(false);
		return;
	}

	warnLabel_->setText(tr("请留意：") + QStringLiteral("\n") +
	                    QStringLiteral("· ") + warnings_.join(QStringLiteral("\n· ")));
	warnLabel_->setVisible(true);
}

void ProblemTable::refreshDropHint() {
	if (! dropArea_ || singleProblemMode_)
		return;

	if (planned_.isEmpty()) {
		dropArea_->setHint(tr("把包含题目数据的文件夹拖到这里"),
		                   tr("支持整场比赛的父文件夹，也支持单道题目的数据文件夹；"
		                      "也可以点这里选择文件夹或 .in / .out 文件"));
		return;
	}

	QString subtitle;

	if (containerCount_ > 0)
		subtitle = tr("已自动展开 %1 层父文件夹 · 继续拖入可以追加题目").arg(containerCount_);
	else
		subtitle = tr("继续拖入可以追加题目");

	dropArea_->setHint(tr("已识别 %1 道题目").arg(planned_.size()), subtitle);
}
