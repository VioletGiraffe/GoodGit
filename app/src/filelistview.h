#pragma once

#include <QTreeView>

#include <vector>

// The file list both windows show: the commit window's pending changes, and one commit's files in the
// history window. Owns the column sizing and the delegate they share.
// Every index it hands out or takes is the model's own, never the view's row order.
class FileListView final : public QTreeView
{
	Q_OBJECT

public:
	explicit FileListView(QWidget* parent = nullptr);

	void setModel(QAbstractItemModel* model) override;

	[[nodiscard]] QModelIndex currentSourceIndex() const;
	[[nodiscard]] QModelIndex sourceIndexAt(const QPoint& viewportPos) const;
	// One index per selected row, in row order rather than the order the rows were picked
	[[nodiscard]] QModelIndexList selectedSourceRows() const;
	// Selects exactly these rows; a currentRow below zero sets none
	void setSelectedSourceRows(const std::vector<int>& rows, int currentRow);

signals:
	// Double-click or Enter on a row
	void rowActivated(const QModelIndex& sourceIndex);
};
