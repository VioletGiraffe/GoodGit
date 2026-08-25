#pragma once

#include "queryround.h"
#include "repository.h"

#include <map>
#include <memory>

// The Mercurial backend: every Repository operation as `hg` subprocesses plus parsing. No reimplemented hg logic.
// Two structural differences from git:
//   No index: "added but not committed" is hg's own `A` state, and nothing is staged on the way to a commit.
//   No remote-tracking refs: what the upstream holds is only known from a network query (fetch(), incomingCommits()).
class HgRepository final : public Repository
{
public:
	explicit HgRepository(QString rootPath, QObject* parent = nullptr);

	[[nodiscard]] VcsKind kind() const override { return VcsKind::Mercurial; }

	void commit(const QString& message, const QStringList& pathspec, const QStringList& untrackedPaths, Vcs::Answer<void> onDone) override;
	void commitMergeState(const QString& message, const QStringList& untrackedPaths, Vcs::Answer<void> onDone) override;

	void undoLastCommit(Vcs::Answer<void> onDone) override;
	void abortOperation(Vcs::Answer<void> onDone) override;

	// `hg push -r .` recurses into subrepositories itself, so the plan is this repository alone.
	// `setUpstream` never arrives true: the window offers it only after a git-specific failure message.
	void planPush(Vcs::Answer<std::vector<PushStep>> onDone) override;
	Vcs::Job* runPushStep(const PushStep& step, bool setUpstream, Vcs::Callback onDone) override;
	[[nodiscard]] QString pushCommandLabel(const PushStep& step, bool setUpstream) const override;

	void fetch(Vcs::Answer<void> onDone) override;

	void addToIndex(const QStringList& paths, Vcs::Answer<void> onDone) override;
	void unAdd(const QStringList& paths, Vcs::Answer<void> onDone) override;
	void markResolved(const QStringList& paths, Vcs::Answer<void> onDone) override;
	void discardChanges(const QStringList& pathspec, Vcs::Answer<void> onDone) override;
	// A subrepo may be a git repository, which answers in git's terms
	[[nodiscard]] SubmoduleDiscardPlan submoduleDiscardPlan(const QString& repoRelativePath) const override;
	void discardSubmoduleContent(const QString& repoRelativePath, const SubmoduleDiscardPlan& plan, Vcs::Answer<void> onDone) override;

	void checkoutBranch(const QString& branch, Vcs::Answer<void> onDone) override;
	// Always fails: Mercurial has no remote-tracking branches. Only reached from the reattachment flow, which
	// a backend without a detached state never enters.
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
	Vcs::Query fileAtRevision(const QString& sha, const QString& repoRelativePath, const QObject* context, Vcs::Answer<QByteArray> onDone) override;
	Vcs::Query unpushedCommits(const QObject* context, Vcs::Answer<QSet<QString>> onDone) override;
	Vcs::Query submodulePointerLog(const QString& repoRelativePath, const QObject* context, Vcs::Answer<QString> onDone) override;

	[[nodiscard]] RepositoryLocation submoduleLocation(const QString& repoRelativePath) const override;

	[[nodiscard]] QString ignoreFileName() const override;
	[[nodiscard]] std::vector<IgnorePattern> ignorePatternsFor(const QString& repoRelativePath) const override;
	// .hgignore is read in sections, each introduced by a `syntax:` line (regular expressions before the
	// first), so a pattern goes into the section matching its scope rather than at the end
	[[nodiscard]] QByteArray ignoreFileWithPatternAdded(QByteArray content, const IgnorePattern& pattern) const override;

	void launchExternalDiffTool(const QString& repoRelativePath) const override;

protected:
	void startRefresh() override;

private:
	struct RefreshRun;
	void launchSubrepoQueries(QueryRound& round, const std::shared_ptr<RefreshRun>& run);
	void finishRefresh();
	[[nodiscard]] std::vector<FileEntry> filesFromRun(const RefreshRun& run) const;
	[[nodiscard]] RepoState stateFromRun(const RefreshRun& run) const;

	// Runs a refresh query with whichever tool owns the directory: a subrepo of an hg repository may be a
	// git one, and the arguments are then git's
	[[nodiscard]] QueryRound::Launcher refreshQueries();

	// Whether .hgsub names this subrepo's source as a git repository
	[[nodiscard]] bool isGitSubrepo(const QString& subrepoPath) const;
	// A temp file of NUL-separated paths, to pass as `listfile0:<name>`. Null if the file could not be
	// created; `onFailure` is then already queued with the reason.
	[[nodiscard]] std::shared_ptr<QTemporaryFile> openPathspecFile(const QStringList& paths, const Vcs::Callback& onFailure);

private:
	// .hgsubstate and .hgsub, re-read at the start of every refresh: the recorded node per subrepo path, and
	// each one's source, which names the kind a subrepo window opens on
	std::map<QString, QString> _subrepoNodes;
	std::map<QString, QString> _subrepoSources;

	// From the last incoming query. Mercurial keeps no local ref to read this from, so it is as current as
	// the last Peek - as git's is as current as the last fetch.
	int _behind = 0;

	std::shared_ptr<RefreshRun> _run; // shared with the async callbacks; reset invalidates stragglers
};
