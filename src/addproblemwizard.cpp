/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "addproblemwizard.h"

#include "base/settings.h"
#include "droparea.h"
#include "problemtable.h"
#include "titlebar.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

struct TypeEntry {
	Task::TaskType type;
	QString title;
	QString description;
	QString need;
};

auto typeEntries() -> QList<TypeEntry> {
	QList<TypeEntry> list;

	list.append(TypeEntry{Task::Traditional, QObject::tr("传统题"),
	                      QObject::tr("最常用。选手程序读入数据、写出答案，评测时与标准答案比对。"),
	                      QObject::tr("需要：题目文件夹（含 .in / .out 测试点）、分值、时限、内存。")});

	list.append(TypeEntry{Task::AnswersOnly, QObject::tr("提交答案题"),
	                      QObject::tr("不需要源代码。选手直接上传答案文件，评测时与标准答案比对。"),
	                      QObject::tr("需要：测试点（输入与标准答案）、答案文件扩展名。")});

	list.append(TypeEntry{Task::Interaction, QObject::tr("交互题"),
	                      QObject::tr("选手程序与评测程序来回对话，由交互器控制流程。"),
	                      QObject::tr("需要：测试点、交互器（interactor）源文件。")});

	list.append(TypeEntry{Task::Communication, QObject::tr("通信题（完整）"),
	                      QObject::tr("两个程序通过管道通信。grader 以源码形式提供，评测时按语言分别编译。"),
	                      QObject::tr("需要：测试点、交互器源文件、各语言的 grader 源码。")});

	list.append(TypeEntry{Task::CommunicationExec, QObject::tr("通信题（部分）"),
	                      QObject::tr("两个程序通过管道通信。grader 已经是编译好的可执行文件，直接调用。"),
	                      QObject::tr("需要：测试点、交互器源文件、已编译的 grader 可执行文件。")});

	return list;
}

} // namespace

AddProblemWizard::AddProblemWizard(Settings *settings, QWidget *parent)
    : QWizard(parent), settings_(settings) {
	setWindowTitle(tr("新建题目"));
	setWizardStyle(QWizard::ModernStyle);
	setOption(QWizard::NoBackButtonOnStartPage, true);
	setAcceptDrops(true);
	resize(880, 660);

	setPage(PageType, buildTypePage());
	setPage(PageConfig, buildConfigPage());
	setPage(PageSummary, buildSummaryPage());
	setStartId(PageType);

	setButtonText(QWizard::NextButton, tr("下一步"));
	setButtonText(QWizard::BackButton, tr("上一步"));
	setButtonText(QWizard::FinishButton, tr("完成"));
	setButtonText(QWizard::CancelButton, tr("取消"));

	// 把「下一步 / 完成」做成主按钮（柠檬绿），与整体风格一致
	const auto accentButton = [this](QWizard::WizardButton which) {
		auto *target = qobject_cast<QPushButton *>(button(which));

		if (! target)
			return;

		target->setObjectName(QStringLiteral("PrimaryBtn"));
		target->style()->unpolish(target);
		target->style()->polish(target);
	};

	accentButton(QWizard::NextButton);
	accentButton(QWizard::FinishButton);

	applyTypeVisibility();

	// 无边框窗口：装自绘标题栏（只有关闭钮）
	installTitleBar(this, false);
}

auto AddProblemWizard::plans() -> QList<PlannedProblem> {
	if (collected_.isEmpty())
		collect();

	return collected_;
}

void AddProblemWizard::preloadFromDataDir(const QStringList &existingSourceNames) {
	if (! table_)
		return;

	table_->setExcludedNames(existingSourceNames);

	const QString dataDir = QDir(QStringLiteral("data")).absolutePath();

	if (QDir(dataDir).exists())
		table_->addPaths({dataDir});
}

auto AddProblemWizard::buildTypePage() -> QWizardPage * {
	auto *page = new QWizardPage;
	page->setTitle(tr("选择题型"));
	page->setSubTitle(tr("先告诉程序这是一道什么题。选好之后，下一步只会显示这种题型真正需要填的内容。"));

	auto *outer = new QHBoxLayout(page);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(14);

	typeList_ = new QListWidget(page);
	typeList_->setObjectName(QStringLiteral("TypeList"));
	typeList_->setMinimumWidth(240);
	typeList_->setMaximumWidth(280);

	const QList<TypeEntry> entries = typeEntries();

	for (const TypeEntry &entry : entries)
		typeList_->addItem(entry.title);

	typeList_->setCurrentRow(0);
	outer->addWidget(typeList_);

	auto *card = new QFrame(page);
	card->setObjectName(QStringLiteral("Card"));
	auto *cardLayout = new QVBoxLayout(card);
	cardLayout->setContentsMargins(20, 20, 20, 20);
	cardLayout->setSpacing(10);

	typeTitle_ = new QLabel(card);
	typeTitle_->setObjectName(QStringLiteral("CardTitle"));
	typeTitle_->setWordWrap(true);
	typeDescription_ = new QLabel(card);
	typeDescription_->setObjectName(QStringLiteral("HintText"));
	typeDescription_->setWordWrap(true);
	typeDescription_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

	cardLayout->addWidget(typeTitle_);
	cardLayout->addWidget(typeDescription_);
	cardLayout->addStretch();

	outer->addWidget(card, 1);

	connect(typeList_, &QListWidget::currentRowChanged, this, [this, entries](int row) {
		if (row < 0 || row >= entries.size())
			return;

		currentType_ = entries.at(row).type;
		typeTitle_->setText(entries.at(row).title);
		typeDescription_->setText(entries.at(row).description + QStringLiteral("\n\n") +
		                          entries.at(row).need);
		applyTypeVisibility();
	});

	typeTitle_->setText(entries.first().title);
	typeDescription_->setText(entries.first().description + QStringLiteral("\n\n") +
	                          entries.first().need);

	return page;
}

auto AddProblemWizard::buildConfigPage() -> QWizardPage * {
	auto *page = new QWizardPage;
	page->setTitle(tr("题目配置"));
	page->setSubTitle(tr("把题目数据文件夹拖到下面自动识别测试点；参数可以逐行改，也可以批量设置。"));

	auto *outer = new QVBoxLayout(page);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(12);

	table_ = new ProblemTable(settings_, page);
	outer->addWidget(table_, 1);

	extraCard_ = new QFrame(page);
	extraCard_->setObjectName(QStringLiteral("Card"));
	extraForm_ = new QFormLayout(extraCard_);
	extraForm_->setContentsMargins(16, 14, 16, 14);
	extraForm_->setHorizontalSpacing(14);
	extraForm_->setVerticalSpacing(10);

	specialJudgeEdit_ = new QLineEdit(extraCard_);
	specialJudgeEdit_->setPlaceholderText(
	    tr("可选：Special Judge 命令，例如 lemon-spj.exe %s %s %s（留空则按普通比对）"));
	specialJudgeRow_ = specialJudgeEdit_;

	auto makeFileRowFunc = [this](QLineEdit **edit, const QString &placeholder,
	                              const QString &buttonText,
	                              void (AddProblemWizard::*slot)()) -> QWidget * {
		auto *row = new QWidget(extraCard_);
		auto *layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(8);

		*edit = new QLineEdit(row);
		(*edit)->setPlaceholderText(placeholder);

		auto *button = new QToolButton(row);
		button->setText(buttonText);

		layout->addWidget(*edit, 1);
		layout->addWidget(button);
		connect(button, &QToolButton::clicked, this, slot);

		return row;
	};

	interactorRow_ = makeFileRowFunc(&interactorEdit_, tr("交互器（interactor）源文件路径"),
	                                 QStringLiteral("..."), &AddProblemWizard::chooseInteractor);
	graderSourceRow_ = makeFileRowFunc(&graderSourceEdit_, tr("各语言的 grader 源码，可多选"),
	                                   QStringLiteral("..."),
	                                   &AddProblemWizard::chooseGraderSource);
	graderExecRow_ = makeFileRowFunc(&graderExecEdit_, tr("已编译的 grader 可执行文件，可多选"),
	                                 QStringLiteral("..."),
	                                 &AddProblemWizard::chooseGraderExecutable);

	answerExtensionEdit_ = new QLineEdit(extraCard_);
	answerExtensionEdit_->setPlaceholderText(tr("例如 ans"));

	extraForm_->addRow(tr("Special Judge"), specialJudgeRow_);
	extraForm_->addRow(tr("交互器源文件"), interactorRow_);
	extraForm_->addRow(tr("Grader 源码"), graderSourceRow_);
	extraForm_->addRow(tr("Grader 可执行文件"), graderExecRow_);
	extraForm_->addRow(tr("答案文件扩展名"), answerExtensionEdit_);

	outer->addWidget(extraCard_);

	connect(table_, &ProblemTable::problemsChanged, this, &AddProblemWizard::applyTypeVisibility);

	return page;
}

auto AddProblemWizard::buildSummaryPage() -> QWizardPage * {
	auto *page = new QWizardPage;
	page->setTitle(tr("确认"));
	page->setSubTitle(tr("确认无误后点「完成」，测试点会被复制进当前比赛的 data/ 目录。"));

	auto *outer = new QVBoxLayout(page);
	outer->setContentsMargins(0, 0, 0, 0);

	auto *card = new QFrame(page);
	card->setObjectName(QStringLiteral("Card"));
	auto *cardLayout = new QVBoxLayout(card);
	cardLayout->setContentsMargins(18, 18, 18, 18);

	summaryLabel_ = new QLabel(card);
	summaryLabel_->setObjectName(QStringLiteral("SummaryText"));
	summaryLabel_->setWordWrap(true);
	summaryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	summaryLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	cardLayout->addWidget(summaryLabel_);

	outer->addWidget(card, 1);

	return page;
}

void AddProblemWizard::applyTypeVisibility() {
	if (! extraForm_)
		return;

	const Task::TaskType type = currentType_;

	const bool wantsSpecialJudge = (type == Task::Traditional);
	const bool wantsInteractor =
	    (type == Task::Interaction || type == Task::Communication || type == Task::CommunicationExec);
	const bool wantsGraderSource = (type == Task::Communication);
	const bool wantsGraderExec = (type == Task::CommunicationExec);
	const bool wantsAnswerExtension = (type == Task::AnswersOnly);

	if (specialJudgeRow_)
		extraForm_->setRowVisible(specialJudgeRow_, wantsSpecialJudge);

	if (interactorRow_)
		extraForm_->setRowVisible(interactorRow_, wantsInteractor);

	if (graderSourceRow_)
		extraForm_->setRowVisible(graderSourceRow_, wantsGraderSource);

	if (graderExecRow_)
		extraForm_->setRowVisible(graderExecRow_, wantsGraderExec);

	if (answerExtensionEdit_)
		extraForm_->setRowVisible(answerExtensionEdit_, wantsAnswerExtension);

	if (extraCard_)
		extraCard_->setVisible(wantsSpecialJudge || wantsInteractor || wantsGraderSource ||
		                       wantsGraderExec || wantsAnswerExtension);

	if (table_)
		table_->setSingleProblemMode(false);
}

void AddProblemWizard::chooseInteractor() {
	const QString file = QFileDialog::getOpenFileName(this, tr("选择交互器源文件"), QDir::homePath());

	if (! file.isEmpty())
		interactorEdit_->setText(QDir::toNativeSeparators(file));
}

void AddProblemWizard::chooseGraderSource() {
	const QStringList files =
	    QFileDialog::getOpenFileNames(this, tr("选择 grader 源码（可多选）"), QDir::homePath());

	if (files.isEmpty())
		return;

	graderSourcePaths_ = files;

	QStringList names;

	for (const QString &file : files)
		names.append(QFileInfo(file).fileName());

	graderSourceEdit_->setText(QDir::toNativeSeparators(names.join(QStringLiteral(", "))));
	graderSourceEdit_->setToolTip(QDir::toNativeSeparators(files.join(QStringLiteral("\n"))));
}

void AddProblemWizard::chooseGraderExecutable() {
	const QStringList files = QFileDialog::getOpenFileNames(
	    this, tr("选择已编译的 grader 可执行文件（可多选）"), QDir::homePath(), tr("可执行文件 (*.exe);;所有文件 (*.*)"));

	if (files.isEmpty())
		return;

	graderExecPaths_ = files;

	QStringList names;

	for (const QString &file : files)
		names.append(QFileInfo(file).fileName());

	graderExecEdit_->setText(QDir::toNativeSeparators(names.join(QStringLiteral(", "))));
	graderExecEdit_->setToolTip(QDir::toNativeSeparators(files.join(QStringLiteral("\n"))));
}

void AddProblemWizard::collect() {
	if (! table_)
		return;

	table_->commitEdits();

	QList<PlannedProblem> problems = table_->plannedProblems();

	for (PlannedProblem &plan : problems) {
		plan.taskType = currentType_;
		plan.comparisonMode = Task::IgnoreSpacesMode;

		if (specialJudgeEdit_ && ! specialJudgeEdit_->text().trimmed().isEmpty())
			plan.specialJudge = specialJudgeEdit_->text().trimmed();

		if (interactorEdit_ && ! interactorEdit_->text().trimmed().isEmpty()) {
			plan.interactor = interactorEdit_->text().trimmed();
			plan.interactorName = QFileInfo(plan.interactor).fileName();
		}

		if (answerExtensionEdit_ && ! answerExtensionEdit_->text().trimmed().isEmpty()) {
			QString extension = answerExtensionEdit_->text().trimmed();

			while (extension.startsWith(QLatin1Char('.')))
				extension.remove(0, 1);

			plan.answerFileExtension = extension;
		}

		plan.graderFilesPath = graderExecPaths_;

		for (const QString &file : graderExecPaths_)
			plan.graderFilesName.append(QFileInfo(file).fileName());

		if (currentType_ == Task::Communication) {
			plan.sourceFilesPath = graderSourcePaths_;

			for (const QString &file : graderSourcePaths_)
				plan.sourceFilesName.append(QFileInfo(file).fileName());
		}
	}

	collected_ = problems;
}

void AddProblemWizard::refreshSummary() {
	if (! summaryLabel_)
		return;

	const QList<PlannedProblem> problems = collected_;

	int totalCases = 0;
	int totalScore = 0;

	for (const PlannedProblem &one : problems) {
		totalCases += static_cast<int>(one.scan.cases.size());
		totalScore += one.fullScore;
	}

	QString text = tr("题目数量：%1 道 · 测试点合计 %2 组 · 总分 %3")
	                   .arg(problems.size())
	                   .arg(totalCases)
	                   .arg(totalScore);
	text += QLatin1Char('\n');
	text += tr("选手提交结构：%1")
	            .arg(problems.isEmpty() || problems.first().subFolderCheck
	                     ? tr("次级文件夹（准考证号/题目/题目.cpp）")
	                     : tr("扁平结构（准考证号/题目.cpp）"));
	text += QStringLiteral("\n\n");
	text += tr("即将写入的题目：");
	text += QLatin1Char('\n');

	for (int i = 0; i < problems.size(); i++) {
		const PlannedProblem &one = problems.at(i);
		text += tr("%1. %2（源文件名 %3，%4 组测试点，%5 分）")
		            .arg(i + 1)
		            .arg(one.scan.title)
		            .arg(one.scan.englishName)
		            .arg(one.scan.cases.size())
		            .arg(one.fullScore);
		text += QLatin1Char('\n');
	}

	summaryLabel_->setText(text);
}

bool AddProblemWizard::validateCurrentPage() {
	if (currentId() == PageConfig) {
		if (! table_ || table_->plannedProblems().isEmpty()) {
			QMessageBox::warning(this, tr("新建题目"),
			                     tr("还没有识别到任何测试点。请把题目数据文件夹拖进来，"
			                        "或者点「选择文件夹…」。\n\n"
			                        "如果这是提交答案题、暂时还没有测试点，请先准备好数据再导入。"));
			return false;
		}

		table_->commitEdits();

		const QList<PlannedProblem> problems = table_->plannedProblems();
		QSet<QString> seen;
		QStringList issues;

		for (const PlannedProblem &one : problems) {
			if (one.scan.title.isEmpty())
				issues.append(tr("有题目没有填题目名。"));

			if (one.scan.englishName.isEmpty())
				issues.append(tr("%1：缺少英文名 / 源文件名。").arg(one.scan.title));
			else if (! ContestScanner::isSafePathComponent(one.scan.englishName))
				issues.append(
				    tr("%1：英文名「%2」不是合法的文件夹名。").arg(one.scan.title, one.scan.englishName));
			else if (seen.contains(one.scan.englishName))
				issues.append(
				    tr("%1：英文名「%2」与前面的题目重复。").arg(one.scan.title, one.scan.englishName));

			seen.insert(one.scan.englishName);
		}

		if (currentType_ == Task::Interaction || currentType_ == Task::Communication ||
		    currentType_ == Task::CommunicationExec) {
			if (! interactorEdit_ || interactorEdit_->text().trimmed().isEmpty())
				issues.append(tr("这种题型必须指定交互器（interactor）源文件。"));
		}

		if (currentType_ == Task::Communication && graderSourcePaths_.isEmpty())
			issues.append(tr("通信题（完整）必须提供 grader 源码。"));

		if (currentType_ == Task::CommunicationExec && graderExecPaths_.isEmpty())
			issues.append(tr("通信题（部分）必须提供已编译的 grader 可执行文件。"));

		if (! issues.isEmpty()) {
			QMessageBox::warning(this, tr("新建题目"),
			                     tr("还有地方需要修正：") + QStringLiteral("\n\n") +
			                         issues.join(QStringLiteral("\n")));
			return false;
		}

		collect();
		return true;
	}

	if (currentId() == PageSummary)
		collect();

	return true;
}

void AddProblemWizard::initializePage(int id) {
	QWizard::initializePage(id);

	if (id == PageConfig)
		applyTypeVisibility();

	if (id == PageSummary) {
		collect();
		refreshSummary();
	}
}

void AddProblemWizard::accept() {
	collect();
	QWizard::accept();
}

void AddProblemWizard::dragEnterEvent(QDragEnterEvent *event) {
	if (DropArea::localPaths(event->mimeData()).isEmpty())
		event->ignore();
	else
		event->acceptProposedAction();
}

void AddProblemWizard::dragMoveEvent(QDragMoveEvent *event) { event->acceptProposedAction(); }

void AddProblemWizard::dropEvent(QDropEvent *event) {
	const QStringList paths = DropArea::localPaths(event->mimeData());

	if (paths.isEmpty() || ! table_)
		return;

	event->acceptProposedAction();
	table_->addPaths(paths);
}
