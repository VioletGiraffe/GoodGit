#pragma once

#include <QPlainTextEdit>

// Commit message editor: monospace, with a subject-length guide line at 50 columns
// (the column width the guide dictated the window layout for - plan.md §7).
class MessageEdit final : public QPlainTextEdit
{
public:
	explicit MessageEdit(QWidget* parent = nullptr);

protected:
	void paintEvent(QPaintEvent* event) override;
};
