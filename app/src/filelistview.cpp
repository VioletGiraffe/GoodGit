#include "filelistview.h"
#include "changedfilesmodel.h" // for the column layout and the sort key both file lists share
#include "filelistdelegate.h"

DISABLE_COMPILER_WARNINGS
#include <QGuiApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QSortFilterProxyModel>
#include <QStyleOption>
RESTORE_COMPILER_WARNINGS

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

	// QAbstractItemView::activated carries no modifiers: the activating event is the last one delivered, so
	// the application still reports its state
	connect(this, &QAbstractItemView::activated, this,
		[this](const QModelIndex& index) { emit rowActivated(_proxy->mapToSource(index), QGuiApplication::keyboardModifiers()); });
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
	// Rows come off the selection ranges directly: selectedRows() rescans every range per row, quadratic on
	// a fragmented selection
	std::vector<int> proxyRows;
	for (const QItemSelectionRange& range : selectionModel()->selection())
	{
		for (int row = range.top(); row <= range.bottom(); ++row)
			proxyRows.push_back(row);
	}
	std::sort(proxyRows.begin(), proxyRows.end());

	QModelIndexList rows;
	rows.reserve(qsizetype(proxyRows.size()));
	for (const int row : proxyRows)
		rows.push_back(_proxy->mapToSource(_proxy->index(row, StateColumn)));
	return rows;
}

QModelIndex FileListView::firstShownSourceIndex() const
{
	return _proxy->mapToSource(_proxy->index(0, StateColumn));
}

void FileListView::setSelectedSourceRows(const std::vector<int>& rows, int currentRow)
{
	const QAbstractItemModel* source = _proxy->sourceModel();

	// Maximal runs of adjacent proxy rows, one range each: painting scans every range per visible cell
	std::vector<int> proxyRows;
	proxyRows.reserve(rows.size());
	for (const int row : rows)
		proxyRows.push_back(_proxy->mapFromSource(source->index(row, 0)).row());
	std::sort(proxyRows.begin(), proxyRows.end());

	QItemSelection selection;
	for (size_t first = 0; first < proxyRows.size(); )
	{
		size_t last = first;
		while (last + 1 < proxyRows.size() && proxyRows[last + 1] == proxyRows[last] + 1)
			++last;
		selection.select(_proxy->index(proxyRows[first], 0), _proxy->index(proxyRows[last], FileListColumnCount - 1));
		first = last + 1;
	}

	if (currentRow >= 0)
		setCurrentIndex(_proxy->mapFromSource(source->index(currentRow, StateColumn)));

	// After setCurrentIndex, which selects the row it lands on
	if (!selection.isEmpty())
		selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
}
