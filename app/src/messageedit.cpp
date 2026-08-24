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
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

constexpr int ColumnsPastGuide = 4; // the guide marks a limit to overrun, not the edge of the editor

int subjectGuideColumn()
{
	return CSettings{}.value(Settings::SubjectGuideColumnKey, Settings::SubjectGuideColumnDefault).toInt();
}

}

MessageEdit::MessageEdit(QWidget* parent) :
	QPlainTextEdit(parent)
{
	setFont(monospaceFont());
	// update() for a guide-column change under an unchanged font
	connect(&CSettingsNotifier::instance(), &CSettingsNotifier::settingsChanged, this, [this] { setFont(monospaceFont()); viewport()->update(); });
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
	const qreal textWidth = fontMetrics().horizontalAdvance(QLatin1Char('x')) * (subjectGuideColumn() + ColumnsPastGuide)
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
		showCompletions(wordUnderCursor()); // manual trigger: no minimum prefix length
		return;
	}

	QPlainTextEdit::keyPressEvent(event);

	if (event->text().isEmpty() && !_completer->popup()->isVisible())
		return; // pure navigation

	// With auto-popup off, a popup summoned by Ctrl+Space still follows further typing
	const QString prefix = wordUnderCursor();
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

QString MessageEdit::wordUnderCursor() const
{
	QTextCursor cursor = textCursor();
	cursor.select(QTextCursor::WordUnderCursor);
	return cursor.selectedText();
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
	QTextCursor cursor = textCursor();
	cursor.select(QTextCursor::WordUnderCursor);
	cursor.insertText(completion);
	setTextCursor(cursor);
}

void MessageEdit::paintEvent(QPaintEvent* event)
{
	QPlainTextEdit::paintEvent(event);

	const qreal x = contentOffset().x() + document()->documentMargin()
		+ fontMetrics().horizontalAdvance(QLatin1Char('x')) * subjectGuideColumn();
	if (x >= viewport()->width())
		return;

	QPainter painter{ viewport() };
	QColor color = palette().color(QPalette::PlaceholderText);
	color.setAlpha(90);
	painter.setPen(color);
	painter.drawLine(QPointF{ x, 0.0 }, QPointF{ x, qreal(viewport()->height()) });
}
