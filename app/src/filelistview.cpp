#include "filelistview.h"
#include "changedfilesmodel.h" // for the column layout and the sort key both file lists share
#include "filelistdelegate.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QSortFilterProxyModel>
#include <QStyleOption>

#include <algorithm>
#include <assert.h>

namespace {

[[nodiscard]] bool sortableColumn(int column)
{
	return column == StateColumn || column == PathColumn; // the counts are shown, not sorted by
}

// The status column orders by rank, then by path; the path column by path alone.
// Descending flips both keys: the proxy inverts the whole comparison.
class FileListSortProxy final : public QSortFilterProxyModel
{
public:
	using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
	bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
	{
		if (left.column() == StateColumn)
		{
			const int leftRank = left.data(SortRankRole).toInt();
			const int rightRank = right.data(SortRankRole).toInt();
			if (leftRank != rightRank)
				return leftRank < rightRank;
		}
		// Paths are unique, so this settles every remaining pair
		return QString::compare(left.data(SortPathRole).toString(), right.data(SortPathRole).toString(), Qt::CaseInsensitive) < 0;
	}
};

// The count columns are inert: a click must not move the sort indicator there, and a hover must not suggest
// it could. QHeaderView has no per-section clickability, hence the two overrides.
class FileListHeader final : public QHeaderView
{
public:
	explicit FileListHeader(QWidget* parent) :
		QHeaderView(Qt::Horizontal, parent)
	{}

protected:
	// Only the press is dropped: the base needs every release, to clear the state a press on a sortable section left
	void mousePressEvent(QMouseEvent* event) override
	{
		if (sortableColumn(logicalIndexAt(event->position().toPoint())))
			QHeaderView::mousePressEvent(event);
	}

	void initStyleOptionForIndex(QStyleOptionHeader* option, int logicalIndex) const override
	{
		QHeaderView::initStyleOptionForIndex(option, logicalIndex);
		if (!sortableColumn(logicalIndex))
			option->state.setFlag(QStyle::State_MouseOver, false);
	}
};

} // namespace

FileListView::FileListView(QWidget* parent) :
	QTreeView(parent),
	_proxy{ new FileListSortProxy{ this } }
{
	setItemDelegate(new FileListDelegate{ this });
	setRootIsDecorated(false);
	setUniformRowHeights(true);
	setAllColumnsShowFocus(true);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setContextMenuPolicy(Qt::CustomContextMenu);
	setHeader(new FileListHeader{ this });

	connect(this, &QAbstractItemView::activated, this,
		[this](const QModelIndex& index) { emit rowActivated(_proxy->mapToSource(index)); });
}

void FileListView::setModel(QAbstractItemModel* sourceModel)
{
	assert(sourceModel); // the sizing and the sort below address sections, which exist only once there is a model
	_proxy->setSourceModel(sourceModel);
	QTreeView::setModel(_proxy);

	header()->setSectionResizeMode(StateColumn, QHeaderView::ResizeToContents);
	header()->setSectionResizeMode(AddedColumn, QHeaderView::ResizeToContents);
	header()->setSectionResizeMode(RemovedColumn, QHeaderView::ResizeToContents);
	header()->setSectionResizeMode(PathColumn, QHeaderView::Stretch);

	header()->setSortIndicator(StateColumn, Qt::AscendingOrder);
	setSortingEnabled(true); // sorts by the indicator just set
}

QModelIndex FileListView::currentSourceIndex() const
{
	return _proxy->mapToSource(currentIndex());
}

QModelIndex FileListView::sourceIndexAt(const QPoint& viewportPos) const
{
	return _proxy->mapToSource(indexAt(viewportPos));
}

QModelIndexList FileListView::selectedSourceRows() const
{
	QModelIndexList rows = selectionModel()->selectedRows(StateColumn);
	std::sort(rows.begin(), rows.end(), [](const QModelIndex& left, const QModelIndex& right) { return left.row() < right.row(); });
	for (QModelIndex& row : rows)
		row = _proxy->mapToSource(row);
	return rows;
}

QModelIndex FileListView::firstShownSourceIndex() const
{
	return _proxy->mapToSource(_proxy->index(0, StateColumn));
}

void FileListView::setSelectedSourceRows(const std::vector<int>& rows, int currentRow)
{
	const QAbstractItemModel* source = _proxy->sourceModel();

	QItemSelection selection;
	for (const int row : rows)
	{
		const QModelIndex index = _proxy->mapFromSource(source->index(row, 0));
		selection.select(index, index.siblingAtColumn(FileListColumnCount - 1));
	}

	if (currentRow >= 0)
		setCurrentIndex(_proxy->mapFromSource(source->index(currentRow, StateColumn)));

	// After setCurrentIndex, which selects the row it lands on
	if (!selection.isEmpty())
		selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
}
