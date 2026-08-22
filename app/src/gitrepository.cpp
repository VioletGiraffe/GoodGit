#include "gitrepository.h"
#include "gitparsers.h"
#include "gitprocess.h"
#include "queryround.h"
#include "settings.h"

#include "settings/csettings.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QTemporaryFile>

#include <algorithm>
#include <map>

namespace {

constexpr int MaxUnpushedLogEntries = 30; // for the tooltip; state.ahead carries the true count

// In a temp file: the message is the one argument with no bound on its length
std::shared_ptr<QTemporaryFile> openMessageFile(const QString& message, QObject* context, const Vcs::Callback& onDone)
{
	return Vcs::openTempFile(message.toUtf8(), QStringLiteral("commit message"), context, onDone);
}

// A failed commit would leave the untracked paths staged, and their rows would read Added instead of
// Untracked. The commit's result is what gets reported; the reset's says nothing about why the commit failed.
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

// The index manipulation a commit of `pathspec` needs: `git commit` takes the whole index.
// `resetPaths`: staged entries the commit must not carry.
// `addPaths`: `pathspec` minus the paths the index already records as deleted - `git add` fails on a path
// that is in neither the working tree nor the index.
// `savedModes`: `update-index --index-info` records for cleared entries whose mode re-adding from the working
// tree would lose (with core.filemode off, only the index records a mode).
// Entries only added or deleted in the index are not saved: their content is on disk either way.
struct CommitIndexPlan
{
	QStringList resetPaths;
	QStringList addPaths;
	QByteArray savedModes;
};

CommitIndexPlan plannedIndexChange(const std::vector<Git::StagedEntry>& staged, const QStringList& pathspec)
{
	const QSet<QString> committed(pathspec.begin(), pathspec.end());

	CommitIndexPlan plan;
	plan.addPaths = pathspec;

	QSet<QString> stagedDeletions;
	for (const Git::StagedEntry& entry : staged)
	{
		if (entry.indexMode == "000000")
			stagedDeletions.insert(entry.path);

		if (committed.contains(entry.path))
			continue;

		plan.resetPaths << entry.path;
		if (entry.treeMode != entry.indexMode && entry.treeMode != "000000" && entry.indexMode != "000000")
			plan.savedModes += entry.indexMode + ' ' + entry.indexSha + '\t' + entry.path.toUtf8() + '\0';
	}

	plan.addPaths.removeIf([&stagedDeletions](const QString& path) { return stagedDeletions.contains(path); });
	return plan;
}

// Shared by the name listing and the line counts of the tracked changes: rename pairing and baseline must
// match, or a count could answer for a row that is not in the list
QStringList trackedDiffArgs(const QString& base, const QString& outputFormat, const QStringList& extraFlags = {})
{
	QStringList args = { QStringLiteral("diff"), outputFormat, QStringLiteral("-M"),
		QStringLiteral("--ignore-submodules=dirty"), QStringLiteral("-z") };
	args += extraFlags;
	args << base;
	return args;
}

QStringList trackedChangesArgs(const QString& base)
{
	return trackedDiffArgs(base, QStringLiteral("--name-status"));
}

// Whether a line-endings-only change shows as a change is a display setting.
// The same flags go into every shown diff and every line count, so they agree.
// Either way the row still lists as modified and commits its working-tree content verbatim.
QStringList eolDisplayFlags()
{
	return CSettings{}.value(Settings::ShowLineEndingOnlyChangesKey, Settings::ShowLineEndingOnlyChangesDefault).toBool()
		? QStringList{} : QStringList{ QStringLiteral("--ignore-cr-at-eol") };
}

QStringList trackedChangeCountsArgs(const QString& base)
{
	return trackedDiffArgs(base, QStringLiteral("--numstat"), eolDisplayFlags());
}

// Shared by the name listing and the line counts of one commit's files, for the same reason as trackedDiffArgs
QStringList commitFilesArgs(const QString& sha, const QString& outputFormat, const QStringList& extraFlags)
{
	QStringList args = { QStringLiteral("show"), outputFormat, QStringLiteral("-M"), QStringLiteral("-z"),
		QStringLiteral("--format=") };
	args += extraFlags;
	args << sha;
	return args;
}

// The base of every commit-listing query; the caller appends the walk.
// --topo-order: no commit above any of its children, and each line of history contiguous, which the graph
// needs. The default order only guarantees the former against the child a commit was first reached through.
QStringList commitLogArgs(int maxCommits)
{
	return { QStringLiteral("log"), QStringLiteral("-z"), QStringLiteral("--topo-order"),
		QStringLiteral("--max-count=%1").arg(maxCommits),
		QStringLiteral("--format=") + QLatin1String(Git::CommitLogFormat) };
}

// -S counts occurrences and takes the term literally. -G matches patch lines and takes a regex that
// --fixed-strings does not apply to, so the term has to be escaped (an unescaped `foo(` aborts the query).
void appendPickaxe(QStringList& args, const Repository::LogQuery& query, bool countChangesOnly)
{
	if (query.contentSearch.isEmpty())
		return;

	args << (countChangesOnly ? QStringLiteral("-S") + query.contentSearch
		: QStringLiteral("-G") + escapedForRegex(query.contentSearch));
	if (query.ignoreCase)
		args << QStringLiteral("-i");
}

// When empty, git defaults to HEAD
void appendStartRevision(QStringList& args, const Repository::LogQuery& query)
{
	if (!query.startRevision.isEmpty())
		args << query.startRevision;
}

// Must come last: the pathspec closes the argument list
void appendPathLimit(QStringList& args, const Repository::LogQuery& query)
{
	if (!query.path.isEmpty())
		args << QStringLiteral("--follow") << QStringLiteral("--") << query.path; // --follow requires exactly one path
}

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

// Read-only queries scoped to the repository; what a refresh and a push plan are made of
QueryRound::Launcher readOnlyQueries(const QObject* repo)
{
	return [repo](const QString& workDir, QStringList args, Vcs::Callback onResult) {
		Git::run(workDir, std::move(args), repo, std::move(onResult), {}, /*readOnlyQuery=*/true);
	};
}

// A submodule the push plan visits. Filled as the scan answers and read only once every query has, so the
// plan does not depend on the order they finished in.
struct SubmoduleScan
{
	QString workDir;
	QString displayPath; // relative to the repository the push was asked of
	QString branch;      // what a push inside it would publish; read only for a submodule that needs one
	bool needsPush = false;
	std::vector<std::shared_ptr<SubmoduleScan>> children;
};

struct PushPlanRun
{
	std::shared_ptr<SubmoduleScan> root = std::make_shared<SubmoduleScan>();
	QString refusal; // why no plan can be formed; the first one stands

	void refuse(const QString& reason)
	{
		if (refusal.isEmpty())
			refusal = reason;
	}
};

// Children before their parent: the parent's tree references commits that live in its children
void appendPushSteps(const SubmoduleScan& node, std::vector<PushStep>& steps)
{
	for (const std::shared_ptr<SubmoduleScan>& child : node.children)
		appendPushSteps(*child, steps);

	if (node.needsPush)
		steps.push_back({ .workDir = node.workDir, .subject = node.displayPath, .branch = node.branch });
}

// For a plain push inside a submodule to publish its recorded commit, it must be on a branch, and that branch
// must contain the commit
void requirePushableBranch(const std::shared_ptr<PushPlanRun>& run, const std::shared_ptr<SubmoduleScan>& node,
	const QString& recordedSha, QueryRound round)
{
	round.launch(node->workDir, { QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("-q"), QStringLiteral("HEAD") },
		[run, node](const ProcessResult& r) {
			node->branch = QString::fromUtf8(r.out.trimmed());
			if (!r.ok || node->branch.isEmpty())
				run->refuse(QStringLiteral("Submodule '%1' has commits to push but is not on a branch.\n"
					"Open it and check out a branch first.").arg(node->displayPath));
		});
	round.launch(node->workDir, { QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), recordedSha, QStringLiteral("HEAD") },
		[run, node](const ProcessResult& r) {
			if (!r.ok)
				run->refuse(QStringLiteral("Submodule '%1' is checked out at a commit that does not contain the commit recorded "
					"for it, so pushing its current branch would not publish the recorded commit.\n"
					"Open it and check out a branch that contains the recorded commit.").arg(node->displayPath));
		});
}

// Scans every gitlink in `parent`'s HEAD, recursing into those whose recorded commit no remote has.
// A submodule whose recorded commit is published needs no push, and neither does anything nested in it: the
// commit could not have been published while what it references was not.
void scanSubmodulesForPush(const std::shared_ptr<PushPlanRun>& run, const std::shared_ptr<SubmoduleScan>& parent, QueryRound round)
{
	round.launch(parent->workDir, { QStringLiteral("ls-tree"), QStringLiteral("-r"), QStringLiteral("-z"), QStringLiteral("HEAD") },
		[run, parent, round](const ProcessResult& lsTree) mutable {
			if (!lsTree.ok)
				return; // unborn HEAD: no gitlinks, and nothing to push

			for (const Git::GitlinkEntry& gitlink : Git::parseGitlinkEntries(lsTree.out))
			{
				const QString workDir = parent->workDir + QLatin1Char('/') + gitlink.path;
				if (!QFileInfo::exists(workDir + QStringLiteral("/.git")))
					continue; // never initialized

				auto node = std::make_shared<SubmoduleScan>();
				node->workDir = workDir;
				node->displayPath = parent->displayPath.isEmpty() ? gitlink.path
					: parent->displayPath + QLatin1Char('/') + gitlink.path;
				parent->children.push_back(node);

				// git's own on-demand test: the recorded commit is unpublished if no remote reaches it
				round.launch(workDir, { QStringLiteral("rev-list"), QStringLiteral("-n"), QStringLiteral("1"),
						gitlink.sha, QStringLiteral("--not"), QStringLiteral("--remotes") },
					[run, node, round, sha = gitlink.sha](const ProcessResult& revList) {
						// A recorded commit this clone does not have came from a remote, so a remote has it
						if (!revList.ok || revList.out.trimmed().isEmpty())
							return;

						node->needsPush = true;
						requirePushableBranch(run, node, sha, round);
						scanSubmodulesForPush(run, node, round);
					});
			}
		});
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
	int headParentCount = 0;

	QString failure; // why this run cannot become the state
	// Separate from `failure`: on an unborn HEAD the tracked-changes query against HEAD is expected to fail,
	// and the re-run against the empty tree answers in its place
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

	// The independent base queries, plus the one-time per-repository resolutions
	QueryRound round{ readOnlyQueries(this), [self = QPointer<GitRepository>{ this }, run] {
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
		// Stands in for HEAD where there is none. The sha depends only on the hash algorithm, and git resolves
		// the object in every repository, so nothing has to be written.
		round.launch(path(), { QStringLiteral("hash-object"), QStringLiteral("-t"), QStringLiteral("tree"), QStringLiteral("--stdin") },
			[this](const ProcessResult& r) { _emptyTreeSha = QString::fromUtf8(r.out.trimmed()); });
	}
	// Only the branch header and the unmerged entries are read, so the flags keep git from scanning the
	// working tree and recursing into submodules
	round.launch(path(), { QStringLiteral("status"), QStringLiteral("--porcelain=v2"), QStringLiteral("--branch"),
			QStringLiteral("--untracked-files=no"), QStringLiteral("--ignore-submodules=all"), QStringLiteral("-z") },
		[run](const ProcessResult& r) {
			if (r.ok)
			{
				run->header = Git::parseBranchHeader(r.out);
				run->conflicted = Git::parseUnmergedPaths(r.out);
			}
			else
				run->noteFailure(r); // an unread header would parse as "on a branch, born"
		});
	// HEAD's subject for the message header, and its parent count for the undo action.
	// Fails on an unborn branch, by design.
	// Parents first: %s is a single line and %P is not, so a root commit's empty %P would otherwise be
	// indistinguishable from a missing subject.
	round.launch(path(), { QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%P%n%s") },
		[run](const ProcessResult& r) {
			const QList<QByteArray> lines = r.out.split('\n');
			if (lines.size() < 2)
				return;

			const QByteArray parents = lines[0].simplified();
			run->headParentCount = parents.isEmpty() ? 0 : int(parents.count(' ')) + 1;
			run->headSubject = QString::fromUtf8(lines[1]).trimmed();
		});
	round.launch(path(), trackedChangesArgs(QStringLiteral("HEAD")),
		[run](const ProcessResult& r) {
			if (r.ok)
				run->diffEntries = Git::parseNameStatusZ(r.out);
			else
				run->trackedError = r.errorText(); // fails on an unborn HEAD by design; the empty-tree re-run answers instead
		});
	// Failure costs the rows their counts, which is a shorter answer rather than a wrong one
	round.launch(path(), trackedChangeCountsArgs(QStringLiteral("HEAD")),
		[run](const ProcessResult& r) { run->changeCounts = Git::parseNumstatZ(r.out); });
	// Failure costs the untracked rows, which is a shorter list rather than a wrong one
	round.launch(path(), { QStringLiteral("ls-files"), QStringLiteral("--others"), QStringLiteral("--exclude-standard"), QStringLiteral("-z") },
		[run](const ProcessResult& r) { run->untracked = Git::parseZList(r.out); });
	// Submodules are read from the index rather than `git submodule status`: that is a shell script in Git
	// for Windows and costs more than every other refresh query combined
	round.launch(path(), { QStringLiteral("ls-files"), QStringLiteral("--stage"), QStringLiteral("-z") },
		[run](const ProcessResult& r) {
			if (r.ok)
				run->submodules = Git::parseGitlinkPaths(r.out);
			else
				run->noteFailure(r); // unread, every gitlink would become an ordinary file that discarding may overwrite
		});
}

// The unborn fallback, the detached-HEAD branch tips, per-submodule dirtiness, the unpushed subjects. All
// independent of each other.
void GitRepository::startDependentQueries(const std::shared_ptr<RefreshRun>& run)
{
	QueryRound round{ readOnlyQueries(this), [self = QPointer<GitRepository>{ this }] {
		if (self)
			self->finishRefresh();
	} };

	// Unborn: re-run the tracked-changes queries against the empty tree, the first commit's parent tree anyway
	if (run->header.oid == QLatin1String("(initial)"))
	{
		if (_emptyTreeSha.isEmpty())
			run->trackedError = QStringLiteral("Could not resolve the empty tree; without it the changes of a repository with no commits cannot be listed.");
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
					run->trackedError.clear(); // the first-round failure was the expected one
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
			continue; // never initialized: the directory is empty
		round.launch(workDir, { QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("-z") },
			[run, subPath](const ProcessResult& r) {
				run->submoduleContent[subPath] = submoduleContentOf(r.ok, Git::parsePorcelainDirtiness(r.out));
			});
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
	state.headParentCount = run.headParentCount;
	state.detached = run.header.head == QLatin1String("(detached)");
	state.branch = state.detached ? QString{} : run.header.head;
	state.upstream = run.header.upstream;
	state.ahead = run.header.ahead;
	state.behind = run.header.behind;
	state.localBranchesAtHead = run.localBranchesAtHead;
	state.remoteBranchesAtHead = run.remoteBranchesAtHead;
	state.unpushedSubjects = run.unpushedSubjects;
	state.submodules = run.submodules; // ls-files lists the index, which is ordered by path

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

	// Not queried means never initialized, and an empty directory holds nothing
	const auto contentOf = [&run](const QString& path) {
		const auto it = run.submoduleContent.find(path);
		return it != run.submoduleContent.end() ? it->second : SubmoduleContent::Clean;
	};

	// Tracked changes; a diff row at a submodule path is a moved pointer
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
			// The diff cannot tell a conflict from an edit; the unmerged index entries can
			if (run.conflicted.contains(diffEntry.path))
				entry.type = ChangeType::Conflicted;
			if (const auto it = run.changeCounts.find(diffEntry.path); it != run.changeCounts.end())
				entry.lineCounts = it->second;
		}
		files.push_back(std::move(entry));
	}

	for (const QString& path : run.untracked)
		files.push_back({ .path = path, .type = ChangeType::Untracked });

	// Submodules with an unmoved pointer but blocking content get a row too, so the state is visible and the
	// row double-clickable. Untracked-only content does not earn a row.
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
	// The steps below pass ProcessResults between them (the rollback reports the commit's result, not its
	// own), so the conversion to an Answer happens once, here
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto messageFile = openMessageFile(message, this, report);
	if (!messageFile)
		return;

	// The index is made to hold exactly this commit, then restored to what it was
	const auto prepareIndexThenCommit = [this, messageFile, pathspec, untrackedPaths, report](const ProcessResult& stagedResult) {
		if (!stagedResult.ok)
		{
			report(stagedResult); // nothing touched yet, nothing to restore
			return;
		}

		const CommitIndexPlan plan = plannedIndexChange(Git::parseStagedRawZ(stagedResult.out), pathspec);

		// Every step from the reset on ends here, with whichever result ended the commit
		const auto restoreThenReport = [this, untrackedPaths, savedModes = plan.savedModes, report](const ProcessResult& result) {
			if (savedModes.isEmpty())
			{
				rollBackAddThenReport(path(), this, untrackedPaths, result, report);
				return;
			}
			Git::run(path(), { QStringLiteral("update-index"), QStringLiteral("-z"), QStringLiteral("--index-info") }, this,
				[this, untrackedPaths, result, report](const ProcessResult&) {
					rollBackAddThenReport(path(), this, untrackedPaths, result, report);
				}, savedModes);
		};

		const auto runCommit = [this, messageFile, restoreThenReport] {
			// The callback keeps the message file alive until git has read it
			Git::run(path(), { QStringLiteral("commit"), QStringLiteral("-F"), messageFile->fileName() }, this,
				[messageFile, restoreThenReport](const ProcessResult& result) { restoreThenReport(result); });
		};

		const auto addThenCommit = [this, addPaths = plan.addPaths, runCommit, restoreThenReport] {
			if (addPaths.isEmpty()) // every checked path is staged as deleted, so the index already holds the commit
			{
				runCommit();
				return;
			}
			Git::run(path(), { QStringLiteral("add"), QStringLiteral("--pathspec-from-file=-"), QStringLiteral("--pathspec-file-nul") }, this,
				[runCommit, restoreThenReport](const ProcessResult& result) {
					if (result.ok)
						runCommit();
					else
						restoreThenReport(result);
				}, Vcs::nulJoined(addPaths));
		};

		if (plan.resetPaths.isEmpty())
		{
			addThenCommit();
			return;
		}
		Git::run(path(), { QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("--pathspec-from-file=-"),
				QStringLiteral("--pathspec-file-nul") }, this,
			[addThenCommit, restoreThenReport](const ProcessResult& result) {
				if (result.ok)
					addThenCommit();
				else
					restoreThenReport(result);
			}, Vcs::nulJoined(plan.resetPaths));
	};

	Git::run(path(), { QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--raw"), QStringLiteral("--no-abbrev"),
		QStringLiteral("--no-renames"), QStringLiteral("-z"), diffBase() }, this, prepareIndexThenCommit, {}, /*readOnlyQuery=*/true);
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
				// Only the untracked add is rolled back: resetting a path `add -u` marked resolved would drop
				// the user's resolution
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

void GitRepository::undoLastCommit(Vcs::Answer<void> onDone)
{
	// --soft rather than the default --mixed: a path-limited commit never used the index, so leaving it alone
	// restores exactly the state the commit was made from, and keeps whatever another tool staged
	Git::run(path(), { QStringLiteral("reset"), QStringLiteral("--soft"), QStringLiteral("HEAD~1") },
		this, Vcs::reporting(std::move(onDone)));
}

void GitRepository::planPush(Vcs::Answer<std::vector<PushStep>> onDone)
{
	auto run = std::make_shared<PushPlanRun>();
	run->root->workDir = path();

	QueryRound round{ readOnlyQueries(this), [run, onDone = std::move(onDone), self = QPointer<GitRepository>{ this }] {
		if (!self)
			return; // the round also ends when its context dies

		if (!run->refusal.isEmpty())
		{
			onDone(std::unexpected(run->refusal));
			return;
		}

		std::vector<PushStep> steps;
		appendPushSteps(*run->root, steps);
		steps.push_back({ .workDir = self->path(), .branch = self->state().branch });
		onDone(std::move(steps));
	} };

	scanSubmodulesForPush(run, run->root, round);
}

Vcs::Job* GitRepository::runPushStep(const PushStep& step, bool setUpstream, Vcs::Callback onDone)
{
	QStringList args = { QStringLiteral("push"), QStringLiteral("--progress"),
		step.subject.isEmpty() ? QStringLiteral("--recurse-submodules=on-demand") : QStringLiteral("--recurse-submodules=no") };
	if (setUpstream)
		args << QStringLiteral("--set-upstream") << QStringLiteral("origin") << QStringLiteral("HEAD");

	return Git::run(step.workDir, std::move(args), this, std::move(onDone));
}

QString GitRepository::pushCommandLabel(const PushStep& step, bool setUpstream) const
{
	const QString command = setUpstream ? QStringLiteral("push --set-upstream origin HEAD") : QStringLiteral("push");
	return step.subject.isEmpty() ? QStringLiteral("git ") + command : QStringLiteral("git -C %1 %2").arg(step.subject, command);
}

void GitRepository::fetch(Vcs::Answer<void> onDone)
{
	// Not a read-only query: it writes the remote-tracking refs
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
	QStringList args = QStringList{ QStringLiteral("diff") } + eolDisplayFlags();
	args << QStringLiteral("-M") << diffBase() << QStringLiteral("--") << entry.path;
	if (!entry.oldPath.isEmpty())
		args.push_back(entry.oldPath);
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query GitRepository::diffAllChanges(const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	// --ignore-cr-at-eol regardless of the display setting: a line-ending conversion would flood the word
	// pool with every line of the file
	QStringList args = { QStringLiteral("diff"), QStringLiteral("--ignore-cr-at-eol"), QStringLiteral("-U0"),
		QStringLiteral("--ignore-submodules"), diffBase() };
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query GitRepository::commitLog(const LogQuery& query, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone)
{
	QStringList args = commitLogArgs(query.maxCommits);
	appendPickaxe(args, query, /*countChangesOnly=*/false);
	appendStartRevision(args, query);
	appendPathLimit(args, query);
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), Git::parseCommitLog));
}

Vcs::Query GitRepository::commitsAddingOrRemovingText(const LogQuery& query, const QObject* context, Vcs::Answer<QSet<QString>> onDone)
{
	QStringList args = { QStringLiteral("log"), QStringLiteral("--max-count=%1").arg(query.maxCommits),
		QStringLiteral("--format=%H") };
	appendPickaxe(args, query, /*countChangesOnly=*/true);
	appendStartRevision(args, query);
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
	// --raw rather than --name-status for the modes and object names, which identify submodule rows and the
	// commit their pointer moved to. --no-abbrev because those shas are opened as revisions.
	return runQuery(path(), commitFilesArgs(sha, QStringLiteral("--raw"), { QStringLiteral("--no-abbrev") }),
		context, Vcs::answering(std::move(onDone), Git::parseRawZ));
}

Vcs::Query GitRepository::commitFileCounts(const QString& sha, const QObject* context, Vcs::Answer<std::map<QString, LineCounts>> onDone)
{
	return runQuery(path(), commitFilesArgs(sha, QStringLiteral("--numstat"), eolDisplayFlags()),
		context, Vcs::answering(std::move(onDone), Git::parseNumstatZ));
}

Vcs::Query GitRepository::commitFileDiff(const QString& sha, const CommitFileChange& file, const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	QStringList args = QStringList{ QStringLiteral("show"), QStringLiteral("-M") } + eolDisplayFlags();
	args << QStringLiteral("--format=") << sha << QStringLiteral("--") << file.path;
	if (!file.oldPath.isEmpty())
		args.push_back(file.oldPath); // both sides, or the pathspec filters the rename out before -M can pair it
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query GitRepository::unpushedCommits(const QObject* context, Vcs::Answer<QSet<QString>> onDone)
{
	return runQuery(path(), { QStringLiteral("rev-list"), QStringLiteral("@{upstream}..HEAD") },
		context, Vcs::answering(std::move(onDone), shaSet));
}

Vcs::Query GitRepository::submodulePointerLog(const QString& repoRelativePath, const QObject* context, Vcs::Answer<QString> onDone)
{
	const QString subPath = path() + QLatin1Char('/') + repoRelativePath;
	Vcs::Query query;
	query.attach(Git::run(path(), { QStringLiteral("rev-parse"), QStringLiteral("HEAD:") + repoRelativePath }, context,
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

RepositoryLocation GitRepository::submoduleLocation(const QString& repoRelativePath) const
{
	return { VcsKind::Git, path() + QLatin1Char('/') + repoRelativePath }; // a git submodule is always a git repository
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

// .gitignore has no sections, so the pattern always goes at the end
QByteArray GitRepository::ignoreFileWithPatternAdded(QByteArray content, const IgnorePattern& pattern) const
{
	const QByteArray eol = content.contains("\r\n") ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");
	if (!content.isEmpty() && !content.endsWith('\n'))
		content += eol;
	return content + pattern.text.toUtf8() + eol;
}

void GitRepository::launchExternalDiffTool(const QString& repoRelativePath) const
{
	QString executable = CSettings{}.value(Settings::GitExecutableKey).toString();
	if (executable.isEmpty())
		executable = QLatin1String(Settings::GitExecutableDefault);
	QProcess::startDetached(executable,
		{ QStringLiteral("difftool"), QStringLiteral("-y"), QStringLiteral("HEAD"), QStringLiteral("--"), repoRelativePath },
		path());
}
