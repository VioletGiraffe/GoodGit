#pragma once

#include <QString>
#include <QStringList>

#include <optional>
#include <stdint.h>

// What the windows and the models speak, and what every backend answers in. Nothing here names a
// particular version control system, and nothing here is a parse of any one command's output.

enum class ChangeType : uint8_t { Modified, Added, Untracked, Deleted, Renamed, TypeChanged, Conflicted };

// What ignoring a path would mean. Each backend renders one of these into its own syntax, and knows
// where in its own ignore file that syntax belongs.
enum class IgnoreScope : uint8_t
{
	ExactPath, // this one file, at this one place in the tree
	Extension, // any file with this extension, anywhere
	Name,      // any file with this name, anywhere
	Directory, // this directory and everything under it
};

// One offered way to exclude a path: what the menu shows and what gets written, and what it means
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
};

struct CommitRecord
{
	QString sha;
	// What the system's own commands take for this commit, where it has such a thing: Mercurial's revision
	// number, local to the clone and shifted by anything that rewrites history. Git has none and leaves it absent.
	std::optional<int> revision;
	QStringList parents; // more than one is a merge
	QString author;
	QString date;    // ISO 8601 with offset
	QString refs;    // "HEAD -> master, origin/master"; empty for most commits
	QString message; // the whole thing: subject line, blank line, body

	[[nodiscard]] QString subject() const { return message.section(QLatin1Char('\n'), 0, 0); }
};

enum class RepoOp : uint8_t { None, Merge, CherryPick, Revert, Rebase };

struct RepoState
{
	QString branch;      // empty when detached
	QString headSha;     // full sha of HEAD; empty when unborn
	QString headSubject; // subject line of HEAD; empty when unborn
	// HEAD's own shape, for the one action that cares what undoing it would leave behind: none means a root
	// commit with nothing to move back to, more than one means a merge.
	int headParentCount = 0;
	QString upstream;    // empty if none configured
	int ahead = 0; // commits one push would send, so what the push button offers to do
	int behind = 0;
	bool detached = false;
	bool unborn = false;
	RepoOp op = RepoOp::None;

	// Filled only when detached: branch tips that equal HEAD, for the reattachment logic
	QStringList localBranchesAtHead;
	QStringList remoteBranchesAtHead;

	// Subjects of the commits the upstream has not seen, newest first; capped, `ahead` holds the true count
	QStringList unpushedSubjects;

	// Why the last refresh could not establish this state - empty when it could. Everything above is then
	// the last refresh that did, held rather than half-replaced, and nothing may be acted on.
	QString readFailure;

	[[nodiscard]] bool known() const { return readFailure.isEmpty(); }
	[[nodiscard]] bool operationInProgress() const { return op != RepoOp::None; }

	// Whether undoing the last commit is offered at all. Every refusal is here rather than in a backend:
	// a commit the upstream already has would be rewritten out from under it; a merge would be left half
	// taken apart, and a root commit has nothing to move back to; an operation in progress owns HEAD until
	// it finishes; and a detached HEAD has no upstream to tell pushed from unpushed. No upstream at all
	// means nothing can have been pushed.
	[[nodiscard]] bool lastCommitUndoable() const
	{
		return !unborn && !detached && !operationInProgress() && headParentCount == 1
			&& (upstream.isEmpty() || ahead > 0);
	}
};

// What a worktree holds beyond the commit it is on, as a status of that worktree reports it
struct WorktreeDirtiness
{
	bool dirtyTracked = false; // any entry that is not purely untracked
	bool untracked = false;
};

// What a submodule's own worktree holds, as far as the parent was able to determine
enum class SubmoduleContent : uint8_t
{
	Clean,        // also the never-initialized case: an empty directory has nothing inside to lose
	Untracked,    // untracked files only - shown on the row, blocks nothing
	DirtyTracked, // modified tracked files inside
	Unknown,      // the status query inside failed; it may be dirty, so it counts as dirty
};

// A status that could not be run answers nothing about the worktree it was pointed at, and the parent may
// not act on the pointer without that answer - so it is the dirty case, not the clean one.
[[nodiscard]] inline SubmoduleContent submoduleContentOf(bool statusRead, WorktreeDirtiness dirtiness)
{
	if (!statusRead)
		return SubmoduleContent::Unknown;
	if (dirtiness.dirtyTracked)
		return SubmoduleContent::DirtyTracked;
	return dirtiness.untracked ? SubmoduleContent::Untracked : SubmoduleContent::Clean;
}

// One row of the file list: the working tree's delta from the last commit, one path at a time
struct FileEntry
{
	QString path;    // repo-relative, forward slashes; the new path for renames
	QString oldPath; // renames only
	ChangeType type = ChangeType::Modified;

	// Absent wherever the counts are not available for the row: an untracked file is not in the tracked
	// diff, a binary one has no line count, and a submodule's one-line pointer change is not a count of
	// anything. A backend that cannot count lines at all leaves every row without them.
	std::optional<LineCounts> lineCounts;

	bool isSubmodule = false;
	bool pointerMoved = false; // the recorded commit differs from HEAD's
	SubmoduleContent content = SubmoduleContent::Clean;

	// Committing the pointer and discarding it both walk over whatever is inside, so the same content
	// stops either one
	[[nodiscard]] bool contentBlocksPointer() const
	{
		return content == SubmoduleContent::DirtyTracked || content == SubmoduleContent::Unknown;
	}
	[[nodiscard]] bool committable() const { return !isSubmodule || (pointerMoved && !contentBlocksPointer()); }
};
