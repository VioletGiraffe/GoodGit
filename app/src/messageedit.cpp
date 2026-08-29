#include "messageedit.h"
#include "settings.h"
#include "theme.h"

#include "settings/csettings.h"
#include "settingsui/csettingsdialog.h"

DISABLE_COMPILER_WARNINGS
#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

constexpr int ColumnsPastGuide = 4; // the guide marks a limit to overrun, not the edge of the editor

int subjectGuideColumn()
{
	return CSettings{}.value(Settings::SubjectGuideColumnKey, Settings::SubjectGuideColumnDefault).toInt();
}

// Not Qt's word boundaries: those break on '-' and '/', and a repo-relative path contains both
bool isCompletionTokenChar(QChar c)
{
	return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char('.')
		|| c == QLatin1Char('/');
}

}

MessageEdit::MessageEdit(QWidget* parent) :
	QPlainTextEdit(parent),
	_guideColumn{ subjectGuideColumn() }
{
	setFont(monospaceFont());
	// update() for a guide-column change under an unchanged font
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [this] {
		_guideColumn = subjectGuideColumn();
		setFont(monospaceFont());
		viewport()->update();
	});
	setTabChangesFocus(true);

	_completerModel = new QStringListModel(this);
	_completer = new QCompleter(_completerModel, this);
	_completer->setWidget(this);
	_completer->setCompletionMode(QCompleter::PopupCompletion);
	_completer->setCaseSensitivity(Qt::CaseInsensitive);
	_completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
	connect(_completer, qOverload<const QString&>(&QCompleter::activated), this, &MessageEdit::insertCompletion);

	// Installed after QCompleter's own popup filter, so ours runs first: Tab accepts, Enter stays a newline
	_completer->popup()->installEventFilter(this);
}

QSize MessageEdit::sizeHint() const
{
	const qreal textWidth = fontMetrics().horizontalAdvance(QLatin1Char('x')) * (_guideColumn + ColumnsPastGuide)
		+ 2 * document()->documentMargin();
	// The scrollbar's width is counted in: the box has a fixed height a message routinely outgrows
	return { qRound(textWidth) + 2 * frameWidth() + verticalScrollBar()->sizeHint().width(), QPlainTextEdit::sizeHint().height() };
}

void MessageEdit::setCompletionWords(QStringList words)
{
	// The completer is told the model is pre-sorted, so it can binary-search prefixes
	std::sort(words.begin(), words.end(),
		[](const QString& l, const QString& r) { return l.compare(r, Qt::CaseInsensitive) < 0; });
	_completerModel->setStringList(words);

	// A refresh can replace the pool while the popup is up, and QCompleter only hides it from complete()
	_completer->popup()->hide();
}

void MessageEdit::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Space && event->modifiers().testFlag(Qt::ControlModifier))
	{
		showCompletions(completionPrefix()); // manual trigger: no minimum prefix length
		return;
	}

	QPlainTextEdit::keyPressEvent(event);

	if (event->text().isEmpty() && !_completer->popup()->isVisible())
		return; // pure navigation

	// With auto-popup off, a popup summoned by Ctrl+Space still follows further typing
	const QString prefix = completionPrefix();
	const CSettings settings;
	const bool follow = settings.value(Settings::CompletionAutoPopupKey, Settings::CompletionAutoPopupDefault).toBool()
		? prefix.length() >= settings.value(Settings::CompletionMinPrefixLengthKey, Settings::CompletionMinPrefixLengthDefault).toInt()
		: _completer->popup()->isVisible() && !prefix.isEmpty();
	if (follow)
		showCompletions(prefix);
	else
		_completer->popup()->hide();
}

bool MessageEdit::eventFilter(QObject* watched, QEvent* event)
{
	// QAbstractScrollArea filters its viewport's events through this override during construction, before
	// the completer exists
	if (_completer && watched == _completer->popup() && event->type() == QEvent::KeyPress)
	{
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		switch (keyEvent->key())
		{
		case Qt::Key_Tab:
			acceptCurrentCompletion();
			return true;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			_completer->popup()->hide();
			// The Ctrl+Enter commit shortcuts live on the window and never reach a grabbing popup: swallow the
			// press so it does not become a newline; the next one lands on the shortcut
			if (keyEvent->modifiers().testFlag(Qt::ControlModifier))
				return true;
			keyPressEvent(keyEvent); // Enter means a newline, popup or not
			return true;
		default:
			break;
		}
	}
	return QPlainTextEdit::eventFilter(watched, event);
}

// Ends at the cursor: text to its right is not typed yet
QTextCursor MessageEdit::completionPrefixSelection() const
{
	QTextCursor cursor = textCursor();
	const QString text = cursor.block().text();
	const int blockPosition = cursor.block().position();
	const int end = cursor.positionInBlock();

	int start = end;
	while (start > 0 && isCompletionTokenChar(text.at(start - 1)))
		--start;

	cursor.setPosition(blockPosition + start);
	cursor.setPosition(blockPosition + end, QTextCursor::KeepAnchor);
	return cursor;
}

QString MessageEdit::completionPrefix() const
{
	return completionPrefixSelection().selectedText();
}

void MessageEdit::showCompletions(const QString& prefix)
{
	_completer->setCompletionPrefix(prefix); // even if unchanged: the pool may have changed

	const bool nothingToOffer = _completer->completionCount() == 0
		|| (_completer->completionCount() == 1 && _completer->currentCompletion() == prefix);
	if (nothingToOffer)
	{
		_completer->popup()->hide();
		return;
	}

	QRect rect = cursorRect();
	rect.setWidth(_completer->popup()->sizeHintForColumn(0) + _completer->popup()->verticalScrollBar()->sizeHint().width());
	_completer->complete(rect);
	_completer->popup()->setCurrentIndex(_completer->completionModel()->index(0, 0));
}

void MessageEdit::acceptCurrentCompletion()
{
	const QModelIndex current = _completer->popup()->currentIndex();
	insertCompletion(current.isValid() ? current.data().toString() : _completer->currentCompletion());
	_completer->popup()->hide();
}

void MessageEdit::insertCompletion(const QString& completion)
{
	QTextCursor cursor = completionPrefixSelection();
	cursor.insertText(completion);
	setTextCursor(cursor);
}

void MessageEdit::paintEvent(QPaintEvent* event)
{
	QPlainTextEdit::paintEvent(event);

	const qreal x = contentOffset().x() + document()->documentMargin()
		+ fontMetrics().horizontalAdvance(QLatin1Char('x')) * _guideColumn;
	if (x >= viewport()->width())
		return;

	QPainter painter{ viewport() };
	QColor color = palette().color(QPalette::PlaceholderText);
	color.setAlpha(90);
	painter.setPen(color);
	painter.drawLine(QPointF{ x, 0.0 }, QPointF{ x, qreal(viewport()->height()) });
}
