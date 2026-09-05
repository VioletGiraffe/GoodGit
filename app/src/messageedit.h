#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QPlainTextEdit>
RESTORE_COMPILER_WARNINGS

class QCompleter;
class QStringListModel;

// Commit message editor: monospace, a subject-length guide line, and word completion over a pool the window
// supplies. Auto-popup while typing, Ctrl+Space forces the popup, Tab accepts; Enter always stays a newline.
class MessageEdit final : public QPlainTextEdit
{
public:
	explicit MessageEdit(QWidget* parent = nullptr);

	// Rebuilds the pool: every changed path and its basename, plus identifier-shaped words from `diff`
	void setCompletionSources(const QStringList& changedPaths, QByteArray diff);

	// Wide enough for the subject guide column, which the base's font-independent placeholder hint ignores
	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	// The completion token before the cursor: path and identifier characters, not Qt's word boundaries
	[[nodiscard]] QTextCursor completionPrefixSelection() const;
	[[nodiscard]] QString completionPrefix() const;
	void putExactCaseMatchesFirst(const QString& prefix);
	void showCompletions(const QString& prefix);
	void acceptCurrentCompletion();
	void insertCompletion(const QString& completion);

	QCompleter* _completer = nullptr;
	QStringListModel* _completerModel = nullptr;
	// The pool, sorted case-insensitively. Each prefix reorders a copy into the model; this order stays pristine
	QStringList _completionWords;
	// Cached: the setting is a registry read, and paintEvent runs per keystroke and cursor blink
	int _guideColumn = 0;
};
