#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "hgparsers.h"

// The JSON inputs are hg's own output, trimmed to the fields and records each test needs

TEST_CASE("Status records: a rename is one entry, removed and missing files are both deleted", "[hgparsers]")
{
	const std::vector<CommitFileChange> entries = Hg::parseStatus(R"([
		{"itemtype": "file", "path": "keep.txt", "status": "M"},
		{"itemtype": "file", "path": "added.txt", "status": "A"},
		{"itemtype": "file", "path": "new.txt", "source": "old.txt", "status": "A"},
		{"itemtype": "file", "path": "old.txt", "status": "R"},
		{"itemtype": "file", "path": "removed.txt", "status": "R"},
		{"itemtype": "file", "path": "missing.txt", "status": "!"},
		{"itemtype": "file", "path": "untracked.txt", "status": "?"}
	])");

	REQUIRE(entries.size() == 6);
	CHECK(entries[0].type == ChangeType::Modified);
	CHECK(entries[1].type == ChangeType::Added);
	CHECK(entries[2].type == ChangeType::Renamed);
	CHECK(entries[2].path == QStringLiteral("new.txt"));
	CHECK(entries[2].oldPath == QStringLiteral("old.txt"));
	CHECK(entries[3].path == QStringLiteral("removed.txt"));
	CHECK(entries[3].type == ChangeType::Deleted);
	CHECK(entries[4].type == ChangeType::Deleted);
	CHECK(entries[5].type == ChangeType::Untracked);
}

TEST_CASE("A path byte hg could not decode arrives as a lone surrogate, and valid UTF-8 stays intact", "[hgparsers]")
{
	// hg's utf8b writes the undecodable byte 0xE9 as U+DCE9, raw in WTF-8: ED B3 A9
	const std::vector<CommitFileChange> entries = Hg::parseStatus(
		"[{\"path\": \"caf\xED\xB3\xA9.txt\", \"status\": \"?\"}, {\"path\": \"\xED\x95\x9C.txt\", \"status\": \"?\"}]");

	REQUIRE(entries.size() == 2);
	CHECK(entries[0].path == QStringLiteral("caf") + QChar{ 0xDCE9 } + QStringLiteral(".txt"));
	CHECK(entries[1].path == QString::fromUtf8("\xED\x95\x9C.txt"));
}

TEST_CASE("Records after an incoming or outgoing preamble, and none where it found nothing", "[hgparsers]")
{
	const QByteArray found = "comparing with https://example.invalid/[repo]\nsearching for changes\n"
		"[\n {\"node\": \"3bf090a7f6cbcee054e5d10ab001fe023b9a764c\", \"parents\": [], \"desc\": \"incoming\"}\n]\n";
	CHECK(Hg::parseCommitLog(found).size() == 1);

	CHECK(Hg::parseCommitLog("comparing with https://example.invalid/[repo]\nsearching for changes\nno changes found\n").empty());
}

TEST_CASE("Log records: revision 0, the null parent dropped, the date in the committer's offset, refs without tip", "[hgparsers]")
{
	const std::vector<CommitRecord> commits = Hg::parseCommitLog(R"([
		{"bookmarks": [], "branch": "default", "date": [1789556798, -10800], "desc": "base\nbody line",
		 "node": "3bf090a7f6cbcee054e5d10ab001fe023b9a764c", "parents": ["0000000000000000000000000000000000000000"],
		 "rev": 0, "tags": ["tip", "v1"], "user": "Tester <t@example.invalid>"},
		{"bookmarks": ["mark"], "branch": "feature", "date": [1789556799, 0],
		 "desc": "feature", "node": "50844cf0842c3a9b5eb74c5f0305fb408fa6c3ee", "parents": ["3bf090a7f6cbcee054e5d10ab001fe023b9a764c"],
		 "rev": 2, "tags": [], "user": "No Email"}
	])");

	REQUIRE(commits.size() == 2);
	REQUIRE(commits[0].revision.has_value());
	CHECK(*commits[0].revision == 0);
	CHECK(commits[0].parents.isEmpty());
	CHECK(commits[0].author == QStringLiteral("Tester"));
	CHECK(commits[0].date == QStringLiteral("2026-09-16T14:06:38+03:00"));
	CHECK(commits[0].refs == QStringLiteral("v1"));
	CHECK(commits[0].message == QStringLiteral("base\nbody line"));

	CHECK(commits[1].author == QStringLiteral("No Email"));
	CHECK(commits[1].refs == QStringLiteral("mark, feature"));
}

TEST_CASE("The working directory of an uncommitted merge has two parents", "[hgparsers]")
{
	const Hg::WorkingDirectory directory = Hg::parseWorkingDirectory(R"([
		{"branch": "default", "node": "ffffffffffffffffffffffffffffffffffffffff",
		 "parents": ["1ea189706d0e67cd505f15a705f617b3eff489e7", "50844cf0842c3a9b5eb74c5f0305fb408fa6c3ee"]}
	])");

	CHECK(directory.parents.size() == 2);
	CHECK(directory.branch == QStringLiteral("default"));
}

TEST_CASE("Unresolved paths: content and path conflicts, not resolved files", "[hgparsers]")
{
	const QStringList paths = Hg::parseUnresolvedPaths(R"([
		{"mergestatus": "U", "path": "content.txt"},
		{"mergestatus": "R", "path": "resolved.txt"},
		{"mergestatus": "P", "path": "moved.txt"}
	])");

	CHECK(paths == QStringList{ QStringLiteral("content.txt"), QStringLiteral("moved.txt") });
}

TEST_CASE("Diff counts come from the hunks, keyed by the header path, and a pure rename has none", "[hgparsers]")
{
	const std::map<QString, LineCounts> counts = Hg::parseDiffCounts(
		"diff --git a/added.txt b/added.txt\nnew file mode 100644\n--- /dev/null\n+++ b/added.txt\n@@ -0,0 +1,1 @@\n+added\n"
		"diff --git a/my file.txt b/my file.txt\n--- a/my file.txt\n+++ b/my file.txt\n@@ -1,2 +1,1 @@\n-before\n--- a/looks-like-a-header\n+after\n"
		"diff --git a/old.txt b/new.txt\nrename from old.txt\nrename to new.txt\n"
		"diff --git a/removed.txt b/removed.txt\ndeleted file mode 100644\n--- a/removed.txt\n+++ /dev/null\n@@ -1,1 +0,0 @@\n-gone\n");

	CHECK(counts.size() == 3);
	REQUIRE(counts.contains(QStringLiteral("added.txt")));
	REQUIRE(counts.contains(QStringLiteral("my file.txt")));
	REQUIRE(counts.contains(QStringLiteral("removed.txt")));
	CHECK(counts.at(QStringLiteral("added.txt")).added == 1);
	// A removed line reading like a header is still a removed line
	CHECK(counts.at(QStringLiteral("my file.txt")).removed == 2);
	CHECK(counts.at(QStringLiteral("my file.txt")).added == 1);
	CHECK(counts.at(QStringLiteral("removed.txt")).removed == 1);
}

TEST_CASE("Grep matches count per changeset, newest first", "[hgparsers]")
{
	const std::vector<Hg::GrepMatch> matches = Hg::parseGrepDiff(R"([
		{"change": "+", "node": "aaaa", "rev": 3},
		{"change": "-", "node": "aaaa", "rev": 3},
		{"change": "+", "node": "aaaa", "rev": 3},
		{"change": "+", "node": "bbbb", "rev": 5}
	])");

	REQUIRE(matches.size() == 2);
	CHECK(matches[0].node == QStringLiteral("bbbb"));
	CHECK(matches[1].matchedLines.added == 2);
	CHECK(matches[1].matchedLines.removed == 1);
}

TEST_CASE("Subrepo files: state and sources by path, the pointer changes of a substate diff", "[hgparsers]")
{
	const std::map<QString, QString> state = Hg::parseSubrepoState("# comment\n\nabc123 libs/my sub\n");
	CHECK(state == std::map<QString, QString>{ { QStringLiteral("libs/my sub"), QStringLiteral("abc123") } });

	const std::map<QString, QString> sources = Hg::parseSubrepoSources("libs/hg = https://example.invalid/hg\nlibs/git = [git]https://example.invalid/git\n");
	REQUIRE(sources.size() == 2);
	CHECK(Hg::subrepoKind(sources.at(QStringLiteral("libs/hg"))) == VcsKind::Mercurial);
	CHECK(Hg::subrepoKind(sources.at(QStringLiteral("libs/git"))) == VcsKind::Git);

	const std::vector<Hg::SubrepoPointerChange> changes = Hg::parseSubstateDiff(
		"--- a/.hgsubstate\n+++ b/.hgsubstate\n@@ -1,2 +1,2 @@\n-old1 libs/a\n-gone libs/removed\n+new1 libs/a\n+added libs/new\n");
	REQUIRE(changes.size() == 3);
	CHECK(changes[0].path == QStringLiteral("libs/a"));
	CHECK(changes[0].oldNode == QStringLiteral("old1"));
	CHECK(changes[0].newNode == QStringLiteral("new1"));
	CHECK(changes[1].path == QStringLiteral("libs/new"));
	CHECK(changes[1].oldNode.isEmpty());
	CHECK(changes[2].path == QStringLiteral("libs/removed"));
	CHECK(changes[2].newNode.isEmpty());
}
