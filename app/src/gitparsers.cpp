#include "gitparsers.h"

#include <QList>

namespace Git {

BranchHeader parseBranchHeader(const QByteArray& statusOutput)
{
	BranchHeader header;
	for (const QByteArray& line : statusOutput.split('\0'))
	{
		if (!line.startsWith("# branch."))
			continue;

		const int space = line.indexOf(' ', 9);
		if (space < 0)
			continue;
		const QByteArray key = line.mid(9, space - 9);
		const QByteArray value = line.mid(space + 1);

		if (key == "oid")
			header.oid = QString::fromUtf8(value);
		else if (key == "head")
			header.head = QString::fromUtf8(value);
		else if (key == "upstream")
			header.upstream = QString::fromUtf8(value);
		else if (key == "ab")
		{
			// "+<ahead> -<behind>"
			const auto parts = value.split(' ');
			if (parts.size() == 2)
			{
				header.ahead = parts[0].mid(1).toInt();
				header.behind = parts[1].mid(1).toInt();
			}
		}
	}
	return header;
}

QStringList parseUnmergedPaths(const QByteArray& statusOutput)
{
	// Record: u <XY> <sub> <m1> <m2> <m3> <mW> <h1> <h2> <h3> <path>. The path is whatever follows the tenth
	// space rather than the last field, since -z leaves it unquoted and a path may hold spaces of its own.
	constexpr int fieldsBeforePath = 10;

	QStringList paths;
	for (const QByteArray& record : statusOutput.split('\0'))
	{
		if (!record.startsWith("u "))
			continue;

		qsizetype pathStart = 0;
		for (int field = 0; field < fieldsBeforePath && pathStart >= 0; ++field)
		{
			const qsizetype space = record.indexOf(' ', pathStart);
			pathStart = space < 0 ? -1 : space + 1;
		}
		if (pathStart > 0)
			paths.push_back(QString::fromUtf8(record.mid(pathStart)));
	}
	return paths;
}

namespace {

// The status letter of --name-status and --raw alike. `diff --name-status HEAD` calls an unmerged path M,
// so 'U' does not fire during a merge: parseUnmergedPaths is what names the conflicted rows.
ChangeType changeTypeOfLetter(char letter)
{
	switch (letter)
	{
	case 'A': return ChangeType::Added;
	case 'D': return ChangeType::Deleted;
	case 'T': return ChangeType::TypeChanged;
	case 'U': return ChangeType::Conflicted;
	case 'R': case 'C': return ChangeType::Renamed;
	default:  return ChangeType::Modified;
	}
}

// The one or two path tokens following a record's status, appended to `entry` - two for a rename or copy,
// old first. Returns the index past them, or -1 when the record is cut short.
qsizetype readPathTokens(const QList<QByteArray>& tokens, qsizetype i, CommitFileChange& entry)
{
	if (entry.type == ChangeType::Renamed)
	{
		if (i + 1 >= tokens.size())
			return -1;
		entry.oldPath = QString::fromUtf8(tokens[i]);
		entry.path = QString::fromUtf8(tokens[i + 1]);
		return i + 2;
	}
	entry.path = QString::fromUtf8(tokens[i]);
	return i + 1;
}

} // namespace

std::vector<CommitFileChange> parseNameStatusZ(const QByteArray& diffOutput)
{
	std::vector<CommitFileChange> entries;
	const auto tokens = diffOutput.split('\0');

	// Layout: <letter>\0<path>\0 for most, <R|C><score>\0<old>\0<new>\0 for renames/copies
	for (qsizetype i = 0; i + 1 < tokens.size(); )
	{
		const QByteArray& status = tokens[i];
		if (status.isEmpty())
			break;

		CommitFileChange entry;
		entry.type = changeTypeOfLetter(status[0]);
		i = readPathTokens(tokens, i + 1, entry);
		if (i < 0)
			break;
		entries.push_back(std::move(entry));
	}
	return entries;
}

std::vector<CommitFileChange> parseRawZ(const QByteArray& diffOutput)
{
	std::vector<CommitFileChange> entries;
	const auto tokens = diffOutput.split('\0');

	// Layout: ":<oldmode> <newmode> <oldsha> <newsha> <letter>\0<path>\0", with the same rename/copy
	// letters and path pairs as --name-status. The modes are what --name-status cannot say: 160000 on
	// either side is a gitlink, so the row is a submodule.
	for (qsizetype i = 0; i + 1 < tokens.size(); )
	{
		const QByteArray& meta = tokens[i];
		if (!meta.startsWith(':'))
			break;
		const auto fields = meta.mid(1).split(' ');
		if (fields.size() < 5 || fields[4].isEmpty())
			break;

		CommitFileChange entry;
		entry.type = changeTypeOfLetter(fields[4][0]);
		entry.isSubmodule = fields[0] == "160000" || fields[1] == "160000";
		if (entry.isSubmodule) // the side that names a commit; a removal has only the old one
			entry.submoduleSha = QString::fromUtf8(entry.type == ChangeType::Deleted ? fields[2] : fields[3]);
		i = readPathTokens(tokens, i + 1, entry);
		if (i < 0)
			break;
		entries.push_back(std::move(entry));
	}
	return entries;
}

std::map<QString, LineCounts> parseNumstatZ(const QByteArray& diffOutput)
{
	std::map<QString, LineCounts> counts;
	const auto tokens = diffOutput.split('\0');

	// Layout: <added>\t<removed>\t<path>\0, and <added>\t<removed>\t\0<old>\0<new>\0 for renames - the
	// path field spent on nothing, the two names following as records of their own
	for (qsizetype i = 0; i < tokens.size(); ++i)
	{
		const QByteArray& record = tokens[i];
		const qsizetype firstTab = record.indexOf('\t');
		const qsizetype secondTab = firstTab < 0 ? -1 : record.indexOf('\t', firstTab + 1);
		if (secondTab < 0)
			continue;

		QString path = QString::fromUtf8(record.mid(secondTab + 1));
		if (path.isEmpty())
		{
			if (i + 2 >= tokens.size())
				break;
			path = QString::fromUtf8(tokens[i + 2]); // the new name, the one the file list knows the row by
			i += 2;
		}

		bool addedRead = false, removedRead = false;
		const int added = record.left(firstTab).toInt(&addedRead);
		const int removed = record.mid(firstTab + 1, secondTab - firstTab - 1).toInt(&removedRead);
		if (addedRead && removedRead) // git counts a binary file `-` and `-`: no count, rather than a count of none
			counts[path] = { .added = added, .removed = removed };
	}
	return counts;
}

QStringList parseZList(const QByteArray& output)
{
	QStringList paths;
	for (const QByteArray& token : output.split('\0'))
	{
		if (!token.isEmpty())
			paths.push_back(QString::fromUtf8(token));
	}
	return paths;
}

QStringList parseLineList(const QByteArray& output)
{
	QStringList lines;
	for (const QByteArray& line : output.split('\n'))
	{
		const QByteArray trimmed = line.trimmed();
		if (!trimmed.isEmpty())
			lines.push_back(QString::fromUtf8(trimmed));
	}
	return lines;
}

QStringList parseGitlinkPaths(const QByteArray& lsFilesOutput)
{
	// Record format: <mode> <sha1> <stage>\t<path>, mode 160000 being a gitlink
	QStringList paths;
	for (const QByteArray& record : lsFilesOutput.split('\0'))
	{
		if (!record.startsWith("160000"))
			continue;
		const int tab = record.indexOf('\t');
		if (tab > 0)
			paths.push_back(QString::fromUtf8(record.mid(tab + 1)));
	}
	return paths;
}

std::vector<CommitRecord> parseCommitLog(const QByteArray& logOutput)
{
	std::vector<CommitRecord> commits;
	for (const QByteArray& record : logOutput.split('\0'))
	{
		if (record.isEmpty())
			continue; // -z terminates rather than separates, so the last split yields an empty tail

		const QList<QByteArray> fields = record.split('\x1f');
		if (fields.size() < 6)
			continue;

		CommitRecord commit;
		commit.sha = QString::fromUtf8(fields[0]);
		commit.parents = QString::fromUtf8(fields[1]).split(QLatin1Char(' '), Qt::SkipEmptyParts);
		commit.author = QString::fromUtf8(fields[2]);
		commit.date = QString::fromUtf8(fields[3]);
		commit.refs = QString::fromUtf8(fields[4]);
		// Rejoined: a US in the message must not truncate it. %B carries a trailing newline of its own.
		commit.message = QString::fromUtf8(fields.mid(5).join('\x1f')).trimmed();
		commits.push_back(std::move(commit));
	}
	return commits;
}

WorktreeDirtiness parsePorcelainDirtiness(const QByteArray& statusOutput)
{
	WorktreeDirtiness result;
	const auto tokens = statusOutput.split('\0');
	for (qsizetype i = 0; i < tokens.size(); ++i)
	{
		const QByteArray& token = tokens[i];
		if (token.size() < 4) // "XY <path>"
			continue;

		if (token.startsWith("??"))
			result.untracked = true;
		else
			result.dirtyTracked = true;

		// In -z format a rename entry is followed by the origin path as a separate token
		if (token[0] == 'R' || token[0] == 'C')
			++i;
	}
	return result;
}

} // namespace Git
