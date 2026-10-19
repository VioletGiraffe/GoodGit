#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "gitrepository.h"
#include "repositorytestutils.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <expected>
#include <utility>
#include <vector>

namespace {

// A git repository in a temporary directory, built by running git itself.
// Neither the fixture commands nor the backend under test read the machine's global or system git config.
class ScratchRepository
{
public:
	ScratchRepository()
	{
		REQUIRE(_directory.isValid());
		useEmptyGitConfig(_directory.filePath(QStringLiteral("empty.gitconfig")));

		QDir{}.mkpath(root());
		git({ QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("main") });
	}

	[[nodiscard]] QString root() const { return _directory.filePath(QStringLiteral("repository")); }

	void write(const QString& relativePath, const QByteArray& content) const { writeFile(root(), relativePath, content); }

	// Runs in `relativeDirectory` under the root, and must succeed
	void git(const QStringList& arguments, const QString& relativeDirectory = {}) const
	{
		const auto [exitCode, output] = runGit(arguments, relativeDirectory);
		INFO(output.toStdString());
		REQUIRE(exitCode == 0);
	}

	// For the commands expected to fail, such as a merge that stops on conflicts
	void gitMayFail(const QStringList& arguments) const { (void)runGit(arguments, {}); }

	// A nested repository with one commit, at `relativePath`, not known to the outer one
	void createNestedRepository(const QString& relativePath) const
	{
		write(relativePath + QStringLiteral("/inside.txt"), "inside\n");
		git({ QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("main") }, relativePath);
		git({ QStringLiteral("add"), QStringLiteral("inside.txt") }, relativePath);
		git({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("nested") }, relativePath);
	}

	void commitAll(const QString& message) const
	{
		git({ QStringLiteral("add"), QStringLiteral("-A") });
		git({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), message });
	}

	// The backend's rows and state after one refresh, sorted by path
	[[nodiscard]] std::pair<std::vector<FileEntry>, RepoState> refreshed() const
	{
		GitRepository repository{ root() };
		return refreshedRowsAndState(repository);
	}

	// Requires the query to succeed
	[[nodiscard]] QString historyFingerprint() const
	{
		GitRepository repository{ root() };
		return requireSuccess(awaitAnswer<QString>([&](Vcs::Answer<QString> onDone) { repository.historyFingerprint(&repository, std::move(onDone)); }));
	}

	// One commit made the way the window makes it, over a refreshed repository: the commit's diff baseline
	// comes from the state
	void commitThroughBackend(const QString& message, const QStringList& pathspec) const
	{
		// The backend runs git without the -c arguments the fixture's own commands carry
		git({ QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("GoodGit tests") });
		git({ QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("tests@goodgit.invalid") });

		GitRepository repository{ root() };
		refreshToCompletion(repository);
		requireSuccess(awaitAnswer<void>([&](Vcs::Answer<void> onDone) { repository.commit(message, pathspec, {}, std::move(onDone)); }));
	}

	// Takes a candidate the way the window's strip button does
	void reattachThroughBackend(const ReattachCandidate& candidate) const
	{
		GitRepository repository{ root() };
		requireSuccess(awaitAnswer<void>([&](Vcs::Answer<void> onDone) { repository.reattachHead(candidate, std::move(onDone)); }));
	}

	// main tracks origin/main, whose ref the caller writes. Nothing is ever fetched: the upstream config and the
	// remote-tracking refs are all the branch listings read.
	void setUpstreamOfMainToOriginMain() const
	{
		git({ QStringLiteral("config"), QStringLiteral("remote.origin.url"), QStringLiteral("https://example.invalid/r.git") });
		git({ QStringLiteral("config"), QStringLiteral("remote.origin.fetch"), QStringLiteral("+refs/heads/*:refs/remotes/origin/*") });
		git({ QStringLiteral("config"), QStringLiteral("branch.main.remote"), QStringLiteral("origin") });
		git({ QStringLiteral("config"), QStringLiteral("branch.main.merge"), QStringLiteral("refs/heads/main") });
	}

	[[nodiscard]] QString sha(const QString& revision) const
	{
		return gitOutput({ QStringLiteral("rev-parse"), revision }).trimmed();
	}

	// The branch HEAD is on, or "HEAD" when detached
	[[nodiscard]] QString headName() const
	{
		return gitOutput({ QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD") }).trimmed();
	}

	// What the index holds beyond HEAD, as `<letter>\t<path>` lines in path order
	[[nodiscard]] QString stagedAgainstHead() const
	{
		return gitOutput({ QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--name-status"), QStringLiteral("HEAD") });
	}

	[[nodiscard]] QString stagedContent(const QString& relativePath) const
	{
		return gitOutput({ QStringLiteral("show"), QStringLiteral(":") + relativePath });
	}

private:
	[[nodiscard]] QString gitOutput(const QStringList& arguments) const
	{
		const auto [exitCode, output] = runGit(arguments, {});
		INFO(output.toStdString());
		REQUIRE(exitCode == 0);
		return output;
	}

	[[nodiscard]] std::pair<int, QString> runGit(const QStringList& arguments, const QString& relativeDirectory) const
	{
		const QStringList identity{ QStringLiteral("-c"), QStringLiteral("user.name=GoodGit tests"), QStringLiteral("-c"),
			QStringLiteral("user.email=tests@goodgit.invalid") };
		return runProcess(QStringLiteral("git"), identity + arguments, QDir{ root() }.filePath(relativeDirectory));
	}

	QTemporaryDir _directory;
};

} // namespace

TEST_CASE("Untracked files are listed one by one, an ignored folder not at all, a nested repository as one row", "[git]")
{
	const ScratchRepository scratch;
	scratch.write(QStringLiteral(".gitignore"), "ignored/\n");
	scratch.commitAll(QStringLiteral("initial"));

	scratch.write(QStringLiteral("plain/a.txt"), "a\n");
	scratch.write(QStringLiteral("plain/deeper/b.txt"), "b\n");
	scratch.write(QStringLiteral("ignored/x.log"), "x\n");
	scratch.createNestedRepository(QStringLiteral("nested"));

	const auto [files, state] = scratch.refreshed();

	REQUIRE(pathsOf(files) == QStringList{ QStringLiteral("nested"), QStringLiteral("plain/a.txt"), QStringLiteral("plain/deeper/b.txt") });
	for (const FileEntry& file : files)
	{
		INFO(file.path.toStdString());
		CHECK(file.type == ChangeType::Untracked);
		CHECK_FALSE(file.isSubmodule);
		CHECK(file.isUntrackedRepository == (file.path == QStringLiteral("nested")));
	}
}

TEST_CASE("A staged nested repository is an added submodule row, not an untracked one", "[git]")
{
	const ScratchRepository scratch;
	scratch.write(QStringLiteral("committed.txt"), "committed\n");
	scratch.commitAll(QStringLiteral("initial"));

	// The index state `git submodule add` leaves: a gitlink and .gitmodules, both staged
	scratch.createNestedRepository(QStringLiteral("sub"));
	scratch.write(QStringLiteral(".gitmodules"), "[submodule \"sub\"]\n\tpath = sub\n\turl = ./sub\n");
	scratch.git({ QStringLiteral("add"), QStringLiteral(".gitmodules"), QStringLiteral("sub") });

	const auto [files, state] = scratch.refreshed();

	REQUIRE(pathsOf(files) == QStringList{ QStringLiteral(".gitmodules"), QStringLiteral("sub") });
	CHECK(files[0].type == ChangeType::Added);
	CHECK_FALSE(files[0].isSubmodule);

	const FileEntry& submodule = files[1];
	CHECK(submodule.type == ChangeType::Added);
	CHECK(submodule.isSubmodule);
	CHECK(submodule.pointerMoved);
	CHECK_FALSE(submodule.isUntrackedRepository);
	CHECK(submodule.committable());
}

TEST_CASE("A submodule gets a row for modified files inside, and none for untracked files alone", "[git]")
{
	const ScratchRepository scratch;
	scratch.createNestedRepository(QStringLiteral("sub"));
	scratch.write(QStringLiteral(".gitmodules"), "[submodule \"sub\"]\n\tpath = sub\n\turl = ./sub\n");
	scratch.commitAll(QStringLiteral("with a submodule"));

	SECTION("untracked files inside")
	{
		scratch.write(QStringLiteral("sub/untracked.txt"), "new\n");
		CHECK(scratch.refreshed().first.empty());
	}

	SECTION("a modified tracked file inside")
	{
		scratch.write(QStringLiteral("sub/inside.txt"), "changed\n");
		const auto [files, state] = scratch.refreshed();

		REQUIRE(files.size() == 1);
		const FileEntry& submodule = files[0];
		CHECK(submodule.path == QStringLiteral("sub"));
		CHECK(submodule.isSubmodule);
		CHECK_FALSE(submodule.pointerMoved);
		CHECK(submodule.content == SubmoduleContent::DirtyTracked);
		CHECK_FALSE(submodule.committable());
	}
}

TEST_CASE("Discarding a moved submodule pointer checks the recorded commit out inside it", "[git]")
{
	const ScratchRepository scratch;
	scratch.createNestedRepository(QStringLiteral("sub"));
	scratch.write(QStringLiteral(".gitmodules"), "[submodule \"sub\"]\n\tpath = sub\n\turl = ./sub\n");
	scratch.commitAll(QStringLiteral("with a submodule"));
	// Active, as `git submodule init` leaves it: restore recurses only into an active submodule
	scratch.git({ QStringLiteral("config"), QStringLiteral("submodule.sub.url"), QStringLiteral("./sub") });

	scratch.write(QStringLiteral("sub/inside.txt"), "moved\n");
	scratch.git({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-am"), QStringLiteral("moved") }, QStringLiteral("sub"));
	const std::vector<FileEntry> moved = scratch.refreshed().first;
	REQUIRE(moved.size() == 1);
	REQUIRE(moved[0].pointerMoved);

	GitRepository repository{ scratch.root() };
	requireSuccess(awaitAnswer<void>([&](Vcs::Answer<void> onDone) { repository.discardChanges({ QStringLiteral("sub") }, std::move(onDone)); }));

	CHECK(scratch.refreshed().first.empty());
}

TEST_CASE("A history walk covers every ref, the checkout alone, or one branch, a branch named like a file included", "[git]")
{
	using Scope = Repository::LogQuery::Scope;
	const ScratchRepository scratch;
	scratch.write(QStringLiteral("file.txt"), "base\n");
	scratch.commitAll(QStringLiteral("base"));
	scratch.git({ QStringLiteral("branch"), QStringLiteral("file.txt") }); // at the base
	scratch.write(QStringLiteral("file.txt"), "main\n");
	scratch.commitAll(QStringLiteral("on main"));
	scratch.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("side"), QStringLiteral("HEAD~1") });
	scratch.write(QStringLiteral("file.txt"), "side\n");
	scratch.commitAll(QStringLiteral("on side"));

	GitRepository repository{ scratch.root() };
	const auto subjects = [&](Repository::LogQuery query) {
		query.maxCommits = 50;
		const std::vector<CommitRecord> commits = requireSuccess(awaitAnswer<std::vector<CommitRecord>>(
			[&](Vcs::Answer<std::vector<CommitRecord>> onDone) { repository.commitLog(query, &repository, std::move(onDone)); }));
		QStringList listed;
		for (const CommitRecord& commit : commits)
			listed << commit.subject();
		return listed;
	};

	CHECK(subjects({}).size() == 3);
	CHECK(subjects({ .scope = Scope::CheckedOut }) == QStringList{ QStringLiteral("on side"), QStringLiteral("base") });
	CHECK(subjects({ .scope = Scope::Tip, .tip = QStringLiteral("main") }) == QStringList{ QStringLiteral("on main"), QStringLiteral("base") });
	CHECK(subjects({ .scope = Scope::Tip, .tip = QStringLiteral("file.txt") }) == QStringList{ QStringLiteral("base") });
	CHECK(subjects({ .scope = Scope::Tip, .tip = QStringLiteral("main"), .path = QStringLiteral("file.txt") })
		== QStringList{ QStringLiteral("on main"), QStringLiteral("base") });
}

TEST_CASE("A checkout puts HEAD on a branch, on a new branch tracking a remote-tracking one, or detaches it", "[git]")
{
	const ScratchRepository scratch;
	scratch.write(QStringLiteral("file.txt"), "one\n");
	scratch.commitAll(QStringLiteral("one"));
	const QString first = scratch.sha(QStringLiteral("HEAD"));
	scratch.git({ QStringLiteral("branch"), QStringLiteral("side") });
	scratch.write(QStringLiteral("file.txt"), "two\n");
	scratch.commitAll(QStringLiteral("two"));
	scratch.setUpstreamOfMainToOriginMain(); // the fetch refspec a tracking branch is set up from
	scratch.git({ QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/feature"), first });

	GitRepository repository{ scratch.root() };
	const auto checkOut = [&](const CheckoutTarget& target) {
		return awaitAnswer<void>([&](Vcs::Answer<void> onDone) { repository.checkOut(target, std::move(onDone)); });
	};

	SECTION("an existing branch")
	{
		requireSuccess(checkOut({ .kind = CheckoutTarget::Kind::Branch, .branch = QStringLiteral("side"), .sha = first }));
		CHECK(scratch.headName() == QStringLiteral("side"));
		CHECK(scratch.sha(QStringLiteral("HEAD")) == first);
	}

	SECTION("a new branch tracking a remote-tracking one")
	{
		requireSuccess(checkOut({ .kind = CheckoutTarget::Kind::TrackingBranch, .branch = QStringLiteral("feature"),
			.upstream = QStringLiteral("origin/feature"), .sha = first }));
		CHECK(scratch.headName() == QStringLiteral("feature"));
		CHECK(scratch.sha(QStringLiteral("HEAD")) == first);
		CHECK(scratch.sha(QStringLiteral("feature@{upstream}")) == first);
	}

	SECTION("the commit itself")
	{
		requireSuccess(checkOut({ .kind = CheckoutTarget::Kind::Commit, .sha = first }));
		CHECK(scratch.headName() == QStringLiteral("HEAD"));
		CHECK(scratch.sha(QStringLiteral("HEAD")) == first);
	}

	SECTION("an uncommitted change the checkout would overwrite fails it, and nothing moves")
	{
		scratch.write(QStringLiteral("file.txt"), "uncommitted\n");
		CHECK_FALSE(checkOut({ .kind = CheckoutTarget::Kind::Branch, .branch = QStringLiteral("side"), .sha = first }).has_value());
		CHECK(scratch.headName() == QStringLiteral("main"));
	}
}

TEST_CASE("Both sides of a merge conflict are conflicted rows, including a path modified here and deleted there", "[git]")
{
	const ScratchRepository scratch;
	scratch.write(QStringLiteral("both.txt"), "base\n");
	scratch.write(QStringLiteral("deleted-there.txt"), "base\n");
	scratch.commitAll(QStringLiteral("base"));

	scratch.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("other") });
	scratch.write(QStringLiteral("both.txt"), "theirs\n");
	scratch.git({ QStringLiteral("rm"), QStringLiteral("-q"), QStringLiteral("deleted-there.txt") });
	scratch.commitAll(QStringLiteral("theirs"));

	scratch.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("main") });
	scratch.write(QStringLiteral("both.txt"), "ours\n");
	scratch.write(QStringLiteral("deleted-there.txt"), "ours\n");
	scratch.commitAll(QStringLiteral("ours"));

	scratch.gitMayFail({ QStringLiteral("merge"), QStringLiteral("-q"), QStringLiteral("other") });

	const auto [files, state] = scratch.refreshed();

	CHECK(state.op == RepoOp::Merge);
	REQUIRE(pathsOf(files) == QStringList{ QStringLiteral("both.txt"), QStringLiteral("deleted-there.txt") });
	CHECK(files[0].type == ChangeType::Conflicted);
	CHECK(files[1].type == ChangeType::Conflicted);
}

TEST_CASE("A commit puts back the staging it had to clear: an added file, a partly staged blob, a deletion", "[git]")
{
	const ScratchRepository scratch;
	scratch.write(QStringLiteral("committed.txt"), "one\n");
	scratch.write(QStringLiteral("partly.txt"), "first\n");
	scratch.write(QStringLiteral("doomed.txt"), "doomed\n");
	scratch.commitAll(QStringLiteral("initial"));

	// Staged by something else, and none of it in the commit below: a new file, one version of a file the
	// working tree has moved on from (what `add -p` leaves behind), and a deletion
	scratch.write(QStringLiteral("added.txt"), "brand new\n");
	scratch.git({ QStringLiteral("add"), QStringLiteral("added.txt") });
	scratch.write(QStringLiteral("partly.txt"), "second\n");
	scratch.git({ QStringLiteral("add"), QStringLiteral("partly.txt") });
	scratch.write(QStringLiteral("partly.txt"), "third\n");
	scratch.git({ QStringLiteral("rm"), QStringLiteral("-q"), QStringLiteral("doomed.txt") });

	scratch.write(QStringLiteral("committed.txt"), "two\n");
	scratch.commitThroughBackend(QStringLiteral("only committed.txt"), { QStringLiteral("committed.txt") });

	CHECK(scratch.stagedAgainstHead() == QStringLiteral("A\tadded.txt\nD\tdoomed.txt\nM\tpartly.txt\n"));
	CHECK(scratch.stagedContent(QStringLiteral("partly.txt")) == QStringLiteral("second\n"));

	const auto [files, state] = scratch.refreshed();
	REQUIRE(pathsOf(files) == QStringList{ QStringLiteral("added.txt"), QStringLiteral("doomed.txt"), QStringLiteral("partly.txt") });
	CHECK(files[0].type == ChangeType::Added);
	CHECK(files[1].type == ChangeType::Deleted);
	CHECK(files[2].type == ChangeType::Modified);
	CHECK(state.headSubject() == QStringLiteral("only committed.txt"));
}

TEST_CASE("The state carries HEAD's whole message, and a root commit's parent count is zero", "[git]")
{
	const ScratchRepository scratch;
	scratch.write(QStringLiteral("file.txt"), "content\n");
	scratch.commitAll(QStringLiteral("subject\n\nbody line one\nbody line two\n"));

	const RepoState state = scratch.refreshed().second;
	CHECK(state.headMessage == QStringLiteral("subject\n\nbody line one\nbody line two"));
	CHECK(state.headSubject() == QStringLiteral("subject"));
	CHECK(state.headParentCount == 0);
}

TEST_CASE("The history fingerprint moves with a new branch, a checkout and a commit, and stays put otherwise", "[git]")
{
	const ScratchRepository scratch;
	scratch.write(QStringLiteral("file.txt"), "content\n");
	scratch.commitAll(QStringLiteral("base"));

	const QString base = scratch.historyFingerprint();
	CHECK(scratch.historyFingerprint() == base);

	scratch.git({ QStringLiteral("branch"), QStringLiteral("side") });
	const QString branched = scratch.historyFingerprint();
	CHECK(branched != base);

	scratch.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("side") }); // the same commit
	const QString checkedOut = scratch.historyFingerprint();
	CHECK(checkedOut != branched);

	scratch.write(QStringLiteral("file.txt"), "changed\n");
	scratch.commitAll(QStringLiteral("second"));
	CHECK(scratch.historyFingerprint() != checkedOut);
}

TEST_CASE("A refresh callback runs after the first run started after the request, the running one coalescing it", "[git]")
{
	const ScratchRepository scratch;
	scratch.write(QStringLiteral("file.txt"), "content\n");
	scratch.commitAll(QStringLiteral("base"));

	GitRepository repository{ scratch.root() };
	QEventLoop loop;
	QTimer::singleShot(60'000, &loop, &QEventLoop::quit);
	int completedRuns = 0;
	QObject::connect(&repository, &Repository::refreshed, &loop, [&] { ++completedRuns; });

	std::vector<int> runsSeenByCallbacks;
	repository.refresh([&] { runsSeenByCallbacks.push_back(completedRuns); });
	repository.refresh([&] {
		runsSeenByCallbacks.push_back(completedRuns);
		loop.quit();
	});
	loop.exec();

	CHECK(runsSeenByCallbacks == std::vector<int>{ 1, 2 });
}

TEST_CASE("A detached HEAD on its upstream's line is offered its branch moved, or a new one, without the working tree moving", "[git]")
{
	const ScratchRepository scratch;
	for (const char* content : { "1\n", "2\n", "3\n", "4\n" })
	{
		scratch.write(QStringLiteral("file.txt"), content);
		scratch.commitAll(QString::fromLatin1(content).trimmed());
	}
	scratch.setUpstreamOfMainToOriginMain();
	scratch.git({ QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/main"), QStringLiteral("HEAD") });
	scratch.git({ QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/feature"), QStringLiteral("HEAD~1") });
	// What a submodule update leaves: HEAD at a commit the upstream has, the local branch behind it
	const QString detachedAt = scratch.sha(QStringLiteral("HEAD~2"));
	scratch.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--detach"), detachedAt });
	scratch.git({ QStringLiteral("update-ref"), QStringLiteral("refs/heads/main"), scratch.sha(QStringLiteral("HEAD~1")) });

	const RepoState detached = scratch.refreshed().second;
	REQUIRE(detached.detached);
	REQUIRE(detached.reattachCandidates.size() == 2);
	const ReattachCandidate& move = detached.reattachCandidates[0];
	CHECK(move.kind == ReattachCandidate::Kind::Move);
	CHECK(move.branch == QStringLiteral("main"));
	CHECK(move.upstream == QStringLiteral("origin/main"));
	CHECK(move.refOnlyCommits == 0);
	CHECK(move.headOnlyCommits == 1);
	const ReattachCandidate& create = detached.reattachCandidates[1];
	CHECK(create.kind == ReattachCandidate::Kind::Create);
	CHECK(create.branch == QStringLiteral("feature"));
	CHECK(create.upstream == QStringLiteral("origin/feature"));
	CHECK(create.refOnlyCommits == 1);
	CHECK(create.headOnlyCommits == 0);

	// The whole case runs once per index, each over a repository of its own
	const ReattachCandidate taken = detached.reattachCandidates[GENERATE(size_t{ 0 }, size_t{ 1 })];
	scratch.reattachThroughBackend(taken);

	const auto [files, attached] = scratch.refreshed();
	CHECK(files.empty());
	CHECK_FALSE(attached.detached);
	CHECK(attached.branch == taken.branch);
	CHECK(attached.headSha == detachedAt);
	CHECK(attached.upstream == taken.upstream);
	CHECK(attached.behind == (taken.kind == ReattachCandidate::Kind::Move ? 2 : 1));
	CHECK(attached.reattachCandidates.empty());
}

TEST_CASE("A branch with unpushed commits is not offered, and the state says why", "[git]")
{
	const ScratchRepository scratch;
	for (const char* content : { "1\n", "2\n", "3\n" })
	{
		scratch.write(QStringLiteral("file.txt"), content);
		scratch.commitAll(QString::fromLatin1(content).trimmed());
	}
	scratch.setUpstreamOfMainToOriginMain();
	// main is one commit past origin/main, which contains the detached HEAD
	scratch.git({ QStringLiteral("update-ref"), QStringLiteral("refs/remotes/origin/main"), QStringLiteral("HEAD~1") });
	scratch.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--detach"), QStringLiteral("HEAD~2") });

	const RepoState state = scratch.refreshed().second;
	CHECK(state.reattachCandidates.empty());
	REQUIRE(state.reattachObstacles.size() == 1);
	CHECK(state.reattachObstacles[0].reason == ReattachObstacle::Reason::Unpushed);
	CHECK(state.reattachObstacles[0].branch == QStringLiteral("main"));
	CHECK(state.reattachObstacles[0].upstream == QStringLiteral("origin/main"));
	CHECK(state.reattachObstacles[0].unpushedCommits == 1);
}

TEST_CASE("An incoming check fetches, counts every incoming commit and lists the newest, without a refresh", "[git]")
{
	const ScratchRepository upstream;
	upstream.write(QStringLiteral("file.txt"), "base\n");
	upstream.commitAll(QStringLiteral("base"));

	const ScratchRepository local;
	local.git({ QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), upstream.root() });
	local.git({ QStringLiteral("fetch"), QStringLiteral("-q"), QStringLiteral("origin") });
	local.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--track"), QStringLiteral("-B"), QStringLiteral("main"),
		QStringLiteral("origin/main") });

	for (const QString& subject : { QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three") })
	{
		upstream.write(QStringLiteral("file.txt"), subject.toUtf8());
		upstream.commitAll(subject);
	}

	GitRepository repository{ local.root() };
	refreshToCompletion(repository);
	CHECK(repository.state().behind == 0); // nothing fetched yet
	const uint64_t generation = repository.refreshGeneration();

	const std::vector<CommitRecord> listed = requireSuccess(awaitAnswer<std::vector<CommitRecord>>(
		[&](Vcs::Answer<std::vector<CommitRecord>> onDone) { repository.checkIncoming(/*interactive=*/false, {}, 2, &repository, std::move(onDone)); }));
	REQUIRE(listed.size() == 2);
	CHECK(listed[0].subject() == QStringLiteral("three"));
	CHECK(listed[1].subject() == QStringLiteral("two"));
	CHECK(repository.state().behind == 3);
	CHECK(repository.refreshGeneration() == generation);

	refreshToCompletion(repository);
	CHECK(repository.state().behind == 3); // read from the ref the fetch moved
}

TEST_CASE("A detached HEAD's incoming check compares against origin/HEAD, or the branch it is given", "[git]")
{
	const ScratchRepository upstream;
	upstream.write(QStringLiteral("file.txt"), "base\n");
	upstream.commitAll(QStringLiteral("base"));
	upstream.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("dev") });
	upstream.write(QStringLiteral("file.txt"), "dev\n");
	upstream.commitAll(QStringLiteral("on dev"));
	upstream.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("main") });

	const ScratchRepository local;
	local.git({ QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), upstream.root() });
	local.git({ QStringLiteral("fetch"), QStringLiteral("-q"), QStringLiteral("origin") });
	local.git({ QStringLiteral("remote"), QStringLiteral("set-head"), QStringLiteral("origin"), QStringLiteral("main") });
	local.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--detach"), QStringLiteral("origin/main") });

	for (const QString& subject : { QStringLiteral("one"), QStringLiteral("two") })
	{
		upstream.write(QStringLiteral("file.txt"), subject.toUtf8());
		upstream.commitAll(subject);
	}

	GitRepository repository{ local.root() };
	const auto checkIncoming = [&](const QString& detachedHeadBranch) {
		return requireSuccess(awaitAnswer<std::vector<CommitRecord>>([&](Vcs::Answer<std::vector<CommitRecord>> onDone) {
			repository.checkIncoming(/*interactive=*/false, detachedHeadBranch, 10, &repository, std::move(onDone));
		}));
	};

	const std::vector<CommitRecord> fromDefault = checkIncoming({});
	REQUIRE(fromDefault.size() == 2);
	CHECK(fromDefault[0].subject() == QStringLiteral("two"));
	CHECK(repository.state().behind == 2);

	const std::vector<CommitRecord> fromDev = checkIncoming(QStringLiteral("dev"));
	REQUIRE(fromDev.size() == 1);
	CHECK(fromDev[0].subject() == QStringLiteral("on dev"));
	CHECK(repository.state().behind == 1);
}

TEST_CASE("Submodule update branches come from .gitmodules, \".\" naming the repository's own branch", "[git]")
{
	const ScratchRepository scratch;
	{
		GitRepository repository{ scratch.root() };
		const std::map<QString, QString> none = requireSuccess(awaitAnswer<std::map<QString, QString>>(
			[&](Vcs::Answer<std::map<QString, QString>> onDone) { repository.submoduleUpdateBranches(&repository, std::move(onDone)); }));
		CHECK(none.empty()); // no .gitmodules at all
	}

	scratch.write(QStringLiteral(".gitmodules"),
		"[submodule \"lib\"]\n\tpath = libs/lib\n\turl = ../lib\n\tbranch = release\n"
		"[submodule \"own\"]\n\tpath = own\n\turl = ../own\n\tbranch = .\n"
		"[submodule \"plain\"]\n\tpath = plain\n\turl = ../plain\n");
	scratch.commitAll(QStringLiteral("base"));
	scratch.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("feature") });

	GitRepository repository{ scratch.root() };
	refreshToCompletion(repository); // the state names the branch "." stands for
	const std::map<QString, QString> branches = requireSuccess(awaitAnswer<std::map<QString, QString>>(
		[&](Vcs::Answer<std::map<QString, QString>> onDone) { repository.submoduleUpdateBranches(&repository, std::move(onDone)); }));
	CHECK(branches == std::map<QString, QString>{ { QStringLiteral("libs/lib"), QStringLiteral("release") }, { QStringLiteral("own"), QStringLiteral("feature") } });
}

TEST_CASE("A one-by-one push steps through the first-parent commits descending from the upstream, each moving it by one", "[git]")
{
	const ScratchRepository upstream;
	upstream.write(QStringLiteral("file.txt"), "base\n");
	upstream.commitAll(QStringLiteral("base"));

	const ScratchRepository local;
	local.git({ QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), upstream.root() });
	local.git({ QStringLiteral("fetch"), QStringLiteral("-q"), QStringLiteral("origin") });
	local.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--track"), QStringLiteral("-B"), QStringLiteral("main"),
		QStringLiteral("origin/main") });

	upstream.write(QStringLiteral("upstream.txt"), "upstream\n");
	upstream.commitAll(QStringLiteral("upstream"));
	upstream.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--detach") }); // a checked-out branch refuses pushes

	// Two commits that only the merge of the upstream publishes, then the merge and one on top of it
	for (const QString& subject : { QStringLiteral("before 1"), QStringLiteral("before 2") })
	{
		local.write(QStringLiteral("file.txt"), subject.toUtf8());
		local.commitAll(subject);
	}
	local.git({ QStringLiteral("fetch"), QStringLiteral("-q"), QStringLiteral("origin") });
	local.git({ QStringLiteral("merge"), QStringLiteral("-q"), QStringLiteral("--no-edit"), QStringLiteral("origin/main") });
	local.write(QStringLiteral("file.txt"), "after\n");
	local.commitAll(QStringLiteral("after"));

	GitRepository repository{ local.root() };
	refreshToCompletion(repository);
	CHECK(repository.state().ahead == 4);

	const std::vector<PushStep> steps = requireSuccess(awaitAnswer<std::vector<PushStep>>(
		[&](Vcs::Answer<std::vector<PushStep>> onDone) { repository.planPush(PushMode::OneByOne, std::move(onDone)); }));
	REQUIRE(steps.size() == 2);
	CHECK(steps[0].commit == local.sha(QStringLiteral("HEAD~1")));
	CHECK(steps[1].commit == local.sha(QStringLiteral("HEAD")));
	for (const PushStep& step : steps)
	{
		CHECK(step.subject.isEmpty());
		CHECK(step.remote == QStringLiteral("origin"));
		CHECK(step.remoteRef == QStringLiteral("refs/heads/main"));
	}

	runPushStepToSuccess(repository, steps[0]);
	CHECK(upstream.sha(QStringLiteral("refs/heads/main")) == steps[0].commit);
	runPushStepToSuccess(repository, steps[1]);
	CHECK(upstream.sha(QStringLiteral("refs/heads/main")) == steps[1].commit);

	refreshToCompletion(repository);
	CHECK(repository.state().ahead == 0); // the remote-tracking ref moved with each push
}

TEST_CASE("A one-by-one push is refused for a diverged branch and for one whose plain push goes elsewhere", "[git]")
{
	const ScratchRepository upstream;
	upstream.write(QStringLiteral("file.txt"), "base\n");
	upstream.commitAll(QStringLiteral("base"));

	const ScratchRepository local;
	local.git({ QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), upstream.root() });
	local.git({ QStringLiteral("fetch"), QStringLiteral("-q"), QStringLiteral("origin") });
	local.git({ QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--track"), QStringLiteral("-B"), QStringLiteral("main"),
		QStringLiteral("origin/main") });
	local.write(QStringLiteral("file.txt"), "local\n");
	local.commitAll(QStringLiteral("local"));

	GitRepository repository{ local.root() };
	const auto planOneByOne = [&] {
		refreshToCompletion(repository);
		return awaitAnswer<std::vector<PushStep>>(
			[&](Vcs::Answer<std::vector<PushStep>> onDone) { repository.planPush(PushMode::OneByOne, std::move(onDone)); });
	};

	SECTION("diverged")
	{
		upstream.write(QStringLiteral("upstream.txt"), "upstream\n");
		upstream.commitAll(QStringLiteral("upstream"));
		local.git({ QStringLiteral("fetch"), QStringLiteral("-q"), QStringLiteral("origin") });
		const std::expected<std::vector<PushStep>, QString> plan = planOneByOne();
		REQUIRE_FALSE(plan.has_value());
		CHECK(plan.error().contains(QLatin1String("diverged")));
	}

	SECTION("pushRemote")
	{
		local.git({ QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("fork"), upstream.root() });
		local.git({ QStringLiteral("config"), QStringLiteral("branch.main.pushRemote"), QStringLiteral("fork") });
		const std::expected<std::vector<PushStep>, QString> plan = planOneByOne();
		REQUIRE_FALSE(plan.has_value());
		CHECK(plan.error().contains(QLatin1String("pushRemote")));
	}
}
