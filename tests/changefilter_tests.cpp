#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "changefilter.h"
#include "repositorytestutils.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QSet>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <initializer_list>

namespace {

const QString GitDir = QStringLiteral("/r/.git");

// The git directories under GitDir, by their paths relative to it
[[nodiscard]] ChangeFilter::IsGitDirectory gitDirectoriesAt(std::initializer_list<const char*> relativePaths)
{
	QSet<QString> directories;
	for (const char* relative : relativePaths)
		directories.insert(GitDir + QLatin1Char('/') + QLatin1String(relative));
	return [directories](const QString& absolutePath) { return directories.contains(absolutePath); };
}

} // namespace

TEST_CASE("Git noise is the object store, the reflogs, the hooks, FETCH_HEAD, COMMIT_EDITMSG and the lock files", "[changefilter]")
{
	const ChangeFilter::IsGitDirectory none = gitDirectoriesAt({});

	for (const char* path : { "objects", "objects/ab/cdef", "logs/HEAD", "logs/refs/heads/main", "hooks/pre-commit", "FETCH_HEAD",
			 "COMMIT_EDITMSG", "index.lock", "refs/heads/main.lock" })
	{
		INFO(path);
		CHECK(ChangeFilter::isGitNoise(GitDir, QString::fromLatin1(path), none));
	}

	// A ref whose name holds one of the noise entries' names is a ref
	for (const char* path : { "HEAD", "index", "packed-refs", "refs/heads/main", "refs/heads/objects/x", "refs/remotes/origin/logs", "MERGE_HEAD" })
	{
		INFO(path);
		CHECK_FALSE(ChangeFilter::isGitNoise(GitDir, QString::fromLatin1(path), none));
	}
}

TEST_CASE("Git noise holds in the git directories nested under modules/ and worktrees/, whose names may hold slashes", "[changefilter]")
{
	const ChangeFilter::IsGitDirectory isGitDirectory = gitDirectoriesAt({ "modules/a/b", "modules/a/b/modules/c", "modules/objects", "worktrees/wt" });

	for (const char* path : { "modules/a/b/objects/12/x", "modules/a/b/logs/HEAD", "modules/a/b/modules/c/hooks/x", "modules/objects/FETCH_HEAD",
			 "worktrees/wt/logs/HEAD" })
	{
		INFO(path);
		CHECK(ChangeFilter::isGitNoise(GitDir, QString::fromLatin1(path), isGitDirectory));
	}

	// A submodule named "objects", and a directory on the way to a git directory, hold no noise themselves
	for (const char* path : { "modules/objects/HEAD", "modules/a/b/HEAD", "modules/a/b/refs/heads/main", "modules/a/objects/x", "worktrees/wt/HEAD" })
	{
		INFO(path);
		CHECK_FALSE(ChangeFilter::isGitNoise(GitDir, QString::fromLatin1(path), isGitDirectory));
	}
}

TEST_CASE("Mercurial noise is its caches", "[changefilter]")
{
	for (const char* path : { "cache", "cache/branch2-served", "wcache/checkisexec" })
	{
		INFO(path);
		CHECK(ChangeFilter::isHgNoise(QString::fromLatin1(path)));
	}
	for (const char* path : { "", "dirstate", "bookmarks", "store/00changelog.i", "store/phaseroots", "cachefile" })
	{
		INFO(path);
		CHECK_FALSE(ChangeFilter::isHgNoise(QString::fromLatin1(path)));
	}
}

TEST_CASE("Noise inside a nested repository's state directory is noise in the working tree", "[changefilter]")
{
	const QString root = QStringLiteral("/r");
	const ChangeFilter::IsGitDirectory none = [](const QString&) { return false; };

	for (const char* path : { "sub/.git/objects/ab/cd", "sub/.git/index.lock", "deep/er/.hg/cache/x" })
	{
		INFO(path);
		CHECK(ChangeFilter::isNestedStateNoise(root, QString::fromLatin1(path), none));
	}
	for (const char* path : { "sub/.git", "sub/.git/HEAD", "sub/file.txt", ".gitignore", "sub/.hg/dirstate", "a.git/objects/x" })
	{
		INFO(path);
		CHECK_FALSE(ChangeFilter::isNestedStateNoise(root, QString::fromLatin1(path), none));
	}

	// The git directories nested in the nested one are looked up under its absolute path
	const QString nestedModuleObjects = QStringLiteral("sub/.git/modules/inner/objects/x");
	CHECK_FALSE(ChangeFilter::isNestedStateNoise(root, nestedModuleObjects, none));
	const ChangeFilter::IsGitDirectory isGitDirectory = [](const QString& absolutePath) { return absolutePath == QLatin1String("/r/sub/.git/modules/inner"); };
	CHECK(ChangeFilter::isNestedStateNoise(root, nestedModuleObjects, isGitDirectory));
}

TEST_CASE("The probe applies git's own test of a git directory, so a remote's ref directory holding HEAD is none", "[changefilter]")
{
	const QTemporaryDir directory;
	REQUIRE(directory.isValid());
	const QString root = directory.path();

	writeFile(root, QStringLiteral("repo/HEAD"), "ref: refs/heads/main\n");
	REQUIRE(QDir{ root }.mkpath(QStringLiteral("repo/objects")));
	REQUIRE(QDir{ root }.mkpath(QStringLiteral("repo/refs/heads")));
	writeFile(root, QStringLiteral("repo/refs/remotes/origin/HEAD"), "ref: refs/remotes/origin/main\n");
	writeFile(root, QStringLiteral("repo/worktrees/wt/HEAD"), "ref: refs/heads/wt\n");
	writeFile(root, QStringLiteral("repo/worktrees/wt/commondir"), "../..\n");

	ChangeFilter::GitDirectoryProbe probe;
	CHECK(probe(root + QStringLiteral("/repo")));
	CHECK(probe(root + QStringLiteral("/repo/worktrees/wt")));
	CHECK_FALSE(probe(root + QStringLiteral("/repo/refs/remotes/origin")));
	CHECK_FALSE(probe(root + QStringLiteral("/repo/objects")));
	CHECK_FALSE(probe(root + QStringLiteral("/missing")));
}
