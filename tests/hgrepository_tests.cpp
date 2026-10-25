#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "hgrepository.h"
#include "repositorytestutils.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <expected>
#include <utility>
#include <vector>

namespace {

// Every hg fixture lives under one directory for the whole run: a command server keeps its working directory in
// the first repository it served, which Windows then refuses to delete. main() ends the servers before this goes.
[[nodiscard]] const QTemporaryDir& sharedRoot()
{
	static const QTemporaryDir root;
	return root;
}

// A Mercurial repository built by running hg itself, reading only the configuration written here
class ScratchHgRepository
{
public:
	ScratchHgRepository()
	{
		REQUIRE(sharedRoot().isValid());
		writeFile(sharedRoot().path(), QStringLiteral("hgrc"), "[ui]\nusername = GoodGit tests <tests@goodgit.invalid>\n[subrepos]\ngit:allowed = true\n");
		qputenv("HGRCPATH", sharedRoot().filePath(QStringLiteral("hgrc")).toUtf8());

		useEmptyGitConfig(sharedRoot().filePath(QStringLiteral("empty.gitconfig")));

		static int repositoryCount = 0;
		_root = sharedRoot().filePath(QStringLiteral("repository%1").arg(++repositoryCount));
		REQUIRE(QDir{}.mkpath(_root));
		run(QStringLiteral("hg"), { QStringLiteral("init") });
	}

	[[nodiscard]] const QString& root() const { return _root; }

	void write(const QString& relativePath, const QByteArray& content) const { writeFile(_root, relativePath, content); }

	void remove(const QString& relativePath) const { REQUIRE(QFile::remove(QDir{ _root }.filePath(relativePath))); }

	// Runs in `relativeDirectory` under the root, and must succeed
	void hg(const QStringList& arguments, const QString& relativeDirectory = {}) const { run(QStringLiteral("hg"), arguments, relativeDirectory); }
	// For the commands expected to fail, such as a merge that stops on conflicts
	void hgMayFail(const QStringList& arguments) const { run(QStringLiteral("hg"), arguments, {}, /*mustSucceed=*/false); }
	[[nodiscard]] QString hgOutput(const QStringList& arguments, const QString& relativeDirectory = {}) const
	{
		return run(QStringLiteral("hg"), arguments, relativeDirectory);
	}
	void git(const QStringList& arguments, const QString& relativeDirectory = {}) const
	{
		run(QStringLiteral("git"), QStringList{ QStringLiteral("-c"), QStringLiteral("user.name=GoodGit tests"), QStringLiteral("-c"),
			QStringLiteral("user.email=tests@goodgit.invalid") } + arguments, relativeDirectory);
	}

	void commitAll(const QString& message) const
	{
		hg({ QStringLiteral("addremove"), QStringLiteral("-q") });
		hg({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), message });
	}

	// An hg subrepo at `relativePath` holding one committed inside.txt, recorded by a commit here
	void addSubrepo(const QString& relativePath) const
	{
		write(relativePath + QStringLiteral("/inside.txt"), "inside\n");
		hg({ QStringLiteral("init") }, relativePath);
		hg({ QStringLiteral("addremove"), QStringLiteral("-q") }, relativePath);
		hg({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("inside") }, relativePath);
		write(QStringLiteral(".hgsub"), (relativePath + QStringLiteral(" = ") + relativePath + QLatin1Char('\n')).toUtf8());
		hg({ QStringLiteral("add"), QStringLiteral("-q"), QStringLiteral(".hgsub") });
		hg({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("with a subrepo") });
	}

	// One commit made the way the window makes it, over a refreshed repository. Empty on success, else the error.
	[[nodiscard]] QString commitThroughBackend(const QStringList& pathspec, const QStringList& untrackedPaths) const
	{
		HgRepository repository{ _root };
		refreshToCompletion(repository);
		const std::expected<void, QString> result = awaitAnswer<void>([&](Vcs::Answer<void> onDone) {
			repository.commit(QStringLiteral("through the backend"), pathspec, untrackedPaths, std::move(onDone));
		});
		return result ? QString{} : result.error();
	}

	// The backend's rows and state after one refresh, sorted by path
	[[nodiscard]] std::pair<std::vector<FileEntry>, RepoState> refreshed() const
	{
		HgRepository repository{ _root };
		return refreshedRowsAndState(repository);
	}

	// Requires the query to succeed
	[[nodiscard]] QString historyFingerprint() const
	{
		HgRepository repository{ _root };
		return requireSuccess(awaitAnswer<QString>([&](Vcs::Answer<QString> onDone) { repository.historyFingerprint(&repository, std::move(onDone)); }));
	}

private:
	// Returns the merged output
	QString run(const QString& program, const QStringList& arguments, const QString& relativeDirectory = {}, bool mustSucceed = true) const
	{
		const auto [exitCode, output] = runProcess(program, arguments, QDir{ _root }.filePath(relativeDirectory));
		INFO((program + QLatin1Char(' ') + arguments.join(QLatin1Char(' '))).toStdString() + '\n' + output.toStdString());
		REQUIRE(exitCode != -1); // crashed
		if (mustSucceed)
			REQUIRE(exitCode == 0);
		return output;
	}

	QString _root;
};

} // namespace

TEST_CASE("The hg state carries the working directory parent's whole message", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.write(QStringLiteral("file.txt"), "content\n");
	scratch.commitAll(QStringLiteral("subject\n\nbody line one\nbody line two\n"));

	const RepoState state = scratch.refreshed().second;
	CHECK(state.headMessage == QStringLiteral("subject\n\nbody line one\nbody line two"));
	CHECK(state.headSubject() == QStringLiteral("subject"));
}

TEST_CASE("The hg history fingerprint moves with a bookmark and a commit, and stays put otherwise", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.write(QStringLiteral("file.txt"), "content\n");
	scratch.commitAll(QStringLiteral("base"));

	const QString base = scratch.historyFingerprint();
	CHECK(scratch.historyFingerprint() == base);

	scratch.hg({ QStringLiteral("bookmark"), QStringLiteral("mark") });
	const QString bookmarked = scratch.historyFingerprint();
	CHECK(bookmarked != base);

	scratch.write(QStringLiteral("file.txt"), "changed\n");
	scratch.commitAll(QStringLiteral("second"));
	CHECK(scratch.historyFingerprint() != bookmarked);
}

TEST_CASE("An hg commit adds a head where the parent has a child on the working directory's branch", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.write(QStringLiteral("file.txt"), "base\n");
	scratch.commitAll(QStringLiteral("base"));
	scratch.write(QStringLiteral("file.txt"), "second\n");
	scratch.commitAll(QStringLiteral("second"));
	CHECK_FALSE(scratch.refreshed().second.commitAddsHead);

	scratch.hg({ QStringLiteral("update"), QStringLiteral("-q"), QStringLiteral("-r"), QStringLiteral("0") });
	CHECK(scratch.refreshed().second.commitAddsHead);

	// The commit starts the new branch instead
	scratch.hg({ QStringLiteral("branch"), QStringLiteral("-q"), QStringLiteral("other") });
	CHECK_FALSE(scratch.refreshed().second.commitAddsHead);
}

TEST_CASE("An hg history walk covers every changeset, the working directory's ancestry and its branch's public heads, or one bookmark's or named branch's", "[hg]")
{
	using Scope = Repository::LogQuery::Scope;
	const ScratchHgRepository scratch;
	scratch.write(QStringLiteral("file.txt"), "base\n");
	scratch.commitAll(QStringLiteral("base"));
	scratch.hg({ QStringLiteral("branch"), QStringLiteral("-q"), QStringLiteral("feat") });
	scratch.write(QStringLiteral("file.txt"), "feat\n");
	scratch.commitAll(QStringLiteral("on feat"));
	scratch.hg({ QStringLiteral("bookmark"), QStringLiteral("-r"), QStringLiteral("1"), QStringLiteral("mark") });
	scratch.hg({ QStringLiteral("update"), QStringLiteral("-q"), QStringLiteral("default") });
	scratch.write(QStringLiteral("file.txt"), "default\n");
	scratch.commitAll(QStringLiteral("on default"));

	HgRepository repository{ scratch.root() };
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
	CHECK(subjects({ .scope = Scope::CheckedOut }) == QStringList{ QStringLiteral("on default"), QStringLiteral("base") });
	CHECK(subjects({ .scope = Scope::Tip, .tip = QStringLiteral("feat") }) == QStringList{ QStringLiteral("on feat"), QStringLiteral("base") });
	CHECK(subjects({ .scope = Scope::Tip, .tip = QStringLiteral("mark") }) == QStringList{ QStringLiteral("on feat"), QStringLiteral("base") });
	CHECK(subjects({ .scope = Scope::Tip, .tip = QStringLiteral("feat"), .path = QStringLiteral("file.txt") })
		== QStringList{ QStringLiteral("on feat"), QStringLiteral("base") });

	// The tips: the bookmark, and both named branches at their heads, the working directory's current
	std::vector<BranchTip> tips = requireSuccess(awaitAnswer<std::vector<BranchTip>>(
		[&](Vcs::Answer<std::vector<BranchTip>> onDone) { repository.branchTips(&repository, std::move(onDone)); }));
	std::ranges::sort(tips, {}, &BranchTip::name);
	REQUIRE(tips.size() == 3);
	CHECK(tips[0].name == QStringLiteral("default"));
	CHECK(tips[0].namedBranch);
	CHECK(tips[0].current);
	CHECK(tips[1].name == QStringLiteral("feat"));
	CHECK(tips[1].namedBranch);
	CHECK_FALSE(tips[1].current);
	CHECK(tips[2].name == QStringLiteral("mark"));
	CHECK_FALSE(tips[2].namedBranch);
	CHECK(tips[2].sha == tips[1].sha);

	// The working directory behind the published head of its branch, as after a pull, beside a draft head of its own:
	// the published head is listed, the draft head is not
	scratch.hg({ QStringLiteral("phase"), QStringLiteral("-q"), QStringLiteral("--public"), QStringLiteral("-r"), QStringLiteral("2") });
	scratch.hg({ QStringLiteral("update"), QStringLiteral("-q"), QStringLiteral("-r"), QStringLiteral("0") });
	scratch.write(QStringLiteral("file.txt"), "draft\n");
	scratch.commitAll(QStringLiteral("draft head"));
	scratch.hg({ QStringLiteral("update"), QStringLiteral("-q"), QStringLiteral("-r"), QStringLiteral("0") });
	CHECK(subjects({ .scope = Scope::CheckedOut }) == QStringList{ QStringLiteral("on default"), QStringLiteral("base") });
	CHECK(subjects({ .scope = Scope::CheckedOut, .path = QStringLiteral("file.txt") }) == QStringList{ QStringLiteral("base") });
}

TEST_CASE("An hg checkout updates to a bookmark and activates it, or to a revision leaving no bookmark active", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.write(QStringLiteral("file.txt"), "one\n");
	scratch.commitAll(QStringLiteral("one"));
	scratch.hg({ QStringLiteral("bookmark"), QStringLiteral("-r"), QStringLiteral("0"), QStringLiteral("side") });
	scratch.write(QStringLiteral("file.txt"), "two\n");
	scratch.commitAll(QStringLiteral("two"));
	scratch.hg({ QStringLiteral("bookmark"), QStringLiteral("work") }); // active, at the tip
	const QString first = scratch.hgOutput({ QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("0"), QStringLiteral("-T"), QStringLiteral("{node}") }).trimmed();
	const QString second = scratch.hgOutput({ QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("1"), QStringLiteral("-T"), QStringLiteral("{node}") }).trimmed();

	HgRepository repository{ scratch.root() };
	const auto checkOut = [&](const CheckoutTarget& target) {
		return awaitAnswer<void>([&](Vcs::Answer<void> onDone) { repository.checkOut(target, std::move(onDone)); });
	};
	const auto parentNode = [&] {
		return scratch.hgOutput({ QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("."), QStringLiteral("-T"), QStringLiteral("{node}") }).trimmed();
	};
	const auto activeBookmark = [&] {
		return scratch.hgOutput({ QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("."), QStringLiteral("-T"), QStringLiteral("{activebookmark}") }).trimmed();
	};

	SECTION("a bookmark")
	{
		requireSuccess(checkOut({ .kind = CheckoutTarget::Kind::Branch, .branch = QStringLiteral("side"), .sha = first }));
		CHECK(parentNode() == first);
		CHECK(activeBookmark() == QStringLiteral("side"));
	}

	SECTION("a revision")
	{
		requireSuccess(checkOut({ .kind = CheckoutTarget::Kind::Commit, .sha = first }));
		CHECK(parentNode() == first);
		CHECK(activeBookmark().isEmpty());
	}

	SECTION("an uncommitted change the update would overwrite fails it, and nothing moves")
	{
		scratch.write(QStringLiteral("file.txt"), "uncommitted\n");
		CHECK_FALSE(checkOut({ .kind = CheckoutTarget::Kind::Branch, .branch = QStringLiteral("side"), .sha = first }).has_value());
		CHECK(parentNode() == second);
		CHECK(activeBookmark() == QStringLiteral("work"));
	}
}

TEST_CASE("hg status shapes: a move is one renamed row, removed and missing files are both deleted", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.write(QStringLiteral("keep.txt"), "keep\n");
	scratch.write(QStringLiteral("old.txt"), "moved\n");
	scratch.write(QStringLiteral("removed.txt"), "removed\n");
	scratch.write(QStringLiteral("missing.txt"), "missing\n");
	scratch.commitAll(QStringLiteral("base"));

	scratch.write(QStringLiteral("keep.txt"), "keep\nmore\n");
	scratch.hg({ QStringLiteral("mv"), QStringLiteral("old.txt"), QStringLiteral("new.txt") });
	scratch.hg({ QStringLiteral("rm"), QStringLiteral("removed.txt") });
	scratch.remove(QStringLiteral("missing.txt"));
	scratch.write(QStringLiteral("untracked.txt"), "new\n");

	const auto [files, state] = scratch.refreshed();

	REQUIRE(pathsOf(files) == QStringList{ QStringLiteral("keep.txt"), QStringLiteral("missing.txt"), QStringLiteral("new.txt"),
		QStringLiteral("removed.txt"), QStringLiteral("untracked.txt") });
	CHECK(files[0].type == ChangeType::Modified);
	REQUIRE(files[0].lineCounts.has_value());
	CHECK(files[0].lineCounts->added == 1);
	CHECK(files[1].type == ChangeType::Deleted);
	CHECK(files[2].type == ChangeType::Renamed);
	CHECK(files[2].oldPath == QStringLiteral("old.txt"));
	CHECK(files[3].type == ChangeType::Deleted);
	CHECK(files[4].type == ChangeType::Untracked);
}

TEST_CASE("hg merge conflicts are rows, including a file modified here and deleted there, which has no status record", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.write(QStringLiteral("both.txt"), "base\n");
	scratch.write(QStringLiteral("deleted-there.txt"), "base\n");
	scratch.commitAll(QStringLiteral("base"));

	scratch.write(QStringLiteral("both.txt"), "theirs\n");
	scratch.hg({ QStringLiteral("rm"), QStringLiteral("deleted-there.txt") });
	scratch.hg({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("theirs") });

	scratch.hg({ QStringLiteral("update"), QStringLiteral("-q"), QStringLiteral("0") });
	scratch.write(QStringLiteral("both.txt"), "ours\n");
	scratch.write(QStringLiteral("deleted-there.txt"), "ours\n");
	scratch.hg({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("ours") });
	// :fail leaves every conflict unresolved instead of prompting or picking a side; hg then exits 1
	scratch.hgMayFail({ QStringLiteral("merge"), QStringLiteral("-q"), QStringLiteral("--tool"), QStringLiteral(":fail"), QStringLiteral("1") });

	const auto [files, state] = scratch.refreshed();

	CHECK(state.op == RepoOp::Merge);
	REQUIRE(pathsOf(files) == QStringList{ QStringLiteral("both.txt"), QStringLiteral("deleted-there.txt") });
	CHECK(files[0].type == ChangeType::Conflicted);
	CHECK(files[1].type == ChangeType::Conflicted);
}

TEST_CASE("An hg subrepo gets a row for modified files inside, and none for untracked files alone", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.addSubrepo(QStringLiteral("sub"));
	CHECK(HgRepository{ scratch.root() }.nestedRepositoryLocation(QStringLiteral("sub")).kind == VcsKind::Mercurial);

	SECTION("untracked files inside")
	{
		scratch.write(QStringLiteral("sub/untracked.txt"), "new\n");
		CHECK(describedRows(scratch.refreshed().first) == "");
	}

	SECTION("a modified tracked file inside")
	{
		scratch.write(QStringLiteral("sub/inside.txt"), "changed\n");
		const auto [files, state] = scratch.refreshed();

		REQUIRE(files.size() == 1);
		CHECK(files[0].path == QStringLiteral("sub"));
		CHECK(files[0].isSubmodule);
		CHECK_FALSE(files[0].pointerMoved);
		CHECK(files[0].content == SubmoduleContent::DirtyTracked);
	}
}

TEST_CASE("An hg commit of a subrepo row records its pointer and leaves the unknown files inside untracked", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.addSubrepo(QStringLiteral("sub"));
	scratch.write(QStringLiteral("sub/inside.txt"), "changed\n");
	scratch.hg({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("moves the pointer") }, QStringLiteral("sub"));
	scratch.write(QStringLiteral("sub/unknown.txt"), "new\n");

	REQUIRE(scratch.commitThroughBackend({ QStringLiteral("sub") }, {}).isEmpty());

	CHECK(scratch.hgOutput({ QStringLiteral("status") }, QStringLiteral("sub")) == QStringLiteral("? unknown.txt\n"));
	CHECK(scratch.hgOutput({ QStringLiteral("status") }).isEmpty());
	CHECK(describedRows(scratch.refreshed().first) == ""); // the pointer is recorded
}

TEST_CASE("An hg commit records a missing file's removal, and keeps a forgotten file removed", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.write(QStringLiteral("missing.txt"), "missing\n");
	scratch.write(QStringLiteral("forgotten.txt"), "forgotten\n");
	scratch.commitAll(QStringLiteral("base"));
	scratch.remove(QStringLiteral("missing.txt"));
	scratch.hg({ QStringLiteral("forget"), QStringLiteral("forgotten.txt") });
	scratch.write(QStringLiteral("new.txt"), "new\n");

	REQUIRE(scratch.commitThroughBackend({ QStringLiteral("forgotten.txt"), QStringLiteral("missing.txt"), QStringLiteral("new.txt") },
		{ QStringLiteral("new.txt") }).isEmpty());

	CHECK(scratch.hgOutput({ QStringLiteral("files"), QStringLiteral("-r"), QStringLiteral(".") }) == QStringLiteral("new.txt\n"));
	CHECK(scratch.hgOutput({ QStringLiteral("status") }) == QStringLiteral("? forgotten.txt\n"));
}

TEST_CASE("A failed hg commit leaves the checked untracked files untracked, here and in a listed subrepo", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.addSubrepo(QStringLiteral("sub"));
	// hg refuses to commit a pointer while the subrepo has uncommitted changes
	scratch.write(QStringLiteral("sub/inside.txt"), "changed\n");
	scratch.write(QStringLiteral("sub/unknown.txt"), "new\n");
	scratch.write(QStringLiteral("new.txt"), "new\n");

	CHECK_FALSE(scratch.commitThroughBackend({ QStringLiteral("new.txt"), QStringLiteral("sub") }, { QStringLiteral("new.txt") }).isEmpty());

	CHECK(scratch.hgOutput({ QStringLiteral("status") }) == QStringLiteral("? new.txt\n"));
	CHECK(scratch.hgOutput({ QStringLiteral("status") }, QStringLiteral("sub")) == QStringLiteral("M inside.txt\n? unknown.txt\n"));
}

TEST_CASE("A git subrepo inside an hg repository is a git location, and its content is read with git", "[hg]")
{
	const ScratchHgRepository scratch;
	scratch.write(QStringLiteral("gsub/inside.txt"), "inside\n");
	scratch.git({ QStringLiteral("init"), QStringLiteral("-q") }, QStringLiteral("gsub"));
	scratch.git({ QStringLiteral("add"), QStringLiteral("inside.txt") }, QStringLiteral("gsub"));
	scratch.git({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("inside") }, QStringLiteral("gsub"));
	scratch.write(QStringLiteral(".hgsub"), "gsub = [git]gsub\n");
	scratch.hg({ QStringLiteral("add"), QStringLiteral("-q"), QStringLiteral(".hgsub") });
	scratch.hg({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("with a git subrepo") });

	// No refresh: the history window opens nested repositories from a repository it never refreshes
	CHECK(HgRepository{ scratch.root() }.nestedRepositoryLocation(QStringLiteral("gsub")).kind == VcsKind::Git);

	scratch.write(QStringLiteral("gsub/inside.txt"), "changed\n");
	const auto [files, state] = scratch.refreshed();

	REQUIRE(files.size() == 1);
	CHECK(files[0].path == QStringLiteral("gsub"));
	CHECK(files[0].isSubmodule);
	CHECK(files[0].content == SubmoduleContent::DirtyTracked);
}

TEST_CASE("An hg incoming check counts every incoming changeset and lists the newest first, without a refresh", "[hg]")
{
	const ScratchHgRepository upstream;
	upstream.write(QStringLiteral("file.txt"), "base\n");
	upstream.commitAll(QStringLiteral("base"));

	const ScratchHgRepository local;
	local.hg({ QStringLiteral("pull"), QStringLiteral("-q"), QStringLiteral("-u"), upstream.root() });
	local.write(QStringLiteral(".hg/hgrc"), (QStringLiteral("[paths]\ndefault = ") + upstream.root() + QLatin1Char('\n')).toUtf8());

	for (const QString& subject : { QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three") })
	{
		upstream.write(QStringLiteral("file.txt"), subject.toUtf8());
		upstream.commitAll(subject);
	}

	HgRepository repository{ local.root() };
	refreshToCompletion(repository);
	CHECK(repository.state().upstream == QStringLiteral("default"));
	CHECK(repository.state().behind == 0); // nothing checked yet
	const uint64_t generation = repository.refreshGeneration();

	const std::vector<CommitRecord> listed = requireSuccess(awaitAnswer<std::vector<CommitRecord>>(
		[&](Vcs::Answer<std::vector<CommitRecord>> onDone) { repository.checkIncoming(/*interactive=*/false, {}, 2, &repository, std::move(onDone)); }));
	REQUIRE(listed.size() == 2);
	CHECK(listed[0].subject() == QStringLiteral("three"));
	CHECK(listed[1].subject() == QStringLiteral("two"));
	CHECK_FALSE(listed[0].revision.has_value()); // the remote's numbering
	CHECK(repository.state().behind == 3);
	CHECK(repository.refreshGeneration() == generation);

	refreshToCompletion(repository);
	CHECK(repository.state().behind == 3); // carried over: no local ref holds it
}

TEST_CASE("An hg one-by-one push steps through the first-parent drafts descending from the public head", "[hg]")
{
	const ScratchHgRepository upstream;
	upstream.write(QStringLiteral("file.txt"), "base\n");
	upstream.commitAll(QStringLiteral("base"));

	const ScratchHgRepository local;
	local.hg({ QStringLiteral("pull"), QStringLiteral("-q"), QStringLiteral("-u"), upstream.root() });
	local.write(QStringLiteral(".hg/hgrc"), (QStringLiteral("[paths]\ndefault = ") + upstream.root() + QLatin1Char('\n')).toUtf8());

	upstream.write(QStringLiteral("upstream.txt"), "upstream\n");
	upstream.commitAll(QStringLiteral("upstream"));

	// Two drafts that only the merge of the pulled head publishes, then the merge and one on top of it
	for (const QString& subject : { QStringLiteral("before 1"), QStringLiteral("before 2") })
	{
		local.write(QStringLiteral("file.txt"), subject.toUtf8());
		local.commitAll(subject);
	}
	local.hg({ QStringLiteral("pull"), QStringLiteral("-q") });
	local.hg({ QStringLiteral("merge"), QStringLiteral("-q") });
	local.hg({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("merge") });
	local.write(QStringLiteral("file.txt"), "after\n");
	local.commitAll(QStringLiteral("after"));

	const auto node = [&](const QString& revision) {
		return local.hgOutput({ QStringLiteral("log"), QStringLiteral("-r"), revision, QStringLiteral("-T"), QStringLiteral("{node}") }).trimmed();
	};
	const auto upstreamHeads = [&] {
		return upstream.hgOutput({ QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("heads(all())"), QStringLiteral("-T"),
			QStringLiteral("{node}\n") }).trimmed();
	};

	HgRepository repository{ local.root() };
	refreshToCompletion(repository);
	CHECK(repository.state().ahead == 4);

	const std::vector<PushStep> steps = requireSuccess(awaitAnswer<std::vector<PushStep>>(
		[&](Vcs::Answer<std::vector<PushStep>> onDone) { repository.planPush(PushMode::OneByOne, {}, std::move(onDone)); }));
	REQUIRE(steps.size() == 2);
	CHECK(steps[0].commit == node(QStringLiteral(".^")));
	CHECK(steps[1].commit == node(QStringLiteral(".")));

	runPushStepToSuccess(repository, steps[0]);
	CHECK(upstreamHeads() == steps[0].commit);
	runPushStepToSuccess(repository, steps[1]);
	CHECK(upstreamHeads() == steps[1].commit);

	refreshToCompletion(repository);
	CHECK(repository.state().ahead == 0);

	const std::expected<std::vector<PushStep>, QString> nothingLeft = awaitAnswer<std::vector<PushStep>>(
		[&](Vcs::Answer<std::vector<PushStep>> onDone) { repository.planPush(PushMode::OneByOne, {}, std::move(onDone)); });
	CHECK_FALSE(nothingLeft.has_value());
}

TEST_CASE("An hg push up to a changeset publishes it and its ancestors, and one it cannot push alone is refused with the reason", "[hg]")
{
	const ScratchHgRepository upstream;
	upstream.write(QStringLiteral("file.txt"), "base\n");
	upstream.commitAll(QStringLiteral("base"));

	const ScratchHgRepository local;
	local.hg({ QStringLiteral("pull"), QStringLiteral("-q"), QStringLiteral("-u"), upstream.root() });
	local.write(QStringLiteral(".hg/hgrc"), (QStringLiteral("[paths]\ndefault = ") + upstream.root() + QLatin1Char('\n')).toUtf8());

	upstream.write(QStringLiteral("upstream.txt"), "upstream\n");
	upstream.commitAll(QStringLiteral("upstream"));

	const auto node = [&](const QString& revision) {
		return local.hgOutput({ QStringLiteral("log"), QStringLiteral("-r"), revision, QStringLiteral("-T"), QStringLiteral("{node}") }).trimmed();
	};

	// A draft on a head of its own, then one only the merge of the pulled head publishes, the merge, and one on top of it
	local.write(QStringLiteral("side.txt"), "side\n");
	local.commitAll(QStringLiteral("side"));
	const QString side = node(QStringLiteral("."));
	local.hg({ QStringLiteral("update"), QStringLiteral("-q"), QStringLiteral("-r"), QStringLiteral("0") });
	local.write(QStringLiteral("file.txt"), "before\n");
	local.commitAll(QStringLiteral("before"));
	local.hg({ QStringLiteral("pull"), QStringLiteral("-q") });
	local.hg({ QStringLiteral("merge"), QStringLiteral("-q"), QStringLiteral("-r"), QStringLiteral("public() and head()") });
	local.hg({ QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("merge") });
	local.write(QStringLiteral("file.txt"), "after\n");
	local.commitAll(QStringLiteral("after"));

	// Never refreshed: the plan reads nothing from the state
	HgRepository repository{ local.root() };
	const auto planUpTo = [&](const QString& commit) {
		return awaitAnswer<std::vector<PushStep>>(
			[&](Vcs::Answer<std::vector<PushStep>> onDone) { repository.planPush(PushMode::UpTo, commit, std::move(onDone)); });
	};

	SECTION("on the first-parent line")
	{
		const QString merge = node(QStringLiteral(".^"));
		const std::vector<PushStep> steps = requireSuccess(planUpTo(merge));
		REQUIRE(steps.size() == 1);
		CHECK(steps[0].commit == merge);

		runPushStepToSuccess(repository, steps[0]);
		CHECK(upstream.hgOutput({ QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("heads(all())"), QStringLiteral("-T"),
			QStringLiteral("{node}\n") }).trimmed() == merge);

		const std::expected<std::vector<PushStep>, QString> again = planUpTo(merge);
		REQUIRE_FALSE(again.has_value());
		CHECK(again.error().contains(QLatin1String("public already")));
	}

	SECTION("before a merge of the pulled head")
	{
		const std::expected<std::vector<PushStep>, QString> plan = planUpTo(node(QStringLiteral(".^^")));
		REQUIRE_FALSE(plan.has_value());
		CHECK(plan.error().contains(QLatin1String("first-parent line")));
	}

	SECTION("not an ancestor of the working directory's parent")
	{
		const std::expected<std::vector<PushStep>, QString> plan = planUpTo(side);
		REQUIRE_FALSE(plan.has_value());
		CHECK(plan.error().contains(QLatin1String("not an ancestor")));
	}
}
