#include "recentrepositories.h"
#include "repositoryfactory.h"
#include "settings.h"

#include "settings/csettings.h"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QFileInfo>
#include <QSet>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

constexpr qsizetype MaxRecentRepositories = 100;

[[nodiscard]] QString kindText(VcsKind kind)
{
	return QLatin1String(kind == VcsKind::Mercurial ? Settings::VcsKindMercurial : Settings::VcsKindGit);
}

// Most recently used first. Stable, so entries sharing a timestamp - every one stored before the field
// existed - keep the order they were read in.
void sortByLastUsed(std::vector<RecentRepository>& repositories)
{
	std::ranges::stable_sort(repositories, [](const RecentRepository& left, const RecentRepository& right) {
		return left.lastUsedMSecs > right.lastUsedMSecs;
	});
}

// Anything but Mercurial's text reads as git: the stored value may be anything
[[nodiscard]] VcsKind kindFromText(const QString& text)
{
	return text == QLatin1String(Settings::VcsKindMercurial) ? VcsKind::Mercurial : VcsKind::Git;
}

[[nodiscard]] QString submoduleRoot(const RecentRepository& repository, const Submodule& submodule)
{
	return repository.root + QLatin1Char('/') + submodule.path;
}

[[nodiscard]] bool holdsSubmoduleAt(const RecentRepository& repository, const QString& root)
{
	return std::ranges::any_of(repository.submodules,
		[&](const Submodule& submodule) { return sameRepositoryPath(submoduleRoot(repository, submodule), root); });
}

// Honored only while the claiming repository is still on disk: a deleted parent must not hide its
// ex-submodules from the list forever. A live parent's stale claim stands until the parent is reopened and
// setSubmodules() re-reads it: only the parent knows its current submodules.
[[nodiscard]] bool claimsSubmoduleAt(const RecentRepository& repository, const QString& root)
{
	return holdsSubmoduleAt(repository, root) && QFileInfo::exists(repository.root);
}

// Not atomic: the remove and the writes are separate operations, and no caller re-reads, so a second
// instance can observe a torn list mid-save and store it back
void save(const std::vector<RecentRepository>& repositories)
{
	CSettings settings;
	// beginWriteArray only records the new size, leaving a longer previous array's tail behind
	settings.remove(QLatin1String(Settings::RecentRepositoriesKey));

	settings.beginWriteArray(QLatin1String(Settings::RecentRepositoriesKey), int(repositories.size()));
	for (size_t i = 0; i < repositories.size(); ++i)
	{
		const RecentRepository& repository = repositories[i];
		settings.setArrayIndex(int(i));
		settings.setValue(QLatin1String(Settings::RecentRepositoryRootKey), repository.root);
		settings.setValue(QLatin1String(Settings::RecentRepositoryKindKey), kindText(repository.kind));
		settings.setValue(QLatin1String(Settings::RecentRepositoryLastUsedKey), qint64(repository.lastUsedMSecs));

		QStringList paths, kinds;
		for (const Submodule& submodule : repository.submodules)
		{
			paths << submodule.path;
			kinds << kindText(submodule.kind);
		}
		settings.setValue(QLatin1String(Settings::RecentRepositorySubmodulePathsKey), paths);
		settings.setValue(QLatin1String(Settings::RecentRepositorySubmoduleKindsKey), kinds);
	}
	settings.endArray();

	RecentRepositories::Notifier::instance().notifyChanged();
}

} // namespace

namespace RecentRepositories {

std::vector<RecentRepository> list()
{
	CSettings settings;
	const int count = settings.beginReadArray(QLatin1String(Settings::RecentRepositoriesKey));

	std::vector<RecentRepository> repositories;
	repositories.reserve(size_t(count));
	for (int i = 0; i < count; ++i)
	{
		settings.setArrayIndex(i);
		RecentRepository repository;
		repository.root = settings.value(QLatin1String(Settings::RecentRepositoryRootKey)).toString();
		if (repository.root.isEmpty())
			continue;

		repository.kind = kindFromText(settings.value(QLatin1String(Settings::RecentRepositoryKindKey)).toString());
		// Absent before the field existed, and 0 is exactly what such an entry should sort by
		repository.lastUsedMSecs = settings.value(QLatin1String(Settings::RecentRepositoryLastUsedKey)).toLongLong();

		const QStringList paths = settings.value(QLatin1String(Settings::RecentRepositorySubmodulePathsKey)).toStringList();
		const QStringList kinds = settings.value(QLatin1String(Settings::RecentRepositorySubmoduleKindsKey)).toStringList();
		for (qsizetype submodule = 0; submodule < paths.size(); ++submodule)
		{
			// A missing kind defaults to the parent's
			const VcsKind kind = submodule < kinds.size() ? kindFromText(kinds[submodule]) : repository.kind;
			repository.submodules.push_back({ paths[submodule], kind });
		}
		repositories.push_back(std::move(repository));
	}
	settings.endArray();

	return repositories;
}

void recordOpen(const RepositoryLocation& location)
{
	std::vector<RecentRepository> repositories = list();
	const int64_t now = QDateTime::currentMSecsSinceEpoch();

	const auto moveToFront = [&repositories](std::vector<RecentRepository>::iterator entry) {
		std::rotate(repositories.begin(), entry, entry + 1);
	};

	const auto parent = std::ranges::find_if(repositories,
		[&](const RecentRepository& repository) { return claimsSubmoduleAt(repository, location.root); });
	if (parent != repositories.end())
	{
		parent->lastUsedMSecs = now;
		moveToFront(parent);
		save(repositories);
		return;
	}

	const auto existing = std::ranges::find_if(repositories,
		[&](const RecentRepository& repository) { return sameDirectoryOnDisk(repository.root, location.root); });
	if (existing != repositories.end())
	{
		existing->kind = location.kind;
		existing->root = location.root; // the spelling that was actually opened
		existing->lastUsedMSecs = now;
		moveToFront(existing);
	}
	else
	{
		repositories.insert(repositories.begin(), RecentRepository{ location.root, location.kind, now, {} });
		if (qsizetype(repositories.size()) > MaxRecentRepositories)
			repositories.resize(size_t(MaxRecentRepositories));
	}

	save(repositories);
}

size_t recordFound(const std::vector<FoundRepository>& found)
{
	std::vector<RecentRepository> repositories = list();
	QSet<QString> addedRoots;

	for (const FoundRepository& candidate : found)
	{
		const QString& root = candidate.location.root;
		const bool listed = std::ranges::any_of(repositories, [&](const RecentRepository& repository) {
			return sameRepositoryPath(repository.root, root) || claimsSubmoduleAt(repository, root);
		});
		if (listed)
			continue;

		repositories.push_back(RecentRepository{ root, candidate.location.kind, candidate.lastUsedMSecs, candidate.submodules });
		addedRoots.insert(root);
	}

	if (addedRoots.isEmpty())
		return 0;

	// The cap falls on the oldest of the whole list, so a scan can push out an entry that was already there
	sortByLastUsed(repositories);
	if (qsizetype(repositories.size()) > MaxRecentRepositories)
		repositories.resize(size_t(MaxRecentRepositories));

	const size_t added = size_t(std::ranges::count_if(repositories,
		[&](const RecentRepository& repository) { return addedRoots.contains(repository.root); }));
	if (added == 0)
		return 0; // every one of them was older than the entries the cap kept, which are unchanged

	save(repositories); // one write and one announcement for the whole scan
	return added;
}

void setSubmodules(const Repository& repository)
{
	if (!repository.state().known())
		return; // a failed first refresh would replace the stored list with the empty default

	std::vector<RecentRepository> repositories = list();
	const auto entry = std::ranges::find_if(repositories,
		[&](const RecentRepository& listed) { return sameRepositoryPath(listed.root, repository.path()); });
	if (entry == repositories.end())
		return;

	std::vector<Submodule> submodules;
	submodules.reserve(size_t(repository.state().submodules.size()));
	for (const QString& path : repository.state().submodules)
		submodules.push_back({ path, repository.submoduleLocation(path).kind });

	const bool submodulesChanged = submodules != entry->submodules;
	if (submodulesChanged)
		entry->submodules = std::move(submodules);

	// A top-level entry a submodule shadows: the repository was scanned or opened standalone before this
	// parent claimed it. Checked on every call, so an old duplicate also heals.
	const RecentRepository parent = *entry; // erase_if relocates entries, so the iterator does not survive it
	const size_t sizeBefore = repositories.size();
	std::erase_if(repositories, [&](const RecentRepository& listed) {
		return !sameRepositoryPath(listed.root, parent.root) && holdsSubmoduleAt(parent, listed.root);
	});

	if (!submodulesChanged && repositories.size() == sizeBefore)
		return; // nothing to write or announce
	save(repositories);
}

void forget(const QString& root)
{
	std::vector<RecentRepository> repositories = list();
	const auto entry = std::ranges::find_if(repositories,
		[&](const RecentRepository& repository) { return sameRepositoryPath(repository.root, root); });
	if (entry == repositories.end())
		return;

	repositories.erase(entry);
	save(repositories);
}

} // namespace RecentRepositories
