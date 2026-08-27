#include "hgrepository.h"
#include "gitparsers.h"
#include "gitprocess.h"
#include "gitrepository.h"
#include "hgparsers.h"
#include "hgprocess.h"
#include "queryround.h"
#include "settings.h"

#include "settings/csettings.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <assert.h>
#include <functional>
#include <utility>

namespace {

constexpr int MaxUnpushedLogEntries = 30; // for the tooltip; state.ahead carries the true count

QByteArray fileContents(const QString& path)
{
	QFile file{ path };
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

// A pattern naming a file of NUL-separated paths, so a long list never reaches the command line
QString listfilePattern(const std::shared_ptr<QTemporaryFile>& file)
{
	return QStringLiteral("listfile0:") + file->fileName();
}

// Glob metacharacters in a file name must stay literal, and '#' starts a comment at line start. Unlike
// git's ignore syntax there is no '!' negation to escape.
QString escapedForHgIgnore(const QString& text)
{
	QString out;
	out.reserve(text.size());
	for (const QChar c : text)
	{
		if (c == QLatin1Char('\\') || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('[') || c == QLatin1Char(']'))
			out += QLatin1Char('\\');
		out += c;
	}
	if (out.startsWith(QLatin1Char('#')))
		out.prepend(QLatin1Char('\\'));
	return out;
}

// hg exits with 1 for "nothing to report": no incoming changesets, none outgoing, no search hits, nothing
// to push. That is an ordinary empty answer, not a failure.
Vcs::Callback tolerantOfEmptyResult(Vcs::Callback callback)
{
	return [callback = std::move(callback)](const ProcessResult& result) {
		ProcessResult corrected = result;
		corrected.ok = result.outcome == ProcessOutcome::Exited && result.exitCode <= 1;
		callback(corrected);
	};
}

QString outputAsText(const QByteArray& output)
{
	return QString::fromUtf8(output);
}

Vcs::Query runQuery(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback)
{
	return Vcs::Query{ Hg::run(workDir, std::move(args), context, std::move(callback)) };
}

// The base of every commit-listing query; the caller appends the walk.
// -f: the ancestors of `.` (or of the -r revision) only; a plain `hg log` lists every changeset, unrelated heads included.
// With a path, -f also follows that file across renames.
QStringList commitLogArgs(int maxCommits)
{
	return { QStringLiteral("log"), QStringLiteral("-f"), QStringLiteral("-l"), QString::number(maxCommits),
		QStringLiteral("-T"), QStringLiteral("json") };
}

// Must precede the pattern and the pathspec, which are positional
void appendStartRevision(QStringList& args, const Repository::LogQuery& query)
{
	if (!query.startRevision.isEmpty())
		args << QStringLiteral("-r") << query.startRevision;
}

// Must come last: the pathspec closes the argument list
void appendPathLimit(QStringList& args, const Repository::LogQuery& query)
{
	if (!query.path.isEmpty())
		args << QStringLiteral("--") << query.path;
}

// `hg grep` has no fixed-string mode, so the term has to be escaped. -f confines it to the line of history
// the listing walks; without it every head is searched.
QStringList grepArgs(const Repository::LogQuery& query)
{
	QStringList args = { QStringLiteral("grep"), QStringLiteral("--diff"), QStringLiteral("-f"),
		QStringLiteral("-T"), QStringLiteral("json") };
	if (query.ignoreCase)
		args << QStringLiteral("-i");
	appendStartRevision(args, query);
	args << escapedForRegex(query.contentSearch);
	if (!query.path.isEmpty())
		args << QStringLiteral("--") << query.path;
	return args;
}

// The found changesets as one revset, capped and sorted newest first (hg's own order for a union is the
// opposite of the log view's)
QString revsetOf(const std::vector<Hg::GrepMatch>& matches, int maxCommits)
{
	QStringList nodes;
	for (const Hg::GrepMatch& match : matches)
	{
		if (nodes.size() >= maxCommits)
			break;
		nodes << match.node;
	}
	return QStringLiteral("sort(%1, -rev)").arg(nodes.join(QLatin1Char('+')));
}

// --git: names renames and binary files instead of printing every line as added.
// -Z (git's --ignore-cr-at-eol): whether a line-endings-only change shows is a display setting.
// The line counts come from these same args, so they always match the shown diff.
// Either way the row still commits its content verbatim.
QStringList diffArgs()
{
	QStringList args = { QStringLiteral("diff"), QStringLiteral("--git") };
	if (!CSettings{}.value(Settings::ShowLineEndingOnlyChangesKey, Settings::ShowLineEndingOnlyChangesDefault).toBool())
		args << QStringLiteral("-Z");
	return args;
}

// No context lines: for the word pool and for line counting
QStringList contextFreeDiffArgs()
{
	QStringList args = diffArgs();
	args << QStringLiteral("-U") << QStringLiteral("0");
	return args;
}

// One changeset's subrepo pointer moves, as the diff of .hgsubstate. `path:` keeps the name a literal path
// rather than a pattern.
QStringList substateDiffArgs(const QString& sha)
{
	QStringList args = contextFreeDiffArgs();
	args << QStringLiteral("-c") << sha << QStringLiteral("--") << QStringLiteral("path:.hgsubstate");
	return args;
}

// A pointer move in .hgsubstate's own format, which the diff view colors like any diff
QByteArray pointerMoveText(const Hg::SubrepoPointerChange& change)
{
	QByteArray text;
	if (!change.oldNode.isEmpty())
		text += "-" + change.oldNode.toUtf8() + " " + change.path.toUtf8() + "\n";
	if (!change.newNode.isEmpty())
		text += "+" + change.newNode.toUtf8() + " " + change.path.toUtf8() + "\n";
	return text;
}

} // namespace

struct HgRepository::RefreshRun
{
	std::vector<CommitFileChange> status;
	std::map<QString, LineCounts> changeCounts; // by path; only files with countable lines have one
	std::vector<CommitRecord> head; // `.`; the null changeset in an unborn repository
	Hg::WorkingDirectory workingDir;
	QStringList pathNames; // the configured [paths] names; which one a push contacts is stateFromRun's call
	std::vector<CommitRecord> unpushed; // the drafts `push -r .` would send
	QStringList conflicted; // only queried while a mergestate exists
	std::map<QString, QString> subrepoNodes; // the changeset each subrepo is actually on; absent if unread
	std::map<QString, SubmoduleContent> subrepoContent; // absent if unread

	QString failure;

	void noteFailure(const ProcessResult& result)
	{
		if (failure.isEmpty())
			failure = result.errorText();
	}
};

HgRepository::HgRepository(QString rootPath, QObject* parent) :
	Repository(std::move(rootPath), parent)
{
}

void HgRepository::startRefresh()
{
	auto run = std::make_shared<RefreshRun>();
	_run = run;

	// Re-read first: this decides which subrepos are asked about, and which tool answers for each
	_subrepoNodes = Hg::parseSubrepoState(fileContents(QDir{ path() }.filePath(QStringLiteral(".hgsubstate"))));
	_subrepoSources = Hg::parseSubrepoSources(fileContents(QDir{ path() }.filePath(QStringLiteral(".hgsub"))));

	// One round: every hg invocation pays Python startup, so nothing waits that does not have to
	QueryRound round{ refreshQueries(), [self = QPointer<HgRepository>{ this }] {
		if (self)
			self->finishRefresh();
	} };

	// Not recursing: a subrepo's content is asked of the subrepo itself, and the parent's list holds the
	// subrepo rather than the files under it
	round.launch(path(), { QStringLiteral("status"), QStringLiteral("-C"), QStringLiteral("-T"), QStringLiteral("json") },
		[run](const ProcessResult& r) {
			if (r.ok)
				run->status = Hg::parseStatus(r.out);
			else
				run->noteFailure(r);
		});
	// Failure costs the rows their counts, which is a shorter answer rather than a wrong one
	round.launch(path(), contextFreeDiffArgs(),
		[run](const ProcessResult& r) { run->changeCounts = Hg::parseDiffCounts(r.out); });
	// The parent changeset: what the next commit builds on, and what the header names
	round.launch(path(), { QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("."), QStringLiteral("-T"), QStringLiteral("json") },
		[run](const ProcessResult& r) {
			if (r.ok)
				run->head = Hg::parseCommitLog(r.out);
			else
				run->noteFailure(r);
		});
	// The working directory carries the branch a commit would land on (after `hg branch`, the parent
	// changeset does not yet) and has two parents while a merge is uncommitted
	round.launch(path(), { QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("wdir()"), QStringLiteral("-T"), QStringLiteral("json") },
		[run](const ProcessResult& r) {
			if (r.ok)
				run->workingDir = Hg::parseWorkingDirectory(r.out);
			else
				run->noteFailure(r); // unread, a merge in progress is indistinguishable from none
		});
	// No configured path is an answer, not a failure: nowhere to push to
	round.launch(path(), { QStringLiteral("paths"), QStringLiteral("-T"), QStringLiteral("json") },
		[run](const ProcessResult& r) { run->pathNames = Hg::parsePathNames(r.out); });
	// Drafts among the ancestors of the parent: what `push -r .` sends, next to which this count is shown.
	// Without a remote every changeset is draft, so stateFromRun only reads this when there is one.
	round.launch(path(), { QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("draft() and ::."),
			QStringLiteral("-T"), QStringLiteral("json") },
		[run](const ProcessResult& r) { run->unpushed = Hg::parseCommitLog(r.out); });

	// `status` reports a conflicted file as modified; only the mergestate knows better.
	// Every command that can conflict (merge, graft, rebase, update over local changes) leaves a mergestate
	// until the resolve is committed or aborted, so its absence is a complete answer.
	if (QFileInfo::exists(QDir{ path() }.filePath(QStringLiteral(".hg/merge"))))
	{
		round.launch(path(), { QStringLiteral("resolve"), QStringLiteral("--list"), QStringLiteral("-T"), QStringLiteral("json") },
			[run](const ProcessResult& r) { run->conflicted = Hg::parseUnresolvedPaths(r.out); });
	}

	launchSubrepoQueries(round, run);
}

// What each subrepo is on and holds, asked of the subrepo itself
void HgRepository::launchSubrepoQueries(QueryRound& round, const std::shared_ptr<RefreshRun>& run)
{
	for (const auto& subrepo : _subrepoNodes)
	{
		const QString& subPath = subrepo.first;
		const QString workDir = QDir{ path() }.filePath(subPath);
		if (!subrepoCloned(subPath))
			continue; // never cloned, or not a repository anymore

		// Dirtiness is queried in the subrepo, not via the parent's recursing status: that compares against the
		// node .hgsubstate records rather than the subrepo's parent changeset, so after a committed pointer move
		// it reports the files as modified, exactly when the pointer must be committable.
		// The subrepo's own status must recurse: a nested subrepo the enclosing one has not recorded yet is
		// uncommitted work there, which blocks this row.
		if (isGitSubrepo(subPath))
		{
			// Needs no flag: git status reports a submodule holding its own commits as modified
			round.launch(workDir, { QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("-z") },
				[run, subPath = subPath](const ProcessResult& r) {
					run->subrepoContent[subPath] = submoduleContentOf(r.ok, Git::parsePorcelainDirtiness(r.out));
				});
			round.launch(workDir, { QStringLiteral("rev-parse"), QStringLiteral("HEAD") },
				[run, subPath = subPath](const ProcessResult& r) {
					if (r.ok)
						run->subrepoNodes[subPath] = QString::fromUtf8(r.out.trimmed());
				});
			continue;
		}
		round.launch(workDir, { QStringLiteral("status"), QStringLiteral("-S"), QStringLiteral("-T"), QStringLiteral("json") },
			[run, subPath = subPath](const ProcessResult& r) {
				run->subrepoContent[subPath] = submoduleContentOf(r.ok, Hg::parseDirtiness(r.out));
			});
		round.launch(workDir, { QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("."), QStringLiteral("-T"), QStringLiteral("json") },
			[run, subPath = subPath](const ProcessResult& r) {
				const std::vector<CommitRecord> parent = Hg::parseCommitLog(r.out);
				if (r.ok && !parent.empty())
					run->subrepoNodes[subPath] = parent.front().sha;
			});
	}
}

void HgRepository::finishRefresh()
{
	const RefreshRun& run = *_run;

	RepoState state;
	std::vector<FileEntry> files;
	if (run.failure.isEmpty())
	{
		state = stateFromRun(run);
		files = filesFromRun(run);
	}
	state.readFailure = run.failure;

	_run.reset();
	completeRefresh(std::move(state), std::move(files));
}

RepoState HgRepository::stateFromRun(const RefreshRun& run) const
{
	const CommitRecord head = run.head.empty() ? CommitRecord{} : run.head.front();

	RepoState state;
	state.unborn = head.sha.isEmpty() || head.sha == QLatin1String(Hg::NullNode);
	state.headSha = state.unborn ? QString{} : head.sha;
	state.headSubject = state.unborn ? QString{} : head.subject();
	state.headParentCount = int(head.parents.size()); // the parser already drops a root changeset's null parent
	// The named branch (`default` unless another was created); bookmarks are not read, and hg has no detached state
	state.branch = run.workingDir.branch;
	// hg has no per-branch upstream; a push contacts default-push where configured, else default
	state.upstream = run.pathNames.contains(QLatin1String("default-push")) ? QStringLiteral("default-push")
		: run.pathNames.contains(QLatin1String("default")) ? QStringLiteral("default") : QString{};
	state.behind = _behind;

	if (!state.upstream.isEmpty())
	{
		state.ahead = int(run.unpushed.size());
		for (const CommitRecord& commit : run.unpushed)
		{
			if (state.unpushedSubjects.size() >= MaxUnpushedLogEntries)
				break;
			state.unpushedSubjects << commit.subject();
		}
	}

	// From .hgsub, which declares subrepos; .hgsubstate lags a newly added one by a commit. The map is keyed
	// by path, so this comes out in path order.
	for (const auto& subrepo : _subrepoSources)
		state.submodules << subrepo.first;

	// Rebase, graft and histedit leave state of their own that this does not read
	if (run.workingDir.parents.size() > 1)
		state.op = RepoOp::Merge;
	return state;
}

std::vector<FileEntry> HgRepository::filesFromRun(const RefreshRun& run) const
{
	std::vector<FileEntry> files;

	for (const CommitFileChange& change : run.status)
	{
		const ChangeType type = run.conflicted.contains(change.path) ? ChangeType::Conflicted : change.type;
		FileEntry entry{ .path = change.path, .oldPath = change.oldPath, .type = type };
		if (const auto counted = run.changeCounts.find(change.path); counted != run.changeCounts.end())
			entry.lineCounts = counted->second;
		files.push_back(std::move(entry));
	}

	// A modified-here/deleted-there conflict keeps the local file with an empty status, so it has no row;
	// the mergestate is the only evidence
	for (const QString& conflictedPath : run.conflicted)
	{
		if (std::ranges::none_of(files, [&](const FileEntry& f) { return f.path == conflictedPath; }))
			files.push_back({ .path = conflictedPath, .type = ChangeType::Conflicted });
	}

	for (const auto& [subPath, recordedNode] : _subrepoNodes)
	{
		if (!subrepoCloned(subPath))
			continue; // never cloned, or not a repository anymore

		const auto contentInside = run.subrepoContent.find(subPath);
		const auto currentNode = run.subrepoNodes.find(subPath);
		FileEntry entry{ .path = subPath, .isSubmodule = true,
			.content = contentInside != run.subrepoContent.end() ? contentInside->second : SubmoduleContent::Unknown };
		if (currentNode == run.subrepoNodes.end())
		{
			// Whether the pointer moved is unknown, which blocks committing it as much as dirty content does
			if (entry.content == SubmoduleContent::Clean || entry.content == SubmoduleContent::Untracked)
				entry.content = SubmoduleContent::Unknown;
		}
		else
			entry.pointerMoved = currentNode->second != recordedNode;

		if (entry.pointerMoved || entry.contentBlocksPointer())
			files.push_back(std::move(entry));
	}
	return files;
}

RepoOp HgRepository::probeOperation() const
{
	// The mergestate is the only marker hg leaves; the base contract covers what it can and cannot show
	return QFileInfo::exists(QDir{ path() }.filePath(QStringLiteral(".hg/merge"))) ? RepoOp::Merge : RepoOp::None;
}

bool HgRepository::isGitSubrepo(const QString& subrepoPath) const
{
	const auto source = _subrepoSources.find(subrepoPath);
	return source != _subrepoSources.end() && Hg::subrepoKind(source->second) == VcsKind::Git;
}

// Directory existence is not enough: hg queried in a marker-less directory resolves upward and answers for
// the parent. A submodule's .git may be a file, which exists() covers.
bool HgRepository::subrepoCloned(const QString& subrepoPath) const
{
	const QDir workDir{ QDir{ path() }.filePath(subrepoPath) };
	return QFileInfo::exists(workDir.filePath(isGitSubrepo(subrepoPath) ? QStringLiteral(".git") : QStringLiteral(".hg")));
}

QueryRound::Launcher HgRepository::refreshQueries()
{
	return [this](const QString& workDir, QStringList args, Vcs::Callback onResult) {
		if (workDir != path() && isGitSubrepo(QDir{ path() }.relativeFilePath(workDir)))
			Git::run(workDir, std::move(args), this, std::move(onResult), {}, /*readOnlyQuery=*/true);
		else
			Hg::run(workDir, std::move(args), this, std::move(onResult));
	};
}

std::shared_ptr<QTemporaryFile> HgRepository::openPathspecFile(const QStringList& paths, const Vcs::Callback& onFailure)
{
	// Not Vcs::nulJoined: hg reads the listfile as local-encoding bytes, not UTF-8
	QByteArray joined;
	for (const QString& path : paths)
	{
		joined += Hg::localBytes(path);
		joined += '\0';
	}
	return Vcs::openTempFile(joined, QStringLiteral("pathspec"), this, onFailure);
}

void HgRepository::commit(const QString& message, const QStringList& pathspec, const QStringList& /*untrackedPaths*/, Vcs::Answer<void> onDone)
{
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto messageFile = Vcs::openTempFile(Hg::localBytes(message), QStringLiteral("commit message"), this, report);
	if (!messageFile)
		return;
	const auto pathspecFile = openPathspecFile(pathspec, report);
	if (!pathspecFile)
		return;

	// -A: adds the new and removes the missing files within the pathspec. A missing file cannot otherwise be
	// committed by name.
	// No separate add step for the untracked paths, and no rollback: a failed commit rolls its own -A back with
	// the rest of the transaction.
	Hg::run(path(), { QStringLiteral("commit"), QStringLiteral("-l"), messageFile->fileName(),
			QStringLiteral("-A"), listfilePattern(pathspecFile) }, this,
		[messageFile, pathspecFile, report](const ProcessResult& result) { report(result); });
}

void HgRepository::commitMergeState(const QString& message, const QStringList& untrackedPaths, Vcs::Answer<void> onDone)
{
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto messageFile = Vcs::openTempFile(Hg::localBytes(message), QStringLiteral("commit message"), this, report);
	if (!messageFile)
		return;

	const auto runCommit = [this, messageFile, untrackedPaths, report] {
		// No pathspec: a merge cannot be committed in parts. No -A either: unscoped, it would sweep in every
		// unknown file rather than the ticked ones.
		Hg::run(path(), { QStringLiteral("commit"), QStringLiteral("-l"), messageFile->fileName() }, this,
			[this, messageFile, untrackedPaths, report](const ProcessResult& result) {
				// A failed commit would leave the untracked paths added, and their rows would read Added instead
				// of Untracked. The commit's result is what gets reported.
				if (result.ok || untrackedPaths.isEmpty())
				{
					report(result);
					return;
				}
				unAdd(untrackedPaths, [report, result](std::expected<void, QString>) { report(result); });
			});
	};

	if (untrackedPaths.isEmpty())
	{
		runCommit();
		return;
	}
	const auto pathspecFile = openPathspecFile(untrackedPaths, report);
	if (!pathspecFile)
		return;
	Hg::run(path(), { QStringLiteral("add"), listfilePattern(pathspecFile) }, this,
		[pathspecFile, runCommit, report](const ProcessResult& result) {
			if (result.ok)
				runCommit();
			else
				report(result);
		});
}

void HgRepository::undoLastCommit(Vcs::Answer<void> onDone)
{
	// `uncommit` ships with hg but is off by default, so the command enables it for itself.
	// Narrower than `rollback`, which undoes the last transaction of any kind; refuses public and merge changesets.
	// --allow-dirty-working-copy: whatever the commit did not take is still modified.
	Hg::run(path(), { QStringLiteral("--config"), QStringLiteral("extensions.uncommit="),
		QStringLiteral("uncommit"), QStringLiteral("--allow-dirty-working-copy") },
		this, Vcs::reporting(std::move(onDone)));
}

void HgRepository::abortOperation(Vcs::Answer<void> onDone)
{
	// stateFromRun reads a merge and nothing else, so a merge is the only operation that reaches this
	assert(state().op == RepoOp::Merge);
	Hg::run(path(), { QStringLiteral("merge"), QStringLiteral("--abort") }, this, Vcs::reporting(std::move(onDone)));
}

void HgRepository::planPush(Vcs::Answer<std::vector<PushStep>> onDone)
{
	// Answered from the event loop, like every other operation
	QTimer::singleShot(0, this, [onDone = std::move(onDone), root = path()] {
		onDone(std::vector<PushStep>{ { .workDir = root } });
	});
}

Vcs::Job* HgRepository::runPushStep(const PushStep& step, bool /*setUpstream*/, Vcs::Callback onDone)
{
	// A process of its own: the push log streams its output, and cancelling mid-transfer must kill it
	return Hg::run(step.workDir, { QStringLiteral("push"), QStringLiteral("-r"), QStringLiteral(".") }, this,
		tolerantOfEmptyResult(std::move(onDone)), {}, Hg::Transport::Process);
}

QString HgRepository::pushCommandLabel(const PushStep& /*step*/, bool /*setUpstream*/) const
{
	return QStringLiteral("hg push -r .");
}

void HgRepository::fetch(Vcs::Answer<void> onDone)
{
	// Nothing to do: Mercurial keeps no remote-tracking state, incomingCommits() reads the remote directly
	QTimer::singleShot(0, this, [onDone = std::move(onDone)] { onDone({}); });
}

void HgRepository::addToIndex(const QStringList& paths, Vcs::Answer<void> onDone)
{
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto pathspecFile = openPathspecFile(paths, report);
	if (!pathspecFile)
		return;

	Hg::run(path(), { QStringLiteral("add"), listfilePattern(pathspecFile) }, this,
		[pathspecFile, report](const ProcessResult& result) { report(result); });
}

// The mergestate entry goes from U to R; the file itself is not touched
void HgRepository::markResolved(const QStringList& paths, Vcs::Answer<void> onDone)
{
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto pathspecFile = openPathspecFile(paths, report);
	if (!pathspecFile)
		return;

	Hg::run(path(), { QStringLiteral("resolve"), QStringLiteral("-m"), listfilePattern(pathspecFile) }, this,
		[pathspecFile, report](const ProcessResult& result) { report(result); });
}

void HgRepository::unAdd(const QStringList& paths, Vcs::Answer<void> onDone)
{
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto pathspecFile = openPathspecFile(paths, report);
	if (!pathspecFile)
		return;

	Hg::run(path(), { QStringLiteral("forget"), listfilePattern(pathspecFile) }, this,
		[pathspecFile, report](const ProcessResult& result) { report(result); });
}

void HgRepository::discardChanges(const QStringList& pathspec, Vcs::Answer<void> onDone)
{
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto pathspecFile = openPathspecFile(pathspec, report);
	if (!pathspecFile)
		return;

	// -C: without it every reverted file leaves a .orig copy, which the next refresh would list as untracked
	Hg::run(path(), { QStringLiteral("revert"), QStringLiteral("-C"), QStringLiteral("--rev"), QStringLiteral("."),
			listfilePattern(pathspecFile) }, this,
		[pathspecFile, report](const ProcessResult& result) { report(result); });
}

SubmoduleDiscardPlan HgRepository::submoduleDiscardPlan(const QString& repoRelativePath) const
{
	const QString workDir = QDir{ path() }.filePath(repoRelativePath);
	if (isGitSubrepo(repoRelativePath))
		return Git::uncommittedDiscardPlan(workDir);

	// A mergestate outlives its command until the resolve is committed or aborted (see startRefresh), so its
	// presence answers for every command that can conflict
	if (QFileInfo::exists(QDir{ workDir }.filePath(QStringLiteral(".hg/merge"))))
		return { .refusal = QObject::tr("An unfinished merge, graft or rebase is in progress there. Finish or abort it first.") };

	const ProcessResult workingDir = Hg::runSync(workDir,
		{ QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("wdir()"), QStringLiteral("-T"), QStringLiteral("json") });
	if (!workingDir.ok)
		return { .refusal = QObject::tr("Its working directory could not be read: %1").arg(workingDir.errorText()) };
	if (Hg::parseWorkingDirectory(workingDir.out).parents.size() > 1)
		return { .refusal = QObject::tr("A merge is in progress there. Commit or abort it first.") };

	// Not recursing: `revert` does not either, and a nested subrepo's own content is discarded in its window
	const ProcessResult status = Hg::runSync(workDir,
		{ QStringLiteral("status"), QStringLiteral("-C"), QStringLiteral("-T"), QStringLiteral("json") });
	if (!status.ok)
		return { .refusal = QObject::tr("Its status could not be read: %1").arg(status.errorText()) };

	const std::vector<CommitFileChange> changes = Hg::parseStatus(status.out);
	const bool nestedSubmoduleChanged = std::ranges::any_of(changes,
		[](const CommitFileChange& change) { return change.path == QLatin1String(".hgsubstate"); });
	return discardPlanFor(changes, nestedSubmoduleChanged);
}

void HgRepository::discardSubmoduleContent(const QString& repoRelativePath, const SubmoduleDiscardPlan& plan, Vcs::Answer<void> onDone)
{
	const QString workDir = QDir{ path() }.filePath(repoRelativePath);
	if (isGitSubrepo(repoRelativePath))
	{
		Git::discardAllUncommitted(workDir, plan, this, std::move(onDone));
		return;
	}

	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto pathspecFile = openPathspecFile(plan.restored + plan.keptOnDisk, report);
	if (!pathspecFile)
		return;

	// Reverting a path the parent changeset does not have only takes it out of tracking, so both lists go in
	// one command. -C for the same reason as in discardChanges().
	Hg::run(workDir, { QStringLiteral("revert"), QStringLiteral("-C"), QStringLiteral("--rev"), QStringLiteral("."),
			listfilePattern(pathspecFile) }, this,
		[pathspecFile, report](const ProcessResult& result) { report(result); });
}

void HgRepository::checkoutBranch(const QString& branch, Vcs::Answer<void> onDone)
{
	Hg::run(path(), { QStringLiteral("update"), branch }, this, Vcs::reporting(std::move(onDone)));
}

void HgRepository::createTrackingBranch(const QString& /*localName*/, const QString& /*remoteBranch*/, Vcs::Answer<void> onDone)
{
	QTimer::singleShot(0, this, [onDone = std::move(onDone)] {
		onDone(std::unexpected(QObject::tr("Mercurial has no remote branches to create a local branch from.")));
	});
}

void HgRepository::localBranchExists(const QString& name, const QObject* context, std::function<void(bool)> onDone)
{
	Hg::run(path(), { QStringLiteral("branches"), QStringLiteral("-T"), QStringLiteral("json") }, context,
		[name, onDone = std::move(onDone)](const ProcessResult& result) {
			onDone(result.ok && Hg::parseBranchNames(result.out).contains(name));
		});
}

Vcs::Query HgRepository::diffFile(const FileEntry& entry, const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	// No need to name a rename's source: hg records copies rather than inferring them, so --git prints the
	// old path on its own
	QStringList args = diffArgs();
	if (entry.type == ChangeType::Deleted)
	{
		// A file missing from disk stays in the dirstate until its removal is recorded, so `hg diff` would report
		// no change. Parent against null is the removal this row commits, and the same diff for an `hg remove`d file.
		args << QStringLiteral("-r") << QStringLiteral(".") << QStringLiteral("-r") << QStringLiteral("null");
	}
	args << QStringLiteral("--") << entry.path;
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query HgRepository::diffAllChanges(const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	// -Z regardless of the display setting: a line-ending conversion would flood the word pool with every
	// line of the file
	QStringList args = { QStringLiteral("diff"), QStringLiteral("--git"), QStringLiteral("-Z"),
		QStringLiteral("-U"), QStringLiteral("0") };
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query HgRepository::commitLog(const LogQuery& query, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone)
{
	if (query.contentSearch.isEmpty())
	{
		QStringList args = commitLogArgs(query.maxCommits);
		appendStartRevision(args, query);
		appendPathLimit(args, query);
		return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), Hg::parseCommitLog));
	}

	// grep only names changesets; a second query lists them
	Vcs::Query query2;
	const int maxCommits = query.maxCommits;
	query2.attach(Hg::run(path(), grepArgs(query), context,
		tolerantOfEmptyResult([this, query2, context, maxCommits, onDone = std::move(onDone)](const ProcessResult& result) mutable {
			if (!result.ok)
			{
				onDone(std::unexpected(result.errorText()));
				return;
			}
			const std::vector<Hg::GrepMatch> matches = Hg::parseGrepDiff(result.out);
			if (matches.empty())
			{
				onDone(std::vector<CommitRecord>{});
				return;
			}
			query2.attach(Hg::run(path(), { QStringLiteral("log"), QStringLiteral("-r"), revsetOf(matches, maxCommits),
				QStringLiteral("-T"), QStringLiteral("json") }, context, Vcs::answering(std::move(onDone), Hg::parseCommitLog)));
		})));
	return query2;
}

Vcs::Query HgRepository::commitsAddingOrRemovingText(const LogQuery& query, const QObject* context, Vcs::Answer<QSet<QString>> onDone)
{
	// The same grep as the listing's: a changeset whose added and removed matching lines differ in number
	// gained or lost the text rather than editing around it
	const auto changedOccurrences = [maxCommits = query.maxCommits](const QByteArray& output) {
		QSet<QString> nodes;
		for (const Hg::GrepMatch& match : Hg::parseGrepDiff(output))
		{
			if (nodes.size() >= maxCommits)
				break;
			if (match.matchedLines.added != match.matchedLines.removed)
				nodes.insert(match.node);
		}
		return nodes;
	};
	return runQuery(path(), grepArgs(query), context,
		tolerantOfEmptyResult(Vcs::answering(std::move(onDone), changedOccurrences)));
}

Vcs::Query HgRepository::incomingCommits(int maxCommits, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone)
{
	// The result is also the only source of the behind count, so it is kept for the next refresh
	const auto record = [this](const QByteArray& output) {
		std::vector<CommitRecord> commits = Hg::parseCommitLog(output);
		_behind = int(commits.size());
		// Revision numbers are the remote's; pulling would number these differently
		for (CommitRecord& commit : commits)
			commit.revision.reset();
		return commits;
	};
	return runQuery(path(), { QStringLiteral("incoming"), QStringLiteral("-l"), QString::number(maxCommits),
		QStringLiteral("-T"), QStringLiteral("json") }, context,
		tolerantOfEmptyResult(Vcs::answering(std::move(onDone), record)));
}

Vcs::Query HgRepository::commitFiles(const QString& sha, const QObject* context, Vcs::Answer<std::vector<CommitFileChange>> onDone)
{
	// An .hgsubstate row is replaced by one row per subrepo pointer move, read from its diff
	Vcs::Query query;
	query.attach(Hg::run(path(), { QStringLiteral("status"), QStringLiteral("--change"), sha, QStringLiteral("-C"),
			QStringLiteral("-T"), QStringLiteral("json") }, context,
		[this, query, sha, context, onDone = std::move(onDone)](const ProcessResult& result) mutable {
			if (!result.ok)
			{
				onDone(std::unexpected(result.errorText()));
				return;
			}

			std::vector<CommitFileChange> entries = Hg::parseStatus(result.out);
			const auto substate = std::ranges::find_if(entries,
				[](const CommitFileChange& entry) { return entry.path == QLatin1String(".hgsubstate"); });
			if (substate == entries.end())
			{
				onDone(std::move(entries));
				return;
			}
			const qsizetype insertAt = substate - entries.begin(); // the subrepo rows take .hgsubstate's place
			entries.erase(substate);

			query.attach(Hg::run(path(), substateDiffArgs(sha), context,
				[entries = std::move(entries), insertAt, onDone = std::move(onDone)](const ProcessResult& diffResult) mutable {
					if (!diffResult.ok)
					{
						onDone(std::unexpected(diffResult.errorText()));
						return;
					}

					std::vector<CommitFileChange> rows;
					for (Hg::SubrepoPointerChange& change : Hg::parseSubstateDiff(diffResult.out))
					{
						const ChangeType type = change.oldNode.isEmpty() ? ChangeType::Added
							: change.newNode.isEmpty() ? ChangeType::Deleted : ChangeType::Modified;
						QString node = change.newNode.isEmpty() ? std::move(change.oldNode) : std::move(change.newNode);
						rows.push_back({ .type = type, .path = std::move(change.path), .isSubmodule = true,
							.submoduleSha = std::move(node) });
					}
					entries.insert(entries.begin() + insertAt, rows.begin(), rows.end());
					onDone(std::move(entries));
			}));
		}));
	return query;
}

Vcs::Query HgRepository::commitFileCounts(const QString& sha, const QObject* context, Vcs::Answer<std::map<QString, LineCounts>> onDone)
{
	// hg has no --numstat, and `diff --stat` scales its bars to the widest file, so the counts come from the
	// diff lines themselves
	QStringList args = contextFreeDiffArgs();
	args << QStringLiteral("-c") << sha;
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), Hg::parseDiffCounts));
}

Vcs::Query HgRepository::commitFileDiff(const QString& sha, const CommitFileChange& file, const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	// A subrepo row's diff is its pointer move, read from .hgsubstate's diff
	if (file.isSubmodule)
	{
		const auto pointerMove = [path = file.path](const QByteArray& diff) {
			for (const Hg::SubrepoPointerChange& change : Hg::parseSubstateDiff(diff))
			{
				if (change.path == path)
					return pointerMoveText(change);
			}
			return QByteArray{};
		};
		return runQuery(path(), substateDiffArgs(sha), context, Vcs::answering(std::move(onDone), pointerMove));
	}

	QStringList args = diffArgs();
	args << QStringLiteral("-c") << sha << QStringLiteral("--") << file.path;
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query HgRepository::fileAtRevision(const QString& sha, const QString& repoRelativePath, const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	// --decode applies the [decode] filters: the bytes arrive as a working tree would hold them
	return runQuery(path(), { QStringLiteral("cat"), QStringLiteral("--decode"), QStringLiteral("-r"), sha,
		QStringLiteral("--"), repoRelativePath }, context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query HgRepository::unpushedCommits(const QObject* context, Vcs::Answer<QSet<QString>> onDone)
{
	// Every draft changeset, wherever it sits; the header's count covers only what one push would send
	const auto nodes = [](const QByteArray& output) {
		QSet<QString> shas;
		for (const CommitRecord& commit : Hg::parseCommitLog(output))
			shas.insert(commit.sha);
		return shas;
	};
	return runQuery(path(), { QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("draft()"),
		QStringLiteral("-T"), QStringLiteral("json") }, context, Vcs::answering(std::move(onDone), nodes));
}

Vcs::Query HgRepository::submodulePointerLog(const QString& repoRelativePath, const QObject* context, Vcs::Answer<QString> onDone)
{
	const auto recorded = _subrepoNodes.find(repoRelativePath);
	if (recorded == _subrepoNodes.end())
	{
		QTimer::singleShot(0, context, [onDone = std::move(onDone), path = repoRelativePath] {
			onDone(std::unexpected(QStringLiteral("'%1' is not recorded in .hgsubstate.").arg(path)));
		});
		return {};
	}

	const QString workDir = QDir{ path() }.filePath(repoRelativePath);
	if (isGitSubrepo(repoRelativePath))
	{
		return Vcs::Query{ Git::run(workDir, { QStringLiteral("log"), QStringLiteral("--oneline"), QStringLiteral("--no-decorate"),
			recorded->second + QStringLiteral("..HEAD") }, context, Vcs::answering(std::move(onDone), outputAsText), {}, /*readOnlyQuery=*/true) };
	}

	// only(., X): what the working directory's parent has and X does not
	return Vcs::Query{ Hg::run(workDir, { QStringLiteral("log"), QStringLiteral("-r"),
		QStringLiteral("only(., %1)").arg(recorded->second), QStringLiteral("-T"), QStringLiteral("{node|short} {desc|firstline}\n") },
		context, Vcs::answering(std::move(onDone), outputAsText)) };
}

RepositoryLocation HgRepository::submoduleLocation(const QString& repoRelativePath) const
{
	return { isGitSubrepo(repoRelativePath) ? VcsKind::Git : VcsKind::Mercurial, QDir{ path() }.filePath(repoRelativePath) };
}

QString HgRepository::ignoreFileName() const
{
	return QStringLiteral(".hgignore");
}

std::vector<IgnorePattern> HgRepository::ignorePatternsFor(const QString& repoRelativePath) const
{
	const qsizetype slash = repoRelativePath.lastIndexOf(QLatin1Char('/'));
	const QString fileName = repoRelativePath.mid(slash + 1);
	const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));

	std::vector<IgnorePattern> patterns;
	patterns.push_back({ escapedForHgIgnore(repoRelativePath), IgnoreScope::ExactPath });
	if (dot > 0 && dot < fileName.size() - 1)
		patterns.push_back({ QStringLiteral("*.") + escapedForHgIgnore(fileName.mid(dot + 1)), IgnoreScope::Extension });
	patterns.push_back({ escapedForHgIgnore(fileName), IgnoreScope::Name });
	if (slash >= 0)
	{
		// No trailing slash: hg matches any leading part of a path, so the directory itself is the pattern
		patterns.push_back({ escapedForHgIgnore(repoRelativePath.left(slash)), IgnoreScope::Directory });
	}
	return patterns;
}

QByteArray HgRepository::ignoreFileWithPatternAdded(QByteArray content, const IgnorePattern& pattern) const
{
	// In a glob section a path matches in any directory; only rootglob anchors it to the repository root
	const bool rooted = pattern.scope == IgnoreScope::ExactPath || pattern.scope == IgnoreScope::Directory;
	const QByteArray section = rooted ? QByteArrayLiteral("rootglob") : QByteArrayLiteral("glob");
	const QByteArray eol = content.contains("\r\n") ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");

	// The end of the last section of the right kind. Lines before the first `syntax:` are regular
	// expressions, so there is nothing to extend until one is declared.
	qsizetype insertAt = -1;
	bool inSection = false;
	for (qsizetype lineStart = 0; lineStart < content.size(); )
	{
		const qsizetype newline = content.indexOf('\n', lineStart);
		const qsizetype lineEnd = newline < 0 ? content.size() : newline + 1;
		const QByteArray line = content.mid(lineStart, lineEnd - lineStart).trimmed();

		if (line.startsWith("syntax:"))
			inSection = line.mid(7).trimmed().toLower() == section;
		if (inSection)
			insertAt = lineEnd;

		lineStart = lineEnd;
	}

	if (insertAt >= 0)
	{
		QByteArray addition;
		if (insertAt > 0 && content.at(insertAt - 1) != '\n')
			addition += eol; // the section's last line is unterminated
		addition += Hg::localBytes(pattern.text) + eol;
		content.insert(insertAt, addition);
		return content;
	}

	if (!content.isEmpty() && !content.endsWith('\n'))
		content += eol;
	return content + "syntax: " + section + eol + Hg::localBytes(pattern.text) + eol;
}

void HgRepository::launchExternalDiffTool(const QString& repoRelativePath) const
{
	// extdiff ships with hg but is off by default; which tool it starts is the user's `[extdiff]` configuration
	QString executable = CSettings{}.value(Settings::HgExecutableKey).toString();
	if (executable.isEmpty())
		executable = QLatin1String(Settings::HgExecutableDefault);
	QProcess::startDetached(executable,
		Hg::invariantArgs() + QStringList{ QStringLiteral("--config"), QStringLiteral("extensions.extdiff="),
			QStringLiteral("extdiff"), QStringLiteral("--"), repoRelativePath },
		path());
}
