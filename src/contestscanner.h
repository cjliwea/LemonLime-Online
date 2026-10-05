/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
//

#include "core/task.h"

#include <QList>
#include <QString>
#include <QStringList>

// 单个测试点：两端路径都相对「题目目录」
struct ScannedCase {
	QString inputRel;
	QString outputRel;
};

// 扫描出来的单道题目
struct ScannedProblem {
	QString dirPath;     // 题目目录（绝对路径）
	QString title;       // 展示用题目名，可以是中文
	QString englishName; // 源文件名 / data 子目录名；推不出来时为空，由老师补
	QList<ScannedCase> cases;
	QStringList notes; // 这道题自己的提示（未配对、0 字节等）
};

// 待导入的题目 = 扫描结果 + 老师确认过的导入参数
struct PlannedProblem {
	ScannedProblem scan;
	int fullScore = 100;      // 该题总分（平均分给各测试点）
	int timeLimit = 1000;     // ms
	int memoryLimit = 512;    // MiB
	bool subFolderCheck = true; // 选手提交结构：true = 准考证号/题目/题目.cpp

	// 以下字段只在「新建题目」向导里用，新建比赛向导保持默认值
	Task::TaskType taskType = Task::Traditional;
	Task::ComparisonMode comparisonMode = Task::IgnoreSpacesMode;
	QString diffArguments;
	int realPrecision = 3;
	QString specialJudge;
	QString interactor;
	QString interactorName;
	QString answerFileExtension;
	QStringList sourceFilesPath;
	QStringList sourceFilesName;
	QStringList graderFilesPath;
	QStringList graderFilesName;
};

// 一次扫描的结果
struct ScanResult {
	enum Kind { ContestRoot, SingleProblem, Unknown };

	Kind kind = Unknown;
	QList<ScannedProblem> problems;
	QStringList warnings;
	QString suggestedTitle; // 从拖入的根目录推断的比赛标题
	QString suggestedName;  // 从拖入的根目录推断的保存文件名（已 ASCII 化）
	int containerCount = 0; // 识别出的「父文件夹」层数
};

// 目录形态识别：把老师拖进来的文件夹/文件，自动判断是「整场比赛」还是「单道题目」。
// 纯逻辑、无界面依赖，可以单独回归。
class ContestScanner {
  public:
	ContestScanner();

	void setExtensions(const QStringList &inExts, const QStringList &outExts);

	// paths 里可以混放文件夹与散装文件
	ScanResult scan(const QStringList &paths) const;

	// 路径安全：禁 ..、路径分隔符、Windows 保留名、结尾空格与点
	static bool isSafePathComponent(const QString &name);
	// 把任意字符串压成合法 ASCII 目录名；压不出东西时返回空串
	static QString sanitizeEnglishName(const QString &raw);

  private:
	QStringList inExts_;
	QStringList outExts_;
};
