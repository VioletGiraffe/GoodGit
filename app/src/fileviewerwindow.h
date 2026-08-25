#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

class QLabel;
class QStackedWidget;
class CLabelElided;
class CLightningFastViewerWidget;
class Repository;

// One file's content as of one commit, read-only: decoded where the bytes are text, a hex dump where they are not.
// Never deduplicated and never given a stored geometry: several are meant to sit side by side, so each opens
// cascaded from the window that spawned it.
class FileViewerWindow final : public QMainWindow
{
public:
	// `repo` is only used here: the window keeps no reference to it, and may outlive it
	FileViewerWindow(Repository& repo, const QString& sha, const QString& repoRelativePath, QWidget* parent);

private:
	void showMessage(const QString& text);
	void showContent(const QByteArray& bytes);

private:
	CLabelElided* _pathLabel = nullptr; // kept for the font, which follows the settings
	QStackedWidget* _stack = nullptr;
	QLabel* _messageLabel = nullptr;
	CLightningFastViewerWidget* _viewer = nullptr;
};
