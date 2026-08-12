#include "filelistdelegate.h"
#include "changedfilesmodel.h"
#include "theme.h"

#include <QPainter>

#include <algorithm>

void FileListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	QStyledItemDelegate::paint(painter, option, index);

	if (index.column() == ChangedFilesModel::StateColumn && option.state.testFlag(QStyle::State_Selected))
		painter->fillRect(QRect{ option.rect.left(), option.rect.top(), 2, option.rect.height() }, activeTheme().accent);

	if (const QFont font = qvariant_cast<QFont>(index.data(Qt::FontRole)); !font.strikeOut())
		return;

	// Redraw the strike over the base-painted one, same position and thickness, in the deleted color
	QStyleOptionViewItem opt = option;
	initStyleOption(&opt, index);
	const QRect textRect = opt.widget->style()->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
	const QFontMetrics fm{ opt.font };
	const int width = std::min(fm.horizontalAdvance(opt.text), textRect.width());
	const int baseline = textRect.top() + (textRect.height() + fm.ascent() - fm.descent()) / 2;
	const int y = baseline - fm.strikeOutPos();

	painter->save();
	painter->setPen(QPen{ activeTheme().stDeleted, qreal(fm.lineWidth()) });
	painter->drawLine(textRect.left(), y, textRect.left() + width, y);
	painter->restore();
}
