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

struct FileEntry
{
	QString path;    // repo-relative, forward slashes; the new path for renames
	QString oldPath; // renames only
	ChangeType type = ChangeType::Modified;

	bool isSubmodule = false;
	bool pointerMoved = false;       // the recorded commit differs from HEAD's
	bool dirtyTrackedInside = false; // modified tracked files inside - blocks committing the pointer
	bool untrackedInside = false;    // indicated on the row, does not block

	[[nodiscard]] bool committable() const { return !isSubmodule || (pointerMoved && !dirtyTrackedInside); }
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

	void push(Git::Callback onDone);
	void pushSetUpstream(Git::Callback onDone);

	void addToIndex(const QStringList& paths, Git::Callback onDone);
	void unAdd(const QStringList& paths, Git::Callback onDone);

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

	// At most maxCommits reachable from HEAD, newest first. Widening the window means re-running with
	// a larger cap: a date-ordered walk has no resumable cursor (doc/ARCHITECTURE.md).
	Git::Job* commitLog(int maxCommits, const QObject* context, Git::Callback onDone);
	// The files one commit touched, as parseNameStatusZ input. Empty for a merge - git shows no diff
	// for one without --cc - so detect merges from the parent count, not from an empty result.
	Git::Job* commitFiles(const QString& sha, const QObject* context, Git::Callback onDone);
	Git::Job* commitFileDiff(const QString& sha, const NameStatusEntry& file, const QObject* context, Git::Callback onDone);
	// For a moved submodule pointer: the commits being pulled in, as `log --oneline old..HEAD` run inside the submodule
	void submodulePointerLog(const FileEntry& entry, const QObject* context, Git::Callback onDone);

signals:
	void refreshed();

private:
	void finishRefresh();

private:
	const QString _rootPath;
	QString _gitDir; // absolute; resolved on first refresh. In a submodule .git is a file pointing here.

	RepoState _state;
	std::vector<FileEntry> _files;

	bool _refreshing = false;
	bool _refreshPending = false;

	struct RefreshRun;
	std::shared_ptr<RefreshRun> _run; // shared with the async callbacks; reset invalidates stragglers
};
