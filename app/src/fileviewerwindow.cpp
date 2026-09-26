#include "fileviewerwindow.h"
#include "historymodels.h"
#include "repository.h"
#include "settings.h"
#include "theme.h"

#include "appdialogs/csettingsnotifier.h"
#include "string/stringutils.h"
#include "widgets/cfindbar.h"
#include "widgets/clabelelided.h"
#include "widgets/clightningfastviewer.h"

DISABLE_COMPILER_WARNINGS
#include <QDeadlineTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPoint>
#include <QRegularExpression>
#include <QSettings>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTextDocument>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

namespace {

constexpr int DefaultWidth = 900;
constexpr int DefaultHeight = 700;
constexpr int CascadeStep = 28;  // about a title bar, so the window underneath stays identifiable
constexpr int CascadeLength = 8; // windows before the offset starts over, which keeps the last one on screen

int cascadePosition = 0;

} // namespace

FileViewerWindow::FileViewerWindow(Repository& repo, const QString& sha, const QString& repoRelativePath, QWidget* parent) :
	FileViewerWindow(repoRelativePath.section(QLatin1Char('/'), -1) + QStringLiteral(" @ ") + shortSha(sha), repoRelativePath, shortSha(sha), parent)
{
	showMessage(tr("Loading..."));

	// The limit travels into the read: probing the size first would not bound it, the read applying checkout
	// filters, so an LFS pointer's stored size is not the size of what arrives.
	const qint64 maxBytes = QSettings{}.value(Settings::MaxViewedFileBytesKey, Settings::MaxViewedFileBytesDefault).toLongLong();
	repo.fileAtRevision(sha, repoRelativePath, maxBytes, this, [this](std::expected<QByteArray, QString> content) {
		if (!content)
			showMessage(content.error()); // an oversize file fails here, never held whole
		else if (content->isEmpty())
			showMessage(tr("The file is empty."));
		else
			showContent(*content);
	});
}

void FileViewerWindow::showChangeSetDiff(const std::optional<ChangeSetDiff>& set, bool pending, const QString& currentPath,
	const QString& repositoryName, const QString& tag, QWidget* parent)
{
	auto* window = new FileViewerWindow(tr("Diff - %1 @ %2").arg(repositoryName, tag), repositoryName, tag, parent);
	if (pending || !set || set->text().isEmpty())
	{
		window->showMessage(pending ? tr("The diff is still loading.") : set ? tr("The diff is empty.") : tr("The whole diff is not available."));
		window->show();
		return;
	}

	window->_viewer->setText(set->text());
	window->_stack->setCurrentWidget(window->_viewer);
	// Shown before scrolling: the viewer indexes its lines on its first resize
	window->show();

	const std::optional<int> file = set->fileIndex(currentPath);
	if (!file)
		return;

	// The section's "diff --git" line, matched whole: a later file's content may hold the same text
	const QStringView section = set->fileDiff(*file);
	const QRegularExpression headerLine{ QStringLiteral("^") + QRegularExpression::escape(section.left(section.indexOf(QLatin1Char('\n'))))
		+ QStringLiteral("$"), QRegularExpression::MultilineOption };
	// Searched backward from the end: the viewer scrolls a match above the view to its top row
	window->_viewer->moveToEnd();
	window->_viewer->find(headerLine, QTextDocument::FindBackward | QTextDocument::FindCaseSensitively);
}

FileViewerWindow::FileViewerWindow(const QString& title, const QString& headerText, const QString& tag, QWidget* parent) :
	QMainWindow(parent, Qt::Window)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(title);

	auto* central = new QWidget;
	auto* layout = new QVBoxLayout(central);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto* header = new QFrame;
	header->setObjectName(QStringLiteral("diffHeader"));
	auto* headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(8, 6, 8, 6);
	_pathLabel = new CLabelElided;
	// Eliding does not shrink a QLabel's minimum width, which would otherwise become the window's
	_pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	_pathLabel->setText(headerText);
	auto* tagLabel = new QLabel{ tag };
	tagLabel->setObjectName(QStringLiteral("diffTagLabel"));
	headerLayout->addWidget(_pathLabel, 1);
	headerLayout->addWidget(tagLabel);
	layout->addWidget(header);

	_messageLabel = new QLabel;
	_messageLabel->setAlignment(Qt::AlignCenter);
	_messageLabel->setWordWrap(true);
	_messageLabel->setMargin(16);
	_viewer = new CLightningFastViewerWidget;

	_stack = new QStackedWidget;
	_stack->addWidget(_messageLabel);
	_stack->addWidget(_viewer);
	layout->addWidget(_stack, 1);
	const auto findWithWrapAround = [viewer = _viewer](const auto& pattern, QTextDocument::FindFlags flags) {
		return viewer->find(pattern, flags, /*wrapAround=*/true);
	};
	const auto countMatches = [viewer = _viewer](const auto& pattern, QTextDocument::FindFlags flags, QDeadlineTimer deadline, bool highlight) {
		viewer->setCountedMatchesHighlighted(highlight);
		return viewer->countMatches(pattern, flags, deadline);
	};
	_findBar = new CFindBar{
		CFindBar::HostFunctions{ .findText = findWithWrapAround, .findRegex = findWithWrapAround, .countText = countMatches, .countRegex = countMatches,
			.clearHighlights = [viewer = _viewer] { viewer->setCountedMatchesHighlighted(false); } },
		CFindBar::Keys{ .find = QKeySequence::Find, .findNext = QKeySequence::FindNext, .findPrevious = QKeySequence::FindPrevious },
		QString::fromLatin1(Settings::FileViewerWindowFindGroupKey) };
	_findBar->setObjectName(QStringLiteral("findBar"));
	layout->addWidget(_findBar);
	addActions(_findBar->findActions());
	setCentralWidget(central);

	const auto applyFontSettings = [this] {
		const QFont mono = monospaceFont();
		_pathLabel->setFont(mono);
		_viewer->setFont(mono);
		_viewer->setTabWidth(QSettings{}.value(Settings::DiffTabWidthKey, Settings::DiffTabWidthDefault).toInt());
	};
	applyFontSettings();
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, applyFontSettings);

	resize(DefaultWidth, DefaultHeight);
	if (const QWidget* anchor = parent ? parent->window() : nullptr)
	{
		const int offset = CascadeStep * (cascadePosition++ % CascadeLength + 1);
		move(anchor->pos() + QPoint{ offset, offset });
	}
}

void FileViewerWindow::showMessage(const QString& text)
{
	_messageLabel->setText(text);
	_stack->setCurrentWidget(_messageLabel);
}

void FileViewerWindow::showContent(const QByteArray& bytes)
{
	if (const std::optional<QString> text = decodedAsText(bytes))
		_viewer->setText(*text);
	else
		_viewer->setData(bytes);
	_findBar->clearStatus();

	_stack->setCurrentWidget(_viewer);
}
