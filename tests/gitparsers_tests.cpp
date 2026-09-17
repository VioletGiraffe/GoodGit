#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "gitparsers.h"

#include <initializer_list>

namespace {

// -z output: every token NUL-terminated
[[nodiscard]] QByteArray nulTerminated(std::initializer_list<const char*> tokens)
{
	QByteArray output;
	for (const char* token : tokens)
	{
		output += token;
		output += '\0';
	}
	return output;
}

} // namespace

TEST_CASE("The branch header reads the upstream counts, and a rename's origin path is never read as a record", "[gitparsers]")
{
	const QByteArray trackedRecords = nulTerminated({
		"1 .M N... 100644 100644 100644 f384549cbeb481e437091320de6d1f2e15e11b4a f384549cbeb481e437091320de6d1f2e15e11b4a keep.txt",
		"2 R. N... 100644 100644 100644 f384549cbeb481e437091320de6d1f2e15e11b4a f384549cbeb481e437091320de6d1f2e15e11b4a R100 new.txt",
		"# branch.head not-the-branch", // the origin path of the rename above
	});

	SECTION("an upstream with counts")
	{
		const Git::BranchHeader header = Git::parseBranchHeader(nulTerminated({ "# branch.oid 7111d06b02662e3a26833e79c6e4f63bb5de11c6",
			"# branch.head main", "# branch.upstream origin/main", "# branch.ab +2 -1" }) + trackedRecords);

		CHECK(header.oid == QStringLiteral("7111d06b02662e3a26833e79c6e4f63bb5de11c6"));
		CHECK(header.head == QStringLiteral("main"));
		CHECK(header.upstream == QStringLiteral("origin/main"));
		CHECK(header.aheadBehindKnown);
		CHECK(header.ahead == 2);
		CHECK(header.behind == 1);
	}

	SECTION("an upstream whose ref is gone reports no counts")
	{
		const Git::BranchHeader header = Git::parseBranchHeader(nulTerminated({ "# branch.oid 7111d06b02662e3a26833e79c6e4f63bb5de11c6",
			"# branch.head main", "# branch.upstream origin/deleted" }));

		CHECK(header.upstream == QStringLiteral("origin/deleted"));
		CHECK_FALSE(header.aheadBehindKnown);
	}

	SECTION("--no-ahead-behind's placeholders are unknown counts, not zeros")
	{
		const Git::BranchHeader header = Git::parseBranchHeader(nulTerminated({ "# branch.head main", "# branch.upstream origin/main",
			"# branch.ab +? -?" }));

		CHECK_FALSE(header.aheadBehindKnown);
	}
}

TEST_CASE("Unmerged paths keep their spaces, and other records are ignored", "[gitparsers]")
{
	const QByteArray status = nulTerminated({
		"# branch.head main",
		"1 .M N... 100644 100644 100644 f384549cbeb481e437091320de6d1f2e15e11b4a f384549cbeb481e437091320de6d1f2e15e11b4a keep.txt",
		"u UU N... 100644 100644 100644 100644 df967b96a579e45a18b8251732d16804b2e56a55 cbb9aa30a6518a54df04c4d7b62e5c5e2864eafc "
			"2299c37978265a95cbe835a4b0f0bbf15aad5549 con flict.txt",
	});

	CHECK(Git::parseUnmergedPaths(status) == QStringList{ QStringLiteral("con flict.txt") });
}

TEST_CASE("Name-status rows, a rename naming both paths", "[gitparsers]")
{
	const std::vector<CommitFileChange> entries = Git::parseNameStatusZ(
		nulTerminated({ "A", "blob.bin", "M", "con flict.txt", "R100", "old name.txt", "new name.txt", "T", "link", "D", "gone.txt" }));

	REQUIRE(entries.size() == 5);
	CHECK(entries[0].type == ChangeType::Added);
	CHECK(entries[1].path == QStringLiteral("con flict.txt"));
	CHECK(entries[2].type == ChangeType::Renamed);
	CHECK(entries[2].oldPath == QStringLiteral("old name.txt"));
	CHECK(entries[2].path == QStringLiteral("new name.txt"));
	CHECK(entries[3].type == ChangeType::TypeChanged);
	CHECK(entries[4].type == ChangeType::Deleted);
	CHECK(entries[4].path == QStringLiteral("gone.txt"));
}

TEST_CASE("Raw rows mark gitlinks as submodules, with the commit on the side that exists", "[gitparsers]")
{
	const std::vector<CommitFileChange> entries = Git::parseRawZ(nulTerminated({
		":000000 160000 0000000000000000000000000000000000000000 1111111111111111111111111111111111111111 A", "added-sub",
		":160000 000000 2222222222222222222222222222222222222222 0000000000000000000000000000000000000000 D", "removed-sub",
		":100644 100644 3333333333333333333333333333333333333333 4444444444444444444444444444444444444444 R087", "old.txt", "new.txt",
		":100644 100644 5555555555555555555555555555555555555555 6666666666666666666666666666666666666666 M", "file.txt",
	}));

	REQUIRE(entries.size() == 4);
	CHECK(entries[0].isSubmodule);
	CHECK(entries[0].submoduleSha == QStringLiteral("1111111111111111111111111111111111111111"));
	CHECK(entries[1].isSubmodule);
	CHECK(entries[1].submoduleSha == QStringLiteral("2222222222222222222222222222222222222222"));
	CHECK_FALSE(entries[2].isSubmodule);
	CHECK(entries[2].type == ChangeType::Renamed);
	CHECK(entries[2].oldPath == QStringLiteral("old.txt"));
	CHECK(entries[2].path == QStringLiteral("new.txt"));
	CHECK(entries[3].path == QStringLiteral("file.txt"));
	CHECK(entries[3].submoduleSha.isEmpty());
}

TEST_CASE("Staged raw rows carry both modes, the index object and the status letter", "[gitparsers]")
{
	const std::vector<Git::StagedEntry> entries = Git::parseStagedRawZ(nulTerminated({
		":100644 100755 3333333333333333333333333333333333333333 4444444444444444444444444444444444444444 M", "script.sh",
		":000000 100644 0000000000000000000000000000000000000000 5555555555555555555555555555555555555555 A", "new.txt",
		":100644 000000 6666666666666666666666666666666666666666 0000000000000000000000000000000000000000 D", "gone.txt",
		":100644 000000 7777777777777777777777777777777777777777 0000000000000000000000000000000000000000 U", "conflicted.txt",
	}));

	REQUIRE(entries.size() == 4);
	CHECK(entries[0].path == QStringLiteral("script.sh"));
	CHECK(entries[0].treeMode == "100644");
	CHECK(entries[0].indexMode == "100755");
	CHECK(entries[0].indexSha == "4444444444444444444444444444444444444444");
	CHECK(entries[0].status == "M");
	CHECK(entries[1].treeMode == "000000");
	CHECK(entries[1].status == "A");

	// The letter is all that tells an unmerged path from a deletion: the modes and the object are the same
	CHECK(entries[3].treeMode == entries[2].treeMode);
	CHECK(entries[3].indexMode == entries[2].indexMode);
	CHECK(entries[2].status == "D");
	CHECK(entries[3].status == "U");
}

TEST_CASE("Line counts: a rename is keyed by its new path, a binary file has none", "[gitparsers]")
{
	const std::map<QString, LineCounts> counts = Git::parseNumstatZ(
		nulTerminated({ "-\t-\tblob.bin", "4\t0\tcon flict.txt", "0\t0\t", "old name.txt", "new name.txt", "3\t1\tafter.txt" }));

	CHECK(counts.size() == 3);
	CHECK_FALSE(counts.contains(QStringLiteral("blob.bin")));
	CHECK_FALSE(counts.contains(QStringLiteral("old name.txt")));
	REQUIRE(counts.contains(QStringLiteral("new name.txt")));
	REQUIRE(counts.contains(QStringLiteral("after.txt")));
	CHECK(counts.at(QStringLiteral("con flict.txt")).added == 4);
	CHECK(counts.at(QStringLiteral("after.txt")).added == 3);
	CHECK(counts.at(QStringLiteral("after.txt")).removed == 1);
}

TEST_CASE("Porcelain dirtiness tells tracked changes from untracked files", "[gitparsers]")
{
	SECTION("untracked alone")
	{
		const WorktreeDirtiness dirtiness = Git::parsePorcelainDirtiness(nulTerminated({ "?? untracked.txt" }));
		CHECK(dirtiness.untracked);
		CHECK_FALSE(dirtiness.dirtyTracked);
	}

	SECTION("a rename's origin path is not a record of its own")
	{
		const WorktreeDirtiness dirtiness = Git::parsePorcelainDirtiness(nulTerminated({ "R  new.txt", "?? looks-untracked" }));
		CHECK(dirtiness.dirtyTracked);
		CHECK_FALSE(dirtiness.untracked);
	}
}

TEST_CASE("Commit log records: a separator inside the message survives, a root commit has no parents", "[gitparsers]")
{
	const QByteArray log = QByteArray("7111d06b02662e3a26833e79c6e4f63bb5de11c6\x1f" "aaaa bbbb\x1f" "Ann\x1f" "2026-09-16T14:06:10+03:00\x1f"
		"HEAD -> main, origin/main\x1f" "Subject\n\nBody with \x1f inside\n") + '\0'
		+ QByteArray("2b9db0f1027b55307d2af7d3519953030c1b8996\x1f\x1f" "Bob\x1f" "2026-09-15T10:00:00+03:00\x1f\x1f" "Root\n") + '\0';

	const std::vector<CommitRecord> commits = Git::parseCommitLog(log);

	REQUIRE(commits.size() == 2);
	CHECK(commits[0].parents == QStringList{ QStringLiteral("aaaa"), QStringLiteral("bbbb") });
	CHECK(commits[0].author == QStringLiteral("Ann"));
	CHECK(commits[0].refs == QStringLiteral("HEAD -> main, origin/main"));
	CHECK(commits[0].message == QStringLiteral("Subject\n\nBody with \x1f inside"));
	CHECK(commits[1].parents.isEmpty());
	CHECK(commits[1].message == QStringLiteral("Root"));
}

TEST_CASE("Gitlink entries of a tree listing, other entries skipped", "[gitparsers]")
{
	const std::vector<Git::GitlinkEntry> entries = Git::parseGitlinkEntries(nulTerminated({
		"100644 blob 3333333333333333333333333333333333333333\tfile.txt",
		"160000 commit 4444444444444444444444444444444444444444\tlibs/my sub",
	}));

	REQUIRE(entries.size() == 1);
	CHECK(entries[0].path == QStringLiteral("libs/my sub"));
	CHECK(entries[0].sha == QStringLiteral("4444444444444444444444444444444444444444"));
}

TEST_CASE("An object size is a number or an error, never a zero", "[gitparsers]")
{
	CHECK(Git::parseObjectSize("1234\n") == 1234);
	CHECK_FALSE(Git::parseObjectSize("fatal: path 'x' does not exist in 'HEAD'\n").has_value());
}
