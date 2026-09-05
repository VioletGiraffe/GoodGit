#include "recentrepositories.h"
#include "repositoryfactory.h"
#include "settings.h"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
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

// One setValue: a reader in another instance sees the previous list or this one, never a mix of the two.
// Simultaneous writers still lose one update, and the cost of that is an entry's position in the order.
void save(const std::vector<RecentRepository>& repositories)
{
	QJsonArray stored;
	for (const RecentRepository& repository : repositories)
	{
		QJsonArray paths, kinds;
		for (const Submodule& submodule : repository.submodules)
		{
			paths.append(submodule.path);
			kinds.append(kindText(submodule.kind));
		}

		stored.append(QJsonObject{
			{ QLatin1String(Settings::RecentRepositoryRootKey), repository.root },
			{ QLatin1String(Settings::RecentRepositoryKindKey), kindText(repository.kind) },
			{ QLatin1String(Settings::RecentRepositoryLastUsedKey), qint64(repository.lastUsedMSecs) },
			{ QLatin1String(Settings::RecentRepositorySubmodulePathsKey), paths },
			{ QLatin1String(Settings::RecentRepositorySubmoduleKindsKey), kinds } });
	}

	QSettings{}.setValue(QLatin1String(Settings::RecentRepositoriesKey),
		QString::fromUtf8(QJsonDocument{ stored }.toJson(QJsonDocument::Compact)));

	RecentRepositories::Notifier::instance().notifyChanged();
}

} // namespace

namespace RecentRepositories {

std::vector<RecentRepository> list()
{
	const QString text = QSettings{}.value(QLatin1String(Settings::RecentRepositoriesKey)).toString();
	const QJsonArray stored = QJsonDocument::fromJson(text.toUtf8()).array();

	std::vector<RecentRepository> repositories;
	repositories.reserve(size_t(stored.size()));
	for (const QJsonValue& value : stored)
	{
		const QJsonObject entry = value.toObject();
		RecentRepository repository;
		repository.root = entry.value(QLatin1String(Settings::RecentRepositoryRootKey)).toString();
		if (repository.root.isEmpty())
			continue;

		repository.kind = kindFromText(entry.value(QLatin1String(Settings::RecentRepositoryKindKey)).toString());
		repository.lastUsedMSecs = entry.value(QLatin1String(Settings::RecentRepositoryLastUsedKey)).toInteger();

		const QJsonArray paths = entry.value(QLatin1String(Settings::RecentRepositorySubmodulePathsKey)).toArray();
		const QJsonArray kinds = entry.value(QLatin1String(Settings::RecentRepositorySubmoduleKindsKey)).toArray();
		for (qsizetype submodule = 0; submodule < paths.size(); ++submodule)
		{
			// The two lists are written together, so a shorter kinds list means a damaged value
			const VcsKind kind = submodule < kinds.size() ? kindFromText(kinds[submodule].toString()) : repository.kind;
			repository.submodules.push_back({ paths[submodule].toString(), kind });
		}
		repositories.push_back(std::move(repository));
	}

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
