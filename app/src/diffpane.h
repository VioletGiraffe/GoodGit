#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

class QLabel;
class QPushButton;
class CLabelElided;
class DiffTextView;

// A header naming what is shown and where it is from, over a read-only monospace view. The pane neither
// reads nor caps the text; the caller does both.
class DiffPane final : public QWidget
{
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

	void showDiff(const ItemInfo& item, const QString& text);
	// A file's own contents, for a file no backend is asked to diff: numbered, undecorated
	void showFileText(const ItemInfo& item, const QString& text);
	// Prose rather than file content: a placeholder, a failure, a commit message. Neither numbered nor decorated.
	void showMessage(const ItemInfo& item, const QString& text);

private:
	void setHeader(const ItemInfo& item);
	[[nodiscard]] QWidget* buildHunkNavigator();
	// Follows the view's scrolling as well as its content: the hunk named is the one at the top of it
	void updateHunkNavigator();

private:
	CLabelElided* _pathLabel = nullptr; // shows the size too, appended to the path
	QLabel* _tagLabel = nullptr;
	QWidget* _hunkNavigator = nullptr; // hidden whole where the content holds no hunk
	QLabel* _hunkLabel = nullptr;
	QPushButton* _previousHunkButton = nullptr;
	QPushButton* _nextHunkButton = nullptr;
	DiffTextView* _view = nullptr;
};
