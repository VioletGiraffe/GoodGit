#pragma once

#include "repository.h"

#include <QObject>
#include <QString>

#include <vector>

// One subrepo of a listed repository, as the list shows it without opening anything
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
	// What the last refresh of this repository found, in path order. Empty until it has been opened once,
	// and stale in the same way the rest of the entry is - the repository moves on while nothing watches it.
	std::vector<RecentSubrepo> subrepos;
};

// The repositories windows have been opened on, most recent first, capped. Only top-level entries are
// listed: a subrepo appears under the one that holds it and never beside it, since opening a subrepo is
// working on its parent.
//
// Every call reads and writes the settings there and then. The list is short enough that no cached copy
// is worth the staleness one window's copy would have while another window writes.
namespace RecentRepositories {

[[nodiscard]] std::vector<RecentRepository> list();

// Records a repository a window just opened on. One that is a listed repository's subrepo bumps that
// repository instead of joining the list itself.
void recordOpen(const RepositoryLocation& location);

// Replaces the recorded subrepos of `repository` with the ones its state now names. Does nothing for a
// repository the list does not hold - a subrepo's own subrepos included, which nothing shows.
void setSubrepos(const Repository& repository);

void forget(const QString& root);

// Announces every write above, so that the panels showing the list rebuild - including those in other
// windows, which is where the list would otherwise go stale.
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
