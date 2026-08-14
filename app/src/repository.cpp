#include "repository.h"

#include <QDir>
#include <QFileInfo>
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
			// Not launchFailed: that flag would make errorText() report a missing git installation instead
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

} // namespace

struct Repository::RefreshRun
{
	int pendingJobs = 0;

	BranchHeader header;
	std::vector<NameStatusEntry> diffEntries;
	QStringList untracked;
	QStringList submodules; // repo-relative paths of the gitlink entries
	std::map<QString, SubmoduleContent> submoduleContent; // only the submodules that were queried at all
	QStringList unbornCachedFiles;
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

	// Launches one query; onResult fills its slice of RefreshRun. When the phase drains, whenDrained is called
	// (each phase passes the next one).
	const auto launch = [this, run](const QString& workDir, QStringList args, auto onResult, auto whenDrained) {
		++run->pendingJobs;
		Git::run(workDir, std::move(args), this, [run, onResult, whenDrained](const GitResult& result) {
			onResult(result);
			if (--run->pendingJobs == 0)
				whenDrained();
		}, {}, /*readOnlyQuery=*/true);
	};

	const auto phase3 = [this] { finishRefresh(); };

	// Phase 2: queries that depend on phase 1 results - unborn fallback, detached-HEAD branch tips,
	// per-submodule dirtiness. All independent of each other.
	const auto phase2 = [this, run, launch, phase3] {
		if (run->header.oid == QLatin1String("(initial)"))
		{
			launch(_rootPath, { QStringLiteral("ls-files"), QStringLiteral("--cached"), QStringLiteral("-z") },
				[run](const GitResult& r) { run->unbornCachedFiles = parseZList(r.out); }, phase3);
		}

		if (run->header.head == QLatin1String("(detached)"))
		{
			for (const char* refRoot : { "refs/heads", "refs/remotes" })
			{
				const bool local = qstrcmp(refRoot, "refs/heads") == 0;
				launch(_rootPath, { QStringLiteral("for-each-ref"), QStringLiteral("--points-at"), QStringLiteral("HEAD"),
						QStringLiteral("--format=%(refname:short)"), QString::fromLatin1(refRoot) },
					[this, run, local](const GitResult& r) {
						QStringList names = parseLineList(r.out);
						if (!local)
							names.removeIf([](const QString& n) { return n.endsWith(QLatin1String("/HEAD")); });
						(local ? _state.localBranchesAtHead : _state.remoteBranchesAtHead) = names;
					}, phase3);
			}
		}

		for (const QString& subPath : run->submodules)
		{
			const QString workDir = _rootPath + QLatin1Char('/') + subPath;
			if (!QFileInfo::exists(workDir + QLatin1String("/.git")))
				continue; // never initialized: the directory is empty, there is nothing inside to query
			launch(workDir, { QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("-z") },
				[run, subPath](const GitResult& r) { run->submoduleContent[subPath] = contentFromStatus(r); }, phase3);
		}

		if (!run->header.upstream.isEmpty() && run->header.ahead > 0)
		{
			launch(_rootPath, { QStringLiteral("log"), QStringLiteral("--oneline"), QStringLiteral("--no-decorate"),
					QStringLiteral("-n"), QString::number(MaxUnpushedLogEntries), QStringLiteral("@{upstream}..HEAD") },
				[this](const GitResult& r) { _state.unpushedSubjects = parseLineList(r.out); }, phase3);
		}

		if (run->pendingJobs == 0)
			phase3();
	};

	// Refilled by phase 2 when still applicable
	_state.localBranchesAtHead.clear();
	_state.remoteBranchesAtHead.clear();
	_state.unpushedSubjects.clear();

	// Phase 1: the four independent base queries, plus one-time gitdir resolution
	if (_gitDir.isEmpty())
	{
		launch(_rootPath, { QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir") },
			[this](const GitResult& r) { _gitDir = QString::fromUtf8(r.out.trimmed()); }, phase2);
	}
	// Only the branch header is parsed out of this, so the flags keep git from scanning the working tree and
	// recursing into submodules to produce entries nobody reads
	launch(_rootPath, { QStringLiteral("status"), QStringLiteral("--porcelain=v2"), QStringLiteral("--branch"),
			QStringLiteral("--untracked-files=no"), QStringLiteral("--ignore-submodules=all"), QStringLiteral("-z") },
		[run](const GitResult& r) { run->header = parseBranchHeader(r.out); }, phase2);
	launch(_rootPath, { QStringLiteral("diff"), QStringLiteral("--name-status"), QStringLiteral("-M"), QStringLiteral("--ignore-submodules=dirty"), QStringLiteral("-z"), QStringLiteral("HEAD") },
		[run](const GitResult& r) { if (r.ok) run->diffEntries = parseNameStatusZ(r.out); }, phase2); // fails on unborn HEAD - phase 2 covers that
	launch(_rootPath, { QStringLiteral("ls-files"), QStringLiteral("--others"), QStringLiteral("--exclude-standard"), QStringLiteral("-z") },
		[run](const GitResult& r) { run->untracked = parseZList(r.out); }, phase2);
	// The submodule list is read out of the index, not from `git submodule status`: that one is a shell script in
	// Git for Windows and costs more than every other refresh query combined
	launch(_rootPath, { QStringLiteral("ls-files"), QStringLiteral("--stage"), QStringLiteral("-z") },
		[run](const GitResult& r) { run->submodules = parseGitlinkPaths(r.out); }, phase2);
}

void Repository::finishRefresh()
{
	const RefreshRun& run = *_run;

	_state.unborn = run.header.oid == QLatin1String("(initial)");
	_state.headSha = _state.unborn ? QString{} : run.header.oid;
	_state.detached = run.header.head == QLatin1String("(detached)");
	_state.branch = _state.detached ? QString{} : run.header.head;
	_state.upstream = run.header.upstream;
	_state.ahead = run.header.ahead;
	_state.behind = run.header.behind;

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

	// A submodule phase 2 never queried was never initialized, and an empty directory holds nothing
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
		_files.push_back(std::move(entry));
	}

	// Unborn HEAD: everything staged shows as Added
	for (const QString& path : run.unbornCachedFiles)
		_files.push_back({ .path = path, .type = ChangeType::Added });

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

	_run.reset();
	_refreshing = false;
	emit refreshed();

	if (_refreshPending)
	{
		_refreshPending = false;
		refresh();
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
		[onDone = std::move(onDone)](const GitResult& result) { onDone(result.exitCode == 0); }, {}, /*readOnlyQuery=*/true);
}

Git::Job* Repository::diffFile(const FileEntry& entry, const QObject* context, Git::Callback onDone)
{
	QStringList args;
	if (entry.type == ChangeType::Untracked)
	{
		// No HEAD side exists; --no-index against the null device exits 1 when there is a diff - that is success here
		args = { QStringLiteral("diff"), QStringLiteral("--no-index"), QStringLiteral("--"), QStringLiteral("/dev/null"), entry.path };
	}
	else if (_state.unborn)
	{
		args = { QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("-M"), QStringLiteral("--"), entry.path };
	}
	else
	{
		args = { QStringLiteral("diff"), QStringLiteral("-M"), QStringLiteral("HEAD"), QStringLiteral("--"), entry.path };
		if (!entry.oldPath.isEmpty())
			args.push_back(entry.oldPath);
	}
	// Display only - the row still lists as modified and commits its working-tree content verbatim
	args.insert(1, QStringLiteral("--ignore-cr-at-eol"));
	return Git::run(_rootPath, std::move(args), context, std::move(onDone), {}, /*readOnlyQuery=*/true);
}

Git::Job* Repository::diffAllChanges(const QObject* context, Git::Callback onDone)
{
	QStringList args = _state.unborn
		? QStringList{ QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("-U0") }
		: QStringList{ QStringLiteral("diff"), QStringLiteral("-U0"), QStringLiteral("--ignore-submodules"), QStringLiteral("HEAD") };
	// Or a wholesale line-ending conversion floods the word pool with every line of the file
	args.insert(1, QStringLiteral("--ignore-cr-at-eol"));
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
	return Git::run(_rootPath, { QStringLiteral("show"), QStringLiteral("--name-status"), QStringLiteral("-M"),
		QStringLiteral("-z"), QStringLiteral("--format="), sha },
		context, std::move(onDone), {}, /*readOnlyQuery=*/true);
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
	Git::run(_rootPath, { QStringLiteral("rev-parse"), QStringLiteral("HEAD:") + entry.path }, context,
		[subPath, context = QPointer<const QObject>(context), onDone](const GitResult& revResult) {
			if (!context)
				return; // the context died; a null context pointer would mean "no context" to Git::run
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
