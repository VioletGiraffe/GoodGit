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
#include <QStyledItemDelegate>

#include <algorithm>

namespace {

enum ItemRole
{
	RootRole = Qt::UserRole, // absolute for subrepo rows too
	PathRole,     // the line under the name, in native separators; empty where the name says it all
	BadgeRole,    // the kind, where worth showing; empty otherwise
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

// Splits `path` across at most two lines at a component boundary, eliding the second in the middle if
// needed. A path that fits stays on one line, one with nothing to break on is elided as is.
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

// Two-line rows, a subrepo icon and a kind badge: none of which item roles can express
class RecentRepositoryDelegate final : public QStyledItemDelegate
{
public:
	explicit RecentRepositoryDelegate(QTreeWidget* view) : QStyledItemDelegate{ view }, _view{ view } {}

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);
		opt.text.clear(); // the base paints only the row separator; every glyph is painted here

		// The view is NoSelection, so State_Selected never arrives
		// The mouse-over is drawn for visual feedback, current item rect is for keyboard navigation
		const Theme& theme = activeTheme();
		if (opt.state.testFlag(QStyle::State_MouseOver) || opt.state.testFlag(QStyle::State_HasFocus))
			painter->fillRect(option.rect, theme.palette.selectionBg);
		else if (index.data(CurrentRole).toBool())
			painter->fillRect(option.rect, theme.palette.surfaceAlt);
		// After the fill, so the stylesheet's separator survives it
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
			// Centred on the name's line, the two fonts being different sizes
			const QRect badgeRect{ nameRight - badgeSize.width(), y + (nameMetrics.height() - badgeSize.height()) / 2,
				badgeSize.width(), badgeSize.height() };

			painter->save();
			painter->setRenderHint(QPainter::Antialiasing);
			painter->setPen(theme.palette.border);
			painter->setBrush(Qt::NoBrush); // the row background's brush is still set
			painter->setFont(badgeFont);
			// Half a pixel in, so the one-pixel pen lands on a pixel instead of across two
			painter->drawRoundedRect(QRectF{ badgeRect }.adjusted(0.5, 0.5, -0.5, -0.5),
				theme.metrics.controlRadius, theme.metrics.controlRadius);
			painter->setPen(theme.palette.accentText);
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
	// From the viewport rather than the item's rect, which is not the row's width while a size hint is being
	// asked for. Painting uses the same width, so both agree.
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
	// A row is opened, never selected; the keyboard focus still shows while the panel has focus
	setSelectionMode(QAbstractItemView::NoSelection);
	setExpandsOnDoubleClick(false); // a double click opens the repository
	setContextMenuPolicy(Qt::CustomContextMenu);
	setItemDelegate(new RecentRepositoryDelegate{ this });

	connect(this, &QAbstractItemView::activated, this, [this](const QModelIndex& index) {
		openRepository(rootOf(itemFromIndex(index)), rootOf(itemFromIndex(index.parent())));
	});
	connect(this, &QWidget::customContextMenuRequested, this, &RecentRepositoriesPanel::showContextMenu);
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
	rememberExpansion(); // clear() is about to destroy the rows holding it
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

		for (const Subrepo& subrepo : repository.subrepos)
		{
			const QString root = repository.root + QLatin1Char('/') + subrepo.path;
			const QString name = subrepo.path.section(QLatin1Char('/'), -1);

			auto* subrepoItem = new QTreeWidgetItem{ item };
			subrepoItem->setText(0, name);
			subrepoItem->setData(0, RootRole, root);
			// Only shown for a subrepo deeper than the parent's root directory
			subrepoItem->setData(0, PathRole, name == subrepo.path ? QString{} : QDir::toNativeSeparators(subrepo.path));
			// Only shown where the kind differs from the parent's
			subrepoItem->setData(0, BadgeRole, subrepo.kind == repository.kind ? QString{} : kindLabel(subrepo.kind));
			subrepoItem->setData(0, CurrentRole, sameRepositoryPath(root, _currentRoot));
			subrepoItem->setToolTip(0, QDir::toNativeSeparators(root));
		}
	}

	applyFilter(); // also sets each row's expansion
}

void RecentRepositoriesPanel::setFilter(const QString& text)
{
	QString filter = text.trimmed();
	filter.replace(QLatin1Char('\\'), QLatin1Char('/'));
	if (filter == _filter)
		return;

	rememberExpansion(); // applyFilter() is about to expand whatever the filter matches
	_filter = filter;
	applyFilter();
}

// Goes through _expandedRoots rather than the row: a rebuild while the menu is open destroys the row.
// applyFilter() then re-applies the state to whatever rows exist.
void RecentRepositoriesPanel::setRepositoryExpanded(const QString& root, bool expanded)
{
	if (expanded)
		_expandedRoots.insert(root);
	else
		_expandedRoots.remove(root);

	applyFilter();
}

// Records only while unfiltered: what a filter expanded is not the user's choice
void RecentRepositoriesPanel::rememberExpansion()
{
	if (!_filter.isEmpty())
		return;

	_expandedRoots.clear();
	for (int i = 0; i < topLevelItemCount(); ++i)
	{
		if (topLevelItem(i)->isExpanded())
			_expandedRoots.insert(rootOf(topLevelItem(i)));
	}
}

void RecentRepositoriesPanel::applyFilter()
{
	for (int i = 0; i < topLevelItemCount(); ++i)
	{
		QTreeWidgetItem* item = topLevelItem(i);
		// A repository that matches brings its subrepos with it: they are part of what was matched
		const bool repositoryMatches = matchesFilter(item);

		int shownSubrepos = 0;
		for (int child = 0; child < item->childCount(); ++child)
		{
			const bool show = repositoryMatches || matchesFilter(item->child(child));
			item->child(child)->setHidden(!show);
			shownSubrepos += show ? 1 : 0;
		}

		item->setHidden(!repositoryMatches && shownSubrepos == 0);
		// A match inside a repository has to be unfolded to be seen; one on the repository itself does not
		const bool matchIsInside = !repositoryMatches && shownSubrepos > 0;
		item->setExpanded(matchIsInside || _expandedRoots.contains(rootOf(item)));
	}
}

bool RecentRepositoriesPanel::matchesFilter(const QTreeWidgetItem* item) const
{
	return _filter.isEmpty() || rootOf(item).contains(_filter, Qt::CaseInsensitive);
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

	// A commit in the subrepo moves the pointer row in the parent
	if (CommitWindow* parentWindow = repositoryWindow(parentRoot))
		connect(window, &CommitWindow::committed, parentWindow, &CommitWindow::refreshRepository, Qt::UniqueConnection);
}

void RecentRepositoriesPanel::showContextMenu(const QPoint& pos)
{
	const QTreeWidgetItem* item = itemAt(pos);
	if (!item)
		return;

	// By value: opening a repository rebuilds this tree, and the items are gone by the time the action returns
	const QString root = rootOf(item);
	const QString parentRoot = rootOf(item->parent());

	QMenu menu{ this };
	menu.addAction(tr("&Open"), this, [this, root, parentRoot] { openRepository(root, parentRoot); });
	if (parentRoot.isEmpty()) // a subrepo has no rows under it, and only its parent is listed
	{
		if (item->childCount() > 0)
		{
			const bool expanded = item->isExpanded();
			menu.addAction(expanded ? tr("&Hide subrepos") : tr("&Show subrepos"), this,
				[this, root, expanded] { setRepositoryExpanded(root, !expanded); });
		}
		menu.addAction(tr("&Remove from list"), this, [root] { RecentRepositories::forget(root); });
	}
	menu.exec(viewport()->mapToGlobal(pos));
}

QString RecentRepositoriesPanel::rootOf(const QTreeWidgetItem* item)
{
	return item ? item->data(0, RootRole).toString() : QString{};
}
