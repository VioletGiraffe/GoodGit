#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "changedfilesmodel.h"
#include "fileicons.h"
#include "filelistview.h"
#include "settings.h"
#include "theme.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

namespace {

[[nodiscard]] FileEntry fileRow(const QString& path, ChangeType type)
{
	return { .path = path, .type = type };
}

[[nodiscard]] FileEntry blockedSubmoduleRow(const QString& path)
{
	return { .path = path, .isSubmodule = true, .content = SubmoduleContent::DirtyTracked };
}

[[nodiscard]] FileEntry movedSubmoduleRow(const QString& path)
{
	return { .path = path, .isSubmodule = true, .pointerMoved = true };
}

[[nodiscard]] FileEntry untrackedRepositoryRow(const QString& path)
{
	return { .path = path, .type = ChangeType::Untracked, .isUntrackedRepository = true };
}

// The paths in the order the view shows them
[[nodiscard]] QStringList shownPaths(const FileListView& view)
{
	const QAbstractItemModel* shown = view.model();
	QStringList paths;
	for (int row = 0; row < shown->rowCount(); ++row)
		paths.push_back(shown->index(row, PathColumn).data(SortPathRole).toString());
	return paths;
}

[[nodiscard]] qint64 decorationKey(const QAbstractItemModel& model, int row, int column)
{
	return qvariant_cast<QIcon>(model.index(row, column).data(Qt::DecorationRole)).cacheKey();
}

} // namespace

TEST_CASE("Status order: blocked submodules, tracked submodules, tracked files, then everything untracked", "[filelist]")
{
	ChangedFilesModel model;
	// The paths sort against the expected order, so only the status rank can produce it
	model.setEntries({
		untrackedRepositoryRow(QStringLiteral("a-repo")),
		fileRow(QStringLiteral("b.txt"), ChangeType::Untracked),
		movedSubmoduleRow(QStringLiteral("y-sub")),
		fileRow(QStringLiteral("d.txt"), ChangeType::Deleted),
		fileRow(QStringLiteral("e.txt"), ChangeType::Modified),
		fileRow(QStringLiteral("f.txt"), ChangeType::Conflicted),
		blockedSubmoduleRow(QStringLiteral("z-blocked")),
	}, /*mergeMode=*/false);

	FileListView view;
	view.setModel(&model);

	const QStringList expected{ QStringLiteral("z-blocked"), QStringLiteral("y-sub"), QStringLiteral("f.txt"), QStringLiteral("e.txt"),
		QStringLiteral("d.txt"), QStringLiteral("a-repo"), QStringLiteral("b.txt") };
	CHECK(shownPaths(view) == expected);
}

TEST_CASE("An untracked repository row cannot be checked, and shows the folder glyph beside its path", "[filelist]")
{
	ChangedFilesModel model;
	model.setEntries({ untrackedRepositoryRow(QStringLiteral("nested")), fileRow(QStringLiteral("new.txt"), ChangeType::Untracked) },
		/*mergeMode=*/false);
	constexpr int repositoryRow = 0, fileRowIndex = 1;

	CHECK(model.index(repositoryRow, StateColumn).data(Qt::DisplayRole).toString() == QStringLiteral("Untracked repository"));
	CHECK_FALSE(model.index(repositoryRow, StateColumn).data(Qt::CheckStateRole).isValid());
	CHECK_FALSE(model.isUserCheckable(repositoryRow));
	CHECK(model.checkableCount() == 1);

	model.setAllChecked(true);
	CHECK_FALSE(model.isChecked(repositoryRow));
	CHECK(model.checkedUntrackedPaths() == QStringList{ QStringLiteral("new.txt") });

	CHECK(decorationKey(model, repositoryRow, PathColumn) == submoduleIcon().cacheKey());
	CHECK(decorationKey(model, fileRowIndex, PathColumn) == fileTypeIcon(QStringLiteral("new.txt")).cacheKey());
	CHECK(model.index(fileRowIndex, StateColumn).data(Qt::DecorationRole).isNull());
}

TEST_CASE("Merge mode forces the tracked rows on and leaves an untracked repository unchecked", "[filelist]")
{
	ChangedFilesModel model;
	model.setEntries({ fileRow(QStringLiteral("merged.txt"), ChangeType::Modified), untrackedRepositoryRow(QStringLiteral("nested")) },
		/*mergeMode=*/true);

	CHECK(model.isChecked(0));
	CHECK_FALSE(model.isUserCheckable(0));
	CHECK_FALSE(model.isChecked(1));
	CHECK_FALSE(model.index(1, StateColumn).data(Qt::CheckStateRole).isValid());
}

TEST_CASE("Becoming Added checks the row, and unchecking it afterwards survives the next refresh", "[filelist]")
{
	const std::vector<FileEntry> added{ fileRow(QStringLiteral("new.txt"), ChangeType::Added) };

	ChangedFilesModel model;
	model.setEntries({ fileRow(QStringLiteral("new.txt"), ChangeType::Untracked) }, /*mergeMode=*/false);
	model.setRowChecked(0, false); // whatever the new-row setting made it

	model.setEntries(added, /*mergeMode=*/false);
	CHECK(model.isChecked(0));

	model.setRowChecked(0, false);
	model.setEntries(added, /*mergeMode=*/false);
	CHECK_FALSE(model.isChecked(0)); // Added at the previous refresh too, so the unchecking was a choice
}

TEST_CASE("A row first seen as Added is checked even where new rows start unchecked", "[filelist]")
{
	QSettings settings;
	const QVariant previousPolicy = settings.value(Settings::NewRowCheckPolicyKey);
	settings.setValue(Settings::NewRowCheckPolicyKey, QLatin1String(Settings::NewRowCheckPolicyNone));

	ChangedFilesModel model;
	model.setEntries({ fileRow(QStringLiteral("added.txt"), ChangeType::Added), fileRow(QStringLiteral("edited.txt"), ChangeType::Modified) },
		/*mergeMode=*/false);

	CHECK(model.isChecked(0));
	CHECK_FALSE(model.isChecked(1));

	if (previousPolicy.isValid())
		settings.setValue(Settings::NewRowCheckPolicyKey, previousPolicy);
	else
		settings.remove(Settings::NewRowCheckPolicyKey);
}
