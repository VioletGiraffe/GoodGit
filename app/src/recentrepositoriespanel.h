#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QSet>
#include <QTreeWidget>
RESTORE_COMPILER_WARNINGS

class QLineEdit;

// The recent repositories dock: one row per repository, expanding to the submodules its last refresh found (one level).
// Activating a row opens that repository in its own window.
// The rows come from the stored list alone; nothing here starts a process, so a moved or deleted repository
// looks like any other until opened.
class RecentRepositoriesPanel final : public QTreeWidget
{
public:
	// `currentRepositoryRoot` is the repository of the window this panel belongs to, marked in the list
	explicit RecentRepositoriesPanel(QString currentRepositoryRoot, QWidget* parent = nullptr);

	// Keeps the rows whose absolute path contains `text`, plus the parent of every submodule kept, and
	// expands those parents. An empty filter keeps everything and restores the expansion the user left.
	void setFilter(const QString& text);

	// The filter field for this panel: the same placeholder, tooltip and wiring at every site that shows a panel
	[[nodiscard]] QLineEdit* createFilterField();

	// Puts the keyboard cursor on the first row a filter left visible and takes the focus: Enter then opens it
	void focusFirstRow();

protected:
	// Row heights depend on the width the paths wrap into
	void resizeEvent(QResizeEvent* event) override;
	// Watches the field createFilterField() made: Escape clears it, Up, Down and Enter move the keyboard to the list
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void rebuild();
	void setRepositoryExpanded(const QString& root, bool expanded);
	void rememberExpansion();
	void applyFilter();
	[[nodiscard]] bool matchesFilter(const QTreeWidgetItem* item) const;
	// `parentRoot` is empty for a listed repository, set for a submodule of one. By root rather than item:
	// opening a repository rebuilds this tree, so no row survives it.
	void openRepository(const QString& root, const QString& parentRoot);
	void showContextMenu(const QPoint& pos);
	void focusRow(QTreeWidgetItem* item); // a null item does nothing: a filter can leave no row to move to
	// In the order the rows are drawn in, so a filter's hidden rows and a collapsed repository's submodules are skipped
	[[nodiscard]] QTreeWidgetItem* firstVisibleRow();
	[[nodiscard]] QTreeWidgetItem* lastVisibleRow();
	[[nodiscard]] QTreeWidgetItem* itemForRoot(const QString& root); // null where the list no longer holds `root`
	[[nodiscard]] static QString rootOf(const QTreeWidgetItem* item); // empty for no item

private:
	const QString _currentRoot;
	QString _filter; // forward slashes, so a typed backslash matches a stored root
	QSet<QString> _expandedRoots; // what the user left expanded, re-applied on every rebuild
};
