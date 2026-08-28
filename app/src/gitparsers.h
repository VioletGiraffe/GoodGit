#pragma once

#include "vcstypes.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <map>
#include <vector>

// Parsers for the git outputs the app consumes. UI- and process-free so they can be tested directly.
namespace Git {

// The text of a path git printed, and the bytes git reads for one.
// Git stores path bytes verbatim, so on Unix a path is the filesystem's bytes. Qt encodes file names with
// the local 8-bit codec, so decoding them the same way keeps a path from git and one from QDir meaning the
// same file, and lets a path survive the trip back to git through argv or a pathspec. Git for Windows
// stores UTF-8 whatever the codepage.
// Paths only: a commit message, an author and a ref are UTF-8 by git's own convention, whatever the locale.
[[nodiscard]] QString pathFromOutput(const QByteArray& bytes);
[[nodiscard]] QByteArray pathBytes(const QString& path);

// The pathspec bytes for `--pathspec-from-file=- --pathspec-file-nul`, which every mutation passes on stdin
[[nodiscard]] QByteArray nulJoinedPaths(const QStringList& paths);

struct BranchHeader
{
	QString oid;      // full sha, or "(initial)" for an unborn HEAD
	QString head;     // branch name, or "(detached)"
	QString upstream; // empty if none configured
	int ahead = 0;
	int behind = 0;
	// The `ab` field was present and parsed. With an upstream set, absent means its ref does not resolve
	// (deleted on the remote and pruned): the counts are then unknown, not zero.
	bool aheadBehindKnown = false;
};

// Input: `status --porcelain=v2 --branch --untracked-files=no -z` output
[[nodiscard]] BranchHeader parseBranchHeader(const QByteArray& statusOutput);

// The unmerged paths from the same output.
// The only refresh query that names them: a diff reports an unmerged path as modified or (UD) not at all,
// and not every conflict leaves a marker in the git directory (an interrupted `stash pop` leaves none).
[[nodiscard]] QStringList parseUnmergedPaths(const QByteArray& statusOutput);

// Input: `diff --name-status -M -z HEAD` output
[[nodiscard]] std::vector<CommitFileChange> parseNameStatusZ(const QByteArray& diffOutput);

// Input: `show --raw --no-abbrev -M -z` output: the rows of --name-status plus modes and object names.
// Mode 160000 (gitlink) marks a submodule; its object names are the commits the pointer moved between.
// Requires --no-abbrev, or those arrive as prefixes.
[[nodiscard]] std::vector<CommitFileChange> parseRawZ(const QByteArray& diffOutput);

// One index entry as a `--cached` diff reports it against a tree. Modes are the six raw digits, "000000"
// where that side has no entry for the path.
struct StagedEntry
{
	QString path;
	QByteArray treeMode;
	QByteArray indexMode;
	QByteArray indexSha;
};

// Input: `diff --cached --raw --no-abbrev --no-renames -z <tree>` output.
// --no-renames: detection is on by default, and a rename record names two paths where every other names one.
// --no-abbrev, or update-index will not take the object names back.
[[nodiscard]] std::vector<StagedEntry> parseStagedRawZ(const QByteArray& diffOutput);

// Input: `diff --numstat -M -z` output. Keyed by path (the new one for a rename, as parseNameStatusZ names
// its entries). A binary file, which git counts as `-`, is absent rather than zero.
[[nodiscard]] std::map<QString, LineCounts> parseNumstatZ(const QByteArray& diffOutput);

// Input: any NUL-separated path list (`ls-files -z` and friends)
[[nodiscard]] QStringList parseZList(const QByteArray& output);

// Input: any newline-separated list; blank lines and surrounding whitespace are dropped
[[nodiscard]] QStringList parseLineList(const QByteArray& output);

// The query whose output parseGitlinkPaths reads. The index rather than `git submodule status`: that is a
// shell script in Git for Windows and costs more than every other refresh query combined.
[[nodiscard]] QStringList submoduleListingArgs();
// Input: `ls-files --stage -z` output. Returns the paths of the gitlink entries, i.e. the submodules
[[nodiscard]] QStringList parseGitlinkPaths(const QByteArray& lsFilesOutput);

// One gitlink: the submodule's path, and the commit a tree records for it
struct GitlinkEntry
{
	QString path;
	QString sha;
};

// Input: `ls-tree -r -z <rev>` output - that revision's gitlinks, as opposed to the index's above. A push
// publishes what the commits record, not what is staged.
[[nodiscard]] std::vector<GitlinkEntry> parseGitlinkEntries(const QByteArray& lsTreeOutput);

// Input: `status --porcelain -z` output (v1 format)
[[nodiscard]] WorktreeDirtiness parsePorcelainDirtiness(const QByteArray& statusOutput);

// The --format parseCommitLog expects. Fields are separated by US (0x1f); the message goes last since it may
// contain anything, newlines included (records are NUL-separated, so that is no problem).
// The message must stay last: git permits US inside an ident name, and parseCommitLog's tail rejoin then
// absorbs the surplus fields of such a row instead of losing the record.
inline constexpr char CommitLogFormat[] = "%H%x1f%P%x1f%an%x1f%aI%x1f%D%x1f%B";

// Input: `log -z --format=<CommitLogFormat>` output - one NUL-terminated record per commit, newest first
[[nodiscard]] std::vector<CommitRecord> parseCommitLog(const QByteArray& logOutput);

} // namespace Git
