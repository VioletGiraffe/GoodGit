#pragma once

#include <QTreeWidget>

// The recently opened repositories, as the dock beside a commit window shows them: one row per
// repository, expanding to the subrepos its last refresh found - one level, since a subrepo's own
// subrepos are nobody's workspace. Activating a row opens that repository in its own window.
//
// The rows come from the stored list alone. Nothing here starts a process, so a repository that has
// moved or gone looks like any other until it is opened, which is where that is reported.
class RecentRepositoriesPanel final : public QTreeWidget
{
	Q_OBJECT

public:
	// `currentRepositoryRoot` is the repository of the window this panel belongs to: it is marked in the
	// list rather than left to look like somewhere else to go
	explicit RecentRepositoriesPanel(QString currentRepositoryRoot, QWidget* parent = nullptr);

protected:
	// Row heights follow the width the paths wrap into, so the layout is redone rather than reused
	void resizeEvent(QResizeEvent* event) override;

private:
	void rebuild();
	// Opens one row's repository. `parentRoot` is empty for a listed repository, set for a subrepo of one.
	// Named by root rather than by item: opening a repository rebuilds this tree, so no row survives it.
	void openRepository(const QString& root, const QString& parentRoot);
	void showContextMenu(const QPoint& pos);
	[[nodiscard]] static QString rootOf(const QTreeWidgetItem* item); // empty for no item

private:
	const QString _currentRoot;
};
