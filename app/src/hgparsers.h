#pragma once

#include "vcstypes.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <expected>
#include <map>
#include <vector>

// Parsers for the hg outputs the app consumes. UI- and process-free so they can be tested directly.
// Every command whose output carries paths or records is run with `-T json`: paths arrive forward-slashed even
// on Windows, a rename's source is a labelled field, and nothing needs unescaping.
// Never use `-q` or `-v`: the field set varies with verbosity.
namespace Hg {

// The text hg's non-JSON output bytes mean: hg writes the local 8-bit encoding, the ANSI codepage on
// Windows. The inverse of Hg::localBytes, which encodes every byte handed back to hg.
// JSON output needs none of this: hg escapes it to ASCII, undecodable path bytes included (see jsonRecords).
[[nodiscard]] QString textFromOutput(const QByteArray& bytes);

// An unborn repository's parent, and the parent of a root changeset
inline constexpr char NullNode[] = "0000000000000000000000000000000000000000";

// From `log -r 'wdir()' -T json`. Its branch is the one the next commit lands on, which after `hg branch`
// differs from the parent changeset's.
struct WorkingDirectory
{
	QStringList parents; // nodes, the null one included; two of them is a merge in progress
	QString branch;
};
[[nodiscard]] WorkingDirectory parseWorkingDirectory(const QByteArray& logOutput);

// Input: `status -C -T json` or `status --change REV -C -T json`.
// A rename arrives as an `A` record carrying `source` plus an `R` record for the source path; the two become one Renamed entry.
// Both `R` (removed) and `!` (missing on disk) become Deleted: the app has one state for a file that will not be in the next commit.
[[nodiscard]] std::vector<CommitFileChange> parseStatus(const QByteArray& statusOutput);

// Input: `status -T json`, reduced to whether anything is uncommitted - tracked, untracked or both
[[nodiscard]] WorktreeDirtiness parseDirtiness(const QByteArray& statusOutput);

// Input: `diff --git -U0` output, of the working tree or of one changeset.
// Keyed by path (the new one for a rename, as parseStatus names its entries).
// A file with no counted lines is absent rather than zero; that includes binary files, which `diff.nobinary`
// (see hgprocess) reduces to a one-line note.
[[nodiscard]] std::map<QString, LineCounts> parseDiffCounts(const QByteArray& diffOutput);

// Input: `resolve --list -T json`, which lists every file the merge touched while the mergestate lives.
// Returns the ones still conflicted; a resolved file is marked `R` and is an ordinary modification by then.
[[nodiscard]] QStringList parseUnresolvedPaths(const QByteArray& resolveOutput);

// Input: `log -T json`. Records keep the revset's order.
[[nodiscard]] std::vector<CommitRecord> parseCommitLog(const QByteArray& logOutput);

// Input: `branches -T json`
[[nodiscard]] QStringList parseBranchNames(const QByteArray& branchesOutput);

// Input: `paths -T json`. The configured path names ("default", "default-push"), in hg's order.
[[nodiscard]] QStringList parsePathNames(const QByteArray& pathsOutput);

// `grep --diff` reports one record per changed line, so these count matching lines: a changeset whose two
// counts differ gained or lost the text rather than editing around it
struct GrepMatch
{
	QString node;
	int rev = 0;
	LineCounts matchedLines;
};

// Input: `grep --diff -T json`. Newest changeset first (hg lists them oldest first).
[[nodiscard]] std::vector<GrepMatch> parseGrepDiff(const QByteArray& grepOutput);

// Input: `.hgsubstate` - one "<node> <path>" line per subrepo. Keyed by path.
[[nodiscard]] std::map<QString, QString> parseSubrepoState(const QByteArray& content);

// Input: `files -r <rev> -T {size}` output - one decimal count of bytes.
// Fails on anything else, so a size is never mistaken for zero.
[[nodiscard]] std::expected<qint64, QString> parseFileSize(const QByteArray& output);

// One subrepo pointer as one changeset moved it; an empty side means the subrepo was added or removed
struct SubrepoPointerChange
{
	QString path;
	QString oldNode;
	QString newNode;
};

// Input: one changeset's `diff --git -U0` output scoped to .hgsubstate. Ordered by path.
[[nodiscard]] std::vector<SubrepoPointerChange> parseSubstateDiff(const QByteArray& diffOutput);

// Input: `.hgsub` - one "<path> = <source>" line per subrepo. Keyed by path; the source may name another
// system, as in "[git]https://...".
[[nodiscard]] std::map<QString, QString> parseSubrepoSources(const QByteArray& content);

// The system a `.hgsub` source names: only the "[git]" prefix means another one
[[nodiscard]] VcsKind subrepoKind(const QString& source);

} // namespace Hg
