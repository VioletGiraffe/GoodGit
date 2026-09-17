#pragma once

class CLoggerInterface;

// While alive, routes Qt messages and failed assertions into applicationLog(). Messages logged outside its lifetime are not kept.
// Must be destroyed before static destruction: the message handler must not outlive the log.
class ScopedApplicationLog
{
public:
	ScopedApplicationLog();
	~ScopedApplicationLog();

	ScopedApplicationLog(const ScopedApplicationLog&) = delete;
	ScopedApplicationLog& operator=(const ScopedApplicationLog&) = delete;
};

// In memory only, the most recent lines: what Help > Report a Bug shows
[[nodiscard]] CLoggerInterface& applicationLog();
