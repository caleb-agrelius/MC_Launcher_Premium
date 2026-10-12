#pragma once

#include "UI/BackgroundTasks.h"

#include <memory>
#include <utility>

// Runs work() on a worker, then done(result) on the UI thread. Use it instead of hand-writing
// the run/post pair and the shared result that goes between them. done is dropped silently if
// the app shuts down first, so guard it against stale state (a generation number) the usual way.
template <class Work, class Done>
void RunInBackground(const BackgroundTasks& tasks, Work work, Done done)
{
	if (!tasks.run || !tasks.post)
		return;
	tasks.run([post = tasks.post, work = std::move(work), done = std::move(done)]() mutable {
		auto result = std::make_shared<decltype(work())>(work());
		post([done = std::move(done), result]() mutable { done(std::move(*result)); });
	});
}
