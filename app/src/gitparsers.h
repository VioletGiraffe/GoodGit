#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <stdint.h>
#include <vector>

// Free-function parsers for the git outputs the app consumes, kept UI- and process-free
// so they can be exercised directly.

enum class ChangeType : uint8_t { Modified, Added, Untracked, Deleted, Renamed, TypeChanged, Conflicted };

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

struct NameStatusEntry
{
	ChangeType type = ChangeType::Modified;
	QString path;    // the new path for renames
	QString oldPath; // renames only
};

// Input: `diff --name-status -M -z HEAD` output
[[nodiscard]] std::vector<NameStatusEntry> parseNameStatusZ(const QByteArray& diffOutput);

// Input: any NUL-separated path list (`ls-files -z` and friends)
[[nodiscard]] QStringList parseZList(const QByteArray& output);

// Input: `ls-files --stage -z` output. Returns the paths of the gitlink entries - that is, the submodules
[[nodiscard]] QStringList parseGitlinkPaths(const QByteArray& lsFilesOutput);

struct WorktreeDirtiness
{
	bool dirtyTracked = false; // any entry that is not purely untracked
	bool untracked = false;
};

// Input: `status --porcelain -z` output (v1 format)
[[nodiscard]] WorktreeDirtiness parsePorcelainDirtiness(const QByteArray& statusOutput);

struct CommitRecord
{
	QString sha;
	QStringList parents; // more than one is a merge
	QString author;
	QString date;    // ISO 8601 with offset, verbatim from git
	QString refs;    // "HEAD -> master, origin/master"; empty for most commits
	QString subject;
};

// The --format parseCommitLog expects, kept beside it so the two cannot drift. Fields are separated
// by US (0x1f); the subject goes last because it is the one field whose content is unconstrained.
inline constexpr char CommitLogFormat[] = "%H%x1f%P%x1f%an%x1f%aI%x1f%D%x1f%s";

// Input: `log -z --format=<CommitLogFormat>` output - one NUL-terminated record per commit, newest first
[[nodiscard]] std::vector<CommitRecord> parseCommitLog(const QByteArray& logOutput);
