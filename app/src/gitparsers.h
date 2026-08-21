#pragma once

#include "vcstypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

// Parsers for the git outputs the app consumes. UI- and process-free so they can be tested directly.
namespace Git {

struct BranchHeader
{
	QString oid;      // full sha, or "(initial)" for an unborn HEAD
	QString head;     // branch name, or "(detached)"
	QString upstream; // empty if none configured
	int ahead = 0;
	int behind = 0;
};

// Input: `status --porcelain=v2 --branch --untracked-files=no -z` output
[[nodiscard]] BranchHeader parseBranchHeader(const QByteArray& statusOutput);

// The unmerged paths from the same output.
// The only refresh query that names them: a diff reports an unmerged path as modified, and not every
// conflict leaves a marker in the git directory (an interrupted `stash pop` leaves none).
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

// Input: `diff --cached --raw --no-abbrev -z <tree>` output. Without -M, so every record names one path;
// --no-abbrev, or update-index will not take the object names back.
[[nodiscard]] std::vector<StagedEntry> parseStagedRawZ(const QByteArray& diffOutput);

// Input: `diff --numstat -M -z` output. Keyed by path (the new one for a rename, as parseNameStatusZ names
// its entries). A binary file, which git counts as `-`, is absent rather than zero.
[[nodiscard]] std::map<QString, LineCounts> parseNumstatZ(const QByteArray& diffOutput);

// Input: any NUL-separated path list (`ls-files -z` and friends)
[[nodiscard]] QStringList parseZList(const QByteArray& output);

// Input: any newline-separated list; blank lines and surrounding whitespace are dropped
[[nodiscard]] QStringList parseLineList(const QByteArray& output);

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
inline constexpr char CommitLogFormat[] = "%H%x1f%P%x1f%an%x1f%aI%x1f%D%x1f%B";

// Input: `log -z --format=<CommitLogFormat>` output - one NUL-terminated record per commit, newest first
[[nodiscard]] std::vector<CommitRecord> parseCommitLog(const QByteArray& logOutput);

} // namespace Git
