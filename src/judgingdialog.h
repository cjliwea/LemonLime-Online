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

#include "base/LemonType.hpp"
#include <QDialog>
#include <QTextCursor>
#include <QVector>

class Contest;
class QLabel;
class QGroupBox;
class QTimer;
class QVBoxLayout;

namespace Ui {
	class JudgingDialog;
}

class JudgingDialog : public QDialog {
	Q_OBJECT

  public:
	explicit JudgingDialog(QWidget *parent = nullptr);
	~JudgingDialog();
	void setContest(Contest *);
	void judge(const QList<std::pair<QString, QVector<int>>> &);
	void judgeAll();
	void reject();

  private slots:
	void stopJudgingSlot();
	void sendNotify(QString, QString);

  private:
	Ui::JudgingDialog *ui;
	Contest *curContest{};
	QTextCursor *cursor;
	bool stopJudging{};

	// —— 重构后的界面元素（对外接口不变，仅内部布局与更新逻辑）——
	QLabel *overallLabel{};     // 总体进度文字
	QGroupBox *contestantBox{}; // 当前选手卡片区
	QVBoxLayout *contestantLayout{};
	QVector<QString> taskRowTitles;      // 每行对应的题名
	QVector<QVector<QLabel *>> taskDots; // 每题测试点状态色块
	QVector<QVector<char>> dotState;     // 色块状态：0 等待 1 评测中 2 已完成
	QVector<QLabel *> taskScoreLabels;   // 每题得分标签
	QString curContestantName;
	QString curTaskTitle;
	int curTaskIdx{-1};
	int totalContestants{};
	int judgedContestants{};
	QTimer *blinkTimer{}; // 「评测中」色块闪烁定时器
	bool blinkOn{};

	void rebuildLayout();       // 基于 .ui 已有控件重建整体布局
	void rebuildContestantCard(); // 重建当前选手卡片（每题一行 + 测试点色块）
	void updateOverallLabel();  // 刷新总体进度文字
	void setDotStyle(int, int, char, const QColor &); // 设置色块状态与颜色
	void insertTimePrefix();    // 插入日志行的灰色时间前缀

  public slots:
	void dialogAlert(const QString &);
	void singleCaseFinished(QString, int, int, int, int, int, int, qint64);
	void singleSubtaskDependenceFinished(int, int, int);
	void taskJudgingStarted(const QString &);
	void taskJudgedDisplay(const QString &, const QList<QList<int>> &, const int);
	void contestantJudgingStart(const QString &);
	void contestantJudgingFinished();
	void contestantJudgedDisplay(const QString &, const int, const int);
	void compileError(int, int);

  signals:
	void stopJudgingSignal();
};
