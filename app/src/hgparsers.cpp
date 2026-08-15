#include "hgparsers.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <functional>
#include <utility>

namespace {

// `incoming` and `outgoing` print "comparing with ..." before their JSON, and print no JSON at all when
// they found nothing. The array starts a line of its own, which is what keeps a bracket inside the chatter
// - a remote path may hold one - from being mistaken for it.
QJsonArray jsonRecords(const QByteArray& output)
{
	qsizetype start = 0;
	if (!output.startsWith('['))
	{
		start = output.indexOf("\n[");
		if (start < 0)
			return {};
		++start;
	}
	return QJsonDocument::fromJson(output.mid(start)).array();
}

// hg's dates are [seconds since the epoch, seconds *west* of UTC]; QTimeZone counts them east
QString isoDate(const QJsonValue& value)
{
	const QJsonArray parts = value.toArray();
	if (parts.size() < 2)
		return {};

	return QDateTime::fromSecsSinceEpoch(parts.at(0).toInteger(), QTimeZone{ -parts.at(1).toInt() }).toString(Qt::ISODate);
}

// `user` is a whole "Name <email>"; the list shows the name alone, as it does for git
QString authorName(const QString& user)
{
	const qsizetype bracket = user.indexOf(QLatin1String(" <"));
	return bracket < 0 ? user : user.left(bracket);
}

// What this changeset is known by besides its node. "tip" is left out: it names whichever changeset is
// newest and so says nothing about this one.
QString refsOf(const QJsonObject& record)
{
	QStringList refs;
	for (const QJsonValue& bookmark : record.value(QStringLiteral("bookmarks")).toArray())
		refs << bookmark.toString();
	for (const QJsonValue& tag : record.value(QStringLiteral("tags")).toArray())
	{
		if (tag.toString() != QLatin1String("tip"))
			refs << tag.toString();
	}

	const QString branch = record.value(QStringLiteral("branch")).toString();
	if (!branch.isEmpty() && branch != QLatin1String("default"))
		refs << branch;
	return refs.join(QStringLiteral(", "));
}

QStringList nodeList(const QJsonValue& value)
{
	QStringList nodes;
	for (const QJsonValue& node : value.toArray())
		nodes << node.toString();
	return nodes;
}

// Each half of a "path = source" or "node path" line, or nothing for a line that is neither
std::pair<QString, QString> splitAt(const QByteArray& line, QChar separator)
{
	const QString text = QString::fromUtf8(line).trimmed();
	if (text.isEmpty() || text.startsWith(QLatin1Char('#')))
		return {};

	const qsizetype at = text.indexOf(separator);
	if (at < 0)
		return {};
	return { text.left(at).trimmed(), text.mid(at + 1).trimmed() };
}

} // namespace

namespace Hg {

WorkingDirectory parseWorkingDirectory(const QByteArray& logOutput)
{
	const QJsonArray records = jsonRecords(logOutput);
	if (records.isEmpty())
		return {};

	const QJsonObject record = records.first().toObject();
	return { nodeList(record.value(QStringLiteral("parents"))), record.value(QStringLiteral("branch")).toString() };
}

std::vector<CommitFileChange> parseStatus(const QByteArray& statusOutput)
{
	const QJsonArray records = jsonRecords(statusOutput);

	QSet<QString> renameSources;
	for (const QJsonValue& value : records)
	{
		const QString source = value.toObject().value(QStringLiteral("source")).toString();
		if (!source.isEmpty())
			renameSources.insert(source);
	}

	std::vector<CommitFileChange> entries;
	entries.reserve(size_t(records.size()));
	for (const QJsonValue& value : records)
	{
		const QJsonObject record = value.toObject();
		const QString status = record.value(QStringLiteral("status")).toString();
		const QString path = record.value(QStringLiteral("path")).toString();
		const QString source = record.value(QStringLiteral("source")).toString();
		if (status.isEmpty() || path.isEmpty())
			continue;

		CommitFileChange entry;
		entry.path = path;
		entry.oldPath = source;
		switch (status.at(0).toLatin1())
		{
		case 'M': entry.type = ChangeType::Modified; break;
		case 'A': entry.type = source.isEmpty() ? ChangeType::Added : ChangeType::Renamed; break;
		case 'R':
			if (renameSources.contains(path))
				continue; // the same file leaving its old path, already listed as the rename
			entry.type = ChangeType::Deleted;
			break;
		case '!': entry.type = ChangeType::Deleted; break;
		case '?': entry.type = ChangeType::Untracked; break;
		default: continue; // clean and ignored records, which the app never asks for
		}
		entries.push_back(std::move(entry));
	}
	return entries;
}

std::vector<CommitRecord> parseCommitLog(const QByteArray& logOutput)
{
	const QJsonArray records = jsonRecords(logOutput);

	std::vector<CommitRecord> commits;
	commits.reserve(size_t(records.size()));
	for (const QJsonValue& value : records)
	{
		const QJsonObject record = value.toObject();

		CommitRecord commit;
		commit.sha = record.value(QStringLiteral("node")).toString();
		commit.parents = nodeList(record.value(QStringLiteral("parents")));
		commit.parents.removeAll(QString::fromLatin1(NullNode)); // a root changeset has no parent the app can name
		commit.author = authorName(record.value(QStringLiteral("user")).toString());
		commit.date = isoDate(record.value(QStringLiteral("date")));
		commit.refs = refsOf(record);
		commit.message = record.value(QStringLiteral("desc")).toString();
		commits.push_back(std::move(commit));
	}
	return commits;
}

QStringList parseBranchNames(const QByteArray& branchesOutput)
{
	QStringList names;
	for (const QJsonValue& value : jsonRecords(branchesOutput))
		names << value.toObject().value(QStringLiteral("branch")).toString();
	return names;
}

std::vector<GrepMatch> parseGrepDiff(const QByteArray& grepOutput)
{
	std::map<QString, GrepMatch> byNode;
	for (const QJsonValue& value : jsonRecords(grepOutput))
	{
		const QJsonObject record = value.toObject();
		const QString node = record.value(QStringLiteral("node")).toString();
		if (node.isEmpty())
			continue;

		GrepMatch& match = byNode[node];
		match.node = node;
		match.rev = record.value(QStringLiteral("rev")).toInt();
		if (record.value(QStringLiteral("change")).toString() == QLatin1String("-"))
			++match.matchedLines.removed;
		else
			++match.matchedLines.added;
	}

	std::vector<GrepMatch> matches;
	matches.reserve(byNode.size());
	for (auto& [node, match] : byNode)
		matches.push_back(std::move(match));
	std::ranges::sort(matches, std::ranges::greater{}, &GrepMatch::rev);
	return matches;
}

std::map<QString, QString> parseSubrepoState(const QByteArray& content)
{
	std::map<QString, QString> nodes;
	for (const QByteArray& line : content.split('\n'))
	{
		// "<node> <path>", and a path may hold spaces of its own - only the first one separates
		const auto [node, path] = splitAt(line, QLatin1Char(' '));
		if (!path.isEmpty())
			nodes[path] = node;
	}
	return nodes;
}

std::map<QString, QString> parseSubrepoSources(const QByteArray& content)
{
	std::map<QString, QString> sources;
	for (const QByteArray& line : content.split('\n'))
	{
		const auto [path, source] = splitAt(line, QLatin1Char('='));
		if (!path.isEmpty())
			sources[path] = source;
	}
	return sources;
}

} // namespace Hg
