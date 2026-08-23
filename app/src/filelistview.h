#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QTreeView>
RESTORE_COMPILER_WARNINGS

#include <vector>

class QSortFilterProxyModel;

// The file list both windows show: the commit window's pending changes, and one commit's files in the
// history window. Owns the column sizing, the delegate and the sort proxy they share.
// Every index it hands out or takes is the source model's, never the sorted order the view shows.
class FileListView final : public QTreeView
{
	Q_OBJECT

public:
	explicit FileListView(QWidget* parent = nullptr);

	// Takes the model the rows come from; the view's own model is a sort proxy over it
	void setModel(QAbstractItemModel* sourceModel) override;

	[[nodiscard]] QModelIndex currentSourceIndex() const;
	[[nodiscard]] QModelIndex sourceIndexAt(const QPoint& viewportPos) const;
	// One index per selected row, in the order the rows are shown
	[[nodiscard]] QModelIndexList selectedSourceRows() const;
	// The topmost row in the order shown; invalid when the list is empty
	[[nodiscard]] QModelIndex firstShownSourceIndex() const;
	// Selects exactly these rows; a currentRow below zero sets none
	void setSelectedSourceRows(const std::vector<int>& rows, int currentRow);

signals:
	// Double-click or Enter on a row
	void rowActivated(const QModelIndex& sourceIndex);

private:
	QSortFilterProxyModel* const _proxy;
};
