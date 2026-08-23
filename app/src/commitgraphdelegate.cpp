#include "commitgraphdelegate.h"
#include "commitgraph.h"
#include "historymodels.h"
#include "theme.h"

DISABLE_COMPILER_WARNINGS
#include <QPainter>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

constexpr qreal NodeRadius = 5.0; // logical pixels

// The column is hinted at least this many lanes wide, so the deeper listing a cold open appends rarely
// widens it mid-view
constexpr int MinHintedLanes = 6;

// The floor suits any ordinary font; the metrics take over for a large one, where the taller row wants
// wider lanes
int laneWidth(const QStyleOptionViewItem& option)
{
	return std::max(14, option.fontMetrics.height() * 3 / 4);
}

const QColor& chainColor(int chain)
{
	const auto& colors = activeTheme().graphLanes;
	return colors[size_t(chain % int(colors.size()))];
}

} // namespace

void CommitGraphDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	QStyledItemDelegate::paint(painter, option, index); // the row background, selection included

	const GraphRow row = qvariant_cast<GraphRow>(index.data(CommitLogModel::GraphRole));
	const int width = laneWidth(option);
	const auto x = [&](int lane) { return qreal(option.rect.left() + width / 2 + lane * width); };
	const qreal top = option.rect.top();
	const qreal bottom = option.rect.bottom() + 1; // the next row's top edge, so a lane draws as one unbroken line
	const qreal middle = option.rect.center().y();
	// Lane -1 is the node itself
	const auto endpoint = [&](int lane, qreal edge) { return lane < 0 ? QPointF{ x(row.lane), middle } : QPointF{ x(lane), edge }; };

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);

	const qreal thickness = std::max(1.0, width / 8.0);
	for (const GraphSegment& segment : row.segments)
	{
		QPen pen{ chainColor(segment.chain), thickness };
		if (segment.elided)
			pen.setStyle(Qt::DashLine);
		painter->setPen(pen);
		painter->drawLine(endpoint(segment.fromLane, top), endpoint(segment.toLane, bottom));
	}

	// An unpushed commit is a dotted ring rather than a disc. The lines run behind the node, so the ring is
	// filled with the row's background to hide them. Round caps, or the dots draw as squares against the curve.
	const bool unpushed = index.data(CommitLogModel::UnpushedRole).toBool();
	const QColor nodeFill = unpushed
		? (option.state.testFlag(QStyle::State_Selected) ? activeTheme().palette.selectionBg : activeTheme().palette.surface)
		: chainColor(row.chain);
	const QPen ringPen{ chainColor(row.chain), thickness, Qt::DotLine, Qt::RoundCap };
	painter->setPen(unpushed ? ringPen : QPen{ Qt::NoPen });
	painter->setBrush(nodeFill);
	painter->drawEllipse(QPointF{ x(row.lane), middle }, NodeRadius, NodeRadius);
	painter->restore();
}

QSize CommitGraphDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	const QSize base = QStyledItemDelegate::sizeHint(option, index);
	const int lanes = std::max(MinHintedLanes, index.data(CommitLogModel::GraphLaneCountRole).toInt());
	return { laneWidth(option) * lanes, base.height() };
}
