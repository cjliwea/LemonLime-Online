/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "contestscanner.h"

#include <QString>
#include <QStringList>

// 把识别到的题目测试点复制进比赛目录（data/<englishName>/）。
// 只复制、不移动、不删除源目录。
class ProblemInstaller {
  public:
	enum Overwrite { Skip, Replace };

	struct Report {
		int copiedCases = 0; // 成功复制的测试点组数
		int written = 0;     // 成功复制的文件数
		int skipped = 0;     // 因已存在而跳过的文件数
		int failed = 0;      // 复制失败的文件数
		QStringList messages;
	};

	ProblemInstaller();

	// 比赛目录（绝对路径）
	void setContestDir(const QString &dir);
	void setOverwrite(Overwrite policy);

	auto targetDir(const QString &englishName) const -> QString;

	// 落盘。失败时把原因写进 error。
	auto install(const ScannedProblem &problem, Report *report, QString *error) const -> bool;

  private:
	QString contestDir_;
	Overwrite policy_ = Skip;
};
