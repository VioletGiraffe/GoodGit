#pragma once

#include "vcsprocess.h"
#include "vcstypes.h"

#include <QObject>
#include <QSet>

#include <map>
#include <vector>

// The version control systems the app can drive
enum class VcsKind : uint8_t { Git };

// A repository named the way a window is opened on one: what it is, and where it is
struct RepositoryLocation
{
	VcsKind kind;
	QString root; // absolute
};

// One repository, of whatever kind: its state, its file entries, and every action on it. The unit of
// the whole application - a submodule is simply another Repository in another window.
//
// This is what the windows and the models are written against; a backend supplies the commands behind
// it. Every operation on the repository is asynchronous, and every answer is parsed before it arrives,
// so no command output crosses this line in either direction.
class Repository : public QObject
{
	Q_OBJECT

public:
	explicit Repository(QString rootPath, QObject* parent = nullptr);

	[[nodiscard]] const QString& path() const { return _rootPath; }
	[[nodiscard]] QString name() const;
	[[nodiscard]] virtual VcsKind kind() const = 0;
	// This repository named the way a second window on it is opened
	[[nodiscard]] RepositoryLocation location() const { return { kind(), _rootPath }; }
	[[nodiscard]] const RepoState& state() const { return _state; }
	[[nodiscard]] const std::vector<FileEntry>& files() const { return _files; }
	[[nodiscard]] bool refreshing() const { return _refreshing; }

	// Re-reads the state and the file list. Re-entry while one is running is coalesced into one more run
	// after it, so the answers of two runs never interleave.
	void refresh();

	// Commits the given pathspec (which must already include both sides of every rename).
	// untrackedPaths (a subset of the pathspec) is added to tracking first, and un-added if the commit fails.
	virtual void commit(const QString& message, const QStringList& pathspec, const QStringList& untrackedPaths, Vcs::Answer<void> onDone) = 0;

	// Merge/rebase mode: commits every tracked change plus the given untracked paths, with no pathspec -
	// the backends that have an in-progress operation at all forbid a path-limited commit during one.
	virtual void commitMergeState(const QString& message, const QStringList& untrackedPaths, Vcs::Answer<void> onDone) = 0;

	// The one command reported as a process rather than as an answer: the push log is a console view of
	// one, and shows the exit code when it had nothing to say for itself. Both return the job, so the
	// caller can stream the output into that log while the push runs.
	virtual Vcs::Job* push(Vcs::Callback onDone) = 0;
	virtual Vcs::Job* pushSetUpstream(Vcs::Callback onDone) = 0;
	// What the push log names as the command it is showing the output of. Not the literal argument list:
	// the invocation invariants and the progress flags are noise there.
	[[nodiscard]] virtual QString pushCommandLabel(bool setUpstream) const = 0;

	// Reads from the remote and records what it found, so the ahead/behind counts and the incoming list
	// mean only what the last fetch left behind. The only thing in the app that goes to the network
	// besides push.
	virtual void fetch(Vcs::Answer<void> onDone) = 0;

	// Puts untracked paths under version control, and takes them back out again. "Added but not yet
	// committed" is a state every backend has; what it is called there is the backend's business.
	virtual void addToIndex(const QStringList& paths, Vcs::Answer<void> onDone) = 0;
	virtual void unAdd(const QStringList& paths, Vcs::Answer<void> onDone) = 0;

	// Restores the pathspec (both sides of every rename) to the last commit, on disk and in whatever the
	// backend stages through. Three things the caller must screen for: a path the backend does not know
	// may abort the whole command, a path that is added but not committed is deleted outright, and a
	// submodule is checked out to the recorded commit - overwriting uncommitted changes inside it
	// without a word.
	virtual void discardChanges(const QStringList& pathspec, Vcs::Answer<void> onDone) = 0;

	virtual void checkoutBranch(const QString& branch, Vcs::Answer<void> onDone) = 0;
	// Creates `localName` tracking `remoteBranch` (e.g. "origin/master") at HEAD, without moving the working tree
	virtual void createTrackingBranch(const QString& localName, const QString& remoteBranch, Vcs::Answer<void> onDone) = 0;
	// Not asked as an answer: the query failing is the answer, there being no branch of that name
	virtual void localBranchExists(const QString& name, const QObject* context, std::function<void(bool)> onDone) = 0;

	// Read-only queries. Each parses what it ran before it answers. The query dies with `context`, so
	// pass the object that will display the answer - not the Repository, which outlives any one view of it.

	// Diff providers for the window; the bytes are the caller's to size up and decode. Cancel the
	// returned query when the selection moves on.

	// One tracked change against the last commit. An untracked file has no other side to diff against and
	// is not asked of the backend at all: the window reads it and shows what it holds.
	virtual Vcs::Query diffFile(const FileEntry& entry, const QObject* context, Vcs::Answer<QByteArray> onDone) = 0;
	// One diff of every change at once, with no context lines; feeds the message completion word pool
	virtual Vcs::Query diffAllChanges(const QObject* context, Vcs::Answer<QByteArray> onDone) = 0;

	// Everything one history view is showing, and every parameter of the walk behind it
	struct LogQuery
	{
		int maxCommits = 0;
		QString path;          // empty: the whole repo. Otherwise the walk follows this one path across renames
		QString contentSearch; // empty: no content search. A literal string - never a pattern, whatever it contains
		bool ignoreCase = true;
	};

	// At most maxCommits reachable from HEAD, newest first. Widening the window means re-running with
	// a larger cap: a date-ordered walk has no resumable cursor (doc/ARCHITECTURE.md).
	// With a content search this lists every commit that changed a line containing the text.
	virtual Vcs::Query commitLog(const LogQuery& query, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone) = 0;
	// The narrower half of a content search: commits where the number of occurrences changed, so the
	// text was genuinely added or removed rather than merely edited around. A subset of the listing
	// above, but for what it finds inside files the listing cannot read.
	virtual Vcs::Query commitsAddingOrRemovingText(const LogQuery& query, const QObject* context, Vcs::Answer<QSet<QString>> onDone) = 0;
	// What the upstream has and HEAD does not, newest first - as current as the last fetch, no more.
	virtual Vcs::Query incomingCommits(int maxCommits, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone) = 0;
	// The files one commit touched. Empty for a merge, which has no diff of its own, so detect merges
	// from the parent count rather than from an empty result.
	virtual Vcs::Query commitFiles(const QString& sha, const QObject* context, Vcs::Answer<std::vector<CommitFileChange>> onDone) = 0;
	// The lines behind those same files, keyed by path. Its own query, so it may answer either side of
	// the one above; the list is built from whichever arrives first. A file whose lines the backend
	// cannot count is absent rather than zero, and a backend that cannot count at all answers empty.
	virtual Vcs::Query commitFileCounts(const QString& sha, const QObject* context, Vcs::Answer<std::map<QString, LineCounts>> onDone) = 0;
	virtual Vcs::Query commitFileDiff(const QString& sha, const CommitFileChange& file, const QObject* context, Vcs::Answer<QByteArray> onDone) = 0;
	// The shas HEAD holds that its upstream does not. Fails when there is no upstream to compare
	// against - none configured, or a detached HEAD - which is not an error to report.
	virtual Vcs::Query unpushedCommits(const QObject* context, Vcs::Answer<QSet<QString>> onDone) = 0;
	// For a moved submodule pointer: the commits being pulled in, one line each
	virtual Vcs::Query submodulePointerLog(const FileEntry& entry, const QObject* context, Vcs::Answer<QString> onDone) = 0;

	// Where a submodule row's own repository is, so a window can be opened on it. The parent names the
	// kind: a nested repository need not be of the same kind as the repository holding it.
	[[nodiscard]] virtual RepositoryLocation submoduleLocation(const FileEntry& entry) const = 0;

	// The ignore file this repository's kind reads, at the repository root, and the patterns that would
	// exclude `repoRelativePath` from it - most specific first, in that file's own syntax.
	[[nodiscard]] virtual QString ignoreFileName() const = 0;
	[[nodiscard]] virtual std::vector<IgnorePattern> ignorePatternsFor(const QString& repoRelativePath) const = 0;
	// `content` with `pattern` added where its scope belongs, creating that section if the file has none.
	// Empty `content` is a file that does not exist yet. The caller reads and writes it; only the placement
	// is the backend's to decide.
	[[nodiscard]] virtual QByteArray ignoreFileWithPatternAdded(QByteArray content, const IgnorePattern& pattern) const = 0;

	// Opens the user's configured external diff tool on one path, detached from the job queue: the tool
	// blocks the process that launched it until it closes, and a queue slot held for minutes would
	// starve refreshes.
	virtual void launchExternalDiffTool(const QString& repoRelativePath) const = 0;

signals:
	void refreshed();

protected:
	// The backend's own refresh: it runs whatever queries its kind needs and calls completeRefresh()
	// exactly once, when the last of them has answered.
	virtual void startRefresh() = 0;

	// The run's answers, whole. A state the run could not establish (readFailure set) leaves the
	// previous state and rows in place rather than replacing them with a half-read repository - rows
	// assembled from some of the answers would look exactly as complete as rows assembled from all of
	// them. Only the failure itself is recorded, and the window refuses to act on what it shows.
	void completeRefresh(RepoState state, std::vector<FileEntry> files);

private:
	const QString _rootPath;

	RepoState _state;
	std::vector<FileEntry> _files;

	bool _refreshing = false;
	bool _refreshPending = false;
};
