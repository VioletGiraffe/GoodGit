#pragma once

#include <QStyledItemDelegate>

// Paints the commit graph column: the lines crossing a row and the row's own node, read from the model's
// GraphRole. Its width follows the lane count the same model reports, so it fits the whole list at once.
class CommitGraphDelegate final : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
