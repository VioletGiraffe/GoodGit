#pragma once

#include "unifieddiff.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

#include <optional>

class QLabel;
class QStackedWidget;
class CFindBar;
class CLabelElided;
class CLightningFastViewerWidget;
class Repository;

// Read-only text in a window of its own: one file as of one commit, or a change's whole diff.
// A file is decoded where its bytes are text, and shown as a hex dump where they are not.
// Never deduplicated and never given a stored geometry: several are meant to sit side by side, so each opens
// cascaded from the window that spawned it.
class FileViewerWindow final : public QMainWindow
{
public:
	// `repo` is only used here: the window keeps no reference to it, and may outlive it
	FileViewerWindow(Repository& repo, const QString& sha, const QString& repoRelativePath, QWidget* parent);

	// A change's whole diff in a new window, scrolled to `currentPath`'s section where the set has one.
	// A message instead where there is no set or it is empty, or while `pending`: a set held then may be stale.
	static void showChangeSetDiff(const std::optional<ChangeSetDiff>& set, bool pending, const QString& currentPath,
		const QString& repositoryName, const QString& tag, QWidget* parent);

private:
	// Shows nothing yet: the caller fills it
	FileViewerWindow(const QString& title, const QString& headerText, const QString& tag, QWidget* parent);

	void showMessage(const QString& text);
	void showContent(const QByteArray& bytes);

private:
	CLabelElided* _pathLabel = nullptr; // kept for the font, which follows the settings
	QStackedWidget* _stack = nullptr;
	QLabel* _messageLabel = nullptr;
	CLightningFastViewerWidget* _viewer = nullptr;
	CFindBar* _findBar = nullptr;
};
