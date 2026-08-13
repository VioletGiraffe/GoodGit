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
