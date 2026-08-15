#include "hgrepository.h"
#include "gitprocess.h"
#include "hgparsers.h"
#include "hgprocess.h"
#include "queryround.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>

#include <functional>
#include <utility>

namespace {

constexpr int MaxUnpushedLogEntries = 30; // tooltip fodder; state.ahead carries the true count
constexpr char GitSubrepoPrefix[] = "[git]";

QByteArray fileContents(const QString& path)
{
	QFile file{ path };
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

// A path list as hg takes one: a pattern naming a file of NUL-separated paths, so a long list never
// reaches the command line
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

// hg says "nothing to report" with exit code 1: no incoming changesets, none outgoing, no search hits,
// nothing to push. Left uncorrected, the ordinary empty answer would reach the user as a failure.
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

// The base of every commit-listing query; the walk itself is whatever the caller appends
QStringList commitLogArgs(int maxCommits)
{
	return { QStringLiteral("log"), QStringLiteral("-l"), QString::number(maxCommits),
		QStringLiteral("-T"), QStringLiteral("json") };
}

// -f follows the one path across renames. It closes the argument list, so everything else is in place first.
void appendPathLimit(QStringList& args, const Repository::LogQuery& query)
{
	if (!query.path.isEmpty())
		args << QStringLiteral("-f") << QStringLiteral("--") << query.path;
}

// `hg grep` has no fixed-string mode, so the search term reaches it as a pattern whatever it holds
QStringList grepArgs(const Repository::LogQuery& query)
{
	QStringList args = { QStringLiteral("grep"), QStringLiteral("--diff"), QStringLiteral("-T"), QStringLiteral("json") };
	if (query.ignoreCase)
		args << QStringLiteral("-i");
	args << escapedForRegex(query.contentSearch);
	if (!query.path.isEmpty())
		args << QStringLiteral("--") << query.path;
	return args;
}

// The changesets a search found, newest first and capped, as one revset. hg would otherwise list a union
// in its own order, which is the opposite of the one the log view shows.
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

// The diff of one changeset or of the working tree. --git names renames and binary files instead of
// printing an add of every line; -Z is display only, the same choice as git's --ignore-cr-at-eol - the row
// still lists as modified and still commits its working-tree content verbatim.
QStringList diffArgs()
{
	return { QStringLiteral("diff"), QStringLiteral("--git"), QStringLiteral("-Z") };
}

// The same diff with no context lines: an unchanged line is neither a word for the pool nor a line to count
QStringList contextFreeDiffArgs()
{
	QStringList args = diffArgs();
	args << QStringLiteral("-U") << QStringLiteral("0");
	return args;
}

} // namespace

struct HgRepository::RefreshRun
{
	std::vector<CommitFileChange> status;
	std::map<QString, LineCounts> changeCounts; // by path; only the files with countable lines have one
	std::vector<CommitRecord> head; // `.`, which is the null changeset in an unborn repository
	Hg::WorkingDirectory workingDir;
	bool hasDefaultPath = false;
	std::vector<CommitRecord> unpushed; // the drafts `push -r .` would send
	QStringList conflicted; // paths still unresolved; only asked for while a mergestate exists
	std::map<QString, QString> subrepoNodes; // the changeset each subrepo is actually on; absent if unread

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

	// The parent's own record of its subrepos, re-read before the first process: it decides whether status
	// has to recurse, which of the paths it then reports belong to a subrepo rather than to the parent, and
	// which tool answers for each one.
	_subrepoNodes = Hg::parseSubrepoState(fileContents(QDir{ path() }.filePath(QStringLiteral(".hgsubstate"))));
	_subrepoSources = Hg::parseSubrepoSources(fileContents(QDir{ path() }.filePath(QStringLiteral(".hgsub"))));

	QueryRound round{ refreshQueries(), [self = QPointer<HgRepository>{ this }, run] {
		if (self)
			self->startDependentQueries(run);
	} };

	QStringList statusArgs = { QStringLiteral("status"), QStringLiteral("-C"), QStringLiteral("-T"), QStringLiteral("json") };
	if (!_subrepoNodes.empty())
		statusArgs << QStringLiteral("-S"); // recursing is the only way to learn what is going on inside a subrepo
	round.launch(path(), statusArgs,
		[run](const ProcessResult& r) {
			if (r.ok)
				run->status = Hg::parseStatus(r.out);
			else
				run->noteFailure(r);
		});
	// Losing this costs the rows their counts, which is a shorter answer rather than a wrong one
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
	// The working directory is its own revision: it carries the branch a commit would land on, which after
	// `hg branch` the parent changeset does not yet, and two parents while a merge is uncommitted
	round.launch(path(), { QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("wdir()"), QStringLiteral("-T"), QStringLiteral("json") },
		[run](const ProcessResult& r) {
			if (r.ok)
				run->workingDir = Hg::parseWorkingDirectory(r.out);
			else
				run->noteFailure(r); // unread, a merge in progress is indistinguishable from none
		});
	// No default path is an answer rather than a failure: this repository has nowhere to push to
	round.launch(path(), { QStringLiteral("paths"), QStringLiteral("default") },
		[run](const ProcessResult& r) { run->hasDefaultPath = r.ok; });
}

// The queries the first round's answers call for: the unpushed set, which is only worth walking where
// there is a remote to be ahead of, and what each subrepo is actually on.
void HgRepository::startDependentQueries(const std::shared_ptr<RefreshRun>& run)
{
	QueryRound round{ refreshQueries(), [self = QPointer<HgRepository>{ this }] {
		if (self)
			self->finishRefresh();
	} };

	// A draft changeset is one no remote has seen, limited here to the ancestors of the parent because that
	// is what `push -r .` sends and this count stands next to that button. Without a remote every changeset
	// is draft, so the question is only asked where its answer means something.
	if (run->hasDefaultPath)
	{
		round.launch(path(), { QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("draft() and ::."),
				QStringLiteral("-T"), QStringLiteral("json") },
			[run](const ProcessResult& r) { run->unpushed = Hg::parseCommitLog(r.out); });
	}

	// `status` calls a conflicted file modified, so only the mergestate knows better. Every command that can
	// conflict leaves one - merge, graft, rebase, an update over local changes - and it lives until the
	// resolve is committed or aborted, so its absence is a complete answer.
	if (QFileInfo::exists(QDir{ path() }.filePath(QStringLiteral(".hg/merge"))))
	{
		round.launch(path(), { QStringLiteral("resolve"), QStringLiteral("--list"), QStringLiteral("-T"), QStringLiteral("json") },
			[run](const ProcessResult& r) { run->conflicted = Hg::parseUnresolvedPaths(r.out); });
	}

	for (const auto& subrepo : _subrepoNodes)
	{
		const QString& subPath = subrepo.first;
		const QString workDir = QDir{ path() }.filePath(subPath);
		if (!QFileInfo::exists(workDir))
			continue; // never cloned: there is nothing inside to ask, and nothing inside to lose

		if (isGitSubrepo(subPath))
		{
			round.launch(workDir, { QStringLiteral("rev-parse"), QStringLiteral("HEAD") },
				[run, subPath = subPath](const ProcessResult& r) {
					if (r.ok)
						run->subrepoNodes[subPath] = QString::fromUtf8(r.out.trimmed());
				});
			continue;
		}
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
	// Mercurial has no nameless state for a branch to be in, and no bookmarks are read: this is the named
	// branch, `default` unless another was created
	state.branch = run.workingDir.branch;
	// One name for the whole remote, which is as far as hg's own vocabulary goes - there is no per-branch
	// upstream, and `default` is what a push contacts
	state.upstream = run.hasDefaultPath ? QStringLiteral("default") : QString{};
	state.ahead = int(run.unpushed.size());
	state.behind = _behind;

	for (const CommitRecord& commit : run.unpushed)
	{
		if (state.unpushedSubjects.size() >= MaxUnpushedLogEntries)
			break;
		state.unpushedSubjects << commit.subject();
	}

	// An uncommitted merge is the working directory having two parents. Rebase, graft and histedit leave
	// state of their own that this does not read.
	if (run.workingDir.parents.size() > 1)
		state.op = RepoOp::Merge;
	return state;
}

std::vector<FileEntry> HgRepository::filesFromRun(const RefreshRun& run) const
{
	std::map<QString, SubmoduleContent> content;
	std::vector<FileEntry> files;

	for (const CommitFileChange& change : run.status)
	{
		// A recursing status reports the files inside a subrepo by their paths in the parent. They are not
		// the parent's rows: what the parent can act on is the subrepo as a whole.
		if (const QString subPath = enclosingSubrepo(change.path); !subPath.isEmpty())
		{
			SubmoduleContent& inside = content[subPath];
			if (change.type != ChangeType::Untracked)
				inside = SubmoduleContent::DirtyTracked;
			else if (inside == SubmoduleContent::Clean)
				inside = SubmoduleContent::Untracked;
			continue;
		}

		const ChangeType type = run.conflicted.contains(change.path) ? ChangeType::Conflicted : change.type;
		FileEntry entry{ .path = change.path, .oldPath = change.oldPath, .type = type };
		if (const auto counted = run.changeCounts.find(change.path); counted != run.changeCounts.end())
			entry.lineCounts = counted->second;
		files.push_back(std::move(entry));
	}

	for (const auto& [subPath, recordedNode] : _subrepoNodes)
	{
		if (!QFileInfo::exists(QDir{ path() }.filePath(subPath)))
			continue; // never cloned: nothing inside to have moved, and nothing inside to lose

		const auto currentNode = run.subrepoNodes.find(subPath);
		FileEntry entry{ .path = subPath, .isSubmodule = true, .content = content[subPath] };
		if (currentNode == run.subrepoNodes.end())
		{
			// It is there but would not say what it is on, so whether the pointer moved is unknown - and an
			// unknown pointer is as good a reason to refuse to commit it as dirty content is
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

QString HgRepository::enclosingSubrepo(const QString& repoRelativePath) const
{
	for (const auto& subrepo : _subrepoNodes)
	{
		const QString& subPath = subrepo.first;
		if (repoRelativePath == subPath || repoRelativePath.startsWith(subPath + QLatin1Char('/')))
			return subPath;
	}
	return {};
}

bool HgRepository::isGitSubrepo(const QString& subrepoPath) const
{
	const auto source = _subrepoSources.find(subrepoPath);
	return source != _subrepoSources.end() && source->second.startsWith(QLatin1String(GitSubrepoPrefix));
}

// Every refresh query is scoped to the repository that asked, and run by whichever tool owns the directory
// it is pointed at: a subrepo of an hg repository may be a git one, and its arguments are then git's.
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
	return Vcs::openTempFile(Vcs::nulJoined(paths), QStringLiteral("pathspec"), this, onFailure);
}

void HgRepository::commit(const QString& message, const QStringList& pathspec, const QStringList& /*untrackedPaths*/, Vcs::Answer<void> onDone)
{
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto messageFile = Vcs::openTempFile(message.toUtf8(), QStringLiteral("commit message"), this, report);
	if (!messageFile)
		return;
	const auto pathspecFile = openPathspecFile(pathspec, report);
	if (!pathspecFile)
		return;

	// -A marks the new and the missing files within the pathspec, and only within it - a missing file
	// cannot be committed by name otherwise, and the ticked untracked rows need it. So the untracked paths
	// need no step of their own here, and none of their own to undo: a commit that fails rolls its own -A
	// back with the rest of the transaction.
	Hg::run(path(), { QStringLiteral("commit"), QStringLiteral("-l"), messageFile->fileName(),
			QStringLiteral("-A"), listfilePattern(pathspecFile) }, this,
		[messageFile, pathspecFile, report](const ProcessResult& result) { report(result); });
}

void HgRepository::commitMergeState(const QString& message, const QStringList& untrackedPaths, Vcs::Answer<void> onDone)
{
	const Vcs::Callback report = Vcs::reporting(std::move(onDone));
	const auto messageFile = Vcs::openTempFile(message.toUtf8(), QStringLiteral("commit message"), this, report);
	if (!messageFile)
		return;

	const auto runCommit = [this, messageFile, untrackedPaths, report] {
		// No pathspec: a merge cannot be committed in parts. -A is out for the same reason - with nothing to
		// scope it, it would sweep in every unknown file rather than the ticked ones.
		Hg::run(path(), { QStringLiteral("commit"), QStringLiteral("-l"), messageFile->fileName() }, this,
			[this, messageFile, untrackedPaths, report](const ProcessResult& result) {
				// A commit that failed after the add leaves those paths tracked, and the rows would then read
				// Added rather than Untracked. The reported result stays the commit's: the forget's says
				// nothing about why the commit was refused.
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

Vcs::Job* HgRepository::push(Vcs::Callback onDone)
{
	return Hg::run(path(), { QStringLiteral("push"), QStringLiteral("-r"), QStringLiteral(".") }, this,
		tolerantOfEmptyResult(std::move(onDone)));
}

Vcs::Job* HgRepository::pushSetUpstream(Vcs::Callback onDone)
{
	return push(std::move(onDone));
}

QString HgRepository::pushCommandLabel(bool /*setUpstream*/) const
{
	return QStringLiteral("hg push -r .");
}

void HgRepository::fetch(Vcs::Answer<void> onDone)
{
	// Mercurial keeps no remote-tracking state, so there is nothing for a fetch to move: what the remote
	// holds is read by incomingCommits(), at the moment it is asked for. Answered from the event loop, as
	// every other operation answers.
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

	// -C, or every reverted file leaves a .orig copy behind - which the next refresh would list as untracked
	Hg::run(path(), { QStringLiteral("revert"), QStringLiteral("-C"), QStringLiteral("--rev"), QStringLiteral("."),
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
		onDone(std::unexpected(QStringLiteral("Mercurial has no remote branches to create a local branch from.")));
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
	// The rename's source needs no naming: hg records a copy rather than inferring one, so --git prints the
	// old path from what it already knows
	QStringList args = diffArgs();
	if (entry.type == ChangeType::Deleted)
	{
		// A file gone from disk stays in the dirstate until its removal is recorded, and the dirstate is what
		// the working directory means to `hg diff`: it would report no change at all. The parent against the
		// null revision is the removal this row commits, and the same answer for an already `hg remove`d file.
		args << QStringLiteral("-r") << QStringLiteral(".") << QStringLiteral("-r") << QStringLiteral("null");
	}
	args << QStringLiteral("--") << entry.path;
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query HgRepository::diffAllChanges(const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	// -Z, or a wholesale line-ending conversion floods the word pool with every line of the file
	return runQuery(path(), contextFreeDiffArgs(), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query HgRepository::commitLog(const LogQuery& query, const QObject* context, Vcs::Answer<std::vector<CommitRecord>> onDone)
{
	if (query.contentSearch.isEmpty())
	{
		QStringList args = commitLogArgs(query.maxCommits);
		appendPathLimit(args, query);
		return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), Hg::parseCommitLog));
	}

	// A search names changesets and nothing else - what the listing shows of each one is a second query
	// over what the first found. A callback only runs while its context lives, so this one can hand the
	// same context to that query.
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
	// The same search as the listing's, read for the narrower thing: a changeset whose matching lines added
	// and removed differ in number genuinely gained or lost the text, rather than editing around it
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
	// The one query that goes to the network for what the remote has. What it finds is also the only
	// answer this backend ever gets to the header's behind count, so it is kept for the next refresh.
	const auto record = [this](const QByteArray& output) {
		std::vector<CommitRecord> commits = Hg::parseCommitLog(output);
		_behind = int(commits.size());
		return commits;
	};
	return runQuery(path(), { QStringLiteral("incoming"), QStringLiteral("-l"), QString::number(maxCommits),
		QStringLiteral("-T"), QStringLiteral("json") }, context,
		tolerantOfEmptyResult(Vcs::answering(std::move(onDone), record)));
}

Vcs::Query HgRepository::commitFiles(const QString& sha, const QObject* context, Vcs::Answer<std::vector<CommitFileChange>> onDone)
{
	return runQuery(path(), { QStringLiteral("status"), QStringLiteral("--change"), sha, QStringLiteral("-C"),
		QStringLiteral("-T"), QStringLiteral("json") }, context, Vcs::answering(std::move(onDone), Hg::parseStatus));
}

Vcs::Query HgRepository::commitFileCounts(const QString& sha, const QObject* context, Vcs::Answer<std::map<QString, LineCounts>> onDone)
{
	// Mercurial has no --numstat, and `diff --stat` scales its bar to the widest file in the set, so the
	// split it shows is not a count. The lines themselves are, and -U0 leaves nothing else in the output.
	QStringList args = contextFreeDiffArgs();
	args << QStringLiteral("-c") << sha;
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), Hg::parseDiffCounts));
}

Vcs::Query HgRepository::commitFileDiff(const QString& sha, const CommitFileChange& file, const QObject* context, Vcs::Answer<QByteArray> onDone)
{
	QStringList args = diffArgs();
	args << QStringLiteral("-c") << sha << QStringLiteral("--") << file.path;
	return runQuery(path(), std::move(args), context, Vcs::answering(std::move(onDone), std::identity{}));
}

Vcs::Query HgRepository::unpushedCommits(const QObject* context, Vcs::Answer<QSet<QString>> onDone)
{
	// Local and free: a draft changeset is one no remote has seen. Every draft, wherever it sits - unlike the
	// header's count, which is what one push would send.
	const auto nodes = [](const QByteArray& output) {
		QSet<QString> shas;
		for (const CommitRecord& commit : Hg::parseCommitLog(output))
			shas.insert(commit.sha);
		return shas;
	};
	return runQuery(path(), { QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("draft()"),
		QStringLiteral("-T"), QStringLiteral("json") }, context, Vcs::answering(std::move(onDone), nodes));
}

Vcs::Query HgRepository::submodulePointerLog(const FileEntry& entry, const QObject* context, Vcs::Answer<QString> onDone)
{
	const auto recorded = _subrepoNodes.find(entry.path);
	if (recorded == _subrepoNodes.end())
	{
		QTimer::singleShot(0, context, [onDone = std::move(onDone), path = entry.path] {
			onDone(std::unexpected(QStringLiteral("'%1' is not recorded in .hgsubstate.").arg(path)));
		});
		return {};
	}

	const QString workDir = QDir{ path() }.filePath(entry.path);
	if (isGitSubrepo(entry.path))
	{
		// A subrepo of an hg repository may be a git one, and the commits its pointer pulls in are then
		// git's to list
		return Vcs::Query{ Git::run(workDir, { QStringLiteral("log"), QStringLiteral("--oneline"), QStringLiteral("--no-decorate"),
			recorded->second + QStringLiteral("..HEAD") }, context, Vcs::answering(std::move(onDone), outputAsText), {}, /*readOnlyQuery=*/true) };
	}

	// only(., X) is what the working directory's parent has and X does not
	return Vcs::Query{ Hg::run(workDir, { QStringLiteral("log"), QStringLiteral("-r"),
		QStringLiteral("only(., %1)").arg(recorded->second), QStringLiteral("-T"), QStringLiteral("{node|short} {desc|firstline}\n") },
		context, Vcs::answering(std::move(onDone), outputAsText)) };
}

RepositoryLocation HgRepository::submoduleLocation(const FileEntry& entry) const
{
	// .hgsub may name a subrepo of another kind, and a window on one is that kind's window
	return { isGitSubrepo(entry.path) ? VcsKind::Git : VcsKind::Mercurial, QDir{ path() }.filePath(entry.path) };
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
		// No trailing slash: hg excludes a file when any leading part of its path matches, so the directory
		// itself is the whole pattern
		patterns.push_back({ escapedForHgIgnore(repoRelativePath.left(slash)), IgnoreScope::Directory });
	}
	return patterns;
}

QByteArray HgRepository::ignoreFileWithPatternAdded(QByteArray content, const IgnorePattern& pattern) const
{
	// A path only means "at the repository root" in a rootglob section; in a glob section it matches that
	// relative path in any directory, which is the opposite of what the two rooted scopes offer
	const bool rooted = pattern.scope == IgnoreScope::ExactPath || pattern.scope == IgnoreScope::Directory;
	const QByteArray section = rooted ? QByteArrayLiteral("rootglob") : QByteArrayLiteral("glob");
	const QByteArray eol = content.contains("\r\n") ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\n");

	// Where the section this pattern belongs to ends. Everything before the first `syntax:` line is read as
	// regular expressions, so there is no section to extend until one has been declared.
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
			insertAt = lineEnd; // the last line of the section so far, its own declaration included

		lineStart = lineEnd;
	}

	if (insertAt >= 0)
	{
		QByteArray addition;
		if (insertAt > 0 && content.at(insertAt - 1) != '\n')
			addition += eol; // the section's last line ends the file, and unterminated
		addition += pattern.text.toUtf8() + eol;
		content.insert(insertAt, addition);
		return content;
	}

	if (!content.isEmpty() && !content.endsWith('\n'))
		content += eol;
	return content + "syntax: " + section + eol + pattern.text.toUtf8() + eol;
}

void HgRepository::launchExternalDiffTool(const QString& repoRelativePath) const
{
	// extdiff ships with hg but is off unless enabled, and which tool it starts is the user's `[extdiff]`
	// configuration - exactly as which tool `git difftool` starts is theirs
	QProcess::startDetached(Settings::hgExecutable(),
		Hg::invariantArgs() + QStringList{ QStringLiteral("--config"), QStringLiteral("extensions.extdiff="),
			QStringLiteral("extdiff"), QStringLiteral("--"), repoRelativePath },
		path());
}
