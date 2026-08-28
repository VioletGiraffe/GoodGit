#include "fileviewerwindow.h"
#include "historymodels.h"
#include "repository.h"
#include "settings.h"
#include "theme.h"

#include "settings/csettings.h"
#include "settingsui/csettingsdialog.h"
#include "string/stringutils.h"
#include "widgets/clabelelided.h"
#include "widgets/clightningfastviewer.h"

DISABLE_COMPILER_WARNINGS
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPoint>
#include <QSizePolicy>
#include <QStackedWidget>
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
	QMainWindow(parent, Qt::Window)
{
	setAttribute(Qt::WA_DeleteOnClose);

	const QString tag = shortSha(sha);
	setWindowTitle(repoRelativePath.section(QLatin1Char('/'), -1) + QStringLiteral(" @ ") + tag);

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
	_pathLabel->setText(repoRelativePath);
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
	setCentralWidget(central);

	const auto applyFontSettings = [this] {
		const QFont mono = monospaceFont();
		_pathLabel->setFont(mono);
		_viewer->setFont(mono);
		_viewer->setTabWidth(CSettings{}.value(Settings::DiffTabWidthKey, Settings::DiffTabWidthDefault).toInt());
	};
	applyFontSettings();
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, applyFontSettings);

	showMessage(tr("Loading..."));

	resize(DefaultWidth, DefaultHeight);
	if (const QWidget* anchor = parent ? parent->window() : nullptr)
	{
		const int offset = CascadeStep * (cascadePosition++ % CascadeLength + 1);
		move(anchor->pos() + QPoint{ offset, offset });
	}

	repo.fileAtRevision(sha, repoRelativePath, this, [this](std::expected<QByteArray, QString> content) {
		// The bytes are read whole before this runs: the cap bounds what is decoded and indexed, not what is
		// buffered. Probing the size first would not bound it either: the read applies checkout filters, so an
		// LFS pointer's stored size is not the size of what arrives.
		if (!content)
			showMessage(content.error());
		else if (content->size() > CSettings{}.value(Settings::MaxViewedFileBytesKey, Settings::MaxViewedFileBytesDefault).toLongLong())
			showMessage(tr("The file is too large to display (%1 MB).").arg(double(content->size()) / (1024 * 1024), 0, 'f', 1));
		else if (content->isEmpty())
			showMessage(tr("The file is empty."));
		else
			showContent(*content);
	});
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

	_stack->setCurrentWidget(_viewer);
}
