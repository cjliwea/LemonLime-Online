/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "newcontestwizard.h"

#include "base/settings.h"
#include "droparea.h"
#include "problemtable.h"
#include "titlebar.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

NewContestWizard::NewContestWizard(Settings *settings, QWidget *parent)
    : QWizard(parent), settings_(settings) {
	setWindowTitle(tr("新建比赛"));
	setWizardStyle(QWizard::ModernStyle);
	setOption(QWizard::NoBackButtonOnStartPage, true);
	setAcceptDrops(true);
	resize(820, 620);

	setPage(PageBasic, buildBasicPage());
	setPage(PageImport, buildImportPage());
	setPage(PageFinish, buildFinishPage());
	setStartId(PageBasic);

	setButtonText(QWizard::NextButton, tr("下一步"));
	setButtonText(QWizard::BackButton, tr("上一步"));
	setButtonText(QWizard::FinishButton, tr("完成并创建"));
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

	titleEdit_->setText(tr("新建比赛"));
	nameEdit_->setText(QStringLiteral("contest"));
	parentEdit_->setText(QDir::toNativeSeparators(QDir::homePath()));
	updatePathPreview();

	// 无边框窗口：装自绘标题栏（只有关闭钮）
	installTitleBar(this, false);
}

auto NewContestWizard::contestTitle() const -> QString {
	return titleEdit_ ? titleEdit_->text().trimmed() : QString();
}

auto NewContestWizard::savingName() const -> QString {
	return nameEdit_ ? nameEdit_->text().trimmed() : QString();
}

auto NewContestWizard::parentDir() const -> QString {
	const QString text = parentEdit_ ? parentEdit_->text().trimmed() : QString();

	return text.isEmpty() ? QDir::homePath() : text;
}

auto NewContestWizard::contestDir() const -> QString {
	const QString name = savingName();

	if (name.isEmpty())
		return QDir::toNativeSeparators(QDir(parentDir()).absolutePath());

	return QDir::toNativeSeparators(QDir(parentDir()).absoluteFilePath(name));
}

auto NewContestWizard::plannedProblems() -> QList<PlannedProblem> {
	if (importPanel_)
		importPanel_->commitEdits();

	return importPanel_ ? importPanel_->plannedProblems() : QList<PlannedProblem>();
}

auto NewContestWizard::buildBasicPage() -> QWizardPage * {
	auto *page = new QWizardPage;
	page->setTitle(tr("比赛信息"));
	page->setSubTitle(tr("先给这场比赛起个名字、选好它放在哪里。下面的路径预览会实时告诉你文件究竟会写到哪里。"));

	auto *outer = new QVBoxLayout(page);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(12);

	auto *card = new QFrame(page);
	card->setObjectName(QStringLiteral("Card"));
	auto *form = new QFormLayout(card);
	form->setContentsMargins(18, 18, 18, 18);
	form->setHorizontalSpacing(14);
	form->setVerticalSpacing(12);

	titleEdit_ = new QLineEdit(card);
	titleEdit_->setPlaceholderText(tr("例如：2026 国庆 CSP-J 模拟赛"));
	nameEdit_ = new QLineEdit(card);
	nameEdit_->setPlaceholderText(tr("英文 / 数字，作为文件夹名与 .cdf 文件名"));

	auto *parentRow = new QWidget(card);
	auto *parentLayout = new QHBoxLayout(parentRow);
	parentLayout->setContentsMargins(0, 0, 0, 0);
	parentLayout->setSpacing(8);
	parentEdit_ = new QLineEdit(parentRow);
	parentEdit_->setPlaceholderText(tr("比赛文件夹放在哪个目录下"));
	auto *browseButton = new QToolButton(parentRow);
	browseButton->setText(QStringLiteral("..."));
	parentLayout->addWidget(parentEdit_, 1);
	parentLayout->addWidget(browseButton);

	pathPreview_ = new QLabel(card);
	pathPreview_->setObjectName(QStringLiteral("PathPreview"));
	pathPreview_->setWordWrap(true);
	pathPreview_->setTextInteractionFlags(Qt::TextSelectableByMouse);

	form->addRow(tr("比赛标题"), titleEdit_);
	form->addRow(tr("保存文件名"), nameEdit_);
	form->addRow(tr("父目录"), parentRow);
	form->addRow(tr("最终路径"), pathPreview_);

	outer->addWidget(card);

	auto *tip = new QLabel(tr("data/ 与 source/ 会在创建比赛时自动生成，"
	                          "不需要事先手工新建任何文件夹。"),
	                      page);
	tip->setObjectName(QStringLiteral("HintText"));
	tip->setWordWrap(true);
	outer->addWidget(tip);
	outer->addStretch();

	connect(browseButton, &QToolButton::clicked, this, &NewContestWizard::chooseParentDir);
	connect(titleEdit_, &QLineEdit::textChanged, this, &NewContestWizard::updatePathPreview);
	connect(nameEdit_, &QLineEdit::textChanged, this, &NewContestWizard::updatePathPreview);
	connect(parentEdit_, &QLineEdit::textChanged, this, &NewContestWizard::updatePathPreview);

	return page;
}

auto NewContestWizard::buildImportPage() -> QWizardPage * {
	auto *page = new QWizardPage;
	page->setTitle(tr("装配题目"));
	page->setSubTitle(tr("把装着题目数据的文件夹拖到下面。程序会自动判断你拖的是整场比赛还是单道题目，"
	                     "并把识别结果列出来让你确认。"));

	auto *outer = new QVBoxLayout(page);
	outer->setContentsMargins(0, 0, 0, 0);

	importPanel_ = new ProblemTable(settings_, page);
	outer->addWidget(importPanel_, 1);

	return page;
}

auto NewContestWizard::buildFinishPage() -> QWizardPage * {
	auto *page = new QWizardPage;
	page->setTitle(tr("确认并创建"));
	page->setSubTitle(tr("确认无误后点「完成并创建」，比赛目录、data/、source/ 与题目数据会在这一步一次性建好。"));

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

void NewContestWizard::chooseParentDir() {
	const QString dir =
	    QFileDialog::getExistingDirectory(this, tr("选择父目录"), parentDir());

	if (! dir.isEmpty())
		parentEdit_->setText(QDir::toNativeSeparators(dir));
}

void NewContestWizard::updatePathPreview() {
	if (! pathPreview_)
		return;

	QString text = tr("比赛目录：%1").arg(QDir::toNativeSeparators(contestDir()));
	text += QLatin1Char('\n');
	text += tr("比赛文件：%1")
	            .arg(QDir::toNativeSeparators(
	                contestDir() + QDir::separator() + savingName() + QStringLiteral(".cdf")));
	text += QLatin1Char('\n');
	text += tr("题目数据：%1")
	            .arg(QDir::toNativeSeparators(contestDir() + QDir::separator() +
	                                          QStringLiteral("data") + QDir::separator()));

	pathPreview_->setText(text);
}

void NewContestWizard::refreshSummary() {
	if (! summaryLabel_)
		return;

	const QList<PlannedProblem> problems = plannedProblems();

	int totalCases = 0;
	int totalScore = 0;

	for (const PlannedProblem &one : problems) {
		totalCases += static_cast<int>(one.scan.cases.size());
		totalScore += one.fullScore;
	}

	QString text = tr("比赛标题：%1").arg(contestTitle());
	text += QLatin1Char('\n');
	text += tr("存放位置：%1").arg(QDir::toNativeSeparators(contestDir()));
	text += QLatin1Char('\n');
	text += tr("题目数量：%1 道 · 测试点合计 %2 组 · 总分 %3")
	            .arg(problems.size())
	            .arg(totalCases)
	            .arg(totalScore);
	text += QLatin1Char('\n');
	text += tr("选手提交结构：%1")
	            .arg(problems.isEmpty() || problems.first().subFolderCheck
	                     ? tr("次级文件夹（准考证号/题目/题目.cpp）")
	                     : tr("扁平结构（准考证号/题目.cpp）"));
	text += QStringLiteral("\n\n");
	text += tr("即将创建的题目：");
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

bool NewContestWizard::validateCurrentPage() {
	if (currentId() == PageBasic) {
		titleEdit_->setText(titleEdit_->text().trimmed());
		nameEdit_->setText(nameEdit_->text().trimmed());
		parentEdit_->setText(parentEdit_->text().trimmed());

		if (contestTitle().isEmpty() || savingName().isEmpty() || parentDir().isEmpty()) {
			QMessageBox::warning(this, tr("新建比赛"), tr("比赛标题、保存文件名、父目录都不能为空。"));
			return false;
		}

		if (! ContestScanner::isSafePathComponent(savingName())) {
			QMessageBox::warning(this, tr("新建比赛"),
			                     tr("保存文件名不能包含路径分隔符，也不能是 CON、PRN、AUX、NUL 之类的系统保留名，"
			                        "且不能以空格或点结尾。"));
			return false;
		}

		const QString cdfPath =
		    contestDir() + QDir::separator() + savingName() + QStringLiteral(".cdf");

		if (QFile::exists(cdfPath)) {
			QMessageBox::warning(this, tr("新建比赛"),
			                     tr("目标位置已经存在同名比赛，请换个保存文件名或父目录：\n%1")
			                         .arg(QDir::toNativeSeparators(cdfPath)));
			return false;
		}

		return true;
	}

	if (currentId() == PageImport) {
		if (! importPanel_) {
			QMessageBox::warning(this, tr("新建比赛"), tr("题目面板没有初始化。"));
			return false;
		}

		if (importPanel_->plannedProblems().isEmpty()) {
			QMessageBox::warning(this, tr("新建比赛"),
			                     tr("还没有识别到任何题目。请先把题目数据文件夹拖进来，"
			                        "或者点「选择文件夹…」。"));
			return false;
		}

		importPanel_->commitEdits();

		const QList<PlannedProblem> problems = importPanel_->plannedProblems();
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

			if (one.scan.cases.isEmpty())
				issues.append(tr("%1：没有测试点。").arg(one.scan.title));
		}

		if (! issues.isEmpty()) {
			QMessageBox::warning(this, tr("新建比赛"),
			                     tr("还有地方需要修正：") + QStringLiteral("\n\n") +
			                         issues.join(QStringLiteral("\n")));
			return false;
		}

		return true;
	}

	return true;
}

void NewContestWizard::initializePage(int id) {
	QWizard::initializePage(id);

	if (id == PageImport && importPanel_)
		importPanel_->commitEdits();

	if (id == PageFinish)
		refreshSummary();
}

void NewContestWizard::dragEnterEvent(QDragEnterEvent *event) {
	if (DropArea::localPaths(event->mimeData()).isEmpty())
		event->ignore();
	else
		event->acceptProposedAction();
}

void NewContestWizard::dragMoveEvent(QDragMoveEvent *event) { event->acceptProposedAction(); }

void NewContestWizard::dropEvent(QDropEvent *event) {
	const QStringList paths = DropArea::localPaths(event->mimeData());

	if (paths.isEmpty())
		return;

	event->acceptProposedAction();

	if (! importPanel_)
		return;

	importPanel_->addPaths(paths);

	// 拖进来的文件夹名顺手填进比赛信息，老师可以再改
	if (savingName().isEmpty() || savingName() == QStringLiteral("contest")) {
		for (const QString &path : paths) {
			const QFileInfo info(path);

			if (! info.isDir())
				continue;

			const QString suggested = ContestScanner::sanitizeEnglishName(info.fileName());

			if (! suggested.isEmpty()) {
				nameEdit_->setText(suggested);
				break;
			}
		}
	}

	if (contestTitle().isEmpty() || contestTitle() == tr("新建比赛")) {
		for (const QString &path : paths) {
			const QFileInfo info(path);

			if (info.isDir()) {
				titleEdit_->setText(info.fileName());
				break;
			}
		}
	}
}
