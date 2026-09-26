#pragma once

#include "unifieddiff.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringView>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <optional>

class QLabel;
class QPushButton;
class CLabelElided;
class DiffTextView;

// The unit format every header states a size in
[[nodiscard]] QString formattedFileSize(qint64 bytes);

// A header naming what is shown and where it is from, over a read-only monospace view. The pane reads
// nothing; the caller does, and caps what it reads.
class DiffPane final : public QWidget
{
	Q_OBJECT

public:
	// What the header states about the item shown. Every field may be empty.
	struct ItemInfo
	{
		QString path;
		// The short label at the right of the header: which revisions the text is of, or how the two sides compare
		QString tag;
		QString size; // formatted for display; empty where the item has no size
	};

	explicit DiffPane(QWidget* parent = nullptr);

	// Parsed both processed and raw, for the header's toggle between the two
	void showDiff(const ItemInfo& item, QStringView diff);
	// One file's section of a change set, parsed as showDiff() does, the processed rendering with the set's moves.
	// A message instead where the section is over `maxBytes`, as a query's answer would be, or holds no content
	// change - headers alone - which `noContentText` describes.
	void showSection(const ItemInfo& item, const ChangeSetDiff& set, int file, qint64 maxBytes, const QString& noContentText);
	// A file's own contents, for a file no backend is asked to diff: numbered, undecorated
	void showFileText(const ItemInfo& item, const QString& text);
	// Prose rather than file content: a placeholder, a failure, a commit message. Neither numbered nor decorated.
	void showMessage(const ItemInfo& item, const QString& text);

	// Restates the header over the content already shown, for a field that arrives after it
	void setHeader(const ItemInfo& item);

	// A line of the diff shown, as ForeignEnd::diffLine names one in the file it points into
	void scrollDiffLineToTop(int diffLine);
	// The view's line at its top, for scrollLineToTop(); empty while a message is shown.
	// A diff's line is one of the rendering shown: the toggle maps the top line across itself.
	[[nodiscard]] std::optional<int> topLine() const;
	// Does nothing for a line past the end
	void scrollLineToTop(int line);

signals:
	// A click on the mark of a block moved to or from another file: the window owning the file list shows that file
	void foreignEndActivated(const ForeignEnd& end);

private:
	[[nodiscard]] QWidget* buildHunkNavigator();
	// Follows the view's scrolling as well as its content: the hunk named is the one at the top of it
	void updateHunkNavigator();

	void showRenderings(const ItemInfo& item, ParsedDiff processed, ParsedDiff raw);
	void clearRenderings(); // before anything but a diff is shown
	// Keeps the line at the top of the view at the top, as the other rendering shows it
	void setRawDiffShown(bool raw);

private:
	struct Renderings
	{
		ParsedDiff processed;
		ParsedDiff raw;
	};
	std::optional<Renderings> _diff; // of the diff shown, empty for the other content kinds

	QPushButton* _rawDiffButton = nullptr; // checked: the raw rendering is shown. Disabled without a diff.
	CLabelElided* _pathLabel = nullptr;
	// Doubles as the header's spacer, so it must stay visible even with no size to show
	CLabelElided* _sizeLabel = nullptr;
	QLabel* _tagLabel = nullptr;
	QWidget* _hunkNavigator = nullptr; // hidden whole where the content holds no hunk
	QLabel* _hunkLabel = nullptr;
	QPushButton* _previousHunkButton = nullptr;
	QPushButton* _nextHunkButton = nullptr;
	DiffTextView* _view = nullptr;
};
