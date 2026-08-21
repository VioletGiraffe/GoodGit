#include "recentrepositoriespanel.h"
#include "commitwindow.h"
#include "recentrepositories.h"
#include "repositorywindows.h"
#include "theme.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QSet>
#include <QStyledItemDelegate>

#include <algorithm>

namespace {

// What a row is, beside the name it displays. The root is absolute for every level: a subrepo row opens
// a window like any other.
enum ItemRole
{
	RootRole = Qt::UserRole,
	PathRole,     // the line under the name, in native separators; empty where the name says it all
	BadgeRole,    // the kind, where naming it tells the reader something; empty otherwise
	CurrentRole,  // this window's own repository
};

constexpr int HorizontalPadding = 5;
constexpr int VerticalPadding = 3;
constexpr int IconSize = 14;
constexpr int IconSpacing = 5;
constexpr int BadgeSpacing = 6;
constexpr int BadgeTextPadding = 6;
constexpr int BadgeVerticalPadding = 2;
constexpr int MinimumTextWidth = 40;

[[nodiscard]] QFont smallerFont(QFont font)
{
	if (font.pointSizeF() > 0)
		font.setPointSizeF(std::max(1.0, font.pointSizeF() - 1.0));
	else
		font.setPixelSize(std::max(1, font.pixelSize() - 1));
	return font;
}

// `path` across at most two lines: as many whole components as the first line takes, the rest below it,
// elided in the middle where even that does not fit. A path that fits stays one line, and one with
// nothing to break on is elided where it is.
[[nodiscard]] QStringList pathLines(const QString& path, const QFontMetrics& metrics, int width)
{
	if (metrics.horizontalAdvance(path) <= width)
		return { path };

	qsizetype split = -1;
	for (qsizetype i = 0; i < path.size(); ++i)
	{
		if (path[i] != QLatin1Char('/') && path[i] != QLatin1Char('\\'))
			continue;
		if (metrics.horizontalAdvance(path.left(i + 1)) > width)
			break;
		split = i;
	}

	if (split < 0)
		return { metrics.elidedText(path, Qt::ElideMiddle, width) };
	return { path.left(split + 1), metrics.elidedText(path.mid(split + 1), Qt::ElideMiddle, width) };
}

// Two lines per row where the path needs them, an icon on the subrepo rows, and the kind where it is
// worth naming - none of which item roles can express on their own.
class RecentRepositoryDelegate final : public QStyledItemDelegate
{
public:
	explicit RecentRepositoryDelegate(QTreeWidget* view) : QStyledItemDelegate{ view }, _view{ view } {}

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);
		opt.text.clear(); // the row separator; every glyph below is ours

		// The view is NoSelection, so State_Selected never arrives: these two are every background a row has
		const Theme& theme = activeTheme();
		if (opt.state.testFlag(QStyle::State_MouseOver) || opt.state.testFlag(QStyle::State_HasFocus))
			painter->fillRect(option.rect, theme.palette.selectionBg);
		else if (index.data(CurrentRole).toBool())
			painter->fillRect(option.rect, theme.palette.surfaceAlt);
		// Over the fill, so the separator the sheet draws survives it
		opt.widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

		// Over the padding rather than beside it, so the text sits where it does on every other row
		if (index.data(CurrentRole).toBool())
			painter->fillRect(QRect{ option.rect.left(), option.rect.top(), theme.metrics.selectionStripeWidth, option.rect.height() },
				theme.palette.accent);

		const bool subrepo = index.parent().isValid();
		const QFontMetrics nameMetrics{ option.font };
		int x = option.rect.left() + HorizontalPadding;
		int y = option.rect.top() + VerticalPadding;

		if (subrepo)
		{
			const QRect iconRect{ x, y + (nameMetrics.height() - IconSize) / 2, IconSize, IconSize };
			submoduleIcon().paint(painter, iconRect);
			x += IconSize + IconSpacing;
		}

		int nameRight = option.rect.right() - HorizontalPadding;
		if (const QString badge = index.data(BadgeRole).toString(); !badge.isEmpty())
		{
			const QFont badgeFont = smallerFont(option.font);
			const QFontMetrics badgeMetrics{ badgeFont };
			const QSize badgeSize{ badgeMetrics.horizontalAdvance(badge) + 2 * BadgeTextPadding,
				badgeMetrics.height() + 2 * BadgeVerticalPadding };
			// Centred on the name's line rather than sharing its top, the two fonts being different sizes
			const QRect badgeRect{ nameRight - badgeSize.width(), y + (nameMetrics.height() - badgeSize.height()) / 2,
				badgeSize.width(), badgeSize.height() };

			painter->save();
			painter->setRenderHint(QPainter::Antialiasing); // the corners are rounded
			painter->setPen(theme.palette.border);
			painter->setBrush(Qt::NoBrush); // whatever the row's background was drawn with is still set
			painter->setFont(badgeFont);
			// Half a pixel in, so the one-pixel pen lands on a pixel instead of across two
			painter->drawRoundedRect(QRectF{ badgeRect }.adjusted(0.5, 0.5, -0.5, -0.5),
				theme.metrics.controlRadius, theme.metrics.controlRadius);
			painter->setPen(theme.palette.accentText); // accent as text on a plain surface, not the fill
			painter->drawText(badgeRect, Qt::AlignCenter, badge);
			painter->restore();

			nameRight = badgeRect.left() - BadgeSpacing;
		}

		painter->save();
		painter->setPen(theme.palette.text);
		const QRect nameRect{ x, y, std::max(0, nameRight - x), nameMetrics.height() };
		painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
			nameMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, nameRect.width()));
		y += nameMetrics.height();

		if (const QString path = index.data(PathRole).toString(); !path.isEmpty())
		{
			const QFont pathFont = smallerFont(option.font);
			const QFontMetrics pathMetrics{ pathFont };
			const int width = textWidth(index);
			painter->setFont(pathFont);
			painter->setPen(theme.palette.textDim);
			for (const QString& line : pathLines(path, pathMetrics, width))
			{
				painter->drawText(QRect{ x, y, width, pathMetrics.height() }, Qt::AlignLeft | Qt::AlignVCenter, line);
				y += pathMetrics.height();
			}
		}
		painter->restore();
	}

	[[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		int height = QFontMetrics{ option.font }.height();
		if (const QString path = index.data(PathRole).toString(); !path.isEmpty())
		{
			const QFontMetrics pathMetrics{ smallerFont(option.font) };
			height += int(pathLines(path, pathMetrics, textWidth(index)).size()) * pathMetrics.height();
		}
		return { MinimumTextWidth, height + 2 * VerticalPadding };
	}

private:
	// What the text has to fit into. Read off the viewport rather than the item's rect, which is not the
	// row's width when a size is being asked for; painting uses this same width, so both agree.
	[[nodiscard]] int textWidth(const QModelIndex& index) const
	{
		const bool subrepo = index.parent().isValid();
		const int indent = _view->indentation() * (subrepo ? 2 : 1);
		const int icon = subrepo ? IconSize + IconSpacing : 0;
		return std::max(MinimumTextWidth, _view->viewport()->width() - indent - icon - 2 * HorizontalPadding);
	}

private:
	const QTreeWidget* const _view;
};

[[nodiscard]] QString kindLabel(VcsKind kind)
{
	return kind == VcsKind::Mercurial ? QStringLiteral("hg") : QStringLiteral("git");
}

} // namespace

RecentRepositoriesPanel::RecentRepositoriesPanel(QString currentRepositoryRoot, QWidget* parent) :
	QTreeWidget{ parent },
	_currentRoot{ std::move(currentRepositoryRoot) }
{
	setColumnCount(1);
	setHeaderHidden(true);
	header()->setSectionResizeMode(0, QHeaderView::Stretch); // the rows are laid out against the viewport's width
	setRootIsDecorated(true);
	setIndentation(14);
	setAllColumnsShowFocus(true);
	// A row is opened, never selected: what the keyboard is on still shows, but only while this has focus
	setSelectionMode(QAbstractItemView::NoSelection);
	setExpandsOnDoubleClick(false); // a double click opens the repository, whether or not the row also folds
	setContextMenuPolicy(Qt::CustomContextMenu);
	setItemDelegate(new RecentRepositoryDelegate{ this });

	connect(this, &QAbstractItemView::activated, this, [this](const QModelIndex& index) {
		openRepository(rootOf(itemFromIndex(index)), rootOf(itemFromIndex(index.parent())));
	});
	connect(this, &QWidget::customContextMenuRequested, this, &RecentRepositoriesPanel::showContextMenu);
	// Another window opening a repository writes the list this shows, and there is no other way to hear of it
	connect(&RecentRepositories::Notifier::instance(), &RecentRepositories::Notifier::changed,
		this, &RecentRepositoriesPanel::rebuild);

	rebuild();
}

void RecentRepositoriesPanel::resizeEvent(QResizeEvent* event)
{
	QTreeWidget::resizeEvent(event);
	if (event->size().width() != event->oldSize().width())
		scheduleDelayedItemsLayout(); // the cached row heights were computed against the old width
}

void RecentRepositoriesPanel::rebuild()
{
	QSet<QString> expandedRoots;
	for (int i = 0; i < topLevelItemCount(); ++i)
	{
		if (topLevelItem(i)->isExpanded())
			expandedRoots.insert(topLevelItem(i)->data(0, RootRole).toString());
	}

	clear();

	for (const RecentRepository& repository : RecentRepositories::list())
	{
		auto* item = new QTreeWidgetItem{ this };
		item->setText(0, QFileInfo{ repository.root }.fileName());
		item->setData(0, RootRole, repository.root);
		item->setData(0, PathRole, QDir::toNativeSeparators(repository.root));
		item->setData(0, BadgeRole, kindLabel(repository.kind));
		item->setData(0, CurrentRole, sameRepositoryPath(repository.root, _currentRoot));
		item->setToolTip(0, QDir::toNativeSeparators(repository.root));

		for (const RecentSubrepo& subrepo : repository.subrepos)
		{
			const QString root = repository.root + QLatin1Char('/') + subrepo.path;
			const QString name = subrepo.path.section(QLatin1Char('/'), -1);

			auto* subrepoItem = new QTreeWidgetItem{ item };
			subrepoItem->setText(0, name);
			subrepoItem->setData(0, RootRole, root);
			// The parent's path is right above, and a subrepo directly under it adds nothing by repeating
			// its own name; one further down is worth placing
			subrepoItem->setData(0, PathRole, name == subrepo.path ? QString{} : QDir::toNativeSeparators(subrepo.path));
			// The kind is the parent's unless it isn't, and that is the case worth pointing out
			subrepoItem->setData(0, BadgeRole, subrepo.kind == repository.kind ? QString{} : kindLabel(subrepo.kind));
			subrepoItem->setData(0, CurrentRole, sameRepositoryPath(root, _currentRoot));
			subrepoItem->setToolTip(0, QDir::toNativeSeparators(root));
		}

		item->setExpanded(expandedRoots.contains(repository.root));
	}
}

void RecentRepositoriesPanel::openRepository(const QString& root, const QString& parentRoot)
{
	if (root.isEmpty())
		return;

	if (parentRoot.isEmpty())
	{
		openRecentRepository(root, this); // only a listed repository can be dropped from the list
		return;
	}

	CommitWindow* window = openRepositoryWindowAt(root, this);
	if (!window)
		return;

	// The parent shows this subrepo's pointer as a row of its own, which a commit in here moves
	if (CommitWindow* parentWindow = repositoryWindow(parentRoot))
		connect(window, &CommitWindow::committed, parentWindow, &CommitWindow::refreshRepository, Qt::UniqueConnection);
}

void RecentRepositoriesPanel::showContextMenu(const QPoint& pos)
{
	const QTreeWidgetItem* item = itemAt(pos);
	if (!item)
		return;

	// The roots travel into the actions by value: opening one of these repositories rebuilds this tree
	// from the list it just wrote, and every item here is gone by the time the action returns
	const QString root = rootOf(item);
	const QString parentRoot = rootOf(item->parent());

	QMenu menu{ this };
	menu.addAction(tr("&Open"), this, [this, root, parentRoot] { openRepository(root, parentRoot); });
	if (parentRoot.isEmpty())
		menu.addAction(tr("&Remove from list"), this, [root] { RecentRepositories::forget(root); });
	menu.exec(viewport()->mapToGlobal(pos));
}

QString RecentRepositoriesPanel::rootOf(const QTreeWidgetItem* item)
{
	return item ? item->data(0, RootRole).toString() : QString{};
}
