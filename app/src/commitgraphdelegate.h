#pragma once

#include <QStyledItemDelegate>

// Paints the commit graph column from the model's GraphRole. The width follows the model's lane count, so
// it fits the whole list at once.
class CommitGraphDelegate final : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
