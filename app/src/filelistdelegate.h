#pragma once

#include <QStyledItemDelegate>

// Paints the two file-list details that item data roles cannot express: the accent stripe on the
// selected row's left edge, and the deleted-file strikethrough recolored to the deleted-state color
// (a QFont strike always draws in the text color, which makes it near-invisible).
// Serves both file lists - the commit window's and the history window's - which share a column layout.
class FileListDelegate final : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
