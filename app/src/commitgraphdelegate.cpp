#include "commitgraphdelegate.h"
#include "commitgraph.h"
#include "historymodels.h"
#include "theme.h"

#include <QPainter>

#include <algorithm>

namespace {

constexpr qreal NodeRadius = 5.0; // a 10px node, in logical pixels - the display's scaling applies on top

// The column is hinted at least this many lanes wide, so the deeper listing a cold open appends rarely
// widens it mid-view; a diagram that genuinely needs more still gets more
constexpr int MinHintedLanes = 6;

// One lane's share of the width, leaving a node the same gap either side. The floor is what any ordinary
// font gets; the metrics take over only for a large one, where the taller row wants wider lanes to match.
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
	const qreal bottom = option.rect.bottom() + 1; // the next row's top edge, so a lane reads as one unbroken line
	const qreal middle = option.rect.center().y();
	// A lane of -1 is the node itself, where a line begins or ends; any other meets the row's edge
	const auto endpoint = [&](int lane, qreal edge) { return lane < 0 ? QPointF{ x(row.lane), middle } : QPointF{ x(lane), edge }; };

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);

	const qreal thickness = std::max(1.0, width / 8.0);
	for (const GraphSegment& segment : row.segments)
	{
		QPen pen{ chainColor(segment.chain), thickness };
		if (segment.elided)
			pen.setStyle(Qt::DashLine); // commits between the two ends are hidden: not one step but several
		painter->setPen(pen);
		painter->drawLine(endpoint(segment.fromLane, top), endpoint(segment.toLane, bottom));
	}

	// A commit no upstream has seen is a dotted ring rather than a disc. The lines are drawn first and run
	// behind the node, so the ring is filled with the row's own background rather than left open - no line
	// crosses it. Round caps, or the dots draw as squares against the curve.
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
