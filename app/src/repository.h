#pragma once

#include "vcsprocess.h"
#include "vcstypes.h"

DISABLE_COMPILER_WARNINGS
#include <QObject>
#include <QSet>
RESTORE_COMPILER_WARNINGS

#include <map>
#include <vector>

// Escapes the metacharacters common to extended and Python regular expressions. LogQuery::contentSearch is
// always a literal, but every backend's content search takes a pattern.
[[nodiscard]] QString escapedForRegex(const QString& literal);

// What a repository window is opened on
struct RepositoryLocation
{
	VcsKind kind;
	QString root; // absolute
};

// Case-insensitive: a root's spelling depends on the path it was resolved from, and no platform this runs on
// hosts two repositories differing only in case.
// Used by every place that matches repositories (open windows, the recent list), so they agree.
[[nodiscard]] bool sameRepositoryPath(const QString& left, const QString& right);

// One command of a push. Usually just the repository's own, but a superproject commit referencing an
// unpublished submodule commit is unfetchable, so such submodules are pushed first.
struct PushStep
{
	QString workDir; // absolute
	QString subject; // empty for the repository itself, else the submodule's path relative to it
	QString branch;  // what this step pushes, for the offer to set an upstream. Empty where the backend has no such notion
};

// What discarding everything uncommitted inside one submodule would do, or why it cannot be done
struct SubmoduleDiscardPlan
{
	QString refusal;        // non-empty: nothing is discarded. A sentence about the submodule, which the caller's message names
	QStringList restored;   // paths that go back to the submodule's last commit
	QStringList keptOnDisk; // paths that commit does not have: taken out of version control, left on disk
};

// What one repository's uncommitted changes amount to for a discard of all of them. A path its last commit
// does not have is left on disk rather than deleted: nothing here destroys content that was never committed.
// `nestedSubmoduleChanged`: one of the changes is a nested submodule's - a moved pointer, or uncommitted
// changes inside it - which the discard would check out over.
[[nodiscard]] SubmoduleDiscardPlan discardPlanFor(const std::vector<CommitFileChange>& changes, bool nestedSubmoduleChanged);

// One repository of any kind: its state, file entries and every action on it. A submodule is another Repository.
// The windows and models are written against this interface; a backend supplies the commands.
// Every operation is asynchronous.
// Every answer is parsed before delivery: no raw command output crosses this boundary.
class Repository : public QObject
{
	Q_OBJECT

public:
	explicit Repository(QString rootPath, QObject* parent = nullptr);

	[[nodiscard]] const QString& path() const { return _rootPath; }
	[[nodiscard]] QString name() const;
	[[nodiscard]] virtual VcsKind kind() const = 0;
	[[nodiscard]] RepositoryLocation location() const { return { kind(), _rootPath }; }
	[[nodiscard]] const RepoState& state() const { return _state; }
	[[nodiscard]] const std::vector<FileEntry>& files() const { return _files; }
	[[nodiscard]] bool refreshing() const { return _refreshing; }

	// Re-reads the state and the file list. A call while a refresh is running is coalesced into one more
	// run after it, so two runs' answers never interleave.
	void refresh();

	// Commits the pathspec (which must include both sides of every rename). untrackedPaths (a subset of the
	// pathspec) is added to tracking first, and un-added if the commit fails.
	virtual void commit(const QString& message, const QStringList& pathspec, const QStringList& untrackedPaths, Vcs::Answer<void> onDone) = 0;

	// Merge/rebase mode: commits every tracked change plus the given untracked paths, with no pathspec -
	// backends that have in-progress operations forbid a path-limited commit during one.
	// The caller must screen out a still-conflicted path: a backend may take the working tree copy as the
	// resolution, committing the conflict markers with it.
	virtual void commitMergeState(const QString& message, const QStringList& untrackedPaths, Vcs::Answer<void> onDone) = 0;

	// Undoes the last commit, leaving its changes as uncommitted changes.
	// Only called when RepoState::lastCommitUndoRefusal() returns None; every refusal is decided there.
	// A backend that refuses anyway reports it like any failure.
	virtual void undoLastCommit(Vcs::Answer<void> onDone) = 0;

	// Returns the repository to the state it was in before the operation in progress started. Every conflict
	// resolution is lost with it, and a change already uncommitted when the operation began may not survive.
	// Only called while an operation is in progress; which one it is, the backend reads off its own state.
	virtual void abortOperation(Vcs::Answer<void> onDone) = 0;

	// The commands the push needs, in the order they must run; this repository's own is last. Nothing has
	// run yet, so a submodule that cannot be pushed is a refusal here rather than a failure halfway through.
	virtual void planPush(Vcs::Answer<std::vector<PushStep>> onDone) = 0;
	// Runs one planned step. Returns the job so the caller can stream its output into the push log.
	// Reported as a process rather than an answer: the push log shows the exit code when there was no output.
	// `setUpstream` retries a step that reported having no upstream.
	virtual Vcs::Job* runPushStep(const PushStep& step, bool setUpstream, Vcs::Callback onDone) = 0;
	// The command line the push log shows. Not the literal argument list: the invocation invariants and
	// progress flags are noise there.
	[[nodiscard]] virtual QString pushCommandLabel(const PushStep& step, bool setUpstream) const = 0;

	// Updates whatever local state the behind count is read from; Peek runs this before refreshing.
	// A backend without such state succeeds immediately; its count comes from incomingCommits().
	// This, push and incomingCommits() are the only operations that touch the network.
	virtual void fetch(Vcs::Answer<void> onDone) = 0;

	// Puts untracked paths under version control, and takes them back out. Every backend has an "added but
	// not committed" state, under some name.
	virtual void addToIndex(const QStringList& paths, Vcs::Answer<void> onDone) = 0;
	virtual void unAdd(const QStringList& paths, Vcs::Answer<void> onDone) = 0;

	// Records conflicted paths as resolved, the working tree copy standing as the resolution. Kept apart from
	// the file's content by every backend, so a resolved file is an ordinary modification from then on.
	virtual void markResolved(const QStringList& paths, Vcs::Answer<void> onDone) = 0;

	// Restores the pathspec (both sides of every rename) to the last commit, on disk and in the staging area.
	// The caller must screen out:
	//   a path the backend does not know - may abort the whole command
	//   an added-but-not-committed path - deleted outright
	//   a submodule with changes inside - checked out to the recorded commit, silently overwriting them
	virtual void discardChanges(const QStringList& pathspec, Vcs::Answer<void> onDone) = 0;

	// Discarding everything uncommitted inside the submodule at a repo-relative path, which is what a row
	// whose content blocks its pointer needs. The pointer itself is not touched.
	// Blocking: read-only queries inside the submodule, run when the user picks the action.
	[[nodiscard]] virtual SubmoduleDiscardPlan submoduleDiscardPlan(const QString& repoRelativePath) const = 0;
	// Carries out that plan: `restored` goes back to the submodule's last commit, `keptOnDisk` comes out of
	// version control and stays on disk. Untracked files are untouched and the submodule stays on its branch.
	// Only called with a plan that carries no refusal.
	virtual void discardSubmoduleContent(const QString& repoRelativePath, const SubmoduleDiscardPlan& plan, Vcs::Answer<void> onDone) = 0;

	virtual void checkoutBranch(const QString& branch, Vcs::Answer<void> onDone) = 0;
	// Creates `localName` tracking `remoteBranch` (e.g. "origin/master") at HEAD, without moving the working tree
	virtual void createTrackingBranch(const QString& localName, const QString& remoteBranch, Vcs::Answer<void> onDone) = 0;
	// Not an Answer: the query failing means there is no branch of that name
	virtual void localBranchExists(const QString& name, const QObject* context, std::function<void(bool)> onDone) = 0;

	// Read-only queries. Each parses its output before answering. The query dies with `context`, so pass
	// the object that will display the answer - not the Repository, which outlives any one view of it.

	// Diffs; the bytes are the caller's to size up and decode. Cancel the returned query when the
	// selection moves on.

	// One tracked change against the last commit. Untracked files are not asked of the backend: the window
	// reads and shows the file itself.
	virtual Vcs::Query diffFile(const FileEntry& entry, const QObject* context, Vcs::Answer<QByteArray> onDone) = 0;
	// Every change in one diff, with no context lines; feeds the message completion word pool
	virtual Vcs::Query diffAllChanges(const QObject* context, Vcs::Answer<QByteArray> onDone) = 0;

	// The parameters of one history view's walk
	struct LogQuery
	{
		int maxCommits = 0;
		QString path;          // empty: the whole repo. Otherwise the walk follows this one path across renames
		QString contentSearch; // empty: no content search. Always a literal string, never a pattern
		// empty: the walk starts at HEAD. Otherwise it covers this commit's ancestry, which need not overlap the checkout's
		QString startRevision;
		bool ignoreCase = true;
	};

	// At most maxCommits reachable from the start revision, newest first.
	// No commit is listed before any of its children: the lane diagram depends on that ordering.
	// Widening the window means re-running with a larger cap: the walk has no resumable cursor (doc/ARCHITECTURE.md).
	// With a content search: every commit that changed a line containing the text.
	virtual Vcs::Query commitLog(const LogQuery& query, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone) = 0;
	// The narrower half of a content search: commits where the number of occurrences changed.
	// A subset of commitLog's result, except inside binary files, which the listing cannot see.
	virtual Vcs::Query commitsAddingOrRemovingText(const LogQuery& query, const QObject* context, Vcs::Answer<QSet<QString>> onDone) = 0;
	// What the upstream has and HEAD does not, newest first. A backend may read the remote directly or use
	// what fetch() brought in; Peek does both in that order.
	virtual Vcs::Query incomingCommits(int maxCommits, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone) = 0;
	// The files one commit touched. Empty for a merge, which has no diff of its own - detect merges from the
	// parent count, not from an empty result.
	virtual Vcs::Query commitFiles(const QString& sha, const QObject* context, Vcs::Answer<std::vector<CommitFileChange>> onDone) = 0;
	// Line counts for those files, keyed by path.
	// A separate query: it may answer before or after commitFiles(), and the list is built from whichever arrives first.
	// A file the backend cannot count is absent rather than zero; a backend that cannot count at all answers empty.
	virtual Vcs::Query commitFileCounts(const QString& sha, const QObject* context, Vcs::Answer<std::map<QString, LineCounts>> onDone) = 0;
	virtual Vcs::Query commitFileDiff(const QString& sha, const CommitFileChange& file, const QObject* context, Vcs::Answer<QByteArray> onDone) = 0;
	// Commits the upstream does not have. A backend names as many as it can cheaply (git: only those on HEAD's
	// line), so an unlisted commit is not necessarily pushed.
	// Fails without an upstream to compare against (none configured, or detached HEAD); not an error to report.
	virtual Vcs::Query unpushedCommits(const QObject* context, Vcs::Answer<QSet<QString>> onDone) = 0;
	// For a moved submodule pointer at a repo-relative path: the commits being pulled in, one line each
	virtual Vcs::Query submodulePointerLog(const QString& repoRelativePath, const QObject* context, Vcs::Answer<QString> onDone) = 0;

	// Where the submodule at a repo-relative path has its own repository, for opening a window on it. The
	// parent names the kind: a nested repository need not be of the same kind.
	[[nodiscard]] virtual RepositoryLocation submoduleLocation(const QString& repoRelativePath) const = 0;

	// The ignore file at the repository root, and the patterns that would exclude `repoRelativePath` from
	// it - most specific first, in that file's syntax
	[[nodiscard]] virtual QString ignoreFileName() const = 0;
	[[nodiscard]] virtual std::vector<IgnorePattern> ignorePatternsFor(const QString& repoRelativePath) const = 0;
	// `content` with `pattern` added in the section its scope belongs to, creating the section if absent.
	// Empty `content` means the file does not exist yet.
	// The caller does the file I/O; only the placement is the backend's.
	[[nodiscard]] virtual QByteArray ignoreFileWithPatternAdded(QByteArray content, const IgnorePattern& pattern) const = 0;

	// Opens the configured external diff tool on one path, detached from the job queue: the tool blocks the
	// process that launched it until closed, and a queue slot held for minutes would starve refreshes.
	virtual void launchExternalDiffTool(const QString& repoRelativePath) const = 0;

signals:
	void refreshed();

protected:
	// Runs whatever queries the backend needs and calls completeRefresh() exactly once, when the last has answered
	virtual void startRefresh() = 0;

	// A run that could not establish the state (readFailure set) leaves the previous state and rows in place:
	// rows built from partial answers would look as complete as full ones.
	// Only the failure is recorded; the window refuses to act on what it shows.
	void completeRefresh(RepoState state, std::vector<FileEntry> files);

private:
	const QString _rootPath;

	RepoState _state;
	std::vector<FileEntry> _files;

	bool _refreshing = false;
	bool _refreshPending = false;
};
