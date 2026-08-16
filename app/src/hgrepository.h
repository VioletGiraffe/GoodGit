#pragma once

#include "queryround.h"
#include "repository.h"

#include <map>
#include <memory>

// The Mercurial backend: every Repository operation as one or more `hg` subprocesses, and the parsing of
// what they print. No reimplemented hg logic.
//
// Two shapes differ from the git backend rather than merely being spelled differently. There is no index,
// so "added but not committed" is hg's own `A` state and nothing is staged on the way to a commit; and
// there are no remote-tracking refs, so what the upstream holds is only ever known from a network query -
// see fetch() and incomingCommits().
class HgRepository final : public Repository
{
	Q_OBJECT

public:
	explicit HgRepository(QString rootPath, QObject* parent = nullptr);

	[[nodiscard]] VcsKind kind() const override { return VcsKind::Mercurial; }

	void commit(const QString& message, const QStringList& pathspec, const QStringList& untrackedPaths, Vcs::Answer<void> onDone) override;
	void commitMergeState(const QString& message, const QStringList& untrackedPaths, Vcs::Answer<void> onDone) override;

	// `-r .` is the current changeset and its ancestors, which still recurses into subrepos
	void undoLastCommit(Vcs::Answer<void> onDone) override;

	Vcs::Job* push(Vcs::Callback onDone) override;
	// Mercurial has no per-branch upstream to set, so this is the same push. The window offers it only after
	// a failure message that is git's, so it is never reached here.
	Vcs::Job* pushSetUpstream(Vcs::Callback onDone) override;
	[[nodiscard]] QString pushCommandLabel(bool setUpstream) const override;

	void fetch(Vcs::Answer<void> onDone) override;

	void addToIndex(const QStringList& paths, Vcs::Answer<void> onDone) override;
	void unAdd(const QStringList& paths, Vcs::Answer<void> onDone) override;
	void discardChanges(const QStringList& pathspec, Vcs::Answer<void> onDone) override;

	void checkoutBranch(const QString& branch, Vcs::Answer<void> onDone) override;
	// Mercurial has no remote-tracking branches to create a local one from. Reached only from the
	// reattachment flow, which a backend without a detached state never enters.
	void createTrackingBranch(const QString& localName, const QString& remoteBranch, Vcs::Answer<void> onDone) override;
	void localBranchExists(const QString& name, const QObject* context, std::function<void(bool)> onDone) override;

	Vcs::Query diffFile(const FileEntry& entry, const QObject* context, Vcs::Answer<QByteArray> onDone) override;
	Vcs::Query diffAllChanges(const QObject* context, Vcs::Answer<QByteArray> onDone) override;

	Vcs::Query commitLog(const LogQuery& query, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone) override;
	Vcs::Query commitsAddingOrRemovingText(const LogQuery& query, const QObject* context, Vcs::Answer<QSet<QString>> onDone) override;
	Vcs::Query incomingCommits(int maxCommits, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone) override;
	Vcs::Query commitFiles(const QString& sha, const QObject* context, Vcs::Answer<std::vector<CommitFileChange>> onDone) override;
	Vcs::Query commitFileCounts(const QString& sha, const QObject* context, Vcs::Answer<std::map<QString, LineCounts>> onDone) override;
	Vcs::Query commitFileDiff(const QString& sha, const CommitFileChange& file, const QObject* context, Vcs::Answer<QByteArray> onDone) override;
	Vcs::Query unpushedCommits(const QObject* context, Vcs::Answer<QSet<QString>> onDone) override;
	Vcs::Query submodulePointerLog(const FileEntry& entry, const QObject* context, Vcs::Answer<QString> onDone) override;

	[[nodiscard]] RepositoryLocation submoduleLocation(const FileEntry& entry) const override;

	[[nodiscard]] QString ignoreFileName() const override;
	[[nodiscard]] std::vector<IgnorePattern> ignorePatternsFor(const QString& repoRelativePath) const override;
	// hg reads the file in sections, each introduced by a `syntax:` line and regular expressions until the
	// first of them, so a pattern belongs in the section its scope is written in - not merely at the end.
	[[nodiscard]] QByteArray ignoreFileWithPatternAdded(QByteArray content, const IgnorePattern& pattern) const override;

	void launchExternalDiffTool(const QString& repoRelativePath) const override;

protected:
	void startRefresh() override;

private:
	struct RefreshRun;
	// The refresh's second round: the queries the first one's answers call for. Runs once they have all answered.
	void startDependentQueries(const std::shared_ptr<RefreshRun>& run);
	void finishRefresh();
	[[nodiscard]] std::vector<FileEntry> filesFromRun(const RefreshRun& run) const;
	[[nodiscard]] RepoState stateFromRun(const RefreshRun& run) const;

	// Every refresh query, run by whichever tool owns the directory it is pointed at: a subrepo of an hg
	// repository may be a git one, and its arguments are then git's.
	[[nodiscard]] QueryRound::Launcher refreshQueries();

	// Whether .hgsub names this subrepo's source as a git repository
	[[nodiscard]] bool isGitSubrepo(const QString& subrepoPath) const;
	// A pathspec as hg takes one: a temp file of NUL-separated paths, named as `listfile0:<name>`. Null if
	// the file could not be created, `onFailure` already on its way with the reason.
	[[nodiscard]] std::shared_ptr<QTemporaryFile> openPathspecFile(const QStringList& paths, const Vcs::Callback& onFailure);

private:
	// The parent's own record of its subrepos, re-read from disk at the start of every refresh: the recorded
	// node per path, and where each one comes from - which is what names the kind a subrepo window opens on.
	std::map<QString, QString> _subrepoNodes;
	std::map<QString, QString> _subrepoSources;

	// What the last incoming query found. Mercurial keeps no local ref for a refresh to read this from, so
	// it is as current as the last Peek - which is what git's behind count is too, being as current as the
	// last fetch.
	int _behind = 0;

	std::shared_ptr<RefreshRun> _run; // shared with the async callbacks; reset invalidates stragglers
};
