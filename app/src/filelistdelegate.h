#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QStyledItemDelegate>
RESTORE_COMPILER_WARNINGS

// Paints what item data roles cannot express: the accent stripe on the selected row's left edge, and the
// deleted-file strikethrough in the deleted-state color (a QFont strike draws in the text color, near-invisible).
// Serves both file lists, which share a column layout.
class FileListDelegate final : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
