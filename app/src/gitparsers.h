#pragma once

#include "vcstypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

// Free-function parsers for the git outputs the app consumes, kept UI- and process-free
// so they can be exercised directly.

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

// Input: `diff --name-status -M -z HEAD` output
[[nodiscard]] std::vector<CommitFileChange> parseNameStatusZ(const QByteArray& diffOutput);

// Input: `diff --numstat -M -z` output. Keyed by path - the new one for a rename, as parseNameStatusZ
// names its entries. A binary file, which git counts as `-`, is absent rather than zero.
[[nodiscard]] std::map<QString, LineCounts> parseNumstatZ(const QByteArray& diffOutput);

// Input: any NUL-separated path list (`ls-files -z` and friends)
[[nodiscard]] QStringList parseZList(const QByteArray& output);

// Input: any newline-separated list; blank lines and surrounding whitespace are dropped
[[nodiscard]] QStringList parseLineList(const QByteArray& output);

// Input: `ls-files --stage -z` output. Returns the paths of the gitlink entries - that is, the submodules
[[nodiscard]] QStringList parseGitlinkPaths(const QByteArray& lsFilesOutput);

struct WorktreeDirtiness
{
	bool dirtyTracked = false; // any entry that is not purely untracked
	bool untracked = false;
};

// Input: `status --porcelain -z` output (v1 format)
[[nodiscard]] WorktreeDirtiness parsePorcelainDirtiness(const QByteArray& statusOutput);

// The --format parseCommitLog expects, kept beside it so the two cannot drift. Fields are separated
// by US (0x1f); the message goes last, being the one field that may contain anything, newlines
// included - records are separated by NUL, so a multi-line field is no problem there.
inline constexpr char CommitLogFormat[] = "%H%x1f%P%x1f%an%x1f%aI%x1f%D%x1f%B";

// Input: `log -z --format=<CommitLogFormat>` output - one NUL-terminated record per commit, newest first
[[nodiscard]] std::vector<CommitRecord> parseCommitLog(const QByteArray& logOutput);
