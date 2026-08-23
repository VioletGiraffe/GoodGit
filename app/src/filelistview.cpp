#include "filelistview.h"
#include "changedfilesmodel.h" // for the column layout both file lists share
#include "filelistdelegate.h"

#include <QHeaderView>
#include <QItemSelectionModel>

#include <algorithm>
#include <assert.h>

FileListView::FileListView(QWidget* parent) :
	QTreeView(parent)
{
	setItemDelegate(new FileListDelegate{ this });
	setRootIsDecorated(false);
	setUniformRowHeights(true);
	setAllColumnsShowFocus(true);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setContextMenuPolicy(Qt::CustomContextMenu);
	header()->hide();

	connect(this, &QAbstractItemView::activated, this, [this](const QModelIndex& index) { emit rowActivated(index); });
}

void FileListView::setModel(QAbstractItemModel* model)
{
	assert(model); // the sizing below addresses sections, which exist only once there is a model
	QTreeView::setModel(model);

	header()->setSectionResizeMode(StateColumn, QHeaderView::ResizeToContents);
	header()->setSectionResizeMode(AddedColumn, QHeaderView::ResizeToContents);
	header()->setSectionResizeMode(RemovedColumn, QHeaderView::ResizeToContents);
	header()->setSectionResizeMode(PathColumn, QHeaderView::Stretch);
}

QModelIndex FileListView::currentSourceIndex() const
{
	return currentIndex();
}

QModelIndex FileListView::sourceIndexAt(const QPoint& viewportPos) const
{
	return indexAt(viewportPos);
}

QModelIndexList FileListView::selectedSourceRows() const
{
	QModelIndexList rows = selectionModel()->selectedRows(StateColumn);
	std::sort(rows.begin(), rows.end(), [](const QModelIndex& left, const QModelIndex& right) { return left.row() < right.row(); });
	return rows;
}

void FileListView::setSelectedSourceRows(const std::vector<int>& rows, int currentRow)
{
	QItemSelection selection;
	for (const int row : rows)
		selection.select(model()->index(row, 0), model()->index(row, FileListColumnCount - 1));

	if (currentRow >= 0)
		setCurrentIndex(model()->index(currentRow, StateColumn));

	// After setCurrentIndex, which selects the row it lands on
	if (!selection.isEmpty())
		selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
}
