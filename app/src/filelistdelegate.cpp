#include "filelistdelegate.h"
#include "changedfilesmodel.h"
#include "historymodels.h"
#include "theme.h"

#include <QPainter>

#include <algorithm>

static_assert(int(ChangedFilesModel::StateColumn) == int(CommitFilesModel::StateColumn),
	"the stripe is painted on one column number for both file lists");

void FileListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	QStyledItemDelegate::paint(painter, option, index);

	if (index.column() == ChangedFilesModel::StateColumn && option.state.testFlag(QStyle::State_Selected))
	{
		const Theme& t = activeTheme();
		painter->fillRect(QRect{ option.rect.left(), option.rect.top(), t.metrics.selectionStripeWidth, option.rect.height() }, t.palette.accent);
	}

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
