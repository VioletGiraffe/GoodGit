#include "init_logging.h"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "logger/cloggerinmemory.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QtLogging>
RESTORE_COMPILER_WARNINGS

namespace {

QtMessageHandler previousMessageHandler = nullptr;

// Runs on whichever thread logged
void logMessageAndForward(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	applicationLog().log(qFormatLogMessage(type, context, message));
	previousMessageHandler(type, context, message); // Qt's default: the debugger output on Windows, stderr elsewhere
}

} // namespace

ScopedApplicationLog::ScopedApplicationLog()
{
	qSetMessagePattern(QStringLiteral("%{time hh:mm:ss.zzz} [%{type}] %{if-category}%{category}: %{endif}%{message}"));
	previousMessageHandler = qInstallMessageHandler(&logMessageAndForward); // never null: Qt returns its default handler

	// Stays set: it only reaches the log through the installed message handler
	AdvancedAssert::setLoggingFunc([](const char* message) { qCritical("%s", message); });
}

ScopedApplicationLog::~ScopedApplicationLog()
{
	qInstallMessageHandler(previousMessageHandler);
}

CLoggerInterface& applicationLog()
{
	return loggerInstance<CLoggerInMemory>();
}
