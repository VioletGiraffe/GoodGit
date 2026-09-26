#pragma once

// Helpers the repository backend tests share: scratch files and processes, and waiting on asynchronous answers

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "repository.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <expected>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

// How long any wait on a process or an answer lasts before the test fails
inline constexpr int TestTimeoutMs = 60'000;

// Points every later git, the backend's included, at an empty global config and away from the system one
inline void useEmptyGitConfig(const QString& configPath)
{
	REQUIRE(QFile{ configPath }.open(QIODevice::WriteOnly));
	qputenv("GIT_CONFIG_GLOBAL", configPath.toUtf8());
	qputenv("GIT_CONFIG_NOSYSTEM", "1");
}

// Creates the directories on the way
inline void writeFile(const QString& root, const QString& relativePath, const QByteArray& content)
{
	const QString path = QDir{ root }.filePath(relativePath);
	REQUIRE(QDir{}.mkpath(QFileInfo{ path }.absolutePath()));
	QFile file{ path };
	REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
	file.write(content);
}

// The exit code, -1 where the process crashed, and its stdout and stderr merged
[[nodiscard]] inline std::pair<int, QString> runProcess(const QString& program, const QStringList& arguments, const QString& workDir)
{
	QProcess process;
	process.setWorkingDirectory(workDir);
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start(program, arguments);
	REQUIRE(process.waitForFinished(TestTimeoutMs));
	return { process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1, QString::fromUtf8(process.readAll()) };
}

// Runs one asynchronous operation to its answer: `start` hands the operation the callback
template <typename T>
[[nodiscard]] std::expected<T, QString> awaitAnswer(const std::function<void(Vcs::Answer<T>)>& start)
{
	QEventLoop loop;
	QTimer::singleShot(TestTimeoutMs, &loop, &QEventLoop::quit);
	std::optional<std::expected<T, QString>> answer;
	start([&](std::expected<T, QString> result) {
		answer = std::move(result);
		loop.quit();
	});
	loop.exec();

	REQUIRE(answer.has_value()); // the timer fired instead of the operation answering
	return *std::move(answer);
}

// The answer's value; requires the operation to have succeeded
template <typename T>
T requireSuccess(std::expected<T, QString> answer)
{
	INFO((answer ? QString{} : answer.error()).toStdString());
	REQUIRE(answer.has_value());
	if constexpr (!std::is_void_v<T>)
		return *std::move(answer);
}

// Requires the refresh to have read the state
inline void refreshToCompletion(Repository& repository)
{
	QEventLoop loop;
	QTimer::singleShot(TestTimeoutMs, &loop, &QEventLoop::quit);
	repository.refresh([&loop] { loop.quit(); });
	loop.exec();

	REQUIRE_FALSE(repository.refreshing());
	INFO(repository.state().readFailure.toStdString());
	REQUIRE(repository.state().known());
}

// The rows sorted by path
[[nodiscard]] inline std::pair<std::vector<FileEntry>, RepoState> refreshedRowsAndState(Repository& repository)
{
	refreshToCompletion(repository);
	std::vector<FileEntry> files = repository.files();
	std::ranges::sort(files, {}, &FileEntry::path);
	return { std::move(files), repository.state() };
}

[[nodiscard]] inline QStringList pathsOf(const std::vector<FileEntry>& files)
{
	QStringList paths;
	for (const FileEntry& file : files)
		paths.push_back(file.path);
	return paths;
}
