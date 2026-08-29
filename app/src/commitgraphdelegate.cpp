#include "commitgraphdelegate.h"
#include "commitgraph.h"
#include "historymodels.h"
#include "theme.h"

DISABLE_COMPILER_WARNINGS
#include <QLineF>
#include <QPainter>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cmath>

namespace {

constexpr qreal NodeRadius = 5.0; // logical pixels
// The current commit's ring, drawn outside the node. The widest thing the column holds, so it is what
// laneInset() and the height hint are sized against
constexpr qreal CurrentRingRadius = NodeRadius + 2.5;

// Clear space at each edge of the column
constexpr qreal GraphMargin = 4.0;

// The column is hinted at least this many lanes wide, so the deeper listing a cold open appends rarely
// widens it mid-view
constexpr int MinHintedLanes = 6;

// The floor suits any ordinary font; the metrics take over for a large one, where the taller row wants
// wider lanes
int laneWidth(const QStyleOptionViewItem& option)
{
	return std::max(14, option.fontMetrics.height() * 3 / 4);
}

// Every line and ring is drawn with this pen width
qreal lineThickness(int width)
{
	return std::max(1.0, width / 8.0);
}

// The widest a node is drawn, ring included
qreal nodeReach(int width)
{
	return CurrentRingRadius + lineThickness(width) / 2;
}

// Where the first lane's centre sits, and the room the last one needs past it. A node is drawn at the middle
// of its lane and reaches past that half width, so the lanes start far enough in to keep the margin clear of
// the column's edge.
qreal laneInset(int width)
{
	return std::max(width / 2.0, nodeReach(width) + GraphMargin);
}

const QColor& chainColor(int chain)
{
	const auto& colors = activeTheme().graphLanes;
	return colors[size_t(chain % int(colors.size()))];
}

// Lines and nodes above the current commit are faded: the checkout does not hold that history yet
QColor faded(QColor color)
{
	color.setAlphaF(0.4f);
	return color;
}

// Where a line ending at the node meets the node's edge. A faded node is translucent, so a line may not run
// under it.
QPointF nodeEdgeToward(QPointF node, QPointF other)
{
	QLineF toOther{ node, other };
	toOther.setLength(NodeRadius);
	return toOther.p2();
}

} // namespace

void CommitGraphDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	QStyledItemDelegate::paint(painter, option, index); // the row background, selection included

	const GraphRow row = qvariant_cast<GraphRow>(index.data(CommitLogModel::GraphRole));
	const int width = laneWidth(option);
	const auto x = [&, inset = laneInset(width)](int lane) { return option.rect.left() + inset + lane * width; };
	const qreal top = option.rect.top();
	const qreal bottom = option.rect.bottom() + 1; // the next row's top edge, so a lane draws as one unbroken line
	const qreal middle = option.rect.center().y();
	// Lane -1 is the node itself
	const auto endpoint = [&](int lane, qreal edge) { return lane < 0 ? QPointF{ x(row.lane), middle } : QPointF{ x(lane), edge }; };

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);

	const qreal thickness = lineThickness(width);
	for (const GraphSegment& segment : row.segments)
	{
		QPen pen{ segment.ahead ? faded(chainColor(segment.chain)) : chainColor(segment.chain), thickness };
		if (segment.elided)
			pen.setStyle(Qt::DashLine);
		painter->setPen(pen);

		QPointF from = endpoint(segment.fromLane, top);
		QPointF to = endpoint(segment.toLane, bottom);
		if (segment.fromLane < 0)
			from = nodeEdgeToward(from, to);
		else if (segment.toLane < 0)
			to = nodeEdgeToward(to, from);
		painter->drawLine(from, to);
	}

	// An unpushed commit is a dotted ring rather than a disc, the row's own background showing inside it.
	// Round caps, or the dots draw as squares against the curve.
	const bool unpushed = index.data(CommitLogModel::UnpushedRole).toBool();
	const QColor nodeColor = row.ahead ? faded(chainColor(row.chain)) : chainColor(row.chain);
	const QPen ringPen{ nodeColor, thickness, Qt::DotLine, Qt::RoundCap };
	painter->setPen(unpushed ? ringPen : QPen{ Qt::NoPen });
	painter->setBrush(unpushed ? QBrush{ Qt::NoBrush } : QBrush{ nodeColor });
	painter->drawEllipse(QPointF{ x(row.lane), middle }, NodeRadius, NodeRadius);

	// The current commit takes a second ring outside the node: the fill and the inner ring are spoken for
	if (row.current)
	{
		const CBasePalette& palette = activeTheme().palette;
		painter->setPen(QPen{ option.state.testFlag(QStyle::State_Selected) ? palette.selectionFg : palette.text, thickness });
		painter->setBrush(Qt::NoBrush);
		painter->drawEllipse(QPointF{ x(row.lane), middle }, CurrentRingRadius, CurrentRingRadius);
	}
	painter->restore();
}

QSize CommitGraphDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	const QSize base = QStyledItemDelegate::sizeHint(option, index);
	const int lanes = std::max(MinHintedLanes, index.data(CommitLogModel::GraphLaneCountRole).toInt());
	const int width = laneWidth(option);
	// The lanes between the two insets, which are what the outermost nodes need. The height matters only in a
	// theme whose rows are shorter than a node is tall.
	return { int(std::ceil(2 * laneInset(width) + (lanes - 1) * width)),
		std::max(base.height(), int(std::ceil(2 * nodeReach(width)))) };
}
