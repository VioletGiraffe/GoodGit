#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "gitrepository.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <expected>
#include <optional>

namespace {

// A git repository in a temporary directory, built by running git itself.
// Neither the fixture commands nor the backend under test read the machine's global or system git config.
class ScratchRepository
{
public:
	ScratchRepository()
	{
		REQUIRE(_directory.isValid());
		const QString emptyConfig = _directory.filePath(QStringLiteral("empty.gitconfig"));
		REQUIRE(QFile{ emptyConfig }.open(QIODevice::WriteOnly));
		qputenv("GIT_CONFIG_GLOBAL", emptyConfig.toUtf8());
		qputenv("GIT_CONFIG_NOSYSTEM", "1");

		QDir{}.mkpath(root());
		git({ QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("main") });
	}

	[[nodiscard]] QString root() const { return _directory.filePath(QStringLiteral("repository")); }

	void write(const QString& relativePath, const QByteArray& content) const
	{
		const QString path = QDir{ root() }.filePath(relativePath);
		REQUIRE(QDir{}.mkpath(QFileInfo{ path }.absolutePath()));
		QFile file{ path };
		REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		file.write(content);
	}

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
		refresh(repository);

		std::vector<FileEntry> files = repository.files();
		std::ranges::sort(files, {}, &FileEntry::path);
		return { std::move(files), repository.state() };
	}

	// One commit made the way the window makes it, over a refreshed repository: the commit's diff baseline
	// comes from the state
	void commitThroughBackend(const QString& message, const QStringList& pathspec) const
	{
		// The backend runs git without the -c arguments the fixture's own commands carry
		git({ QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("GoodGit tests") });
		git({ QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("tests@goodgit.invalid") });

		GitRepository repository{ root() };
		refresh(repository);

		QEventLoop loop;
		QTimer::singleShot(60'000, &loop, &QEventLoop::quit);
		std::optional<QString> failure;
		repository.commit(message, pathspec, {}, [&](std::expected<void, QString> result) {
			failure = result ? QString{} : result.error();
			loop.quit();
		});
		loop.exec();

		REQUIRE(failure.has_value()); // the timer fired instead of the commit answering
		INFO(failure->toStdString());
		REQUIRE(failure->isEmpty());
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
	// Runs one refresh to completion, so the caller can go on using the same repository object
	static void refresh(GitRepository& repository)
	{
		QEventLoop loop;
		QObject::connect(&repository, &Repository::refreshed, &loop, &QEventLoop::quit);
		QTimer::singleShot(60'000, &loop, &QEventLoop::quit);
		repository.refresh();
		loop.exec();
		REQUIRE_FALSE(repository.refreshing());
		INFO(repository.state().readFailure.toStdString());
		REQUIRE(repository.state().known());
	}

	[[nodiscard]] QString gitOutput(const QStringList& arguments) const
	{
		const auto [exitCode, output] = runGit(arguments, {});
		INFO(output.toStdString());
		REQUIRE(exitCode == 0);
		return output;
	}

	[[nodiscard]] std::pair<int, QString> runGit(const QStringList& arguments, const QString& relativeDirectory) const
	{
		QProcess process;
		process.setWorkingDirectory(QDir{ root() }.filePath(relativeDirectory));
		process.setProcessChannelMode(QProcess::MergedChannels);
		const QStringList identity{ QStringLiteral("-c"), QStringLiteral("user.name=GoodGit tests"), QStringLiteral("-c"),
			QStringLiteral("user.email=tests@goodgit.invalid") };
		process.start(QStringLiteral("git"), identity + arguments);
		REQUIRE(process.waitForFinished(60'000));
		return { process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1, QString::fromUtf8(process.readAll()) };
	}

	QTemporaryDir _directory;
};

[[nodiscard]] QStringList pathsOf(const std::vector<FileEntry>& files)
{
	QStringList paths;
	for (const FileEntry& file : files)
		paths.push_back(file.path);
	return paths;
}

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
	CHECK(state.headSubject == QStringLiteral("only committed.txt"));
}
