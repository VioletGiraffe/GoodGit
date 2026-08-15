#include "gitrepository.h"
#include "gitparsers.h"
#include "gitprocess.h"
#include "queryround.h"
#include "settings.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QTemporaryFile>

#include <algorithm>
#include <map>

namespace {

constexpr int MaxUnpushedLogEntries = 30; // tooltip fodder; state.ahead carries the true count

// The commit message travels in a temp file - `-F -` and a stdin pathspec cannot share the pipe
std::shared_ptr<QTemporaryFile> openMessageFile(const QString& message, QObject* context, const Vcs::Callback& onDone)
{
	return Vcs::openTempFile(message.toUtf8(), QStringLiteral("commit message"), context, onDone);
}

// A commit that failed after its untracked paths were added leaves them staged, and the rows would then
// read Added rather than Untracked. The reported result stays the commit's: the reset's says nothing about
// why the commit was refused.
void rollBackAddThenReport(const QString& workDir, const QObject* context, const QStringList& untrackedPaths,
	const ProcessResult& commitResult, const Vcs::Callback& onDone)
{
	if (commitResult.ok || untrackedPaths.isEmpty())
	{
		onDone(commitResult);
		return;
	}

	Git::run(workDir, { QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("--pathspec-from-file=-"),
		QStringLiteral("--pathspec-file-nul") }, context,
		[onDone, commitResult](const ProcessResult&) { onDone(commitResult); }, Vcs::nulJoined(untrackedPaths));
}

// A status that could not be run answers nothing about the worktree it was pointed at, and the parent
// may not act on the pointer without that answer - so it is the dirty case, not the clean one.
SubmoduleContent contentFromStatus(const ProcessResult& statusResult)
{
	if (!statusResult.ok)
		return SubmoduleContent::Unknown;

	const Git::WorktreeDirtiness dirtiness = Git::parsePorcelainDirtiness(statusResult.out);
	if (dirtiness.dirtyTracked)
		return SubmoduleContent::DirtyTracked;
	return dirtiness.untracked ? SubmoduleContent::Untracked : SubmoduleContent::Clean;
}

// The tracked half of the file list, read out two ways: the names, and the lines behind them. The pairing
// and the baseline are shared, or a count would answer for a row that is not in the list. Identical
// whatever it is taken against, so an unborn HEAD changes the baseline and nothing else.
QStringList trackedDiffArgs(const QString& base, const QString& outputFormat)
{
	return { QStringLiteral("diff"), outputFormat, QStringLiteral("-M"),
		QStringLiteral("--ignore-submodules=dirty"), QStringLiteral("-z"), base };
}

QStringList trackedChangesArgs(const QString& base)
{
	return trackedDiffArgs(base, QStringLiteral("--name-status"));
}

// --ignore-cr-at-eol, so the counts are the ones the diff a row opens shows: that carries the flag too,
// and a line-endings-only change reads as no change there.
QStringList trackedChangeCountsArgs(const QString& base)
{
	QStringList args = trackedDiffArgs(base, QStringLiteral("--numstat"));
	args.insert(1, QStringLiteral("--ignore-cr-at-eol"));
	return args;
}

// One commit's files, read out two ways as the file list's own delta is: the names, and the lines behind
// them. The rename detection is shared, or a count would answer for a row that is not in the list.
QStringList commitFilesArgs(const QString& sha, const QString& outputFormat)
{
	return { QStringLiteral("show"), outputFormat, QStringLiteral("-M"), QStringLiteral("-z"),
		QStringLiteral("--format="), sha };
}

// The base of every commit-listing query; the walk itself is whatever the caller appends
QStringList commitLogArgs(int maxCommits)
{
	return { QStringLiteral("log"), QStringLiteral("-z"), QStringLiteral("--max-count=%1").arg(maxCommits),
		QStringLiteral("--format=") + QLatin1String(Git::CommitLogFormat) };
}

// -S counts occurrences and takes the term literally; -G matches patch lines and takes a regex, which
// --fixed-strings does not reach, so `foo(` would abort the whole query unescaped
void appendPickaxe(QStringList& args, const Repository::LogQuery& query, bool countChangesOnly)
{
	if (query.contentSearch.isEmpty())
		return;

	args << (countChangesOnly ? QStringLiteral("-S") + query.contentSearch
		: QStringLiteral("-G") + escapedForRegex(query.contentSearch));
	if (query.ignoreCase)
		args << QStringLiteral("-i");
}

// The pathspec closes the argument list, so everything else has to be in place first
void appendPathLimit(QStringList& args, const Repository::LogQuery& query)
{
	if (!query.path.isEmpty())
		args << QStringLiteral("--follow") << QStringLiteral("--") << query.path; // --follow takes the one path we ever pass
}

// The shas a listing command printed, as the set the log view marks its rows from
QSet<QString> shaSet(const QByteArray& output)
{
	const QStringList shas = Git::parseLineList(output);
	return QSet<QString>{ shas.begin(), shas.end() };
}

QString outputAsText(const QByteArray& output)
{
	return QString::fromUtf8(output);
}

Vcs::Query runQuery(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback)
{
	return Vcs::Query{ Git::run(workDir, std::move(args), context, std::move(callback), {}, /*readOnlyQuery=*/true) };
}

// Every refresh query: read-only, and scoped to the repository that asked
QueryRound::Launcher refreshQueries(const QObject* repo)
{
	return [repo](const QString& workDir, QStringList args, Vcs::Callback onResult) {
		Git::run(workDir, std::move(args), repo, std::move(onResult), {}, /*readOnlyQuery=*/true);
	};
}

// Glob metacharacters in a file name must stay literal; '#' and '!' are special at line start
QString escapedForGitIgnore(const QString& text)
{
	QString out;
	out.reserve(text.size());
	for (const QChar c : text)
	{
		if (c == QLatin1Char('\\') || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('[') || c == QLatin1Char(']'))
			out += QLatin1Char('\\');
		out += c;
	}
	if (out.startsWith(QLatin1Char('#')) || out.startsWith(QLatin1Char('!')))
		out.prepend(QLatin1Char('\\'));
	return out;
}

} // namespace

struct GitRepository::RefreshRun
{
	Git::BranchHeader header;
	QString headSubject;
	std::vector<CommitFileChange> diffEntries;
	std::map<QString, LineCounts> changeCounts; // by path; only the tracked changes have one
	QStringList untracked;
	QStringList submodules; // repo-relative paths of the gitlink entries
	std::map<QString, SubmoduleContent> submoduleContent; // only the submodules that were queried at all
	QStringList localBranchesAtHead;
	QStringList remoteBranchesAtHead;
	QStringList unpushedSubjects;
	QStringList conflicted; // paths with unmerged index entries

	// Why this run cannot become the state. The tracked-changes query keeps its own slot: on an unborn HEAD
	// its attempt against HEAD is meant to fail, and the re-run against the empty tree answers in its place.
	QString failure;
	QString trackedError;

	void noteFailure(const ProcessResult& result)
	{
		if (failure.isEmpty())
			failure = result.errorText();
	}
};

GitRepository::GitRepository(QString rootPath, QObject* parent) :
	Repository(std::move(rootPath), parent)
{
}

void GitRepository::startRefresh()
{
	auto run = std::make_shared<RefreshRun>();
	_run = run;

	// The independent base queries, plus the one-time per-repository resolutions. Whatever their
	// answers call for is asked once they are all in.
	QueryRound round{ refreshQueries(this), [self = QPointer<GitRepository>{ this }, run] {
		if (self)
			self->startDependentQueries(run);
	} };

	if (_gitDir.isEmpty())
	{
		round.launch(path(), { QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir") },
			[this, run](const ProcessResult& r) {
				if (r.ok)
					_gitDir = QString::fromUtf8(r.out.trimmed());
				else
					run->noteFailure(r); // without it a merge in progress is indistinguishable from none
			});
	}
	if (_emptyTreeSha.isEmpty())
	{
		// Stands in for HEAD where there is none. Its name follows from the repository's hash algorithm
		// alone, and git keeps the object itself resolvable everywhere, so nothing has to be written.
		round.launch(path(), { QStringLiteral("hash-object"), QStringLiteral("-t"), QStringLiteral("tree"), QStringLiteral("--stdin") },
			[this](const ProcessResult& r) { _emptyTreeSha = QString::fromUtf8(r.out.trimmed()); });
	}
	// The branch header and the unmerged entries are what this is parsed for, so the flags keep git from
	// scanning the working tree and recursing into submodules to produce entries nobody reads
	round.launch(path(), { QStringLiteral("status"), QStringLiteral("--porcelain=v2"), QStringLiteral("--branch"),
			QStringLiteral("--untracked-files=no"), QStringLiteral("--ignore-submodules=all"), QStringLiteral("-z") },
		[run](const ProcessResult& r) {
			if (r.ok)
			{
				run->header = Git::parseBranchHeader(r.out);
				run->conflicted = Git::parseUnmergedPaths(r.out);
			}
			else
				run->noteFailure(r); // an unread header parses as "on a branch, born", the two things nothing may assume
		});
	// Names HEAD in the message header. An unborn branch has no commit to name, and fails here by design
	round.launch(path(), { QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%s") },
		[run](const ProcessResult& r) { run->headSubject = QString::fromUtf8(r.out.trimmed()); });
	round.launch(path(), trackedChangesArgs(QStringLiteral("HEAD")),
		[run](const ProcessResult& r) {
			if (r.ok)
				run->diffEntries = Git::parseNameStatusZ(r.out);
			else
				run->trackedError = r.errorText(); // an unborn HEAD fails here by design; the empty-tree re-run asks again
		});
	// Losing this costs the rows their counts, which is a shorter answer rather than a wrong one
	round.launch(path(), trackedChangeCountsArgs(QStringLiteral("HEAD")),
		[run](const ProcessResult& r) { run->changeCounts = Git::parseNumstatZ(r.out); });
	// Losing these costs untracked rows, which is a shorter list rather than a wrong one
	round.launch(path(), { QStringLiteral("ls-files"), QStringLiteral("--others"), QStringLiteral("--exclude-standard"), QStringLiteral("-z") },
		[run](const ProcessResult& r) { run->untracked = Git::parseZList(r.out); });
	// The submodule list is read out of the index, not from `git submodule status`: that one is a shell script in
	// Git for Windows and costs more than every other refresh query combined
	round.launch(path(), { QStringLiteral("ls-files"), QStringLiteral("--stage"), QStringLiteral("-z") },
		[run](const ProcessResult& r) {
			if (r.ok)
				run->submodules = Git::parseGitlinkPaths(r.out);
			else
				run->noteFailure(r); // unread, every gitlink demotes to an ordinary file that discarding may overwrite
		});
}

// The queries the base ones' answers call for - the unborn fallback, the detached-HEAD branch tips,
// per-submodule dirtiness, the unpushed subjects. All independent of each other.
void GitRepository::startDependentQueries(const std::shared_ptr<RefreshRun>& run)
{
	QueryRound round{ refreshQueries(this), [self = QPointer<GitRepository>{ this }] {
		if (self)
			self->finishRefresh();
	} };

	// The base query needed a HEAD to compare against. Re-run it against the empty tree, which is
	// what an unborn branch will commit on top of anyway.
	if (run->header.oid == QLatin1String("(initial)"))
	{
		if (_emptyTreeSha.isEmpty())
			run->trackedError = QStringLiteral("The empty tree could not be resolved, leaving nothing to list an unborn repository's changes against.");
		else
		{
			round.launch(path(), trackedChangesArgs(_emptyTreeSha),
				[run](const ProcessResult& r) {
					if (!r.ok)
					{
						run->trackedError = r.errorText();
						return;
					}
					run->diffEntries = Git::parseNameStatusZ(r.out);
					run->trackedError.clear(); // the earlier attempt only failed for want of a HEAD, and this answered instead
				});
			round.launch(path(), trackedChangeCountsArgs(_emptyTreeSha),
				[run](const ProcessResult& r) { run->changeCounts = Git::parseNumstatZ(r.out); });
		}
	}

	if (run->header.head == QLatin1String("(detached)"))
	{
		for (const char* refRoot : { "refs/heads", "refs/remotes" })
		{
			const bool local = qstrcmp(refRoot, "refs/heads") == 0;
			round.launch(path(), { QStringLiteral("for-each-ref"), QStringLiteral("--points-at"), QStringLiteral("HEAD"),
					QStringLiteral("--format=%(refname:short)"), QString::fromLatin1(refRoot) },
				[run, local](const ProcessResult& r) {
					QStringList names = Git::parseLineList(r.out);
					if (!local)
						names.removeIf([](const QString& n) { return n.endsWith(QLatin1String("/HEAD")); });
					(local ? run->localBranchesAtHead : run->remoteBranchesAtHead) = names;
				});
		}
	}

	for (const QString& subPath : run->submodules)
	{
		const QString workDir = path() + QLatin1Char('/') + subPath;
		if (!QFileInfo::exists(workDir + QLatin1String("/.git")))
			continue; // never initialized: the directory is empty, there is nothing inside to query
		round.launch(workDir, { QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("-z") },
			[run, subPath](const ProcessResult& r) { run->submoduleContent[subPath] = contentFromStatus(r); });
	}

	if (!run->header.upstream.isEmpty() && run->header.ahead > 0)
	{
		round.launch(path(), { QStringLiteral("log"), QStringLiteral("--oneline"), QStringLiteral("--no-decorate"),
				QStringLiteral("-n"), QString::number(MaxUnpushedLogEntries), QStringLiteral("@{upstream}..HEAD") },
			[run](const ProcessResult& r) { run->unpushedSubjects = Git::parseLineList(r.out); });
	}
}

void GitRepository::finishRefresh()
{
	const RefreshRun& run = *_run;

	// Only the tracked-changes slot can still hold a failure the second round was the one to answer
	const QString failure = !run.failure.isEmpty() ? run.failure : run.trackedError;

	RepoState state;
	std::vector<FileEntry> files;
	if (failure.isEmpty())
	{
		state = stateFromRun(run);
		files = filesFromRun(run);
	}
	state.readFailure = failure;

	_run.reset();
	completeRefresh(std::move(state), std::move(files));
}

RepoState GitRepository::stateFromRun(const RefreshRun& run) const
{
	RepoState state;
	state.unborn = run.header.oid == QLatin1String("(initial)");
	state.headSha = state.unborn ? QString{} : run.header.oid;
	state.headSubject = run.headSubject;
	state.detached = run.header.head == QLatin1String("(detached)");
	state.branch = state.detached ? QString{} : run.header.head;
	state.upstream = run.header.upstream;
	state.ahead = run.header.ahead;
	state.behind = run.header.behind;
	state.localBranchesAtHead = run.localBranchesAtHead;
	state.remoteBranchesAtHead = run.remoteBranchesAtHead;
	state.unpushedSubjects = run.unpushedSubjects;

	if (!_gitDir.isEmpty())
	{
		const QDir gitDir{ _gitDir };
		if (gitDir.exists(QStringLiteral("MERGE_HEAD")))
			state.op = RepoOp::Merge;
		else if (gitDir.exists(QStringLiteral("CHERRY_PICK_HEAD")))
			state.op = RepoOp::CherryPick;
		else if (gitDir.exists(QStringLiteral("REVERT_HEAD")))
			state.op = RepoOp::Revert;
		else if (gitDir.exists(QStringLiteral("rebase-merge")) || gitDir.exists(QStringLiteral("rebase-apply")))
			state.op = RepoOp::Rebase;
	}
	return state;
}

std::vector<FileEntry> GitRepository::filesFromRun(const RefreshRun& run) const
{
	std::vector<FileEntry> files;

	// A submodule the second round never queried was never initialized, and an empty directory holds nothing
	const auto contentOf = [&run](const QString& path) {
		const auto it = run.submoduleContent.find(path);
		return it != run.submoduleContent.end() ? it->second : SubmoduleContent::Clean;
	};

	// Tracked changes; a diff row whose path is a submodule becomes a moved-pointer submodule row
	for (const CommitFileChange& diffEntry : run.diffEntries)
	{
		FileEntry entry;
		entry.path = diffEntry.path;
		entry.oldPath = diffEntry.oldPath;
		entry.type = diffEntry.type;
		if (run.submodules.contains(diffEntry.path))
		{
			entry.isSubmodule = true;
			entry.pointerMoved = true;
			entry.content = contentOf(diffEntry.path);
		}
		else
		{
			// The diff that named this row cannot tell a conflict from an edit; the unmerged index entries can
			if (run.conflicted.contains(diffEntry.path))
				entry.type = ChangeType::Conflicted;
			if (const auto it = run.changeCounts.find(diffEntry.path); it != run.changeCounts.end())
				entry.lineCounts = it->second;
		}
		files.push_back(std::move(entry));
	}

	for (const QString& path : run.untracked)
		files.push_back({ .path = path, .type = ChangeType::Untracked });

	// Submodules whose pointer has not moved but whose content would stop it moving: shown so the state
	// is visible and the row double-clickable. Untracked-only content inside does not earn a row.
	for (const QString& subPath : run.submodules)
	{
		FileEntry entry{ .path = subPath, .isSubmodule = true, .content = contentOf(subPath) };
		if (!entry.contentBlocksPointer())
			continue;
		if (std::ranges::any_of(files, [&](const FileEntry& f) { return f.isSubmodule && f.path == subPath; }))
			continue; // already present as a moved-pointer row
		files.push_back(std::move(entry));
	}
	return files;
}

void GitRepository::commit(const QString& message, const QStringList& pathspec, const QStringList& untrackedPaths, Vcs::Answer<void> onDone)
{
	// Converted once, here: the steps below pass process results between them, the rollback reporting
	// the commit's rather than its own
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto messageFile = openMessageFile(message, this, report);
	if (!messageFile)
		return;

	const auto runCommit = [this, messageFile, pathspec, untrackedPaths, report] {
		Git::run(path(), { QStringLiteral("commit"), QStringLiteral("-F"), messageFile->fileName(),
				QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") }, this,
			[this, messageFile, untrackedPaths, report](const ProcessResult& result) {
				rollBackAddThenReport(path(), this, untrackedPaths, result, report);
			}, Vcs::nulJoined(pathspec));
	};

	if (untrackedPaths.isEmpty())
	{
		runCommit();
		return;
	}
	Git::run(path(), { QStringLiteral("add"), QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") }, this,
		[runCommit, report](const ProcessResult& result) {
			if (result.ok)
				runCommit();
			else
				report(result);
		}, Vcs::nulJoined(untrackedPaths));
}

void GitRepository::commitMergeState(const QString& message, const QStringList& untrackedPaths, Vcs::Answer<void> onDone)
{
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto messageFile = openMessageFile(message, this, report);
	if (!messageFile)
		return;

	const auto runCommit = [this, messageFile, untrackedPaths, report] {
		// No pathspec: the index already holds the merge result, and staging conflicted files marks them resolved
		Git::run(path(), { QStringLiteral("commit"), QStringLiteral("-F"), messageFile->fileName() }, this,
			[this, messageFile, untrackedPaths, report](const ProcessResult& result) {
				// Only the untracked add is rolled back: `add -u` marked conflicted paths resolved, and
				// resetting one of those would drop the resolution the user just made
				rollBackAddThenReport(path(), this, untrackedPaths, result, report);
			});
	};

	const auto addUntracked = [this, untrackedPaths, runCommit, report] {
		if (untrackedPaths.isEmpty())
		{
			runCommit();
			return;
		}
		Git::run(path(), { QStringLiteral("add"), QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") }, this,
			[runCommit, report](const ProcessResult& result) {
				if (result.ok)
					runCommit();
				else
					report(result);
			}, Vcs::nulJoined(untrackedPaths));
	};

	Git::run(path(), { QStringLiteral("add"), QStringLiteral("-u") }, this,
		[addUntracked, report](const ProcessResult& result) {
			if (result.ok)
				addUntracked();
			else
				report(result);
		});
}

Vcs::Job* GitRepository::push(Vcs::Callback onDone)
{
	return Git::run(path(), { QStringLiteral("push"), QStringLiteral("--recurse-submodules=on-demand"),
		QStringLiteral("--progress") }, this, std::move(onDone));
}

Vcs::Job* GitRepository::pushSetUpstream(Vcs::Callback onDone)
{
	return Git::run(path(), { QStringLiteral("push"), QStringLiteral("--recurse-submodules=on-demand"),
		QStringLiteral("--progress"), QStringLiteral("--set-upstream"), QStringLiteral("origin"), QStringLiteral("HEAD") },
		this, std::move(onDone));
}

QString GitRepository::pushCommandLabel(bool setUpstream) const
{
	return setUpstream ? QStringLiteral("git push --set-upstream origin HEAD") : QStringLiteral("git push");
}

void GitRepository::fetch(Vcs::Answer<void> onDone)
{
	// Not a read-only query despite reading from the network: it writes the remote-tracking refs
	Git::run(path(), { QStringLiteral("fetch") }, this, Vcs::reporting(std::move(onDone)));
}

void GitRepository::addToIndex(const QStringList& paths, Vcs::Answer<void> onDone)
{
	Git::run(path(), { QStringLiteral("add"), QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") },
		this, Vcs::reporting(std::move(onDone)), Vcs::nulJoined(paths));
}

void GitRepository::unAdd(const QStringList& paths, Vcs::Answer<void> onDone)
{
	Git::run(path(), { QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") },
		this, Vcs::reporting(std::move(onDone)), Vcs::nulJoined(paths));
}

void GitRepository::discardChanges(const QStringList& pathspec, Vcs::Answer<void> onDone)
{
	Git::run(path(), { QStringLiteral("restore"), QStringLiteral("--source=HEAD"), QStringLiteral("--staged"), QStringLiteral("--worktree"),
		QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") }, this, Vcs::reporting(std::move(onDone)), Vcs::nulJoined(pathspec));
}

void GitRepository::checkoutBranch(const QString& branch, Vcs::Answer<void> onDone)
{
	Git::run(path(), { QStringLiteral("checkout"), branch }, this, Vcs::reporting(std::move(onDone)));
}

void GitRepository::createTrackingBranch(const QString& localName, const QString& remoteBranch, Vcs::Answer<void> onDone)
{
	Git::run(path(), { QStringLiteral("checkout"), QStringLiteral("-b"), localName, QStringLiteral("--track"), remoteBranch },
		this, Vcs::reporting(std::move(onDone)));
}

void GitRepository::localBranchExists(const QString& name, const QObject* context, std::function<void(bool)> onDone)
{
	Git::run(path(), { QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
		QStringLiteral("refs/heads/") + name }, context,
		[onDone = std::move(onDone)](const ProcessResult& result) { onDone(result.ok); }, {}, /*readOnlyQuery=*/true);
}

QString GitRepository::diffBase() const
{
	return state().unborn ? _emptyTreeSha : QStringLiteral("HEAD");
}

Vcs::Query GitRepository::diffFile(const FileEntry& entry, const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	// --ignore-cr-at-eol is display only - the row still lists as modified and commits its working-tree
	// content verbatim
	QStringList args = { QStringLiteral("diff"), QStringLiteral("--ignore-cr-at-eol"), QStringLiteral("-M"),
		diffBase(), QStringLiteral("--"), entry.path };
	if (!entry.oldPath.isEmpty())
		args.push_back(entry.oldPath);
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query GitRepository::diffAllChanges(const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	// --ignore-cr-at-eol, or a wholesale line-ending conversion floods the word pool with every line of the file
	QStringList args = { QStringLiteral("diff"), QStringLiteral("--ignore-cr-at-eol"), QStringLiteral("-U0"),
		QStringLiteral("--ignore-submodules"), diffBase() };
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query GitRepository::commitLog(const LogQuery& query, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone)
{
	QStringList args = commitLogArgs(query.maxCommits);
	appendPickaxe(args, query, /*countChangesOnly=*/false);
	appendPathLimit(args, query);
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), Git::parseCommitLog));
}

Vcs::Query GitRepository::commitsAddingOrRemovingText(const LogQuery& query, const QObject* context, Vcs::Answer<QSet<QString>> onDone)
{
	QStringList args = { QStringLiteral("log"), QStringLiteral("--max-count=%1").arg(query.maxCommits),
		QStringLiteral("--format=%H") };
	appendPickaxe(args, query, /*countChangesOnly=*/true);
	appendPathLimit(args, query);
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), shaSet));
}

Vcs::Query GitRepository::incomingCommits(int maxCommits, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone)
{
	QStringList args = commitLogArgs(maxCommits);
	args << QStringLiteral("HEAD..@{upstream}");
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), Git::parseCommitLog));
}

Vcs::Query GitRepository::commitFiles(const QString& sha, const QObject* context, Vcs::Answer<std::vector<CommitFileChange>> onDone)
{
	return runQuery(path(), commitFilesArgs(sha, QStringLiteral("--name-status")),
		context, Vcs::answering(std::move(onDone), Git::parseNameStatusZ));
}

Vcs::Query GitRepository::commitFileCounts(const QString& sha, const QObject* context, Vcs::Answer<std::map<QString, LineCounts>> onDone)
{
	QStringList args = commitFilesArgs(sha, QStringLiteral("--numstat"));
	args.insert(1, QStringLiteral("--ignore-cr-at-eol")); // as commitFileDiff carries it: a row's counts are the ones its own diff shows
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), Git::parseNumstatZ));
}

Vcs::Query GitRepository::commitFileDiff(const QString& sha, const CommitFileChange& file, const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	QStringList args = { QStringLiteral("show"), QStringLiteral("-M"), QStringLiteral("--ignore-cr-at-eol"),
		QStringLiteral("--format="), sha, QStringLiteral("--"), file.path };
	if (!file.oldPath.isEmpty())
		args.push_back(file.oldPath); // both sides, or the pathspec filters the rename out before -M can pair it up
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query GitRepository::unpushedCommits(const QObject* context, Vcs::Answer<QSet<QString>> onDone)
{
	return runQuery(path(), { QStringLiteral("rev-list"), QStringLiteral("@{upstream}..HEAD") },
		context, Vcs::answering(std::move(onDone), shaSet));
}

Vcs::Query GitRepository::submodulePointerLog(const FileEntry& entry, const QObject* context, Vcs::Answer<QString> onDone)
{
	const QString subPath = path() + QLatin1Char('/') + entry.path;
	Vcs::Query query;
	// A callback only runs while its context lives, so this one can hand the same context to the second query
	query.attach(Git::run(path(), { QStringLiteral("rev-parse"), QStringLiteral("HEAD:") + entry.path }, context,
		[query, subPath, context, onDone = std::move(onDone)](const ProcessResult& revResult) mutable {
			if (!revResult.ok)
			{
				onDone(std::unexpected(revResult.errorText()));
				return;
			}
			const QString oldSha = QString::fromUtf8(revResult.out.trimmed());
			query.attach(Git::run(subPath, { QStringLiteral("log"), QStringLiteral("--oneline"), QStringLiteral("--no-decorate"),
				oldSha + QStringLiteral("..HEAD") }, context, Vcs::answering(std::move(onDone), outputAsText), {}, /*readOnlyQuery=*/true));
		}, {}, /*readOnlyQuery=*/true));
	return query;
}

RepositoryLocation GitRepository::submoduleLocation(const FileEntry& entry) const
{
	return { VcsKind::Git, path() + QLatin1Char('/') + entry.path }; // a git submodule is a git repository
}

QString GitRepository::ignoreFileName() const
{
	return QStringLiteral(".gitignore");
}

// Literal path patterns are anchored to the repo root with a leading '/'
std::vector<IgnorePattern> GitRepository::ignorePatternsFor(const QString& repoRelativePath) const
{
	const qsizetype slash = repoRelativePath.lastIndexOf(QLatin1Char('/'));
	const QString fileName = repoRelativePath.mid(slash + 1);
	const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));

	std::vector<IgnorePattern> patterns;
	patterns.push_back({ QLatin1Char('/') + escapedForGitIgnore(repoRelativePath), IgnoreScope::ExactPath });
	if (dot > 0 && dot < fileName.size() - 1)
		patterns.push_back({ QStringLiteral("*.") + escapedForGitIgnore(fileName.mid(dot + 1)), IgnoreScope::Extension });
	patterns.push_back({ escapedForGitIgnore(fileName), IgnoreScope::Name });
	if (slash >= 0)
		patterns.push_back({ QLatin1Char('/') + escapedForGitIgnore(repoRelativePath.left(slash)) + QLatin1Char('/'), IgnoreScope::Directory });
	return patterns;
}

// Every pattern is legal on any line of a .gitignore, so the scope decides nothing about where it goes
QByteArray GitRepository::ignoreFileWithPatternAdded(QByteArray content, const IgnorePattern& pattern) const
{
	const QByteArray eol = content.contains("\r\n") ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");
	if (!content.isEmpty() && !content.endsWith('\n'))
		content += eol;
	return content + pattern.text.toUtf8() + eol;
}

void GitRepository::launchExternalDiffTool(const QString& repoRelativePath) const
{
	QProcess::startDetached(Settings::gitExecutable(),
		{ QStringLiteral("difftool"), QStringLiteral("-y"), QStringLiteral("HEAD"), QStringLiteral("--"), repoRelativePath },
		path());
}
