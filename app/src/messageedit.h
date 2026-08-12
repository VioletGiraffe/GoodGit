#pragma once

#include <QPlainTextEdit>

class QCompleter;
class QStringListModel;

// Commit message editor: monospace, subject-length guide line at 50 columns (the width the left column
// of the window was sized for), and word completion over a pool the window supplies.
// Auto-popup while typing, Ctrl+Space forces the popup, Tab accepts - Enter always stays a newline.
class MessageEdit final : public QPlainTextEdit
{
public:
	explicit MessageEdit(QWidget* parent = nullptr);

	void setCompletionWords(QStringList words); // sorted internally

protected:
	void paintEvent(QPaintEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	[[nodiscard]] QString wordUnderCursor() const;
	void showCompletions(const QString& prefix);
	void acceptCurrentCompletion();
	void insertCompletion(const QString& completion);

	QCompleter* _completer = nullptr;
	QStringListModel* _completerModel = nullptr;
};
