#include "recentrepositories.h"
#include "settings.h"

#include "settings/csettings.h"

#include <algorithm>

namespace {

constexpr qsizetype MaxRecentRepositories = 10;

[[nodiscard]] QString kindText(VcsKind kind)
{
	return QLatin1String(kind == VcsKind::Mercurial ? Settings::VcsKindMercurial : Settings::VcsKindGit);
}

// Anything but Mercurial's own text reads as git, this being a stored value the app may meet in any state
[[nodiscard]] VcsKind kindFromText(const QString& text)
{
	return text == QLatin1String(Settings::VcsKindMercurial) ? VcsKind::Mercurial : VcsKind::Git;
}

[[nodiscard]] QString subrepoRoot(const RecentRepository& repository, const RecentSubrepo& subrepo)
{
	return repository.root + QLatin1Char('/') + subrepo.path;
}

[[nodiscard]] bool holdsSubrepoAt(const RecentRepository& repository, const QString& root)
{
	return std::ranges::any_of(repository.subrepos,
		[&](const RecentSubrepo& subrepo) { return sameRepositoryPath(subrepoRoot(repository, subrepo), root); });
}

void save(const std::vector<RecentRepository>& repositories)
{
	CSettings settings;
	// Written whole: beginWriteArray only records the new size, leaving any longer previous array's tail
	// behind as entries nothing reads and nothing removes
	settings.remove(QLatin1String(Settings::RecentRepositoriesKey));

	settings.beginWriteArray(QLatin1String(Settings::RecentRepositoriesKey), int(repositories.size()));
	for (size_t i = 0; i < repositories.size(); ++i)
	{
		const RecentRepository& repository = repositories[i];
		settings.setArrayIndex(int(i));
		settings.setValue(QLatin1String(Settings::RecentRepositoryRootKey), repository.root);
		settings.setValue(QLatin1String(Settings::RecentRepositoryKindKey), kindText(repository.kind));

		QStringList paths, kinds;
		for (const RecentSubrepo& subrepo : repository.subrepos)
		{
			paths << subrepo.path;
			kinds << kindText(subrepo.kind);
		}
		settings.setValue(QLatin1String(Settings::RecentRepositorySubrepoPathsKey), paths);
		settings.setValue(QLatin1String(Settings::RecentRepositorySubrepoKindsKey), kinds);
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

		const QStringList paths = settings.value(QLatin1String(Settings::RecentRepositorySubrepoPathsKey)).toStringList();
		const QStringList kinds = settings.value(QLatin1String(Settings::RecentRepositorySubrepoKindsKey)).toStringList();
		for (qsizetype subrepo = 0; subrepo < paths.size(); ++subrepo)
		{
			// The two lists are written together and read apart, so a kind that is not there is the
			// parent's - the common case even when both lists are intact
			const VcsKind kind = subrepo < kinds.size() ? kindFromText(kinds[subrepo]) : repository.kind;
			repository.subrepos.push_back({ paths[subrepo], kind });
		}
		repositories.push_back(std::move(repository));
	}
	settings.endArray();

	return repositories;
}

void recordOpen(const RepositoryLocation& location)
{
	std::vector<RecentRepository> repositories = list();

	const auto moveToFront = [&repositories](std::vector<RecentRepository>::iterator entry) {
		std::rotate(repositories.begin(), entry, entry + 1);
	};

	// A subrepo is opened as part of working on what holds it, so that is what the list records
	const auto parent = std::ranges::find_if(repositories,
		[&](const RecentRepository& repository) { return holdsSubrepoAt(repository, location.root); });
	if (parent != repositories.end())
	{
		if (parent == repositories.begin())
			return; // already the newest, and nothing else about it changes

		moveToFront(parent);
		save(repositories);
		return;
	}

	const auto existing = std::ranges::find_if(repositories,
		[&](const RecentRepository& repository) { return sameRepositoryPath(repository.root, location.root); });
	if (existing != repositories.end())
	{
		existing->kind = location.kind;
		existing->root = location.root; // the spelling of the path that was actually opened
		moveToFront(existing);
	}
	else
	{
		repositories.insert(repositories.begin(), RecentRepository{ location.root, location.kind, {} });
		if (qsizetype(repositories.size()) > MaxRecentRepositories)
			repositories.resize(size_t(MaxRecentRepositories));
	}

	save(repositories);
}

void setSubrepos(const Repository& repository)
{
	std::vector<RecentRepository> repositories = list();
	const auto entry = std::ranges::find_if(repositories,
		[&](const RecentRepository& listed) { return sameRepositoryPath(listed.root, repository.path()); });
	if (entry == repositories.end())
		return;

	std::vector<RecentSubrepo> subrepos;
	subrepos.reserve(size_t(repository.state().submodules.size()));
	for (const QString& path : repository.state().submodules)
		subrepos.push_back({ path, repository.submoduleLocation(path).kind });

	if (subrepos == entry->subrepos)
		return; // a refresh that found what the last one did writes nothing, and announces nothing

	entry->subrepos = std::move(subrepos);
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
