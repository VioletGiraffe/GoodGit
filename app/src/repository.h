#pragma once

#include "gitparsers.h"
#include "gitprocess.h"

#include <QObject>
#include <QPointer>

#include <vector>

enum class RepoOp : uint8_t { None, Merge, CherryPick, Revert, Rebase };

struct RepoState
{
	QString branch;      // empty when detached
	QString headSha;     // full sha of HEAD; empty when unborn
	QString upstream;    // empty if none configured
	int ahead = 0;
	int behind = 0;
	bool detached = false;
	bool unborn = false;
	RepoOp op = RepoOp::None;

	// Filled only when detached: branch tips that equal HEAD, for the reattachment logic
	QStringList localBranchesAtHead;
	QStringList remoteBranchesAtHead;

	// Subjects of the commits the upstream has not seen, newest first; capped, `ahead` holds the true count
	QStringList unpushedSubjects;

	[[nodiscard]] bool operationInProgress() const { return op != RepoOp::None; }
};

// What a submodule's own worktree holds, as far as the parent was able to determine
enum class SubmoduleContent : uint8_t
{
	Clean,        // also the never-initialized case: an empty directory has nothing inside to lose
	Untracked,    // untracked files only - shown on the row, blocks nothing
	DirtyTracked, // modified tracked files inside
	Unknown,      // the status query inside failed; it may be dirty, so it counts as dirty
};

struct FileEntry
{
	QString path;    // repo-relative, forward slashes; the new path for renames
	QString oldPath; // renames only
	ChangeType type = ChangeType::Modified;

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

// One git repository: state queries and actions, all asynchronous via Git::run.
// The unit of the whole application - a submodule is simply another Repository in another window.
class Repository : public QObject
{
	Q_OBJECT

public:
	explicit Repository(QString rootPath, QObject* parent = nullptr);

	[[nodiscard]] const QString& path() const { return _rootPath; }
	[[nodiscard]] QString name() const;
	[[nodiscard]] const RepoState& state() const { return _state; }
	[[nodiscard]] const std::vector<FileEntry>& files() const { return _files; }
	[[nodiscard]] bool refreshing() const { return _refreshing; }

	void refresh();

	// Commits the given pathspec (which must already include both sides of every rename).
	// untrackedPaths (a subset of the pathspec) is `git add`ed first and un-added again if the commit fails.
	void commit(const QString& message, const QStringList& pathspec, const QStringList& untrackedPaths, Git::Callback onDone);

	// Merge/rebase mode: stages all tracked changes plus the given untracked paths, commits with no pathspec
	void commitMergeState(const QString& message, const QStringList& untrackedPaths, Git::Callback onDone);

	// Both return the job, so the caller can stream the output into a log while the push runs. They ask for
	// --progress because git writes none into a pipe otherwise; the meter arrives as carriage returns.
	Git::Job* push(Git::Callback onDone);
	Git::Job* pushSetUpstream(Git::Callback onDone);
	// Moves the remote-tracking refs. Nothing else in the app does, so state.behind and the incoming
	// list mean only what the last fetch left behind.
	void fetch(Git::Callback onDone);

	void addToIndex(const QStringList& paths, Git::Callback onDone);
	void unAdd(const QStringList& paths, Git::Callback onDone);

	// Restores the pathspec (both sides of every rename) to HEAD, in the index and the worktree alike.
	// Three things the caller must screen for: a path git does not know aborts the whole command, a path
	// in the index but not in HEAD is deleted outright, and a submodule is checked out to the recorded
	// commit - overwriting uncommitted changes inside it without a word, and leaving it detached.
	void discardChanges(const QStringList& pathspec, Git::Callback onDone);

	void checkoutBranch(const QString& branch, Git::Callback onDone);
	// Creates `localName` tracking `remoteBranch` (e.g. "origin/master") at HEAD, without moving the working tree
	void createTrackingBranch(const QString& localName, const QString& remoteBranch, Git::Callback onDone);
	void localBranchExists(const QString& name, const QObject* context, std::function<void(bool)> onDone);

	// Read-only queries. The job dies with `context`, so pass the object that will display the answer -
	// not the Repository, which outlives any one view of it.

	// Diff providers for the window. Kill the returned job when the selection moves on.
	Git::Job* diffFile(const FileEntry& entry, const QObject* context, Git::Callback onDone);
	// One `-U0` diff of every change at once; feeds the message completion word pool
	Git::Job* diffAllChanges(const QObject* context, Git::Callback onDone);

	// Everything one history view is showing, and every parameter of the `git log` behind it
	struct LogQuery
	{
		int maxCommits = 0;
		QString path;    // empty: the whole repo. Otherwise the walk follows this one path across renames
		QString pickaxe; // empty: no content search. A literal string - never a pattern, whatever it contains
		bool ignoreCase = true;
	};

	// At most maxCommits reachable from HEAD, newest first. Widening the window means re-running with
	// a larger cap: a date-ordered walk has no resumable cursor (doc/ARCHITECTURE.md).
	// With a pickaxe this lists every commit that changed a line containing the text.
	Git::Job* commitLog(const LogQuery& query, const QObject* context, Git::Callback onDone);
	// The narrower half of a pickaxe: commits where the number of occurrences changed, so the text was
	// genuinely added or removed rather than merely edited around. Shas only, as parseLineList input.
	Git::Job* commitsAddingOrRemovingText(const LogQuery& query, const QObject* context, Git::Callback onDone);
	// The files one commit touched, as parseNameStatusZ input. Empty for a merge - git shows no diff
	// for one without --cc - so detect merges from the parent count, not from an empty result.
	// What the upstream has and HEAD does not, newest first, as parseCommitLog input. Compares against
	// the remote-tracking ref, so it is only as current as the last fetch.
	Git::Job* incomingCommits(int maxCommits, const QObject* context, Git::Callback onDone);
	Git::Job* commitFiles(const QString& sha, const QObject* context, Git::Callback onDone);
	Git::Job* commitFileDiff(const QString& sha, const NameStatusEntry& file, const QObject* context, Git::Callback onDone);
	// The shas HEAD holds that its upstream does not, as parseLineList input. Fails when there is no
	// upstream to compare against - none configured, or a detached HEAD - which is not an error to report.
	Git::Job* unpushedCommits(const QObject* context, Git::Callback onDone);
	// For a moved submodule pointer: the commits being pulled in, as `log --oneline old..HEAD` run inside the submodule
	void submodulePointerLog(const FileEntry& entry, const QObject* context, Git::Callback onDone);

signals:
	void refreshed();

private:
	void finishRefresh();
	// What every diff in this repository is taken against: HEAD, or the empty tree while there is no HEAD
	[[nodiscard]] QString diffBase() const;

private:
	const QString _rootPath;
	QString _gitDir; // absolute; resolved on first refresh. In a submodule .git is a file pointing here.
	QString _emptyTreeSha; // resolved on first refresh; empty only if that one query failed

	RepoState _state;
	std::vector<FileEntry> _files;

	bool _refreshing = false;
	bool _refreshPending = false;

	struct RefreshRun;
	std::shared_ptr<RefreshRun> _run; // shared with the async callbacks; reset invalidates stragglers
};
