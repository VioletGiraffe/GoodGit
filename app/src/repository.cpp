#include "repository.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryFile>

#include <algorithm>
#include <map>
#include <memory>

namespace {

constexpr int MaxUnpushedLogEntries = 30; // tooltip fodder; state.ahead carries the true count

QByteArray nulJoined(const QStringList& paths)
{
	QByteArray data;
	for (const QString& path : paths)
	{
		data += path.toUtf8();
		data += '\0';
	}
	return data;
}

// The commit message travels in a temp file - `-F -` and a stdin pathspec cannot share the pipe.
// Null if the file cannot be created, the failure already on its way to onDone: like every Git::run
// callback, it has to reach the caller from the event loop rather than from inside this call.
std::shared_ptr<QTemporaryFile> openMessageFile(const QString& message, QObject* context, const Git::Callback& onDone)
{
	auto file = std::make_shared<QTemporaryFile>();
	if (!file->open())
	{
		QMetaObject::invokeMethod(context, [onDone] {
			// The default outcome is Exited, which is what makes errorText() report this err rather than
			// a process failure the app never had
			onDone(GitResult{ .err = "Failed to create the commit message temp file" });
		}, Qt::QueuedConnection);
		return nullptr;
	}

	file->write(message.toUtf8());
	file->close(); // release the handle for git; the file is removed when the last owner drops it
	return file;
}

// A commit that failed after its untracked paths were added leaves them staged, and the rows would then
// read Added rather than Untracked. The reported result stays the commit's: the reset's says nothing about
// why the commit was refused.
void rollBackAddThenReport(const QString& workDir, const QObject* context, const QStringList& untrackedPaths,
	const GitResult& commitResult, const Git::Callback& onDone)
{
	if (commitResult.ok || untrackedPaths.isEmpty())
	{
		onDone(commitResult);
		return;
	}

	Git::run(workDir, { QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("--pathspec-from-file=-"),
		QStringLiteral("--pathspec-file-nul") }, context,
		[onDone, commitResult](const GitResult&) { onDone(commitResult); }, nulJoined(untrackedPaths));
}

// A status that could not be run answers nothing about the worktree it was pointed at, and the parent
// may not act on the pointer without that answer - so it is the dirty case, not the clean one.
SubmoduleContent contentFromStatus(const GitResult& statusResult)
{
	if (!statusResult.ok)
		return SubmoduleContent::Unknown;

	const WorktreeDirtiness dirtiness = parsePorcelainDirtiness(statusResult.out);
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
		QStringLiteral("--format=") + QLatin1String(CommitLogFormat) };
}

// -G takes an extended regular expression and has no fixed-string mode - --fixed-strings does not reach
// it - so a literal search term has to arrive pre-escaped, or `foo(` aborts the whole query. The set is
// ERE's exactly: escaping beyond it would turn literals into operators under other flavours.
QString escapedForExtendedRegex(const QString& text)
{
	static const QString metacharacters = QStringLiteral(".^$*+?()[]{}|\\");

	QString escaped;
	escaped.reserve(text.size() * 2);
	for (const QChar c : text)
	{
		if (metacharacters.contains(c))
			escaped += QLatin1Char('\\');
		escaped += c;
	}
	return escaped;
}

// -S counts occurrences and takes the term literally; -G matches patch lines and takes a regex
void appendPickaxe(QStringList& args, const Repository::LogQuery& query, bool countChangesOnly)
{
	if (query.pickaxe.isEmpty())
		return;

	args << (countChangesOnly ? QStringLiteral("-S") + query.pickaxe
		: QStringLiteral("-G") + escapedForExtendedRegex(query.pickaxe));
	if (query.ignoreCase)
		args << QStringLiteral("-i");
}

// The pathspec closes the argument list, so everything else has to be in place first
void appendPathLimit(QStringList& args, const Repository::LogQuery& query)
{
	if (!query.path.isEmpty())
		args << QStringLiteral("--follow") << QStringLiteral("--") << query.path; // --follow takes the one path we ever pass
}

// A set of queries launched together, and what happens once the last of them has answered. Every query
// holds a reference to the round's end, and the launching scope holds one of its own - so a round that
// launched nothing ends as that scope does, and one that launched anything ends from the final callback.
// A skipped callback is still a destroyed one, so `then` also runs when the context dies mid-round and
// has to check for that.
class QueryRound
{
	struct End
	{
		explicit End(std::function<void()> then) : then(std::move(then)) {}
		~End() { then(); }

		std::function<void()> then;
	};

public:
	QueryRound(const QObject* context, std::function<void()> then) :
		_context(context),
		_end(std::make_shared<End>(std::move(then)))
	{}

	void launch(const QString& workDir, QStringList args, Git::Callback onResult)
	{
		Git::run(workDir, std::move(args), _context,
			[end = _end, onResult = std::move(onResult)](const GitResult& result) mutable {
				onResult(result);
				end.reset(); // a delivered result is done with the round here, not at the job's later deletion
			}, {}, /*readOnlyQuery=*/true);
	}

private:
	const QObject* const _context;
	const std::shared_ptr<End> _end;
};

} // namespace

struct Repository::RefreshRun
{
	BranchHeader header;
	std::vector<NameStatusEntry> diffEntries;
	std::map<QString, LineCounts> changeCounts; // by path; only the tracked changes have one
	QStringList untracked;
	QStringList submodules; // repo-relative paths of the gitlink entries
	std::map<QString, SubmoduleContent> submoduleContent; // only the submodules that were queried at all
	QStringList localBranchesAtHead;
	QStringList remoteBranchesAtHead;
	QStringList unpushedSubjects;

	// Why this run cannot become the state. The tracked-changes query keeps its own slot: on an unborn HEAD
	// its attempt against HEAD is meant to fail, and the re-run against the empty tree answers in its place.
	QString failure;
	QString trackedError;

	void noteFailure(const GitResult& result)
	{
		if (failure.isEmpty())
			failure = result.errorText();
	}
};

Repository::Repository(QString rootPath, QObject* parent) :
	QObject(parent),
	_rootPath{ std::move(rootPath) }
{
}

QString Repository::name() const
{
	return QFileInfo{ _rootPath }.fileName();
}

void Repository::refresh()
{
	if (_refreshing)
	{
		_refreshPending = true;
		return;
	}

	_refreshing = true;
	auto run = std::make_shared<RefreshRun>();
	_run = run;

	// The four independent base queries, plus the one-time per-repository resolutions. Whatever their
	// answers call for is asked once they are all in.
	QueryRound round{ this, [self = QPointer<Repository>{ this }, run] {
		if (self)
			self->startDependentQueries(run);
	} };

	if (_gitDir.isEmpty())
	{
		round.launch(_rootPath, { QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir") },
			[this, run](const GitResult& r) {
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
		round.launch(_rootPath, { QStringLiteral("hash-object"), QStringLiteral("-t"), QStringLiteral("tree"), QStringLiteral("--stdin") },
			[this](const GitResult& r) { _emptyTreeSha = QString::fromUtf8(r.out.trimmed()); });
	}
	// Only the branch header is parsed out of this, so the flags keep git from scanning the working tree and
	// recursing into submodules to produce entries nobody reads
	round.launch(_rootPath, { QStringLiteral("status"), QStringLiteral("--porcelain=v2"), QStringLiteral("--branch"),
			QStringLiteral("--untracked-files=no"), QStringLiteral("--ignore-submodules=all"), QStringLiteral("-z") },
		[run](const GitResult& r) {
			if (r.ok)
				run->header = parseBranchHeader(r.out);
			else
				run->noteFailure(r); // an unread header parses as "on a branch, born", the two things nothing may assume
		});
	round.launch(_rootPath, trackedChangesArgs(QStringLiteral("HEAD")),
		[run](const GitResult& r) {
			if (r.ok)
				run->diffEntries = parseNameStatusZ(r.out);
			else
				run->trackedError = r.errorText(); // an unborn HEAD fails here by design; the empty-tree re-run asks again
		});
	// Losing this costs the rows their counts, which is a shorter answer rather than a wrong one
	round.launch(_rootPath, trackedChangeCountsArgs(QStringLiteral("HEAD")),
		[run](const GitResult& r) { run->changeCounts = parseNumstatZ(r.out); });
	// Losing these costs untracked rows, which is a shorter list rather than a wrong one
	round.launch(_rootPath, { QStringLiteral("ls-files"), QStringLiteral("--others"), QStringLiteral("--exclude-standard"), QStringLiteral("-z") },
		[run](const GitResult& r) { run->untracked = parseZList(r.out); });
	// The submodule list is read out of the index, not from `git submodule status`: that one is a shell script in
	// Git for Windows and costs more than every other refresh query combined
	round.launch(_rootPath, { QStringLiteral("ls-files"), QStringLiteral("--stage"), QStringLiteral("-z") },
		[run](const GitResult& r) {
			if (r.ok)
				run->submodules = parseGitlinkPaths(r.out);
			else
				run->noteFailure(r); // unread, every gitlink demotes to an ordinary file that discarding may overwrite
		});
}

// The queries the base ones' answers call for - the unborn fallback, the detached-HEAD branch tips,
// per-submodule dirtiness, the unpushed subjects. All independent of each other.
void Repository::startDependentQueries(const std::shared_ptr<RefreshRun>& run)
{
	QueryRound round{ this, [self = QPointer<Repository>{ this }] {
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
			round.launch(_rootPath, trackedChangesArgs(_emptyTreeSha),
				[run](const GitResult& r) {
					if (!r.ok)
					{
						run->trackedError = r.errorText();
						return;
					}
					run->diffEntries = parseNameStatusZ(r.out);
					run->trackedError.clear(); // the earlier attempt only failed for want of a HEAD, and this answered instead
				});
			round.launch(_rootPath, trackedChangeCountsArgs(_emptyTreeSha),
				[run](const GitResult& r) { run->changeCounts = parseNumstatZ(r.out); });
		}
	}

	if (run->header.head == QLatin1String("(detached)"))
	{
		for (const char* refRoot : { "refs/heads", "refs/remotes" })
		{
			const bool local = qstrcmp(refRoot, "refs/heads") == 0;
			round.launch(_rootPath, { QStringLiteral("for-each-ref"), QStringLiteral("--points-at"), QStringLiteral("HEAD"),
					QStringLiteral("--format=%(refname:short)"), QString::fromLatin1(refRoot) },
				[run, local](const GitResult& r) {
					QStringList names = parseLineList(r.out);
					if (!local)
						names.removeIf([](const QString& n) { return n.endsWith(QLatin1String("/HEAD")); });
					(local ? run->localBranchesAtHead : run->remoteBranchesAtHead) = names;
				});
		}
	}

	for (const QString& subPath : run->submodules)
	{
		const QString workDir = _rootPath + QLatin1Char('/') + subPath;
		if (!QFileInfo::exists(workDir + QLatin1String("/.git")))
			continue; // never initialized: the directory is empty, there is nothing inside to query
		round.launch(workDir, { QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("-z") },
			[run, subPath](const GitResult& r) { run->submoduleContent[subPath] = contentFromStatus(r); });
	}

	if (!run->header.upstream.isEmpty() && run->header.ahead > 0)
	{
		round.launch(_rootPath, { QStringLiteral("log"), QStringLiteral("--oneline"), QStringLiteral("--no-decorate"),
				QStringLiteral("-n"), QString::number(MaxUnpushedLogEntries), QStringLiteral("@{upstream}..HEAD") },
			[run](const GitResult& r) { run->unpushedSubjects = parseLineList(r.out); });
	}
}

void Repository::finishRefresh()
{
	const RefreshRun& run = *_run;

	// Only the tracked-changes slot can still hold a failure the second round was the one to answer
	_state.readFailure = !run.failure.isEmpty() ? run.failure : run.trackedError;

	// A half-read repository is not written over the last fully-read one: rows assembled from some of the
	// answers would look exactly as complete as rows assembled from all of them
	if (_state.known())
		applyRefreshResults(run);

	_run.reset();
	_refreshing = false;
	emit refreshed();

	if (_refreshPending)
	{
		_refreshPending = false;
		refresh();
	}
}

void Repository::applyRefreshResults(const RefreshRun& run)
{
	_state.unborn = run.header.oid == QLatin1String("(initial)");
	_state.headSha = _state.unborn ? QString{} : run.header.oid;
	_state.detached = run.header.head == QLatin1String("(detached)");
	_state.branch = _state.detached ? QString{} : run.header.head;
	_state.upstream = run.header.upstream;
	_state.ahead = run.header.ahead;
	_state.behind = run.header.behind;
	_state.localBranchesAtHead = run.localBranchesAtHead;
	_state.remoteBranchesAtHead = run.remoteBranchesAtHead;
	_state.unpushedSubjects = run.unpushedSubjects;

	_state.op = RepoOp::None;
	if (!_gitDir.isEmpty())
	{
		const QDir gitDir{ _gitDir };
		if (gitDir.exists(QStringLiteral("MERGE_HEAD")))
			_state.op = RepoOp::Merge;
		else if (gitDir.exists(QStringLiteral("CHERRY_PICK_HEAD")))
			_state.op = RepoOp::CherryPick;
		else if (gitDir.exists(QStringLiteral("REVERT_HEAD")))
			_state.op = RepoOp::Revert;
		else if (gitDir.exists(QStringLiteral("rebase-merge")) || gitDir.exists(QStringLiteral("rebase-apply")))
			_state.op = RepoOp::Rebase;
	}

	_files.clear();

	// A submodule the second round never queried was never initialized, and an empty directory holds nothing
	const auto contentOf = [&run](const QString& path) {
		const auto it = run.submoduleContent.find(path);
		return it != run.submoduleContent.end() ? it->second : SubmoduleContent::Clean;
	};

	// Tracked changes; a diff row whose path is a submodule becomes a moved-pointer submodule row
	for (const NameStatusEntry& diffEntry : run.diffEntries)
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
		else if (const auto it = run.changeCounts.find(diffEntry.path); it != run.changeCounts.end())
			entry.lineCounts = it->second;
		_files.push_back(std::move(entry));
	}

	for (const QString& path : run.untracked)
		_files.push_back({ .path = path, .type = ChangeType::Untracked });

	// Submodules whose pointer has not moved but whose content would stop it moving: shown so the state
	// is visible and the row double-clickable. Untracked-only content inside does not earn a row.
	for (const QString& subPath : run.submodules)
	{
		FileEntry entry{ .path = subPath, .isSubmodule = true, .content = contentOf(subPath) };
		if (!entry.contentBlocksPointer())
			continue;
		if (std::ranges::any_of(_files, [&](const FileEntry& f) { return f.isSubmodule && f.path == subPath; }))
			continue; // already present as a moved-pointer row
		_files.push_back(std::move(entry));
	}
}

void Repository::commit(const QString& message, const QStringList& pathspec, const QStringList& untrackedPaths, Git::Callback onDone)
{
	const auto messageFile = openMessageFile(message, this, onDone);
	if (!messageFile)
		return;

	const auto runCommit = [this, messageFile, pathspec, untrackedPaths, onDone] {
		Git::run(_rootPath, { QStringLiteral("commit"), QStringLiteral("-F"), messageFile->fileName(),
				QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") }, this,
			[this, messageFile, untrackedPaths, onDone](const GitResult& result) {
				rollBackAddThenReport(_rootPath, this, untrackedPaths, result, onDone);
			}, nulJoined(pathspec));
	};

	if (untrackedPaths.isEmpty())
	{
		runCommit();
		return;
	}
	Git::run(_rootPath, { QStringLiteral("add"), QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") }, this,
		[runCommit, onDone](const GitResult& result) {
			if (result.ok)
				runCommit();
			else
				onDone(result);
		}, nulJoined(untrackedPaths));
}

void Repository::commitMergeState(const QString& message, const QStringList& untrackedPaths, Git::Callback onDone)
{
	const auto messageFile = openMessageFile(message, this, onDone);
	if (!messageFile)
		return;

	const auto runCommit = [this, messageFile, untrackedPaths, onDone] {
		// No pathspec: the index already holds the merge result, and staging conflicted files marks them resolved
		Git::run(_rootPath, { QStringLiteral("commit"), QStringLiteral("-F"), messageFile->fileName() }, this,
			[this, messageFile, untrackedPaths, onDone](const GitResult& result) {
				// Only the untracked add is rolled back: `add -u` marked conflicted paths resolved, and
				// resetting one of those would drop the resolution the user just made
				rollBackAddThenReport(_rootPath, this, untrackedPaths, result, onDone);
			});
	};

	const auto addUntracked = [this, untrackedPaths, runCommit, onDone] {
		if (untrackedPaths.isEmpty())
		{
			runCommit();
			return;
		}
		Git::run(_rootPath, { QStringLiteral("add"), QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") }, this,
			[runCommit, onDone](const GitResult& result) {
				if (result.ok)
					runCommit();
				else
					onDone(result);
			}, nulJoined(untrackedPaths));
	};

	Git::run(_rootPath, { QStringLiteral("add"), QStringLiteral("-u") }, this,
		[addUntracked, onDone](const GitResult& result) {
			if (result.ok)
				addUntracked();
			else
				onDone(result);
		});
}

Git::Job* Repository::push(Git::Callback onDone)
{
	return Git::run(_rootPath, { QStringLiteral("push"), QStringLiteral("--recurse-submodules=on-demand"),
		QStringLiteral("--progress") }, this, std::move(onDone));
}

Git::Job* Repository::pushSetUpstream(Git::Callback onDone)
{
	return Git::run(_rootPath, { QStringLiteral("push"), QStringLiteral("--recurse-submodules=on-demand"),
		QStringLiteral("--progress"), QStringLiteral("--set-upstream"), QStringLiteral("origin"), QStringLiteral("HEAD") },
		this, std::move(onDone));
}

void Repository::fetch(Git::Callback onDone)
{
	// Not a read-only query despite reading from the network: it writes the remote-tracking refs
	Git::run(_rootPath, { QStringLiteral("fetch") }, this, std::move(onDone));
}

void Repository::addToIndex(const QStringList& paths, Git::Callback onDone)
{
	Git::run(_rootPath, { QStringLiteral("add"), QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") },
		this, std::move(onDone), nulJoined(paths));
}

void Repository::unAdd(const QStringList& paths, Git::Callback onDone)
{
	Git::run(_rootPath, { QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") },
		this, std::move(onDone), nulJoined(paths));
}

void Repository::discardChanges(const QStringList& pathspec, Git::Callback onDone)
{
	Git::run(_rootPath, { QStringLiteral("restore"), QStringLiteral("--source=HEAD"), QStringLiteral("--staged"), QStringLiteral("--worktree"),
		QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") }, this, std::move(onDone), nulJoined(pathspec));
}

void Repository::checkoutBranch(const QString& branch, Git::Callback onDone)
{
	Git::run(_rootPath, { QStringLiteral("checkout"), branch }, this, std::move(onDone));
}

void Repository::createTrackingBranch(const QString& localName, const QString& remoteBranch, Git::Callback onDone)
{
	Git::run(_rootPath, { QStringLiteral("checkout"), QStringLiteral("-b"), localName, QStringLiteral("--track"), remoteBranch },
		this, std::move(onDone));
}

void Repository::localBranchExists(const QString& name, const QObject* context, std::function<void(bool)> onDone)
{
	Git::run(_rootPath, { QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
		QStringLiteral("refs/heads/") + name }, context,
		[onDone = std::move(onDone)](const GitResult& result) { onDone(result.ok); }, {}, /*readOnlyQuery=*/true);
}

QString Repository::diffBase() const
{
	return _state.unborn ? _emptyTreeSha : QStringLiteral("HEAD");
}

Git::Job* Repository::diffFile(const FileEntry& entry, const QObject* context, Git::Callback onDone)
{
	QStringList args;
	if (entry.type == ChangeType::Untracked)
	{
		// The file is in neither the base nor the index, so there is no pair for git to diff - the null
		// device supplies the missing side
		args = { QStringLiteral("diff"), QStringLiteral("--no-index"), QStringLiteral("--"), QStringLiteral("/dev/null"), entry.path };
		onDone = [onDone = std::move(onDone)](const GitResult& result) {
			GitResult corrected = result;
			corrected.ok = result.outcome == GitOutcome::Exited && result.exitCode <= 1; // --no-index exits 1 on a difference, which is the point
			onDone(corrected);
		};
	}
	else
	{
		args = { QStringLiteral("diff"), QStringLiteral("-M"), diffBase(), QStringLiteral("--"), entry.path };
		if (!entry.oldPath.isEmpty())
			args.push_back(entry.oldPath);
	}
	// Display only - the row still lists as modified and commits its working-tree content verbatim
	args.insert(1, QStringLiteral("--ignore-cr-at-eol"));
	return Git::run(_rootPath, std::move(args), context, std::move(onDone), {}, /*readOnlyQuery=*/true);
}

Git::Job* Repository::diffAllChanges(const QObject* context, Git::Callback onDone)
{
	// --ignore-cr-at-eol, or a wholesale line-ending conversion floods the word pool with every line of the file
	QStringList args = { QStringLiteral("diff"), QStringLiteral("--ignore-cr-at-eol"), QStringLiteral("-U0"),
		QStringLiteral("--ignore-submodules"), diffBase() };
	return Git::run(_rootPath, std::move(args), context, std::move(onDone), {}, /*readOnlyQuery=*/true);
}

Git::Job* Repository::commitLog(const LogQuery& query, const QObject* context, Git::Callback onDone)
{
	QStringList args = commitLogArgs(query.maxCommits);
	appendPickaxe(args, query, /*countChangesOnly=*/false);
	appendPathLimit(args, query);
	return Git::run(_rootPath, std::move(args), context, std::move(onDone), {}, /*readOnlyQuery=*/true);
}

Git::Job* Repository::commitsAddingOrRemovingText(const LogQuery& query, const QObject* context, Git::Callback onDone)
{
	QStringList args = { QStringLiteral("log"), QStringLiteral("--max-count=%1").arg(query.maxCommits),
		QStringLiteral("--format=%H") };
	appendPickaxe(args, query, /*countChangesOnly=*/true);
	appendPathLimit(args, query);
	return Git::run(_rootPath, std::move(args), context, std::move(onDone), {}, /*readOnlyQuery=*/true);
}

Git::Job* Repository::incomingCommits(int maxCommits, const QObject* context, Git::Callback onDone)
{
	QStringList args = commitLogArgs(maxCommits);
	args << QStringLiteral("HEAD..@{upstream}");
	return Git::run(_rootPath, std::move(args), context, std::move(onDone), {}, /*readOnlyQuery=*/true);
}

Git::Job* Repository::commitFiles(const QString& sha, const QObject* context, Git::Callback onDone)
{
	return Git::run(_rootPath, commitFilesArgs(sha, QStringLiteral("--name-status")),
		context, std::move(onDone), {}, /*readOnlyQuery=*/true);
}

Git::Job* Repository::commitFileCounts(const QString& sha, const QObject* context, Git::Callback onDone)
{
	QStringList args = commitFilesArgs(sha, QStringLiteral("--numstat"));
	args.insert(1, QStringLiteral("--ignore-cr-at-eol")); // as commitFileDiff carries it: a row's counts are the ones its own diff shows
	return Git::run(_rootPath, std::move(args), context, std::move(onDone), {}, /*readOnlyQuery=*/true);
}

Git::Job* Repository::commitFileDiff(const QString& sha, const NameStatusEntry& file, const QObject* context, Git::Callback onDone)
{
	QStringList args = { QStringLiteral("show"), QStringLiteral("-M"), QStringLiteral("--ignore-cr-at-eol"),
		QStringLiteral("--format="), sha, QStringLiteral("--"), file.path };
	if (!file.oldPath.isEmpty())
		args.push_back(file.oldPath); // both sides, or the pathspec filters the rename out before -M can pair it up
	return Git::run(_rootPath, std::move(args), context, std::move(onDone), {}, /*readOnlyQuery=*/true);
}

Git::Job* Repository::unpushedCommits(const QObject* context, Git::Callback onDone)
{
	return Git::run(_rootPath, { QStringLiteral("rev-list"), QStringLiteral("@{upstream}..HEAD") },
		context, std::move(onDone), {}, /*readOnlyQuery=*/true);
}

void Repository::submodulePointerLog(const FileEntry& entry, const QObject* context, Git::Callback onDone)
{
	const QString subPath = _rootPath + QLatin1Char('/') + entry.path;
	// A callback only runs while its context lives, so this one can hand the same context to the second query
	Git::run(_rootPath, { QStringLiteral("rev-parse"), QStringLiteral("HEAD:") + entry.path }, context,
		[subPath, context, onDone](const GitResult& revResult) {
			if (!revResult.ok)
			{
				onDone(revResult);
				return;
			}
			const QString oldSha = QString::fromUtf8(revResult.out.trimmed());
			Git::run(subPath, { QStringLiteral("log"), QStringLiteral("--oneline"), QStringLiteral("--no-decorate"),
				oldSha + QStringLiteral("..HEAD") }, context, onDone, {}, /*readOnlyQuery=*/true);
		}, {}, /*readOnlyQuery=*/true);
}
