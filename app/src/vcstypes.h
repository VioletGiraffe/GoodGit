#pragma once

#include <QString>
#include <QStringList>

#include <optional>
#include <stdint.h>

// The VCS-neutral types the windows and models work with and every backend answers in

enum class VcsKind : uint8_t { Git, Mercurial };

// One subrepo of a repository: a git submodule, a Mercurial subrepo. The kind need not be the parent's - a
// git subrepo may sit inside a Mercurial repository.
struct Subrepo
{
	QString path; // relative to the repository that holds it
	VcsKind kind;

	[[nodiscard]] bool operator==(const Subrepo&) const = default;
};

enum class ChangeType : uint8_t { Modified, Added, Untracked, Deleted, Renamed, TypeChanged, Conflicted };

// Each backend renders a scope into its own ignore syntax and knows where in its ignore file it belongs
enum class IgnoreScope : uint8_t
{
	ExactPath, // this one file, at this one place in the tree
	Extension, // any file with this extension, anywhere
	Name,      // any file with this name, anywhere
	Directory, // this directory and everything under it
};

// One offered way to exclude a path: what the menu shows and what gets written
struct IgnorePattern
{
	QString text;
	IgnoreScope scope = IgnoreScope::ExactPath;
};

struct LineCounts
{
	int added = 0;
	int removed = 0;
};

// One file as one commit changed it
struct CommitFileChange
{
	ChangeType type = ChangeType::Modified;
	QString path;    // the new path for renames
	QString oldPath; // renames only
	bool isSubmodule = false; // a submodule pointer rather than file content, so its diff is not the path's own
	// Submodule rows only: the commit the pointer moved to (or held before, if the change removed the
	// submodule). Where the submodule's own history opens.
	QString submoduleSha;
};

struct CommitRecord
{
	QString sha;
	// Mercurial's revision number: local to the clone and shifted by history rewrites. Git has none.
	std::optional<int> revision;
	QStringList parents; // more than one is a merge
	QString author;
	QString date;    // ISO 8601 with offset
	QString refs;    // "HEAD -> master, origin/master"; empty for most commits
	QString message; // the whole thing: subject line, blank line, body

	[[nodiscard]] QString subject() const { return message.section(QLatin1Char('\n'), 0, 0); }
};

enum class RepoOp : uint8_t { None, Merge, CherryPick, Revert, Rebase };

// Why the last commit cannot be undone. Decided here rather than in a backend: git and Mercurial refuse on
// the same grounds.
enum class UndoRefusal : uint8_t
{
	None,
	Unborn,              // no commit to undo
	Detached,            // no upstream to tell a pushed commit from an unpushed one
	OperationInProgress, // owns HEAD
	MergeCommit,         // would be left half undone
	RootCommit,          // nothing to move back to
	AlreadyPushed,       // the upstream would have the commit rewritten out from under it
};

struct RepoState
{
	QString branch;      // empty when detached
	QString headSha;     // full sha of HEAD; empty when unborn
	QString headSubject; // subject line of HEAD; empty when unborn
	int headParentCount = 0; // 0 for a root commit, more than one for a merge; read by lastCommitUndoRefusal()
	QString upstream;    // empty if none configured
	int ahead = 0; // commits one push would send
	int behind = 0;
	bool detached = false;
	bool unborn = false;
	RepoOp op = RepoOp::None;

	// Filled only when detached: branch tips that equal HEAD, for the reattachment logic
	QStringList localBranchesAtHead;
	QStringList remoteBranchesAtHead;

	// Subjects of the commits the upstream has not seen, newest first; capped, `ahead` holds the true count
	QStringList unpushedSubjects;

	// Every subrepo this repository declares, repo-relative and in path order - not only the ones with a
	// file list row. Their kind is the backend's to answer, through submoduleLocation().
	QStringList submodules;

	// Why the last refresh could not establish this state; empty when it could. When set, everything above
	// is from the last successful refresh and nothing may be acted on.
	QString readFailure;

	[[nodiscard]] bool known() const { return readFailure.isEmpty(); }
	[[nodiscard]] bool operationInProgress() const { return op != RepoOp::None; }

	[[nodiscard]] UndoRefusal lastCommitUndoRefusal() const
	{
		if (unborn)
			return UndoRefusal::Unborn;
		if (detached)
			return UndoRefusal::Detached;
		if (operationInProgress())
			return UndoRefusal::OperationInProgress;
		if (headParentCount > 1)
			return UndoRefusal::MergeCommit;
		if (headParentCount == 0)
			return UndoRefusal::RootCommit;
		if (!upstream.isEmpty() && ahead == 0) // no upstream at all means nothing can have been pushed
			return UndoRefusal::AlreadyPushed;

		return UndoRefusal::None;
	}
};

// What a status command reports a worktree holds beyond the commit it is on
struct WorktreeDirtiness
{
	bool dirtyTracked = false; // any entry that is not purely untracked
	bool untracked = false;
};

// What a submodule's own worktree holds, as far as the parent could determine
enum class SubmoduleContent : uint8_t
{
	Clean,        // also the never-initialized case: an empty directory has nothing inside to lose
	Untracked,    // untracked files only - shown on the row, blocks nothing
	DirtyTracked, // modified tracked files inside
	Unknown,      // the status query inside failed; it may be dirty, so it counts as dirty
};

[[nodiscard]] inline SubmoduleContent submoduleContentOf(bool statusRead, WorktreeDirtiness dirtiness)
{
	if (!statusRead)
		return SubmoduleContent::Unknown;
	if (dirtiness.dirtyTracked)
		return SubmoduleContent::DirtyTracked;
	return dirtiness.untracked ? SubmoduleContent::Untracked : SubmoduleContent::Clean;
}

// One row of the file list: one path's delta from the last commit
struct FileEntry
{
	QString path;    // repo-relative, forward slashes; the new path for renames
	QString oldPath; // renames only
	ChangeType type = ChangeType::Modified;

	// Absent for untracked files, binary files and submodule pointer changes, and for every row when the
	// backend cannot count lines at all
	std::optional<LineCounts> lineCounts;

	bool isSubmodule = false;
	bool pointerMoved = false; // the recorded commit differs from HEAD's
	SubmoduleContent content = SubmoduleContent::Clean;

	// Committing the pointer and discarding it both walk over whatever is inside, so the same content blocks both
	[[nodiscard]] bool contentBlocksPointer() const
	{
		return content == SubmoduleContent::DirtyTracked || content == SubmoduleContent::Unknown;
	}
	[[nodiscard]] bool committable() const { return !isSubmodule || (pointerMoved && !contentBlocksPointer()); }
};
