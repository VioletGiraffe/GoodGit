#pragma once

#include "repository.h"

#include <memory>

// The git backend: every Repository operation as one or more `git` subprocesses, and the parsing of
// what they print. No libgit2, no reimplemented git logic.
class GitRepository final : public Repository
{
	Q_OBJECT

public:
	explicit GitRepository(QString rootPath, QObject* parent = nullptr);

	[[nodiscard]] VcsKind kind() const override { return VcsKind::Git; }

	void commit(const QString& message, const QStringList& pathspec, const QStringList& untrackedPaths, Vcs::Answer<void> onDone) override;
	void commitMergeState(const QString& message, const QStringList& untrackedPaths, Vcs::Answer<void> onDone) override;

	// Both carry --progress, because into a pipe git prints nothing at all until it finishes; the meter
	// arrives as carriage returns rewriting a single line. Both also carry
	// --recurse-submodules=on-demand, passed explicitly because machine config varies: a superproject
	// commit referencing an unpushed submodule commit is unfetchable, so those go first.
	void undoLastCommit(Vcs::Answer<void> onDone) override;

	Vcs::Job* push(Vcs::Callback onDone) override;
	Vcs::Job* pushSetUpstream(Vcs::Callback onDone) override;
	[[nodiscard]] QString pushCommandLabel(bool setUpstream) const override;

	void fetch(Vcs::Answer<void> onDone) override;

	void addToIndex(const QStringList& paths, Vcs::Answer<void> onDone) override;
	void unAdd(const QStringList& paths, Vcs::Answer<void> onDone) override;
	void discardChanges(const QStringList& pathspec, Vcs::Answer<void> onDone) override;

	void checkoutBranch(const QString& branch, Vcs::Answer<void> onDone) override;
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
	[[nodiscard]] QByteArray ignoreFileWithPatternAdded(QByteArray content, const IgnorePattern& pattern) const override;

	void launchExternalDiffTool(const QString& repoRelativePath) const override;

protected:
	void startRefresh() override;

private:
	struct RefreshRun;
	// The refresh's second round: the queries the base ones' answers call for. Runs once they have all answered.
	void startDependentQueries(const std::shared_ptr<RefreshRun>& run);
	void finishRefresh();
	// A completed run's answers as the state and the rows they describe. Only ever called for a run that
	// answered in full.
	[[nodiscard]] std::vector<FileEntry> filesFromRun(const RefreshRun& run) const;
	[[nodiscard]] RepoState stateFromRun(const RefreshRun& run) const;
	// What every diff in this repository is taken against: HEAD, or the empty tree while there is no HEAD
	[[nodiscard]] QString diffBase() const;

private:
	QString _gitDir; // absolute; resolved on first refresh. In a submodule .git is a file pointing here.
	QString _emptyTreeSha; // resolved on first refresh; empty only if that one query failed

	std::shared_ptr<RefreshRun> _run; // shared with the async callbacks; reset invalidates stragglers
};
