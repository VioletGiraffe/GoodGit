#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class CLabelElided;
class DiffTextView;

// A header naming what is shown and where it is from, over a read-only monospace view. The pane neither
// reads nor caps the text; the caller does both.
class DiffPane final : public QWidget
{
public:
	explicit DiffPane(QWidget* parent = nullptr);

	// `tag` is the short label at the right of the header: which revisions the text is of, or how the two
	// sides compare. Either label may be empty.
	void showDiff(const QString& pathLabel, const QString& tag, const QString& text);
	// A file's own contents, for a file no backend is asked to diff: numbered, undecorated
	void showFileText(const QString& pathLabel, const QString& tag, const QString& text);
	// Prose rather than file content: a placeholder, a failure, a commit message. Neither numbered nor decorated.
	void showMessage(const QString& pathLabel, const QString& tag, const QString& text);

private:
	void setHeader(const QString& pathLabel, const QString& tag);
	[[nodiscard]] QWidget* buildHunkNavigator();
	// Follows the view's scrolling as well as its content: the hunk named is the one at the top of it
	void updateHunkNavigator();

private:
	CLabelElided* _pathLabel = nullptr;
	QLabel* _tagLabel = nullptr;
	QWidget* _hunkNavigator = nullptr; // hidden whole where the content holds no hunk
	QLabel* _hunkLabel = nullptr;
	QPushButton* _previousHunkButton = nullptr;
	QPushButton* _nextHunkButton = nullptr;
	DiffTextView* _view = nullptr;
};
