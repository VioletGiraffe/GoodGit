#include "gitparsers.h"

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

std::vector<NameStatusEntry> parseNameStatusZ(const QByteArray& diffOutput)
{
	std::vector<NameStatusEntry> entries;
	const auto tokens = diffOutput.split('\0');

	// Layout: <letter>\0<path>\0 for most, <R|C><score>\0<old>\0<new>\0 for renames/copies
	for (qsizetype i = 0; i + 1 < tokens.size(); )
	{
		const QByteArray& status = tokens[i];
		if (status.isEmpty())
			break;

		NameStatusEntry entry;
		switch (status[0])
		{
		case 'A': entry.type = ChangeType::Added; break;
		case 'D': entry.type = ChangeType::Deleted; break;
		case 'T': entry.type = ChangeType::TypeChanged; break;
		// The refresh never sees U: `diff --name-status HEAD` calls an unmerged path M, so a conflicted
		// file reaches the list as Modified. Nothing downstream may key off Conflicted to find one.
		case 'U': entry.type = ChangeType::Conflicted; break;
		case 'R': case 'C': entry.type = ChangeType::Renamed; break;
		default:  entry.type = ChangeType::Modified; break;
		}

		if (entry.type == ChangeType::Renamed)
		{
			if (i + 2 >= tokens.size())
				break;
			entry.oldPath = QString::fromUtf8(tokens[i + 1]);
			entry.path = QString::fromUtf8(tokens[i + 2]);
			i += 3;
		}
		else
		{
			entry.path = QString::fromUtf8(tokens[i + 1]);
			i += 2;
		}
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
