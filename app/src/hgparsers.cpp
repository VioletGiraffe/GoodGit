#include "hgparsers.h"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimeZone>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>

namespace {

constexpr char GitSubrepoPrefix[] = "[git]"; // the only source prefix naming another system

// hg writes a path byte the local encoding cannot decode as a lone surrogate U+DC80..DCFF (its utf8b
// scheme), in raw WTF-8 that fails Qt's JSON parse of the whole document. Those sequences become \udcXX
// escapes, which Qt parses into the lone surrogate; Hg::localBytes restores the byte when the path
// travels back to hg.
QByteArray withLoneSurrogatesEscaped(const QByteArray& json)
{
	QByteArray escaped;
	qsizetype copied = 0;
	for (qsizetype at = json.indexOf('\xED'); at >= 0 && at + 2 < json.size(); at = json.indexOf('\xED', at + 1))
	{
		// U+DC80..DCFF in WTF-8 is ED B2..B3 80..BF; an ED B2/B3 pair is never valid UTF-8
		const uchar b1 = uchar(json.at(at + 1)), b2 = uchar(json.at(at + 2));
		if ((b1 & 0xFE) != 0xB2 || (b2 & 0xC0) != 0x80)
			continue;
		escaped.append(json.constData() + copied, at - copied);
		escaped += "\\u" + QByteArray::number(0xD000 + ((b1 & 0x3F) << 6) + (b2 & 0x3F), 16);
		copied = at + 3;
		at += 2; // with the loop's +1, the search resumes past the sequence
	}
	if (copied == 0)
		return json;
	escaped.append(json.constData() + copied, json.size() - copied);
	return escaped;
}

// `incoming` and `outgoing` print "comparing with ..." before their JSON, and no JSON at all when they found
// nothing. The array starts on a line of its own, which distinguishes it from a bracket in a remote path.
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
	return QJsonDocument::fromJson(withLoneSurrogatesEscaped(output.mid(start))).array();
}

// hg's dates are [seconds since the epoch, seconds *west* of UTC]; QTimeZone counts them east
QString isoDate(const QJsonValue& value)
{
	const QJsonArray parts = value.toArray();
	if (parts.size() < 2)
		return {};

	return QDateTime::fromSecsSinceEpoch(parts.at(0).toInteger(), QTimeZone{ -parts.at(1).toInt() }).toString(Qt::ISODate);
}

// `user` is "Name <email>"; the list shows the name alone, as for git
QString authorName(const QString& user)
{
	const qsizetype bracket = user.indexOf(QLatin1String(" <"));
	return bracket < 0 ? user : user.left(bracket);
}

// Bookmarks, tags and a non-default branch. "tip" is left out: it names whichever changeset is newest.
QString refsOf(const QJsonObject& record)
{
	QStringList refs;
	for (const QJsonValue& bookmark : record.value(QLatin1String("bookmarks")).toArray())
		refs << bookmark.toString();
	for (const QJsonValue& tag : record.value(QLatin1String("tags")).toArray())
	{
		if (tag.toString() != QLatin1String("tip"))
			refs << tag.toString();
	}

	const QString branch = record.value(QLatin1String("branch")).toString();
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

// Splits at the first separator; empty for blank and comment lines.
// Reads repository files rather than command output, so Hg::textFromOutput does not apply; UTF-8 is the
// assumption, no encoding being declared for them.
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

QString textFromOutput(const QByteArray& bytes)
{
	return QString::fromLocal8Bit(bytes);
}

WorkingDirectory parseWorkingDirectory(const QByteArray& logOutput)
{
	const QJsonArray records = jsonRecords(logOutput);
	if (records.isEmpty())
		return {};

	const QJsonObject record = records.first().toObject();
	return { nodeList(record.value(QLatin1String("parents"))), record.value(QLatin1String("branch")).toString() };
}

std::vector<CommitFileChange> parseStatus(const QByteArray& statusOutput)
{
	const QJsonArray records = jsonRecords(statusOutput);

	QSet<QString> renameSources;
	for (const QJsonValue& value : records)
	{
		const QString source = value.toObject().value(QLatin1String("source")).toString();
		if (!source.isEmpty())
			renameSources.insert(source);
	}

	std::vector<CommitFileChange> entries;
	entries.reserve(size_t(records.size()));
	for (const QJsonValue& value : records)
	{
		const QJsonObject record = value.toObject();
		const QString status = record.value(QLatin1String("status")).toString();
		const QString path = record.value(QLatin1String("path")).toString();
		const QString source = record.value(QLatin1String("source")).toString();
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
				continue; // the old path of a rename, already listed as the rename
			entry.type = ChangeType::Deleted;
			break;
		case '!': entry.type = ChangeType::Deleted; break;
		case '?': entry.type = ChangeType::Untracked; break;
		default: continue; // clean and ignored records, never requested
		}
		entries.push_back(std::move(entry));
	}
	return entries;
}

WorktreeDirtiness parseDirtiness(const QByteArray& statusOutput)
{
	WorktreeDirtiness dirtiness;
	for (const QJsonValue& value : jsonRecords(statusOutput))
	{
		const QString status = value.toObject().value(QLatin1String("status")).toString();
		if (status.isEmpty())
			continue;

		switch (status.at(0).toLatin1())
		{
		case 'M': case 'A': case 'R': case '!': dirtiness.dirtyTracked = true; break;
		case '?': dirtiness.untracked = true; break;
		default: break; // clean and ignored records, never requested
		}
	}
	return dirtiness;
}

std::map<QString, LineCounts> parseDiffCounts(const QByteArray& diffOutput)
{
	// A `diff --git` line cannot be diff content: every hunk line carries a '+', '-' or ' ' prefix.
	// The path is read from the `---`/`+++` pair rather than that line: there a space in a path is unambiguous,
	// and a rename names its new path, as the row does.
	std::map<QString, LineCounts> counts;
	QString path;
	bool inHunks = false;

	// The path of `--- a/<path>` or `+++ b/<path>`; empty for the /dev/null side of an add or a removal
	const auto headerPath = [](const QByteArray& line) {
		const QByteArray name = line.mid(4);
		// The counts are keyed by this path and read by the path the JSON status gave, so both must decode alike
		return name.startsWith("a/") || name.startsWith("b/") ? textFromOutput(name.mid(2)) : QString{};
	};

	for (const QByteArray& line : diffOutput.split('\n'))
	{
		if (line.startsWith("diff --git "))
		{
			path.clear();
			inHunks = false;
		}
		else if (line.startsWith("@@"))
			inHunks = true;
		else if (!inHunks)
		{
			if (line.startsWith("--- ") || line.startsWith("+++ "))
			{
				if (const QString named = headerPath(line); !named.isEmpty())
					path = named;
			}
		}
		else if (!path.isEmpty())
		{
			if (line.startsWith('+'))
				++counts[path].added;
			else if (line.startsWith('-'))
				++counts[path].removed;
		}
	}
	return counts;
}

QStringList parseUnresolvedPaths(const QByteArray& resolveOutput)
{
	QStringList paths;
	for (const QJsonValue& value : jsonRecords(resolveOutput))
	{
		const QJsonObject record = value.toObject();
		const QString status = record.value(QLatin1String("mergestatus")).toString();
		// U: unresolved content conflict. P: unresolved path conflict, whose incoming file lands under a
		// ~<hash> name; hg's own commit gate counts both.
		if (status == QLatin1String("U") || status == QLatin1String("P"))
			paths << record.value(QLatin1String("path")).toString();
	}
	return paths;
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
		commit.sha = record.value(QLatin1String("node")).toString();
		if (const QJsonValue rev = record.value(QLatin1String("rev")); rev.isDouble())
			commit.revision = rev.toInt(); // revision 0 is a real one, so presence is tested rather than defaulted
		commit.parents = nodeList(record.value(QLatin1String("parents")));
		commit.parents.removeAll(QString::fromLatin1(NullNode)); // a root changeset's parent
		commit.author = authorName(record.value(QLatin1String("user")).toString());
		commit.date = isoDate(record.value(QLatin1String("date")));
		commit.refs = refsOf(record);
		commit.message = record.value(QLatin1String("desc")).toString();
		commits.push_back(std::move(commit));
	}
	return commits;
}

QStringList parseBranchNames(const QByteArray& branchesOutput)
{
	QStringList names;
	for (const QJsonValue& value : jsonRecords(branchesOutput))
		names << value.toObject().value(QLatin1String("branch")).toString();
	return names;
}

QStringList parsePathNames(const QByteArray& pathsOutput)
{
	QStringList names;
	for (const QJsonValue& value : jsonRecords(pathsOutput))
		names << value.toObject().value(QLatin1String("name")).toString();
	return names;
}

std::vector<GrepMatch> parseGrepDiff(const QByteArray& grepOutput)
{
	std::map<QString, GrepMatch> byNode;
	for (const QJsonValue& value : jsonRecords(grepOutput))
	{
		const QJsonObject record = value.toObject();
		const QString node = record.value(QLatin1String("node")).toString();
		if (node.isEmpty())
			continue;

		GrepMatch& match = byNode[node];
		match.node = node;
		match.rev = record.value(QLatin1String("rev")).toInt();
		if (record.value(QLatin1String("change")).toString() == QLatin1String("-"))
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
		// "<node> <path>"; the path may contain spaces
		const auto [node, path] = splitAt(line, QLatin1Char(' '));
		if (!path.isEmpty())
			nodes[path] = node;
	}
	return nodes;
}

std::vector<SubrepoPointerChange> parseSubstateDiff(const QByteArray& diffOutput)
{
	// Every hunk line is a "<node> <path>" leaving or entering the file; the only other lines starting with
	// '-' or '+' are the `---`/`+++` header pair
	std::map<QString, SubrepoPointerChange> byPath;
	for (const QByteArray& line : diffOutput.split('\n'))
	{
		if (line.startsWith("---") || line.startsWith("+++"))
			continue;
		const bool oldSide = line.startsWith('-');
		if (!oldSide && !line.startsWith('+'))
			continue;

		const auto [node, path] = splitAt(line.mid(1), QLatin1Char(' '));
		if (path.isEmpty())
			continue;

		SubrepoPointerChange& change = byPath[path];
		change.path = path;
		(oldSide ? change.oldNode : change.newNode) = node;
	}

	std::vector<SubrepoPointerChange> changes;
	changes.reserve(byPath.size());
	for (auto& [path, change] : byPath)
		changes.push_back(std::move(change));
	return changes;
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

VcsKind subrepoKind(const QString& source)
{
	return source.startsWith(QLatin1String(GitSubrepoPrefix)) ? VcsKind::Git : VcsKind::Mercurial;
}

} // namespace Hg
