#pragma once

#include "vcsprocess.h"

#include <functional>
#include <memory>

// A set of queries launched together; `then` runs once the last has answered.
// Every query and the launching scope hold a reference to the round's end: a round that launched nothing
// ends with the scope, one that launched anything ends from the final callback.
// `then` also runs when the context dies mid-round (a skipped callback is still destroyed), and must check for that.
// The backend supplies the launcher: it knows the executable, the invariants and the context.
class QueryRound
{
	struct End
	{
		explicit End(std::function<void()> then) : then(std::move(then)) {}
		~End() { then(); }

		std::function<void()> then;
	};

public:
	using Launcher = std::function<void(const QString& workDir, QStringList args, Vcs::Callback onResult)>;

	QueryRound(Launcher launcher, std::function<void()> then) :
		_launcher(std::move(launcher)),
		_end(std::make_shared<End>(std::move(then)))
	{}

	void launch(const QString& workDir, QStringList args, Vcs::Callback onResult)
	{
		_launcher(workDir, std::move(args),
			[end = _end, onResult = std::move(onResult)](const ProcessResult& result) mutable {
				onResult(result);
				end.reset(); // now, not at the job's later deletion
			});
	}

private:
	const Launcher _launcher;
	const std::shared_ptr<End> _end;
};
