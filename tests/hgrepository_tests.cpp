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
		CHECK(scratch.refreshed().first.empty());
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
	CHECK(scratch.refreshed().first.empty()); // the pointer is recorded
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
