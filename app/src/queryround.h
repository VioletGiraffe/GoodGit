#pragma once

#include "vcsprocess.h"

#include <functional>
#include <memory>

// A set of queries launched together, and what happens once the last of them has answered. Every query
// holds a reference to the round's end, and the launching scope holds one of its own - so a round that
// launched nothing ends as that scope does, and one that launched anything ends from the final callback.
// A skipped callback is still a destroyed one, so `then` also runs when the context dies mid-round and
// has to check for that.
//
// Running a query is the backend's - it is what knows the executable, the invariants and the context to
// scope the job to. Everything here is the counting.
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
				end.reset(); // a delivered result is done with the round here, not at the job's later deletion
			});
	}

private:
	const Launcher _launcher;
	const std::shared_ptr<End> _end;
};
