#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "vcstypes.h"

namespace {

QString bodyOf(const QString& message)
{
	return CommitRecord{ .message = message }.body();
}

} // namespace

TEST_CASE("A message that is only a subject has no body", "[vcstypes]")
{
	CHECK(bodyOf(QStringLiteral("Subject")).isEmpty());
	CHECK(bodyOf(QStringLiteral("Subject\n")).isEmpty());
	CHECK(bodyOf(QStringLiteral("Subject\n\n  \n\t\n")).isEmpty());
}

TEST_CASE("The body drops the blank lines around it and keeps its own layout", "[vcstypes]")
{
	CHECK(bodyOf(QStringLiteral("Subject\n\nFirst\n\nSecond\n")) == QStringLiteral("First\n\nSecond"));
	CHECK(bodyOf(QStringLiteral("Subject\n\n\n    indented\nplain  \n\n")) == QStringLiteral("    indented\nplain"));
	CHECK(bodyOf(QStringLiteral("Subject\r\n\r\nBody\r\n")) == QStringLiteral("Body"));
}
