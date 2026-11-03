#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QHash>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <functional>

// The filesystem changes that cannot alter anything a view reads, under git's and Mercurial's state directories.
// UI- and process-free, so the rules can be tested directly.
// Paths are '/'-separated and relative to the directory the function names.
namespace ChangeFilter {

// Whether an absolute directory is a git directory
using IsGitDirectory = std::function<bool(const QString& absolutePath)>;

// A change in objects/, logs/ or hooks/, to FETCH_HEAD or COMMIT_EDITMSG, or to a lock file.
// The rules hold in every git directory under `gitDir` too: a submodule's under modules/, a linked worktree's under worktrees/.
// `isGitDirectory` finds those: a path cannot show where one begins, a submodule's name holding slashes.
[[nodiscard]] bool isGitNoise(const QString& gitDir, QStringView path, const IsGitDirectory& isGitDirectory);

// A change under an .hg directory in cache/ or wcache/, which read-only commands write
[[nodiscard]] bool isHgNoise(QStringView path);

// A change under the working tree `root` that is noise inside the state directory of a repository nested in it
[[nodiscard]] bool isNestedStateNoise(const QString& root, QStringView path, const IsGitDirectory& isGitDirectory);

// IsGitDirectory over the disk: a HEAD file, and refs/ and objects/ or a commondir file, as git itself requires.
// HEAD alone would not do: refs/remotes/<remote> holds one too.
// Remembers each answer: one batch of changes can name thousands of paths under one directory.
class GitDirectoryProbe
{
public:
	[[nodiscard]] bool operator()(const QString& absolutePath);
	// A directory becomes a git directory when a submodule is cloned into it
	void forget() { _answers.clear(); }

private:
	QHash<QString, bool> _answers;
};

} // namespace ChangeFilter
