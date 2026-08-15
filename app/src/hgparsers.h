#pragma once

#include "vcstypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

// Free-function parsers for the hg outputs the app consumes, kept UI- and process-free so they can be
// exercised directly. Every command is asked with `-T json`: paths arrive forward-slashed there even on
// Windows, a rename's source is a labelled field rather than a line to be paired positionally, and nothing
// needs unescaping. Never ask with `-q` or `-v` - the field set varies with verbosity.
namespace Hg {

// The revision nothing points at: an unborn repository's parent, and the parent of a root changeset
inline constexpr char NullNode[] = "0000000000000000000000000000000000000000";

// The working directory itself, from `log -r 'wdir()' -T json`. Its branch is the one the next commit
// lands on, which after `hg branch` is not yet the parent changeset's.
struct WorkingDirectory
{
	QStringList parents; // nodes, the null one included; two of them is a merge in progress
	QString branch;
};
[[nodiscard]] WorkingDirectory parseWorkingDirectory(const QByteArray& logOutput);

// Input: `status -C -T json`, or `status --change REV -C -T json` - the same shape. A rename arrives as an
// `A` record carrying `source` plus an `R` record for the source path; the two become one Renamed entry.
// Both `R` (removed) and `!` (missing on disk) are Deleted: the distinction is hg's, and the app's vocabulary
// has one state for a file that will not be in the next commit.
[[nodiscard]] std::vector<CommitFileChange> parseStatus(const QByteArray& statusOutput);

// Input: `log -T json`, in whatever order the revset produced. Records keep that order.
[[nodiscard]] std::vector<CommitRecord> parseCommitLog(const QByteArray& logOutput);

// Input: `branches -T json`
[[nodiscard]] QStringList parseBranchNames(const QByteArray& branchesOutput);

// One changeset as a content search found it. `grep --diff` reports one record per changed line, so these
// are counts of matching lines: a changeset whose two counts differ genuinely gained or lost the text
// rather than editing around it.
struct GrepMatch
{
	QString node;
	int rev = 0;
	LineCounts matchedLines;
};

// Input: `grep --diff -T json`. Newest changeset first - hg walks the other way.
[[nodiscard]] std::vector<GrepMatch> parseGrepDiff(const QByteArray& grepOutput);

// Input: the repository root's `.hgsubstate` - one "<node> <path>" line per subrepo. Keyed by path.
[[nodiscard]] std::map<QString, QString> parseSubrepoState(const QByteArray& content);

// Input: the repository root's `.hgsub` - one "<path> = <source>" line per subrepo. Keyed by path; the
// source may name another system, as in "[git]https://...".
[[nodiscard]] std::map<QString, QString> parseSubrepoSources(const QByteArray& content);

} // namespace Hg
