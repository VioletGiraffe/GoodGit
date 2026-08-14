#pragma once

#include "vcsprocess.h"
#include "vcstypes.h"

#include <QObject>
#include <QSet>

#include <map>
#include <memory>
#include <vector>

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
	void commit(const QString& message, const QStringList& pathspec, const QStringList& untrackedPaths, Vcs::Answer<void> onDone);

	// Merge/rebase mode: stages all tracked changes plus the given untracked paths, commits with no pathspec
	void commitMergeState(const QString& message, const QStringList& untrackedPaths, Vcs::Answer<void> onDone);

	// The one command reported as a process rather than as an answer: the push log is a console view of
	// one, and shows the exit code when it had nothing to say for itself. Both return the job, so the
	// caller can stream the output into that log while the push runs. They ask for --progress because git
	// writes none into a pipe otherwise; the meter arrives as carriage returns.
	Vcs::Job* push(Vcs::Callback onDone);
	Vcs::Job* pushSetUpstream(Vcs::Callback onDone);
	// Moves the remote-tracking refs. Nothing else in the app does, so state.behind and the incoming
	// list mean only what the last fetch left behind.
	void fetch(Vcs::Answer<void> onDone);

	void addToIndex(const QStringList& paths, Vcs::Answer<void> onDone);
	void unAdd(const QStringList& paths, Vcs::Answer<void> onDone);

	// Restores the pathspec (both sides of every rename) to HEAD, in the index and the worktree alike.
	// Three things the caller must screen for: a path git does not know aborts the whole command, a path
	// in the index but not in HEAD is deleted outright, and a submodule is checked out to the recorded
	// commit - overwriting uncommitted changes inside it without a word, and leaving it detached.
	void discardChanges(const QStringList& pathspec, Vcs::Answer<void> onDone);

	void checkoutBranch(const QString& branch, Vcs::Answer<void> onDone);
	// Creates `localName` tracking `remoteBranch` (e.g. "origin/master") at HEAD, without moving the working tree
	void createTrackingBranch(const QString& localName, const QString& remoteBranch, Vcs::Answer<void> onDone);
	// Not asked as an answer: the query failing is the answer, there being no branch of that name
	void localBranchExists(const QString& name, const QObject* context, std::function<void(bool)> onDone);

	// Read-only queries. Each parses what it ran before it answers, so no command output crosses this
	// line. The query dies with `context`, so pass the object that will display the answer - not the
	// Repository, which outlives any one view of it.

	// Diff providers for the window; the bytes are the caller's to size up and decode. Cancel the
	// returned query when the selection moves on.
	Vcs::Query diffFile(const FileEntry& entry, const QObject* context, Vcs::Answer<QByteArray> onDone);
	// One `-U0` diff of every change at once; feeds the message completion word pool
	Vcs::Query diffAllChanges(const QObject* context, Vcs::Answer<QByteArray> onDone);

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
	Vcs::Query commitLog(const LogQuery& query, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone);
	// The narrower half of a pickaxe: commits where the number of occurrences changed, so the text was
	// genuinely added or removed rather than merely edited around.
	Vcs::Query commitsAddingOrRemovingText(const LogQuery& query, const QObject* context, Vcs::Answer<QSet<QString>> onDone);
	// What the upstream has and HEAD does not, newest first. Compares against the remote-tracking ref,
	// so it is only as current as the last fetch.
	Vcs::Query incomingCommits(int maxCommits, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone);
	// The files one commit touched. Empty for a merge - git shows no diff for one without --cc - so
	// detect merges from the parent count, not from an empty result.
	Vcs::Query commitFiles(const QString& sha, const QObject* context, Vcs::Answer<std::vector<CommitFileChange>> onDone);
	// The lines behind those same files, keyed by path. Its own query, so it may answer either side of
	// the one above; the list is built from whichever arrives first. A file whose lines the backend
	// cannot count is absent rather than zero.
	Vcs::Query commitFileCounts(const QString& sha, const QObject* context, Vcs::Answer<std::map<QString, LineCounts>> onDone);
	Vcs::Query commitFileDiff(const QString& sha, const CommitFileChange& file, const QObject* context, Vcs::Answer<QByteArray> onDone);
	// The shas HEAD holds that its upstream does not. Fails when there is no upstream to compare
	// against - none configured, or a detached HEAD - which is not an error to report.
	Vcs::Query unpushedCommits(const QObject* context, Vcs::Answer<QSet<QString>> onDone);
	// For a moved submodule pointer: the commits being pulled in, one line each
	Vcs::Query submodulePointerLog(const FileEntry& entry, const QObject* context, Vcs::Answer<QString> onDone);

signals:
	void refreshed();

private:
	struct RefreshRun;
	// The refresh's second round: the queries the base ones' answers call for. Runs once they have all answered.
	void startDependentQueries(const std::shared_ptr<RefreshRun>& run);
	void finishRefresh();
	// Takes a completed run's answers as the new state. Only ever called for a run that answered in full.
	void applyRefreshResults(const RefreshRun& run);
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

	std::shared_ptr<RefreshRun> _run; // shared with the async callbacks; reset invalidates stragglers
};
