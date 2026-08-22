#pragma once

#include "repositoryfactory.h"

#include <QObject>
#include <QString>

#include <vector>

struct RecentSubrepo
{
	QString path;  // relative to the repository that holds it
	VcsKind kind;  // need not be the parent's: a git subrepo may sit inside a Mercurial repository

	[[nodiscard]] bool operator==(const RecentSubrepo&) const = default;
};

struct RecentRepository
{
	QString root;
	VcsKind kind;
	int64_t lastUsedMSecs = 0; // 0 in an entry stored before the field existed, which sorts it last
	// From the last refresh of this repository, in path order. Empty until it has been opened once.
	std::vector<RecentSubrepo> subrepos;
};

// The repositories windows have been opened on, and the ones a folder scan found, most recently used first, capped.
// Only top-level repositories are listed; a subrepo appears under its parent, since opening it is working on the parent.
// Every call reads and writes the settings directly: a cached copy would go stale whenever another window writes.
namespace RecentRepositories {

[[nodiscard]] std::vector<RecentRepository> list();

// A repository that is a listed one's subrepo bumps that repository instead of joining the list itself
void recordOpen(const RepositoryLocation& location);

// Adds the ones not listed yet, each placed among the entries already there by its timestamp. The cap then
// falls on the oldest of the merged list, whichever side they came from. Returns how many were added and kept.
size_t recordFound(const std::vector<FoundRepository>& found);

// Replaces the recorded subrepos with the ones the repository's state names. Does nothing for a repository
// the list does not hold, a subrepo's own subrepos included.
void setSubrepos(const Repository& repository);

void forget(const QString& root);

// Announces every write, so the panels in every window rebuild
class Notifier final : public QObject
{
	Q_OBJECT

public:
	[[nodiscard]] static Notifier& instance()
	{
		static Notifier notifier;
		return notifier;
	}

	void notifyChanged() { emit changed(); }

signals:
	void changed();
};

} // namespace RecentRepositories
