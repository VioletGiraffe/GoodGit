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
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
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

bool looksLikeHexSha(const QString& token)
{
	if (token.length() < 7)
		return false;
	for (const QChar c : token)
	{
		if (!c.isDigit() && !(c >= QLatin1Char('a') && c <= QLatin1Char('f')) && !(c >= QLatin1Char('A') && c <= QLatin1Char('F')))
			return false;
	}
	return true;
}

// Letters, not characters: "x1234567" and "a_2_bc" are not words.
// Four: one more than the default minimum prefix the auto-popup waits for.
bool hasAtLeastFourLetters(const QString& token)
{
	int letters = 0;
	for (const QChar c : token)
	{
		if (c.isLetter() && ++letters == 4)
			return true;
	}
	return false;
}

// Every changed path and its basename, plus identifier-shaped words from the diff.
// Context lines count as much as changed ones: the backends ask for the enclosing function, whose names
// describe the change.
// Fewer than 4 letters and a small stoplist weed out prose function words.
// Hex-sha-shaped tokens are dropped: submodule pointer diffs would pollute the pool.
QStringList completionWordsFor(const QStringList& changedPaths, QByteArray diff)
{
	constexpr qsizetype MaxDiffBytesForWords = 8 * 1024 * 1024;
	constexpr qsizetype MaxWords = 20000;
	static const QSet<QString> stopWords = {
		QStringLiteral("with"), QStringLiteral("from"), QStringLiteral("this"), QStringLiteral("that"),
		QStringLiteral("when"), QStringLiteral("then"), QStringLiteral("than"), QStringLiteral("they"),
		QStringLiteral("them"), QStringLiteral("their"), QStringLiteral("there"), QStringLiteral("these"),
		QStringLiteral("those"), QStringLiteral("have"), QStringLiteral("been"), QStringLiteral("being"),
		QStringLiteral("will"), QStringLiteral("would"), QStringLiteral("should"), QStringLiteral("could"),
		QStringLiteral("into"), QStringLiteral("onto"), QStringLiteral("only"), QStringLiteral("over"),
		QStringLiteral("also"), QStringLiteral("each"), QStringLiteral("some"), QStringLiteral("such"),
		QStringLiteral("must"), QStringLiteral("does"), QStringLiteral("done"), QStringLiteral("upon"),
		QStringLiteral("very"), QStringLiteral("more"), QStringLiteral("most"), QStringLiteral("here"),
		QStringLiteral("where"), QStringLiteral("which"), QStringLiteral("while"), QStringLiteral("after"),
		QStringLiteral("before"), QStringLiteral("about"), QStringLiteral("because"),
	};
	static const QRegularExpression wordRe(QStringLiteral("[A-Za-z_][A-Za-z0-9_]{3,}"));

	QSet<QString> words;
	for (const QString& path : changedPaths)
	{
		words.insert(path);
		words.insert(path.mid(path.lastIndexOf(QLatin1Char('/')) + 1));
	}

	diff.truncate(MaxDiffBytesForWords);
	// Wontfix: split copies the capped diff into lines on every refresh; a skip-when-unchanged check is not worth the complication
	for (const QByteArray& rawLine : diff.split('\n'))
	{
		if (words.size() >= MaxWords)
			break;
		// A hunk header's tail is the enclosing function's name; the numbers before it cannot start a word.
		// Every --git metadata line starts with a letter, so the prefix test alone keeps them out.
		if (rawLine.size() < 2 || (rawLine[0] != ' ' && rawLine[0] != '+' && rawLine[0] != '-' && rawLine[0] != '@')
			|| rawLine.startsWith("+++") || rawLine.startsWith("---"))
			continue;

		const QString line = QString::fromUtf8(rawLine);
		for (auto it = wordRe.globalMatch(line); it.hasNext(); )
		{
			const QString token = it.next().captured();
			if (hasAtLeastFourLetters(token) && !stopWords.contains(token.toLower()) && !looksLikeHexSha(token))
				words.insert(token);
		}
	}
	return QStringList{ words.begin(), words.end() };
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
	// Unsorted: putExactCaseMatchesFirst reorders the model per prefix, so no sort order holds across prefixes
	_completer->setModelSorting(QCompleter::UnsortedModel);
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

void MessageEdit::setCompletionSources(const QStringList& changedPaths, QByteArray diff)
{
	QStringList words = completionWordsFor(changedPaths, std::move(diff));
	// Sorted once, so that each block putExactCaseMatchesFirst produces is alphabetical
	std::sort(words.begin(), words.end(),
		[](const QString& l, const QString& r) { return l.compare(r, Qt::CaseInsensitive) < 0; });
	_completionWords = std::move(words);
	_completerModel->setStringList(_completionWords);

	// A refresh can replace the pool while the popup is up, and QCompleter only hides it from complete()
	_completer->popup()->hide();
}

// QCompleter offers matches in model order, so the ranking is the model's to impose: the words whose case
// the prefix already matches lead, the ones matching it only case-insensitively follow.
// Always partitions the pristine order: re-partitioning what a previous prefix left would interleave the
// two blocks that one made.
void MessageEdit::putExactCaseMatchesFirst(const QString& prefix)
{
	QStringList ordered = _completionWords;
	std::stable_partition(ordered.begin(), ordered.end(),
		[&prefix](const QString& word) { return word.startsWith(prefix, Qt::CaseSensitive); });
	_completerModel->setStringList(ordered);
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
	putExactCaseMatchesFirst(prefix);
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
