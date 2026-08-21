#pragma once

#include <QTreeWidget>

// The recent repositories dock: one row per repository, expanding to the subrepos its last refresh found (one level).
// Activating a row opens that repository in its own window.
// The rows come from the stored list alone; nothing here starts a process, so a moved or deleted repository
// looks like any other until opened.
class RecentRepositoriesPanel final : public QTreeWidget
{
	Q_OBJECT

public:
	// `currentRepositoryRoot` is the repository of the window this panel belongs to, marked in the list
	explicit RecentRepositoriesPanel(QString currentRepositoryRoot, QWidget* parent = nullptr);

protected:
	// Row heights depend on the width the paths wrap into
	void resizeEvent(QResizeEvent* event) override;

private:
	void rebuild();
	// `parentRoot` is empty for a listed repository, set for a subrepo of one. By root rather than item:
	// opening a repository rebuilds this tree, so no row survives it.
	void openRepository(const QString& root, const QString& parentRoot);
	void showContextMenu(const QPoint& pos);
	[[nodiscard]] static QString rootOf(const QTreeWidgetItem* item); // empty for no item

private:
	const QString _currentRoot;
};
