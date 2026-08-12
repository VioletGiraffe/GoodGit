#pragma once

#include <QPlainTextEdit>

// Commit message editor: monospace, with a subject-length guide line at 50 columns
// (the width the left column of the window was sized for).
class MessageEdit final : public QPlainTextEdit
{
public:
	explicit MessageEdit(QWidget* parent = nullptr);

protected:
	void paintEvent(QPaintEvent* event) override;
};
