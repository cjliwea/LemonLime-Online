/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "contestscanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>

namespace {

// 一次「目录内配对」的结果
struct PairResult {
	QList<ScannedCase> cases;
	QStringList unpairedInputs;
	QStringList unpairedOutputs;
};

// 与 LemonLime::compareFileName() 一致：先比长度，再比本地化顺序，
// 天然得到 1,2,...,9,10,100 的正确顺序
auto fileNameLess(const QString &a, const QString &b) -> bool {
	if (a.length() != b.length())
		return a.length() < b.length();

	return QString::localeAwareCompare(a, b) < 0;
}

// 把一个目录里的文件按 basename 配对成测试点
auto collectPairs(const QString &dir, const QStringList &inExts, const QStringList &outExts) -> PairResult {
	PairResult result;

	const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files | QDir::NoSymLinks);
	QMap<QString, QString> inMap;
	QMap<QString, QString> outMap;

	for (const QFileInfo &info : files) {
		const QString suffix = info.suffix().toLower();

		if (inExts.contains(suffix))
			inMap.insert(info.completeBaseName(), info.fileName());
		else if (outExts.contains(suffix))
			outMap.insert(info.completeBaseName(), info.fileName());
	}

	QStringList baseNames = inMap.keys();
	std::sort(baseNames.begin(), baseNames.end(), fileNameLess);

	for (const QString &base : baseNames) {
		if (! outMap.contains(base)) {
			result.unpairedInputs.append(inMap.value(base));
			continue;
		}

		ScannedCase one;
		one.inputRel = inMap.value(base);
		one.outputRel = outMap.value(base);
		result.cases.append(one);
	}

	const QStringList outBaseNames = outMap.keys();

	for (const QString &base : outBaseNames) {
		if (! inMap.contains(base))
			result.unpairedOutputs.append(outMap.value(base));
	}

	return result;
}

auto anySubdirHasPairs(const QString &dir, const QStringList &inExts, const QStringList &outExts) -> bool {
	const QFileInfoList subs =
	    QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);

	for (const QFileInfo &info : subs) {
		if (! collectPairs(info.absoluteFilePath(), inExts, outExts).cases.isEmpty())
			return true;
	}

	return false;
}

// 通用「数据目录名」：HydroOJ 等 OJ 包常见的 `题目名/testdata/`、`题目名/data/` 结构里的中间层。
// 收题如果收到这一层，题目名应该回到上一层（真正的题目目录）去取，否则题名会变成 "testdata"。
auto isGenericDataDirName(const QString &name) -> bool {
	static const QSet<QString> generic = {
	    QStringLiteral("testdata"), QStringLiteral("testdatas"), QStringLiteral("data"),
	    QStringLiteral("datas"),    QStringLiteral("tests"),     QStringLiteral("test"),
	    QStringLiteral("cases"),    QStringLiteral("case"),      QStringLiteral("dataset"),
	    QStringLiteral("datasets"), QStringLiteral("input"),     QStringLiteral("inputs"),
	    QStringLiteral("output"),   QStringLiteral("outputs"),   QStringLiteral("io"),
	};

	return generic.contains(name.trimmed().toLower());
}

// 从 problem.yaml 里读 title（HydroOJ 包结构）。读不到就返回空串。
auto readProblemTitle(const QString &dir) -> QString {
	QFile file(QDir(dir).absoluteFilePath(QStringLiteral("problem.yaml")));

	if (! file.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();

	static const QRegularExpression re(QStringLiteral("^\\s*title\\s*:\\s*(.+?)\\s*$"));
	QTextStream stream(&file);
	stream.setEncoding(QStringConverter::Utf8);

	while (! stream.atEnd()) {
		const QRegularExpressionMatch match = re.match(stream.readLine());

		if (! match.hasMatch())
			continue;

		QString title = match.captured(1).trimmed();
		const bool quoted =
		    title.size() >= 2 && ((title.startsWith(QLatin1Char('"')) && title.endsWith(QLatin1Char('"'))) ||
		                          (title.startsWith(QLatin1Char('\'')) && title.endsWith(QLatin1Char('\''))));

		if (quoted)
			title = title.mid(1, title.size() - 2).trimmed();

		return title;
	}

	return QString();
}

const int maxScanDepth = 4;

// 自顶向下找「题目目录」：自己有配对、子目录也有配对 → 自己是容器，继续下钻；
// 自己有配对、子目录没有 → 自己就是题目目录，收下。
void scanDir(const QString &dir, const QStringList &inExts, const QStringList &outExts, int depth,
             QList<ScannedProblem> *problems, QStringList *warnings, int *containers) {
	if (depth > maxScanDepth)
		return;

	const PairResult paired = collectPairs(dir, inExts, outExts);
	const bool hasOwn = ! paired.cases.isEmpty();

	if (hasOwn && ! anySubdirHasPairs(dir, inExts, outExts)) {
		ScannedProblem problem;
		problem.dirPath = QDir(dir).absolutePath();
		problem.metaDir = problem.dirPath;
		problem.cases = paired.cases;

		// HydroOJ 包结构：`题目名/testdata/01.in`。
		// 配对发生在 testdata 这一层，但题目名应该回到上一层取，
		// 否则题名会变成 "testdata"，英文名也会跟着错。
		int logicalDepth = depth;

		if (isGenericDataDirName(QFileInfo(dir).fileName())) {
			const QDir parent = QFileInfo(dir).dir();
			const QString parentName = QFileInfo(parent.absolutePath()).fileName();

			if (parent.exists() && ! parentName.isEmpty()) {
				problem.metaDir = parent.absolutePath();
				logicalDepth = std::max(0, depth - 1);
			}
		}

		if (! paired.unpairedInputs.isEmpty())
			problem.notes.append(QObject::tr("有 %1 个输入文件没有配对的输出文件：%2")
			                         .arg(paired.unpairedInputs.size())
			                         .arg(paired.unpairedInputs.join(QStringLiteral("、"))));

		if (! paired.unpairedOutputs.isEmpty())
			problem.notes.append(QObject::tr("有 %1 个输出文件没有配对的输入文件：%2")
			                         .arg(paired.unpairedOutputs.size())
			                         .arg(paired.unpairedOutputs.join(QStringLiteral("、"))));

		int zeroByteCases = 0;

		for (const ScannedCase &one : problem.cases) {
			const QFileInfo inInfo(QDir(problem.dirPath).absoluteFilePath(one.inputRel));
			const QFileInfo outInfo(QDir(problem.dirPath).absoluteFilePath(one.outputRel));

			if (inInfo.size() == 0 || outInfo.size() == 0)
				++zeroByteCases;
		}

		if (zeroByteCases > 0)
			problem.notes.append(
			    QObject::tr("有 %1 组测试点是 0 字节文件，请确认数据是否导出完整。").arg(zeroByteCases));

		if (logicalDepth >= 2)
			warnings->append(QObject::tr("题目「%1」在比较深的层级（第 %2 层），请确认数据放置位置。")
			                     .arg(QDir::toNativeSeparators(problem.metaDir))
			                     .arg(logicalDepth + 1));

		problems->append(problem);
		return;
	}

	if (hasOwn)
		++(*containers);

	const QFileInfoList subs =
	    QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);

	for (const QFileInfo &info : subs)
		scanDir(info.absoluteFilePath(), inExts, outExts, depth + 1, problems, warnings, containers);
}

// 去掉目录名开头的题号前缀：`1 垚垚的改造` / `T1-abc` / `3、快递` → 后面的部分
auto stripIndexPrefix(const QString &name) -> QString {
	static const QRegularExpression re(
	    QStringLiteral("^\\s*(?:[TtPp]\\s*)?[0-9]+(?:\\s*[\\.\\-_、:：]+\\s*|\\s+)"));
	const QRegularExpressionMatch match = re.match(name);

	if (! match.hasMatch())
		return name;

	const QString rest = name.mid(match.capturedLength());

	if (rest.isEmpty())
		return name;

	return rest;
}

auto isAsciiIdentifier(const QString &text) -> bool {
	static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));

	return re.match(text).hasMatch();
}

// 英文名推断：目录名本身是 ASCII 标识符就用它，否则从测试点文件名里找公共的非数字前缀
auto inferEnglishName(const QString &dirName, const QList<ScannedCase> &cases) -> QString {
	if (isAsciiIdentifier(dirName))
		return dirName;

	if (cases.isEmpty())
		return QString();

	QString common;
	bool first = true;

	for (const ScannedCase &one : cases) {
		QString base = QFileInfo(one.inputRel).completeBaseName();
		int cut = base.length();

		while (cut > 0 && base.at(cut - 1).isDigit())
			--cut;

		base = base.left(cut);

		while (! base.isEmpty() && (base.endsWith(QLatin1Char('_')) || base.endsWith(QLatin1Char('-'))))
			base.chop(1);

		if (first) {
			common = base;
			first = false;
		} else if (common != base) {
			return QString();
		}
	}

	if (isAsciiIdentifier(common))
		return common;

	return QString();
}

auto appendUnique(QStringList *list, const QStringList &extra) -> void {
	for (const QString &one : extra) {
		if (! list->contains(one))
			list->append(one);
	}
}

// 去掉开头的点：".in" -> "in"
auto stripLeadingDot(QString text) -> QString {
	while (text.startsWith(QLatin1Char('.')))
		text.remove(0, 1);

	return text;
}

} // namespace

ContestScanner::ContestScanner()
    : inExts_({QStringLiteral("in")}), outExts_({QStringLiteral("out"), QStringLiteral("ans")}) {}

void ContestScanner::setExtensions(const QStringList &inExts, const QStringList &outExts) {
	inExts_.clear();
	outExts_.clear();

	for (const QString &one : inExts) {
		const QString normalized = stripLeadingDot(one.trimmed()).toLower();

		if (! normalized.isEmpty() && ! inExts_.contains(normalized))
			inExts_.append(normalized);
	}

	for (const QString &one : outExts) {
		const QString normalized = stripLeadingDot(one.trimmed()).toLower();

		if (! normalized.isEmpty() && ! outExts_.contains(normalized))
			outExts_.append(normalized);
	}

	if (inExts_.isEmpty())
		inExts_.append(QStringLiteral("in"));

	if (outExts_.isEmpty()) {
		outExts_.append(QStringLiteral("out"));
		outExts_.append(QStringLiteral("ans"));
	}
}

auto ContestScanner::scan(const QStringList &paths) const -> ScanResult {
	ScanResult result;

	QStringList dirs;
	QStringList files;

	for (const QString &path : paths) {
		const QFileInfo info(path);

		if (info.isDir())
			dirs.append(info.absoluteFilePath());
		else if (info.isFile())
			files.append(info.absoluteFilePath());
	}

	if (dirs.isEmpty() && files.isEmpty()) {
		result.warnings.append(QObject::tr("没有识别到任何文件夹或文件。"));
		return result;
	}

	int containers = 0;

	for (const QString &dir : dirs)
		scanDir(dir, inExts_, outExts_, 0, &result.problems, &result.warnings, &containers);

	if (! files.isEmpty()) {
		const QString parent = QFileInfo(files.first()).absolutePath();
		const PairResult paired = collectPairs(parent, inExts_, outExts_);

		if (paired.cases.isEmpty()) {
			result.warnings.append(QObject::tr("拖入的文件里没有找到配对的输入 / 输出文件。"));
		} else {
			ScannedProblem problem;
			problem.dirPath = QDir(parent).absolutePath();
			problem.metaDir = problem.dirPath;
			problem.cases = paired.cases;
			result.problems.append(problem);
		}
	}

	if (result.problems.isEmpty()) {
		result.kind = ScanResult::Unknown;
		appendUnique(&result.warnings,
		             {QObject::tr("没有在拖入的内容里找到配对的测试点（默认按 %1 与 %2 配对）。")
		                  .arg(inExts_.join(QStringLiteral(" / ")), outExts_.join(QStringLiteral(" / ")))});
		return result;
	}

	// 只有一个题目目录、且没展开过父文件夹 → 单题；否则就是整场比赛
	result.kind = (result.problems.size() == 1 && containers == 0) ? ScanResult::SingleProblem
	                                                              : ScanResult::ContestRoot;

	for (ScannedProblem &problem : result.problems) {
		const QString metaDir = problem.metaDir.isEmpty() ? problem.dirPath : problem.metaDir;
		const QString base = QFileInfo(metaDir).fileName();

		// 题名优先级：problem.yaml 的 title → 目录名（去掉题号前缀）
		if (problem.title.isEmpty())
			problem.title = readProblemTitle(metaDir);

		if (problem.title.isEmpty())
			problem.title = stripIndexPrefix(base);

		if (problem.englishName.isEmpty())
			problem.englishName = inferEnglishName(base, problem.cases);
	}

	// 冲突与缺失检查
	QMap<QString, int> counter;

	for (const ScannedProblem &problem : result.problems) {
		if (! problem.englishName.isEmpty())
			counter[problem.englishName] += 1;
	}

	QStringList duplicated;
	QStringList missing;

	for (auto it = counter.cbegin(); it != counter.cend(); ++it) {
		if (it.value() > 1)
			duplicated.append(it.key());
	}

	for (const ScannedProblem &problem : result.problems) {
		if (problem.englishName.isEmpty())
			missing.append(problem.title);
	}

	if (! duplicated.isEmpty())
		result.warnings.append(QObject::tr("以下英文名 / 源文件名重复，导入前必须改掉：%1")
		                           .arg(duplicated.join(QStringLiteral("、"))));

	if (! missing.isEmpty())
		result.warnings.append(QObject::tr("以下题目无法从目录名推断出英文名，需要手填：%1")
		                           .arg(missing.join(QStringLiteral("、"))));

	if (! dirs.isEmpty()) {
		const QString root = QFileInfo(dirs.first()).fileName();
		result.suggestedTitle = root;
		result.suggestedName = sanitizeEnglishName(root);

		if (result.suggestedName.isEmpty())
			result.suggestedName = QStringLiteral("contest");
	}

	result.containerCount = containers;

	return result;
}

auto ContestScanner::isSafePathComponent(const QString &name) -> bool {
	if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
		return false;

	if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
	    name.contains(QLatin1Char(':')))
		return false;

	if (name.endsWith(QLatin1Char(' ')) || name.endsWith(QLatin1Char('.')))
		return false;

	static const QRegularExpression reserved(
	    QStringLiteral("^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])$"));

	return ! reserved.match(name.toLower()).hasMatch();
}

auto ContestScanner::sanitizeEnglishName(const QString &raw) -> QString {
	QString out;
	out.reserve(raw.size());

	for (const QChar ch : raw) {
		if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
		    (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
		    (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) || ch == QLatin1Char('_'))
			out.append(ch);
		else if (ch == QLatin1Char(' ') || ch == QLatin1Char('-') || ch == QLatin1Char('.'))
			out.append(QLatin1Char('_'));
	}

	while (out.startsWith(QLatin1Char('_')))
		out.remove(0, 1);

	while (out.endsWith(QLatin1Char('_')))
		out.chop(1);

	if (! out.isEmpty() && out.at(0).isDigit())
		out.prepend(QLatin1Char('p'));

	return out;
}
