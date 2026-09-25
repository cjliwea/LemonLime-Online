/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "judgingdialog.h"
#include "ui_judgingdialog.h"
//
#include "base/LemonType.hpp"
#include "core/contest.h"
#include "core/subtaskdependencelib.h"
#include "core/task.h"
#include "core/testcase.h"
//
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

JudgingDialog::JudgingDialog(QWidget *parent) : QDialog(parent), ui(new Ui::JudgingDialog) {
	ui->setupUi(this);
	rebuildLayout();
	ui->progressBar->setValue(0);
	cursor = new QTextCursor(ui->logViewer->document());
	connect(ui->cancelButton, &QPushButton::clicked, this, &JudgingDialog::stopJudgingSlot);

	// 「评测中」色块闪烁定时器：在当前题目的未完成色块上交替两种绿色
	blinkTimer = new QTimer(this);
	blinkTimer->setInterval(500);
	connect(blinkTimer, &QTimer::timeout, this, [this]() {
		blinkOn = ! blinkOn;

		if (curTaskIdx < 0 || curTaskIdx >= taskDots.size())
			return;

		for (int i = 0; i < taskDots[curTaskIdx].size(); i++) {
			if (dotState[curTaskIdx][i] == 1) {
				taskDots[curTaskIdx][i]->setStyleSheet(
				    QStringLiteral("background-color:%1;border-radius:7px;")
				        .arg(blinkOn ? QStringLiteral("#84CC16") : QStringLiteral("#BBF7D0")));
			}
		}
	});
	blinkTimer->start();
}

JudgingDialog::~JudgingDialog() {
	delete ui;
	delete cursor;
}

// 基于 .ui 已有控件（progressBar / logViewer / cancelButton）重建整体布局
void JudgingDialog::rebuildLayout() {
	ui->skipButton->hide(); // Skip 按钮当前未启用，隐藏
	ui->progressBar->setObjectName(QStringLiteral("JudgeProgressBar"));
	ui->logViewer->setReadOnly(true);

	// 1. 总体进度区：大号进度条 + 一行状态文字
	overallLabel = new QLabel(tr("总体进度 0/0 人 · 当前 —"), this);
	overallLabel->setStyleSheet(QStringLiteral("color:#475569;font-weight:600;"));

	// 2. 当前选手卡片区
	contestantBox = new QGroupBox(tr("当前选手"), this);
	contestantLayout = new QVBoxLayout(contestantBox);
	auto *placeholder = new QLabel(tr("等待评测开始……"), contestantBox);
	placeholder->setStyleSheet(QStringLiteral("color:#94A3B8;"));
	contestantLayout->addWidget(placeholder);

	// 3. 实时日志区
	auto *logTitle = new QLabel(tr("实时日志"), this);
	logTitle->setStyleSheet(QStringLiteral("color:#475569;font-weight:600;"));

	auto *mainLay = new QVBoxLayout;
	mainLay->addWidget(ui->progressBar);
	mainLay->addWidget(overallLabel);
	mainLay->addWidget(contestantBox);
	mainLay->addWidget(logTitle);
	mainLay->addWidget(ui->logViewer, 1);
	auto *btnLay = new QHBoxLayout;
	btnLay->addStretch();
	btnLay->addWidget(ui->cancelButton);
	mainLay->addLayout(btnLay);

	// 把控件逐个挪入新布局后，移除 .ui 生成的旧布局
	delete layout();
	setLayout(mainLay);
	resize(600, 560);
}

// 重建当前选手卡片：每题一行「T 编号 题名 + 测试点色块 + 得分」
void JudgingDialog::rebuildContestantCard() {
	// 清空旧行
	QLayoutItem *child = nullptr;

	while ((child = contestantLayout->takeAt(0)) != nullptr) {
		delete child->widget();
		delete child;
	}

	taskRowTitles.clear();
	taskDots.clear();
	dotState.clear();
	taskScoreLabels.clear();

	if (! curContest)
		return;

	const QList<Task *> &taskList = curContest->getTaskList();

	for (int t = 0; t < taskList.size(); t++) {
		auto *row = new QWidget(contestantBox);
		auto *rowLay = new QHBoxLayout(row);
		rowLay->setContentsMargins(0, 2, 0, 2);
		rowLay->setSpacing(4);

		// 行首：T 编号 + 题名
		auto *titleLabel = new QLabel(tr("T%1 %2").arg(t + 1).arg(taskList[t]->getProblemTitle()), row);
		titleLabel->setMinimumWidth(110);
		titleLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
		rowLay->addWidget(titleLabel);

		// 中部：该题各测试点状态小色块（初始为「等待」灰）
		QVector<QLabel *> dots;
		QVector<char> states;
		const QList<TestCase *> &caseList = taskList[t]->getTestCaseList();

		for (auto *tc : caseList) {
			int caseCnt = qMax(1, tc->getInputFiles().size());

			for (int c = 0; c < caseCnt; c++) {
				auto *dot = new QLabel(row);
				dot->setFixedSize(14, 14);
				dot->setStyleSheet(QStringLiteral("background-color:#E2E8F0;border-radius:7px;"));
				dot->setToolTip(tr("测试点 %1").arg(dots.size() + 1));
				rowLay->addWidget(dot);
				dots.append(dot);
				states.append(char(0));
			}
		}

		rowLay->addStretch();

		// 行尾：该题得分
		auto *scoreLabel = new QLabel(tr("-- / %1").arg(taskList[t]->getTotalScore()), row);
		scoreLabel->setStyleSheet(QStringLiteral("color:#475569;font-weight:600;"));
		rowLay->addWidget(scoreLabel);

		contestantLayout->addWidget(row);
		taskRowTitles.append(taskList[t]->getProblemTitle());
		taskDots.append(dots);
		dotState.append(states);
		taskScoreLabels.append(scoreLabel);
	}
}

// 刷新总体进度文字
void JudgingDialog::updateOverallLabel() {
	QString current = QStringLiteral("—");

	if (! curContestantName.isEmpty()) {
		current = curContestantName;

		if (! curTaskTitle.isEmpty())
			current += QStringLiteral(" / ") + curTaskTitle;
	}

	overallLabel->setText(
	    tr("总体进度 %1/%2 人 · 当前 %3").arg(judgedContestants).arg(totalContestants).arg(current));
}

// 设置某个测试点色块的状态与颜色
void JudgingDialog::setDotStyle(int taskIdx, int dotIdx, char state, const QColor &color) {
	if (taskIdx < 0 || taskIdx >= taskDots.size() || dotIdx < 0 || dotIdx >= taskDots[taskIdx].size())
		return;

	dotState[taskIdx][dotIdx] = state;
	taskDots[taskIdx][dotIdx]->setStyleSheet(
	    QStringLiteral("background-color:%1;border-radius:7px;").arg(color.name()));
}

// 插入日志行的灰色时间前缀
void JudgingDialog::insertTimePrefix() {
	QTextCharFormat fmt;
	fmt.setFontPointSize(8);
	fmt.setForeground(QBrush(QColor(QStringLiteral("#94A3B8"))));
	cursor->insertText(QTime::currentTime().toString(QStringLiteral("hh:mm:ss ")), fmt);
}

void JudgingDialog::sendNotify(QString head, QString body) {
#ifdef Q_OS_LINUX
	QString text = "notify-send";
	QStringList args;
	args.append(head);
	args.append(body);
	QProcess::execute(text, args);
#endif
}

void JudgingDialog::setContest(Contest *contest) {
	curContest = contest;
	connect(curContest, &Contest::dialogAlert, this, &JudgingDialog::dialogAlert);
	connect(curContest, &Contest::singleCaseFinished, this, &JudgingDialog::singleCaseFinished);
	connect(curContest, &Contest::singleSubtaskDependenceFinished, this,
	        &JudgingDialog::singleSubtaskDependenceFinished);
	connect(curContest, &Contest::taskJudgingStarted, this, &JudgingDialog::taskJudgingStarted);
	connect(curContest, &Contest::taskJudgedDisplay, this, &JudgingDialog::taskJudgedDisplay);
	connect(curContest, &Contest::contestantJudgingStart, this, &JudgingDialog::contestantJudgingStart);
	connect(curContest, &Contest::contestantJudgingFinished, this, &JudgingDialog::contestantJudgingFinished);
	connect(curContest, &Contest::contestantJudgedDisplay, this, &JudgingDialog::contestantJudgedDisplay);
	connect(curContest, &Contest::compileError, this, &JudgingDialog::compileError);
	connect(this, &JudgingDialog::stopJudgingSignal, curContest, &Contest::stopJudgingSlot);
}

void JudgingDialog::judge(const QList<std::pair<QString, QVector<int>>> &lists) {
	stopJudging = false;
	totalContestants = lists.size();
	judgedContestants = 0;
	curContestantName.clear();
	curTaskTitle.clear();
	curTaskIdx = -1;
	updateOverallLabel();
	int allTime = 0;
	int listsSize = lists.size();

	for (int i = 0; i < listsSize; i++) {
		for (int j : lists[i].second) {
			allTime += curContest->getTask(j)->getTotalTimeLimit();
		}
	}

	ui->progressBar->setMaximum(allTime);

	curContest->judge(lists);

	sendNotify(tr("Finished"), tr("Judge Finished - LemonLime"));
}

void JudgingDialog::judgeAll() {
	stopJudging = false;
	totalContestants = curContest->getContestantList().size();
	judgedContestants = 0;
	curContestantName.clear();
	curTaskTitle.clear();
	curTaskIdx = -1;
	updateOverallLabel();
	ui->progressBar->setMaximum(curContest->getTotalTimeLimit() * curContest->getContestantList().size());
	curContest->judgeAll();

	sendNotify(tr("Judge All: Finished"), tr("Judge Finished - LemonLime"));
	accept();
}

void JudgingDialog::singleCaseFinished(QString contestantName, int progress, int x, int y, int result,
                                       int scoreGot, int timeUsed, qint64 memoryUsed) {
	bool isOnMaxValue =
	    ui->logViewer->verticalScrollBar()->value() == ui->logViewer->verticalScrollBar()->maximum();

	// 更新当前选手卡片中对应测试点的状态色块
	if (contestantName == curContestantName && curTaskIdx >= 0 && curTaskIdx < taskDots.size()) {
		Task *curTask = curContest->getTask(curTaskIdx);
		// (x, y) 是「子任务序号, 子任务内测试点序号」，换算成卡片中的扁平色块序号
		int flat = y;

		for (int i = 0; i < x && i < curTask->getTestCaseList().size(); i++)
			flat += qMax(1, curTask->getTestCaseList()[i]->getInputFiles().size());

		QColor dotColor(QStringLiteral("#DC2626"));

		switch (ResultState(result)) {
			case CorrectAnswer:
				dotColor = QColor(QStringLiteral("#16A34A")); // AC 绿
				break;

			case PartlyCorrect:
				dotColor = QColor(QStringLiteral("#CA8A04")); // 部分分 黄
				break;

			case WrongAnswer:
				dotColor = QColor(QStringLiteral("#DC2626")); // WA 红
				break;

			case TimeLimitExceeded:
			case SpecialJudgeTimeLimitExceeded:
				dotColor = QColor(QStringLiteral("#D97706")); // TLE 橙
				break;

			case MemoryLimitExceeded:
				dotColor = QColor(QStringLiteral("#7C3AED")); // MLE 紫
				break;

			case RunTimeError:
			case SpecialJudgeRunTimeError:
			case CannotStartProgram:
				dotColor = QColor(QStringLiteral("#0891B2")); // RE 青
				break;

			case Skipped:
				dotColor = QColor(QStringLiteral("#E2E8F0")); // 跳过 灰
				break;

			default:
				break;
		}

		setDotStyle(curTaskIdx, flat, char(2), dotColor);
	}

	QTextBlockFormat blockFormat;
	blockFormat.setLeftMargin(30);
	cursor->insertBlock(blockFormat);
	insertTimePrefix();
	QTextCharFormat preCharFormat;
	QTextCharFormat charFormat;
	QTextCharFormat addcharFormat;
	QTextCharFormat scorecharFormat;
	preCharFormat.setFontPointSize(9);
	charFormat.setFontPointSize(9);
	addcharFormat.setFontPointSize(7);
	addcharFormat.setForeground(QBrush(QColor(QStringLiteral("#94A3B8"))));
	scorecharFormat.setFontPointSize(8);
	QString text;
	QString addtext = "";
	QString scoretext = "";

	switch (ResultState(result)) {
		case CorrectAnswer:
			text = tr("Correct answer");

			if (timeUsed >= 0)
				addtext += tr(" %1 ms").arg(timeUsed);

			if (memoryUsed >= 0)
				addtext += tr(" %1 MiB").arg(1.00 * memoryUsed / 1024.00 / 1024.00);

			if (scoreGot > 0)
				scoretext = tr("  %1 %2").arg(scoreGot).arg(scoreGot == 1 ? tr("Pt") : tr("Pts"));

			charFormat.setForeground(QBrush(QColor(QStringLiteral("#16A34A"))));
			scorecharFormat.setForeground(QBrush(QColor(QStringLiteral("#15803D"))));
			scorecharFormat.setFontWeight(QFont::Bold);
			break;

		case PartlyCorrect:
			text = tr("Partly correct");

			if (timeUsed >= 0)
				addtext += tr(" %1 ms").arg(timeUsed);

			if (memoryUsed >= 0)
				addtext += tr(" %1 MiB").arg(1.00 * memoryUsed / 1024.00 / 1024.00);

			if (scoreGot > 0) {
				scoretext = tr("  %1 %2").arg(scoreGot).arg(scoreGot == 1 ? tr("Pt") : tr("Pts"));
				scorecharFormat.setForeground(QBrush(QColor(QStringLiteral("#15803D"))));
				scorecharFormat.setFontWeight(QFont::Bold);
			} else {
				scoretext = tr("  %1 %2").arg(qAbs(scoreGot)).arg(qAbs(scoreGot) == 1 ? tr("Pt") : tr("Pts"));
				scorecharFormat.setForeground(QBrush(QColor(QStringLiteral("#A16207"))));
			}

			charFormat.setForeground(QBrush(QColor(QStringLiteral("#CA8A04"))));
			break;

		case WrongAnswer:
			text = tr("Wrong answer");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#DC2626"))));
			break;

		case PresentationError:
			text = tr("Presentation Error");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#D97706"))));
			break;

		case TimeLimitExceeded:
			text = tr("Time limit exceeded");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#D97706"))));
			break;

		case MemoryLimitExceeded:
			text = tr("Memory limit exceeded");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#7C3AED"))));
			break;

		case OutputLimitExceeded:
			text = tr("Output Limit Exceeded");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#7C3AED"))));
			break;

		case RunTimeError:
			text = tr("Run time error");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#0891B2"))));
			break;

		case Skipped:
			text = tr("Skipped");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#CBD5E1"))));
			preCharFormat.setFontPointSize(4);
			charFormat.setFontPointSize(4);
			addcharFormat.setFontPointSize(2);
			scorecharFormat.setFontPointSize(3);
			break;

		case CannotStartProgram:
			text = tr("Cannot start program");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#DC2626"))));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#FEE2E2"))));
			break;

		case FileError:
			text = tr("File error");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#D97706"))));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#F1F5F9"))));
			break;

		case InteractorError:
			text = tr("Interactor error");
			charFormat.setForeground(QBrush(Qt::white));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#7C3AED"))));
			break;

		case InvalidSpecialJudge:
			text = tr("Invalid special judge");
			charFormat.setForeground(QBrush(Qt::white));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#B91C1C"))));
			break;

		case SpecialJudgeTimeLimitExceeded:
			text = tr("Special judge time limit exceeded");
			charFormat.setForeground(QBrush(Qt::white));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#D97706"))));
			break;

		case SpecialJudgeRunTimeError:
			text = tr("Special judge run time error");
			charFormat.setForeground(QBrush(Qt::white));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#0891B2"))));
			break;
	}

	cursor->insertText(tr("Contestant %3 Test case %1.%2: ").arg(x + 1).arg(y + 1).arg(contestantName),
	                   preCharFormat);
	cursor->insertText(text, charFormat);

	if (addtext.length() > 0)
		cursor->insertText(addtext, addcharFormat);

	if (scoretext.length() > 0)
		cursor->insertText(scoretext, scorecharFormat);

	ui->progressBar->setValue(ui->progressBar->value() + progress);
	QScrollBar *bar = ui->logViewer->verticalScrollBar();

	if (isOnMaxValue)
		bar->setValue(bar->maximum());
}

void JudgingDialog::dialogAlert(const QString &msg) {
	bool isOnMaxValue =
	    ui->logViewer->verticalScrollBar()->value() == ui->logViewer->verticalScrollBar()->maximum();
	QTextBlockFormat blockFormat;
	blockFormat.setLeftMargin(30);
	cursor->insertBlock(blockFormat);
	insertTimePrefix();
	QTextCharFormat format;
	format.setFontPointSize(9);
	format.setForeground(QBrush(QColor(QStringLiteral("#94A3B8"))));
	cursor->insertText(msg, format);
	QScrollBar *bar = ui->logViewer->verticalScrollBar();

	if (isOnMaxValue)
		bar->setValue(bar->maximum());
}

void JudgingDialog::singleSubtaskDependenceFinished(int x, int y, int status) {
	bool isOnMaxValue =
	    ui->logViewer->verticalScrollBar()->value() == ui->logViewer->verticalScrollBar()->maximum();
	QTextBlockFormat blockFormat;
	blockFormat.setLeftMargin(30);
	cursor->insertBlock(blockFormat);
	insertTimePrefix();
	QTextCharFormat charFormat;
	QTextCharFormat ratioFormat;
	charFormat.setFontPointSize(9);
	ratioFormat.setFontPointSize(9);
	QString text = statusRankingText(status);

	if (status >= maxDependValue) {
		charFormat.setForeground(QBrush(QColor(QStringLiteral("#CBD5E1"))));
		ratioFormat.setForeground(QBrush(QColor(QStringLiteral("#16A34A"))));
	} else if (status < 0) {
		charFormat.setForeground(QBrush(QColor(QStringLiteral("#DC2626"))));
		ratioFormat.setForeground(QBrush(QColor(QStringLiteral("#DC2626"))));
		ratioFormat.setFontWeight(QFont::Bold);
	} else {
		charFormat.setForeground(QBrush(QColor(QStringLiteral("#D97706"))));
		ratioFormat.setForeground(QBrush(QColor(QStringLiteral("#D97706"))));
		ratioFormat.setFontWeight(QFont::Bold);
	}

	cursor->insertText(tr("Subtask Dependence %1: #%2: ").arg(x + 1).arg(y), charFormat);
	cursor->insertText(text, ratioFormat);
	QScrollBar *bar = ui->logViewer->verticalScrollBar();

	if (isOnMaxValue)
		bar->setValue(bar->maximum());
}

void JudgingDialog::taskJudgingStarted(const QString &taskName) {
	bool isOnMaxValue =
	    ui->logViewer->verticalScrollBar()->value() == ui->logViewer->verticalScrollBar()->maximum();
	curTaskTitle = taskName;

	// 在卡片行中按题名定位当前题目（优先从上次位置之后找，兼容重名题）
	int found = -1;

	for (int i = qMax(0, curTaskIdx + 1); i < taskRowTitles.size(); i++) {
		if (taskRowTitles[i] == taskName) {
			found = i;
			break;
		}
	}

	if (found == -1) {
		for (int i = 0; i < taskRowTitles.size(); i++) {
			if (taskRowTitles[i] == taskName) {
				found = i;
				break;
			}
		}
	}

	curTaskIdx = found;

	// 把当前题目的等待色块全部标记为「评测中」（闪烁绿）
	if (curTaskIdx >= 0) {
		for (int i = 0; i < taskDots[curTaskIdx].size(); i++)
			setDotStyle(curTaskIdx, i, char(1), QColor(QStringLiteral("#84CC16")));
	}

	updateOverallLabel();
	QTextBlockFormat blockFormat;
	blockFormat.setLeftMargin(15);
	cursor->insertBlock(blockFormat);
	insertTimePrefix();
	QTextCharFormat charFormat;
	charFormat.setFontPointSize(10);
	cursor->insertText(tr("Start judging task %1").arg(taskName), charFormat);
	QScrollBar *bar = ui->logViewer->verticalScrollBar();

	if (isOnMaxValue)
		bar->setValue(bar->maximum());
}

void JudgingDialog::taskJudgedDisplay(const QString &taskName, const QList<QList<int>> &scoreList,
                                      const int mxScore) {
	bool isOnMaxValue =
	    ui->logViewer->verticalScrollBar()->value() == ui->logViewer->verticalScrollBar()->maximum();
	QTextBlockFormat blockFormat;
	blockFormat.setLeftMargin(15);
	cursor->insertBlock(blockFormat);
	insertTimePrefix();
	QTextCharFormat charFormat;
	QTextCharFormat scoreFormat;
	charFormat.setFontPointSize(10);
	scoreFormat.setFontPointSize(10);
	scoreFormat.setFontWeight(QFont::Bold);
	scoreFormat.setForeground(QBrush(QColor(QStringLiteral("#15803D"))));
	int allScore = 0;

	for (const auto &i : scoreList) {
		int miScore = 2147483647;

		for (auto j : i) {
			if (j >= 0)
				miScore = qMin(miScore, j);

			if (miScore <= 0)
				break;
		}

		allScore += miScore;
	}

	// 更新卡片中该题的得分
	int idx = (curTaskIdx >= 0 && curTaskIdx < taskRowTitles.size() && taskRowTitles[curTaskIdx] == taskName)
	              ? curTaskIdx
	              : -1;

	if (idx == -1) {
		for (int i = 0; i < taskRowTitles.size(); i++) {
			if (taskRowTitles[i] == taskName) {
				idx = i;
				break;
			}
		}
	}

	if (idx >= 0)
		taskScoreLabels[idx]->setText(tr("%1 / %2").arg(allScore).arg(mxScore));

	cursor->insertText(tr("Score of Task %1 : ").arg(taskName), charFormat);
	cursor->insertText(tr("%1 / %2\n").arg(allScore).arg(mxScore), scoreFormat);
	QScrollBar *bar = ui->logViewer->verticalScrollBar();

	if (isOnMaxValue)
		bar->setValue(bar->maximum());
}

void JudgingDialog::contestantJudgingStart(const QString &contestantName) {
	bool isOnMaxValue =
	    ui->logViewer->verticalScrollBar()->value() == ui->logViewer->verticalScrollBar()->maximum();

	// 切换到新的选手：重建卡片并重置题目定位
	curContestantName = contestantName;
	curTaskTitle.clear();
	curTaskIdx = -1;
	rebuildContestantCard();
	updateOverallLabel();

	QTextCharFormat charFormat;
	charFormat.setFontPointSize(12);
	charFormat.setFontWeight(QFont::Bold);
	insertTimePrefix();
	cursor->insertText(tr("Start judging contestant %1").arg(contestantName), charFormat);
	QScrollBar *bar = ui->logViewer->verticalScrollBar();

	if (isOnMaxValue)
		bar->setValue(bar->maximum());
}

void JudgingDialog::contestantJudgingFinished() {
	bool isOnMaxValue =
	    ui->logViewer->verticalScrollBar()->value() == ui->logViewer->verticalScrollBar()->maximum();
	QTextBlockFormat blockFormat;
	cursor->insertBlock(blockFormat);
	cursor->insertBlock(blockFormat);
	QScrollBar *bar = ui->logViewer->verticalScrollBar();

	if (isOnMaxValue)
		bar->setValue(bar->maximum());
}

void JudgingDialog::contestantJudgedDisplay(const QString &contestantName, const int score,
                                            const int mxScore) {
	bool isOnMaxValue =
	    ui->logViewer->verticalScrollBar()->value() == ui->logViewer->verticalScrollBar()->maximum();

	// 一名选手评测完成：推进总体进度计数
	judgedContestants = qMin(judgedContestants + 1, totalContestants);
	curTaskTitle.clear();
	updateOverallLabel();

	QTextBlockFormat blockFormat;
	blockFormat.setLeftMargin(15);
	cursor->insertBlock(blockFormat);
	insertTimePrefix();
	QTextCharFormat charFormat;
	QTextCharFormat scoreFormat;
	charFormat.setFontPointSize(12);
	scoreFormat.setFontPointSize(12);
	scoreFormat.setFontWeight(QFont::Bold);
	scoreFormat.setForeground(QBrush(QColor(QStringLiteral("#15803D"))));
	cursor->insertText(tr("Total score of %1 : ").arg(contestantName), charFormat);
	cursor->insertText(tr("%1 / %2\n").arg(score).arg(mxScore), scoreFormat);
	QScrollBar *bar = ui->logViewer->verticalScrollBar();

	if (isOnMaxValue)
		bar->setValue(bar->maximum());
}

void JudgingDialog::compileError(int progress, int compileState) {
	bool isOnMaxValue =
	    ui->logViewer->verticalScrollBar()->value() == ui->logViewer->verticalScrollBar()->maximum();
	QTextBlockFormat blockFormat;
	blockFormat.setLeftMargin(30);
	cursor->insertBlock(blockFormat);
	insertTimePrefix();
	QTextCharFormat charFormat;
	charFormat.setFontPointSize(9);
	QString text;

	switch (CompileState(compileState)) {
		case NoValidSourceFile:
			text = tr("Cannot find valid source file");
			charFormat.setForeground(QBrush(Qt::white));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#0F172A"))));
			break;

		case NoValidGraderFile:
			text = tr("Main grader (grader.*) cannot be found");
			charFormat.setForeground(QBrush(Qt::white));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#DC2626"))));
			break;

		case CompileError:
			text = tr("Compile error");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#DC2626"))));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#0F172A"))));
			break;

		case CompileTimeLimitExceeded:
			text = tr("Compile time limit exceeded");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#FACC15"))));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#0F172A"))));
			break;

		case InvalidCompiler:
			text = tr("Invalid compiler");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#C026D3"))));
			charFormat.setBackground(QBrush(QColor(QStringLiteral("#0F172A"))));
			break;

		case CompileSuccessfully:
			text = tr("Compile Successfully");
			charFormat.setForeground(QBrush(QColor(QStringLiteral("#94A3B8"))));
			break;
	}

	cursor->insertText(text, charFormat);
	ui->progressBar->setValue(ui->progressBar->value() + progress);
	QScrollBar *bar = ui->logViewer->verticalScrollBar();

	if (isOnMaxValue)
		bar->setValue(bar->maximum());
}

void JudgingDialog::stopJudgingSlot() {
	stopJudging = true;
	emit stopJudgingSignal();
}

void JudgingDialog::reject() { stopJudgingSlot(); }
