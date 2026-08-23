#pragma once

#include <QString>
#include <QWidget>

class QLabel;
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

private:
	CLabelElided* _pathLabel = nullptr;
	QLabel* _tagLabel = nullptr;
	DiffTextView* _view = nullptr;
};
