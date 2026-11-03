#include "changefilter.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
#include <QList>
RESTORE_COMPILER_WARNINGS

namespace ChangeFilter {

namespace {

// Entries of a git directory that only the tool itself reads back
[[nodiscard]] bool isGitNoiseEntry(QStringView name)
{
	return name == u"objects" || name == u"logs" || name == u"hooks" || name == u"FETCH_HEAD" || name == u"COMMIT_EDITMSG";
}

} // namespace

bool isGitNoise(const QString& gitDir, QStringView path, const IsGitDirectory& isGitDirectory)
{
	const QList<QStringView> components = path.split(u'/', Qt::SkipEmptyParts);
	if (components.isEmpty())
		return false;
	// The rename onto the final name is reported under that name
	if (components.back().endsWith(u".lock"))
		return true;

	QString directory = gitDir;
	bool inGitDirectory = true; // whether `directory` is a git directory, whose entries the rules apply to
	for (qsizetype i = 0; i < components.size(); ++i)
	{
		if (inGitDirectory && isGitNoiseEntry(components[i]))
			return true;
		if (i + 1 == components.size())
			break;

		directory += u'/';
		directory += components[i];
		inGitDirectory = isGitDirectory(directory);
	}
	return false;
}

bool isHgNoise(QStringView path)
{
	const qsizetype slash = path.indexOf(u'/');
	const QStringView first = slash < 0 ? path : path.first(slash);
	return first == u"cache" || first == u"wcache";
}

bool isNestedStateNoise(const QString& root, QStringView path, const IsGitDirectory& isGitDirectory)
{
	for (qsizetype start = 0; start < path.size();)
	{
		qsizetype end = path.indexOf(u'/', start);
		if (end < 0)
			end = path.size();

		const QStringView name = path.sliced(start, end - start);
		if (name == u".git" || name == u".hg")
		{
			const QStringView rest = end < path.size() ? path.sliced(end + 1) : QStringView{};
			if (name == u".hg")
				return isHgNoise(rest);

			QString gitDir = root;
			gitDir += u'/';
			gitDir += path.first(end);
			return isGitNoise(gitDir, rest, isGitDirectory);
		}
		start = end + 1;
	}
	return false;
}

bool GitDirectoryProbe::operator()(const QString& absolutePath)
{
	if (const auto it = _answers.constFind(absolutePath); it != _answers.cend())
		return *it;

	const QDir directory{ absolutePath };
	const bool isGitDirectory = QFileInfo{ directory.filePath(QStringLiteral("HEAD")) }.isFile()
		&& (QFileInfo{ directory.filePath(QStringLiteral("commondir")) }.isFile()
			|| (QFileInfo{ directory.filePath(QStringLiteral("objects")) }.isDir() && QFileInfo{ directory.filePath(QStringLiteral("refs")) }.isDir()));
	_answers.insert(absolutePath, isGitDirectory);
	return isGitDirectory;
}

} // namespace ChangeFilter
