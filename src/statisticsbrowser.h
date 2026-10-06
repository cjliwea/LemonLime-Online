/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//

#include "base/LemonType.hpp"
#include "core/contestant.h"
#include "core/task.h"
#include "core/testcase.h"
#include <QList>
#include <QMap>
#include <QWidget>

namespace Ui {
	class StatisticsBrowser;
}

class Contest;
class TestCase;
class QGridLayout;
class QLabel;
class QTableWidget;
class QTextBrowser;

// 统计页自绘控件（定义在 statisticsbrowser.cpp）
class StatKpiCard;
class StatBarChart;
class StatLineChart;
class StatRankDelegate;

static QString nowBrowserText;

class StatisticsBrowser : public QWidget {
	Q_OBJECT

  public:
	explicit StatisticsBrowser(QWidget *parent = nullptr);
	void setContest(Contest *);
	static void exportStatistics(QWidget *, Contest *);
	~StatisticsBrowser();

  public slots:
	void refresh();

  private:
	void buildDashboard();

	Ui::StatisticsBrowser *ui;
	Contest *curContest{};
	bool dashboardBuilt{};
	// 顶部概览
	QLabel *summaryLabel_{};
	QList<StatKpiCard *> kpiCards_;
	StatBarChart *scoreChart_{};
	QGridLayout *acRingGrid_{};
	QGridLayout *problemGrid_{};
	StatLineChart *timelineChart_{};
	QLabel *timelineSub_{};
	QTableWidget *rankTable_{};
	StatRankDelegate *rankDelegate_{};
	QTextBrowser *detailBrowser_{};

	static bool checkValid(QList<Task *>, const QList<Contestant *> &);
	static QString getScoreNormalChart(const QMap<int, int> &, int, int);
	static QString getTestcaseScoreChart(QList<TestCase *>, QList<QList<QList<int>>>,
	                                     QList<QList<QList<ResultState>>>);
	// 原统计表格（分数段分布 / 总览 / 逐题 / 测试点明细），保留在页面底部并用于导出
	static QString buildDetailHtml(Contest *);
	static void exportStatisticsHtml(QWidget *, const QString &);
};