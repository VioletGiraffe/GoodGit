#include "hgprocess.h"
#include "hgcommandserver.h"
#include "settings.h"

#include "settings/csettings.h"

namespace {

Vcs::Tool hgTool()
{
	auto environment = QProcessEnvironment::systemEnvironment();
	// No localisation, user aliases or defaults rewriting the command
	environment.insert(QStringLiteral("HGPLAIN"), QStringLiteral("1"));
	// hg writes and reads the local 8-bit encoding, unlike git; every decode of its output goes through
	// TextEncoding::Local, and every byte handed to it through Hg::localBytes
	return { Hg::executablePath(), QStringLiteral("hg"), std::move(environment), TextEncoding::Local };
}

} // namespace

namespace Hg {

QString executablePath()
{
	QString executable = CSettings{}.value(Settings::HgExecutableKey).toString();
	if (executable.isEmpty())
		executable = QLatin1String(Settings::HgExecutableDefault);
	return executable;
}

QStringList invariantArgs()
{
	// diff.nobinary: a binary file's diff is a one-line note (as with git) rather than the base85 patch --git
	// mode would otherwise put into the diff pane and every line count
	return { QStringLiteral("--config"), QStringLiteral("ui.interactive=False"),
		QStringLiteral("--config"), QStringLiteral("diff.nobinary=True") };
}

Vcs::Job* run(const QString& workDir, QStringList args, const QObject* context, Vcs::Callback callback,
	QByteArray stdinData, Transport transport)
{
	args = invariantArgs() + args; // global options come before the subcommand
	if (transport == Transport::Server)
		return HgServerPool::instance().run(hgTool(), workDir, std::move(args), context, std::move(callback), std::move(stdinData));
	return Vcs::run(hgTool(), workDir, std::move(args), context, std::move(callback), std::move(stdinData));
}

ProcessResult runSync(const QString& workDir, QStringList args, int timeoutMs)
{
	args = invariantArgs() + args;
	return Vcs::runSync(hgTool(), workDir, std::move(args), timeoutMs);
}

QByteArray localBytes(const QString& text)
{
	QByteArray bytes;
	qsizetype runStart = 0; // the text between lone surrogates, encoded as one run
	for (qsizetype i = 0; i < text.size(); ++i)
	{
		const QChar c = text.at(i);
		if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate())
		{
			++i; // a valid pair; its low half is not a lone surrogate
			continue;
		}
		const char16_t unit = c.unicode();
		if (unit < 0xDC80 || unit > 0xDCFF)
			continue;
		bytes += QStringView{ text }.mid(runStart, i - runStart).toLocal8Bit();
		bytes += char(unit & 0xFF);
		runStart = i + 1;
	}
	if (runStart == 0)
		return text.toLocal8Bit();
	bytes += QStringView{ text }.mid(runStart).toLocal8Bit();
	return bytes;
}

} // namespace Hg
